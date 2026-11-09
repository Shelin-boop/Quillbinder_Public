// Scripted run for version 0.64.1, the fixes of the system test's first run (2026-11-08):
//  - Markdown storage: table cells that start or end with an empty paragraph, and a table inside a cell;
//  - Split at Selection inside a table or next to one;
//  - a text only shown here and changed elsewhere (a sync program) is not written over;
//  - the other version of a file changed elsewhere must be kept before this one replaces it;
//  - a figure cut and pasted is a figure again, with its picture;
//  - compiling lets the event loop run (the freeze watchdog's timer);
//  - table cells are not justified with the body text;
//  - Merge also merges the notes;
//  - Merge Cells, Undo, Redo, Delete Rows under the merged cell.
// Saves screenshots into the folder given as first argument. Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_v0641_tests /tmp/shots
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "core/Snapshots.h"
#include "compile/CompileFormat.h"
#include "compile/Compiler.h"
#include "compile/DocxWriter.h"
#include "compile/EpubExporter.h"
#include "compile/PdfExporter.h"
#include "compile/PluginManager.h"
#include "compile/Progress.h"
#include "text/DisplayFormat.h"
#include "text/MarkdownIO.h"
#include "text/RtfIO.h"
#include "text/Tables.h"
#include "text/TextProps.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include "AppInfo.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QPushButton>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFrame>
#include <QTextTable>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <functional>

using namespace qb;
using namespace qb::compile;

static int failures = 0;
static QString outDir;
static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail.left(3000)));
    }
    std::fflush(stdout);
}

static void info(const QString &what)
{
    std::printf("      %s\n", qPrintable(what));
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
    if (w)
        w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}

// answers the next modal QMessageBox (button: the standard button to click)
static void answerBox(int delayMs, QMessageBox::StandardButton button, QString *text = nullptr, QString *name = nullptr)
{
    QTimer::singleShot(delayMs, qApp, [=] {
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *d = qobject_cast<QMessageBox *>(tl))
                if (d->isVisible() && d->isModal()) {
                    if (text)
                        *text = d->text();
                    if (name)
                        *name = d->objectName();
                    if (QAbstractButton *b = d->button(button))
                        b->click();
                    else
                        d->reject();
                    return;
                }
    });
}

static QMessageBox *visibleBox(const QString &name)
{
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (auto *b = qobject_cast<QMessageBox *>(tl))
            if (b->isVisible() && b->objectName() == name)
                return b;
    return nullptr;
}

static QByteArray readAll(const QString &f)
{
    QFile file(f);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

static void writeAll(const QString &f, const QByteArray &data)
{
    QFile file(f);
    if (file.open(QIODevice::WriteOnly))
        file.write(data);
}

static QString runTool(const QString &prog, const QStringList &args)
{
    QProcess p;
    p.start(prog, args);
    if (!p.waitForFinished(60000))
        return QString();
    return QString::fromUtf8(p.readAllStandardOutput());
}

static BinderItem *textDoc(Project *p, BinderItem *parent, const QString &title, const QString &text)
{
    BinderItem *it = p->model()->createItem(parent, -1, ItemType::Text, title);
    QTextCursor c(p->docs()->text(it->id));
    c.insertText(text);
    return it;
}

static EditorPane *firstPane(MainWindow &w)
{
    for (EditorPane *ep : w.findChildren<EditorPane *>())
        if (ep->isVisible())
            return ep;
    return nullptr;
}

// the paragraphs of a table cell (also those of a table inside it)
static QStringList cellParas(QTextTable *t, int r, int c)
{
    QStringList out;
    const QTextTableCell cell = t->cellAt(r, c);
    QTextDocument *d = t->document();
    for (QTextBlock b = d->findBlock(cell.firstPosition()); b.isValid() && b.position() <= cell.lastPosition(); b = b.next())
        out << b.text();
    return out;
}

static QTextTable *firstTable(QTextDocument *d)
{
    for (QTextFrame *f : d->rootFrame()->childFrames())
        if (auto *t = qobject_cast<QTextTable *>(f))
            return t;
    return nullptr;
}

static QList<QTextFrame *> figuresIn(QTextDocument *d)
{
    QList<QTextFrame *> out;
    for (QTextFrame *f : d->rootFrame()->childFrames())
        if (figures::isFigure(f))
            out << f;
    return out;
}

// the picture of a figure: its name, and whether the document has the picture itself
static bool figureHasPicture(QTextDocument *d, QTextFrame *f, QString *name)
{
    for (QTextBlock b = d->findBlock(f->firstPosition()); b.isValid() && b.position() <= f->lastPosition(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it)
            if (it.fragment().charFormat().isImageFormat()) {
                const QString n = it.fragment().charFormat().toImageFormat().name();
                if (name)
                    *name = n;
                const QVariant v = d->resource(QTextDocument::ImageResource, QUrl(n));
                return (v.canConvert<QImage>() && !v.value<QImage>().isNull()) || (v.canConvert<QPixmap>() && !v.value<QPixmap>().isNull());
            }
    return false;
}

static QString oneLine(QString s) { return s.replace(QChar(QChar::ParagraphSeparator), QLatin1String(" / ")).replace(QLatin1Char('\n'), QLatin1String(" / ")); }

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV0641Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV0641Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);

    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.64.1")) >= 0,
          QStringLiteral("version 0.64.1 or newer"), QStringLiteral(QB_APP_VERSION_SHOWN));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.backupOnClose = false;
    as.backupOnOpen = false;
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();
    templates::Info blank;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("blank"))
            blank = t;

    Formatting jf;
    jf.align = Qt::AlignJustify;

    // ================================================================ Markdown storage: table cells
    {
        QTextDocument d;
        display::prepareDocument(&d, jf, 1.0);
        QTextCursor c(&d);
        c.insertText(QStringLiteral("Before the table."));
        c.insertBlock();
        QTextTable *t = tables::insert(c, 2, 2, tables::preset(QStringLiteral("grid")), display::blockFormatFor(jf, QString()),
                                       display::charFormatFor(jf, QString()));
        QTextCursor a = t->cellAt(0, 0).firstCursorPosition();
        a.insertBlock();
        a.insertText(QStringLiteral("x"));
        QTextCursor b = t->cellAt(0, 1).firstCursorPosition();
        b.insertText(QStringLiteral("y"));
        b.insertBlock();
        QTextCursor e0 = t->cellAt(1, 0).firstCursorPosition();
        e0.insertBlock();
        QTextCursor n = t->cellAt(1, 1).firstCursorPosition();
        n.insertText(QStringLiteral("z"));
        n.insertBlock();
        QTextTable *inner = n.insertTable(1, 2);
        inner->cellAt(0, 0).firstCursorPosition().insertText(QStringLiteral("inner one"));
        inner->cellAt(0, 1).firstCursorPosition().insertText(QStringLiteral("inner two"));
        const QString written = md::write(&d, jf);
        QTextDocument back;
        md::read(written, &back, jf);
        QTextTable *bt = firstTable(&back);
        const QString all = back.toPlainText();
        check(bt && cellParas(bt, 0, 0) == QStringList({QString(), QStringLiteral("x")})
                  && cellParas(bt, 0, 1) == QStringList({QStringLiteral("y"), QString()})
                  && cellParas(bt, 1, 0) == QStringList({QString(), QString()}) && !all.contains(QLatin1String("<p/>")),
              QStringLiteral("Markdown storage: cells that start or end with an empty paragraph come back as they were (no "
                             "\"<p/>\" as text)"),
              bt ? QStringLiteral("(0,0) [%1] (0,1) [%2] (1,0) [%3] | %4")
                       .arg(cellParas(bt, 0, 0).join(QLatin1Char('|')), cellParas(bt, 0, 1).join(QLatin1Char('|')),
                            cellParas(bt, 1, 0).join(QLatin1Char('|')), written)
                 : written);
        check(all.contains(QLatin1String("inner one")) && all.contains(QLatin1String("inner two")) && all.contains(QLatin1String("z")),
              QStringLiteral("Markdown storage: the text of a table inside a cell is kept (as paragraphs of the cell)"), oneLine(all));
        {
            // the same in RTF storage (the system test's C-28 after the first fixes: the inner text was left out)
            QTextDocument rback;
            rtf::read(rtf::write(&d, jf), &rback, jf);
            const QString rall = rback.toPlainText();
            check(rall.contains(QLatin1String("inner one")) && rall.contains(QLatin1String("inner two")) && rall.contains(QLatin1String("z"))
                      && firstTable(&rback),
                  QStringLiteral("RTF storage: the text of a table inside a cell is kept (as paragraphs of the cell)"), oneLine(rall));
        }
        // a cell as 0.62 to 0.64 wrote it, read by this version
        QTextDocument old;
        md::read(QStringLiteral(":::table {}\n| <p/> x | y <p/> |\n| <p/> | \\<p/> typed |\n:::"), &old, jf);
        QTextTable *ot = firstTable(&old);
        check(ot && cellParas(ot, 0, 0) == QStringList({QString(), QStringLiteral("x")})
                  && cellParas(ot, 0, 1) == QStringList({QStringLiteral("y"), QString()})
                  && cellParas(ot, 1, 0) == QStringList({QString(), QString()})
                  && cellParas(ot, 1, 1) == QStringList({QStringLiteral("<p/> typed")}),
              QStringLiteral("... cells written by 0.62 to 0.64 are read right too; \"<p/>\" typed by the writer stays text"),
              ot ? QStringLiteral("(0,0) [%1] (0,1) [%2] (1,0) [%3] (1,1) [%4]")
                       .arg(cellParas(ot, 0, 0).join(QLatin1Char('|')), cellParas(ot, 0, 1).join(QLatin1Char('|')),
                            cellParas(ot, 1, 0).join(QLatin1Char('|')), cellParas(ot, 1, 1).join(QLatin1Char('|')))
                 : QString());
    }

    // ================================================================ table cells are not justified
    {
        QTextDocument d;
        display::prepareDocument(&d, jf, 1.0);
        QTextCursor c(&d);
        c.setBlockFormat(display::blockFormatFor(jf, QString()));
        c.insertText(QStringLiteral("Body text that is justified."));
        c.insertBlock();
        QTextTable *t = tables::insert(c, 2, 2, tables::preset(QStringLiteral("grid")), display::blockFormatFor(jf, QString()),
                                       display::charFormatFor(jf, QString()));
        t->cellAt(1, 1).firstCursorPosition().insertText(QStringLiteral("Row 10 cell"));
        auto cellsJustified = [](QTextDocument *doc) {
            int n = 0;
            for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
                if (QTextCursor(b).currentTable() && (b.blockFormat().alignment() & Qt::AlignJustify))
                    ++n;
            return n;
        };
        const bool bodyJustified = d.begin().blockFormat().alignment() & Qt::AlignJustify;
        // a style or the project's formatting given again keeps cells unjustified
        display::normalizeDocument(&d, jf);
        const int inEditor = cellsJustified(&d);
        QTextDocument mdBack, rtfBack;
        md::read(md::write(&d, jf), &mdBack, jf);
        display::normalizeDocument(&mdBack, jf);
        rtf::read(rtf::write(&d, jf), &rtfBack, jf, rtf::ReadOptions());
        display::normalizeDocument(&rtfBack, jf);
        check(bodyJustified && inEditor == 0 && cellsJustified(&mdBack) == 0 && cellsJustified(&rtfBack) == 0,
              QStringLiteral("with justified body text, table cells are not justified (in the text, and after Markdown and RTF "
                             "storage)"),
              QStringLiteral("body %1, cells justified: %2, Markdown %3, RTF %4")
                  .arg(bodyJustified).arg(inEditor).arg(cellsJustified(&mdBack)).arg(cellsJustified(&rtfBack)));
    }

    // ================================================================ a project in the window
    QTemporaryDir work;
    QString err;
    auto proj = templates::createProject(blank, work.path(), QStringLiteral("Fixes"), &err);
    check(proj != nullptr, QStringLiteral("(project made)"), err);
    if (!proj)
        return 1;
    Project *p = proj.get();
    p->formatting.align = Qt::AlignJustify;
    const QString projFolder = p->folderPath();
    const QString projFile = p->indexPath();
    BinderItem *splitDoc = textDoc(p, p->draft(), QStringLiteral("Split"), QStringLiteral("Before the table."));
    BinderItem *shown = textDoc(p, p->draft(), QStringLiteral("Shown"), QStringLiteral("Shown here only."));
    BinderItem *kept = textDoc(p, p->draft(), QStringLiteral("Kept"), QStringLiteral("Kept here."));
    QTextCursor(p->docs()->notes(kept->id)).insertText(QStringLiteral("Notes here."));
    BinderItem *figDoc = textDoc(p, p->draft(), QStringLiteral("Figures"), QStringLiteral("Text before the picture."));
    BinderItem *m1 = textDoc(p, p->draft(), QStringLiteral("Merge one"), QStringLiteral("First scene."));
    QTextCursor(p->docs()->notes(m1->id)).insertText(QStringLiteral("Notes of the first scene."));
    BinderItem *m2 = textDoc(p, p->draft(), QStringLiteral("Merge two"), QStringLiteral("Second scene."));
    QTextCursor(p->docs()->notes(m2->id)).insertText(QStringLiteral("Notes of the second scene."));
    BinderItem *tableDoc = textDoc(p, p->draft(), QStringLiteral("Table"), QStringLiteral("A table follows."));
    {
        // the split document: Before / Middle / table / After
        QTextDocument *d = p->docs()->text(splitDoc->id);
        QTextCursor c(d);
        c.movePosition(QTextCursor::End);
        c.insertBlock();
        c.insertText(QStringLiteral("Middle paragraph."));
        c.insertBlock();
        QTextTable *t = tables::insert(c, 2, 2, tables::preset(QStringLiteral("grid")), display::blockFormatFor(p->formatting, QString()),
                                       display::charFormatFor(p->formatting, QString()));
        const char *cells[] = {"A1 first", "B1 second", "A2 third", "B2 fourth"};
        for (int i = 0; i < 4; ++i)
            t->cellAt(i / 2, i % 2).firstCursorPosition().insertText(QString::fromLatin1(cells[i]));
        QTextCursor end(d);
        end.movePosition(QTextCursor::End);
        end.insertText(QStringLiteral("After the table."));
    }
    check(p->save(&err), QStringLiteral("(saved)"), err);

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1400, 950);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(proj)), QStringLiteral("(project open)"));
    settle(800);
    EditorPane *pane = firstPane(w);
    QAction *splitAct = w.findChild<QAction *>(QStringLiteral("split"));
    QAction *mergeAct = w.findChild<QAction *>(QStringLiteral("merge"));
    check(pane && splitAct && mergeAct, QStringLiteral("(editor and commands found)"));
    if (!pane || !splitAct || !mergeAct)
        return failures;
    // for profiling: only the typing measure (QB_V0641_ONLY_TYPING=1)
    if (qEnvironmentVariableIsSet("QB_V0641_ONLY_TYPING"))
        goto typing;

    // ================================================================ Split at Selection and tables
    {
        pane->showItems({splitDoc}, false, int(ViewMode::Single));
        settle(400);
        TextEditor *e = pane->currentEditor();
        QTextDocument *d = e->document();
        QTextTable *t = firstTable(d);
        const QString before = d->toPlainText();
        const int itemsBefore = int(p->draft()->children.size());
        // the cursor inside a cell: refused with a note, nothing changes
        QString asked;
        e->setFocus();
        e->setTextCursor(t->cellAt(0, 0).firstCursorPosition());
        answerBox(400, QMessageBox::Ok, &asked);
        splitAct->trigger();
        settle(600);
        check(d->toPlainText() == before && int(p->draft()->children.size()) == itemsBefore && asked.contains(QLatin1String("table")),
              QStringLiteral("Split at Selection inside a table cell: not done, the writer is told why; no text is lost"),
              asked + QStringLiteral(" | ") + oneLine(d->toPlainText()));
        // at the start of the paragraph right after the table: the table stays in the first document
        QTextCursor at(d);
        at.setPosition(t->lastPosition() + 1);
        e->setTextCursor(at);
        splitAct->trigger();
        settle(600);
        BinderItem *after = p->draft()->children.value(splitDoc->row() + 1);
        const QString first = p->docs()->plainText(splitDoc->id), second = after ? p->docs()->plainText(after->id) : QString();
        check(first.contains(QLatin1String("A1 first")) && first.contains(QLatin1String("B2 fourth")) && !first.contains(QLatin1String("After the"))
                  && second.trimmed() == QLatin1String("After the table."),
              QStringLiteral("Split at the paragraph right after a table: the table stays in the first document, the new one "
                             "starts with that paragraph"),
              QStringLiteral("first: %1 | second: %2").arg(oneLine(first), oneLine(second)));
        // at the start of the paragraph before the table: the table goes into the new document
        pane->showItems({splitDoc}, false, int(ViewMode::Single));
        settle(400);
        e = pane->currentEditor();
        d = e->document();
        QTextCursor mid(d);
        mid.setPosition(d->findBlockByNumber(1).position());
        e->setFocus();
        e->setTextCursor(mid);
        splitAct->trigger();
        settle(600);
        BinderItem *second2 = p->draft()->children.value(splitDoc->row() + 1);
        const QString f2 = p->docs()->plainText(splitDoc->id), s2 = second2 ? p->docs()->plainText(second2->id) : QString();
        check(f2.trimmed() == QLatin1String("Before the table.") && s2.contains(QLatin1String("Middle paragraph."))
                  && s2.contains(QLatin1String("A1 first")) && s2.contains(QLatin1String("B2 fourth")),
              QStringLiteral("Split at a paragraph before a table: the table goes with it into the new document"),
              QStringLiteral("first: %1 | second: %2").arg(oneLine(f2), oneLine(s2)));
    }

    // ================================================================ a text only shown here, changed elsewhere
    {
        pane->showItems({shown}, false, int(ViewMode::Single));
        settle(400);
        TextEditor *e = pane->currentEditor();
        const QString path = p->docs()->textPath(shown->id);
        QTest::qWait(1100);
        writeAll(path, "Changed on the other computer.\n");
        // a change that leaves the text as it was read (showing a text can mark it as changed)
        QTextCursor c(e->document());
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral("x"));
        c.deletePreviousChar();
        p->save(&err);
        settle(400);
        const QStringList copies = QDir(projFolder + QStringLiteral("/settings/conflicts")).entryList({QStringLiteral("Shown*")}, QDir::Files);
        check(readAll(path) == "Changed on the other computer.\n" && copies.isEmpty() && !visibleBox(QStringLiteral("conflictNotice"))
                  && snapshots::list(p, shown->id).isEmpty(),
              QStringLiteral("a text only shown here and changed elsewhere is not written over (no copy, no snapshot, no notice)"),
              QString::fromUtf8(readAll(path)) + QStringLiteral(" | copies: ") + copies.join(QLatin1String(", ")));
        if (QMessageBox *nb = visibleBox(QStringLiteral("conflictNotice")))
            nb->accept();
    }

    // ================================================================ the other version must be kept before ours is written
    {
        const QString textPath = p->docs()->textPath(kept->id), notesPath = p->docs()->notesPath(kept->id);
        QTextCursor tc(p->docs()->text(kept->id));
        tc.movePosition(QTextCursor::End);
        tc.insertText(QStringLiteral(" Edited here."));
        QTextCursor nc(p->docs()->notes(kept->id));
        nc.movePosition(QTextCursor::End);
        nc.insertText(QStringLiteral(" More notes here."));
        QTest::qWait(1100);
        writeAll(textPath, "Other computer text.\n");
        writeAll(notesPath, "Other computer notes.\n");
        // nowhere to keep them: settings/conflicts and the document's snapshot folder are plain files
        // (permissions would not stop the tests' root user)
        const QString conflicts = projFolder + QStringLiteral("/settings/conflicts");
        QDir(conflicts).removeRecursively();
        writeAll(conflicts, "blocked");
        QDir().mkpath(projFolder + QStringLiteral("/snapshots"));
        QDir(p->snapshotsDir(kept->id)).removeRecursively();
        writeAll(p->snapshotsDir(kept->id), "blocked");
        QString warn;
        answerBox(500, QMessageBox::Ok, &warn);
        QString saveErr;
        const bool saved = p->save(&saveErr);
        settle(800);
        check(!saved && readAll(textPath) == "Other computer text.\n" && readAll(notesPath) == "Other computer notes.\n"
                  && p->docs()->hasDirty() && !visibleBox(QStringLiteral("conflictNotice")),
              QStringLiteral("changed elsewhere and here, and the other version cannot be kept: it is not written over, the "
                             "save fails, this computer's text stays unsaved, no notice says it was kept"),
              QStringLiteral("saved %1 | %2 | %3").arg(saved).arg(QString::fromUtf8(readAll(textPath)), saveErr));
        check(saveErr.contains(QLatin1String("could not be kept")) && saveErr.contains(kept->id),
              QStringLiteral("... the error names the file and why"), saveErr);
        // free again: now kept, then written
        QFile::remove(conflicts);
        QFile::remove(p->snapshotsDir(kept->id));
        const bool saved2 = p->save(&saveErr);
        settle(500);
        const QStringList copies = QDir(conflicts).entryList(QDir::Files);
        check(saved2 && QString::fromUtf8(readAll(textPath)).contains(QLatin1String("Edited here."))
                  && QString::fromUtf8(readAll(notesPath)).contains(QLatin1String("More notes here.")) && copies.size() == 2
                  && !snapshots::list(p, kept->id).isEmpty(),
              QStringLiteral("... once it can be kept: kept (copies and a snapshot), then this computer's text is saved"),
              copies.join(QLatin1String(", ")) + QStringLiteral(" | ") + saveErr);
        if (QMessageBox *nb = visibleBox(QStringLiteral("conflictNotice")))
            nb->accept();
        // the project file
        QTest::qWait(1100);
        const QByteArray theirs = readAll(projFile) + "\n<!-- written by another computer -->\n";
        writeAll(projFile, theirs);
        QDir(conflicts).removeRecursively();
        writeAll(conflicts, "blocked");
        tableDoc->title = QStringLiteral("Table (renamed)");
        p->notifyItemChanged(tableDoc);
        answerBox(500, QMessageBox::Ok);
        const bool saved3 = p->save(&saveErr);
        settle(800);
        check(!saved3 && readAll(projFile) == theirs && saveErr.contains(QLatin1String("project file")),
              QStringLiteral("the project file changed elsewhere and not keepable: not written over, the save fails"), saveErr);
        QFile::remove(conflicts);
        const bool saved4 = p->save(&saveErr);
        settle(400);
        check(saved4 && readAll(projFile).contains("Table (renamed)")
                  && !QDir(conflicts).entryList({QStringLiteral("project file*")}, QDir::Files).isEmpty(),
              QStringLiteral("... once it can be kept: kept, then written"), saveErr);
        if (QMessageBox *nb = visibleBox(QStringLiteral("conflictNotice")))
            nb->accept();
    }

    // ================================================================ a figure cut and pasted
    {
        pane->showItems({figDoc}, false, int(ViewMode::Single));
        settle(400);
        TextEditor *e = pane->currentEditor();
        QTextDocument *d = e->document();
        QImage img(80, 60, QImage::Format_RGB32);
        img.fill(QColor(30, 90, 200));
        const QString rel = p->docs()->importImage(figDoc->id, img);
        d->addResource(QTextDocument::ImageResource, QUrl(rel), img);
        QTextCursor c(d);
        c.movePosition(QTextCursor::End);
        c.insertBlock();
        const int imagePos = c.position();
        QTextImageFormat imf;
        imf.setName(rel);
        c.insertImage(imf);
        c.insertBlock();
        c.insertText(QStringLiteral("Text after the picture."));
        e->setPicture(imagePos, 80, figures::Left, QStringLiteral("A caption"), QStringLiteral("A blue box"));
        settle(200);
        QList<QTextFrame *> figs = figuresIn(d);
        check(figs.size() == 1, QStringLiteral("(a figure beside the text)"));
        if (!figs.isEmpty()) {
            QTextCursor sel(d);
            sel.setPosition(figs.first()->firstPosition() - 1);
            sel.setPosition(figs.first()->lastPosition() + 1, QTextCursor::KeepAnchor);
            e->setFocus();
            e->setTextCursor(sel);
            e->cut();
            settle(200);
            const int afterCut = int(figuresIn(d).size());
            QTextCursor pc(d);
            pc.movePosition(QTextCursor::End);
            e->setTextCursor(pc);
            e->paste();
            settle(400);
            figs = figuresIn(d);
            QString name;
            const bool pic = !figs.isEmpty() && figureHasPicture(d, figs.first(), &name);
            if (!(afterCut == 0 && figs.size() == 1 && pic)) {
                // what the clipboard held (without picture data) and what the text became
                QString rtf = QString::fromLatin1(QApplication::clipboard()->mimeData() ? QApplication::clipboard()->mimeData()->data(QStringLiteral("text/rtf")) : QByteArray());
                rtf.replace(QRegularExpression(QStringLiteral("[0-9a-f]{64,}")), QStringLiteral("<hex>"));
                info(QStringLiteral("clipboard RTF: ") + rtf.right(1500));
                info(QStringLiteral("text now: ") + oneLine(d->toPlainText()) + QStringLiteral(" | frames %1").arg(d->rootFrame()->childFrames().size()));
                QTextDocument probe;
                rtf::ReadOptions ro;
                ro.keepFormatting = true;
                rtf::read(QApplication::clipboard()->mimeData()->data(QStringLiteral("text/rtf")), &probe, p->formatting, ro);
                info(QStringLiteral("the clipboard's RTF read alone: frames %1, figures %2, text %3")
                         .arg(probe.rootFrame()->childFrames().size()).arg(figuresIn(&probe).size()).arg(oneLine(probe.toPlainText())));
            }
            check(afterCut == 0 && figs.size() == 1 && pic && d->toPlainText().contains(QLatin1String("A caption")),
                  QStringLiteral("a figure cut and pasted is a figure again, with its picture and its caption"),
                  QStringLiteral("after the cut %1, after the paste %2, picture %3 (%4); clipboard: %5")
                      .arg(afterCut).arg(figs.size()).arg(pic).arg(name,
                           QApplication::clipboard()->mimeData() ? QApplication::clipboard()->mimeData()->formats().join(QLatin1String(", ")) : QString()));
            shot(&w, QStringLiteral("01-figure-pasted"));
        }
    }

    // ================================================================ Merge also merges the notes
    {
        auto *binder = w.findChild<BinderPanel *>();
        if (binder) {
            binder->select({m1, m2}, true);
            binder->setFocus();
            settle(200);
        }
        answerBox(400, QMessageBox::Yes);
        mergeAct->trigger();
        settle(800);
        const QString notes = p->docs()->notesPlainText(m1->id), text = p->docs()->plainText(m1->id);
        check(text.contains(QLatin1String("Second scene.")) && notes.contains(QLatin1String("Notes of the first scene."))
                  && notes.contains(QLatin1String("Notes of the second scene.")),
              QStringLiteral("Merge: the notes of the merged documents are merged too"), oneLine(text) + QStringLiteral(" | ") + oneLine(notes));
    }

    // ================================================================ compiling lets the event loop run
    {
        QTextDocument *d = p->docs()->text(tableDoc->id);
        QTextCursor c(d);
        c.movePosition(QTextCursor::End);
        for (int i = 0; i < 1500; ++i) {
            c.insertBlock();
            c.insertText(QStringLiteral("Paragraph %1 of a long text, with enough words to fill a line or two of the page.").arg(i));
        }
        c.insertBlock();
        QTextTable *t = tables::insert(c, 120, 3, tables::preset(QStringLiteral("header")), display::blockFormatFor(p->formatting, QString()),
                                       display::charFormatFor(p->formatting, QString()));
        for (int r = 0; r < 120; ++r)
            for (int col = 0; col < 3; ++col)
                t->cellAt(r, col).firstCursorPosition().insertText(QStringLiteral("Row %1 cell %2").arg(r).arg(col + 1));
        p->save(&err);
        int calls = 0;
        QElapsedTimer clock;
        clock.start();
        qint64 last = 0, largest = 0;
        double shareBefore = -2, shareAfter = -2, lastShare = -2;
        bool ok = false;
        {
            progress::Scope scope([&](double share) {
                ++calls;
                if (clock.elapsed() - last > largest) {
                    largest = clock.elapsed() - last;
                    shareBefore = lastShare;
                    shareAfter = share;
                }
                last = clock.elapsed();
                lastShare = share;
            });
            AuthorInfo a;
            Compiler cc(p, formatByName(QStringLiteral("Paperback (6 x 9 in)")), p->compile, a);
            const Manuscript m = cc.build();
            PdfExporter ex;
            ok = ex.exportManuscript(m, work.path() + QStringLiteral("/long.pdf"), &err);
        }
        largest = qMax(largest, clock.elapsed() - last);
        check(ok && (calls >= 1 || clock.elapsed() < 1000) && largest < 3000,
              QStringLiteral("compiling a long text with a long table to PDF lets the event loop run regularly (the freeze "
                             "watchdog's timer)"),
              QStringLiteral("%1 ms in all, %2 calls, the longest wait %3 ms (between shares %4 and %5); %6")
                  .arg(clock.elapsed()).arg(calls).arg(largest).arg(shareBefore).arg(shareAfter).arg(err));
        info(QStringLiteral("(compile to PDF: %1 ms, %2 progress calls, longest wait %3 ms)").arg(clock.elapsed()).arg(calls).arg(largest));
        // the cells in Word and e-book are not justified (the body text is)
        AuthorInfo a;
        Compiler cc(p, formatByName(QStringLiteral("Paperback (6 x 9 in)")), p->compile, a);
        const Manuscript m = cc.build();
        const QString docx = work.path() + QStringLiteral("/cells.docx"), epub = work.path() + QStringLiteral("/cells.epub");
        DocxExporter dx;
        EpubExporter ep;
        const bool okD = dx.exportManuscript(m, docx, &err), okE = ep.exportManuscript(m, epub, &err);
        const QString xml = runTool(QStringLiteral("unzip"), {QStringLiteral("-p"), docx, QStringLiteral("word/document.xml")});
        const int tc = int(xml.indexOf(QLatin1String("<w:tc>")));
        const QString firstCell = tc >= 0 ? xml.mid(tc, int(xml.indexOf(QLatin1String("</w:tc>"), tc)) - tc) : QString();
        QString css;
        for (const QString &fn : runTool(QStringLiteral("unzip"), {QStringLiteral("-Z1"), epub}).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            if (fn.endsWith(QLatin1String(".css")))
                css += runTool(QStringLiteral("unzip"), {QStringLiteral("-p"), epub, fn});
        check(okD && okE && firstCell.contains(QLatin1String("<w:jc w:val=\"left\"/>"))
                  && css.contains(QLatin1String("table.qbtable p { margin: 0; text-indent: 0; text-align: left; }")),
              QStringLiteral("compile: table cells are left-aligned in Word and e-book while the body text is justified"),
              firstCell.left(600));
    }

    // ================================================================ Merge Cells, Undo, Redo, Delete Rows
    // the steps of the system test's S-T01 before its crash: commands from the menu bar, each undone and redone
    {
        pane->showItems({tableDoc}, false, int(ViewMode::Single));
        settle(400);
        TextEditor *e = pane->currentEditor();
        QTextCursor c(e->document());
        c.setPosition(0);
        c.movePosition(QTextCursor::EndOfBlock);
        e->setTextCursor(c);
        e->setFocus();
        e->insertTableAtCursor(3, 3, tables::preset(QStringLiteral("header")));
        settle(200);
        QTextTable *t = e->currentTable();
        QPointer<QTextTable> alive(t);
        bool replaced = false;
        auto shape = [&] {
            if (!alive) {
                // the table object was deleted (Qt makes a new one when undo or redo brings the table back)
                replaced = true;
                t = firstTable(e->document());
                alive = t;
            }
            QString out;
            for (int r = 0; r < t->rows(); ++r)
                for (int col = 0; col < t->columns(); ++col) {
                    const QTextTableCell cell = t->cellAt(r, col);
                    out += QStringLiteral("[%1,%2 %3x%4 %5]").arg(cell.row()).arg(cell.column()).arg(cell.rowSpan()).arg(cell.columnSpan())
                               .arg(cellParas(t, cell.row(), cell.column()).join(QLatin1Char('/')));
                }
            return out;
        };
        QStringList steps;
        bool allUndone = true, allRedone = true, stayed = true;
        auto command = [&](const char *name) {
            w.activateWindow();
            (void)QTest::qWaitForWindowActive(&w, 1000);
            e->setFocus();
            settle(30);
            QAction *a = w.findChild<QAction *>(QLatin1String(name));
            const QString before = shape();
            const bool enabled = a && a->isEnabled();
            if (enabled)
                a->trigger();
            settle(80);
            const QString after = shape();
            const bool replacedBefore = replaced;
            QTest::keyClick(e, Qt::Key_Z, Qt::ControlModifier);
            settle(60);
            const bool undone = shape() == before;
            const bool replacedByUndo = replaced && !replacedBefore;
            QTest::keyClick(e, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
            settle(60);
            const bool redone = shape() == after;
            if (replaced && !replacedBefore)
                info(QStringLiteral("(the table object was replaced by the %1 of %2)").arg(replacedByUndo ? QStringLiteral("undo") : QStringLiteral("redo")).arg(QLatin1String(name)));
            info(QStringLiteral("(%1, enabled %2: %3 -> %4)").arg(QLatin1String(name)).arg(enabled).arg(before.left(200), after.left(200)));
            const bool inTable = e->textCursor().currentTable() == t;
            allUndone = allUndone && undone;
            allRedone = allRedone && redone;
            stayed = stayed && enabled && inTable;
            steps << QStringLiteral("%1: enabled %2, undo %3, redo %4, cursor in the table %5").arg(QLatin1String(name)).arg(enabled).arg(undone).arg(redone).arg(inTable);
        };
        if (t) {
            e->setTextCursor(t->cellAt(1, 1).firstCursorPosition());
            QTest::keyClicks(e, QStringLiteral("First line"));
            QTest::keyClick(e, Qt::Key_Return);
            QTest::keyClicks(e, QStringLiteral("Second line"));
            e->setTextCursor(t->cellAt(t->rows() - 1, t->columns() - 1).firstCursorPosition());
            QTest::keyClick(e, Qt::Key_Tab); // adds a row
            e->setTextCursor(t->cellAt(1, 1).firstCursorPosition());
            for (const char *name : {"tableRowAbove", "tableRowBelow", "tableColLeft", "tableColRight"})
                command(name);
            QTextCursor sel = t->cellAt(1, 1).firstCursorPosition();
            sel.setPosition(t->cellAt(2, 2).lastCursorPosition().position(), QTextCursor::KeepAnchor);
            e->setTextCursor(sel);
            command("tableMerge");
            const bool merged = t->cellAt(1, 1).rowSpan() == 2 && t->cellAt(1, 1).columnSpan() == 2;
            e->setTextCursor(t->cellAt(2, t->columns() - 1).firstCursorPosition());
            command("tableDeleteRows");
            e->setTextCursor(t->cellAt(1, 2).firstCursorPosition());
            command("tableDeleteCols");
            check(merged && allUndone && allRedone && stayed,
                  QStringLiteral("Format > Table commands from the menu bar, each undone and redone (also Merge Cells, then Delete "
                                 "Rows and Delete Columns under the merged cell): no crash, the same table again, the cursor "
                                 "stays in the table"),
                  steps.join(QLatin1String(" | ")) + QStringLiteral(" | merged %1").arg(merged));
        } else {
            check(false, QStringLiteral("(a table inserted)"));
        }
    }

    // ================================================================ typing in a cell of a 1,000-row table
typing:
    {
        // the same table in a plain QTextEdit (Qt's own share) and in the program's editor
        auto typeInto = [](QTextEdit *e, int keys) {
            QList<double> ms;
            for (int i = 0; i < keys; ++i) {
                QElapsedTimer t;
                t.start();
                QTest::keyClick(e, Qt::Key_A);
                QCoreApplication::processEvents(); // layout and paint
                ms << t.nsecsElapsed() / 1e6;
            }
            std::sort(ms.begin(), ms.end());
            return ms;
        };
        auto fill = [](QTextCursor c, const Formatting &f) {
            QTextTable *t = tables::insert(c, 1000, 3, tables::preset(QStringLiteral("header")), display::blockFormatFor(f, QString()),
                                           display::charFormatFor(f, QString()));
            for (int r = 0; r < 1000; ++r)
                for (int col = 0; col < 3; ++col)
                    t->cellAt(r, col).firstCursorPosition().insertText(QStringLiteral("Row %1 cell").arg(r));
            return t;
        };
        const int keys = qEnvironmentVariableIntValue("QB_V0641_KEYS") > 0 ? qEnvironmentVariableIntValue("QB_V0641_KEYS") : 40;
        QTextEdit plain;
        plain.resize(800, 600);
        plain.show();
        QTextTable *pt = fill(QTextCursor(plain.document()), p->formatting);
        settle(300);
        plain.setFocus();
        plain.setTextCursor(pt->cellAt(5, 1).lastCursorPosition());
        const QList<double> qt = typeInto(&plain, keys);
        plain.hide();
        BinderItem *big = textDoc(p, p->draft(), QStringLiteral("Big table"), QStringLiteral("A big table:"));
        QTextCursor bc(p->docs()->text(big->id));
        bc.movePosition(QTextCursor::End);
        bc.insertBlock();
        QTextTable *bt = fill(bc, p->formatting);
        pane->showItems({big}, false, int(ViewMode::Single));
        settle(800);
        TextEditor *e = pane->currentEditor();
        e->setFocus();
        e->setTextCursor(bt->cellAt(5, 1).lastCursorPosition());
        settle(200);
        const QList<double> ours = typeInto(e, keys);
        // a small table above the big one (the system test's S-T01 types there first)
        QTextCursor top(e->document());
        top.setPosition(0);
        top.movePosition(QTextCursor::EndOfBlock);
        e->setTextCursor(top);
        e->insertTableAtCursor(3, 3, tables::preset(QStringLiteral("header")));
        settle(300);
        QElapsedTimer above;
        above.start();
        QTest::keyClicks(e, QStringLiteral("First"));
        QTest::keyClick(e, Qt::Key_Return);
        QTest::keyClicks(e, QStringLiteral("Second"));
        QCoreApplication::processEvents();
        const qint64 aboveMs = above.elapsed();
        info(QStringLiteral("(12 keys in a small table above the big one: %1 ms)").arg(aboveMs));
        check(aboveMs < 12 * 1000, QStringLiteral("typing in a small table above a 1,000-row table: no key takes seconds"),
              QStringLiteral("%1 ms for 12 keys").arg(aboveMs));
        auto med = [](const QList<double> &l) { return l.value(l.size() / 2); };
        auto p95 = [](const QList<double> &l) { return l.value(int(l.size() * 0.95)); };
        info(QStringLiteral("(typing in a cell of a 1,000 x 3 table, %1 keys: Qt alone median %2 ms, p95 %3 ms; the program's "
                            "editor median %4 ms, p95 %5 ms)")
                 .arg(keys).arg(med(qt), 0, 'f', 1).arg(p95(qt), 0, 'f', 1).arg(med(ours), 0, 'f', 1).arg(p95(ours), 0, 'f', 1));
        check(med(ours) <= med(qt) * 1.25 + 10,
              QStringLiteral("typing in a big table: the program's editor adds little to Qt's own table layout (median at most "
                             "Qt's + 25 % + 10 ms)"),
              QStringLiteral("Qt %1 ms, the program %2 ms").arg(med(qt), 0, 'f', 1).arg(med(ours), 0, 'f', 1));
    }

    w.closeProject();
    settle(300);
    delete wp;
    std::printf("%d failure(s)\n", failures);
    return failures;
}
