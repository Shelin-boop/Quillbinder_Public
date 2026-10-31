// Headless checks for the text formats, compile and ZIP code.
#include "compile/Compiler.h"
#include "compile/DocxWriter.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/Replace.h"
#include "core/Search.h"
#include "text/FormatFind.h"
#include "core/ProjectTemplates.h"
#include "core/Snapshots.h"
#include "core/Addons.h"
#include "core/Zip.h"
#include "compile/CompileFormat.h"
#include "AppInfo.h"
#include "import/ScrivenerImporter.h"
#include "text/DirectFormat.h"
#include "text/DisplayFormat.h"
#include "text/MarkdownIO.h"
#include "text/RtfIO.h"
#include "text/Styles.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextCursor>
#include <cstdio>
#include <functional>

using namespace qb;
int scriptChecks(); // script_checks.cpp
int scrivenerChecks(); // scrivener_checks.cpp
int compileChecks(); // compile_checks.cpp
static int failures = 0;
static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail));
    }
}

static QString mdRoundTrip(const QString &in)
{
    Formatting f;
    QTextDocument d;
    md::read(in, &d, f);
    return md::write(&d);
}

// Builds a package in memory the way tools/make-package.py does.
static QByteArray makePackage(QJsonObject manifest, const QList<QPair<QString, QByteArray>> &payload,
                              const QList<QPair<QString, QByteArray>> &packages = {}, bool breakChecksum = false)
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("p.qbpkg"));
    {
        QJsonArray files;
        for (const auto &f : payload) {
            QJsonObject o;
            o.insert(QStringLiteral("path"), f.first);
            o.insert(QStringLiteral("size"), f.second.size());
            QByteArray sum = QCryptographicHash::hash(f.second, QCryptographicHash::Sha256).toHex();
            if (breakChecksum)
                sum[0] = sum[0] == 'a' ? 'b' : 'a';
            o.insert(QStringLiteral("sha256"), QString::fromLatin1(sum));
            if (f.first == QLatin1String("quillbinder"))
                o.insert(QStringLiteral("executable"), true);
            files.append(o);
        }
        manifest.insert(QStringLiteral("format"), 1);
        if (!files.isEmpty())
            manifest.insert(QStringLiteral("files"), files);
        QJsonArray names;
        for (const auto &pk : packages)
            names.append(pk.first);
        if (!names.isEmpty())
            manifest.insert(QStringLiteral("packages"), names);
        ZipWriter zw(path);
        zw.addFile(QStringLiteral("manifest.json"), QJsonDocument(manifest).toJson());
        for (const auto &f : payload)
            zw.addFile(QLatin1String("payload/") + f.first, f.second);
        for (const auto &pk : packages)
            zw.addFile(QLatin1String("packages/") + pk.first, pk.second, false);
        zw.close();
    }
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

static QJsonObject manifestOf(const char *id, const char *kind, const char *version, const char *name)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), QLatin1String(id));
    o.insert(QStringLiteral("kind"), QLatin1String(kind));
    o.insert(QStringLiteral("version"), QLatin1String(version));
    o.insert(QStringLiteral("name"), QLatin1String(name));
    return o;
}

int main(int argc, char **argv)
{
    // keep the user's real data folder out of the tests
    QTemporaryDir dataHome;
    qputenv("XDG_DATA_HOME", dataHome.path().toUtf8());
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("QuillbinderTest"));

    // ---------------------------------------------------------------- markdown
    const QStringList samples = {
        QStringLiteral("Plain paragraph.\n"),
        QStringLiteral("Some **bold** and *italic* and ***both*** text.\n"),
        QStringLiteral("Nested **bold *and italic* here** end.\n"),
        QStringLiteral("~~strike~~ ==mark== <u>under</u> H<sub>2</sub>O x<sup>2</sup>\n"),
        QStringLiteral("# Heading One\n## Heading Two\nBody text.\n"),
        QStringLiteral("> A quote\n->Centered<-\n::Title:: The Title\n"),
        QStringLiteral("- one\n- two\n  - nested\n1. first\n2. second\n"),
        QStringLiteral("A link to [the site](https://example.com) and [a doc](qb://ABC-123).\n"),
        QStringLiteral("Annotation {>>check this<<} and a footnote^[Source: me.] here.\n"),
        QStringLiteral("A {==linked comment==}{>>Needs work\\nsecond line<<} and {==linked note==}^[The footnote.]\n"),
        QStringLiteral("Colored {>>@#0000ff blue note<<}.\n"),
        QStringLiteral("Escapes: 5 \\* 3, snake\\_case, \\# not heading, \\[brackets\\](x)\n"),
        QStringLiteral("\\# Not a heading\n\\- not a list\n1\\. not numbered\n"),
        QStringLiteral("* * *\n#\n\nAfter empty line.\n"),
        QStringLiteral("Line<br>break inside paragraph.\n"),
        QStringLiteral("Tab\there.\n"),
        QStringLiteral("Revised <rev2>new **words**</rev2> here.\n"),
    };
    for (const QString &s0 : samples) {
        // the writer may normalise escapes; after one pass it must be stable
        const QString s = s0.startsWith(QLatin1String("Escapes")) ? mdRoundTrip(s0) : s0;
        const QString out = mdRoundTrip(s);
        check(out == s, QStringLiteral("md round trip: ") + s.left(40).replace(QLatin1Char('\n'), QLatin1String("|")),
              QStringLiteral("got: ") + QString(out).replace(QLatin1Char('\n'), QLatin1String("|")));
    }
    // semantic check
    {
        Formatting f;
        QTextDocument d;
        md::read(QStringLiteral("A {==anchor==}{>>the comment<<} end"), &d, f);
        bool found = false;
        for (auto it = d.begin().begin(); !it.atEnd(); ++it) {
            const QTextCharFormat cf = it.fragment().charFormat();
            if (NoteKind(cf.intProperty(props::NoteKind)) == NoteKind::Comment && it.fragment().text() == QLatin1String("anchor")
                && cf.stringProperty(props::NoteText) == QLatin1String("the comment"))
                found = true;
        }
        check(found, QStringLiteral("md linked comment parsed"));
        check(text::plainText(&d) == QLatin1String("A anchor end"), QStringLiteral("plain text"), text::plainText(&d));
    }
    // word count
    {
        const auto c = text::count(QStringLiteral("Hello, world! It's a test - really.\nSecond line"));
        check(c.words == 8, QStringLiteral("word count"), QString::number(c.words));
    }
    // standard markdown conversion
    {
        const QString std = QStringLiteral("First line\nwraps here.\n\nSecond paragraph.\n\n- a\n- b\n");
        const QString d = md::fromStandardMarkdown(std);
        check(d == QLatin1String("First line wraps here.\nSecond paragraph.\n- a\n- b\n"), QStringLiteral("from standard md"), d);
    }

    // ---------------------------------------------------------------- RTF
    {
        Formatting f;
        QTextDocument d;
        md::read(samples.join(QString()), &d, f);
        const QByteArray r = rtf::write(&d, f);
        QTextDocument back;
        rtf::ReadOptions o;
        o.keepFormatting = false;
        rtf::read(r, &back, f, o);
        const QString a = md::write(&d), b = md::write(&back);
        check(a == b, QStringLiteral("rtf round trip (semantic)"), QStringLiteral("\nA=") + a + QStringLiteral("\nB=") + b);
    }
    {
        // foreign RTF (Word/Scrivener style)
        const QByteArray rtfIn =
            "{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0{\\fonttbl{\\f0\\froman\\fcharset0 Times New Roman;}{\\f1\\fswiss Arial;}}"
            "{\\colortbl;\\red0\\green0\\blue0;\\red255\\green255\\blue0;}"
            "{\\stylesheet{\\s0 Normal;}{\\s1\\b\\fs36 Heading 1;}}"
            "\\pard\\plain\\s1\\qc\\f0\\fs36\\b Chapter One\\par"
            "\\pard\\plain\\f0\\fs24 It was a \\i dark\\i0  and \\b stormy\\b0  night \\u8212?caf\\'e9 {\\field{\\*\\fldinst{HYPERLINK \"scrivcmt://C1\"}}{\\fldrslt anchor}} \\ldblquote q\\rdblquote .\\par"
            "\\pard\\plain Second{\\footnote\\pard\\plain {\\super\\chftn} The note.} paragraph with \\highlight2 marked\\highlight0  text.\\par}";
        Formatting f;
        QTextDocument d;
        rtf::ReadOptions o;
        o.keepFormatting = false;
        const auto res = rtf::read(rtfIn, &d, f, o);
        const QString m = md::write(&d);
        check(res.ok, QStringLiteral("foreign rtf parsed"));
        check(m.startsWith(QString::fromUtf8("# Chapter One\nIt was a *dark* and **stormy** night —café {==anchor==}{>><<} “q”.\nSecond^[The note.] paragraph with ==marked== text.")),
              QStringLiteral("foreign rtf content"), m);
        check(res.linkedNoteIds.contains(QStringLiteral("C1")), QStringLiteral("scrivener comment link found"));
    }

    // ---------------------------------------------------------------- project + compile
    QTemporaryDir tmp;
    {
        QString err;
        templates::Info info;
        for (const auto &i : templates::all())
            if (i.id == QLatin1String("novel"))
                info = i;
        auto p = templates::createProject(info, tmp.path(), QStringLiteral("Test Novel"), &err);
        check(p != nullptr, QStringLiteral("create project from novel template"), err);
        if (p) {
            BinderItem *chapter = p->draft()->children.value(0);
            BinderItem *scene = chapter ? chapter->children.value(0) : nullptr;
            check(scene != nullptr, QStringLiteral("template has chapter/scene"));
            if (scene) {
                templates::setMarkdown(p.get(), scene,
                                       QStringLiteral("It was *dark*.^[A footnote.]\nSecond {==para==}{>>A comment<<}.\n* * *\nThird <$n:x>."));
                BinderItem *s2 = p->model()->createItem(chapter, -1, ItemType::Text, QStringLiteral("Scene 2"));
                templates::setMarkdown(p.get(), s2, QStringLiteral("Another scene with an ![](missing.png) image."));
                BinderItem *ch2 = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Chapter"));
                BinderItem *s3 = p->model()->createItem(ch2, -1, ItemType::Text, QStringLiteral("Scene 3"));
                templates::setMarkdown(p.get(), s3, QStringLiteral("Final scene."));
            }
            check(p->save(&err), QStringLiteral("save project"), err);
            check(p->draftWordCount() > 5, QStringLiteral("draft word count"), QString::number(p->draftWordCount()));

            // snapshots
            check(snapshots::take(p.get(), scene->id, QStringLiteral("First"), &err), QStringLiteral("take snapshot"), err);
            templates::setMarkdown(p.get(), scene, QStringLiteral("Changed text."));
            const auto snaps = snapshots::list(p.get(), scene->id);
            check(snaps.size() == 1, QStringLiteral("list snapshots"));
            if (!snaps.isEmpty()) {
                QTextDocument sd;
                snapshots::load(p.get(), snaps.first(), &sd);
                const auto ops = diffTexts(text::plainText(&sd), text::plainText(p->docs()->text(scene->id)));
                check(ops.size() >= 2, QStringLiteral("diff produced"), QString::number(ops.size()));
                snapshots::rollBack(p.get(), snaps.first());
                check(text::plainText(p->docs()->text(scene->id)).startsWith(QLatin1String("It was dark")),
                      QStringLiteral("roll back"), text::plainText(p->docs()->text(scene->id)));
            }

            // reopen
            const QString folder = p->folderPath();
            p->save(&err);
            p.reset();
            auto q = Project::open(folder, &err);
            check(q != nullptr, QStringLiteral("reopen project"), err);
            if (q) {
                check(q->draft()->children.size() == 2, QStringLiteral("binder restored"));
                compile::AuthorInfo author{QStringLiteral("Jane"), QStringLiteral("Doe")};
                for (const auto &fmt : compile::builtinFormats()) {
                    compile::Compiler c(q.get(), fmt, q->compile, author);
                    const compile::Manuscript m = c.build();
                    compile::DocxExporter docx;
                    const QString out = QDir(tmp.path()).filePath(fmt.name + QStringLiteral(".docx"));
                    const bool ok = docx.exportManuscript(m, out, &err);
                    check(ok && ZipReader(out).fileNames().contains(QStringLiteral("word/document.xml")),
                          QStringLiteral("compile + docx: ") + fmt.name, err);
                    if (fmt.name == QLatin1String("Manuscript (Times)")) {
                        QString all;
                        for (const auto &para : m.paragraphs)
                            all += para.plainText() + QLatin1Char('|');
                        check(all.contains(QLatin1String("Chapter One")) && all.contains(QLatin1String("Chapter Two")),
                              QStringLiteral("chapter numbering"), all);
                        check(m.footnotes.size() == 1 && m.comments.size() == 1, QStringLiteral("footnote + comment compiled"),
                              QStringLiteral("%1 %2").arg(m.footnotes.size()).arg(m.comments.size()));
                        check(all.contains(QLatin1String("Third 1.")), QStringLiteral("placeholder in text"), all);
                        std::printf("      %s\n", qPrintable(all.left(400)));
                        QFile::copy(out, QStringLiteral("/tmp/claude-0/test-manuscript.docx"));
                    }
                }
                // storage conversion
                check(q->docs()->convertStorage(StorageFormat::Rtf, &err), QStringLiteral("convert storage to RTF"), err);
                check(QFile::exists(q->contentDir(q->draft()->children[0]->children[0]->id) + QStringLiteral("/text.rtf")),
                      QStringLiteral("rtf file written"));
                check(q->docs()->convertStorage(StorageFormat::Markdown, &err), QStringLiteral("convert back to Markdown"), err);
                const QString back = QString::fromUtf8([&] {
                    QFile f(q->contentDir(q->draft()->children[0]->children[0]->id) + QStringLiteral("/text.md"));
                    f.open(QIODevice::ReadOnly);
                    return f.readAll();
                }());
                check(back.startsWith(QLatin1String("It was *dark*.^[A footnote.]")), QStringLiteral("md after rtf round trip"), back);
            }
        }
    }
    // zip
    {
        const QString z = QDir(tmp.path()).filePath(QStringLiteral("t.zip"));
        {
            ZipWriter w(z);
            w.addFile(QStringLiteral("a.txt"), QByteArray(5000, 'x'));
            w.addFile(QStringLiteral("dir/b.txt"), "hello");
            w.close();
        }
        ZipReader r(z);
        check(r.fileData(QStringLiteral("a.txt")) == QByteArray(5000, 'x') && r.fileData(QStringLiteral("dir/b.txt")) == "hello",
              QStringLiteral("zip round trip"));
    }

    // ---------------------------------------------------------------- Scrivener import (synthetic v3 project)
    {
        // Built from the structure the importer expects. The real format is
        // not verified against Scrivener here; this checks the code path.
        QTemporaryDir tmp;
        const QString scriv = tmp.filePath(QStringLiteral("Sample.scriv"));
        const QString a = QStringLiteral("AAAAAAAA-0000-0000-0000-000000000001");
        const QString b = QStringLiteral("BBBBBBBB-0000-0000-0000-000000000002");
        const QString d = QStringLiteral("DDDDDDDD-0000-0000-0000-000000000004");
        const QString r = QStringLiteral("RRRRRRRR-0000-0000-0000-000000000005");
        const QString t = QStringLiteral("TTTTTTTT-0000-0000-0000-000000000006");
        const QString img = QStringLiteral("IIIIIIII-0000-0000-0000-000000000007");
        auto write = [](const QString &path, const QByteArray &data) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            f.open(QIODevice::WriteOnly);
            f.write(data);
        };
        const QString scrivx = QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<ScrivenerProject Template=\"NO\" Version=\"2.0\" Creator=\"SCRWIN-3.1.5.1\">\n"
            " <Binder>\n"
            "  <BinderItem UUID=\"%1\" Type=\"DraftFolder\" Created=\"2024-03-01 10:00:00 +0100\"><Title>Manuscript</Title>\n"
            "   <Children>\n"
            "    <BinderItem UUID=\"%2\" Type=\"Folder\"><Title>Chapter 1</Title><MetaData><SectionType>ST1</SectionType></MetaData>\n"
            "     <Children>\n"
            "      <BinderItem UUID=\"%3\" Type=\"Text\" Created=\"2024-03-02 11:00:00 +0100\"><Title>Opening</Title>\n"
            "       <MetaData><LabelID>1</LabelID><StatusID>2</StatusID><IncludeInCompile>Yes</IncludeInCompile>\n"
            "        <CustomMetaData><MetaDataItem><FieldID>pov</FieldID><Value>Tomas</Value></MetaDataItem></CustomMetaData></MetaData>\n"
            "       <Keywords><KeywordID>0</KeywordID></Keywords></BinderItem>\n"
            "     </Children></BinderItem>\n"
            "   </Children></BinderItem>\n"
            "  <BinderItem UUID=\"%4\" Type=\"ResearchFolder\"><Title>Research</Title><Children>\n"
            "    <BinderItem UUID=\"%6\" Type=\"Image\"><Title>Map</Title><MetaData><FileExtension>png</FileExtension></MetaData></BinderItem>\n"
            "  </Children></BinderItem>\n"
            "  <BinderItem UUID=\"%5\" Type=\"TrashFolder\"><Title>Trash</Title></BinderItem>\n"
            " </Binder>\n"
            " <LabelSettings><Title>POV</Title><Labels><Label ID=\"-1\">No Label</Label><Label ID=\"1\" Color=\"0.9 0.2 0.1\">Tomas</Label></Labels></LabelSettings>\n"
            " <StatusSettings><StatusItems><Status ID=\"-1\">No Status</Status><Status ID=\"2\">First Draft</Status></StatusItems></StatusSettings>\n"
            " <Keywords><Keyword ID=\"0\"><Title>Lighthouse</Title><Color>0.2 0.4 0.8</Color></Keyword></Keywords>\n"
            " <CustomMetaDataSettings><MetaDataField ID=\"pov\" Type=\"Text\"><Title>Viewpoint</Title></MetaDataField></CustomMetaDataSettings>\n"
            " <SectionTypes><SectionType ID=\"ST1\">Chapter</SectionType></SectionTypes>\n"
            " <ProjectTargets><DraftTarget Type=\"Words\">80000</DraftTarget><SessionTarget Type=\"Words\">1000</SessionTarget></ProjectTargets>\n"
            "</ScrivenerProject>\n")
            .arg(a, b, d, r, t, img);
        write(scriv + QLatin1String("/Sample.scrivx"), scrivx.toUtf8());
        const QString data = scriv + QLatin1String("/Files/Data/");
        write(data + d + QLatin1String("/content.rtf"),
              "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Times New Roman;}}\\pard\\f0\\fs24 The keeper {\\field{\\*\\fldinst{HYPERLINK "
              "\"scrivcmt://C1\"}}{\\fldrslt counted}} the ships.\\par Second paragraph.\\par}");
        write(data + d + QLatin1String("/content.comments"),
              "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Comments><Comment ID=\"C1\" Color=\"1.0 0.9 0.5\">Too plain?</Comment></Comments>");
        write(data + d + QLatin1String("/synopsis.txt"), "The keeper counts ships.");
        write(data + d + QLatin1String("/notes.rtf"), "{\\rtf1\\ansi Check the tide tables.\\par}");
        QImage map(40, 30, QImage::Format_RGB32);
        map.fill(Qt::darkCyan);
        QDir().mkpath(data + img);
        map.save(data + img + QLatin1String("/content.png"));
        write(scriv + QLatin1String("/Snapshots/") + d + QLatin1String(".snapshots/2024-03-05-10-00-00+0100.rtf"),
              "{\\rtf1\\ansi An older version.\\par}");

        ScrivenerImportReport rep;
        QString ierr;
        auto proj = ScrivenerImporter::importProject(scriv, tmp.path(), QStringLiteral("Imported"), StorageFormat::Markdown,
                                                     &rep, &ierr);
        check(proj != nullptr, QStringLiteral("scrivener import runs"), ierr);
        if (proj) {
            BinderItem *open = proj->item(d);
            check(open && open->title == QLatin1String("Opening") && open->parent && open->parent->title == QLatin1String("Chapter 1"),
                  QStringLiteral("scrivener binder structure"));
            check(proj->draft()->title == QLatin1String("Manuscript"), QStringLiteral("scrivener draft title"));
            if (open) {
                check(open->synopsis == QLatin1String("The keeper counts ships."), QStringLiteral("scrivener synopsis"));
                check(open->label == 1 && open->status == 2, QStringLiteral("scrivener label and status"));
                check(!open->keywords.isEmpty(), QStringLiteral("scrivener keywords"));
                check(!open->custom.isEmpty() && open->custom.first() == QLatin1String("Tomas"), QStringLiteral("scrivener custom metadata"));
                const QString mdText = md::write(proj->docs()->text(open->id));
                check(mdText.contains(QLatin1String("{==counted==}{>>")) && mdText.contains(QLatin1String("Too plain?")),
                      QStringLiteral("scrivener linked comment"), mdText);
                check(proj->docs()->notesPlainText(open->id).contains(QLatin1String("tide tables")), QStringLiteral("scrivener notes"));
                check(open->hasSnapshots, QStringLiteral("scrivener snapshot"));
            }
            check(rep.mediaFiles == 1 && proj->item(img) && QFile::exists(proj->mediaFilePath(proj->item(img))),
                  QStringLiteral("scrivener research image"));
            check(proj->targets.draftTarget == 80000, QStringLiteral("scrivener targets"));
            check(proj->save(&ierr), QStringLiteral("imported project saves"), ierr);
            std::printf("      report: %d texts, %d media, %d snapshots, %d comments; warnings: %s\n", rep.textDocuments,
                        rep.mediaFiles, rep.snapshots, rep.comments, qPrintable(rep.warnings.join(QLatin1String("; "))));
        }
    }

    // ---------------------------------------------------------------- packages and add-ons
    {
        using namespace addons;
        check(compareVersions(QStringLiteral("0.10.0"), QStringLiteral("0.9.3")) > 0
                  && compareVersions(QStringLiteral("1.2"), QStringLiteral("1.2.0")) == 0,
              QStringLiteral("version comparison"));
        const QByteArray theme = "{\"name\":\"Test Theme\",\"colors\":{\"window\":\"#202020\",\"text\":\"#f0f0f0\"}}";
        QJsonObject am = manifestOf("test-themes", "addon", "1.0", "Test themes");
        const QByteArray addonPkg = makePackage(am, {{QStringLiteral("themes/test.json"), theme}});
        PackageInfo pi = inspect(addonPkg);
        check(pi.isValid() && pi.provides.size() == 1, QStringLiteral("package: valid add-on is accepted"), pi.error);
        check(!inspect(makePackage(am, {{QStringLiteral("themes/test.json"), theme}}, {}, true)).isValid(),
              QStringLiteral("package: wrong checksum is rejected"));
        check(!inspect(makePackage(am, {{QStringLiteral("../evil.json"), theme}})).isValid(),
              QStringLiteral("package: path outside the add-on is rejected"));
        check(!inspect(QByteArray("this is not a zip file")).isValid(), QStringLiteral("package: non-package is rejected"));
        QJsonObject future = am;
        future.insert(QStringLiteral("minApp"), QStringLiteral("99.0"));
        check(!inspect(makePackage(future, {{QStringLiteral("themes/test.json"), theme}})).isValid(),
              QStringLiteral("package: add-on for a newer program is rejected"));

        // install from a file
        QTemporaryDir tmp;
        auto writeFile = [&](const QString &name, const QByteArray &data) {
            QFile f(tmp.filePath(name));
            f.open(QIODevice::WriteOnly);
            f.write(data);
            return tmp.filePath(name);
        };
        QStringList log;
        QString perr;
        bool restart = true;
        check(install(writeFile(QStringLiteral("a.qbpkg"), addonPkg), &log, &perr, &restart) && !restart,
              QStringLiteral("add-on installs without restart"), perr);
        check(installedAddons().size() == 1 && contentDirs(QStringLiteral("themes")).size() == 1,
              QStringLiteral("add-on is listed and provides its content"));
        setEnabled(QStringLiteral("test-themes"), false);
        check(contentDirs(QStringLiteral("themes")).isEmpty() && !installedAddons().first().enabled,
              QStringLiteral("add-on can be switched off"));
        setEnabled(QStringLiteral("test-themes"), true);
        check(contentDirs(QStringLiteral("themes")).size() == 1, QStringLiteral("add-on can be switched on again"));

        // program updates
        const QByteArray binary = "#!/bin/sh\necho fake program\n";
        QJsonObject cm = manifestOf("core", "core", "99.1.0", "Program update");
        cm.insert(QStringLiteral("binary"), QStringLiteral("quillbinder"));
        cm.insert(QStringLiteral("runtime"), QStringLiteral("some-other-runtime"));
        check(!inspect(makePackage(cm, {{QStringLiteral("quillbinder"), binary}})).isValid(),
              QStringLiteral("program update for another runtime is rejected"));
        cm.insert(QStringLiteral("runtime"), QStringLiteral(QB_RUNTIME_ID));
        QJsonObject old = cm;
        old.insert(QStringLiteral("version"), QStringLiteral("0.0.1"));
        check(!inspect(makePackage(old, {{QStringLiteral("quillbinder"), binary}})).isValid(),
              QStringLiteral("program update older than the AppImage is rejected"));
        const QByteArray corePkg = makePackage(cm, {{QStringLiteral("quillbinder"), binary}});
        check(inspect(corePkg).isValid(), QStringLiteral("program update is accepted"), inspect(corePkg).error);

        // bundle: add-on that needs the new program version + the program update
        QJsonObject needsNew = manifestOf("test-format", "addon", "2.0", "Needs new version");
        needsNew.insert(QStringLiteral("minApp"), QStringLiteral("99.1.0"));
        const QByteArray needsPkg = makePackage(needsNew, {{QStringLiteral("compile-formats/x.json"), "{\"name\":\"X Format\"}"}});
        check(!inspect(needsPkg).isValid(), QStringLiteral("add-on alone needs the newer program"));
        const QByteArray bundle = makePackage(manifestOf("update-99", "bundle", "99.1", "Update 99.1"), {},
                                              {{QStringLiteral("core.qbpkg"), corePkg}, {QStringLiteral("fmt.qbpkg"), needsPkg}});
        const PackageInfo bi = inspect(bundle);
        check(bi.isValid() && bi.nested.size() == 2, QStringLiteral("bundle with program update and add-on is accepted"), bi.error);
        log.clear();
        restart = false;
        check(install(writeFile(QStringLiteral("b.qbpkg"), bundle), &log, &perr, &restart) && restart && log.size() == 2,
              QStringLiteral("bundle installs both parts and asks for a restart"), perr + log.join(QLatin1Char('|')));
        check(installedAddons().size() == 2, QStringLiteral("bundle add-on is installed"));
        bool addonFormatFound = false;
        for (const compile::Format &f : compile::allFormats())
            addonFormatFound = addonFormatFound || (f.name == QLatin1String("X Format") && f.builtin);
        check(addonFormatFound, QStringLiteral("compile format from an add-on is available (read-only)"));
        QList<CoreVersion> cvs = coreVersions();
        check(cvs.size() == 2 && cvs.first().version == QLatin1String("99.1.0") && cvs.first().active && cvs.last().builtIn
                  && cvs.last().running && !cvs.last().active,
              QStringLiteral("installed program version is active for the next start"));
        const QString bin = coreRoot() + QLatin1String("/99.1.0/quillbinder");
        check(QFileInfo(bin).isExecutable(), QStringLiteral("program file is executable"));
        check(setActiveCore(QString(), &perr) && coreVersions().last().active, QStringLiteral("roll back to the built-in version"));
        check(setActiveCore(QStringLiteral("99.1.0"), &perr) && coreVersions().first().active, QStringLiteral("roll forward again"));
        check(!setActiveCore(QStringLiteral("5.5.5"), &perr), QStringLiteral("unknown program version cannot be activated"));
        check(removeCore(QStringLiteral("99.1.0"), &perr) && coreVersions().size() == 1 && coreVersions().first().active,
              QStringLiteral("program version can be removed"), perr);
        check(removeAddon(QStringLiteral("test-themes"), &perr) && removeAddon(QStringLiteral("test-format"), &perr)
                  && installedAddons().isEmpty(),
              QStringLiteral("add-ons can be removed"), perr);
    }

    // ---------------------------------------------------------------- fonts and paragraph formatting (0.3.0)
    {
        Formatting f;
        // characters
        const QString in = QStringLiteral(
            "Plain <span style=\"font-family:'DejaVu Sans'; font-size:16pt\">big **bold** text</span> and "
            "<span style=\"color:#aa0000; letter-spacing:0.05em; font-variant:small-caps\">red caps</span>.\n");
        check(mdRoundTrip(in) == in, QStringLiteral("font spans round trip in Markdown"), mdRoundTrip(in));
        QTextDocument d;
        md::read(in, &d, f);
        QTextCursor c(&d);
        c.setPosition(8);
        direct::CharSpec sp = direct::charSpec(c.charFormat());
        check(sp.family == QLatin1String("DejaVu Sans") && qFuzzyCompare(sp.size, 16.0) && !sp.color.isValid(),
              QStringLiteral("font family and size are read"));
        c.setPosition(11);
        check(c.charFormat().fontWeight() == QFont::Bold && direct::charSpec(c.charFormat()).family == sp.family,
              QStringLiteral("bold inside a font span keeps the font"));
        c.setPosition(d.characterCount() - 4);
        sp = direct::charSpec(c.charFormat());
        check(sp.color == QColor(0xaa, 0, 0) && sp.spacing == 110 && sp.caps == 1 && sp.family.isEmpty(),
              QStringLiteral("colour, letter spacing and small caps are read"),
              QStringLiteral("%1 %2 %3").arg(sp.color.name()).arg(sp.spacing).arg(sp.caps));
        check(text::plainText(&d) == QLatin1String("Plain big bold text and red caps."),
              QStringLiteral("font markers do not appear in the text"), text::plainText(&d));
        // literal text that looks like a marker stays text
        const QString lit = QStringLiteral("Write \\<span style=\"x\">this\\</span> literally.\n");
        check(mdRoundTrip(lit) == lit, QStringLiteral("literal <span> text is escaped"), mdRoundTrip(lit));
        QTextDocument dl;
        md::read(lit, &dl, f);
        check(dl.toPlainText() == QLatin1String("Write <span style=\"x\">this</span> literally."),
              QStringLiteral("escaped <span> is read as text"), dl.toPlainText());
        // nested spans inherit
        QTextDocument dn;
        md::read(QStringLiteral("<span style=\"font-size:20pt\">a<span style=\"color:#0000ff\">b</span>c</span>\n"), &dn, f);
        QTextCursor cn(&dn);
        cn.setPosition(2);
        check(qFuzzyCompare(direct::charSpec(cn.charFormat()).size, 20.0)
                  && direct::charSpec(cn.charFormat()).color == QColor(0, 0, 255),
              QStringLiteral("nested font spans inherit the outer font"));
        // unusual weight
        const QString light = QStringLiteral("<span style=\"font-weight:300\">light</span> text\n");
        check(mdRoundTrip(light) == light, QStringLiteral("font weight round trip"), mdRoundTrip(light));

        // paragraphs
        const QString para = QStringLiteral(
            "{: align=right left=10mm right=5mm first=0mm before=6pt after=12pt line=1.5 tabs=20mm,50mmR,80mmC,100mmD} Right\n"
            "{: line=14pt} exact\n"
            "{: line=14pt+} at least\n"
            "{: left=20mm} # Indented heading\n"
            "{: after=18pt} - list item\n"
            "->centred<-\n"
            "{: first=12.5mm} ->centred with indent<-\n"
            "\\{: left=10mm} literal\n"
            "normal\n");
        check(mdRoundTrip(para) == para, QStringLiteral("paragraph attributes round trip in Markdown"), mdRoundTrip(para));
        QTextDocument dp;
        md::read(para, &dp, f);
        QTextBlockFormat bf = dp.begin().blockFormat();
        check((bf.alignment() & Qt::AlignRight) && qAbs(bf.leftMargin() - direct::mmToPx(10)) < 0.01
                  && qAbs(bf.rightMargin() - direct::mmToPx(5)) < 0.01 && qAbs(bf.textIndent()) < 0.01
                  && qAbs(bf.topMargin() - 8.0) < 0.01 && qAbs(bf.bottomMargin() - 16.0) < 0.01
                  && bf.lineHeightType() == QTextBlockFormat::ProportionalHeight && qAbs(bf.lineHeight() - 150) < 0.01,
              QStringLiteral("paragraph attributes are applied"));
        const auto tabs = bf.tabPositions();
        check(tabs.size() == 4 && tabs[1].type == QTextOption::RightTab && tabs[2].type == QTextOption::CenterTab
                  && tabs[3].type == QTextOption::DelimiterTab && qAbs(tabs[0].position - direct::mmToPx(20)) < 0.01,
              QStringLiteral("tab stops are read"));
        check(bf.intProperty(props::ParaDirect) == direct::AllPara, QStringLiteral("attributes are marked as set by hand"));
        check(dp.findBlockByNumber(1).blockFormat().lineHeightType() == QTextBlockFormat::FixedHeight
                  && dp.findBlockByNumber(2).blockFormat().lineHeightType() == QTextBlockFormat::MinimumHeight,
              QStringLiteral("exact and minimum line height are read"));
        check(dp.findBlockByNumber(3).blockFormat().headingLevel() == 1
                  && qAbs(dp.findBlockByNumber(3).blockFormat().leftMargin() - direct::mmToPx(20)) < 0.01,
              QStringLiteral("attributes combine with a heading"));
        check(dp.findBlockByNumber(4).textList() != nullptr, QStringLiteral("attributes combine with a list"));
        check(dp.findBlockByNumber(7).text() == QLatin1String("{: left=10mm} literal"),
              QStringLiteral("escaped attribute prefix is text"), dp.findBlockByNumber(7).text());
        // changing the project formatting keeps what was set by hand
        Formatting g = f;
        g.firstLineIndent = 50;
        g.lineSpacing = 2.0;
        g.align = Qt::AlignJustify;
        display::normalizeDocument(&dp, g);
        bf = dp.begin().blockFormat();
        check((bf.alignment() & Qt::AlignRight) && qAbs(bf.lineHeight() - 150) < 0.01 && qAbs(bf.textIndent()) < 0.01,
              QStringLiteral("project formatting does not override formatting set by hand"));
        bf = dp.lastBlock().blockFormat();
        check((bf.alignment() & Qt::AlignJustify) && qAbs(bf.lineHeight() - 200) < 0.01 && qAbs(bf.textIndent() - 50) < 0.01,
              QStringLiteral("unformatted paragraphs follow the project formatting"));
        check(md::write(&dp) == para, QStringLiteral("Markdown is unchanged by a new project formatting"), md::write(&dp));
        // files of version 0.2 read as before
        const QString old = QStringLiteral("# Title\n->centre<-\n> quote\nBody **bold** <u>u</u>\n");
        check(mdRoundTrip(old) == old, QStringLiteral("Markdown of version 0.2 is unchanged"), mdRoundTrip(old));
        // remove direct formatting
        QTextDocument dr;
        md::read(in + para, &dr, f);
        direct::removeDirect(&dr, f, true, direct::AllPara);
        const QString cleaned = md::write(&dr);
        check(!cleaned.contains(QLatin1String("<span")) && !cleaned.startsWith(QLatin1String("{: "))
                  && !cleaned.contains(QLatin1String("\n{: ")) && cleaned.contains(QLatin1String("- list item"))
                  && cleaned.contains(QLatin1String("big **bold** text")) && cleaned.contains(QLatin1String("# Indented heading")),
              QStringLiteral("direct formatting can be removed, text and styles stay"), cleaned);

        // RTF
        QTextDocument rd;
        md::read(in + para, &rd, f);
        const QByteArray rtfData = rtf::write(&rd, f);
        QTextDocument rb;
        rtf::read(rtfData, &rb, f);
        QTextCursor rc(&rb);
        rc.setPosition(8);
        sp = direct::charSpec(rc.charFormat());
        check(sp.family == QLatin1String("DejaVu Sans") && qFuzzyCompare(sp.size, 16.0),
              QStringLiteral("RTF keeps font family and size"));
        rc.setPosition(rb.begin().length() - 4);
        sp = direct::charSpec(rc.charFormat());
        check(sp.color == QColor(0xaa, 0, 0) && sp.spacing == 110 && sp.caps == 1,
              QStringLiteral("RTF keeps colour, letter spacing and small caps"),
              QStringLiteral("%1 %2 %3").arg(sp.color.name()).arg(sp.spacing).arg(sp.caps));
        bf = rb.findBlockByNumber(1).blockFormat();
        check(bf.tabPositions().size() == 4 && bf.tabPositions()[1].type == QTextOption::RightTab
                  && qAbs(bf.tabPositions()[3].position - direct::mmToPx(100)) < 0.1 && (bf.alignment() & Qt::AlignRight)
                  && qAbs(bf.leftMargin() - direct::mmToPx(10)) < 0.1,
              QStringLiteral("RTF keeps tab stops and indents"));
        check(md::write(&rb).contains(QLatin1String("{: align=right left=10mm right=5mm first=0mm before=6pt after=12pt line=1.5 tabs=20mm,50mmR,80mmC,100mmD} Right")),
              QStringLiteral("RTF and back to Markdown gives the same paragraph attributes"), md::write(&rb));

        // RTF text that becomes Markdown: differences become formatting set by hand
        QTextDocument rm;
        rtf::read("{\\rtf1\\ansi{\\fonttbl{\\f0 Times;}{\\f1 Arial;}}\\pard\\qr\\li567\\fi0 {\\f1\\fs32 Hello}\\par\\pard\\fi360 plain\\par}", &rm, f);
        direct::markDifferences(&rm, f);
        display::normalizeDocument(&rm, f);
        const QString conv = md::write(&rm);
        check(conv.startsWith(QLatin1String("{: align=right left=10mm} <span")) && conv.contains(QLatin1String("font-family:'Arial'; font-size:16pt"))
                  && conv.endsWith(QLatin1String("\nplain\n")),
              QStringLiteral("foreign RTF converts to Markdown with its formatting"), conv);
    }

    // ---------------------------------------------------------------- compile with fonts and paragraph formatting (0.3.0)
    {
        QTemporaryDir ctmp;
        QString err;
        templates::Info info;
        for (const auto &i : templates::all())
            if (i.id == QLatin1String("novel"))
                info = i;
        auto p = templates::createProject(info, ctmp.path(), QStringLiteral("Format Test"), &err);
        BinderItem *chapter = p ? p->draft()->children.value(0) : nullptr;
        BinderItem *scene = chapter ? chapter->children.value(0) : nullptr;
        check(scene != nullptr, QStringLiteral("project for the compile formatting checks"), err);
        if (scene) {
            templates::setMarkdown(
                p.get(), scene,
                QStringLiteral("Plain <span style=\"font-family:'DejaVu Sans'; font-size:16pt; color:#aa0000; letter-spacing:0.05em; "
                               "font-variant:small-caps\">styled &amp; text</span> end.\n"
                               "{: align=right left=10mm right=5mm first=-5mm before=6pt after=12pt line=1.5 tabs=20mm,50mmR} Item\tprice\n"
                               "{: line=14pt} Exact line.\n"
                               "{: align=justify} Justified."));
            p->save(&err);
            compile::AuthorInfo author{QStringLiteral("Jane"), QStringLiteral("Doe")};
            for (const auto &fmt : compile::builtinFormats()) {
                if (fmt.name != QLatin1String("Manuscript (Times)") && !fmt.useEditorFormatting)
                    continue;
                compile::Compiler c(p.get(), fmt, p->compile, author);
                const compile::Manuscript m = c.build();
                compile::DocxExporter docx;
                const QString out = QDir(ctmp.path()).filePath(fmt.name + QStringLiteral(".docx"));
                check(docx.exportManuscript(m, out, &err), QStringLiteral("compile formatted text: ") + fmt.name, err);
                const QString xml = QString::fromUtf8(ZipReader(out).fileData(QStringLiteral("word/document.xml")));
                if (fmt.useEditorFormatting) {
                    check(xml.contains(QLatin1String("<w:rFonts w:ascii=\"DejaVu Sans\""))
                              && xml.contains(QLatin1String("<w:smallCaps/><w:color w:val=\"AA0000\"/><w:spacing w:val=\"16\"/><w:sz w:val=\"32\"/>")),
                          QStringLiteral("font, size, colour, spacing and small caps reach the DOCX file"), xml.left(3000));
                    check(xml.contains(QLatin1String("<w:tabs><w:tab w:val=\"left\" w:pos=\"1417\"/><w:tab w:val=\"right\" w:pos=\"3118\"/></w:tabs>"
                                                     "<w:spacing w:before=\"120\" w:after=\"240\" w:line=\"360\" w:lineRule=\"auto\"/>"
                                                     "<w:ind w:left=\"567\" w:right=\"283\" w:hanging=\"283\"/><w:jc w:val=\"right\"/>")),
                          QStringLiteral("tab stops, spacing, indents and alignment reach the DOCX file"), xml.left(3000));
                    check(xml.contains(QLatin1String("w:line=\"280\" w:lineRule=\"exact\"")) && xml.contains(QLatin1String("<w:jc w:val=\"both\"/>")),
                          QStringLiteral("exact line height and justified text reach the DOCX file"));
                } else {
                    check(!xml.contains(QLatin1String("DejaVu Sans")) && !xml.contains(QLatin1String("<w:sz w:val=\"32\"/>"))
                              && xml.contains(QLatin1String("<w:color w:val=\"AA0000\"/>")) && xml.contains(QLatin1String("<w:smallCaps/>")),
                          QStringLiteral("a format with its own font replaces family and size, keeps colour and capitals"), xml.left(3000));
                    check(!xml.contains(QLatin1String("w:left=\"567\"")) && xml.contains(QLatin1String("<w:jc w:val=\"right\"/>"))
                              && xml.contains(QLatin1String("<w:tabs>")),
                          QStringLiteral("a format with its own look replaces indents, keeps alignment and tab stops"));
                }
                // every element must be closed properly
                QXmlStreamReader xr(xml);
                while (!xr.atEnd())
                    xr.readNext();
                check(!xr.hasError(), QStringLiteral("document.xml is well-formed: ") + fmt.name, xr.errorString());
            }
            // RTF project: the same formatting compiles the same way
            check(p->docs()->convertStorage(StorageFormat::Rtf, &err), QStringLiteral("convert formatted project to RTF"), err);
            for (const auto &fmt : compile::builtinFormats()) {
                if (!fmt.useEditorFormatting)
                    continue;
                compile::Compiler c(p.get(), fmt, p->compile, author);
                const compile::Manuscript m = c.build();
                bool found = false;
                for (const auto &para : m.paragraphs)
                    if (para.plainText().startsWith(QLatin1String("Item"))) {
                        found = (para.direct & compile::Paragraph::DLeft) && (para.direct & compile::Paragraph::DTabs)
                                && qAbs(para.leftIndentMm - 10) < 0.05 && para.tabs.size() == 2 && (para.align & Qt::AlignRight);
                    }
                check(found, QStringLiteral("RTF project: paragraph formatting is compiled"));
            }
            check(p->docs()->convertStorage(StorageFormat::Markdown, &err), QStringLiteral("convert formatted project back to Markdown"), err);
            QFile f(p->contentDir(scene->id) + QStringLiteral("/text.md"));
            f.open(QIODevice::ReadOnly);
            const QString back = QString::fromUtf8(f.readAll());
            check(back.contains(QLatin1String("<span style=\"font-family:'DejaVu Sans'; font-size:16pt; color:#aa0000; letter-spacing:0.05em; font-variant:small-caps\">"))
                      && back.contains(QLatin1String("{: align=right left=10mm right=5mm first=-5mm before=6pt after=12pt line=1.5 tabs=20mm,50mmR} Item"))
                      && back.contains(QLatin1String("{: line=14pt} Exact line.")) && back.contains(QLatin1String("{: align=justify} Justified.")),
                  QStringLiteral("fonts and paragraph formatting survive Markdown > RTF > Markdown"), back);
        }
    }

    // ---------------------------------------------------------------- styles (0.4.0)
    {
        using direct::mmToPx;
        const StyleSheet &def = styles::defaults();
        check(def.find(QStringLiteral("heading 2")) && def.find(QStringLiteral("Block Quote")) && def.find(QStringLiteral("Emphasis"))
                  && def.find(QStringLiteral("Emphasis"))->character && !def.find(QStringLiteral("Nope")),
              QStringLiteral("built-in styles: headings, block quote, character style"));
        StyleSheet back;
        check(styles::fromXmlString(styles::toXmlString(def), &back) && back == def,
              QStringLiteral("the built-in styles survive saving and loading"), styles::toXmlString(back));

        // a project's own styles
        StyleSheet sheet = def;
        Style poem;
        poem.name = QStringLiteral("Poem");
        poem.italic = true;
        poem.chars.family = QStringLiteral("DejaVu Sans");
        poem.chars.size = 11;
        poem.next = QString();
        poem.para.bits = direct::Left | direct::First | direct::After;
        poem.para.left = mmToPx(20);
        poem.para.first = 0;
        poem.para.after = direct::ptToPx(3);
        Style term;
        term.name = QStringLiteral("Term");
        term.character = true;
        term.bold = true;
        term.chars.caps = 1;
        term.chars.color = QColor(0, 90, 160);
        sheet.styles << poem << term;
        StyleSheet again;
        check(styles::fromXmlString(styles::toXmlString(sheet), &again) && again == sheet,
              QStringLiteral("own paragraph and character styles survive saving and loading"), styles::toXmlString(again));
        Formatting f = styles::withSheet(Formatting(), sheet);

        const QString text = QStringLiteral(
            "::Poem:: A line with a <span class=\"Term\">word</span> and **strong** words.\n"
            "Body with <span class=\"Term\">term</span> and <span class=\"Term\" style=\"color:#00aa00\">green term</span>.\n"
            "> A quote with <span class=\"Emphasis\">stress</span>.\n");
        QTextDocument d;
        md::read(text, &d, f);
        const QTextBlock b0 = d.begin();
        QTextCursor c(&d);
        c.setPosition(3);
        check(b0.blockFormat().stringProperty(props::Style) == QLatin1String("Poem")
                  && qAbs(b0.blockFormat().leftMargin() - mmToPx(20)) < 0.1 && qAbs(b0.blockFormat().textIndent()) < 0.01
                  && c.charFormat().fontItalic() && direct::charSpec(c.charFormat()).family == QLatin1String("DejaVu Sans")
                  && qAbs(c.charFormat().fontPointSize() - 11) < 0.01,
              QStringLiteral("a paragraph style gives indents, font and italics"));
        c.setPosition(16);
        check(c.charFormat().stringProperty(props::CharStyle) == QLatin1String("Term") && c.charFormat().fontWeight() == QFont::Bold
                  && c.charFormat().fontCapitalization() == QFont::SmallCaps && c.charFormat().fontItalic()
                  && c.charFormat().foreground().color() == QColor(0, 90, 160),
              QStringLiteral("a character style adds its formatting inside a paragraph style"));
        check(md::write(&d, f) == text, QStringLiteral("styles round trip in Markdown (names only)"), md::write(&d, f));
        check(mdRoundTrip(QStringLiteral("a <span class=\"Nope\">b</span> c\n")) == QLatin1String("a b c\n"),
              QStringLiteral("an unknown character style leaves plain text"), mdRoundTrip(QStringLiteral("a <span class=\"Nope\">b</span> c\n")));
        check(styles::usedStyles(&d) == QStringList({QStringLiteral("Poem"), QStringLiteral("Term"), QStringLiteral("Block Quote"), QStringLiteral("Emphasis")}),
              QStringLiteral("styles in use are found"), styles::usedStyles(&d).join(QLatin1Char(',')));

        // redefine: the look changes, the stored text does not
        StyleSheet sheet2 = sheet;
        Style &p2 = sheet2.styles[sheet2.indexOf(QStringLiteral("Poem"))];
        p2.italic = false;
        p2.chars = direct::CharSpec();
        p2.chars.color = QColor(120, 0, 0);
        p2.para.left = mmToPx(30);
        p2.para.bits |= direct::Align;
        p2.para.align = Qt::AlignRight;
        Style &t2 = sheet2.styles[sheet2.indexOf(QStringLiteral("Term"))];
        t2.bold = false;
        t2.underline = true;
        const Formatting f2 = styles::withSheet(Formatting(), sheet2);
        const bool names = styles::restyle(&d, f, f2, styles::Changes());
        c.setPosition(3);
        const QTextCharFormat plainCf = c.charFormat();
        c.setPosition(16);
        const QTextCharFormat termCf = c.charFormat();
        c.setPosition(26);
        check(!names && qAbs(d.begin().blockFormat().leftMargin() - mmToPx(30)) < 0.1 && (d.begin().blockFormat().alignment() & Qt::AlignRight)
                  && !plainCf.fontItalic() && direct::charSpec(plainCf).family.isEmpty() && plainCf.foreground().color() == QColor(120, 0, 0)
                  && termCf.fontUnderline() && termCf.fontWeight() != QFont::Bold && termCf.foreground().color() == QColor(0, 90, 160)
                  && c.charFormat().fontWeight() == QFont::Bold,
              QStringLiteral("redefined styles reformat the text, bold set by hand stays"));
        check(md::write(&d, f2) == text, QStringLiteral("the Markdown text is the same after redefining"), md::write(&d, f2));
        QTextDocument fresh;
        md::read(text, &fresh, f2);
        bool sameLook = fresh.blockCount() == d.blockCount();
        for (QTextBlock x = d.begin(), y = fresh.begin(); sameLook && x.isValid(); x = x.next(), y = y.next()) {
            sameLook = direct::differences(x.blockFormat(), y.blockFormat()) == 0;
            auto ix = x.begin(), iy = y.begin();
            for (; sameLook && !ix.atEnd() && !iy.atEnd(); ++ix, ++iy)
                sameLook = ix.fragment().text() == iy.fragment().text()
                           && ix.fragment().charFormat().properties() == iy.fragment().charFormat().properties();
            sameLook = sameLook && ix.atEnd() && iy.atEnd();
        }
        check(sameLook, QStringLiteral("updated text looks like freshly loaded text"));

        // rename
        StyleSheet sheet3 = sheet2;
        sheet3.styles[sheet3.indexOf(QStringLiteral("Poem"))].name = QStringLiteral("Verse");
        sheet3.styles[sheet3.indexOf(QStringLiteral("Term"))].name = QStringLiteral("Keyword");
        sheet3.aliases.insert(QStringLiteral("Poem"), QStringLiteral("Verse"));
        sheet3.aliases.insert(QStringLiteral("Term"), QStringLiteral("Keyword"));
        const Formatting f3 = styles::withSheet(Formatting(), sheet3);
        styles::Changes ren;
        ren.renamed.insert(QStringLiteral("Poem"), QStringLiteral("Verse"));
        ren.renamed.insert(QStringLiteral("Term"), QStringLiteral("Keyword"));
        QString renamedText = text;
        renamedText.replace(QLatin1String("::Poem::"), QLatin1String("::Verse::"));
        renamedText.replace(QLatin1String("class=\"Term\""), QLatin1String("class=\"Keyword\""));
        check(styles::restyle(&d, f2, f3, ren) && md::write(&d, f3) == renamedText,
              QStringLiteral("renamed styles are renamed in the text"), md::write(&d, f3));
        QTextDocument old;
        md::read(text, &old, f3);
        check(md::write(&old, f3) == renamedText, QStringLiteral("text with the former names (snapshots) finds the renamed styles"),
              md::write(&old, f3));

        // delete: the look stays as formatting set by hand
        const Formatting f4 = styles::withSheet(Formatting(), def);
        styles::Changes none;
        const bool removedNames = styles::restyle(&d, f3, f4, none);
        const QString unstyled = md::write(&d, f4);
        check(removedNames && unstyled.startsWith(QLatin1String("{: align=right left=30mm first=0mm after=3pt} <span style=\"color:#780000\">A line with a </span>"))
                  && unstyled.contains(QLatin1String("<span style=\"color:#005aa0; font-variant:small-caps\"><u>word</u></span>"))
                  && !unstyled.contains(QLatin1String("Verse")) && !unstyled.contains(QLatin1String("Keyword"))
                  && unstyled.contains(QLatin1String("> A quote with <span class=\"Emphasis\">stress</span>.")),
              QStringLiteral("text of deleted styles keeps its look as formatting set by hand"), unstyled);

        // RTF
        QTextDocument r1;
        md::read(text, &r1, f);
        const QByteArray rtfData = rtf::write(&r1, f);
        check(rtfData.contains("\\additive Term;}") && rtfData.contains(" Poem;}") && rtfData.contains("\\li1134"),
              QStringLiteral("RTF has a style sheet with paragraph and character styles"), QString::fromLatin1(rtfData.left(1500)));
        QTextDocument r2;
        rtf::read(rtfData, &r2, f);
        check(md::write(&r2, f) == text, QStringLiteral("styles round trip in RTF"), md::write(&r2, f));
        // the way a project is converted from RTF to Markdown
        QTextDocument r3;
        rtf::read(rtfData, &r3, f);
        direct::markDifferences(&r3, f);
        display::normalizeDocument(&r3, f);
        check(md::write(&r3, f) == text, QStringLiteral("RTF with styles converts to the same Markdown"), md::write(&r3, f));
        QTextDocument r4;
        md::read(QStringLiteral("Body.\n{: align=right left=10mm} Last paragraph.\n"), &r4, f);
        QTextDocument r5;
        rtf::read(rtf::write(&r4, f), &r5, f);
        check(md::write(&r5, f) == QLatin1String("Body.\n{: align=right left=10mm} Last paragraph.\n"),
              QStringLiteral("the last paragraph of an RTF text keeps its formatting"), md::write(&r5, f));

        // a style made from text
        QTextDocument cap;
        md::read(QStringLiteral("{: align=center left=15mm before=12pt} <span style=\"font-family:'DejaVu Serif'; font-size:15pt; color:#333399\">**Motto**</span>\n"), &cap, Formatting());
        QTextCursor cc(&cap);
        cc.setPosition(2);
        styles::CaptureOptions co;
        Style motto = styles::capture(QStringLiteral("Motto"), cap.begin().blockFormat(), cc.charFormat(), Formatting(), co);
        check(!motto.character && motto.bold && motto.chars.family == QLatin1String("DejaVu Serif") && qAbs(motto.chars.size - 15) < 0.01
                  && motto.chars.color == QColor(0x33, 0x33, 0x99)
                  && motto.para.bits == (direct::Align | direct::Left | direct::First | direct::Before)
                  && qAbs(motto.para.left - mmToPx(15)) < 0.1 && (motto.para.align & Qt::AlignHCenter),
              QStringLiteral("a style captured from text holds what differs from body text"),
              QStringLiteral("bits %1").arg(motto.para.bits));
        co.withFamily = false;
        co.withSize = false;
        co.character = true;
        const Style mc = styles::capture(QStringLiteral("M"), cap.begin().blockFormat(), cc.charFormat(), Formatting(), co);
        check(mc.character && mc.bold && mc.chars.family.isEmpty() && mc.chars.size == 0 && mc.chars.color.isValid() && mc.para.bits == 0,
              QStringLiteral("a character style without font and size"));
        // applying the captured style to the paragraph it came from leaves only the style name
        StyleSheet ms = def;
        ms.styles << motto;
        const Formatting mf = styles::withSheet(Formatting(), ms);
        QTextCursor all(&cap);
        display::applyParagraphStyle(all, QStringLiteral("Motto"), mf);
        check(md::write(&cap, mf) == QLatin1String("::Motto:: Motto\n"), QStringLiteral("text a style was made from carries only the name afterwards"),
              md::write(&cap, mf));
        styles::applyCharacterStyle(&cap, 0, 3, QStringLiteral("Emphasis"), mf);
        check(md::write(&cap, mf) == QLatin1String("::Motto:: <span class=\"Emphasis\">Mot</span>to\n"),
              QStringLiteral("a character style is applied to a range"), md::write(&cap, mf));
        styles::applyCharacterStyle(&cap, 0, 5, QString(), mf);
        check(md::write(&cap, mf) == QLatin1String("::Motto:: Motto\n"), QStringLiteral("and removed again"), md::write(&cap, mf));
        check(styles::validName(QStringLiteral("My Style 2")) && !styles::validName(QStringLiteral("a:b")) && !styles::validName(QStringLiteral(" x"))
                  && !styles::validName(QStringLiteral("No Style")) && styles::isFixedName(QStringLiteral("Heading 3"))
                  && !styles::isFixedName(QStringLiteral("Code Block")),
              QStringLiteral("style names are checked"));
    }

    // ---------------------------------------------------------------- styles in a project, compile (0.4.0)
    {
        using direct::mmToPx;
        QTemporaryDir stmp;
        QString err;
        templates::Info info;
        for (const auto &i : templates::all())
            if (i.id == QLatin1String("novel"))
                info = i;
        auto p = templates::createProject(info, stmp.path(), QStringLiteral("Style Test"), &err);
        BinderItem *chapter = p ? p->draft()->children.value(0) : nullptr;
        BinderItem *scene = chapter ? chapter->children.value(0) : nullptr;
        check(scene && !p->formatting.styles, QStringLiteral("a new project uses the built-in styles"), err);
        if (scene) {
            StyleSheet sheet = styles::sheet(p->formatting);
            Style poem;
            poem.name = QStringLiteral("Poem");
            poem.italic = true;
            poem.para.bits = direct::Left | direct::First;
            poem.para.left = mmToPx(20);
            Style term;
            term.name = QStringLiteral("Term");
            term.character = true;
            term.bold = true;
            sheet.styles << poem << term;
            check(p->setStyleSheet(sheet, {}, {}, &err), QStringLiteral("give a project its own styles"), err);
            templates::setMarkdown(p.get(), scene,
                                   QStringLiteral("Body text.\n::Poem:: Roses are <span class=\"Term\">red</span>,\n"
                                                  "::Code Block:: x = 1\n> Quoted with <span class=\"Code Span\">code</span>.\n# Heading\n"));
            p->save(&err);
            const QString folder = p->folderPath();
            const QString scenePath = p->contentDir(scene->id) + QStringLiteral("/text.md");
            const QString sceneId = scene->id;
            p.reset();
            auto q = Project::open(folder, &err);
            check(q && styles::find(q->formatting, QStringLiteral("Poem")) && styles::findChar(q->formatting, QStringLiteral("Term"))
                      && qAbs(styles::find(q->formatting, QStringLiteral("Poem"))->para.left - mmToPx(20)) < 0.2,
                  QStringLiteral("styles are saved with the project"), err);
            auto readScene = [&] {
                QFile f(scenePath);
                f.open(QIODevice::ReadOnly);
                return QString::fromUtf8(f.readAll());
            };
            if (q) {
                // compile: an own style brings its look along, whatever the format
                compile::AuthorInfo author{QStringLiteral("Jane"), QStringLiteral("Doe")};
                for (const auto &fmt : compile::builtinFormats()) {
                    if (fmt.name != QLatin1String("Manuscript (Times)") && !fmt.useEditorFormatting)
                        continue;
                    compile::Compiler c(q.get(), fmt, q->compile, author);
                    const compile::Manuscript m = c.build();
                    compile::DocxExporter docx;
                    const QString out = QDir(stmp.path()).filePath(fmt.name + QStringLiteral(".docx"));
                    check(docx.exportManuscript(m, out, &err), QStringLiteral("compile styled text: ") + fmt.name, err);
                    const QString xml = QString::fromUtf8(ZipReader(out).fileData(QStringLiteral("word/document.xml")));
                    bool poemOk = false, codeOk = false, quoteOk = false;
                    for (const auto &para : m.paragraphs) {
                        const QString t = para.plainText();
                        if (t.startsWith(QLatin1String("Roses"))) {
                            poemOk = (para.direct & compile::Paragraph::DLeft) && qAbs(para.leftIndentMm - 20) < 0.1 && para.runs.size() >= 2
                                     && para.runs.first().italic && para.runs.at(1).bold && para.runs.at(1).italic;
                        } else if (t.startsWith(QLatin1String("x = 1"))) {
                            codeOk = !para.runs.isEmpty() && !para.runs.first().fontFamily.isEmpty()
                                     && para.runs.first().fontFamily != QLatin1String("monospace") && para.runs.first().fontSize > 0;
                        } else if (t.startsWith(QLatin1String("Quoted"))) {
                            quoteOk = para.style == compile::Paragraph::BlockQuote && para.runs.size() >= 2
                                      && !para.runs.at(1).fontFamily.isEmpty();
                        }
                    }
                    check(poemOk, QStringLiteral("own paragraph and character styles are compiled with their look: ") + fmt.name);
                    check(codeOk && quoteOk, QStringLiteral("fonts of styles are kept and get a real font name: ") + fmt.name);
                    check(xml.contains(QLatin1String("w:left=\"1134\"")), QStringLiteral("the indent of the own style is in the DOCX file: ") + fmt.name,
                          xml.left(2500));
                    QXmlStreamReader xr(xml);
                    while (!xr.atEnd())
                        xr.readNext();
                    check(!xr.hasError(), QStringLiteral("document.xml with styles is well-formed: ") + fmt.name, xr.errorString());
                }

                // rename and redefine with the documents closed: the files are rewritten
                StyleSheet s2 = styles::sheet(q->formatting);
                s2.styles[s2.indexOf(QStringLiteral("Poem"))].name = QStringLiteral("Verse");
                s2.styles[s2.indexOf(QStringLiteral("Verse"))].para.left = mmToPx(30);
                int calls = 0;
                q->save(&err);
                q.reset();
                q = Project::open(folder, &err); // no document in memory
                check(q->setStyleSheet(s2, {{QStringLiteral("Poem"), QStringLiteral("Verse")}}, [&](int, int) { ++calls; return true; }, &err)
                          && readScene().contains(QLatin1String("::Verse:: Roses are <span class=\"Term\">red</span>,")) && calls > 0,
                      QStringLiteral("renaming a style rewrites the documents of the project"), readScene());
                check(styles::sheet(q->formatting).aliases.value(QStringLiteral("Poem")) == QLatin1String("Verse"),
                      QStringLiteral("the former name is remembered"));
                // an open document follows at once
                QTextDocument *doc = q->docs()->text(sceneId);
                StyleSheet s3 = styles::sheet(q->formatting);
                s3.styles[s3.indexOf(QStringLiteral("Verse"))].para.left = mmToPx(40);
                q->docs()->saveDirty(&err);
                q->setStyleSheet(s3);
                check(qAbs(doc->findBlockByNumber(1).blockFormat().leftMargin() - mmToPx(40)) < 0.2 && !q->docs()->hasDirty(),
                      QStringLiteral("an open document follows a redefined style and needs no saving in Markdown"));
                // delete
                StyleSheet s4 = styles::sheet(q->formatting);
                s4.styles.removeAt(s4.indexOf(QStringLiteral("Verse")));
                q->setStyleSheet(s4);
                q->save(&err);
                check(readScene().contains(QLatin1String("{: left=40mm first=0mm} *Roses are* <span class=\"Term\">*red*</span>*,*")),
                      QStringLiteral("deleting a style keeps the look in the project's files"), readScene());

                // RTF project: redefining rewrites the look in closed files
                StyleSheet s5 = styles::sheet(q->formatting);
                Style ind;
                ind.name = QStringLiteral("Inset");
                ind.para.bits = direct::Left;
                ind.para.left = mmToPx(10);
                s5.styles.insert(0, ind);
                q->setStyleSheet(s5);
                templates::setMarkdown(q.get(), q->item(sceneId), QStringLiteral("::Inset:: Inset text.\nBody.\n"));
                check(q->docs()->convertStorage(StorageFormat::Rtf, &err), QStringLiteral("convert the styled project to RTF"), err);
                q->save(&err);
                q.reset();
                q = Project::open(folder, &err);
                const QString rtfPath = q->contentDir(sceneId) + QStringLiteral("/text.rtf");
                auto readRtf = [&] {
                    QFile f(rtfPath);
                    f.open(QIODevice::ReadOnly);
                    return f.readAll();
                };
                const QByteArray before = readRtf();
                StyleSheet s6 = styles::sheet(q->formatting);
                s6.styles[s6.indexOf(QStringLiteral("Inset"))].para.left = mmToPx(25);
                q->setStyleSheet(s6);
                const QByteArray after = readRtf();
                check(before.contains("\\li567") && after.contains("\\li1417") && !after.contains("\\li567"),
                      QStringLiteral("RTF project: a redefined style is written into closed documents"), QString::fromLatin1(after.left(900)));
            }
        }
    }

    // ---------------------------------------------------- search and replace (0.9)
    {
        QTemporaryDir rtmp;
        QString err;
        templates::Info info;
        for (const auto &i : templates::all())
            if (i.id == QLatin1String("novel"))
                info = i;
        auto p = templates::createProject(info, rtmp.path(), QStringLiteral("Search Test"), &err);
        check(p != nullptr, QStringLiteral("search: create project"), err);
        if (p) {
            BinderModel *m = p->model();
            BinderItem *part = m->createItem(p->draft(), 0, ItemType::Folder, QStringLiteral("The Harbour"));
            BinderItem *arrival = m->createItem(part, 0, ItemType::Text, QStringLiteral("Arrival of Marta"));
            BinderItem *storm = m->createItem(part, 1, ItemType::Text, QStringLiteral("The Storm"));
            BinderItem *letter = m->createItem(part, 2, ItemType::Text, QStringLiteral("A Letter"));
            BinderItem *notesDoc = m->createItem(p->research(), 0, ItemType::Text, QStringLiteral("Harbour Notes"));
            BinderItem *gone = m->createItem(p->draft(), 1, ItemType::Text, QStringLiteral("Dropped Scene"));
            templates::setMarkdown(p.get(), arrival,
                                   QStringLiteral("# Marta arrives\nMarta stepped off the boat. **Marta** looked at the harbour, ==the old harbour==.\n"
                                                  "A {==grey gull==}{>>Marta dislikes gulls<<} watched her.\n"));
            templates::setMarkdown(p.get(), storm,
                                   QStringLiteral("The storm came at night. Nobody saw Martas boat.\nmarta was not afraid, <rev2>not at all</rev2>.\n"));
            templates::setMarkdown(p.get(), letter,
                                   QStringLiteral("Dear Anna, the harbour is quiet {>>check the date<<}. See [the map](https://example.com/map).\n"
                                                  "::Block Quote:: Yours, *M.*\nA <span style=\"color:#aa0000\">red</span> word and a note^[Source: Marta].\n"));
            templates::setMarkdown(p.get(), notesDoc, QStringLiteral("Harbour depth: four fathoms.\n"));
            templates::setMarkdown(p.get(), gone, QStringLiteral("Marta in the trash.\n"));
            arrival->synopsis = QStringLiteral("Marta comes to town.");
            storm->synopsis = QStringLiteral("A storm; the boat is lost.");
            storm->includeInCompile = false;
            {
                QTextDocument n;
                md::read(QStringLiteral("Ask Marta's sister about the boat.\n"), &n, p->formatting);
                p->docs()->setNotesContent(storm->id, &n);
            }
            Label red;
            red.id = p->nextLabelId();
            red.name = QStringLiteral("Marta POV");
            red.color = QColor(200, 40, 40);
            p->labels << red;
            Status st;
            st.id = p->nextStatusId();
            st.name = QStringLiteral("Revised Draft");
            p->statuses << st;
            Keyword kw;
            kw.id = QStringLiteral("kw-sea");
            kw.name = QStringLiteral("Sea");
            p->keywords << kw;
            CustomField cfText, cfBox;
            cfText.id = QStringLiteral("place");
            cfText.title = QStringLiteral("Place");
            cfBox.id = QStringLiteral("done");
            cfBox.title = QStringLiteral("Done");
            cfBox.type = CustomFieldType::Checkbox;
            p->customFields << cfText << cfBox;
            arrival->label = red.id;
            arrival->status = st.id;
            storm->keywords << kw.id;
            arrival->custom.insert(QStringLiteral("place"), QStringLiteral("Marta's quay"));
            arrival->custom.insert(QStringLiteral("done"), QStringLiteral("Marta"));
            m->moveToTrash({gone});

            auto titlesOf = [](const QList<BinderItem *> &items) {
                QStringList t;
                for (BinderItem *it : items)
                    t << it->title;
                return t.join(QLatin1String(" | "));
            };
            auto run = [&](std::function<void(SearchQuery &)> set) {
                SearchQuery q;
                set(q);
                return titlesOf(searchProject(p.get(), q));
            };
            // ---- operators
            const QString allWords = run([](SearchQuery &q) { q.text = QStringLiteral("storm boat"); });
            const QString anyWord = run([](SearchQuery &q) {
                q.text = QStringLiteral("storm fathoms");
                q.anyWord = true;
            });
            const QString exact = run([](SearchQuery &q) {
                q.text = QStringLiteral("off the boat");
                q.exactPhrase = true;
                q.allWords = false;
            });
            check(allWords == QLatin1String("The Storm") && anyWord == QLatin1String("The Storm | Harbour Notes")
                      && exact == QLatin1String("Arrival of Marta"),
                  QStringLiteral("search: all words, any word, exact phrase"), allWords + QLatin1String(" / ") + anyWord + QLatin1String(" / ") + exact);
            const QString regex = run([](SearchQuery &q) {
                q.text = QStringLiteral("f(our|ive) fathoms|gr[ae]y gull");
                q.regex = true;
            });
            const QString badRegex = run([](SearchQuery &q) {
                q.text = QStringLiteral("(unclosed");
                q.regex = true;
            });
            SearchQuery bad;
            bad.text = QStringLiteral("(unclosed");
            bad.regex = true;
            check(regex == QLatin1String("Arrival of Marta | Harbour Notes") && badRegex.isEmpty() && !search::patternError(bad).isEmpty()
                      && !search::pattern(bad).isValid(),
                  QStringLiteral("search: regular expression; a mistake in it finds nothing and is reported"), regex + QLatin1String(" / ") + badRegex);
            const QString whole = run([](SearchQuery &q) {
                q.text = QStringLiteral("Marta");
                q.wholeWord = true;
                q.inTitle = q.inSynopsis = q.inNotes = q.inKeywords = q.inCustom = false;
            });
            const QString part2 = run([](SearchQuery &q) {
                q.text = QStringLiteral("Martas");
                q.inTitle = q.inSynopsis = q.inNotes = q.inKeywords = q.inCustom = false;
            });
            const QString cased = run([](SearchQuery &q) {
                q.text = QStringLiteral("marta");
                q.caseSensitive = true;
                q.inTitle = q.inSynopsis = q.inNotes = q.inKeywords = q.inCustom = false;
            });
            check(whole == QLatin1String("Arrival of Marta | The Storm | A Letter") && part2 == QLatin1String("The Storm")
                      && cased == QLatin1String("The Storm"),
                  QStringLiteral("search: whole words and case"), whole + QLatin1String(" / ") + part2 + QLatin1String(" / ") + cased);
            // ---- where
            const QString inLabel = run([](SearchQuery &q) {
                q.text = QStringLiteral("POV");
                q.searchOnlyIn(&SearchQuery::inLabel);
            });
            const QString inStatus = run([](SearchQuery &q) {
                q.text = QStringLiteral("revised");
                q.searchOnlyIn(&SearchQuery::inStatus);
            });
            const QString inKeywords = run([](SearchQuery &q) {
                q.text = QStringLiteral("sea");
                q.searchOnlyIn(&SearchQuery::inKeywords);
            });
            const QString inNotes = run([](SearchQuery &q) {
                q.text = QStringLiteral("sister");
                q.searchOnlyIn(&SearchQuery::inNotes);
            });
            SearchQuery everywhere;
            everywhere.searchEverywhere();
            check(inLabel == QLatin1String("Arrival of Marta") && inStatus == QLatin1String("Arrival of Marta")
                      && inKeywords == QLatin1String("The Storm") && inNotes == QLatin1String("The Storm") && everywhere.searchesEverywhere()
                      && !SearchQuery().searchesEverywhere(),
                  QStringLiteral("search: only in label, status, keywords, notes"),
                  inLabel + QLatin1String(" / ") + inStatus + QLatin1String(" / ") + inKeywords + QLatin1String(" / ") + inNotes);
            // ---- scope
            const QString draftOnly = run([](SearchQuery &q) {
                q.text = QStringLiteral("harbour");
                q.draftOnly = true;
            });
            const QString everywhereHarbour = run([](SearchQuery &q) { q.text = QStringLiteral("harbour"); });
            const QString scoped = run([&](SearchQuery &q) {
                q.text = QStringLiteral("harbour");
                q.scopeIds = QStringList({p->research()->id, letter->id});
            });
            const QString included = run([](SearchQuery &q) {
                q.text = QStringLiteral("marta");
                q.compile = 1;
                q.inTitle = q.inSynopsis = q.inNotes = q.inKeywords = q.inCustom = false;
            });
            const QString excluded = run([](SearchQuery &q) {
                q.text = QStringLiteral("marta");
                q.compile = 2;
            });
            const QString inverted = run([](SearchQuery &q) {
                q.text = QStringLiteral("harbour");
                q.invert = true;
                q.draftOnly = true;
            });
            const QString withTrash = run([](SearchQuery &q) {
                q.text = QStringLiteral("trash");
                q.includeTrash = true;
            });
            check(draftOnly == QLatin1String("The Harbour | Arrival of Marta | A Letter")
                      && everywhereHarbour == QLatin1String("The Harbour | Arrival of Marta | A Letter | Harbour Notes")
                      && scoped == QLatin1String("A Letter | Harbour Notes"),
                  QStringLiteral("search: Draft only, binder selection only"), draftOnly + QLatin1String(" / ") + scoped);
            check(included == QLatin1String("Arrival of Marta | A Letter") && excluded == QLatin1String("The Storm")
                      && inverted.contains(QLatin1String("The Storm")) && !inverted.contains(QLatin1String("Arrival"))
                      && withTrash == QLatin1String("Dropped Scene"),
                  QStringLiteral("search: included / excluded documents, inverted results, trash"),
                  included + QLatin1String(" / ") + excluded + QLatin1String(" / ") + inverted + QLatin1String(" / ") + withTrash);
            // ---- queries as data, in the project file
            {
                SearchQuery q;
                q.text = QStringLiteral("f(ou|ive)");
                q.regex = q.draftOnly = q.invert = q.wholeWord = true;
                q.compile = 2;
                q.scopeIds = QStringList({part->id, letter->id});
                q.searchOnlyIn(&SearchQuery::inText);
                const SearchQuery back = search::fromMap(search::toMap(q));
                Collection c;
                c.id = QStringLiteral("col-regex");
                c.name = QStringLiteral("Saved");
                c.isSearch = true;
                c.query = q;
                SearchQuery any;
                any.text = QStringLiteral("storm fathoms");
                any.anyWord = true;
                any.compile = 1;
                Collection c2 = c;
                c2.id = QStringLiteral("col-any");
                c2.query = any;
                p->collections << c << c2;
                p->save(&err);
                auto q2 = Project::open(p->folderPath(), &err);
                const SearchQuery stored = q2 ? q2->collections.value(q2->collections.size() - 2).query : SearchQuery();
                const SearchQuery stored2 = q2 ? q2->collections.value(q2->collections.size() - 1).query : SearchQuery();
                auto same = [&q](const SearchQuery &o) {
                    return o.text == q.text && o.regex && o.draftOnly && o.invert && o.wholeWord && o.compile == 2 && o.scopeIds == q.scopeIds
                           && o.inText && !o.inTitle && !o.inNotes && !o.anyWord;
                };
                check(same(back) && same(stored) && stored2.anyWord && stored2.compile == 1 && !stored2.regex && stored2.scopeIds.isEmpty(),
                      QStringLiteral("search: the new options survive as data and in the project file"));
                p->collections.removeLast();
                p->collections.removeLast();
            }
            // ---- quick search
            {
                const QList<QuickHit> hits = quickSearchGrouped(p.get(), QStringLiteral("marta"), 10);
                QStringList got;
                for (const QuickHit &h : hits)
                    got << QString::number(int(h.kind)) + QLatin1Char(':') + h.item->title;
                const QString text = got.join(QLatin1String(" | "));
                check(text == QLatin1String("0:Arrival of Marta | 2:The Storm | 2:A Letter") && hits.value(1).excerpt.contains(QLatin1String("Martas boat")),
                      QStringLiteral("quick search: titles, synopses, text; a document is listed once, with the words around the match"),
                      text + QLatin1String(" ~ ") + hits.value(1).excerpt);
                for (int i = 0; i < 14; ++i)
                    m->createItem(p->research(), -1, ItemType::Text, QStringLiteral("Gull study %1").arg(i))->synopsis = QStringLiteral("about a gull");
                const QList<QuickHit> many = quickSearchGrouped(p.get(), QStringLiteral("gull"), 10);
                int nTitle = 0, nText = 0;
                for (const QuickHit &h : many)
                    (h.kind == QuickHit::Title ? nTitle : nText) += 1;
                check(nTitle == 10 && many.size() >= 11, QStringLiteral("quick search: at most ten of each kind"),
                      QStringLiteral("%1 titles of %2").arg(nTitle).arg(many.size()));
                for (BinderItem *it : p->research()->children)
                    if (it->title.startsWith(QLatin1String("Gull study")))
                        it->synopsis.clear();
                QList<BinderItem *> studies;
                for (BinderItem *it : p->research()->children)
                    if (it->title.startsWith(QLatin1String("Gull study")))
                        studies << it;
                m->moveToTrash(studies);
            }
            // ---- filter of corkboard and outliner
            {
                ItemFilter f;
                check(!f.isActive() && search::matches(p.get(), storm, f), QStringLiteral("filter: without anything set, everything is shown"));
                f.text = QStringLiteral("boat");
                const bool bySynopsis = search::matches(p.get(), storm, f) && !search::matches(p.get(), arrival, f);
                f.inSynopsis = false;
                const bool titleOnly = !search::matches(p.get(), storm, f);
                f.inText = true;
                const bool byText = search::matches(p.get(), storm, f) && search::matches(p.get(), arrival, f);
                ItemFilter l;
                l.label = red.id;
                const bool byLabel = search::matches(p.get(), arrival, l) && !search::matches(p.get(), storm, l);
                l.label = -1;
                const bool noLabel = !search::matches(p.get(), arrival, l) && search::matches(p.get(), storm, l);
                ItemFilter s2;
                s2.status = st.id;
                s2.text = QStringLiteral("arrival");
                const bool byStatus = search::matches(p.get(), arrival, s2) && !search::matches(p.get(), letter, s2);
                ItemFilter k;
                k.keyword = kw.id;
                const bool byKeyword = search::matches(p.get(), storm, k) && !search::matches(p.get(), letter, k);
                ItemFilter c;
                c.compile = 2;
                const bool byCompile = search::matches(p.get(), storm, c) && !search::matches(p.get(), letter, c);
                c.invert = true;
                const bool inv = !search::matches(p.get(), storm, c) && search::matches(p.get(), letter, c);
                check(bySynopsis && titleOnly && byText && byLabel && noLabel && byStatus && byKeyword && byCompile && inv,
                      QStringLiteral("filter: text in titles / synopses / text, label, status, keyword, compile, inverted"),
                      QStringLiteral("%1%2%3%4%5%6%7%8%9").arg(bySynopsis).arg(titleOnly).arg(byText).arg(byLabel).arg(noLabel).arg(byStatus).arg(byKeyword).arg(byCompile).arg(inv));
            }
            // ---- find by formatting
            {
                const Formatting f = p->formatting;
                auto texts = [&](BinderItem *it, const fmtfind::Query &q) {
                    QTextDocument *d = p->docs()->text(it->id);
                    QStringList out;
                    for (const fmtfind::Range &r : fmtfind::all(d, q, f)) {
                        QTextCursor c(d);
                        c.setPosition(r.start);
                        c.setPosition(r.end, QTextCursor::KeepAnchor);
                        out << c.selectedText();
                    }
                    return out.join(QLatin1String(" | "));
                };
                fmtfind::Query q;
                q.kind = fmtfind::Kind::Highlight;
                const QString hl = texts(arrival, q);
                q.kind = fmtfind::Kind::CommentOrFootnote;
                const QString comments = texts(arrival, q);
                q.containing = QStringLiteral("dislikes");
                const QString commentByNote = texts(arrival, q);
                q.containing = QStringLiteral("nothing like it");
                const QString commentNone = texts(arrival, q);
                q = fmtfind::Query();
                q.kind = fmtfind::Kind::CharFormat;
                const QString bold = texts(arrival, q);
                q.charFormat = fmtfind::Italic;
                const QString italic = texts(letter, q);
                check(hl == QLatin1String("the old harbour") && comments == QLatin1String("grey gull") && commentByNote == QLatin1String("grey gull")
                          && commentNone.isEmpty() && bold == QLatin1String("Marta") && italic == QLatin1String("M."),
                      QStringLiteral("find by formatting: highlight, comment (also by its text), bold set by hand (not the heading), italic"),
                      hl + QLatin1String(" / ") + comments + QLatin1String(" / ") + bold + QLatin1String(" / ") + italic);
                q = fmtfind::Query();
                q.kind = fmtfind::Kind::InlineAnnotation;
                const QString ann = texts(letter, q);
                q.kind = fmtfind::Kind::InlineFootnote;
                const QString foot = texts(letter, q);
                q.kind = fmtfind::Kind::Link;
                const QString link = texts(letter, q);
                q.kind = fmtfind::Kind::ColoredText;
                const QString colored = texts(letter, q);
                q.color = QColor(0, 0, 255);
                const QString blue = texts(letter, q);
                q.color = QColor(0xaa, 0, 0);
                const QString redText = texts(letter, q);
                check(ann == QLatin1String("check the date") && foot == QLatin1String("Source: Marta") && link == QLatin1String("the map")
                          && colored == QLatin1String("red") && blue.isEmpty() && redText == QLatin1String("red"),
                      QStringLiteral("find by formatting: inline annotation, inline footnote, link, colored text (limited to a color)"),
                      ann + QLatin1String(" / ") + foot + QLatin1String(" / ") + link + QLatin1String(" / ") + colored + QLatin1String(" / ") + blue);
                q = fmtfind::Query();
                q.kind = fmtfind::Kind::Revision;
                const QString rev = texts(storm, q);
                q.revision = 1;
                const QString rev1 = texts(storm, q);
                q.revision = 2;
                const QString rev2 = texts(storm, q);
                q = fmtfind::Query();
                q.kind = fmtfind::Kind::Style;
                const QString anyStyle = texts(letter, q) + QLatin1String(" + ") + texts(arrival, q);
                q.style = QStringLiteral("Block Quote");
                const QString quote = texts(letter, q);
                q.style = QStringLiteral("Heading 1");
                const QString heading = texts(arrival, q);
                check(rev == QLatin1String("not at all") && rev1.isEmpty() && rev2 == QLatin1String("not at all")
                          && anyStyle == QLatin1String("Yours, M. + Marta arrives") && quote == QLatin1String("Yours, M.")
                          && heading == QLatin1String("Marta arrives"),
                      QStringLiteral("find by formatting: revision levels, styles"), rev + QLatin1String(" / ") + anyStyle + QLatin1String(" / ") + quote);
                QTextDocument *d = p->docs()->text(arrival->id);
                fmtfind::Query b;
                b.kind = fmtfind::Kind::Highlight;
                const fmtfind::Range first = fmtfind::next(d, b, f, 0, false);
                const fmtfind::Range none = fmtfind::next(d, b, f, first.end, false);
                const fmtfind::Range back = fmtfind::next(d, b, f, d->characterCount(), true);
                const fmtfind::Range before = fmtfind::next(d, b, f, first.start, true);
                check(first.isValid() && !none.isValid() && back.start == first.start && !before.isValid(),
                      QStringLiteral("find by formatting: next and previous from a position"));
            }
            // ---- replace: strings and documents
            {
                QString s = QStringLiteral("Marta and marta and Martas");
                const int n1 = replace::inString(&s, search::pattern(QStringLiteral("marta"), false, true, false), QStringLiteral("Elsa"), false, false);
                QString s2 = QStringLiteral("4 fathoms, 12 fathoms");
                const int n2 = replace::inString(&s2, search::pattern(QStringLiteral("(\\d+) fathoms"), false, false, true),
                                                 QStringLiteral("\\1 m (\\0)"), true, false);
                QString s3 = QStringLiteral("a.b");
                const int n3 = replace::inString(&s3, search::pattern(QStringLiteral("."), false, false, false), QStringLiteral("-"), false, false);
                QString s4 = QStringLiteral("unchanged Marta");
                const int n4 = replace::inString(&s4, search::pattern(QStringLiteral("Marta"), true, false, false), QStringLiteral("X"), false, true);
                check(n1 == 2 && s == QLatin1String("Elsa and Elsa and Martas") && n2 == 2 && s2 == QLatin1String("4 m (4 fathoms), 12 m (12 fathoms)")
                          && n3 == 1 && s3 == QLatin1String("a-b") && n4 == 1 && s4 == QLatin1String("unchanged Marta"),
                      QStringLiteral("replace in strings: whole words, groups of a regular expression, plain text is not a pattern, count only"),
                      s + QLatin1String(" / ") + s2 + QLatin1String(" / ") + s3);
                QTextDocument d;
                md::read(QStringLiteral("Plain Marta, **bold Marta** and *Marta* again.\nSecond: Marta.\n"), &d, p->formatting);
                d.setUndoRedoEnabled(true);
                const int counted = replace::inDocument(&d, search::pattern(QStringLiteral("Marta"), true, false, false), QStringLiteral("Elsa-Maria"), false, true);
                const QString untouched = md::write(&d, p->formatting);
                const int n = replace::inDocument(&d, search::pattern(QStringLiteral("Marta"), true, false, false), QStringLiteral("Elsa-Maria"), false, false);
                const QString out = md::write(&d, p->formatting);
                d.undo();
                const QString undone = md::write(&d, p->formatting);
                check(counted == 4 && n == 4 && untouched.contains(QLatin1String("**bold Marta**"))
                          && out == QLatin1String("Plain Elsa-Maria, **bold Elsa-Maria** and *Elsa-Maria* again.\nSecond: Elsa-Maria.\n")
                          && undone == untouched,
                      QStringLiteral("replace in a text keeps the formatting and is one undo step"), out);
            }
            // ---- project replace
            {
                const QString folder = p->folderPath();
                const QString arrivalId = arrival->id, stormId = storm->id, letterId = letter->id, goneId = gone->id, notesId = notesDoc->id;
                p->save(&err);
                p.reset();
                auto q = Project::open(folder, &err); // no document in memory
                auto fileOf = [&](const QString &id, bool notes = false) {
                    QFile f(notes ? q->docs()->notesPath(id) : q->docs()->textPath(id));
                    f.open(QIODevice::ReadOnly);
                    return QString::fromUtf8(f.readAll());
                };
                ReplaceOptions o;
                o.find = QStringLiteral("Marta");
                o.with = QStringLiteral("Elsa");
                o.caseSensitive = true;
                o.wholeWord = true;
                o.snapshotFirst = true;
                o.snapshotTitle = QStringLiteral("Before");
                const QString before = fileOf(arrivalId);
                const ReplaceResult counted = replace::run(q.get(), o, true);
                check(counted.texts == 4 && counted.titles == 1 && counted.synopses == 1 && counted.notes == 1 && counted.comments == 1
                          && counted.custom == 1 && counted.documents == 3 && fileOf(arrivalId) == before
                          && q->item(arrivalId)->title == QLatin1String("Arrival of Marta") && q->docs()->loadedText(arrivalId) == nullptr,
                      QStringLiteral("project replace: counting changes nothing and reads no document into memory"),
                      QStringLiteral("texts %1 titles %2 synopses %3 notes %4 comments %5 custom %6 documents %7")
                          .arg(counted.texts).arg(counted.titles).arg(counted.synopses).arg(counted.notes).arg(counted.comments).arg(counted.custom).arg(counted.documents));
                // one document is open in an editor (in memory): it is changed there
                QTextDocument *open = q->docs()->text(letterId);
                int calls = 0;
                const ReplaceResult done = replace::run(q.get(), o, false, [&](int, int) {
                    ++calls;
                    return true;
                });
                BinderItem *a2 = q->item(arrivalId);
                const QString arrivalText = fileOf(arrivalId);
                const QString stormText = fileOf(stormId);
                check(done.total() == counted.total() && calls > 3 && a2->title == QLatin1String("Arrival of Elsa")
                          && a2->synopsis == QLatin1String("Elsa comes to town.") && a2->custom.value(QStringLiteral("place")) == QLatin1String("Elsa's quay")
                          && a2->custom.value(QStringLiteral("done")) == QLatin1String("Marta"),
                      QStringLiteral("project replace: titles, synopses, custom text fields (other kinds of fields are left alone)"),
                      a2->title + QLatin1String(" / ") + a2->synopsis + QLatin1String(" / ") + a2->custom.value(QStringLiteral("place")));
                check(arrivalText.contains(QLatin1String("# Elsa arrives\nElsa stepped off the boat. **Elsa** looked"))
                          && arrivalText.contains(QLatin1String("{==grey gull==}{>>Elsa dislikes gulls<<}"))
                          && stormText.contains(QLatin1String("Martas boat")) && stormText.contains(QLatin1String("marta was not afraid"))
                          && fileOf(stormId, true).contains(QLatin1String("Ask Elsa's sister")) && q->docs()->loadedText(arrivalId) == nullptr,
                      QStringLiteral("project replace: closed documents are rewritten in their files (text with formatting, comment texts, notes); whole words and case are respected"),
                      arrivalText + stormText);
                const bool openChanged = text::plainText(open).contains(QLatin1String("Source: Elsa"));
                open->undo();
                const bool undoable = text::plainText(open).contains(QLatin1String("Source: Marta"));
                open->redo();
                check(openChanged && undoable && text::plainText(open).contains(QLatin1String("Source: Elsa")),
                      QStringLiteral("project replace: an open document is changed in place and Undo takes it back there"));
                const QList<Snapshot> snaps = snapshots::list(q.get(), arrivalId);
                QTextDocument old;
                if (!snaps.isEmpty())
                    snapshots::load(q.get(), snaps.first(), &old);
                check(done.snapshots == 2 && snaps.size() == 1 && snaps.first().title == QLatin1String("Before")
                          && text::plainText(&old).contains(QLatin1String("Marta stepped off")) && a2->hasSnapshots
                          && snapshots::list(q.get(), stormId).isEmpty(),
                      QStringLiteral("project replace: a snapshot of every changed text, taken before the change"),
                      QStringLiteral("%1 snapshots, list %2").arg(done.snapshots).arg(snaps.size()));
                check(fileOf(goneId).contains(QLatin1String("Marta in the trash")) && q->item(goneId)->title == QLatin1String("Dropped Scene"),
                      QStringLiteral("project replace: the trash is left alone unless asked for"));
                // scope, regular expression, a second run finds nothing
                const ReplaceResult again = replace::run(q.get(), o, true);
                ReplaceOptions r;
                r.find = QStringLiteral("(\\w+) fathoms");
                r.with = QStringLiteral("\\1 metres");
                r.regex = true;
                r.scopeIds = QStringList({q->research()->id});
                const ReplaceResult scoped = replace::run(q.get(), r, false);
                ReplaceOptions h;
                h.find = QStringLiteral("harbour");
                h.with = QStringLiteral("port");
                h.scopeIds = QStringList({letterId});
                h.inTitles = false;
                const ReplaceResult one = replace::run(q.get(), h, false);
                ReplaceOptions badRe;
                badRe.find = QStringLiteral("(oops");
                badRe.regex = true;
                const ReplaceResult failed = replace::run(q.get(), badRe, false);
                check(again.total() == 0 && scoped.texts == 1 && scoped.documents == 1 && fileOf(notesId).contains(QLatin1String("four metres"))
                          && one.texts == 1 && one.documents == 1 && fileOf(arrivalId).contains(QLatin1String("old harbour"))
                          && failed.total() == 0 && !failed.error.isEmpty(),
                      QStringLiteral("project replace: selected documents only, regular expression with groups, a bad expression is refused"),
                      QStringLiteral("again %1 scoped %2 one %3 '%4'").arg(again.total()).arg(scoped.texts).arg(one.texts).arg(failed.error));
                q->save(&err);
                q.reset();
                auto q3 = Project::open(folder, &err);
                check(q3 && q3->item(arrivalId)->title == QLatin1String("Arrival of Elsa")
                          && q3->docs()->plainText(letterId).contains(QLatin1String("the port is quiet"))
                          && q3->docs()->plainText(letterId).contains(QLatin1String("Source: Elsa")),
                      QStringLiteral("project replace: everything is there after saving and reopening"));
                // RTF storage
                if (q3) {
                    check(q3->docs()->convertStorage(StorageFormat::Rtf, &err), QStringLiteral("project replace: convert the project to RTF"), err);
                    q3->save(&err);
                    q3.reset();
                    q3 = Project::open(folder, &err);
                    ReplaceOptions e;
                    e.find = QStringLiteral("Elsa");
                    e.with = QStringLiteral("Marta");
                    e.caseSensitive = true;
                    const ReplaceResult rr = replace::run(q3.get(), e, false);
                    QFile rf(q3->docs()->textPath(arrivalId));
                    rf.open(QIODevice::ReadOnly);
                    const QByteArray rtf = rf.readAll();
                    QTextDocument back;
                    q3->docs()->deserialize(arrivalId, rtf, &back, true);
                    QTextCursor bc = back.find(QStringLiteral("Marta"), back.find(QStringLiteral("boat.")));
                    check(rr.texts == 4 && rr.comments == 1 && rtf.contains("Marta stepped off") && q3->docs()->textPath(arrivalId).endsWith(QLatin1String(".rtf"))
                              && !bc.isNull() && bc.charFormat().fontWeight() >= QFont::Bold
                              && q3->item(arrivalId)->title == QLatin1String("Arrival of Marta"),
                          QStringLiteral("project replace in an RTF project: closed documents rewritten, bold stays bold"),
                          QStringLiteral("texts %1 comments %2").arg(rr.texts).arg(rr.comments));
                }
            }
        }
    }
    failures += scriptChecks();
    failures += scrivenerChecks();
    failures += compileChecks();
    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
