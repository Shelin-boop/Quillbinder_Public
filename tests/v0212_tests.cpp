// Scripted run for the corrections of 0.21.2 (asked for by the writer on 2026-10-24):
//
// 1. Headings of projects imported from Scrivener showed Scrivener's style marks as text
//    ("<$Scr_Ps::0><$Scr_H::2>Headline<!$Scr_H::2><!$Scr_Ps::0>"): the import of 0.21.1
//    already took them out (since 0.14.0), so the writer's projects were imported by an older
//    version. Now: such marks are taken out when a project opens; the paragraph style marks
//    give the paragraphs the project's style of that name (content.styles + Files/styles.xml,
//    as Scrivener 3.1.6 for Windows writes them), also across several paragraphs.
// 2. Project bookmarks of Scrivener projects (<Bookmark BinderUUID="...">Title</Bookmark>, as
//    Scrivener for Windows writes them): imported and kept (checked again; they came in since 0.14.0).
// 3. The writer's standard settings: 11.0 pt, line spacing 1.09, no first line indent, 6 pt after
//    paragraphs (new projects and Scrivener imports); the main editor's paper #d1d495, beside the
//    text column #999999; default editor width 500 px. Put once into older settings, and the
//    standard of File > Options > Defaults.
//
// Saves screenshots into the folder given as first argument. Exit code = number of failed checks.
//   QT_QPA_PLATFORM=offscreen ./qb_v0212_tests /tmp/shots
// QB_USER_SAMPLE=<folder.scriv> also imports a real Scrivener project (the writer's test project).
#include "compile/PluginManager.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "import/ScrivenerFormat.h"
#include "import/ScrivenerImporter.h"
#include "text/DirectFormat.h"
#include "text/TextProps.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/BookmarkList.h"
#include "ui/EditorPane.h"
#include "ui/Inspector.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDomDocument>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <cstdio>
#include <functional>

using namespace qb;

static int failures = 0;
static QString outDir;
static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail.left(2000)));
    }
    std::fflush(stdout);
}
static void info(const QString &s)
{
    std::printf("      %s\n", qPrintable(s));
    std::fflush(stdout);
}
static void section(const QString &s)
{
    std::printf("\n== %s\n", qPrintable(s));
    std::fflush(stdout);
}
static void settle(int ms = 150)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }
}
static void shot(QWidget *w, const QString &name)
{
    settle(300);
    w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}
static QAction *byKey(QWidget *w, const QString &name)
{
    for (QAction *a : w->findChildren<QAction *>())
        if (a->objectName() == name)
            return a;
    return nullptr;
}
static void writeAll(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(data);
}
static QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
// Scrivener's marks in a file (Markdown writes "_" as "\\_")
static bool hasMarks(const QByteArray &b) { return b.contains("$Scr") || b.contains("Scr_") || b.contains("Scr\\_"); }
static BinderItem *byTitle(Project *p, const QString &title)
{
    for (BinderItem *it : p->allItems())
        if (it->title == title)
            return it;
    return nullptr;
}
// the style of the paragraph that holds `text` ("" = none, "?" = no such paragraph)
static QString styleOf(QTextDocument *d, const QString &text)
{
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
        if (b.text().contains(text))
            return b.blockFormat().stringProperty(props::Style);
    return QStringLiteral("?");
}
static QString allText(Project *p)
{
    QString out;
    for (BinderItem *it : p->allItems())
        out += p->docs()->plainText(it->id) + QLatin1Char('\n') + p->docs()->notesPlainText(it->id) + QLatin1Char('\n');
    return out;
}

// ------------------------------------------------------------------ a project as Scrivener for Windows writes it

// (made after the writer's test project of Scrivener 3.1.6 for Windows, 2026-10-24: the same files,
// elements and marks; the texts are made up)
namespace ids {
const char *draft = "7EAAD80E-3C97-43E2-AAF8-4F6D4CAC7A93";
const char *book = "82204C2E-D01C-4FB0-8690-96D84D2F1758";
const char *prologue = "8EB0D096-AC27-4BA9-A1AD-DD0A18122119";
const char *call = "FDED0446-3B9A-4D96-B950-87936E22C5F2";
const char *title = "D0D2AEF5-BBC4-4226-B46D-3D795A7DFDD1";
const char *quote = "C9D36830-AD4E-4EB2-861A-7731070BDEAA";
const char *research = "E338B1C5-0D57-4F72-8B7D-5E0941CF65A7";
const char *players = "582A8611-6AC7-4A7E-97A7-6524135F9C8C";
const char *ideas = "D5830956-BBBB-4F5B-984F-A20E8CFA0D0C";
const char *trash = "3395979A-2D3F-40A3-8B5F-1A4AE66F1732";
// styles
const char *sTitle = "C45E5B21-79B9-4D02-BB23-726900C7C8F0";
const char *sH1 = "ECE1A207-6DD3-4242-B134-CD48E3E3FEBA";
const char *sH2 = "1E6ED5CD-8BFB-4798-97ED-D5245C3E5DF3";
const char *sCentered = "6A1B9E2C-1111-4C1D-9A8B-0123456789AB";
const char *sQuote = "6A1B9E2C-2222-4C1D-9A8B-0123456789AB";
const char *sEmphasis = "6A1B9E2C-3333-4C1D-9A8B-0123456789AB";
} // namespace ids

static void buildWinSample(const QString &scriv)
{
    auto item = [](const char *id, const char *type, const char *title, const QString &children = QString()) {
        QString x = QStringLiteral("<BinderItem UUID=\"%1\" Type=\"%2\" Created=\"2026-01-02 06:48:10 +0200\" "
                                   "Modified=\"2026-01-03 15:29:19 +0200\"><Title>%3</Title>"
                                   "<MetaData><IncludeInCompile>Yes</IncludeInCompile></MetaData>")
                        .arg(QLatin1String(id), QLatin1String(type), QLatin1String(title));
        if (!children.isEmpty())
            x += QStringLiteral("<Children>") + children + QStringLiteral("</Children>");
        return x + QStringLiteral("</BinderItem>\n");
    };
    const QString binder =
        item(ids::draft, "DraftFolder", "Draft",
             item(ids::book, "Folder", "Book 2",
                  item(ids::prologue, "Text", "Prologue Text") + item(ids::call, "Text", "The Call")
                      + item(ids::title, "Text", "Title Page") + item(ids::quote, "Text", "Quotes")))
        + item(ids::research, "ResearchFolder", "Research", item(ids::players, "Text", "Players") + item(ids::ideas, "Text", "Ideas"))
        + item(ids::trash, "TrashFolder", "Trash");
    const QString x =
        QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                       "<ScrivenerProject Template=\"No\" Version=\"2.0\" Identifier=\"9805399D-798D-4314-A225-E078DC1367BD\" "
                       "Creator=\"SCRWIN-3.1.6.0\" Device=\"Workstation\" Modified=\"2026-10-24 21:16:23 +0200\" "
                       "ModID=\"97506353-9C8B-464D-B8F7-5C8DB8EECBB4\">\n<Binder>\n")
        + binder
        + QStringLiteral("</Binder>\n"
                         "<ProjectBookmarks>\n"
                         "    <Bookmark BinderUUID=\"%1\">Players</Bookmark>\n"
                         "    <Bookmark BinderUUID=\"%2\">Ideas</Bookmark>\n"
                         "</ProjectBookmarks>\n</ScrivenerProject>\n")
              .arg(QLatin1String(ids::players), QLatin1String(ids::ideas));
    writeAll(scriv + QLatin1String("/test.scrivx"), x.toUtf8());
    writeAll(scriv + QLatin1String("/Files/version.txt"), "23");
    auto style = [](const char *id, const char *name, const char *type) {
        return QStringLiteral("    <Style ID=\"%1\" Name=\"%2\" Type=\"%3\"><Format><![CDATA[{\\rtf1\\ansi Attributes}]]></Format></Style>\n")
            .arg(QLatin1String(id), QLatin1String(name), QLatin1String(type));
    };
    writeAll(scriv + QLatin1String("/Files/styles.xml"),
             (QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Styles>\n") + style(ids::sTitle, "Title", "Para+Char")
              + style(ids::sH1, "Heading 1", "Para+Char") + style(ids::sH2, "Heading 2", "Para+Char")
              + style(ids::sCentered, "Centered Text", "Para") + style(ids::sQuote, "Block Quote", "Para")
              + style(ids::sEmphasis, "Emphasis", "Char") + QStringLiteral("</Styles>\n"))
                 .toUtf8());
    // RTF as Scrivener for Windows writes it: \pard\plain ... \ltrch\loch {\f1\fs48 <marks>text<marks>}
    const QByteArray head = "{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0\n"
                            "{\\fonttbl{\\f0\\fmodern\\fcharset0\\fprq2 DejaVu Serif;}{\\f1\\fmodern\\fcharset0\\fprq2 DejaVu Sans;}}\n"
                            "{\\colortbl;\\red0\\green0\\blue0;\\red255\\green255\\blue255;\\red128\\green128\\blue128;}\n"
                            "\\paperw12240\\paperh15840\\margl1800\\margr1800\\margt1440\\margb1440\\f0\\fs22\\cf0\n";
    const QByteArray body = "\\par\\pard\\plain \\sa160\\sl264\\slmult1\\qj\\ltrch\\loch {\\f0\\fs22\\b0\\i0 ";
    const QByteArray heading = "\\pard\\plain \\tx560\\tx1120\\sa200\\sb260\\sl262\\slmult1\\qc\\ltrch\\loch {\\f1\\fs48\\b0\\i1 ";
    const QString d = scriv + QLatin1String("/Files/Data/");
    writeAll(d + QLatin1String(ids::prologue) + QLatin1String("/content.rtf"),
             head + heading + "<$Scr_Ps::0><$Scr_H::1>Prologue<!$Scr_H::1><!$Scr_Ps::0>}\n" + body
                 + "The keeper watched the light turn over the water, night after night, and counted the ships.}\n"
                 + "\\par\\plain {\\f0\\fs22\\b0\\i0 Gulls followed the boat to the rocks.}}");
    writeAll(d + QLatin1String(ids::prologue) + QLatin1String("/content.styles"), ids::sH1);
    writeAll(d + QLatin1String(ids::prologue) + QLatin1String("/notes.rtf"),
             head + heading + "<$Scr_Ps::0><$Scr_H::1>A note heading<!$Scr_H::1><!$Scr_Ps::0>}\n" + body + "Check the tide tables.}}");
    writeAll(d + QLatin1String(ids::prologue) + QLatin1String("/notes.styles"), ids::sH1);
    writeAll(d + QLatin1String(ids::call) + QLatin1String("/content.rtf"),
             head + heading + "<$Scr_Ps::0><$Scr_H::1>Storm Season<!$Scr_H::1><!$Scr_Ps::0>}\n"
                 + "\\par\\pard\\plain \\sa200\\sb260\\sl262\\slmult1\\ltrch\\loch {\\f1\\fs32\\b0\\i0 <$Scr_Ps::1><$Scr_H::2>The Call<!$Scr_H::2><!$Scr_Ps::1>}\n"
                 + body + "The radio woke him before the bell did.}}");
    writeAll(d + QLatin1String(ids::call) + QLatin1String("/content.styles"), QByteArray(ids::sH1) + ',' + ids::sH2);
    writeAll(d + QLatin1String(ids::title) + QLatin1String("/content.rtf"),
             head + heading + "<$Scr_Ps::0>THE KEEPER'S LOG<!$Scr_Ps::0>}\n" + body + "A story of the coast.}}");
    writeAll(d + QLatin1String(ids::title) + QLatin1String("/content.styles"), ids::sTitle);
    // a style over two paragraphs, a heading mark closed at the start of the next paragraph,
    // a character style, the "Centered Text" of Scrivener
    writeAll(d + QLatin1String(ids::quote) + QLatin1String("/content.rtf"),
             head + "\\pard\\plain \\li720\\ltrch\\loch {\\f0\\fs22 <$Scr_Ps::0>First line of the quote.}\n"
                 + "\\par\\plain {\\f0\\fs22 Second line of the quote.<!$Scr_Ps::0>}\n" + body + "After the quote, an }"
                 + "{\\f0\\fs22\\i1 <$Scr_Cs::2>emphasized<!$Scr_Cs::2>}{\\f0\\fs22  word.}\n"
                 + "\\par\\pard\\plain \\qc\\ltrch\\loch {\\f0\\fs22 <$Scr_Ps::1>A centered line<!$Scr_Ps::1>}\n"
                 + "\\par\\pard\\plain \\ltrch\\loch {\\f1\\fs28 <$ScrKeepWithNext><$Scr_H::3>Small heading}\n"
                 + "\\par\\pard\\plain \\ltrch\\loch {\\f0\\fs22 <!$Scr_H::3>Text after the small heading.}}");
    writeAll(d + QLatin1String(ids::quote) + QLatin1String("/content.styles"),
             QByteArray(ids::sQuote) + ',' + ids::sCentered + ',' + ids::sEmphasis);
    writeAll(d + QLatin1String(ids::players) + QLatin1String("/content.rtf"), head + body + "Shelin, Sage, Onyx.}}");
    writeAll(d + QLatin1String(ids::ideas) + QLatin1String("/content.rtf"), head + body + "A storm in the third act.}}");
    writeAll(d + QLatin1String("docs.checksum"), "");
}

static void importedChecks(Project *p, const QString &label)
{
    auto L = [&](const QString &s) { return QStringLiteral("[%1] ").arg(label) + s; };
    const QString all = allText(p);
    check(!all.contains(QLatin1String("$Scr")) && !all.contains(QLatin1String("<!")),
          L(QStringLiteral("no Scrivener style marks in any text or notes")), all.left(400));
    BinderItem *pro = p->item(QLatin1String(ids::prologue));
    BinderItem *call = p->item(QLatin1String(ids::call));
    BinderItem *title = p->item(QLatin1String(ids::title));
    BinderItem *quote = p->item(QLatin1String(ids::quote));
    if (!pro || !call || !title || !quote) {
        check(false, L(QStringLiteral("the documents keep Scrivener's ids")));
        return;
    }
    QTextDocument *dp = p->docs()->text(pro->id);
    check(dp->begin().text() == QLatin1String("Prologue") && styleOf(dp, QStringLiteral("Prologue")) == QLatin1String("Heading 1")
              && styleOf(dp, QStringLiteral("The keeper watched")).isEmpty() && styleOf(dp, QStringLiteral("Gulls")).isEmpty(),
          L(QStringLiteral("\"<$Scr_Ps::0><$Scr_H::1>Prologue<!$Scr_H::1><!$Scr_Ps::0>\": the heading \"Prologue\" with Heading 1, "
                           "the text after it without a style")),
          dp->toPlainText().left(200) + QStringLiteral(" / ") + styleOf(dp, QStringLiteral("Prologue")));
    QTextDocument *dn = p->docs()->notes(pro->id);
    check(dn && dn->toPlainText().startsWith(QLatin1String("A note heading")), L(QStringLiteral("the notes: marks taken out as well")),
          dn ? dn->toPlainText() : QString());
    QTextDocument *dc = p->docs()->text(call->id);
    check(styleOf(dc, QStringLiteral("Storm Season")) == QLatin1String("Heading 1") && styleOf(dc, QStringLiteral("The Call")) == QLatin1String("Heading 2")
              && styleOf(dc, QStringLiteral("The radio")).isEmpty(),
          L(QStringLiteral("two headings in one document (content.styles with two styles): Heading 1 and Heading 2")));
    QTextDocument *dt = p->docs()->text(title->id);
    check(styleOf(dt, QStringLiteral("THE KEEPER'S LOG")) == QLatin1String("Title") && styleOf(dt, QStringLiteral("A story")).isEmpty(),
          L(QStringLiteral("a paragraph style without a heading mark (\"<$Scr_Ps::0>\" = Title in content.styles / styles.xml): style Title")),
          styleOf(dt, QStringLiteral("THE KEEPER'S LOG")));
    QTextDocument *dq = p->docs()->text(quote->id);
    check(styleOf(dq, QStringLiteral("First line of the quote")) == QLatin1String("Block Quote")
              && styleOf(dq, QStringLiteral("Second line of the quote")) == QLatin1String("Block Quote")
              && styleOf(dq, QStringLiteral("After the quote")).isEmpty(),
          L(QStringLiteral("a style mark over two paragraphs: both get Block Quote, the next one none")),
          styleOf(dq, QStringLiteral("First line")) + QStringLiteral(" / ") + styleOf(dq, QStringLiteral("Second line")) + QStringLiteral(" / ")
              + styleOf(dq, QStringLiteral("After the quote")));
    {
        // (the Markdown dialect writes the style Centered as "->text<-", which reads back as a
        // centered paragraph without a style name: as before 0.21.2)
        bool centered = false;
        for (QTextBlock b = dq->begin(); b.isValid(); b = b.next())
            if (b.text() == QLatin1String("A centered line"))
                centered = b.blockFormat().alignment() & Qt::AlignHCenter;
        const QString st = styleOf(dq, QStringLiteral("A centered line"));
        const bool mdReopened = p->storage == StorageFormat::Markdown && label.endsWith(QLatin1String("reopened"));
        check(st == QLatin1String("Centered") || (mdReopened && st.isEmpty() && centered),
              L(QStringLiteral("Scrivener's \"Centered Text\" becomes the project's Centered")), st);
    }
    check(dq->toPlainText().contains(QLatin1String("After the quote, an emphasized word.")),
          L(QStringLiteral("a character style mark (<$Scr_Cs::2>) is taken out, the word stays")), dq->toPlainText());
    check(styleOf(dq, QStringLiteral("Small heading")) == QLatin1String("Heading 3")
              && styleOf(dq, QStringLiteral("Text after the small heading")).isEmpty(),
          L(QStringLiteral("a heading mark closed at the start of the next paragraph: only the heading gets Heading 3")),
          styleOf(dq, QStringLiteral("Small heading")) + QStringLiteral(" / ") + styleOf(dq, QStringLiteral("Text after the small heading")));
    QStringList bms;
    for (const Bookmark &b : p->projectBookmarks)
        bms << b.title + QLatin1Char('=') + b.target;
    check(bms == QStringList{QStringLiteral("Players=qb://") + QLatin1String(ids::players), QStringLiteral("Ideas=qb://") + QLatin1String(ids::ideas)},
          L(QStringLiteral("project bookmarks of Scrivener for Windows (<Bookmark BinderUUID=\"...\">Title</Bookmark>): both, in order")),
          bms.join(QLatin1String(", ")));
}

static void scrivenerChecks()
{
    section(QStringLiteral("Scrivener projects: style marks, styles, project bookmarks"));
    QTemporaryDir tmp;
    const QString scriv = tmp.filePath(QStringLiteral("test.scriv"));
    buildWinSample(scriv);
    for (const StorageFormat st : {StorageFormat::Markdown, StorageFormat::Rtf}) {
        const QString label = st == StorageFormat::Markdown ? QStringLiteral("import, Markdown") : QStringLiteral("import, RTF");
        const QString into = tmp.filePath(label.right(3));
        QDir().mkpath(into);
        ScrivenerImportReport rep;
        QString err;
        auto p = ScrivenerImporter::importProject(scriv, into, QStringLiteral("Imported"), st, &rep, &err);
        check(p != nullptr, QStringLiteral("[%1] the project is read").arg(label), err);
        if (!p)
            continue;
        importedChecks(p.get(), label);
        // formatting: the writer's standard for new projects, the font most used in the Scrivener project
        check(p->formatting.fontFamily == QLatin1String("DejaVu Serif") && qAbs(p->formatting.fontSize - 11.0) < 0.001
                  && qAbs(p->formatting.lineSpacing - 1.09) < 0.0001 && qAbs(p->formatting.firstLineIndent) < 0.001
                  && qAbs(direct::pxToPt(p->formatting.paragraphSpacing) - 6.0) < 0.001,
              QStringLiteral("[%1] formatting: the font of the Scrivener project, 11.0 pt, line spacing 1.09, no first line indent, 6 pt after")
                  .arg(label),
              QStringLiteral("%1 %2 pt %3 %4 px %5 px").arg(p->formatting.fontFamily).arg(p->formatting.fontSize).arg(p->formatting.lineSpacing)
                  .arg(p->formatting.firstLineIndent).arg(p->formatting.paragraphSpacing));
        // closed and opened again: everything is in the files
        const QString path = p->indexPath();
        p.reset();
        auto q = Project::open(path, &err);
        check(q != nullptr, QStringLiteral("[%1] opened again").arg(label), err);
        if (q)
            importedChecks(q.get(), label + QStringLiteral(", reopened"));
    }
    // in Scrivener format (a working copy)
    {
        const QString copy = tmp.filePath(QStringLiteral("fmt/test.scriv"));
        buildWinSample(copy);
        QString err;
        scrivener::OpenResult r = scrivener::open(copy, &err);
        check(r.project != nullptr, QStringLiteral("[Scrivener format] opened"), err);
        if (r.project) {
            importedChecks(r.project.get(), QStringLiteral("Scrivener format"));
            check(ScrivenerImporter::removeLeftoverMarkers(r.project.get()) == 0,
                  QStringLiteral("[Scrivener format] the clean-up of old imports leaves working copies alone"));
        }
    }
    // the writer's own Scrivener project, when given
    const QString user = qEnvironmentVariable("QB_USER_SAMPLE");
    if (!user.isEmpty() && QFileInfo(user).isDir()) {
        QTemporaryDir ut;
        const QString copy = ut.filePath(QFileInfo(user).fileName());
        QDir().mkpath(copy);
        QProcess::execute(QStringLiteral("cp"), {QStringLiteral("-r"), user + QLatin1String("/."), copy});
        int bookmarks = 0, known = 0;
        {
            QFile f(ScrivenerImporter::findScrivx(copy));
            QDomDocument dom;
            if (f.open(QIODevice::ReadOnly) && dom.setContent(&f)) {
                const QDomNodeList l = dom.documentElement().firstChildElement(QStringLiteral("ProjectBookmarks")).elementsByTagName(QStringLiteral("Bookmark"));
                bookmarks = l.size();
                QSet<QString> uuids;
                const QDomNodeList items = dom.elementsByTagName(QStringLiteral("BinderItem"));
                for (int i = 0; i < items.size(); ++i)
                    uuids.insert(items.at(i).toElement().attribute(QStringLiteral("UUID")).toUpper());
                for (int i = 0; i < l.size(); ++i)
                    known += uuids.contains(l.at(i).toElement().attribute(QStringLiteral("BinderUUID")).toUpper()) ? 1 : 0;
            }
        }
        for (const StorageFormat st : {StorageFormat::Markdown, StorageFormat::Rtf}) {
            const QString into = ut.filePath(st == StorageFormat::Markdown ? QStringLiteral("md") : QStringLiteral("rtf"));
            QDir().mkpath(into);
            ScrivenerImportReport rep;
            QString err;
            auto p = ScrivenerImporter::importProject(copy, into, QStringLiteral("Writer"), st, &rep, &err);
            check(p != nullptr, QStringLiteral("[the writer's project] read"), err);
            if (!p)
                continue;
            const QString all = allText(p.get());
            check(!all.contains(QLatin1String("$Scr")), QStringLiteral("[the writer's project] no Scrivener style marks in any text or notes"));
            check(p->projectBookmarks.size() == known && known > 0,
                  QStringLiteral("[the writer's project] the project bookmarks: %1 of %2 (%3 in the .scrivx)")
                      .arg(p->projectBookmarks.size()).arg(known).arg(bookmarks));
            int headings = 0, titled = 0;
            for (BinderItem *it : p->allItems()) {
                QTextDocument *d = p->docs()->loadedText(it->id) ? p->docs()->loadedText(it->id) : p->docs()->text(it->id);
                for (QTextBlock b = d->begin(); b.isValid(); b = b.next()) {
                    const QString s = b.blockFormat().stringProperty(props::Style);
                    headings += s.startsWith(QLatin1String("Heading")) ? 1 : 0;
                    titled += s == QLatin1String("Title") ? 1 : 0;
                }
            }
            info(QStringLiteral("the writer's project (%1): %2 headings, %3 paragraphs with Title, %4 documents")
                     .arg(st == StorageFormat::Markdown ? QStringLiteral("Markdown") : QStringLiteral("RTF"))
                     .arg(headings).arg(titled).arg(rep.textDocuments));
            check(headings > 0 && titled > 0, QStringLiteral("[the writer's project] headings and the Title paragraph got their styles"));
        }
    }
}

// ------------------------------------------------------------------ projects imported by an older version

static std::unique_ptr<Project> novelProject(const QString &dir, const QString &title, QString *err)
{
    templates::Info novel;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("novel"))
            novel = t;
    return templates::createProject(novel, dir, title, err);
}

static void setText(Project *p, BinderItem *it, const QString &text, bool notes = false)
{
    QTextDocument *d = notes ? p->docs()->notes(it->id) : p->docs()->text(it->id);
    QTextCursor c(d);
    c.select(QTextCursor::Document);
    c.insertText(text);
}

static void leftoverChecks(MainWindow &w)
{
    section(QStringLiteral("Projects imported before 0.14.0: the marks are taken out when they open"));
    const QString marked = QStringLiteral("<$Scr_Ps::0><$Scr_H::2>Headline<!$Scr_H::2><!$Scr_Ps::0>");
    for (const StorageFormat st : {StorageFormat::Markdown, StorageFormat::Rtf}) {
        const QString label = st == StorageFormat::Markdown ? QStringLiteral("Markdown") : QStringLiteral("RTF");
        QTemporaryDir dir;
        QString err;
        auto proj = novelProject(dir.path(), QStringLiteral("Old Import"), &err);
        check(proj != nullptr, QStringLiteral("[%1] project made").arg(label), err);
        if (!proj)
            continue;
        Project *p = proj.get();
        if (st == StorageFormat::Rtf)
            check(p->docs()->convertStorage(StorageFormat::Rtf, &err), QStringLiteral("[RTF] the project stores RTF"), err);
        BinderItem *ch = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Book"));
        BinderItem *a = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("Imported"));
        BinderItem *b = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("Untouched"));
        // the text as an import before 0.14.0 left it: the marks are plain text
        setText(p, a, marked + QStringLiteral("\nThe body of the chapter.\n<$ScrKeepWithNext>Last line."));
        setText(p, a, QStringLiteral("<$Scr_H::1>Note<!$Scr_H::1>\nA note."), true);
        setText(p, b, QStringLiteral("Nothing to do here; the price was $5."));
        check(p->save(&err), QStringLiteral("[%1] saved").arg(label), err);
        const QString textFile = p->docs()->textPath(a->id), untouchedFile = p->docs()->textPath(b->id);
        check(hasMarks(readAll(textFile)), QStringLiteral("[%1] (the file has the marks, as after an old import)").arg(label),
              QString::fromUtf8(readAll(textFile).left(300)));
        const QDateTime untouchedTime = QFileInfo(untouchedFile).lastModified();
        const QString folder = p->folderPath(), ida = a->id;
        proj.reset();
        settle(1100); // file times have a resolution of a second on some file systems
        check(w.openProject(folder), QStringLiteral("[%1] opened in the window").arg(label));
        settle(400);
        p = w.project();
        a = p ? p->item(ida) : nullptr;
        if (!a)
            continue;
        QTextDocument *d = p->docs()->text(a->id);
        check(d->toPlainText() == QLatin1String("Headline\nThe body of the chapter.\nLast line."),
              QStringLiteral("[%1] the reported text \"%2\": the marks are gone").arg(label, marked), d->toPlainText());
        check(styleOf(d, QStringLiteral("Headline")) == QLatin1String("Heading 2") && styleOf(d, QStringLiteral("The body")).isEmpty(),
              QStringLiteral("[%1] \"Headline\" gets Heading 2 (its heading mark)").arg(label), styleOf(d, QStringLiteral("Headline")));
        check(!hasMarks(readAll(textFile)),
              QStringLiteral("[%1] ... and the file no longer has them").arg(label));
        check(p->docs()->notesPlainText(a->id) == QLatin1String("Note\nA note."), QStringLiteral("[%1] the notes as well").arg(label),
              p->docs()->notesPlainText(a->id));
        check(QFileInfo(untouchedFile).lastModified() == untouchedTime && readAll(untouchedFile).contains("$5"),
              QStringLiteral("[%1] a document without marks is not written").arg(label));
        check(w.statusBar()->currentMessage().contains(QLatin1String("1 document")),
              QStringLiteral("[%1] the status bar says so").arg(label), w.statusBar()->currentMessage());
        if (st == StorageFormat::Markdown) {
            w.findChild<BinderPanel *>()->select({a}, true);
            settle(400);
            shot(&w, QStringLiteral("01-old-import-cleaned"));
        }
        w.closeProject();
        settle(200);
        w.statusBar()->clearMessage();
        // opened again: nothing more to do
        check(w.openProject(folder), QStringLiteral("[%1] opened once more").arg(label));
        settle(300);
        check(!w.statusBar()->currentMessage().contains(QLatin1String("style marks")),
              QStringLiteral("[%1] ... nothing is taken out the second time").arg(label), w.statusBar()->currentMessage());
        w.closeProject();
        settle(200);
    }
}

// ------------------------------------------------------------------ Scrivener import through the window

static void windowImportChecks(MainWindow &w)
{
    section(QStringLiteral("File > Import > Scrivener Project in the window"));
    QTemporaryDir tmp;
    const QString scriv = tmp.filePath(QStringLiteral("test.scriv"));
    buildWinSample(scriv);
    const QString into = tmp.filePath(QStringLiteral("into"));
    QDir().mkpath(into);
    QTimer::singleShot(400, qApp, [&] {
        QDialog *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d) {
            info(QStringLiteral("(no import window)"));
            return;
        }
        for (QLineEdit *e : d->findChildren<QLineEdit *>())
            if (e->text().startsWith(QLatin1Char('/')))
                e->setText(into);
        d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QTimer::singleShot(1200, qApp, [] {
            if (auto *m = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                m->accept();
        });
    });
    QAction *imp = byKey(&w, QStringLiteral("importScrivener"));
    imp->setData(scriv);
    imp->trigger();
    imp->setData(QVariant());
    settle(800);
    Project *p = w.project();
    check(p && p->title() == QLatin1String("test"), QStringLiteral("imported through the window"));
    if (!p)
        return;
    BinderItem *pro = p->item(QLatin1String(ids::prologue));
    w.findChild<BinderPanel *>()->select({pro}, true);
    settle(400);
    TextEditor *ed = nullptr;
    for (TextEditor *e : w.findChildren<TextEditor *>())
        if (e->isVisible() && e->item() == pro)
            ed = e;
    check(ed && ed->toPlainText().startsWith(QLatin1String("Prologue\nThe keeper")),
          QStringLiteral("the editor shows the heading \"Prologue\" without marks"), ed ? ed->toPlainText().left(80) : QString());
    Inspector *insp = w.findChild<Inspector *>();
    if (QAction *si = byKey(&w, QStringLiteral("showInspector")); si && !si->isChecked())
        si->trigger();
    insp->showTab(Inspector::Bookmarks);
    settle(300);
    QStringList rows;
    for (int i = 0; i < insp->projectBookmarkList()->count(); ++i)
        rows << insp->projectBookmarkList()->item(i)->text();
    check(rows == QStringList{QStringLiteral("Players"), QStringLiteral("Ideas")},
          QStringLiteral("Inspector > Bookmarks lists the two project bookmarks"), rows.join(QLatin1String(", ")));
    shot(&w, QStringLiteral("02-scrivener-import-bookmarks"));
    w.closeProject();
    settle(200);
}

// ------------------------------------------------------------------ the writer's standard settings

static bool standardValues(const AppSettings &as, QString *detail)
{
    *detail = QStringLiteral("%1 pt, %2, %3 px, %4 px, width %5, paper %6, beside %7")
                  .arg(as.newFontSize).arg(as.newLineSpacing).arg(as.newFirstLineIndent).arg(as.newParagraphSpacing).arg(as.editorWidth)
                  .arg(as.ownColors.value(QStringLiteral("editorPaper")).name(), as.ownColors.value(QStringLiteral("editorMargin")).name());
    return qAbs(as.newFontSize - 11.0) < 0.001 && qAbs(as.newLineSpacing - 1.09) < 0.0001 && qAbs(as.newFirstLineIndent) < 0.001
           && qAbs(direct::pxToPt(as.newParagraphSpacing) - 6.0) < 0.001 && as.editorWidth == 500
           && as.ownColors.value(QStringLiteral("editorPaper")) == QColor(0xd1, 0xd4, 0x95)
           && as.ownColors.value(QStringLiteral("editorMargin")) == QColor(0x99, 0x99, 0x99);
}

static void settingsChecks(MainWindow &w, bool freshWasStandard, const QString &freshDetail)
{
    section(QStringLiteral("The writer's standard settings"));
    AppSettings &as = AppSettings::instance();
    check(freshWasStandard, QStringLiteral("a new installation: 11.0 pt, line spacing 1.09, first line indent 0, 6 pt after paragraphs, "
                                           "paper #d1d495, beside the column #999999, editor width 500 px"),
          freshDetail);
    check(QSettings().value(QLatin1String(AppSettings::kStandardKey)).toBool(), QStringLiteral("... and it is recorded as put"));

    // settings of an older version (no record): the standard is put once, other own colors stay
    {
        QSettings s;
        s.remove(QLatin1String(AppSettings::kStandardKey));
        s.setValue(QStringLiteral("editing/newFontSize"), 13.0);
        s.setValue(QStringLiteral("editing/newLineSpacing"), 1.2);
        s.setValue(QStringLiteral("editing/newFirstLineIndent"), 24.0);
        s.setValue(QStringLiteral("editing/newParagraphSpacing"), 4.0);
        s.setValue(QStringLiteral("appearance/editorWidth"), 760);
        s.setValue(QStringLiteral("appearance/colors/editorPaper"), QStringLiteral("#ffeedd"));
        s.setValue(QStringLiteral("appearance/colors/binderBackground"), QStringLiteral("#334455"));
        s.sync();
    }
    as.load();
    QString detail;
    check(standardValues(as, &detail) && as.ownColors.value(QStringLiteral("binderBackground")) == QColor(0x33, 0x44, 0x55),
          QStringLiteral("settings of 0.21.1: the standard is put in once at the first start, the binder's own color stays"), detail);
    check(QSettings().value(QStringLiteral("editing/newFontSize")).toDouble() == 11.0
              && QSettings().value(QStringLiteral("appearance/colors/editorPaper")).toString() == QLatin1String("#d1d495"),
          QStringLiteral("... and written into the settings file"));
    // later changes of the writer stay
    as.editorWidth = 640;
    as.newFontSize = 12.5;
    as.ownColors.insert(QStringLiteral("editorPaper"), QColor(0xee, 0xee, 0xee));
    as.save();
    as.load();
    check(as.editorWidth == 640 && qAbs(as.newFontSize - 12.5) < 0.001 && as.ownColors.value(QStringLiteral("editorPaper")) == QColor(0xee, 0xee, 0xee),
          QStringLiteral("changes made afterwards stay (the standard is not put in again)"));
    // File > Options > Defaults
    as.resetToDefaults();
    check(standardValues(as, &detail) && !as.ownColors.contains(QStringLiteral("binderBackground")),
          QStringLiteral("File > Options > Defaults: the standard again"), detail);
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.backupOnOpen = false;
    as.save();
    theme::apply();

    // the Options window shows them
    {
        OptionsDialog od(&w, 0);
        od.showSection(OptionsDialog::SEditing, QStringLiteral("Formatting"));
        od.show();
        settle(300);
        QList<QDoubleSpinBox *> spins;
        if (auto *box = od.findChild<QGroupBox *>(QStringLiteral("newFormatting")))
            spins = box->findChildren<QDoubleSpinBox *>();
        QStringList shown;
        for (QDoubleSpinBox *sp : spins)
            shown << sp->text();
        check(spins.size() == 4 && shown.at(0).startsWith(QLatin1String("11.0")) && shown.at(1) == QLatin1String("1.09")
                  && spins.at(2)->value() == 0.0 && shown.at(3).startsWith(QLatin1String("6.0")),
              QStringLiteral("Options > Editing > Formatting shows 11.0 pt, 1.09, 0, 6.0 pt"), shown.join(QLatin1String(" | ")));
        shot(&od, QStringLiteral("03-options-formatting"));
        od.showSection(OptionsDialog::SAppearance);
        settle(200);
        auto *width = od.findChild<QSpinBox *>(QStringLiteral("optEditorWidth"));
        check(width && width->value() == 500, QStringLiteral("Options > Appearance: default editor width 500 px"),
              width ? QString::number(width->value()) : QString());
        check(od.shownColor(QStringLiteral("editorPaper")) == QColor(0xd1, 0xd4, 0x95)
                  && od.shownColor(QStringLiteral("editorMargin")) == QColor(0x99, 0x99, 0x99),
              QStringLiteral("Options > Appearance: paper #d1d495, beside the text column #999999"));
        // OK keeps them as they are (the line spacing is not rounded to 1.1)
        od.accept();
        settle(200);
        check(standardValues(as, &detail), QStringLiteral("OK in the Options window keeps the values exactly"), detail);
    }

    // the colors on screen, in a light and a dark theme
    for (const QString &th : {QStringLiteral("light"), QStringLiteral("dark")}) {
        as.theme = th;
        as.save();
        theme::apply();
        settle(150);
        check(theme::color(theme::Role::EditorPaper) == QColor(0xd1, 0xd4, 0x95)
                  && theme::color(theme::Role::EditorMargin) == QColor(0x99, 0x99, 0x99)
                  && theme::color(theme::Role::EditorText).lightness() < 100,
              QStringLiteral("theme %1: paper #d1d495, beside the column #999999, dark text on the paper").arg(th),
              theme::color(theme::Role::EditorText).name());
    }
    as.theme = QStringLiteral("light");
    as.save();
    theme::apply();

    // a new project (File > New Project) gets the formatting, the editor the width and colors
    QTemporaryDir dir;
    QTimer::singleShot(400, qApp, [&] {
        QDialog *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d) {
            info(QStringLiteral("(no start window)"));
            return;
        }
        d->findChild<QLineEdit *>(QStringLiteral("projectName"))->setText(QStringLiteral("Standard"));
        d->findChild<QLineEdit *>(QStringLiteral("projectLocation"))->setText(dir.path());
        for (QPushButton *b : d->findChildren<QPushButton *>())
            if (b->text() == QLatin1String("Create"))
                b->click();
    });
    byKey(&w, QStringLiteral("newProject"))->trigger();
    settle(800);
    Project *p = w.project();
    check(p && p->title() == QLatin1String("Standard"), QStringLiteral("File > New Project made a project"));
    if (!p)
        return;
    check(qAbs(p->formatting.fontSize - 11.0) < 0.001 && qAbs(p->formatting.lineSpacing - 1.09) < 0.0001
              && qAbs(p->formatting.firstLineIndent) < 0.001 && qAbs(direct::pxToPt(p->formatting.paragraphSpacing) - 6.0) < 0.001,
          QStringLiteral("the new project: 11.0 pt, line spacing 1.09, no first line indent, 6 pt after paragraphs"),
          QStringLiteral("%1 %2 %3 %4").arg(p->formatting.fontSize).arg(p->formatting.lineSpacing).arg(p->formatting.firstLineIndent)
              .arg(p->formatting.paragraphSpacing));
    BinderItem *doc = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Width"));
    setText(p, doc, QStringLiteral("The keeper counted the ships twice each evening, once by lamplight and once by memory, and the numbers "
                                   "never quite agreed.\nThe second paragraph starts at the margin."));
    if (QAction *fw = byKey(&w, QStringLiteral("fixedWidth")); fw && !fw->isChecked())
        fw->trigger();
    w.findChild<BinderPanel *>()->select({doc}, true);
    settle(500);
    TextEditor *ed = nullptr;
    for (TextEditor *e : w.findChildren<TextEditor *>())
        if (e->isVisible() && e->item() == doc)
            ed = e;
    TextColumnView *view = ed ? qobject_cast<TextColumnView *>(ed->parentWidget()) : nullptr;
    for (QWidget *x = ed; x && !view; x = x->parentWidget())
        view = qobject_cast<TextColumnView *>(x);
    QWidget *column = nullptr;
    for (QWidget *x = ed; x && x != view; x = x->parentWidget())
        if (x->minimumWidth() == x->maximumWidth() && x->autoFillBackground())
            column = x;
    const int want = int(500 * p->docs()->zoom());
    check(column && column->width() == want, QStringLiteral("the text column is 500 px wide (at the editor's zoom: %1 px)").arg(want),
          column ? QString::number(column->width()) : QStringLiteral("no column"));
    if (view && column) {
        const QImage img = view->viewport()->grab().toImage();
        const QPoint inColumn = column->mapTo(view->viewport(), QPoint(column->width() / 2, column->height() - 4));
        const QColor beside = img.pixelColor(4, img.height() / 2), paper = img.pixelColor(inColumn.x(), qMin(inColumn.y(), img.height() - 4));
        check(beside == QColor(0x99, 0x99, 0x99) && paper == QColor(0xd1, 0xd4, 0x95),
              QStringLiteral("on screen: #999999 beside the column, #d1d495 in it"), beside.name() + QStringLiteral(" / ") + paper.name());
    }
    if (ed) {
        QTextBlock second = ed->document()->begin().next();
        check(second.isValid() && second.blockFormat().textIndent() == 0.0,
              QStringLiteral("a paragraph after another one has no first line indent"), QString::number(second.blockFormat().textIndent()));
    }
    if (ed) {
        // the format bar names the line spacing (it said "Other…" for 1.09 before)
        ed->setFocus();
        QTextCursor c = ed->textCursor();
        c.setPosition(5);
        ed->setTextCursor(c);
        settle(200);
        QComboBox *line = nullptr;
        for (QComboBox *x : w.findChildren<QComboBox *>())
            if (x->toolTip() == QLatin1String("Line spacing of the paragraph"))
                line = x;
        check(line && line->currentText() == QLatin1String("1.09x"), QStringLiteral("the format bar shows the line spacing as 1.09x"),
              line ? line->currentText() : QString());
    }
    shot(&w, QStringLiteral("04-standard-editor"));
    w.closeProject();
    settle(200);
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV0212Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV0212Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    AppSettings &as = AppSettings::instance();
    as.load(); // a new installation: no settings file yet
    QString freshDetail;
    const bool freshWasStandard = standardValues(as, &freshDetail);
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.backupOnOpen = false;
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();

    scrivenerChecks();
    auto w = std::make_unique<MainWindow>();
    w->resize(1400, 860);
    w->show();
    w->activateWindow();
    settle(300);
    leftoverChecks(*w);
    windowImportChecks(*w);
    settingsChecks(*w, freshWasStandard, freshDetail);
    w.reset();
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
