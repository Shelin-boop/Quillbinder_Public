// Headless checks for projects in Scrivener 3 format (import/ScrivenerFormat.h):
// writing a .scriv, working on one through a working copy, changes made
// elsewhere, converting both ways. Part of qb_tests; scrivenerChecks() returns
// the number of failed checks.
//
// QB_SCRIV_SAMPLE=<folder>.scriv runs the checks on a real Scrivener project as
// well (the one used for 0.14.0: tests/sample-project.scriv of
// github.com/writerslogic/scrivener-mcp, Scrivener Mac 3.4, not part of this
// source because of its licence). Without it, the same checks run on a project
// built here after its structure.
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/Snapshots.h"
#include "import/ScrivenerFormat.h"
#include "import/ScrivenerImporter.h"
#include "text/MarkdownIO.h"
#include "text/RtfIO.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextFragment>
#include <QUuid>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextDocument>
#include <cstdio>

using namespace qb;

namespace {

int failed = 0;
void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failed;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail.left(800)));
    }
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void writeAll(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
}

QString sha1(const QByteArray &b) { return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex()); }

// path relative to the .scriv -> SHA-1, for everything below Files/Data and Snapshots
QMap<QString, QString> dataHashes(const QString &scriv)
{
    QMap<QString, QString> out;
    for (const QString &sub : {QStringLiteral("Files/Data"), QStringLiteral("Snapshots")}) {
        QDirIterator it(QDir(scriv).filePath(sub), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString p = it.next();
            if (p.endsWith(QLatin1String("docs.checksum")))
                continue;
            out.insert(QDir(scriv).relativeFilePath(p), sha1(readAll(p)));
        }
    }
    return out;
}

QDomDocument scrivxDom(const QString &scriv)
{
    QDomDocument d;
    d.setContent(readAll(scrivener::scrivxIn(scriv)));
    return d;
}

QSet<QString> uuids(const QDomDocument &d)
{
    QSet<QString> out;
    const QDomNodeList l = d.elementsByTagName(QStringLiteral("BinderItem"));
    for (int i = 0; i < l.size(); ++i)
        out.insert(l.at(i).toElement().attribute(QStringLiteral("UUID")));
    return out;
}

// element and attribute names below the root (the vocabulary of a file)
void vocabulary(const QDomElement &e, QSet<QString> *names)
{
    names->insert(e.tagName());
    const QDomNamedNodeMap attrs = e.attributes();
    for (int i = 0; i < attrs.size(); ++i)
        names->insert(e.tagName() + QLatin1Char('@') + attrs.item(i).nodeName());
    for (QDomElement c = e.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
        vocabulary(c, names);
}

QDomElement itemEl(const QDomDocument &d, const QString &uuid)
{
    const QDomNodeList l = d.elementsByTagName(QStringLiteral("BinderItem"));
    for (int i = 0; i < l.size(); ++i)
        if (l.at(i).toElement().attribute(QStringLiteral("UUID")).compare(uuid, Qt::CaseInsensitive) == 0)
            return l.at(i).toElement();
    return QDomElement();
}

bool copyTree(const QString &from, const QString &to)
{
    QDir().mkpath(to);
    QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        const QString t = QDir(to).filePath(QDir(from).relativeFilePath(p));
        if (it.fileInfo().isDir())
            QDir().mkpath(t);
        else if (!QFile::copy(p, t))
            return false;
    }
    return true;
}

int countKind(const QTextDocument *d, NoteKind k)
{
    int n = 0;
    QString last;
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat cf = it.fragment().charFormat();
            if (NoteKind(cf.intProperty(props::NoteKind)) == k && cf.stringProperty(props::NoteId) != last) {
                ++n;
                last = cf.stringProperty(props::NoteId);
            }
        }
    return n;
}

void appendText(Project *p, const QString &id, const QString &text)
{
    QTextDocument *d = p->docs()->text(id);
    QTextCursor c(d);
    c.movePosition(QTextCursor::End);
    c.insertText(text);
}

// a project built after the structure of a real Scrivener 3 project
void buildSample(const QString &scriv)
{
    const QString x = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<ScrivenerProject Identifier=\"0BBEBC3E-14C5-4109-A432-97D4726AD46D\" Version=\"2.0\" Creator=\"SCRMAC-3.4-16639\" "
        "Device=\"test\" Modified=\"2025-09-03 13:31:01 -0700\" ModID=\"C7FA6F4C-0119-4968-9B61-DBA33C6599E2\">\n"
        "    <Binder>\n"
        "        <BinderItem UUID=\"4A1D6B18-0952-42EB-A487-D17915DD3BA1\" Type=\"DraftFolder\" Created=\"2025-09-02 03:48:53 -0700\" Modified=\"2025-09-02 03:48:53 -0700\">\n"
        "            <Title>Manuscript</Title>\n"
        "            <Children>\n"
        "                <BinderItem UUID=\"95A0E87E-0497-4ADA-8A51-8855420732BC\" Type=\"Text\" Created=\"2025-09-02 06:12:12 -0700\" Modified=\"2025-09-02 06:36:18 -0700\">\n"
        "                    <Title>Chapter-01</Title>\n"
        "                    <MetaData><IncludeInCompile>Yes</IncludeInCompile></MetaData>\n"
        "                    <TextSettings><TextSelection>0,0</TextSelection></TextSettings>\n"
        "                </BinderItem>\n"
        "                <BinderItem UUID=\"0BFBFA71-F1E9-401C-8A78-97B10BC12399\" Type=\"Text\" Created=\"2025-09-02 06:14:07 -0700\" Modified=\"2025-09-02 06:38:51 -0700\">\n"
        "                    <Title>Chapter-02</Title>\n"
        "                    <MetaData><StatusID>2</StatusID><IncludeInCompile>Yes</IncludeInCompile></MetaData>\n"
        "                    <TextSettings><TextSelection>0,0</TextSelection></TextSettings>\n"
        "                </BinderItem>\n"
        "            </Children>\n"
        "        </BinderItem>\n"
        "        <BinderItem UUID=\"06C0455C-36DD-4F38-BB7B-F7DD2453A8A5\" Type=\"Folder\" Created=\"2025-09-02 03:48:53 -0700\" Modified=\"2025-09-02 03:48:53 -0700\">\n"
        "            <Title>Characters</Title>\n"
        "            <MetaData><IncludeInCompile>Yes</IncludeInCompile><IconFileName>Characters (Photo)</IconFileName></MetaData>\n"
        "            <TextSettings><TextSelection>0,0</TextSelection></TextSettings>\n"
        "        </BinderItem>\n"
        "        <BinderItem UUID=\"57ECC98D-8199-4935-B5F1-D1F7A2D60646\" Type=\"Folder\" Created=\"2025-09-02 03:48:53 -0700\" Modified=\"2025-09-02 03:48:53 -0700\">\n"
        "            <Title>Front Matter</Title>\n"
        "            <MetaData><SectionType ChildDefault=\"F3D99402-96E3-4558-A2BE-FB83286017C8\"></SectionType><IncludeInCompile>Yes</IncludeInCompile></MetaData>\n"
        "            <TextSettings><TextSelection>0,0</TextSelection></TextSettings>\n"
        "            <Children>\n"
        "                <BinderItem UUID=\"684ADA52-4D45-48D2-B03D-5ECB784963EE\" Type=\"Text\" Created=\"2025-09-02 03:48:53 -0700\" Modified=\"2025-09-02 03:48:53 -0700\">\n"
        "                    <Title>Title Page</Title>\n"
        "                    <MetaData><SectionType>F3D99402-96E3-4558-A2BE-FB83286017C8</SectionType><IncludeInCompile>Yes</IncludeInCompile></MetaData>\n"
        "                    <TextSettings><TextSelection>118,0</TextSelection></TextSettings>\n"
        "                </BinderItem>\n"
        "                <BinderItem UUID=\"C88ACCC3-4FDB-4952-80E1-177623CDE141\" Type=\"Image\" Created=\"2025-09-02 03:48:53 -0700\" Modified=\"2025-09-02 03:48:53 -0700\">\n"
        "                    <Title>Cover</Title>\n"
        "                    <MetaData><IncludeInCompile>Yes</IncludeInCompile><FileExtension>png</FileExtension></MetaData>\n"
        "                    <MediaSettings><ImageScaleFactor>1.0</ImageScaleFactor></MediaSettings>\n"
        "                </BinderItem>\n"
        "            </Children>\n"
        "        </BinderItem>\n"
        "        <BinderItem UUID=\"BFCA4BFB-2E93-4067-A9AA-5505F344F096\" Type=\"ResearchFolder\" Created=\"2025-09-02 03:48:53 -0700\" Modified=\"2025-09-02 03:48:53 -0700\">\n"
        "            <Title>Research</Title>\n"
        "        </BinderItem>\n"
        "        <BinderItem UUID=\"94AB659E-F7E8-4131-B1BA-C358110EA09E\" Type=\"TrashFolder\" Created=\"2025-09-02 03:48:53 -0700\" Modified=\"2025-09-02 03:48:53 -0700\">\n"
        "            <Title>Trash</Title>\n"
        "        </BinderItem>\n"
        "    </Binder>\n"
        "    <Collections>\n"
        "        <Collection Type=\"Binder\" ID=\"48DE1CBF-217F-4B94-944D-6D8C97396DCF\" Color=\"0.901957 0.901984 0.901969\"><Title>Binder</Title></Collection>\n"
        "    </Collections>\n"
        "    <SectionTypes>\n"
        "        <TypeDefinitions>\n"
        "            <Type ID=\"417A6CCE-D4E1-44BA-9A8D-038360B4B905\">Chapter Heading</Type>\n"
        "            <Type ID=\"9AFECE21-8C79-45E0-9B9F-D6D62A9729D3\">Scene</Type>\n"
        "            <Type ID=\"F3D99402-96E3-4558-A2BE-FB83286017C8\">Front Matter</Type>\n"
        "        </TypeDefinitions>\n"
        "        <LevelTypes>\n"
        "            <Folders><Type>417A6CCE-D4E1-44BA-9A8D-038360B4B905</Type></Folders>\n"
        "            <Containers><Type>9AFECE21-8C79-45E0-9B9F-D6D62A9729D3</Type></Containers>\n"
        "            <Files><Type>9AFECE21-8C79-45E0-9B9F-D6D62A9729D3</Type></Files>\n"
        "        </LevelTypes>\n"
        "    </SectionTypes>\n"
        "    <LabelSettings><Title>Label</Title><DefaultLabelID>-1</DefaultLabelID><Labels><Label ID=\"-1\">No Label</Label>"
        "<Label ID=\"7\" Color=\"0.993495 0.701207 0.732587\">Red</Label></Labels></LabelSettings>\n"
        "    <StatusSettings><Title>Status</Title><DefaultStatusID>-1</DefaultStatusID><StatusItems><Status ID=\"-1\">No Status</Status>"
        "<Status ID=\"2\">First Draft</Status></StatusItems></StatusSettings>\n"
        "    <ProjectBookmarks><Bookmark BinderUUID=\"06C0455C-36DD-4F38-BB7B-F7DD2453A8A5\"/></ProjectBookmarks>\n"
        "    <ProjectTargets Notify=\"No\"><DraftTarget Type=\"Words\" CountIncludedOnly=\"Yes\">0</DraftTarget>"
        "<SessionTarget Type=\"Words\" CountDraftOnly=\"Yes\">0</SessionTarget></ProjectTargets>\n"
        "    <TemplateFolderUUID>06C0455C-36DD-4F38-BB7B-F7DD2453A8A5</TemplateFolderUUID>\n"
        "    <PrintSettings PaperSize=\"612.0,792.0\" LeftMargin=\"72.0\"/>\n"
        "</ScrivenerProject>\n");
    writeAll(scriv + QLatin1String("/sample-project.scrivx"), x.toUtf8());
    const QByteArray head = "{\\rtf1\\ansi\\ansicpg1252\\cocoartf2864\n\\cocoatextscaling0\\cocoaplatform0{\\fonttbl\\f0\\fswiss\\fcharset0 Helvetica;}\n"
                            "{\\colortbl;\\red255\\green255\\blue255;}\n{\\*\\expandedcolortbl;;\\csgray\\c100000;}\n"
                            "\\pard\\tx560\\tx1120\\sl288\\slmult1\\pardirnatural\n\n\\f0\\fs24 \\cf0 ";
    const QString d = scriv + QLatin1String("/Files/Data/");
    writeAll(d + QLatin1String("95A0E87E-0497-4ADA-8A51-8855420732BC/content.rtf"),
             head + "Lorem ipsum dolor sit amet.\\\n\\\nDuis aute irure dolor.}");
    writeAll(d + QLatin1String("0BFBFA71-F1E9-401C-8A78-97B10BC12399/content.rtf"),
             head + "Test List:\\\n\\'95 Item one\\\nAdditional notes after the list.}");
    writeAll(d + QLatin1String("684ADA52-4D45-48D2-B03D-5ECB784963EE/content.rtf"), head + "<$Scr_H::1>Title Page<!$Scr_H::1>}");
    writeAll(d + QLatin1String("684ADA52-4D45-48D2-B03D-5ECB784963EE/synopsis.txt"), "The title page.");
    writeAll(d + QLatin1String("684ADA52-4D45-48D2-B03D-5ECB784963EE/notes.rtf"), head + "Note that its section type is Front Matter.}");
    QImage img(20, 10, QImage::Format_RGB32);
    img.fill(Qt::darkGreen);
    QDir().mkpath(d + QLatin1String("C88ACCC3-4FDB-4952-80E1-177623CDE141"));
    img.save(d + QLatin1String("C88ACCC3-4FDB-4952-80E1-177623CDE141/content.png"));
    writeAll(d + QLatin1String("docs.checksum"), "95a0e87e-0497-4ada-8a51-8855420732bc/content.rtf=0000\n");
    writeAll(scriv + QLatin1String("/Files/version.txt"), "23");
    const QString sn = scriv + QLatin1String("/Snapshots/684ADA52-4D45-48D2-B03D-5ECB784963EE.snapshots/");
    writeAll(sn + QLatin1String("2025-10-27-02-20-40-0700.rtf"), head + "First draft text.}");
    writeAll(sn + QLatin1String("2025-10-27-09-15-12-0700.rtf"), head + "Second text.}");
    writeAll(sn + QLatin1String("index.xml"),
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Snapshots Version=\"1.0\">\n"
             "    <Snapshot><Title>First draft</Title><Date>2025-10-27 02:20:40 -0700</Date></Snapshot>\n"
             "    <Snapshot><Title>Before rewrite</Title><Date>2025-10-27 09:15:12 -0700</Date></Snapshot>\n</Snapshots>\n");
    writeAll(scriv + QLatin1String("/Settings/ui-common.xml"), "<UIStates/>");
}

void runOn(const QString &origin, const QString &label)
{
    QTemporaryDir tmp;
    const QString scriv = tmp.filePath(QStringLiteral("Lighthouse.scriv"));
    copyTree(origin, scriv);
    QFile::remove(scriv + QLatin1String("/Files/user.lock"));
    auto L = [&](const QString &s) { return QStringLiteral("[%1] ").arg(label) + s; };

    const QDomDocument original = scrivxDom(scriv);
    QSet<QString> sampleVocab;
    vocabulary(original.documentElement(), &sampleVocab);
    const QMap<QString, QString> before = dataHashes(scriv);

    // --- importer: what the real files hold
    {
        ScrivenerImportReport rep;
        QString err;
        auto p = ScrivenerImporter::importProject(scriv, tmp.filePath(QStringLiteral("imp")), QStringLiteral("Imp"),
                                                  StorageFormat::Markdown, &rep, &err);
        QDir().mkpath(tmp.filePath(QStringLiteral("imp")));
        if (!p)
            p = ScrivenerImporter::importProject(scriv, tmp.filePath(QStringLiteral("imp")), QStringLiteral("Imp"),
                                                 StorageFormat::Markdown, &rep, &err);
        check(p != nullptr, L(QStringLiteral("import: the project is read")), err);
        if (!p)
            return;
        auto idOfType = [&](const QString &type) {
            const QDomNodeList l = original.elementsByTagName(QStringLiteral("BinderItem"));
            for (int i = 0; i < l.size(); ++i)
                if (l.at(i).toElement().attribute(QStringLiteral("Type")) == type)
                    return l.at(i).toElement().attribute(QStringLiteral("UUID")).toUpper();
            return QString();
        };
        check(p->draft()->id == idOfType(QStringLiteral("DraftFolder")) && p->research()->id == idOfType(QStringLiteral("ResearchFolder"))
                  && p->trash()->id == idOfType(QStringLiteral("TrashFolder")),
              L(QStringLiteral("import: Draft, Research and Trash keep Scrivener's ids")));
        bool frontMatter = false;
        for (const SectionType &t : p->sectionTypes)
            frontMatter |= t.name == QLatin1String("Front Matter") && t.id == QLatin1String("F3D99402-96E3-4558-A2BE-FB83286017C8");
        check(frontMatter, L(QStringLiteral("import: section types from SectionTypes/TypeDefinitions, with their ids")));
        BinderItem *tp = p->item(QStringLiteral("684ADA52-4D45-48D2-B03D-5ECB784963EE"));
        {
            // every document with a section type of its own gets it
            const QDomNodeList l = original.elementsByTagName(QStringLiteral("SectionType"));
            int with = 0, right = 0;
            for (int i = 0; i < l.size(); ++i) {
                const QDomElement st = l.at(i).toElement();
                if (st.parentNode().nodeName() != QLatin1String("MetaData") || st.text().trimmed().isEmpty())
                    continue;
                ++with;
                const QString uuid = st.parentNode().parentNode().toElement().attribute(QStringLiteral("UUID")).toUpper();
                if (p->item(uuid) && p->item(uuid)->sectionType == st.text().trimmed().toUpper())
                    ++right;
            }
            check(right == with, L(QStringLiteral("import: the section type of a document (MetaData/SectionType): %1 of %2").arg(right).arg(with)));
        }
        check(!p->structureDefaults.folders.isEmpty() && !p->structureDefaults.files.isEmpty(),
              L(QStringLiteral("import: default types by structure from LevelTypes")));
        check(!p->projectBookmarks.isEmpty() && p->projectBookmarks.first().isInternal(),
              L(QStringLiteral("import: project bookmarks (Bookmark BinderUUID)")));
        check(p->templatesFolderId == original.documentElement().firstChildElement(QStringLiteral("TemplateFolderUUID")).text().trimmed().toUpper(),
              L(QStringLiteral("import: the template folder (TemplateFolderUUID)")));
        if (tp) {
            const QList<Snapshot> snaps = snapshots::list(p.get(), tp->id);
            QStringList titles;
            for (const Snapshot &s : snaps)
                titles << s.title;
            check(titles.contains(QStringLiteral("First draft")) && titles.contains(QStringLiteral("Before rewrite")),
                  L(QStringLiteral("import: snapshot titles from index.xml (by date)")), titles.join(QLatin1String(", ")));
            const QString t = p->docs()->plainText(tp->id);
            check(!t.contains(QLatin1String("$Scr")) && !t.contains(QLatin1String("<!")),
                  L(QStringLiteral("import: Scrivener's style markers are not text")), t);
        }
    }

    // --- open in Scrivener format: a working copy
    QString err;
    scrivener::OpenResult r = scrivener::open(scriv, &err);
    check(r.project && r.reread, L(QStringLiteral("open: a working copy is made from the .scriv")), err);
    if (!r.project)
        return;
    Project *p = r.project.get();
    check(p->scrivenerPath == QFileInfo(scriv).absoluteFilePath() && p->folderPath().startsWith(scrivener::workingCopiesDir()),
          L(QStringLiteral("open: the working copy knows its .scriv")));
    check(p->displayFile() == scrivener::scrivxIn(scriv), L(QStringLiteral("open: the project is shown as its .scrivx")));

    // saving without a change writes nothing of the texts
    p->markIndexDirty();
    check(p->save(&err), L(QStringLiteral("save: works")), err);
    const QMap<QString, QString> after = dataHashes(scriv);
    QStringList changed;
    for (auto it = before.cbegin(); it != before.cend(); ++it)
        if (after.value(it.key()) != it.value())
            changed << it.key();
    check(changed.isEmpty(), L(QStringLiteral("save without changes: every file Scrivener wrote stays byte for byte")),
          changed.join(QLatin1String(", ")));
    QDomDocument d1 = scrivxDom(scriv);
    check(uuids(d1) == uuids(original), L(QStringLiteral("save: the binder keeps every item and id")));
    check(d1.documentElement().attribute(QStringLiteral("Creator")) == original.documentElement().attribute(QStringLiteral("Creator"))
              && d1.documentElement().attribute(QStringLiteral("Identifier"))
                     == original.documentElement().attribute(QStringLiteral("Identifier"))
              && d1.documentElement().attribute(QStringLiteral("Version")) == QLatin1String("2.0"),
          L(QStringLiteral("save: Identifier, Creator and Version stay")));
    {
        QSet<QString> v;
        vocabulary(d1.documentElement(), &v);
        QStringList lost;
        for (const QString &n : sampleVocab)
            if (!v.contains(n) && !n.startsWith(QLatin1String("ScrivenerProject@")))
                lost << n;
        check(lost.isEmpty(), L(QStringLiteral("save: no element or attribute of the original .scrivx is lost")), lost.join(QLatin1String(", ")));
        QStringList invented;
        for (const QString &n : v)
            if (!sampleVocab.contains(n))
                invented << n;
        invented.removeAll(QStringLiteral("LabelID")); // seen in a .scrivx quoted by Literature & Latte's forum
        check(invented.isEmpty(),
              L(QStringLiteral("save: nothing is written that the real project does not have")), invented.join(QLatin1String(", ")));
    }
    check(!itemEl(d1, QStringLiteral("06C0455C-36DD-4F38-BB7B-F7DD2453A8A5"))
               .firstChildElement(QStringLiteral("MetaData"))
               .firstChildElement(QStringLiteral("IconFileName"))
               .isNull(),
          L(QStringLiteral("save: things only Scrivener knows (icons) stay")));
    check(!d1.documentElement().firstChildElement(QStringLiteral("PrintSettings")).isNull(),
          L(QStringLiteral("save: Scrivener's own settings (PrintSettings) stay")));
    check(!QFile::exists(scriv + QLatin1String("/Files/binder.autosave")), L(QStringLiteral("save: the old binder.autosave is removed")));

    // --- an edit in this program: only that document is written
    const QString ch1 = QStringLiteral("95A0E87E-0497-4ADA-8A51-8855420732BC");
    appendText(p, ch1, QStringLiteral(" The keeper counted the ships."));
    check(p->save(&err), L(QStringLiteral("edit: saves")), err);
    {
        const QMap<QString, QString> a2 = dataHashes(scriv);
        QStringList ch;
        for (auto it = a2.cbegin(); it != a2.cend(); ++it)
            if (before.value(it.key()) != it.value())
                ch << it.key();
        check(ch == QStringList{QStringLiteral("Files/Data/") + ch1 + QStringLiteral("/content.rtf")},
              L(QStringLiteral("edit: only the changed document's content.rtf is written")), ch.join(QLatin1String(", ")));
        const QByteArray rtf = readAll(scriv + QLatin1String("/Files/Data/") + ch1 + QLatin1String("/content.rtf"));
        check(rtf.startsWith("{\\rtf1") && rtf.contains("counted the ships"), L(QStringLiteral("edit: the new text is RTF")));
        const QByteArray cs = readAll(scriv + QLatin1String("/Files/Data/docs.checksum"));
        check(cs.contains((ch1.toLower() + QStringLiteral("/content.rtf=") + sha1(rtf)).toUtf8()),
              L(QStringLiteral("edit: docs.checksum has the SHA-1 of the new file (lower-case id)")));
        check(readAll(scriv + QLatin1String("/Files/search.indexes")).contains("counted the ships"),
              L(QStringLiteral("edit: Files/search.indexes has the new text")));
        // the binder keeps Scrivener's order (also the top level, where documents may stand before
        // the Draft) and the dates of the other items keep their text (found in the 0.14.0 end-to-end run)
        const QDomDocument d2 = scrivxDom(scriv);
        auto order = [](const QDomDocument &d) {
            QStringList out;
            const QDomNodeList l = d.documentElement().firstChildElement(QStringLiteral("Binder")).elementsByTagName(QStringLiteral("BinderItem"));
            for (int i = 0; i < l.size(); ++i)
                out << l.at(i).toElement().attribute(QStringLiteral("UUID"));
            return out;
        };
        check(order(d2) == order(original), L(QStringLiteral("edit: the binder of the .scrivx keeps its order")),
              order(d2).mid(0, 3).join(QLatin1String(", ")) + QLatin1String(" / ") + order(original).mid(0, 3).join(QLatin1String(", ")));
        QStringList datesChanged;
        const QDomNodeList l2 = d2.documentElement().firstChildElement(QStringLiteral("Binder")).elementsByTagName(QStringLiteral("BinderItem"));
        for (int i = 0; i < l2.size(); ++i) {
            const QDomElement e2 = l2.at(i).toElement();
            const QString id = e2.attribute(QStringLiteral("UUID"));
            const QDomElement e0 = itemEl(original, id);
            for (const QString &a : {QStringLiteral("Created"), QStringLiteral("Modified")})
                if (id != ch1 && !e0.isNull() && e0.attribute(a) != e2.attribute(a))
                    datesChanged << id.left(8) + QLatin1Char(' ') + a + QLatin1Char(' ') + e2.attribute(a);
        }
        check(datesChanged.isEmpty(), L(QStringLiteral("edit: the dates of the other items stay as Scrivener wrote them")),
              datesChanged.mid(0, 5).join(QLatin1String("; ")));
    }

    // --- a new document with notes of every kind
    BinderItem *nd = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Night Watch"));
    {
        QTextDocument *doc = p->docs()->text(nd->id);
        QTextCursor c(doc);
        c.insertText(QStringLiteral("The lamp "));
        c.insertText(QStringLiteral("flickered"), text::noteFormat(NoteKind::Comment, QStringLiteral("c1"), QStringLiteral("Too plain?")));
        c.insertText(QStringLiteral(" twice"), QTextCharFormat());
        c.insertText(QStringLiteral(" "), QTextCharFormat());
        c.insertText(QStringLiteral("check the log"), text::noteFormat(NoteKind::InlineAnnotation, QStringLiteral("a1")));
        c.insertText(QStringLiteral(" and the sea"), QTextCharFormat());
        c.insertText(QStringLiteral("Calm that night."), text::noteFormat(NoteKind::InlineFootnote, QStringLiteral("f1")));
        c.insertText(QStringLiteral("."), QTextCharFormat());
    }
    nd->synopsis = QStringLiteral("The lamp fails.");
    nd->label = 7;
    p->notifyItemChanged(nd);
    check(p->save(&err), L(QStringLiteral("new document: saves")), err);
    {
        const QString base = scriv + QLatin1String("/Files/Data/") + nd->id + QLatin1Char('/');
        const QByteArray rtf = readAll(base + QLatin1String("content.rtf"));
        check(rtf.count("scrivcmt://") == 3, L(QStringLiteral("notes: three HYPERLINK \"scrivcmt://\" anchors")), QString::fromUtf8(rtf.right(400)));
        check(!rtf.contains("Calm that night") && !rtf.contains("check the log"),
              L(QStringLiteral("notes: inline notes leave the text and become linked notes")));
        QDomDocument cd;
        check(cd.setContent(readAll(base + QLatin1String("content.comments"))), L(QStringLiteral("notes: content.comments is XML")));
        const QDomNodeList cs = cd.elementsByTagName(QStringLiteral("Comment"));
        int fns = 0;
        QStringList texts;
        for (int i = 0; i < cs.size(); ++i) {
            const QDomElement e = cs.at(i).toElement();
            fns += e.attribute(QStringLiteral("Footnote")) == QLatin1String("Yes");
            texts << rtf::toPlainText(e.text().toUtf8()).trimmed();
            check(rtf.contains(("scrivcmt://" + e.attribute(QStringLiteral("ID"))).toUtf8()),
                  L(QStringLiteral("notes: comment %1 has its anchor in the text").arg(i + 1)));
        }
        check(cs.size() == 3 && fns == 1, L(QStringLiteral("notes: two comments and one footnote")), QString::number(cs.size()));
        check(texts.contains(QStringLiteral("Too plain?")) && texts.contains(QStringLiteral("check the log"))
                  && texts.contains(QStringLiteral("Calm that night.")),
              L(QStringLiteral("notes: their texts")), texts.join(QLatin1String(" | ")));
        check(QString::fromUtf8(readAll(base + QLatin1String("synopsis.txt"))) == QLatin1String("The lamp fails."),
              L(QStringLiteral("new document: synopsis.txt")));
        const QDomDocument d2 = scrivxDom(scriv);
        const QDomElement e = itemEl(d2, nd->id);
        check(!e.isNull() && e.attribute(QStringLiteral("Type")) == QLatin1String("Text")
                  && e.firstChildElement(QStringLiteral("Title")).text() == QLatin1String("Night Watch")
                  && e.firstChildElement(QStringLiteral("MetaData")).firstChildElement(QStringLiteral("LabelID")).text() == QLatin1String("7")
                  && e.parentNode().parentNode().toElement().attribute(QStringLiteral("Type")) == QLatin1String("DraftFolder"),
              L(QStringLiteral("new document: in the binder with title, label, under the Draft")));
        check(e.attribute(QStringLiteral("Created")).contains(QRegularExpression(QStringLiteral("^\\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d [+-]\\d{4}$"))),
              L(QStringLiteral("new document: dates as Scrivener writes them")), e.attribute(QStringLiteral("Created")));
    }

    // --- a snapshot taken here goes to Scrivener's Snapshots folder
    {
        check(snapshots::take(p, ch1, QStringLiteral("Before the storm"), &err), L(QStringLiteral("snapshot: taken")), err);
        p->item(ch1)->hasSnapshots = true;
        p->markIndexDirty();
        p->save(&err);
        const QString sd = scriv + QLatin1String("/Snapshots/") + ch1 + QLatin1String(".snapshots/");
        const QStringList files = QDir(sd).entryList({QStringLiteral("*.rtf")}, QDir::Files);
        const QByteArray idx = readAll(sd + QLatin1String("index.xml"));
        check(files.size() == 1 && files.first().contains(QRegularExpression(QStringLiteral("^\\d{4}-\\d\\d-\\d\\d-\\d\\d-\\d\\d-\\d\\d[+-]\\d{4}\\.rtf$"))),
              L(QStringLiteral("snapshot: a file named after its date")), files.join(QLatin1String(", ")));
        check(idx.contains("<Title>Before the storm</Title>") && idx.contains("<Date>"), L(QStringLiteral("snapshot: index.xml with title and date")));
    }

    // --- reopening without changes elsewhere: the working copy as it is
    p->uiState.insert(QStringLiteral("testMarker"), QStringLiteral("kept"));
    nd->scriptMode = false;
    BinderItem *chars = p->item(QStringLiteral("06C0455C-36DD-4F38-BB7B-F7DD2453A8A5"));
    Keyword kw;
    kw.id = newId();
    kw.name = QStringLiteral("Lighthouse");
    p->keywords << kw;
    chars->keywords << kw.id;
    p->markIndexDirty();
    p->save(&err);
    const QString nightId = nd->id;
    r.project.reset();
    {
        scrivener::OpenResult r2 = scrivener::open(scriv, &err);
        check(r2.project && !r2.reread, L(QStringLiteral("reopen: unchanged, the working copy is used as it is")), err);
        if (r2.project) {
            check(r2.project->uiState.value(QStringLiteral("testMarker")).toString() == QLatin1String("kept"),
                  L(QStringLiteral("reopen: what only this program knows is there")));
            const QTextDocument *nd2 = r2.project->docs()->text(nightId);
            check(countKind(nd2, NoteKind::InlineAnnotation) == 1 && countKind(nd2, NoteKind::InlineFootnote) == 1
                      && countKind(nd2, NoteKind::Comment) == 1,
                  L(QStringLiteral("reopen: the notes are as they were in this program (inline notes stay inline)")));
        }
    }

    // --- changed in Scrivener meanwhile
    writeAll(scriv + QLatin1String("/Files/Data/0BFBFA71-F1E9-401C-8A78-97B10BC12399/content.rtf"),
             "{\\rtf1\\ansi\\ansicpg1252\\cocoartf2864\n{\\fonttbl\\f0\\fswiss\\fcharset0 Helvetica;}\n\\f0\\fs24 Rewritten in Scrivener.}");
    {
        QByteArray x = readAll(scrivener::scrivxIn(scriv));
        x.replace("<Title>Chapter-01</Title>", "<Title>Chapter One</Title>");
        writeAll(scrivener::scrivxIn(scriv), x);
    }
    {
        scrivener::OpenResult r3 = scrivener::open(scriv, &err);
        check(r3.project && r3.reread && r3.changedElsewhere == 1,
              L(QStringLiteral("changed elsewhere: the .scriv is read again, one text came from it")),
              QStringLiteral("%1 %2 %3").arg(bool(r3.project)).arg(r3.reread).arg(r3.changedElsewhere));
        if (r3.project) {
            Project *q = r3.project.get();
            check(q->docs()->plainText(QStringLiteral("0BFBFA71-F1E9-401C-8A78-97B10BC12399")).contains(QLatin1String("Rewritten in Scrivener")),
                  L(QStringLiteral("changed elsewhere: the new text")));
            check(q->item(ch1) && q->item(ch1)->title == QLatin1String("Chapter One"), L(QStringLiteral("changed elsewhere: the new title")));
            check(q->docs()->plainText(ch1).contains(QLatin1String("counted the ships")),
                  L(QStringLiteral("changed elsewhere: texts changed only here keep this program's version")));
            check(q->uiState.value(QStringLiteral("testMarker")).toString() == QLatin1String("kept"),
                  L(QStringLiteral("changed elsewhere: the window layout and the like of the last time stay")));
            BinderItem *c2 = q->item(QStringLiteral("06C0455C-36DD-4F38-BB7B-F7DD2453A8A5"));
            check(c2 && c2->keywords.size() == 1 && q->keywordById(c2->keywords.first()),
                  L(QStringLiteral("changed elsewhere: keywords (not written to the .scriv) stay")));
            check(countKind(q->docs()->text(nightId), NoteKind::InlineFootnote) == 1,
                  L(QStringLiteral("changed elsewhere: the notes of an untouched text stay as they were")));

            // deleted for good: its folder leaves the .scriv
            BinderItem *gone = q->item(nightId);
            q->model()->moveToTrash({gone});
            q->model()->emptyTrash();
            q->markIndexDirty();
            check(q->save(&err), L(QStringLiteral("empty trash: saves")), err);
            check(!QDir(scriv + QLatin1String("/Files/Data/") + nightId).exists() && itemEl(scrivxDom(scriv), nightId).isNull(),
                  L(QStringLiteral("empty trash: the document's folder and binder entry are gone")),
                  QStringLiteral("folder %1, in binder %2").arg(QDir(scriv + QLatin1String("/Files/Data/") + nightId).exists())
                      .arg(!itemEl(scrivxDom(scriv), nightId).isNull()));
            check(!readAll(scriv + QLatin1String("/Files/Data/docs.checksum")).contains(nightId.toLower().toUtf8()),
                  L(QStringLiteral("empty trash: docs.checksum forgets it")));
            // a copy in this program's own format
            auto copy = scrivener::copyAsQuillbinder(q, tmp.path(), QStringLiteral("Own Copy"), &err);
            check(copy && copy->scrivenerPath.isEmpty() && QFileInfo(copy->folderPath()).fileName() == QLatin1String("Own Copy.quillbinder")
                      && copy->docs()->plainText(ch1).contains(QLatin1String("counted the ships")),
                  L(QStringLiteral("copy as own project: a separate project, not linked to the .scriv")), err);
            check(copy && !readAll(copy->indexPath()).contains("scrivener="), L(QStringLiteral("copy as own project: no link in its project file")));
        }
    }
}

} // namespace

int scrivenerChecks()
{
    failed = 0;
    // working copies of this run in a folder of their own
    QTemporaryDir data;
    qputenv("XDG_DATA_HOME", data.path().toUtf8());

    QTemporaryDir built;
    buildSample(built.filePath(QStringLiteral("sample-project.scriv")));
    runOn(built.filePath(QStringLiteral("sample-project.scriv")), QStringLiteral("built"));
    const QString real = qEnvironmentVariable("QB_SCRIV_SAMPLE");
    if (!real.isEmpty() && QDir(real).exists())
        runOn(real, QStringLiteral("real Scrivener 3.4 project"));
    else
        std::printf("      (QB_SCRIV_SAMPLE not set: the real Scrivener project was not used)\n");

    // ---------------------------------------------------------------- converting a project of ours
    {
        QTemporaryDir tmp;
        QString err;
        auto p = Project::create(tmp.path(), QStringLiteral("Harbour"), &err);
        BinderItem *ch = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Chapter 1"));
        BinderItem *sc = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("Scene 1"));
        appendText(p.get(), sc->id, QStringLiteral("Ships came in at dawn."));
        sc->status = 1;
        BinderItem *pic = p->model()->createItem(p->research(), -1, ItemType::Image, QStringLiteral("Map"));
        pic->fileExt = QStringLiteral("png");
        QDir().mkpath(p->contentDir(pic->id));
        QImage img(30, 20, QImage::Format_RGB32);
        img.fill(Qt::blue);
        img.save(p->mediaFilePath(pic));
        BinderItem *audio = p->model()->createItem(p->research(), -1, ItemType::Media, QStringLiteral("Interview"));
        audio->fileExt = QStringLiteral("mp3");
        writeAll(p->mediaFilePath(audio), QByteArray("ID3 fake mp3 data"));
        p->projectBookmarks << Bookmark{sc->title, QStringLiteral("qb://") + sc->id};
        p->targets.draftTarget = 50000;
        p->markIndexDirty();
        p->save(&err);
        const QString origFolder = p->folderPath();
        const QByteArray origIndex = readAll(p->indexPath());

        const QString scriv = tmp.filePath(QStringLiteral("Harbour.scriv"));
        auto wc = scrivener::convertToScrivener(p.get(), scriv, &err);
        check(wc != nullptr, QStringLiteral("convert to Scrivener format: works"), err);
        if (wc) {
            check(readAll(origIndex.isEmpty() ? QString() : p->indexPath()) == origIndex && QDir(origFolder).exists(),
                  QStringLiteral("convert: the original project stays as it was"));
            QDomDocument d = scrivxDom(scriv);
            const QDomElement root = d.documentElement();
            check(root.tagName() == QLatin1String("ScrivenerProject") && root.attribute(QStringLiteral("Version")) == QLatin1String("2.0")
                      && !QUuid::fromString(root.attribute(QStringLiteral("Identifier"))).isNull(),
                  QStringLiteral("convert: a .scrivx in format 2.0 with an Identifier"));
            QStringList types;
            for (QDomElement e = root.firstChildElement(QStringLiteral("Binder")).firstChildElement(); !e.isNull(); e = e.nextSiblingElement())
                types << e.attribute(QStringLiteral("Type"));
            check(types.contains(QStringLiteral("DraftFolder")) && types.contains(QStringLiteral("ResearchFolder"))
                      && types.contains(QStringLiteral("TrashFolder")),
                  QStringLiteral("convert: Draft, Research and Trash (Scrivener does not open a project without the Trash)"),
                  types.join(QLatin1String(", ")));
            check(QString::fromUtf8(readAll(scriv + QLatin1String("/Files/version.txt"))) == QLatin1String("23"),
                  QStringLiteral("convert: Files/version.txt says 23"));
            check(readAll(scriv + QLatin1String("/Files/Data/") + sc->id + QLatin1String("/content.rtf")).contains("Ships came in at dawn"),
                  QStringLiteral("convert: the text as content.rtf"));
            check(QFile::exists(scriv + QLatin1String("/Files/Data/") + pic->id + QLatin1String("/content.png"))
                      && itemEl(d, pic->id).attribute(QStringLiteral("Type")) == QLatin1String("Image"),
                  QStringLiteral("convert: a picture as Image with content.png"));
            check(QFile::exists(scriv + QLatin1String("/Files/Data/") + audio->id + QLatin1String("/content.mp3"))
                      && itemEl(d, audio->id).attribute(QStringLiteral("Type")) == QLatin1String("Text")
                      && readAll(scriv + QLatin1String("/Files/Data/") + audio->id + QLatin1String("/content.rtf")).contains("content.mp3"),
                  QStringLiteral("convert: an audio file as a text that names it, the file beside it"));
            check(!root.firstChildElement(QStringLiteral("LabelSettings")).isNull()
                      && root.firstChildElement(QStringLiteral("StatusSettings")).elementsByTagName(QStringLiteral("Status")).size()
                             == p->statuses.size() + 1,
                  QStringLiteral("convert: labels and statuses with \"No Status\""));
            check(root.firstChildElement(QStringLiteral("ProjectBookmarks")).firstChildElement().attribute(QStringLiteral("BinderUUID")) == sc->id,
                  QStringLiteral("convert: project bookmarks"));
            check(root.firstChildElement(QStringLiteral("ProjectTargets")).firstChildElement(QStringLiteral("DraftTarget")).text() == QLatin1String("50000"),
                  QStringLiteral("convert: targets"));
            check(QFile::exists(scriv + QLatin1String("/Settings/projectpreferences.xml"))
                      && QFile::exists(scriv + QLatin1String("/Files/search.indexes")),
                  QStringLiteral("convert: project preferences and the search index"));
            // the converted .scriv read by the importer, as a stranger would
            ScrivenerImportReport rep;
            QDir().mkpath(tmp.filePath(QStringLiteral("back")));
            auto back = ScrivenerImporter::importProject(scriv, tmp.filePath(QStringLiteral("back")), QStringLiteral("Back"),
                                                         StorageFormat::Markdown, &rep, &err);
            check(back && back->item(sc->id) && back->item(sc->id)->parent && back->item(sc->id)->parent->title == QLatin1String("Chapter 1")
                      && back->docs()->plainText(sc->id).contains(QLatin1String("Ships came in at dawn"))
                      && back->item(sc->id)->status == 1,
                  QStringLiteral("convert and import: structure, text and status come back"), err);
            check(back && back->item(audio->id) && back->item(audio->id)->type == ItemType::Media
                      && readAll(back->mediaFilePath(back->item(audio->id))) == QByteArray("ID3 fake mp3 data"),
                  QStringLiteral("convert and import: the audio file comes back as audio"));
            check(back && back->item(pic->id) && back->item(pic->id)->type == ItemType::Image,
                  QStringLiteral("convert and import: the picture"));
            // a copy in Scrivener format leaves the project alone
            scrivener::WriteReport wr;
            const QString copyDir = tmp.filePath(QStringLiteral("Copy.scriv"));
            check(scrivener::saveCopyAsScrivener(p.get(), copyDir, &wr, &err) && QFile::exists(scrivener::scrivxIn(copyDir))
                      && p->scrivenerPath.isEmpty(),
                  QStringLiteral("save a copy in Scrivener format: written, the project is not linked to it"), err);
            check(!scrivener::saveCopyAsScrivener(p.get(), copyDir, &wr, &err),
                  QStringLiteral("save a copy: an existing Scrivener project is not overwritten"));
            // Scrivener's lock file
            writeAll(scriv + QLatin1String("/Files/user.lock"), "platform=mac\nhost=desk\nuser=Ann\napp=Scrivener\n");
            QString who;
            check(scrivener::lockedByScrivener(scriv, &who) && who.contains(QLatin1String("Ann")) && who.contains(QLatin1String("desk")),
                  QStringLiteral("Scrivener's user.lock is noticed (who and where)"), who);
        }
    }
    qunsetenv("XDG_DATA_HOME");
    return failed;
}
