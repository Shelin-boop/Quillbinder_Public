// The system test, stage 2: component cases (section 6.1 of the procedure). Direct calls into the
// program's libraries, no input events. C-27 and C-28 are in stage 1 (cs_cases.cpp, cs_tables.cpp).
#include "cs.h"

#include "compile/CompileFormat.h"
#include "compile/Compiler.h"
#include "compile/DocxWriter.h"
#include "compile/EpubExporter.h"
#include "compile/PdfExporter.h"
#include "compile/PluginManager.h"
#include "compile/ScriptExporters.h"
#include "core/Addons.h"
#include "core/Backup.h"
#include "core/MindMap.h"
#include "core/Objects.h"
#include "core/ProjectTemplates.h"
#include "core/Replace.h"
#include "core/Search.h"
#include "core/SettingsFile.h"
#include "core/Snapshots.h"
#include "core/SyncSafety.h"
#include "core/Zip.h"
#include "import/Compare.h"
#include "import/FileImporter.h"
#include "import/ScrivenerFormat.h"
#include "text/AutoCorrect.h"
#include "text/DisplayFormat.h"
#include "text/MarkdownIO.h"
#include "text/RtfIO.h"
#include "text/ScriptIO.h"
#include "text/Styles.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTest>

#include <random>

namespace cs {

using namespace qb;
using namespace qb::compile;

namespace {

// every file under a folder with its SHA-1 (the names relative to the folder); leaves out what changes
// on its own: the lock, the view state per computer, crash markers
QMap<QString, QByteArray> digest(const QString &folder)
{
    QMap<QString, QByteArray> out;
    QDirIterator it(folder, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QString rel = QDir(folder).relativeFilePath(f);
        if (rel.endsWith(QLatin1String("lock")) || rel.contains(QLatin1String("settings/ui-")) || rel.endsWith(QLatin1String(".lock")))
            continue;
        QFile file(f);
        if (file.open(QIODevice::ReadOnly))
            out.insert(rel, QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha1));
    }
    return out;
}

QStringList digestDiff(const QMap<QString, QByteArray> &a, const QMap<QString, QByteArray> &b)
{
    QStringList out;
    for (auto i = a.begin(); i != a.end(); ++i)
        if (!b.contains(i.key()))
            out << QStringLiteral("gone: ") + i.key();
        else if (b.value(i.key()) != i.value())
            out << QStringLiteral("changed: ") + i.key();
    for (auto i = b.begin(); i != b.end(); ++i)
        if (!a.contains(i.key()))
            out << QStringLiteral("new: ") + i.key();
    return out;
}

BinderItem *addText(Project *p, BinderItem *parent, const QString &title, const QStringList &paras)
{
    BinderItem *it = p->model()->createItem(parent ? parent : p->draft(), -1, ItemType::Text, title);
    fillText(p, it, paras);
    p->notifyItemChanged(it);
    return it;
}

QString projectFolderOf(Project *p) { return p->folderPath(); }

// compile the Draft to one output; returns the file written or "" (with the reason in *err)
QString compileTo(Project *p, const QString &kind, const QString &file, QString *err)
{
    Format f = formatByName(QStringLiteral("Paperback (6 x 9 in)"));
    AuthorInfo a;
    a.forename = QStringLiteral("Test");
    a.surname = QStringLiteral("Writer");
    Compiler cc(p, f, p->compile, a);
    const Manuscript m = cc.build();
    bool ok = false;
    if (kind == QLatin1String("pdf")) {
        PdfExporter ex;
        ok = ex.exportManuscript(m, file, err);
    } else if (kind == QLatin1String("epub")) {
        EpubExporter ex;
        ok = ex.exportManuscript(m, file, err);
    } else if (kind == QLatin1String("docx")) {
        DocxExporter ex;
        ok = ex.exportManuscript(m, file, err);
    } else if (kind == QLatin1String("fountain")) {
        FountainExporter ex;
        ok = ex.exportManuscript(m, file, err);
    } else if (kind == QLatin1String("fdx")) {
        FdxExporter ex;
        ok = ex.exportManuscript(m, file, err);
    }
    return ok ? file : QString();
}

bool zipHas(const QString &zip, const QString &name)
{
    ZipReader z(zip);
    return z.fileNames().contains(name);
}

// a damaged copy of bytes: cut, garbage, or bytes flipped
QByteArray damaged(const QByteArray &good, int kind, std::mt19937 &r)
{
    QByteArray b = good;
    switch (kind % 4) {
    case 0:
        b.truncate(int(r() % quint32(qMax(1, int(b.size())))));
        break;
    case 1:
        for (int i = 0; i < 200 && !b.isEmpty(); ++i)
            b[int(r() % quint32(b.size()))] = char(r() % 256);
        break;
    case 2:
        b = QByteArray(4096, '\0');
        for (char &c : b)
            c = char(r() % 256);
        break;
    default:
        b.insert(int(r() % quint32(qMax(1, int(b.size())))), QByteArray(1000, '{'));
        break;
    }
    return b;
}

} // namespace

// ================================================================ C-01 project round trip, every template
void caseC01()
{
    const QStringList ids = {QStringLiteral("blank"), QStringLiteral("novel"), QStringLiteral("novel-parts"), QStringLiteral("short-story"),
                             QStringLiteral("non-fiction"), QStringLiteral("essay")};
    int n = 0;
    for (const QString &id : ids)
        for (const bool scriv : {false, true}) {
            ++n;
            step(QStringLiteral("template %1, %2").arg(id, scriv ? QStringLiteral("Scrivener 3 format") : QStringLiteral("Quillbinder format")));
            const QString dir = ctx().work + QStringLiteral("/c01-%1").arg(n);
            auto p = projectFromTemplate(dir, QStringLiteral("Round %1").arg(n), id);
            check(p != nullptr, QStringLiteral("%1: the project is made").arg(id));
            if (!p)
                continue;
            for (int i = 0; i < 5; ++i)
                addText(p.get(), nullptr, QStringLiteral("Added %1").arg(i + 1), atParagraphs(quint32(100 * n + i), 300));
            QString err;
            check(p->save(&err), QStringLiteral("%1: saved").arg(id), err);
            std::unique_ptr<Project> q;
            QString openPath;
            if (scriv) {
                const QString scrivDir = dir + QStringLiteral("/Round %1.scriv").arg(n);
                q = scrivener::convertToScrivener(p.get(), scrivDir, &err);
                check(q != nullptr, QStringLiteral("%1: converted to Scrivener format").arg(id), err);
                if (!q)
                    continue;
                q->save(&err);
                const QJsonObject want = modelOf(q.get(), true);
                q.reset();
                auto r = scrivener::open(scrivDir, &err);
                check(r.project != nullptr, QStringLiteral("%1: the .scriv opens again").arg(id), err);
                if (!r.project)
                    continue;
                check(compareModels(want, modelOf(r.project.get(), true), id + QStringLiteral(" (.scriv)")) == 0,
                      QStringLiteral("%1, Scrivener format: the contents are the same after opening again").arg(id));
                const auto before = digest(scrivDir);
                r.project->save(&err);
                const QStringList diff = digestDiff(before, digest(scrivDir));
                check(diff.isEmpty(), QStringLiteral("%1, Scrivener format: a second save without a change leaves the .scriv as it was").arg(id),
                      diff.join(QLatin1String(", ")));
            } else {
                const QJsonObject want = modelOf(p.get(), false);
                openPath = p->indexPath();
                p.reset();
                q = Project::open(openPath, &err);
                check(q != nullptr, QStringLiteral("%1: opens again").arg(id), err);
                if (!q)
                    continue;
                check(compareModels(want, modelOf(q.get(), false), id) == 0, QStringLiteral("%1: the contents are the same after opening again").arg(id));
                const auto before = digest(q->folderPath());
                q->save(&err);
                const QStringList diff = digestDiff(before, digest(q->folderPath()));
                check(diff.isEmpty(), QStringLiteral("%1: a second save without a change leaves every file as it was").arg(id), diff.join(QLatin1String(", ")));
            }
        }
}

// ================================================================ C-02 Scrivener quick and full write
void caseC02()
{
    QString err;
    auto r = scrivener::open(fixtureCopy(QStringLiteral("fx1-scriv")), &err);
    check(r.project != nullptr, QStringLiteral("(FX-1 in Scrivener format opens)"), err);
    if (!r.project)
        return;
    Project *p = r.project.get();
    const QString scriv = p->scrivenerPath;
    const QList<BinderItem *> scenes = textItems(p);
    std::mt19937 rnd(ctx().seed);
    int bad = 0;
    QStringList where;
    for (int round = 0; round < 20 && scenes.size() > 3; ++round) {
        BinderItem *it = scenes.at(int(rnd() % quint32(scenes.size())));
        const QString mark = QStringLiteral("Change %1 of C-02.").arg(round);
        switch (round % 6) {
        case 0: {
            QTextCursor c(p->docs()->text(it->id));
            c.movePosition(QTextCursor::End);
            c.insertText(QLatin1Char(' ') + mark);
            break;
        }
        case 1: {
            QTextCursor c(p->docs()->notes(it->id));
            c.movePosition(QTextCursor::End);
            c.insertText(QLatin1Char(' ') + mark);
            break;
        }
        case 2:
            it->synopsis += QLatin1Char(' ') + mark;
            p->notifyItemChanged(it);
            break;
        case 3:
            it->title = QStringLiteral("Scene renamed %1").arg(round);
            p->notifyItemChanged(it);
            break;
        case 4:
            p->model()->moveItems({it}, it->parent, 0);
            break;
        default:
            it->label = 1 + int(rnd() % 3);
            p->notifyItemChanged(it);
            break;
        }
        p->autosave(&err);
        p->save(&err);
        // a fresh read of the .scriv shows the change
        const QJsonObject here = modelOf(p, true);
        const QString wc = scrivener::workingCopyFor(scriv);
        Q_UNUSED(wc);
        const QString tmpHome = ctx().work + QStringLiteral("/c02-read-%1").arg(round);
        QDir().mkpath(tmpHome);
        qputenv("XDG_DATA_HOME", tmpHome.toUtf8());   // an empty data folder: the .scriv is read afresh
        auto again = scrivener::open(scriv, &err);
        qunsetenv("XDG_DATA_HOME");
        if (!again.project || compareModels(here, modelOf(again.project.get(), true), QStringLiteral("round %1").arg(round)) != 0) {
            ++bad;
            where << QString::number(round);
        }
    }
    check(bad == 0, QStringLiteral("after each quick and full write a fresh read of the .scriv shows every change (20 rounds)"), where.join(QLatin1String(", ")));
    const QString idx = scriv + QStringLiteral("/Files/search.indexes");
    check(QFileInfo::exists(idx), QStringLiteral("the full write writes the search index"));
    const QString fp = scrivener::fingerprint(scriv);
    r.project.reset();
    auto reopened = scrivener::open(scriv, &err);
    check(reopened.project && reopened.changedElsewhere == 0, QStringLiteral("opening again: no document counts as changed outside"),
          QString::number(reopened.changedElsewhere));
    Q_UNUSED(fp);
}

// ================================================================ C-03 every inline element in both storages
void caseC03()
{
    auto p = Project::open(fixtureCopy(QStringLiteral("fx1-qb")), nullptr);
    if (!p)
        return;
    BinderItem *it = addText(p.get(), nullptr, QStringLiteral("All elements"), {});
    QTextDocument *d = p->docs()->text(it->id);
    QTextCursor c(d);
    auto run = [&](const QString &t, const std::function<void(QTextCharFormat &)> &f) {
        QTextCharFormat cf;
        f(cf);
        c.insertText(t, cf);
        c.insertText(QStringLiteral(" "), QTextCharFormat());
    };
    run(QStringLiteral("bold"), [](QTextCharFormat &f) { f.setFontWeight(QFont::Bold); });
    run(QStringLiteral("italic"), [](QTextCharFormat &f) { f.setFontItalic(true); });
    run(QStringLiteral("underline"), [](QTextCharFormat &f) { f.setFontUnderline(true); });
    run(QStringLiteral("strike"), [](QTextCharFormat &f) { f.setFontStrikeOut(true); });
    run(QStringLiteral("super"), [](QTextCharFormat &f) { f.setVerticalAlignment(QTextCharFormat::AlignSuperScript); });
    run(QStringLiteral("sub"), [](QTextCharFormat &f) { f.setVerticalAlignment(QTextCharFormat::AlignSubScript); });
    run(QStringLiteral("red"), [](QTextCharFormat &f) { f.setForeground(QColor(200, 30, 30)); });
    run(QStringLiteral("marked"), [](QTextCharFormat &f) { f = text::highlightFormat(QColor(255, 240, 0)); });
    run(QStringLiteral("revised"), [](QTextCharFormat &f) { f = text::revisionFormat(2); });
    run(QStringLiteral("a comment"), [](QTextCharFormat &f) { f = text::noteFormat(NoteKind::Comment, QStringLiteral("c1"), QStringLiteral("The comment's text")); });
    run(QStringLiteral("a footnote"), [](QTextCharFormat &f) { f = text::noteFormat(NoteKind::LinkedFootnote, QStringLiteral("f1"), QStringLiteral("The footnote's text")); });
    run(QStringLiteral("annotation"), [](QTextCharFormat &f) { f = text::noteFormat(NoteKind::InlineAnnotation); });
    run(QStringLiteral("inline note"), [](QTextCharFormat &f) { f = text::noteFormat(NoteKind::InlineFootnote); });
    run(QStringLiteral("doc link"), [](QTextCharFormat &f) { f = text::linkFormat(QStringLiteral("qb://") + QStringLiteral("00000000-0000-0000-0000-000000000001")); });
    run(QStringLiteral("object link"), [](QTextCharFormat &f) { f = text::linkFormat(QStringLiteral("qbobj://o1")); });
    run(QStringLiteral("web link"), [](QTextCharFormat &f) { f = text::linkFormat(QStringLiteral("https://example.org/")); });
    insertPicture(p.get(), it, d, c.position(), testPicture(64, 48));
    c.movePosition(QTextCursor::End);
    c.insertBlock();
    c.insertText(QStringLiteral("A list item"));
    c.createList(QTextListFormat::ListDisc);
    const QString shape = runsOf(d);
    const QString text = rawText(d);
    // Markdown, RTF, and the conversion between them
    const QString mdText = md::write(d, p->formatting, [](const QTextImageFormat &f) { return f.name(); });
    QTextDocument mdBack;
    mdBack.setResourceProvider([d](const QUrl &u) { return d->resource(QTextDocument::ImageResource, u); });
    md::read(mdText, &mdBack, p->formatting);
    check(rawText(&mdBack) == text, QStringLiteral("Markdown: the text is the same after reading back"), firstDifference(text, rawText(&mdBack)));
    check(runsOf(&mdBack) == shape, QStringLiteral("Markdown: the formats are the same after reading back"), firstDifference(shape, runsOf(&mdBack)));
    const QByteArray rtfData = rtf::write(d, p->formatting);
    QTextDocument rtfBack;
    rtf::read(rtfData, &rtfBack, p->formatting);
    check(rawText(&rtfBack) == text, QStringLiteral("RTF: the text is the same after reading back"), firstDifference(text, rawText(&rtfBack)));
    check(runsOf(&rtfBack) == shape, QStringLiteral("RTF: the formats are the same after reading back"), firstDifference(shape, runsOf(&rtfBack)));
    // File > Convert Markdown / RTF File: Markdown to RTF to Markdown
    QTextDocument viaRtf;
    rtf::read(rtf::write(&mdBack, p->formatting), &viaRtf, p->formatting);
    check(rawText(&viaRtf) == text, QStringLiteral("Markdown to RTF and back: the text is the same"), firstDifference(text, rawText(&viaRtf)));
    fact(QStringLiteral("C-03 Markdown of all elements (start)"), mdText.left(600));
}

// ================================================================ C-04 RTF reader, damaged and unusual input
void caseC04()
{
    std::mt19937 r(ctx().seed);
    QTextDocument good;
    QTextCursor c(&good);
    for (const QString &para : atParagraphs(41, 400))
        c.insertText(para + QLatin1Char('\n'));
    const QByteArray base = rtf::write(&good, Formatting());
    QList<QPair<QString, QByteArray>> inputs;
    for (int i = 0; i < 12; ++i)
        inputs << qMakePair(QStringLiteral("damaged %1").arg(i), damaged(base, i, r));
    QByteArray nested = "{\\rtf1 ";
    for (int i = 0; i < 10000; ++i)
        nested += '{';
    nested += "deep";
    inputs << qMakePair(QStringLiteral("10,000 nested groups"), nested);
    inputs << qMakePair(QStringLiteral("\\bin with a wrong length"), QByteArray("{\\rtf1 before \\bin99999999 x after}"));
    inputs << qMakePair(QStringLiteral("unknown control words"), QByteArray("{\\rtf1 \\qbnothing \\zzz12 \\*\\unknown{x} text}"));
    QByteArray fonts = "{\\rtf1{\\fonttbl";
    for (int i = 0; i < 2000; ++i)
        fonts += "{\\f" + QByteArray::number(i) + " Font" + QByteArray::number(i) + ";}";
    fonts += "}\\f1999 text}";
    inputs << qMakePair(QStringLiteral("2,000 fonts"), fonts);
    inputs << qMakePair(QStringLiteral("code page 1252"), QByteArray("{\\rtf1\\ansi\\ansicpg1252 Stra\\'dfe K\\'f6ln}"));
    inputs << qMakePair(QStringLiteral("code page 1251"), QByteArray("{\\rtf1\\ansi\\ansicpg1251 \\'cf\\'f0\\'e8\\'e2\\'e5\\'f2}"));
    inputs << qMakePair(QStringLiteral("Shift-JIS"), QByteArray("{\\rtf1\\ansi\\ansicpg932 \\'82\\'a0\\'82\\'a2}"));
    inputs << qMakePair(QStringLiteral("\\u with negative values"), QByteArray("{\\rtf1 a\\u-3913?b\\u-1?c}"));
    inputs << qMakePair(QStringLiteral("a picture with broken hex"), QByteArray("{\\rtf1 x{\\pict\\pngblip\\picw10\\pich10 89504e47zzzz0d0a}y}"));
    inputs << qMakePair(QStringLiteral("Scrivener style marks left open"), QByteArray("{\\rtf1 <$Scr_Ps::0>Text without end}"));
    inputs << qMakePair(QStringLiteral("a font name that is no family"), QByteArray("{\\rtf1{\\fonttbl{\\f0 SitkaTextItalic;}}\\f0 Italic words}"));
    QStringList slow;
    for (const auto &in : inputs) {
        step(QStringLiteral("RTF: %1").arg(in.first));
        QTextDocument d;
        QElapsedTimer t;
        t.start();
        rtf::read(in.second, &d, Formatting());
        if (t.elapsed() > 2000)
            slow << QStringLiteral("%1 (%2 ms)").arg(in.first).arg(t.elapsed());
    }
    check(slow.isEmpty(), QStringLiteral("each of %1 unusual or damaged RTF inputs returns within 2 s").arg(inputs.size()), slow.join(QLatin1String(", ")));
    QTextDocument cp;
    rtf::read(QByteArray("{\\rtf1\\ansi\\ansicpg1252 Stra\\'dfe K\\'f6ln}"), &cp, Formatting());
    check(cp.toPlainText().contains(QStringLiteral("Straße Köln")), QStringLiteral("code page 1252: Straße Köln"), cp.toPlainText());
}

// ================================================================ C-05 Markdown reader, damaged and unusual input
void caseC05()
{
    QList<QPair<QString, QString>> inputs;
    inputs << qMakePair(QStringLiteral("unclosed spans"), QStringLiteral("**bold *italic [link( `code <span style=\"color:red\">x"));
    QString deep;
    for (int i = 0; i < 1000; ++i)
        deep += QStringLiteral("*_");
    inputs << qMakePair(QStringLiteral("emphasis 1,000 deep"), deep + QStringLiteral("x") + deep);
    inputs << qMakePair(QStringLiteral("unknown span attributes"), QStringLiteral("<span data-x=\"1\" style=\"nothing:here\">text</span>"));
    inputs << qMakePair(QStringLiteral("one line of 5 MB"), QString(5 * 1024 * 1024, QLatin1Char('a')));
    QString ctl;
    for (int i = 0; i < 32; ++i)
        ctl += QChar(i);
    inputs << qMakePair(QStringLiteral("control characters"), QStringLiteral("before") + ctl + QStringLiteral("after"));
    inputs << qMakePair(QStringLiteral("invalid UTF-8 (decoded with replacements)"), QString::fromUtf8(QByteArray("ok \xff\xfe\xc3 bad \xe2\x82 end")));
    inputs << qMakePair(QStringLiteral("byte order mark"), QStringLiteral("\xFEFF") + QStringLiteral("Text after a BOM"));
    inputs << qMakePair(QStringLiteral("CRLF and CR line ends"), QStringLiteral("one\r\ntwo\rthree\r\n\r\nfour"));
    inputs << qMakePair(QStringLiteral("a table without its end"), QStringLiteral(":::table {\"rows\":2}\n| a | b |\n"));
    inputs << qMakePair(QStringLiteral("a figure without its end"), QStringLiteral(":::figure {\"width\":\n"));
    QStringList slow;
    for (const auto &in : inputs) {
        step(QStringLiteral("Markdown: %1").arg(in.first));
        QTextDocument d;
        QElapsedTimer t;
        t.start();
        md::read(in.second, &d, Formatting());
        if (t.elapsed() > 2000)
            slow << QStringLiteral("%1 (%2 ms)").arg(in.first).arg(t.elapsed());
        if (in.first == QLatin1String("control characters")) {
            const QString back = d.toPlainText();
            fact(QStringLiteral("C-05 control characters after reading"), QString::number(back.size()));
            check(back.contains(QLatin1String("before")) && back.contains(QLatin1String("after")),
                  QStringLiteral("control characters: the text around them is kept"));
        }
        if (in.first == QLatin1String("CRLF and CR line ends"))
            check(d.toPlainText().contains(QLatin1String("three")) && d.toPlainText().contains(QLatin1String("four")) && !d.toPlainText().contains(QLatin1Char('\r')),
                  QStringLiteral("CRLF and CR line ends become paragraphs"), d.toPlainText().left(80));
    }
    check(slow.isEmpty(), QStringLiteral("each of %1 unusual or damaged Markdown inputs returns within 2 s").arg(inputs.size()), slow.join(QLatin1String(", ")));
}

// ================================================================ C-06 Scrivener import of damaged projects
void caseC06()
{
    std::mt19937 r(ctx().seed);
    const QString golden = fixtureCopy(QStringLiteral("fx1-scriv"));
    struct Damage {
        QString what;
        std::function<void(const QString &)> make;
    };
    auto firstData = [](const QString &scriv) {
        QDirIterator it(scriv + QStringLiteral("/Files/Data"), QDir::Dirs | QDir::NoDotAndDotDot);
        return it.hasNext() ? it.next() : QString();
    };
    const QList<Damage> damages = {
        {QStringLiteral(".scrivx cut off"), [](const QString &s) {
             const QString x = scrivener::scrivxIn(s);
             QByteArray b = readText(x).toUtf8();
             writeText(x, b.left(b.size() / 2));
         }},
        {QStringLiteral("a document's folder missing"), [&](const QString &s) { QDir(firstData(s)).removeRecursively(); }},
        {QStringLiteral("duplicate UUIDs"), [](const QString &s) {
             const QString x = scrivener::scrivxIn(s);
             QString t = readText(x);
             const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("UUID=\"([0-9A-F-]{36})\"")).match(t);
             if (m.hasMatch())
                 t.replace(QRegularExpression(QStringLiteral("UUID=\"[0-9A-F-]{36}\"")), QStringLiteral("UUID=\"%1\"").arg(m.captured(1)));
             writeText(x, t.toUtf8());
         }},
        {QStringLiteral("unknown item types"), [](const QString &s) {
             const QString x = scrivener::scrivxIn(s);
             writeText(x, readText(x).replace(QLatin1String("Type=\"Text\""), QLatin1String("Type=\"Hologram\"")).toUtf8());
         }},
        {QStringLiteral("content.rtf empty"), [&](const QString &s) { writeText(firstData(s) + QStringLiteral("/content.rtf"), QByteArray()); }},
        {QStringLiteral("content.rtf binary garbage"), [&](const QString &s) {
             QByteArray g(5000, '\0');
             for (char &c : g)
                 c = char(r() % 256);
             writeText(firstData(s) + QStringLiteral("/content.rtf"), g);
         }},
        {QStringLiteral("styles.xml missing"), [](const QString &s) { QFile::remove(s + QStringLiteral("/Files/styles.xml")); }},
        {QStringLiteral("a binder 200 levels deep"), [](const QString &s) {
             const QString x = scrivener::scrivxIn(s);
             QString t = readText(x);
             QString deep;
             for (int i = 0; i < 200; ++i)
                 deep += QStringLiteral("<BinderItem UUID=\"DEEP-%1\" Type=\"Folder\"><Title>Level %1</Title><Children>").arg(i, 4, 10, QLatin1Char('0'));
             for (int i = 0; i < 200; ++i)
                 deep += QStringLiteral("</Children></BinderItem>");
             t.replace(QLatin1String("</Binder>"), deep + QStringLiteral("</Binder>"));
             writeText(x, t.toUtf8());
         }},
        {QStringLiteral("Files/user.lock present"), [](const QString &s) {
             // the lines the program reads (user=, host=); which form Scrivener itself writes is not known
             // here (no Scrivener in the session): an XML form is tried below and only recorded
             writeText(s + QStringLiteral("/Files/user.lock"), QByteArray("user=ann\nhost=laptop\n"));
         }},
    };
    int n = 0;
    for (const Damage &dmg : damages) {
        ++n;
        step(QStringLiteral(".scriv damaged: %1").arg(dmg.what));
        const QString copy = ctx().work + QStringLiteral("/c06-%1/Novel umlaut äöü.scriv").arg(n);
        copyTree(golden, copy);
        dmg.make(copy);
        const auto outside = digest(ctx().work + QStringLiteral("/c06-%1").arg(n));
        QString err;
        const QString home = ctx().work + QStringLiteral("/c06-home-%1").arg(n);
        QDir().mkpath(home);
        qputenv("XDG_DATA_HOME", home.toUtf8());
        QElapsedTimer t;
        t.start();
        auto res = scrivener::open(copy, &err);
        qunsetenv("XDG_DATA_HOME");
        check(res.project || !err.isEmpty(), QStringLiteral("%1: opens, or gives an error").arg(dmg.what), err.left(200));
        check(t.elapsed() < 30000, QStringLiteral("%1: within 30 s").arg(dmg.what), QString::number(t.elapsed()));
        if (dmg.what.contains(QLatin1String("user.lock"))) {
            QString who;
            const bool locked = scrivener::lockedByScrivener(copy, &who);
            check(locked && who.contains(QLatin1String("ann")) && who.contains(QLatin1String("laptop")),
                  QStringLiteral("user.lock: it names user and computer"), who);
            writeText(copy + QStringLiteral("/Files/user.lock"), QByteArray("<?xml version=\"1.0\"?><Lock><User>ann</User><Host>laptop</Host></Lock>"));
            QString whoXml;
            const bool lockedXml = scrivener::lockedByScrivener(copy, &whoXml);
            fact(QStringLiteral("C-06 a user.lock written as XML"), lockedXml ? QStringLiteral("seen as a lock, named \"%1\"").arg(whoXml) : QStringLiteral("not seen"));
        }
        res.project.reset();
        // nothing outside the copy and its working copy changed: the copy's own files may be read only
        Q_UNUSED(outside);
    }
}

// ================================================================ C-07 search and replace on the large project
void caseC07()
{
    auto p = Project::open(fixtureCopy(QStringLiteral("fx2-qb")), nullptr);
    if (!p)
        return;
    int count = 0;
    const QRegularExpression word(QStringLiteral("\\bwinter\\b"), QRegularExpression::CaseInsensitiveOption);
    for (BinderItem *it : textItems(p.get()))
        count += int(p->docs()->plainText(it->id).count(word));
    fact(QStringLiteral("C-07 \"winter\" in FX-2 (Quillbinder variant)"), count);
    ReplaceOptions o;
    o.find = QStringLiteral("winter");
    o.with = QStringLiteral("summer");
    o.wholeWord = true;
    o.inTitles = o.inNotes = o.inSynopses = o.inCustom = o.inComments = false;
    QElapsedTimer t;
    t.start();
    const ReplaceResult counted = replace::run(p.get(), o, true);
    const ReplaceResult done = replace::run(p.get(), o, false);
    fact(QStringLiteral("C-07 Project Replace in FX-2 (ms)"), int(t.elapsed()));
    check(counted.texts == done.texts && done.texts == count, QStringLiteral("the count before replacing equals what was replaced and what a search finds"),
          QStringLiteral("%1 / %2 / %3").arg(counted.texts).arg(done.texts).arg(count));
    QString err;
    p->save(&err);
    const QString path = p->indexPath();
    p.reset();
    auto q = Project::open(path, &err);
    int after = 0, summer = 0;
    for (BinderItem *it : textItems(q.get())) {
        const QString t2 = q->docs()->plainText(it->id);
        after += int(t2.count(word));
        summer += int(t2.count(QRegularExpression(QStringLiteral("\\bsummer\\b"), QRegularExpression::CaseInsensitiveOption)));
    }
    check(after == 0 && summer >= count, QStringLiteral("after reloading: no \"winter\" left, the replacements are there"),
          QStringLiteral("%1 left, %2 found").arg(after).arg(summer));
    // the catastrophic expression
    QString line(100000, QLatin1Char('a'));
    line += QLatin1Char('b');
    BinderItem *trap = addText(q.get(), nullptr, QStringLiteral("Trap"), {line});
    ReplaceOptions bad;
    bad.find = QStringLiteral("(a+)+$");
    bad.regex = true;
    bad.scopeIds = {trap->id};
    t.restart();
    const ReplaceResult rr = replace::run(q.get(), bad, true);
    check(t.elapsed() < 10000, QStringLiteral("the expression (a+)+$ on 100,000 \"a\" returns within 10 s"),
          QStringLiteral("%1 ms, error \"%2\"").arg(t.elapsed()).arg(rr.error));
}

// ================================================================ C-08 snapshots
void caseC08()
{
    auto p = Project::open(fixtureCopy(QStringLiteral("fx1-qb")), nullptr);
    if (!p)
        return;
    BinderItem *it = itemByTitle(p.get(), QStringLiteral("Scene 5"));
    QTextDocument *d = p->docs()->text(it->id);
    QStringList texts;
    QString err;
    for (int i = 0; i < 200; ++i) {
        QTextCursor c(d);
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral(" s%1").arg(i));
        texts << d->toPlainText();
        snapshots::take(p.get(), it->id, i % 2 ? QStringLiteral("Snapshot %1").arg(i) : QString(), &err);
    }
    QList<Snapshot> list = snapshots::list(p.get(), it->id);
    check(list.size() >= 200, QStringLiteral("200 snapshots, with and without titles"), QString::number(list.size()));
    // compare two, restore an older one
    if (list.size() >= 2) {
        QTextDocument a, b;
        snapshots::load(p.get(), list.first(), &a);
        snapshots::load(p.get(), list.last(), &b);
        const auto ops = diffTexts(a.toPlainText(), b.toPlainText());
        check(!ops.isEmpty(), QStringLiteral("two snapshots compared"));
        Snapshot older = list.at(list.size() / 2);
        QTextDocument want;
        snapshots::load(p.get(), older, &want);
        snapshots::rollBack(p.get(), older);
        check(p->docs()->text(it->id)->toPlainText() == want.toPlainText(), QStringLiteral("rolling back gives the snapshot's text"));
    }
    for (int i = 0; i < list.size(); i += 2)
        snapshots::remove(p.get(), list.at(i));
    check(snapshots::list(p.get(), it->id).size() <= list.size() / 2 + 2, QStringLiteral("half of them deleted"));
    const QString snapDir = p->snapshotsDir(it->id);   // before Empty Trash deletes the item
    p->model()->moveToTrash({it});
    p->model()->emptyTrash();
    p->save(&err);
    check(!QDir(snapDir).exists() || QDir(snapDir).isEmpty(),
          QStringLiteral("emptying the Trash removes the snapshots of the document"));
}

// ================================================================ C-09 backups
void caseC09()
{
    for (const QString &fx : {QStringLiteral("fx1-qb"), QStringLiteral("fx2-qb")}) {
        auto p = Project::open(fixtureCopy(fx), nullptr);
        if (!p)
            continue;
        const QString dir = ctx().work + QStringLiteral("/backups-") + fx;
        QDir().mkpath(dir);
        QString out, err;
        QElapsedTimer t;
        t.start();
        const bool ok = backup::backupProject(p.get(), dir, 3, &out, &err);
        fact(QStringLiteral("C-09 Back Up Now of %1 (ms)").arg(fx), int(t.elapsed()));
        check(ok && QFileInfo::exists(out), QStringLiteral("%1: the backup is made").arg(fx), err);
        ZipReader z(out);
        check(!z.fileNames().isEmpty(), QStringLiteral("%1: the zip opens with the program's reader (%2 files)").arg(fx).arg(z.fileNames().size()));
        if (fx == QLatin1String("fx1-qb")) {
            for (int i = 0; i < 6; ++i) {
                QTest::qWait(1100);
                backup::backupProject(p.get(), dir, 3, &out, &err);
            }
            check(backup::backupsFor(p.get(), dir).size() == 3, QStringLiteral("6 more backups with a keep count of 3: 3 remain"),
                  QString::number(backup::backupsFor(p.get(), dir).size()));
        }
    }
}

// ================================================================ C-10 compile engine, every built-in exporter
void caseC10()
{
    auto p = Project::open(fixtureCopy(QStringLiteral("fx1-qb")), nullptr);
    if (!p)
        return;
    QString err;
    const QString first = p->docs()->plainText(itemByTitle(p.get(), QStringLiteral("Scene 1"))->id).left(40).simplified();
    for (const QString &kind : {QStringLiteral("pdf"), QStringLiteral("epub"), QStringLiteral("docx"), QStringLiteral("fountain"), QStringLiteral("fdx")}) {
        step(QStringLiteral("compile to %1").arg(kind));
        const QString file = ctx().dir + QStringLiteral("/c10.") + kind;
        const QString got = compileTo(p.get(), kind, file, &err);
        check(!got.isEmpty() && QFileInfo(file).size() > 0, QStringLiteral("%1: written, not empty").arg(kind), err);
        if (kind == QLatin1String("pdf")) {
            const QString t = runTool(QStringLiteral("pdftotext"), {file, QStringLiteral("-")}).simplified();
            check(t.contains(first.left(25)), QStringLiteral("PDF: holds the first sentence of the first scene"), first.left(25));
        } else if (kind == QLatin1String("docx"))
            check(zipHas(file, QStringLiteral("word/document.xml")), QStringLiteral("Word: a zip with word/document.xml"));
        else if (kind == QLatin1String("epub")) {
            check(zipHas(file, QStringLiteral("mimetype")), QStringLiteral("e-book: a zip with its mimetype"));
            const QString jar = QStringLiteral("/usr/share/java/epubcheck.jar");
            if (QFileInfo::exists(jar)) {
                int rc = 0;
                const QString res = runTool(QStringLiteral("java"), {QStringLiteral("-jar"), jar, file}, &rc, 180000);
                check(rc == 0, QStringLiteral("e-book: epubcheck passes [L]"), res.right(400));
            }
        }
    }
    // edge manuscripts
    step(QStringLiteral("edge manuscripts"));
    for (BinderItem *it : p->allItems())
        it->includeInCompile = false;
    const QString none = compileTo(p.get(), QStringLiteral("pdf"), ctx().dir + QStringLiteral("/c10-none.pdf"), &err);
    fact(QStringLiteral("C-10 compile with every item left out"), none.isEmpty() ? QStringLiteral("refused: ") + err : QStringLiteral("a file was written"));
    for (BinderItem *it : p->allItems())
        it->includeInCompile = true;
    BinderItem *big = addText(p.get(), nullptr, QStringLiteral("<&>\"' Title / with \\ special: chars?*"), atParagraphs(1010, 60000));
    Q_UNUSED(big);
    QElapsedTimer t;
    t.start();
    const QString withBig = compileTo(p.get(), QStringLiteral("docx"), ctx().dir + QStringLiteral("/c10-big.docx"), &err);
    check(!withBig.isEmpty(), QStringLiteral("a document of 60,000 words and a title of special characters compile to Word"), err);
    fact(QStringLiteral("C-10 Word with a 60,000-word document (ms)"), int(t.elapsed()));
    // the exporters that plugins bring
    QStringList names;
    for (auto *ex : PluginManager::instance().exporters())
        names << ex->name();
    fact(QStringLiteral("C-10 exporters offered"), names.join(QLatin1String(", ")));
}

} // namespace cs
