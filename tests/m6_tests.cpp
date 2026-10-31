// Scripted run for page view and copyholders (milestone M6): builds a
// project, switches the page view on and off, changes paper and zoom, opens
// copyholders and saves screenshots into the folder given as first argument.
// Exit code = number of failed checks.
//
//   QT_QPA_PLATFORM=offscreen ./qb_m6_tests /tmp/shots
#include "compile/PluginManager.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/PageSetup.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "text/PageLayout.h"
#include "ui/AppSettings.h"
#include "ui/ProjectLook.h"
#include "ui/BinderPanel.h"
#include "ui/Copyholder.h"
#include "ui/Corkboard.h"
#include "ui/EditorPane.h"
#include "ui/Inspector.h"
#include "ui/MainWindow.h"
#include "ui/MediaView.h"
#include "ui/Outliner.h"
#include "ui/PageDialogs.h"
#include "ui/Ruler.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"
#include "ui/Windows.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QLabel>
#include <QLineEdit>
#include <QTreeView>
#include <QWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QRadioButton>
#include <QScrollBar>
#include <QSplitter>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextFrame>
#include <QTextLayout>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#include <cstdio>

using namespace qb;

static int failures = 0;
static QString outDir;
static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail));
    }
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
    settle(500);
    w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}

static QAction *byKey(QWidget *w, const QString &key)
{
    QAction *a = w->findChild<QAction *>(key);
    if (!a)
        std::printf("      (action key not found: %s)\n", qPrintable(key));
    return a;
}

static QString menuText(QString t)
{
    const int tab = int(t.indexOf(QLatin1Char('\t')));
    if (tab >= 0)
        t.truncate(tab);
    t.remove(QLatin1Char('&'));
    return t;
}

static QAction *menuPath(QMainWindow *w, const QStringList &path)
{
    QList<QAction *> level = w->menuBar()->actions();
    QAction *found = nullptr;
    for (int i = 0; i < path.size(); ++i) {
        found = nullptr;
        for (QAction *a : level)
            if (menuText(a->text()) == path[i]) {
                found = a;
                break;
            }
        if (!found)
            return nullptr;
        if (i + 1 < path.size()) {
            if (!found->menu())
                return nullptr;
            emit found->menu()->aboutToShow();
            level = found->menu()->actions();
        }
    }
    return found;
}

static QString longText(int paragraphs, const QString &tag)
{
    QString t;
    for (int i = 1; i <= paragraphs; ++i)
        t += QStringLiteral("%1 paragraph %2. The keeper climbed the stairs with the lamp oil and counted the steps as he "
                            "always did, one hundred and twelve to the gallery, and the wind leaned on the glass while the "
                            "sea below kept its own slow count of the rocks.\n")
                 .arg(tag)
                 .arg(i);
    return t;
}

// Every line of the text lies on a sheet, between its top and bottom margin.
static bool linesOnSheets(TextEditor *e, QString *why)
{
    const PageMetrics m = pagelayout::metrics(e->document());
    QAbstractTextDocumentLayout *lay = e->document()->documentLayout();
    for (QTextBlock b = e->document()->begin(); b.isValid(); b = b.next()) {
        const QRectF br = lay->blockBoundingRect(b);
        const QTextLayout *tl = b.layout();
        for (int i = 0; i < tl->lineCount(); ++i) {
            const QRectF lr = tl->lineAt(i).rect().translated(br.topLeft());
            const int page = int(lr.top()) / m.step();
            const double top = lr.top() - page * m.step(), bottom = lr.bottom() - page * m.step();
            if (top < m.gapAbove() + m.top - 1 || bottom > m.gapAbove() + m.paperH - m.bottom + 1) {
                *why = QStringLiteral("block %1 line %2: %3..%4 on page %5 (text area %6..%7)")
                           .arg(b.blockNumber())
                           .arg(i)
                           .arg(top)
                           .arg(bottom)
                           .arg(page + 1)
                           .arg(m.gapAbove() + m.top)
                           .arg(m.gapAbove() + m.paperH - m.bottom);
                return false;
            }
        }
    }
    return true;
}

static QList<QuickRefWindow *> quickRefs()
{
    QList<QuickRefWindow *> out;
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (auto *qr = qobject_cast<QuickRefWindow *>(tl))
            if (qr->isVisible())
                out << qr;
    return out;
}

// Drop binder items on a widget as a drag from the binder would.
static void dropItems(QWidget *target, Project *proj, const QList<BinderItem *> &items, Qt::KeyboardModifiers mods)
{
    QModelIndexList idx;
    for (BinderItem *it : items)
        idx << proj->model()->indexFor(it);
    QMimeData *mime = proj->model()->mimeData(idx);
    const QPointF pos(target->width() / 2.0, target->height() / 2.0);
    QDragEnterEvent enter(pos.toPoint(), Qt::MoveAction | Qt::CopyAction, mime, Qt::LeftButton, mods);
    QApplication::sendEvent(target, &enter);
    QDropEvent drop(pos, Qt::MoveAction | Qt::CopyAction, mime, Qt::LeftButton, mods);
    QApplication::sendEvent(target, &drop);
    delete mime;
    settle(300);
}

// A drag with the mouse, through Qt's own drag and drop: press on the source,
// move, release on the target. The drag runs its own event loop inside the
// move that starts it, so the rest of the gesture is scheduled beforehand.
static void mouseDrag(QWidget *window, QWidget *source, const QPoint &from, QWidget *target, const QPoint &to,
                      Qt::KeyboardModifiers mods)
{
    QWindow *win = window->windowHandle();
    const QPoint a = source->mapTo(window, from), b = target->mapTo(window, to);
    QTimer::singleShot(400, window, [=] { QTest::mouseEvent(QTest::MouseMove, win, Qt::LeftButton, mods, (a + b) / 2); });
    QTimer::singleShot(600, window, [=] { QTest::mouseEvent(QTest::MouseMove, win, Qt::LeftButton, mods, b - QPoint(3, 0)); });
    QTimer::singleShot(800, window, [=] { QTest::mouseEvent(QTest::MouseMove, win, Qt::LeftButton, mods, b); });
    QTimer::singleShot(1000, window, [=] { QTest::mouseEvent(QTest::MouseRelease, win, Qt::LeftButton, mods, b); });
    QTest::mouseEvent(QTest::MousePress, win, Qt::LeftButton, mods, a);
    QTest::mouseEvent(QTest::MouseMove, win, Qt::LeftButton, mods, a + QPoint(4, 6));
    QTest::mouseEvent(QTest::MouseMove, win, Qt::LeftButton, mods, a + QPoint(12, 18)); // (returns when the drag is over)
    settle(1300);
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderM6Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderM6Test"));
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany)); // metric: A4, centimetres
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));

    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    // (0.40.2) written for one look for the whole program: the project shows the settings' look,
    // without a border beside the text (tests/v0402_tests.cpp checks the look per project)
    look::setFollowSettings(true);
    as.textBorder = 0;
    theme::apply();
    compile::PluginManager::instance().load();

    // ---------------------------------------------------------------- page setup (no window)
    {
        const PageSetup a4 = PageSetup::forLocale();
        const PageMetrics m = PageMetrics::from(a4, 1.0);
        check(a4.paper == QLatin1String("A4") && m.paperW == 794 && m.paperH == 1123 && m.left == 94 && m.top == 94
                  && m.step() == m.paperH + m.gap,
              QStringLiteral("A4 with 25 mm margins is 794 x 1123 px at 100 %, margins 94 px"),
              QStringLiteral("%1 x %2, %3").arg(m.paperW).arg(m.paperH).arg(m.left));
        const PageMetrics m2 = PageMetrics::from(a4, 1.5);
        PageSetup land = a4;
        land.landscape = true;
        const PageMetrics m3 = PageMetrics::from(land, 1.0);
        check(m2.paperW == 1191 && m2.left == 142 && m2.gap == m.gap && m3.paperW == 1123 && m3.paperH == 794,
              QStringLiteral("zoom scales paper and margins (not the gap); landscape swaps the sides"));
        PageSetup odd;
        odd.paper = QStringLiteral("Custom");
        odd.width = 100;
        odd.height = 120;
        odd.left = odd.right = 80;
        odd.top = odd.bottom = 5;
        const PageSetup fixed = odd.normalized();
        const PageSetup back = PageSetup::fromVariant(fixed.toVariant(), a4);
        check(fixed.left + fixed.right <= 70.01 && fixed.top == 5 && back == fixed
                  && PageSetup::fromVariant(QVariantMap(), a4) == a4,
              QStringLiteral("margins that leave no room for text are reduced; a page survives storing and reading"),
              QStringLiteral("%1 %2").arg(fixed.left).arg(fixed.right));

        // layout of a bare document: pages, and no trace in the undo history
        QTextDocument doc;
        doc.setDocumentMargin(6);
        doc.setPlainText(longText(60, QStringLiteral("Bare")));
        doc.setTextWidth(600);
        doc.clearUndoRedoStacks();
        doc.setModified(false);
        QTextCursor c(&doc);
        c.insertText(QStringLiteral("Typed. "));
        const int steps = doc.availableUndoSteps();
        int changes = 0;
        QObject::connect(&doc, &QTextDocument::contentsChanged, &doc, [&changes] { ++changes; });
        const bool applied = pagelayout::apply(&doc, m);
        const int pages = pagelayout::pageCount(&doc);
        const bool again = pagelayout::apply(&doc, m);
        const bool zoomed = pagelayout::apply(&doc, m2);
        const int pagesZoomed = pagelayout::pageCount(&doc);
        doc.undo();
        const bool undone = !doc.toPlainText().startsWith(QLatin1String("Typed")) && pagelayout::matches(&doc, m2);
        doc.redo();
        const bool cleared = pagelayout::clear(&doc, 600);
        check(applied && pages >= 3 && !again && zoomed && pagesZoomed >= 2 && doc.availableUndoSteps() == steps && changes == 2
                  && undone && cleared && !pagelayout::isPaged(&doc) && doc.pageCount() == 1
                  && qRound(doc.rootFrame()->frameFormat().topMargin()) == 6 && !pagelayout::clear(&doc, 600),
              QStringLiteral("pages are laid out and removed without touching the undo history or the text"),
              QStringLiteral("applied %1 pages %2 again %3 zoomed %4/%5 steps %6/%7 changes %8 undone %9 cleared %10")
                  .arg(applied).arg(pages).arg(again).arg(zoomed).arg(pagesZoomed).arg(doc.availableUndoSteps()).arg(steps)
                  .arg(changes).arg(undone).arg(cleared));
        // a copy for printing loses the screen pages
        pagelayout::apply(&doc, m);
        QTextDocument *copy = doc.clone();
        const bool copyPaged = copy->pageSize().height() > 0;
        pagelayout::clear(copy, 600);
        check(copyPaged && copy->pageSize().height() <= 0 && qRound(copy->rootFrame()->frameFormat().leftMargin()) == qRound(copy->documentMargin()),
              QStringLiteral("a copy made for printing is freed from the pages of the screen"),
              QStringLiteral("%1 %2 %3").arg(copyPaged).arg(copy->pageSize().height()).arg(copy->rootFrame()->frameFormat().leftMargin()));
        delete copy;
    }

    // ---------------------------------------------------------------- project
    QTemporaryDir projectsTmp;
    const QString projectsPath = argc > 2 ? QString::fromLocal8Bit(argv[2]) : projectsTmp.path();
    QDir().mkpath(projectsPath);
    templates::Info novel;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("novel"))
            novel = t;
    QString err;
    auto p = templates::createProject(novel, projectsPath, QStringLiteral("The Lighthouse"), &err);
    check(p != nullptr, QStringLiteral("create project"), err);
    if (!p)
        return 1;
    const QString projectFile = p->indexPath();

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.resize(1500, 950);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(p)), QStringLiteral("open project in main window"));
    Project *proj = w.project();
    settle(400);

    BinderModel *m = proj->model();
    BinderItem *chapter = m->createItem(proj->draft(), 0, ItemType::Folder, QStringLiteral("The Keeper"));
    BinderItem *longDoc = m->createItem(chapter, 0, ItemType::Text, QStringLiteral("Counting Ships"));
    BinderItem *shortDoc = m->createItem(chapter, 1, ItemType::Text, QStringLiteral("The Logbook"));
    BinderItem *midDoc = m->createItem(chapter, 2, ItemType::Text, QStringLiteral("Morning Fog"));
    BinderItem *notesDoc = m->createItem(proj->research(), 0, ItemType::Text, QStringLiteral("Tide Tables"));
    templates::setMarkdown(proj, longDoc, longText(40, QStringLiteral("Ships")));
    templates::setMarkdown(proj, shortDoc, QStringLiteral("He wrote the number in the margin: **forty-one**, not forty.\n"));
    templates::setMarkdown(proj, midDoc, longText(9, QStringLiteral("Fog")));
    templates::setMarkdown(proj, notesDoc, QStringLiteral("High water 04:12 and 16:40.\nLow water 10:25 and 22:51.\n"));
    m->refreshAll();
    proj->save();
    settle(200);

    auto *binder = w.findChild<BinderPanel *>();
    auto *editorSplit = qobject_cast<QSplitter *>(w.findChild<EditorPane *>()->parentWidget());
    auto *pane0 = editorSplit ? qobject_cast<EditorPane *>(editorSplit->widget(0)) : nullptr;
    auto *pane1 = editorSplit ? qobject_cast<EditorPane *>(editorSplit->widget(1)) : nullptr;
    auto *inspector = w.findChild<Inspector *>();
    check(binder && pane0 && pane1 && inspector, QStringLiteral("window parts found"));
    if (!binder || !pane0 || !pane1)
        return 1;
    DocumentStore *docs = proj->docs();
    QAction *pageView = byKey(&w, QStringLiteral("pageView"));

    // ================================================================ PAGE VIEW
    {
        // ---- menus
        QAction *inMenu = menuPath(&w, {QStringLiteral("View"), QStringLiteral("Text Editing"), QStringLiteral("Page View")});
        QAction *sizeEntry = menuPath(&w, {QStringLiteral("View"), QStringLiteral("Text Editing"), QStringLiteral("Page Size and Margins…")});
        bool inViewButton = false;
        if (auto *vb = w.findChild<QToolButton *>(QStringLiteral("viewButton")))
            if (vb->menu())
                inViewButton = vb->menu()->actions().contains(pageView);
        check(inMenu == pageView && pageView && pageView->isCheckable() && !pageView->isChecked() && sizeEntry && inViewButton,
              QStringLiteral("View > Text Editing > Page View and the View button offer the page view"),
              QStringLiteral("%1 %2 %3").arg(inMenu != nullptr).arg(sizeEntry != nullptr).arg(inViewButton));

        binder->select({longDoc}, true);
        settle(300);
        TextEditor *ed = pane0->currentEditor();
        check(ed && !ed->isPaged() && ed->pageCount() == 0 && !pane0->findChild<QLabel *>(QStringLiteral("pageLabel"))->isVisible(),
              QStringLiteral("without page view the text is one column and no page number is shown"));
        const int flowWidth = ed ? ed->width() : 0;
        const int flowHeight = ed ? ed->height() : 0;
        shot(&w, QStringLiteral("m6-01-before-page-view"));

        // typed text must stay undoable through everything that follows
        ed->setFocus();
        QTextCursor c = ed->textCursor();
        c.movePosition(QTextCursor::Start);
        ed->setTextCursor(c);
        QTest::keyClicks(ed, QStringLiteral("Opening. "));
        settle(100);
        proj->save();
        const int undoSteps = ed->document()->availableUndoSteps();
        const bool dirtyBefore = docs->hasDirty();

        // ---- on
        pageView->trigger();
        settle(400);
        ed = pane0->currentEditor();
        const PageMetrics pm = docs->pageMetrics();
        const int pages = ed->pageCount();
        QString why;
        const bool onSheets = linesOnSheets(ed, &why);
        const QRectF firstBlock = ed->document()->documentLayout()->blockBoundingRect(ed->document()->begin());
        check(pageView->isChecked() && docs->pageView() && ed->isPaged() && pages >= 3 && ed->width() == pm.paperW
                  && ed->height() == pages * pm.step() && qRound(firstBlock.top()) == pm.gapAbove() + pm.top
                  && qRound(firstBlock.left()) == pm.left && qRound(firstBlock.width()) == pm.paperW - pm.left - pm.right,
              QStringLiteral("page view lays the text out on A4 sheets with the page margins"),
              QStringLiteral("paged %1 pages %2 width %3/%4 height %5/%6 first block %7,%8 w %9")
                  .arg(ed->isPaged()).arg(pages).arg(ed->width()).arg(pm.paperW).arg(ed->height()).arg(pages * pm.step())
                  .arg(firstBlock.left()).arg(firstBlock.top()).arg(firstBlock.width()));
        check(onSheets, QStringLiteral("no line of text lies in the gap between two sheets or in a page margin"), why);
        check(ed->document()->availableUndoSteps() == undoSteps && docs->hasDirty() == dirtyBefore,
              QStringLiteral("switching the page view on is no editing step and does not mark the text as changed"),
              QStringLiteral("%1/%2 dirty %3/%4").arg(ed->document()->availableUndoSteps()).arg(undoSteps).arg(docs->hasDirty()).arg(dirtyBefore));

        // ---- the sheets are painted: paper, gap, edge
        {
            const QImage img = ed->viewport()->grab(QRect(0, 0, pm.paperW, pm.step() + 60)).toImage();
            const QColor paper = QColor(theme::color(theme::Role::EditorPaper).rgb()), outside = QColor(theme::color(theme::Role::EditorPageSurround).rgb());
            const QColor inGapTop = img.pixelColor(pm.paperW / 2, 2);
            const QColor inGap = img.pixelColor(pm.paperW / 2, pm.step());           // between sheet 1 and 2
            const QColor onPaper = img.pixelColor(20, pm.gapAbove() + 20);            // left margin of sheet 1
            const QColor onPaper2 = img.pixelColor(20, pm.step() + pm.gapAbove() + 20);
            const QColor edgePx = img.pixelColor(pm.paperW / 2, pm.gapAbove());
            check(inGapTop == outside && inGap == outside && onPaper == paper && onPaper2 == paper && edgePx != paper && edgePx != outside,
                  QStringLiteral("sheets are drawn as paper with an edge, the gap between them in the color around the page"),
                  QStringLiteral("gap %1 %2 (want %3) paper %4 %5 (want %6) edge %7")
                      .arg(inGapTop.name(), inGap.name(), outside.name(), onPaper.name(), onPaper2.name(), paper.name(), edgePx.name()));
        }
        shot(&w, QStringLiteral("m6-02-page-view"));

        // ---- page number in the footer
        w.activateWindow();
        ed->setFocus();
        settle(200);
        auto *label = pane0->findChild<QLabel *>(QStringLiteral("pageLabel"));
        c = ed->textCursor();
        c.movePosition(QTextCursor::Start);
        ed->setTextCursor(c);
        settle(100);
        const QString atStart = label->text();
        c.movePosition(QTextCursor::End);
        ed->setTextCursor(c);
        settle(200);
        const QString atEnd = label->text();
        check(label->isVisible() && atStart == QStringLiteral("Page 1 of %1").arg(pages) && atEnd == QStringLiteral("Page %1 of %1").arg(pages)
                  && ed->pageOfCursor() == pages,
              QStringLiteral("the footer shows the page of the cursor and the number of pages"), atStart + QLatin1String(" / ") + atEnd);
        // the view followed the cursor to the last sheet
        const int scrolled = pane0->textView()->verticalScrollBar()->value();
        check(scrolled > (pages - 2) * pm.step(), QStringLiteral("moving the cursor to the end scrolls to the last sheet"),
              QString::number(scrolled));

        // ---- typing past the end of a sheet adds a sheet
        {
            const int before = ed->pageCount();
            QTextCursor t = ed->textCursor();
            t.movePosition(QTextCursor::End);
            t.beginEditBlock();
            for (int i = 0; i < 45; ++i)
                t.insertText(QStringLiteral("\nAnother line for the last page, number %1.").arg(i));
            t.endEditBlock();
            settle(300);
            const int after = ed->pageCount();
            QString w2;
            const bool ok2 = linesOnSheets(ed, &w2);
            check(after > before && ed->height() == after * pm.step() && ok2 && label->text() == QStringLiteral("Page %1 of %1").arg(after),
                  QStringLiteral("text that grows past the end of a sheet continues on a new one"),
                  QStringLiteral("%1 -> %2, height %3, %4 %5").arg(before).arg(after).arg(ed->height()).arg(w2, label->text()));
            ed->document()->undo();
            settle(300);
            check(ed->pageCount() == before && ed->height() == before * pm.step() && ed->isPaged()
                      && pagelayout::matches(ed->document(), pm),
                  QStringLiteral("Undo takes the text back and the sheets stay as they are"),
                  QStringLiteral("%1 pages, height %2").arg(ed->pageCount()).arg(ed->height()));
        }

        // ---- ruler
        {
            byKey(&w, QStringLiteral("showRuler"))->setChecked(true);
            settle(200);
            Ruler *ruler = pane0->ruler();
            const int edLeft = ed->viewport()->mapTo(&w, QPoint(0, 0)).x() - ruler->mapTo(&w, QPoint(0, 0)).x();
            check(ruler->isVisible() && ruler->originX() == edLeft + pm.left && ruler->textWidth() == pm.paperW - pm.left - pm.right,
                  QStringLiteral("the ruler starts at the left page margin and spans the text area"),
                  QStringLiteral("origin %1 (want %2) width %3").arg(ruler->originX()).arg(edLeft + pm.left).arg(ruler->textWidth()));
        }

        // ---- zoom
        {
            docs->setZoom(1.5);
            settle(400);
            const PageMetrics z = docs->pageMetrics();
            QString w2;
            const bool ok2 = linesOnSheets(ed, &w2);
            const int zp = ed->pageCount();
            check(z.paperW == 1191 && ed->width() == z.paperW && ed->height() == zp * z.step() && ok2 && pagelayout::matches(ed->document(), z)
                      && ed->document()->availableUndoSteps() == undoSteps,
                  QStringLiteral("zoom 150 %: sheets, margins and text grow together; still no editing step"),
                  QStringLiteral("w %1 h %2 pages %3 %4 undo %5/%6").arg(ed->width()).arg(ed->height()).arg(zp).arg(w2)
                      .arg(ed->document()->availableUndoSteps()).arg(undoSteps));
            // the sheet is wider than the editor now: it can be scrolled sideways
            QScrollBar *hb = pane0->textView()->horizontalScrollBar();
            check(hb->maximum() > 0, QStringLiteral("a sheet wider than the editor can be scrolled sideways"), QString::number(hb->maximum()));
            shot(&w, QStringLiteral("m6-03-page-view-zoom-150"));
            docs->setZoom(0.75);
            settle(300);
            const PageMetrics s = docs->pageMetrics();
            check(ed->width() == s.paperW && hb->maximum() == 0 && pagelayout::matches(ed->document(), s),
                  QStringLiteral("zoom 75 %: smaller sheets, no sideways scrolling"));
            docs->setZoom(1.0);
            settle(300);
        }

        // ---- Undo still reaches the text typed before the page view was switched on
        {
            ed->document()->undo();
            settle(200);
            const bool gone = !ed->document()->toPlainText().startsWith(QLatin1String("Opening."));
            ed->document()->redo();
            settle(200);
            check(gone && ed->document()->toPlainText().startsWith(QLatin1String("Opening.")) && pagelayout::matches(ed->document(), pm),
                  QStringLiteral("Undo and Redo reach text typed before page view, zoom and back; the pages stay"));
        }

        // ---- paper and margins
        {
            PageSetupDialog dlg(docs->pageSetup(), &w);
            dlg.show();
            auto *paper = dlg.findChild<QComboBox *>(QStringLiteral("pagePaper"));
            auto *width = dlg.findChild<QDoubleSpinBox *>(QStringLiteral("pageWidth"));
            auto *leftM = dlg.findChild<QDoubleSpinBox *>(QStringLiteral("pageLeft"));
            const bool startsA4 = paper && paper->currentData().toString() == QLatin1String("A4") && width
                                  && qAbs(width->value() - 21.0) < 0.01 && width->suffix().contains(QLatin1String("cm"));
            paper->setCurrentIndex(paper->findData(QStringLiteral("A5")));
            emit paper->activated(paper->currentIndex());
            leftM->setValue(1.5);
            dlg.findChild<QDoubleSpinBox *>(QStringLiteral("pageRight"))->setValue(1.5);
            const PageSetup a5 = dlg.setup();
            shot(&dlg, QStringLiteral("m6-04-page-size-dialog"));
            // a width typed by hand makes it a custom paper
            width->setValue(16.0);
            const bool custom = paper->currentData().toString() == QLatin1String("Custom") && qAbs(dlg.setup().width - 160) < 0.01;
            dlg.setSetup(a5);
            dlg.findChild<QRadioButton *>(QStringLiteral("pageLandscape"))->setChecked(true);
            const bool landscape = dlg.setup().landscape && dlg.setup().sizeMm() == QSizeF(210, 148);
            dlg.close();
            check(startsA4 && a5.paper == QLatin1String("A5") && qAbs(a5.width - 148) < 0.01 && qAbs(a5.left - 15) < 0.01 && custom && landscape,
                  QStringLiteral("Page Size and Margins: paper presets, margins in cm, custom size, landscape"),
                  QStringLiteral("%1 %2 %3 %4").arg(startsA4).arg(a5.paper).arg(custom).arg(landscape));

            const int pagesA4 = ed->pageCount();
            docs->setPageSetup(a5);
            settle(400);
            const PageMetrics am = docs->pageMetrics();
            QString w2;
            const bool ok2 = linesOnSheets(ed, &w2);
            check(am.paperW == 559 && am.left == 57 && ed->width() == am.paperW && ed->pageCount() > pagesA4 && ok2
                      && proj->uiState.value(QStringLiteral("pageSetup")).toMap().value(QStringLiteral("paper")).toString() == QLatin1String("A5"),
                  QStringLiteral("A5 with 15 mm side margins: narrower sheets, more pages; stored in the project"),
                  QStringLiteral("w %1 pages %2 -> %3 %4").arg(ed->width()).arg(pagesA4).arg(ed->pageCount()).arg(w2));
            shot(&w, QStringLiteral("m6-05-page-view-a5"));
        }

        // ---- text replaced as a whole (a snapshot rolled back, an import): the sheets are laid out again
        {
            const PageMetrics am = docs->pageMetrics();
            QTextDocument *d = ed->document();
            const QByteArray stored = docs->serialize(longDoc->id, d);
            const int before = ed->pageCount();
            docs->deserialize(longDoc->id, stored, d, proj->storage == StorageFormat::Rtf);
            settle(400);
            QString w2;
            const bool ok2 = linesOnSheets(ed, &w2);
            QTextDocument other;
            other.setPlainText(longText(12, QStringLiteral("Replaced")));
            docs->setContent(midDoc->id, &other);
            check(ed->isPaged() && pagelayout::matches(d, am) && ed->pageCount() == before && ed->height() == before * am.step() && ok2,
                  QStringLiteral("text replaced as a whole (snapshot rolled back) is laid out on the sheets again"),
                  QStringLiteral("paged %1 pages %2/%3 %4").arg(ed->isPaged()).arg(ed->pageCount()).arg(before).arg(w2));
        }

        // ---- Combined text: every document on sheets of its own, the last one cut below the text
        {
            binder->select({chapter}, true);
            settle(200);
            byKey(&w, QStringLiteral("viewText"))->trigger();
            settle(500);
            const PageMetrics am = docs->pageMetrics();
            const QList<TextEditor *> eds = pane0->textView()->editors();
            bool allPaged = eds.size() == 4; // the folder's own text and its three documents
            int total = 0;
            for (TextEditor *e : eds) {
                allPaged = allPaged && e->isPaged() && e->width() == am.paperW;
                total += e->pageCount();
            }
            TextEditor *shortEd = nullptr;
            for (TextEditor *e : eds)
                if (e->item() == shortDoc)
                    shortEd = e;
            const bool trimmed = shortEd && shortEd->pageCount() == 1 && shortEd->height() < am.step() / 2
                                 && shortEd->height() >= am.top + am.bottom + am.gap + 40;
            int page = 0, shown = 0;
            pane0->textView()->pageInfo(&page, &shown);
            // paper and gap colors at the cut end of the short document
            bool cutPainted = false;
            if (shortEd) {
                const QImage img = shortEd->viewport()->grab().toImage();
                cutPainted = img.pixelColor(am.paperW / 2, img.height() - 2) == QColor(theme::color(theme::Role::EditorPageSurround).rgb())
                             && img.pixelColor(20, img.height() - am.gapBelow() - 6) == QColor(theme::color(theme::Role::EditorPaper).rgb());
            }
            check(pane0->mode() == ViewMode::Combined && allPaged && trimmed && shown == total && cutPainted,
                  QStringLiteral("Combined text in page view: each document on its own sheets, short ones end below their text"),
                  QStringLiteral("editors %1 paged %2 trimmed %3 (h %4) pages %5/%6 cut %7").arg(eds.size()).arg(allPaged).arg(trimmed)
                      .arg(shortEd ? shortEd->height() : -1).arg(shown).arg(total).arg(cutPainted));
            pane0->textView()->focusItem(midDoc);
            settle(300);
            shot(&w, QStringLiteral("m6-06-page-view-combined-text"));
            binder->select({longDoc}, true);
            settle(300);
            ed = pane0->currentEditor();
        }

        // ---- the same document in the second editor and in a Quick Reference panel; notes stay as they are
        {
            byKey(&w, QStringLiteral("splitV"))->trigger();
            settle(400);
            TextEditor *e2 = pane1->currentEditor();
            const PageMetrics am = docs->pageMetrics();
            const bool both = e2 && e2 != ed && e2->document() == ed->document() && e2->isPaged() && e2->pageCount() == ed->pageCount()
                              && e2->height() == ed->height() && e2->width() == am.paperW;
            binder->select({notesDoc}, false);
            byKey(&w, QStringLiteral("quickRef"))->trigger();
            settle(400);
            const QList<QuickRefWindow *> qr = quickRefs();
            TextEditor *qe = qr.size() == 1 ? qr.first()->textView()->findChild<TextEditor *>() : nullptr; // (the panel has a notes editor too)
            const bool qrPaged = qe && qe->isPaged() && qe->width() == am.paperW;
            if (!qr.isEmpty())
                shot(qr.first(), QStringLiteral("m6-07-quick-reference-pages"));
            inspector->show();
            inspector->showTab(Inspector::Notes);
            settle(200);
            bool notesPlain = false;
            for (TextEditor *ne : inspector->findChildren<TextEditor *>())
                if (ne->isNotes() && ne->isVisible())
                    notesPlain = !ne->isPaged();
            check(both && qrPaged && notesPlain,
                  QStringLiteral("both editors and Quick Reference panels show pages; document notes do not"),
                  QStringLiteral("%1 %2 %3").arg(both).arg(qrPaged).arg(notesPlain));
            shot(&w, QStringLiteral("m6-08-page-view-two-editors"));
            byKey(&w, QStringLiteral("closeQuickRefs"))->trigger();
            byKey(&w, QStringLiteral("noSplit"))->trigger();
            settle(300);
            binder->select({longDoc}, true);
            settle(300);
            ed = pane0->currentEditor();
        }

        // ---- composition mode has its own paper
        {
            ed->setFocus();
            byKey(&w, QStringLiteral("composition"))->trigger();
            settle(600);
            CompositionWindow *cw = nullptr;
            for (QWidget *tl : QApplication::topLevelWidgets())
                if (auto *x = qobject_cast<CompositionWindow *>(tl))
                    cw = x;
            TextEditor *ce = cw ? cw->view()->activeEditor() : nullptr;
            const bool plain = ce && !ce->isPaged() && !docs->pageView() && docs->pageViewSetting();
            if (cw)
                cw->close();
            settle(500);
            ed = pane0->currentEditor();
            check(plain && docs->pageView() && ed && ed->isPaged() && pagelayout::matches(ed->document(), docs->pageMetrics()),
                  QStringLiteral("composition mode shows its own paper without sheets; the pages are back afterwards"),
                  QStringLiteral("%1 %2").arg(plain).arg(ed && ed->isPaged()));
        }

        // ---- dark theme
        {
            as.theme = QStringLiteral("dark");
            // since 0.21.2 the standard has own colors for the paper and beside the column, which a
            // theme does not change: this check is about the theme's own colors
            as.ownColors.remove(QStringLiteral("editorPaper"));
            as.ownColors.remove(QStringLiteral("editorMargin"));
            as.notifyChanged();
            theme::apply();
            settle(400);
            const PageMetrics am = docs->pageMetrics();
            pane0->textView()->verticalScrollBar()->setValue(0);
            settle(200);
            const QImage img = ed->viewport()->grab(QRect(0, 0, am.paperW, am.step() + 40)).toImage();
            check(img.pixelColor(am.paperW / 2, am.step()) == QColor(theme::color(theme::Role::EditorPageSurround).rgb())
                      && img.pixelColor(20, am.gapAbove() + 20) == QColor(theme::color(theme::Role::EditorPaper).rgb())
                      && theme::color(theme::Role::EditorPaper).lightness() < 100,
                  QStringLiteral("sheets and surround follow a change of theme"));
            shot(&w, QStringLiteral("m6-09-page-view-dark"));
            as.theme = QStringLiteral("light");
            as.notifyChanged();
            theme::apply();
            settle(300);
        }

        // ---- saved with the project; the text files know nothing of pages
        {
            proj->save();
            QFile f(docs->textPath(longDoc->id));
            f.open(QIODevice::ReadOnly);
            const QString stored = QString::fromUtf8(f.readAll());
            check(proj->uiState.value(QStringLiteral("pageView")).toBool() && stored.startsWith(QLatin1String("Opening. Ships paragraph 1."))
                      && !stored.contains(QLatin1String("margin=")) && !stored.contains(QLatin1String("{:")),
                  QStringLiteral("page view is remembered in the project; the stored text is unchanged by it"), stored.left(80));
        }

        // ---- off
        ed->setFocus();
        QTest::keyClicks(ed, QStringLiteral("Last words. "));
        settle(100);
        const int undoBeforeOff = ed->document()->availableUndoSteps();
        pageView->trigger();
        settle(400);
        ed = pane0->currentEditor();
        check(!pageView->isChecked() && !ed->isPaged() && ed->width() == flowWidth && ed->height() >= flowHeight - 40
                  && qRound(ed->document()->rootFrame()->frameFormat().leftMargin()) == 6 && ed->document()->pageSize().height() <= 0
                  && !pane0->findChild<QLabel *>(QStringLiteral("pageLabel"))->isVisible()
                  && pane0->textView()->horizontalScrollBar()->maximum() == 0 && undoBeforeOff >= 1 && ed->document()->availableUndoSteps() == undoBeforeOff,
              QStringLiteral("page view off: one column of the former width, no page number, undo history intact"),
              QStringLiteral("paged %1 w %2/%3 h %4/%5 margin %6 undo %7/%8").arg(ed->isPaged()).arg(ed->width()).arg(flowWidth)
                  .arg(ed->height()).arg(flowHeight).arg(ed->document()->rootFrame()->frameFormat().leftMargin())
                  .arg(ed->document()->availableUndoSteps()).arg(undoBeforeOff));
        pageView->trigger(); // on again for the copyholder checks and the reopening below
        settle(300);
    }

    // ================================================================ COPYHOLDERS
    {
        binder->select({longDoc}, true);
        settle(300);
        QAction *openHolder = byKey(&w, QStringLiteral("openCopyholder"));
        QAction *closeHolder = byKey(&w, QStringLiteral("closeCopyholder"));
        check(menuPath(&w, {QStringLiteral("Navigate"), QStringLiteral("Open"), QStringLiteral("in Copyholder")})
                  && menuPath(&w, {QStringLiteral("Navigate"), QStringLiteral("Editor"), QStringLiteral("Close Copyholder")}) == closeHolder
                  && openHolder && closeHolder && !closeHolder->isEnabled() && !pane0->hasCopyholder(),
              QStringLiteral("Navigate > Open > in Copyholder and Navigate > Editor > Close Copyholder exist; none is open at first"));

        QWidget *header0 = pane0->findChild<QWidget *>(QStringLiteral("editorHeader"));
        {
            // the whole bar takes the drop: no part of it catches a dragged document for itself (the title field would take its name as text)
            QStringList catching;
            for (QWidget *c : header0->findChildren<QWidget *>())
                if (c->acceptDrops())
                    catching << QString::fromLatin1(c->metaObject()->className());
            check(header0->acceptDrops() && catching.isEmpty(),
                  QStringLiteral("a document dropped anywhere on the header bar reaches the bar"), catching.join(QLatin1Char(' ')));
        }
        // ---- the gesture itself: drag a document from the binder to the title in the header bar, with and without Alt
        if (QGuiApplication::platformName() == QLatin1String("xcb")) {
            QTreeView *tree = binder->tree();
            tree->expandAll();
            settle(200);
            QLineEdit *titleField = header0->findChild<QLineEdit *>();
            const QPoint onTitle(titleField->width() / 2, titleField->height() / 2);
            auto itemPos = [&](BinderItem *it) { return tree->visualRect(proj->model()->indexFor(it)).center(); };
            mouseDrag(&w, tree->viewport(), itemPos(midDoc), titleField, onTitle, Qt::NoModifier);
            const bool loadedByDrag = pane0->items() == QList<BinderItem *>({midDoc}) && !pane0->hasCopyholder()
                                      && midDoc->title == QLatin1String("Morning Fog") && titleField->text() == QLatin1String("Morning Fog");
            mouseDrag(&w, tree->viewport(), itemPos(notesDoc), titleField, onTitle, Qt::AltModifier);
            const bool heldByDrag = pane0->items() == QList<BinderItem *>({midDoc}) && pane0->copyholderItem() == notesDoc;
            check(loadedByDrag && heldByDrag,
                  QStringLiteral("dragging with the mouse from the binder to the header bar loads the document; with Alt it opens the copyholder"),
                  QStringLiteral("loaded %1 (editor shows %2, title field '%3') held %4").arg(loadedByDrag)
                      .arg(pane0->items().isEmpty() ? QStringLiteral("-") : pane0->items().first()->title, titleField->text()).arg(heldByDrag));
            pane0->closeCopyholder();
            pane0->showItems({longDoc});
            settle(200);
            // moved inside the binder (onto another folder): the document moves and is shown, as after a click
            BinderItem *otherFolder = nullptr;
            for (BinderItem *c : proj->draft()->children)
                if (c != chapter && c->isContainer())
                    otherFolder = c;
            if (otherFolder) {
                BinderItem *mover = m->createItem(chapter, 3, ItemType::Text, QStringLiteral("Wanderer"));
                templates::setMarkdown(proj, mover, QStringLiteral("On the move.\n"));
                m->refreshAll();
                tree->expandAll();
                settle(300);
                mouseDrag(&w, tree->viewport(), itemPos(mover), tree->viewport(), itemPos(otherFolder), Qt::NoModifier);
                check(mover->parent == otherFolder && pane0->items() == QList<BinderItem *>({mover}),
                      QStringLiteral("a document dragged to another folder in the binder moves there and is shown in the editor"),
                      QStringLiteral("parent %1, editor shows %2").arg(mover->parent ? mover->parent->title : QStringLiteral("-"))
                          .arg(pane0->items().isEmpty() ? QStringLiteral("-") : pane0->items().first()->title));
                m->moveToTrash({mover});
                pane0->showItems({longDoc});
                settle(200);
            }
        } else {
            std::printf("SKIP  mouse drag from the binder to the header bar (the offscreen display has no drag and drop; run on X11)\n");
        }

        // ---- a click in the binder loads its document on release (so that a drag does not open it on the way)
        {
            QTreeView *tree = binder->tree();
            tree->expandAll();
            settle(200);
            const QPoint at = tree->viewport()->mapTo(&w, tree->visualRect(proj->model()->indexFor(shortDoc)).center());
            QTest::mouseEvent(QTest::MousePress, w.windowHandle(), Qt::LeftButton, Qt::NoModifier, at);
            settle(200);
            const bool waits = pane0->items() == QList<BinderItem *>({longDoc}) && binder->selectedItems() == QList<BinderItem *>({shortDoc});
            QTest::mouseEvent(QTest::MouseRelease, w.windowHandle(), Qt::LeftButton, Qt::NoModifier, at);
            settle(300);
            check(waits && pane0->items() == QList<BinderItem *>({shortDoc}),
                  QStringLiteral("a click in the binder selects on press and loads the document when the button is released"),
                  QStringLiteral("%1, shows %2").arg(waits).arg(pane0->items().isEmpty() ? QStringLiteral("-") : pane0->items().first()->title));
            pane0->showItems({longDoc});
            settle(200);
        }

        // ---- a plain drop on the header bar loads the document, Alt+drop opens it in the copyholder
        dropItems(header0, proj, {midDoc}, Qt::NoModifier);
        const bool loaded = pane0->items() == QList<BinderItem *>({midDoc}) && !pane0->hasCopyholder();
        dropItems(header0, proj, {longDoc}, Qt::NoModifier);
        dropItems(header0, proj, {notesDoc}, Qt::AltModifier);
        Copyholder *holder = pane0->copyholder();
        TextEditor *he = holder->editor();
        check(loaded && pane0->items() == QList<BinderItem *>({longDoc}) && pane0->hasCopyholder() && pane0->copyholderItem() == notesDoc
                  && holder->isVisible() && he && he->document() == docs->text(notesDoc->id) && closeHolder->isEnabled(),
              QStringLiteral("Alt+drag onto the header bar opens the document in the copyholder; the editor keeps its own"),
              QStringLiteral("%1 %2 %3").arg(loaded).arg(pane0->hasCopyholder()).arg(he != nullptr));

        // ---- look: narrow header bar with title, Quick Reference button and X; no footer bar
        {
            const QList<QLabel *> title = holder->header()->findChildren<QLabel *>();
            bool titled = false;
            for (QLabel *l : title)
                titled = titled || l->text() == QLatin1String("Tide Tables");
            auto *x = holder->findChild<QToolButton *>(QStringLiteral("copyholderClose"));
            auto *q = holder->findChild<QToolButton *>(QStringLiteral("copyholderQuickRef"));
            const QList<QToolButton *> buttons = holder->header()->findChildren<QToolButton *>();
            const bool order = x && q && q->x() < x->x() && x->geometry().right() > holder->header()->width() - 12;
            check(titled && order && buttons.size() == 2 && holder->header()->height() < header0->height()
                      && holder->findChildren<QComboBox *>().isEmpty(),
                  QStringLiteral("the copyholder has a narrow header bar (title, Quick Reference button, X) and no footer bar"),
                  QStringLiteral("%1 %2 h %3/%4").arg(titled).arg(order).arg(holder->header()->height()).arg(header0->height()));
        }
        // first time in a wide editor: beside it, on the right
        auto *holderSplit = pane0->findChild<QSplitter *>(QStringLiteral("copyholderSplit"));
        check(pane0->copyholderPlacement() == QLatin1String("right") && holderSplit->orientation() == Qt::Horizontal
                  && holderSplit->indexOf(holder) == 1 && holder->width() > 200 && holder->x() > pane0->textView()->x(),
              QStringLiteral("in a wide editor the copyholder opens on the right"), pane0->copyholderPlacement());
        shot(&w, QStringLiteral("m6-10-copyholder-right"));

        // ---- it is an editor: typing and formatting commands reach its text
        {
            he->setFocus();
            settle(100);
            QTextCursor c = he->textCursor();
            c.movePosition(QTextCursor::End);
            he->setTextCursor(c);
            QTest::keyClicks(he, QStringLiteral(" Spring tide on the 14th."));
            c = he->textCursor();
            c.movePosition(QTextCursor::StartOfWord, QTextCursor::MoveAnchor);
            c.movePosition(QTextCursor::PreviousWord, QTextCursor::MoveAnchor, 2);
            c.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
            he->setTextCursor(c);
            const QString word = c.selectedText();
            byKey(&w, QStringLiteral("bold"))->trigger();
            settle(100);
            const bool bold = he->textCursor().charFormat().fontWeight() >= QFont::Bold;
            const bool mainUntouched = !pane0->currentEditor()->textCursor().charFormat().font().bold();
            check(docs->text(notesDoc->id)->toPlainText().endsWith(QLatin1String("Spring tide on the 14th.")) && bold && mainUntouched
                      && pane0->focusedCopyholderEditor() == he && docs->hasDirty(),
                  QStringLiteral("text can be written and formatted in the copyholder (Bold acts on its selection)"),
                  QStringLiteral("word '%1' bold %2").arg(word).arg(bold));
        }

        // ---- placement: right-click on its header bar
        {
            bool menuSeen = false;
            int entries = 0;
            QTimer::singleShot(300, &w, [&] {
                if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                    menuSeen = menu->objectName() == QLatin1String("copyholderMenu");
                    for (QAction *a : menu->actions()) {
                        entries += a->isCheckable();
                        if (menuText(a->text()) == QLatin1String("Copyholder at the Bottom"))
                            a->trigger();
                    }
                    menu->close();
                }
            });
            QContextMenuEvent cme(QContextMenuEvent::Mouse, QPoint(20, 8), holder->header()->mapToGlobal(QPoint(20, 8)));
            QApplication::sendEvent(holder->header(), &cme);
            settle(300);
            const bool bottom = pane0->copyholderPlacement() == QLatin1String("bottom") && holderSplit->orientation() == Qt::Vertical
                                && holderSplit->indexOf(holder) == 1 && holder->y() > pane0->textView()->y();
            shot(&w, QStringLiteral("m6-11-copyholder-bottom"));
            pane0->setCopyholderPlacement(QStringLiteral("left"));
            settle(200);
            const bool left = holderSplit->orientation() == Qt::Horizontal && holderSplit->indexOf(holder) == 0;
            pane0->setCopyholderPlacement(QStringLiteral("top"));
            settle(200);
            const bool top = holderSplit->orientation() == Qt::Vertical && holderSplit->indexOf(holder) == 0
                             && pane0->copyholderItem() == notesDoc && holder->editor() != nullptr;
            pane0->setCopyholderPlacement(QStringLiteral("bottom"));
            settle(200);
            check(menuSeen && entries == 4 && bottom && left && top
                      && proj->uiState.value(QStringLiteral("copyholderPlacement")).toString() == QLatin1String("bottom"),
                  QStringLiteral("right-click on the copyholder header bar places it left, right, above or below the editor"),
                  QStringLiteral("menu %1 entries %2 bottom %3 left %4 top %5").arg(menuSeen).arg(entries).arg(bottom).arg(left).arg(top));
        }

        // ---- the copyholder shows pages in page view as well
        {
            TextEditor *e = holder->editor();
            check(e && e->isPaged() && e->width() == docs->pageMetrics().paperW,
                  QStringLiteral("in page view the copyholder shows its document on sheets, too"));
        }

        // ---- another document: drop on its header bar, or Navigate > Open > in Copyholder
        {
            dropItems(holder->header(), proj, {shortDoc}, Qt::NoModifier);
            const bool dropped = pane0->copyholderItem() == shortDoc && pane0->items() == QList<BinderItem *>({longDoc});
            binder->select({midDoc}, false);
            openHolder->trigger();
            settle(300);
            check(dropped && pane0->copyholderItem() == midDoc && pane0->items() == QList<BinderItem *>({longDoc})
                      && pane0->copyholderPlacement() == QLatin1String("bottom"),
                  QStringLiteral("a drop on the copyholder header bar and Navigate > Open > in Copyholder change its document"));
        }

        // ---- pictures
        {
            QImage img(320, 200, QImage::Format_RGB32);
            img.fill(QColor(40, 90, 160));
            const QString file = home.path() + QLatin1String("/lighthouse.png");
            img.save(file);
            BinderItem *pic = m->createItem(proj->research(), 1, ItemType::Image, QStringLiteral("Lighthouse"));
            pic->fileExt = QStringLiteral("png");
            QDir().mkpath(proj->contentDir(pic->id));
            const QString target = proj->mediaFilePath(pic);
            const bool copied = QFile::copy(file, target);
            pane0->openCopyholder(pic);
            settle(300);
            check(pane0->copyholderItem() == pic && holder->editor() == nullptr && holder->findChild<MediaView *>()->isVisible(),
                  QStringLiteral("a picture can be held in the copyholder"), QStringLiteral("copied %1 to %2").arg(copied).arg(target));
            pane0->openCopyholder(notesDoc);
            settle(200);
        }

        // ---- Selection Affects: other editor, then copyholder
        {
            byKey(&w, QStringLiteral("splitV"))->trigger();
            settle(300);
            pane1->showItems({chapter});
            pane1->setViewMode(ViewMode::Outliner);
            settle(300);
            pane0->showItems({longDoc});
            QToolButton *button = nullptr;
            for (QToolButton *b : pane1->findChildren<QToolButton *>())
                if (b->isVisible() && b->property("affects").isValid())
                    button = b;
            const bool offFirst = button && !button->isChecked() && pane1->selectionAffects() == EditorPane::AffectsNothing;
            // without a copyholder: off -> other editor -> off
            QTest::mouseClick(button, Qt::LeftButton);
            const bool other = pane1->selectionAffects() == EditorPane::AffectsOtherEditor && button->isChecked();
            QTest::mouseClick(button, Qt::LeftButton);
            const bool offAgain = pane1->selectionAffects() == EditorPane::AffectsNothing && !button->isChecked();
            // with a copyholder in that editor: Alt+drop from its outliner onto its own header bar
            dropItems(pane1->findChild<QWidget *>(QStringLiteral("editorHeader")), proj, {shortDoc}, Qt::AltModifier);
            const bool opened = pane1->hasCopyholder() && pane1->copyholderItem() == shortDoc && pane1->mode() == ViewMode::Outliner;
            QTest::mouseClick(button, Qt::LeftButton);
            const QIcon iconOther = button->icon();
            pane1->outliner()->selectItems({midDoc});
            emit pane1->outliner()->selectionChangedByUser();
            settle(300);
            const bool toOther = pane0->items() == QList<BinderItem *>({midDoc}) && pane1->copyholderItem() == shortDoc;
            QTest::mouseClick(button, Qt::LeftButton);
            const bool holderMode = pane1->selectionAffects() == EditorPane::AffectsCopyholder && button->isChecked()
                                    && button->icon().cacheKey() != iconOther.cacheKey();
            pane0->showItems({longDoc});
            pane1->outliner()->selectItems({notesDoc});
            pane1->outliner()->selectItems({longDoc});
            emit pane1->outliner()->selectionChangedByUser();
            settle(300);
            const bool toHolder = pane1->copyholderItem() == longDoc && pane0->items() == QList<BinderItem *>({longDoc})
                                  && pane1->mode() == ViewMode::Outliner;
            shot(&w, QStringLiteral("m6-12-outliner-feeds-copyholder"));
            // four documents at once: two editors, two copyholders
            int visibleTexts = 0;
            for (EditorPane *ep : {pane0, pane1})
                visibleTexts += (ep->isVisible() ? 1 : 0) + (ep->hasCopyholder() ? 1 : 0);
            QTest::mouseClick(button, Qt::LeftButton);
            const bool offLast = pane1->selectionAffects() == EditorPane::AffectsNothing;
            check(offFirst && other && offAgain && opened && toOther && holderMode && toHolder && visibleTexts == 4 && offLast,
                  QStringLiteral("Selection Affects button: other editor, then copyholder (arrow into the box), then off; "
                                 "outliner clicks open there"),
                  QStringLiteral("off %1 other %2 off %3 opened %4 toOther %5 holderMode %6 toHolder %7 views %8 off %9")
                      .arg(offFirst).arg(other).arg(offAgain).arg(opened).arg(toOther).arg(holderMode).arg(toHolder)
                      .arg(visibleTexts).arg(offLast));

            // the same with a corkboard
            pane1->setViewMode(ViewMode::Corkboard);
            settle(300);
            pane1->setSelectionAffects(EditorPane::AffectsCopyholder);
            pane1->corkboard()->selectItems({shortDoc});
            emit pane1->corkboard()->selectionChanged(); // (selectItems is silent; a click is not)
            settle(300);
            check(pane1->copyholderItem() == shortDoc && pane1->selectionAffects() == EditorPane::AffectsCopyholder,
                  QStringLiteral("corkboard selection opens in the copyholder, too"),
                  pane1->copyholderItem() ? pane1->copyholderItem()->title : QStringLiteral("none"));
        }

        // ---- the tutorial's gesture: Alt+drag a card onto the header bar above its own corkboard
        if (QGuiApplication::platformName() == QLatin1String("xcb")) {
            pane1->closeCopyholder();
            pane1->setSelectionAffects(EditorPane::AffectsNothing);
            pane0->showItems({longDoc});
            settle(300);
            Corkboard *cork = pane1->corkboard();
            QList<QGraphicsItem *> cards;
            for (QGraphicsItem *gi : cork->scene()->items())
                if (!gi->parentItem() && gi->isVisible() && gi->sceneBoundingRect().width() > 100 && (gi->flags() & QGraphicsItem::ItemIsSelectable))
                    cards << gi;
            std::sort(cards.begin(), cards.end(), [](QGraphicsItem *a, QGraphicsItem *b) {
                const QPointF pa = a->sceneBoundingRect().topLeft(), pb = b->sceneBoundingRect().topLeft();
                return pa.y() < pb.y() - 5 || (qAbs(pa.y() - pb.y()) <= 5 && pa.x() < pb.x());
            });
            bool held = false;
            if (cards.size() == 3) {
                const QRectF r = cards.at(1)->sceneBoundingRect(); // the second card: The Logbook
                const QPoint from = cork->mapFromScene(QPointF(r.center().x(), r.top() + 14));
                QWidget *header1 = pane1->findChild<QWidget *>(QStringLiteral("editorHeader"));
                mouseDrag(&w, cork->viewport(), from, header1, QPoint(header1->width() / 2, header1->height() / 2), Qt::AltModifier);
                held = pane1->copyholderItem() == shortDoc && pane1->mode() == ViewMode::Corkboard
                       && pane1->items() == QList<BinderItem *>({chapter}) && pane0->items() == QList<BinderItem *>({longDoc});
            }
            check(held, QStringLiteral("Alt+dragging a card with the mouse onto the header bar above the corkboard opens it in that editor's copyholder"),
                  QStringLiteral("cards %1, copyholder: %2").arg(cards.size()).arg(pane1->copyholderItem() ? pane1->copyholderItem()->title : QStringLiteral("none")));
        }

        // ---- closing: X; the button beside it moves the document to a Quick Reference panel
        {
            pane1->copyholder()->findChild<QToolButton *>(QStringLiteral("copyholderClose"))->click();
            settle(200);
            const bool closed = !pane1->hasCopyholder() && !pane1->copyholder()->isVisible()
                                && pane1->selectionAffects() == EditorPane::AffectsNothing;
            w.findChild<QAction *>(QStringLiteral("noSplit"))->trigger();
            settle(300);
            pane0->openCopyholder(notesDoc);
            settle(200);
            pane0->copyholder()->findChild<QToolButton *>(QStringLiteral("copyholderQuickRef"))->click();
            settle(400);
            const QList<QuickRefWindow *> qr = quickRefs();
            check(closed && !pane0->hasCopyholder() && qr.size() == 1 && qr.first()->item() == notesDoc && !closeHolder->isEnabled(),
                  QStringLiteral("X closes the copyholder; the button beside it reopens its document as a Quick Reference panel"),
                  QStringLiteral("closed %1 open %2 qr %3").arg(closed).arg(pane0->hasCopyholder()).arg(qr.size()));
            byKey(&w, QStringLiteral("closeQuickRefs"))->trigger();
            settle(200);
        }

        // ---- a document that leaves the project leaves the copyholder
        {
            BinderItem *scrap = m->createItem(proj->research(), 0, ItemType::Text, QStringLiteral("Scrap"));
            templates::setMarkdown(proj, scrap, QStringLiteral("To be thrown away.\n"));
            pane0->openCopyholder(scrap);
            settle(200);
            const bool held = pane0->copyholderItem() == scrap;
            m->moveToTrash({scrap});
            settle(200);
            const bool keptInTrash = pane0->copyholderItem() == scrap;
            m->emptyTrash();
            pane0->refresh();
            settle(200);
            check(held && keptInTrash && !pane0->hasCopyholder(),
                  QStringLiteral("a document deleted for good is taken off the copyholder"),
                  QStringLiteral("%1 %2 %3").arg(held).arg(keptInTrash).arg(pane0->hasCopyholder()));
        }

        // ---- kept with the project
        pane0->showItems({longDoc});
        pane0->openCopyholder(notesDoc);
        pane0->setCopyholderPlacement(QStringLiteral("left"));
        settle(300);
        shot(&w, QStringLiteral("m6-13-copyholder-left-page-view"));
    }

    // ================================================================ REOPEN
    {
        proj->docs()->setZoom(1.1);
        settle(300);
        const bool closedOk = w.closeProject();
        settle(300);
        check(closedOk && !pane0->hasCopyholder(), QStringLiteral("closing the project empties the copyholder"));
        const bool reopened = w.openProject(projectFile);
        settle(700);
        proj = w.project();
        TextEditor *ed = pane0->currentEditor();
        Copyholder *holder = pane0->copyholder();
        auto *holderSplit = pane0->findChild<QSplitter *>(QStringLiteral("copyholderSplit"));
        const bool pages = proj && proj->docs()->pageView() && pageView->isChecked() && ed && ed->isPaged()
                           && proj->docs()->pageSetup().paper == QLatin1String("A5") && qAbs(proj->docs()->zoom() - 1.1) < 0.001
                           && pagelayout::matches(ed->document(), PageMetrics::from(proj->docs()->pageSetup(), 1.1));
        const bool held = pane0->hasCopyholder() && pane0->copyholderItem() && pane0->copyholderItem()->title == QLatin1String("Tide Tables")
                          && pane0->copyholderPlacement() == QLatin1String("left") && holderSplit->indexOf(holder) == 0
                          && holder->editor()
                          && holder->editor()->document()->toPlainText().endsWith(QLatin1String("Spring tide on the 14th."));
        check(reopened && pages && held, QStringLiteral("after reopening: page view with its paper and zoom, the copyholder with its document and place"),
              QStringLiteral("reopened %1 pages %2 (%4 %5 %6 %7) held %3").arg(reopened).arg(pages).arg(held).arg(proj && proj->docs()->pageView()).arg(pageView->isChecked()).arg(ed && ed->isPaged()).arg(proj ? proj->docs()->pageSetup().paper : QString()));
        shot(&w, QStringLiteral("m6-14-reopened"));

        // "use this page for new projects"
        PageSetup letter = PageSetup::forLocale();
        letter.paper = QStringLiteral("US Letter");
        letter.width = 215.9;
        letter.height = 279.4;
        PageSetup::setStandard(letter);
        QString err2;
        auto fresh = templates::createProject(novel, projectsPath, QStringLiteral("Second Book"), &err2);
        check(fresh && fresh->docs()->pageSetup().paper == QLatin1String("US Letter") && !fresh->docs()->pageView(),
              QStringLiteral("a new project starts with the standard page and without page view"), err2);
    }

    w.closeProject();
    settle(200);
    delete wp;
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
