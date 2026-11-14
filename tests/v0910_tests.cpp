// Checks of 0.91.0 beta (the writer's requests of 2026-11-13):
//  - backups straight in the backup folder: what waited for a project's own folder of 0.70.0 to 0.90.4 is put into the
//    backup folder, titles with special characters (the rest is in v0700, v0810 and v0903);
//  - seven toolbars and the icon bar: their standard places and contents, the grip, a place along the row changed by
//    dragging, the bottom edge, what the program keeps, Reset Toolbar Places, a switch per bar and per row, the
//    right-click menu, Customize Toolbars with all bars. (0.91.1: the drag is the program's own; order, rows and the
//    row that got stuck are in v0911.)
// The theme buttons Bright, Dark, U1 and U2 are checked in v0902 (where the theme list was).
//
//   xvfb-run -a ./qb_v0910_tests /tmp/shots

#include "compile/PluginManager.h"
#include "core/Backup.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "core/Zip.h"
#include "ui/AppSettings.h"
#include "ui/BarDock.h"
#include "ui/BarPlaces.h"
#include "ui/GripBar.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "ui/ToolbarConfig.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QSettings>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QToolBar>
#include <QToolButton>
#include <cstdio>

using namespace qb;

static int failures = 0;
static QString shotDir;

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

static void settle(int ms = 60)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(5);
    }
}

static void shot(QWidget *w, const QString &name)
{
    if (shotDir.isEmpty() || !w)
        return;
    settle(80);
    w->grab().save(shotDir + QLatin1Char('/') + name + QStringLiteral(".png"));
}

static void writeAll(const QString &f, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(f).absolutePath());
    QFile file(f);
    if (file.open(QIODevice::WriteOnly))
        file.write(data);
}

static BinderItem *textDoc(Project *p, BinderItem *parent, const QString &title, const QString &text)
{
    BinderItem *it = p->model()->createItem(parent, -1, ItemType::Text, title);
    QTextCursor c(p->docs()->text(it->id));
    c.insertText(text);
    return it;
}

static void waitNextSecond()
{
    const qint64 s = QDateTime::currentSecsSinceEpoch();
    while (QDateTime::currentSecsSinceEpoch() == s)
        QTest::qWait(20);
}

// ------------------------------------------------------------------ backups

static void backupChecks(const QString &H)
{
    std::printf("== Backups straight in the backup folder (0.91.0)\n");
    QString err;
    // a backup that waited for the project's own folder (0.90.3 and 0.90.4 kept it in that folder's work folder when
    // the backup folder could not be reached) goes into the backup folder with the next backup
    {
        QDir().mkpath(H + QStringLiteral("/p1"));
        auto p = Project::create(H + QStringLiteral("/p1"), QStringLiteral("Waited"), &err);
        textDoc(p.get(), p->draft(), QStringLiteral("Scene"), QStringLiteral("The text of the scene."));
        p->save(&err);
        const QString dir = H + QStringLiteral("/bk-former");
        const QString wouldBe = dir + QStringLiteral("/Waited ") + backup::projectKey(p.get());
        const QString formerWork = backup::workFolderFor(wouldBe);
        QString made, why;
        const bool first = backup::backupProject(p.get(), H + QStringLiteral("/elsewhere"), 0, &made, &why, backup::Where::AsChosen);
        const QString waiting = formerWork + QStringLiteral("/Waited bak 2026-11-12 08-00-00.zip");
        QDir().mkpath(formerWork);
        const bool copied = QFile::copy(made, waiting);
        writeAll(formerWork + QStringLiteral("/target.txt"), wouldBe.toUtf8() + '\n');
        waitNextSecond();
        QString out;
        bool waits = false;
        const bool ok = backup::backupProject(p.get(), dir, 5, &out, &why, backup::Where::BackupFolder, &waits);
        const QString arrived = dir + QStringLiteral("/Waited bak 2026-11-12 08-00-00.zip");
        check(first && copied && ok && !waits && QFile::exists(arrived) && ZipWriter::verify(arrived) > 0 && !QFile::exists(waiting)
                  && !QDir(formerWork).exists() && !QDir(wouldBe).exists() && backup::backupsFor(p.get(), dir).size() == 2
                  && QFileInfo(out).absolutePath() == QFileInfo(dir).absoluteFilePath(),
              QStringLiteral("a backup that waited for the project's own folder of 0.90 is put into the backup folder by the next "
                             "backup; its work folder goes; no folder per project is made"),
              why + QLatin1Char(' ') + backup::backupsFor(p.get(), dir).join(QLatin1String(" | ")));
    }
    // titles with characters that mean something in file patterns: counted and cleaned up exactly
    {
        QDir().mkpath(H + QStringLiteral("/p2"));
        QDir().mkpath(H + QStringLiteral("/p3"));
        auto a = Project::create(H + QStringLiteral("/p2"), QStringLiteral("What [now]? *"), &err);
        auto b = Project::create(H + QStringLiteral("/p3"), QStringLiteral("What n"), &err);
        textDoc(a.get(), a->draft(), QStringLiteral("A"), QStringLiteral("Text of A."));
        textDoc(b.get(), b->draft(), QStringLiteral("B"), QStringLiteral("Text of B."));
        a->save(&err);
        b->save(&err);
        const QString dir = H + QStringLiteral("/bk-titles");
        QString out, why;
        bool ok = backup::backupProject(b.get(), dir, 2, &out, &why);
        const QString ofB = out;
        for (int i = 0; i < 3; ++i) {
            waitNextSecond();
            ok = backup::backupProject(a.get(), dir, 2, &out, &why) && ok;
        }
        const QStringList listA = backup::backupsFor(a.get(), dir), listB = backup::backupsFor(b.get(), dir);
        check(ok && listA.size() == 2 && listA.last() == out && listB == QStringList{ofB} && QFile::exists(ofB),
              QStringLiteral("a title with [ ] ? and *: its backups are counted and cleaned up; another project's stay"),
              listA.join(QLatin1String(" | ")) + QLatin1String(" // ") + listB.join(QLatin1String(" | ")));
        // files that only look like this project's backups are left alone
        const QString base = QFileInfo(out).completeBaseName();
        const QString copyName = dir + QLatin1Char('/') + base + QStringLiteral("-pc-safeBackup-0001.zip");
        QFile::copy(out, copyName);
        for (int i = 0; i < 3; ++i) {
            waitNextSecond();
            ok = backup::backupProject(a.get(), dir, 2, &out, &why) && ok;
        }
        check(ok && QFile::exists(copyName) && backup::backupsFor(a.get(), dir).contains(copyName),
              QStringLiteral("a sync program's copy of a backup is never deleted, and Restore From Backup still offers it"));
    }
}

// ------------------------------------------------------------------ toolbars

static QToolBar *barOf(MainWindow &w, toolbars::Bar b) { return w.findChild<QToolBar *>(toolbars::barObjectName(b)); }
static QPoint placeOf(MainWindow &w, QWidget *bar) { return bar->mapTo(&w, QPoint(0, 0)); }

static void toolbarChecks(MainWindow &w)
{
    std::printf("== Seven toolbars and the icon bar (0.91.0)\n");
    AppSettings &as = AppSettings::instance();
    const QList<toolbars::Bar> all = toolbars::allBars();
    QList<QToolBar *> bars;
    for (toolbars::Bar b : all)
        bars << barOf(w, b);
    auto *iconBar = w.findChild<QToolBar *>(QStringLiteral("iconBar"));
    // (the icon bar is switched off in a fresh program)
    if (QAction *icons = w.findChild<QAction *>(QStringLiteral("showIconBar")); icons && !icons->isChecked()) {
        icons->trigger();
        settle(300);
    }
    bool there = all.size() == 7 && iconBar != nullptr, grips = true;
    for (QToolBar *bar : std::as_const(bars)) {
        there = there && bar != nullptr;
        if (!bar)
            continue;
        grips = grips && bar->isMovable() && !bar->isFloatable() && bar->allowedAreas() == (Qt::TopToolBarArea | Qt::BottomToolBarArea)
                && w.toolBarArea(bar) == Qt::TopToolBarArea;
    }
    check(there, QStringLiteral("the seven toolbars and the icon bar are there"),
          QStringLiteral("mainToolbar themesBar searchBar toolsBar formatBar characterBar paragraphBar iconBar"));
    if (!there)
        return;
    check(grips && iconBar->isMovable() && !iconBar->isFloatable(),
          QStringLiteral("each has a grip to move it by; it can stand at the top or the bottom edge, not as a window of its own"));
    QToolBar *mainBar = bars[0], *themesBar = bars[1], *searchBar = bars[2], *toolsBar = bars[3], *styleBar = bars[4],
             *characterBar = bars[5], *paragraphBar = bars[6];
    // the standard places: two rows, in this order
    auto inRow = [&w](const QList<QToolBar *> &row) {
        bool ok = true;
        for (int i = 0; i < row.size(); ++i) {
            ok = ok && row[i]->isVisible() && placeOf(w, row[i]).y() == placeOf(w, row[0]).y();
            if (i > 0)
                ok = ok && placeOf(w, row[i]).x() >= placeOf(w, row[i - 1]).x() + row[i - 1]->width() - 1;
        }
        return ok;
    };
    auto places = [&] {
        QString s;
        for (QToolBar *bar : bars + QList<QToolBar *>{iconBar})
            s += QStringLiteral("%1 %2,%3 w%4%5; ").arg(bar->objectName()).arg(placeOf(w, bar).x()).arg(placeOf(w, bar).y()).arg(bar->width())
                     .arg(bar->isVisible() ? QString() : QStringLiteral(" hidden"));
        return s;
    };
    const bool standardPlaces = inRow({mainBar, themesBar, searchBar, toolsBar}) && inRow({styleBar, characterBar, paragraphBar, iconBar})
                                && placeOf(w, styleBar).y() > placeOf(w, mainBar).y();
    check(standardPlaces, QStringLiteral("standard places: Main, Themes, Search, Tools in the first row; Style and Font, Character and "
                                         "Alignment, Paragraph and Insert, Icon Bar in the second"),
          places());
    info(places());
    shot(&w, QStringLiteral("toolbars-standard"));
    // what stands where
    auto has = [&w](QToolBar *bar, const char *command) {
        QAction *ac = w.findChild<QAction *>(QLatin1String(command));
        return ac && bar->widgetForAction(ac) != nullptr;
    };
    QWidget *quick = w.findChild<QWidget *>(QStringLiteral("quickSearch"));
    QWidget *picker = w.findChild<QWidget *>(QStringLiteral("themePicker"));
    QWidget *styleCombo = w.findChild<QWidget *>(QStringLiteral("styleCombo"));
    check(has(mainBar, "newFolder") && has(mainBar, "undo") && has(mainBar, "redo") && picker && themesBar->isAncestorOf(picker) && quick
              && searchBar->isAncestorOf(quick) && has(searchBar, "viewCork") && has(toolsBar, "compile") && has(toolsBar, "showInvisibles")
              && has(toolsBar, "showInspector"),
          QStringLiteral("first row: New Folder, Undo, Redo on Main; the theme control on Themes; the quick search and the view modes "
                         "on Search; Compile, Show Invisibles and the inspector's button on Tools"));
    check((!styleCombo || styleBar->isAncestorOf(styleCombo)) && has(styleBar, "defaultTextFormatting") && has(characterBar, "bold")
              && has(characterBar, "alignJustify") && has(characterBar, "showFonts") && has(paragraphBar, "bullets") && has(paragraphBar, "image")
              && !has(styleBar, "bold") && !has(mainBar, "compile"),
          QStringLiteral("second row: styles and fonts on Style and Font; Bold and the alignments on Character and Alignment; lists, "
                         "spacing, comments and pictures on Paragraph and Insert"));
    // the names in the menu
    {
        QStringList names;
        for (toolbars::Bar b : all)
            if (QAction *one = w.findChild<QAction *>(toolbars::barCommand(b)))
                names << one->text().remove(QLatin1Char('&')) + (one->isCheckable() && one->isChecked() ? QStringLiteral("*") : QString());
        check(names == QStringList({QStringLiteral("Main*"), QStringLiteral("Themes*"), QStringLiteral("Search*"), QStringLiteral("Tools*"),
                                    QStringLiteral("Style and Font*"), QStringLiteral("Character and Alignment*"),
                                    QStringLiteral("Paragraph and Insert*")}),
              QStringLiteral("View > Toolbars has a switch for each toolbar, all on"), names.join(QLatin1String(", ")));
        QMenu *menu = w.createPopupMenu();
        int switches = 0;
        bool reset = false, rows = false;
        for (QAction *ac : menu->actions()) {
            for (toolbars::Bar b : all)
                switches += ac->objectName() == toolbars::barCommand(b) ? 1 : 0;
            reset = reset || ac->objectName() == QLatin1String("resetToolbarPlaces");
            rows = rows || ac->objectName() == QLatin1String("showFormatBar");
        }
        check(switches == 7 && reset && rows,
              QStringLiteral("a right-click beside the toolbars offers the same switches, the rows' switches and Reset Toolbar Places"));
        delete menu;
    }

    // ---- a place along the row, changed by the grip: the bars after it make room, the bar keeps its buttons
    const int searchX = placeOf(w, searchBar).x(), toolsX = placeOf(w, toolsBar).x(), searchWidth = searchBar->width();
    auto *themesGrip = qobject_cast<GripBar *>(themesBar);
    auto *searchGrip = qobject_cast<GripBar *>(searchBar);
    check(themesGrip && searchGrip && searchGrip->gripRect().isValid() && searchGrip->gripRect().left() < 6 && themesGrip->gap() == 0,
          QStringLiteral("(the bars are grip bars; a bar's grip is at its front)"));
    if (!themesGrip || !searchGrip)
        return;
    auto drag = [&](QToolBar *bar, int dx) {
        const QPoint grip = qobject_cast<GripBar *>(bar)->gripRect().center();
        const QPoint start = bar->mapToGlobal(grip);
        QTest::mousePress(bar, Qt::LeftButton, Qt::NoModifier, grip);
        // the mouse goes over the screen in steps of 10 px; the bar follows it, so each step is told from where the
        // bar is at that moment
        const int steps = qAbs(dx) / 10;
        for (int k = 1; k <= steps; ++k) {
            settle(15);
            QTest::mouseMove(bar, bar->mapFromGlobal(start + QPoint(dx > 0 ? 10 * k : -10 * k, 0)), 2);
        }
        settle(100);
        QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, bar->mapFromGlobal(start + QPoint(dx, 0)));
        settle(300);
    };
    drag(searchBar, 160);
    const int movedX = placeOf(w, searchBar).x();
    const int moved = movedX - searchX;
    check(moved >= 100 && placeOf(w, searchBar).y() == placeOf(w, mainBar).y() && themesGrip->gap() == moved
              && placeOf(w, toolsBar).x() == toolsX + moved && searchBar->width() == searchWidth && quick && quick->isVisible()
              && placeOf(w, themesBar).x() < movedX,
          QStringLiteral("dragged to the right by its grip, the Search bar stands further right with all its buttons, and the "
                         "Tools bar after it has made room: the space before a bar is adjustable"),
          QStringLiteral("Search x %1 -> %2 (width %3 -> %4), Tools x %5 -> %6, gap after Themes %7")
              .arg(searchX).arg(movedX).arg(searchWidth).arg(searchBar->width()).arg(toolsX).arg(placeOf(w, toolsBar).x()).arg(themesGrip->gap()));
    check(QSettings().value(QStringLiteral("window/barGaps")).toMap().value(QStringLiteral("themesBar")).toInt() == moved,
          QStringLiteral("the program keeps the space (window/barGaps)"));
    shot(&w, QStringLiteral("toolbars-search-moved"));
    // to the right as far as the row has room, no further: the last bar keeps its buttons
    drag(toolsBar, 2000);
    check(toolsBar->width() >= toolsBar->sizeHint().width() && placeOf(w, toolsBar).x() + toolsBar->width() <= w.width() + 1
              && searchGrip->gap() > 100 && has(toolsBar, "showInspector")
              && toolsBar->widgetForAction(w.findChild<QAction *>(QStringLiteral("showInspector")))->isVisible(),
          QStringLiteral("the last bar of a row goes right only as far as the window has room for its buttons"),
          QStringLiteral("Tools x %1 w %2 (needs %3), window %4").arg(placeOf(w, toolsBar).x()).arg(toolsBar->width())
              .arg(toolsBar->sizeHint().width()).arg(w.width()));
    shot(&w, QStringLiteral("toolbars-tools-right"));
    // and back to the left: as far as the space goes (0.91.1: the mouse stays right of the middle of the bar before it;
    // further left it would ask for another order, see v0911)
    drag(toolsBar, -(searchGrip->gap() + 20));
    drag(searchBar, -(themesGrip->gap() + 20));
    // (0.91.1: a bar stays where it stands when the bar before it moves away to the left; it is dragged up to it)
    const bool toolsStayed = placeOf(w, toolsBar).x() == toolsX + moved && searchGrip->gap() == moved;
    drag(toolsBar, -(searchGrip->gap() + 20));
    check(toolsStayed && themesGrip->gap() == 0 && searchGrip->gap() == 0 && placeOf(w, searchBar).x() == searchX && placeOf(w, toolsBar).x() == toolsX
              && QSettings().value(QStringLiteral("window/barGaps")).toMap().isEmpty(),
          QStringLiteral("dragged to the left one after the other, the bars close up again and stop where the bar before them "
                         "ends; a bar keeps its place while the bar before it moves away from it"),
          places() + QStringLiteral(" gaps %1 %2, Search x %3 (was %4), Tools x %5 (was %6), kept gaps %7").arg(themesGrip->gap())
              .arg(searchGrip->gap()).arg(placeOf(w, searchBar).x()).arg(searchX).arg(placeOf(w, toolsBar).x()).arg(toolsX)
              .arg(QSettings().value(QStringLiteral("window/barGaps")).toMap().keys().join(QLatin1Char(','))));
    drag(searchBar, 160);
    const int againX = placeOf(w, searchBar).x();
    // a bar at the bottom edge (where a drag can put it), kept by the program (0.91.1: window/barRows, ui/BarDock.*)
    auto *dock = w.findChild<BarDock *>();
    if (dock) {
        barplaces::Target bottomEdge;
        bottomEdge.valid = true;
        bottomEdge.bottom = true;
        bottomEdge.newRow = true;
        dock->setPlaces(barplaces::moved(dock->places(), QStringLiteral("paragraphBar"), bottomEdge));
    }
    settle(300);
    const bool atBottom = w.toolBarArea(paragraphBar) == Qt::BottomToolBarArea && placeOf(w, paragraphBar).y() > w.height() / 2 && paragraphBar->isVisible();
    shot(&w, QStringLiteral("toolbars-paragraph-bottom"));
    const QStringList keptRows = QSettings().value(QStringLiteral("window/barRows")).toStringList();
    // Reset Toolbar Places
    QAction *reset = w.findChild<QAction *>(QStringLiteral("resetToolbarPlaces"));
    if (reset)
        reset->trigger();
    settle(400);
    const bool back = inRow({mainBar, themesBar, searchBar, toolsBar}) && inRow({styleBar, characterBar, paragraphBar, iconBar})
                      && w.toolBarArea(paragraphBar) == Qt::TopToolBarArea && placeOf(w, searchBar).x() == searchX && themesGrip->gap() == 0
                      && QSettings().value(QStringLiteral("window/barGaps")).toMap().isEmpty();
    check(atBottom && againX > searchX + 100 && reset && back && w.statusBar()->currentMessage().contains(QLatin1String("standard places")),
          QStringLiteral("a bar can stand at the bottom edge; View > Toolbars > Reset Toolbar Places puts all bars back and closes "
                         "the spaces"),
          places());
    // the rows and edges are kept by the program (what it finds again at its next start)
    if (dock) {
        dock->setPlaces(barplaces::fromText(keptRows));
        w.addToolBar(Qt::TopToolBarArea, paragraphBar); // (thrown about with the window's own means)
        dock->load();
    }
    settle(400);
    check(dock && keptRows.contains(QStringLiteral("bottom|paragraphBar:0")) && w.toolBarArea(paragraphBar) == Qt::BottomToolBarArea,
          QStringLiteral("the program keeps the rows and edges: the bar at the bottom is there again"), places());
    if (reset)
        reset->trigger();
    settle(300);

    // ---- dragged out of its row by the grip into another row (here: from the second row up into the first). 0.91.1:
    // the drag is the program's own; the bar is never lifted out of the window as Qt would do it
    {
        const int rowOne = placeOf(w, mainBar).y(), rowTwo = placeOf(w, characterBar).y();
        const QPoint grip = qobject_cast<GripBar *>(characterBar)->gripRect().center();
        const QPoint start = characterBar->mapToGlobal(grip);
        QTest::mousePress(characterBar, Qt::LeftButton, Qt::NoModifier, grip);
        for (int k = 1; k <= 9; ++k) {
            settle(30);
            QTest::mouseMove(characterBar, characterBar->mapFromGlobal(start + QPoint(0, -4 * k)), 2);
        }
        settle(300);
        const bool lifted = characterBar->isWindow();
        const bool marked = dock && !dock->markerRect().isEmpty();
        QTest::mouseRelease(characterBar, Qt::LeftButton, Qt::NoModifier, characterBar->mapFromGlobal(start + QPoint(0, -36)));
        settle(500);
        check(!lifted && marked && !characterBar->isWindow() && placeOf(w, characterBar).y() == rowOne && placeOf(w, paragraphBar).y() == rowTwo
                  && placeOf(w, styleBar).y() == rowTwo && characterBar->isVisible() && has(characterBar, "bold"),
              QStringLiteral("dragged up out of its row by the grip, Character and Alignment stands in the first row; the others "
                             "of its row stay"),
              QStringLiteral("lifted %1, marker %2, now %3 (rows at %4 and %5), window %6")
                  .arg(lifted).arg(marked).arg(places()).arg(rowOne).arg(rowTwo).arg(characterBar->isWindow()));
        shot(&w, QStringLiteral("toolbars-character-dragged-up"));
        if (reset)
            reset->trigger();
        settle(300);
        check(placeOf(w, characterBar).y() == rowTwo && inRow({styleBar, characterBar, paragraphBar, iconBar}),
              QStringLiteral("Reset Toolbar Places brings it back into the second row"), places());
    }

    // ---- a switch per bar, and the rows' switches
    QAction *showThemes = w.findChild<QAction *>(QStringLiteral("showBarThemes"));
    QAction *showRow = w.findChild<QAction *>(QStringLiteral("showToolbar"));
    QAction *showFormat = w.findChild<QAction *>(QStringLiteral("showFormatBar"));
    if (showThemes)
        showThemes->trigger();
    settle(200);
    check(showThemes && !themesBar->isVisible() && mainBar->isVisible() && searchBar->isVisible() && as.toolbarHidden == QStringList{QStringLiteral("themes")}
              && QSettings().value(QStringLiteral("toolbars/hidden")).toStringList() == QStringList{QStringLiteral("themes")}
              && placeOf(w, searchBar).x() < searchX,
          QStringLiteral("the Themes bar switched off: it goes, the bars after it move up, and the program keeps the choice"),
          places());
    if (showRow)
        showRow->trigger(); // Show Toolbar off: the whole first row
    settle(200);
    check(showRow && !mainBar->isVisible() && !searchBar->isVisible() && !toolsBar->isVisible() && !themesBar->isVisible()
              && styleBar->isVisible() && characterBar->isVisible() && showThemes && !showThemes->isEnabled()
              && w.findChild<QAction *>(QStringLiteral("showBarStyle"))->isEnabled(),
          QStringLiteral("Show Toolbar off: the four bars of the first row go (as layouts and Composition Mode expect); their "
                         "switches are off limits meanwhile"));
    if (showRow)
        showRow->trigger();
    settle(200);
    check(mainBar->isVisible() && searchBar->isVisible() && toolsBar->isVisible() && !themesBar->isVisible(),
          QStringLiteral("Show Toolbar on again: the row is back as it was, the Themes bar still off"));
    if (showFormat)
        showFormat->trigger();
    settle(200);
    const bool formatOff = !styleBar->isVisible() && !characterBar->isVisible() && !paragraphBar->isVisible() && mainBar->isVisible();
    if (showFormat)
        showFormat->trigger();
    settle(200);
    check(formatOff && styleBar->isVisible() && characterBar->isVisible() && paragraphBar->isVisible(),
          QStringLiteral("Show Format Bar switches the three format toolbars"));
    if (showThemes)
        showThemes->trigger();
    settle(200);
    check(themesBar->isVisible() && as.toolbarHidden.isEmpty() && inRow({mainBar, themesBar, searchBar, toolsBar}),
          QStringLiteral("the Themes bar switched on again stands where it was"), places());

    // ---- Customize Toolbars with all bars
    {
        ToolbarDialog td(&w, toolbars::Bar::Main);
        auto *combo = td.findChild<QComboBox *>(QStringLiteral("toolbarBar"));
        QStringList offered;
        for (int i = 0; combo && i < combo->count(); ++i)
            offered << combo->itemText(i);
        check(offered.size() == 7 && offered.first() == QLatin1String("Main") && offered.contains(QStringLiteral("Search"))
                  && td.items() == toolbars::standard(toolbars::Bar::Main),
              QStringLiteral("Customize Toolbars lists the seven toolbars"), offered.join(QLatin1String(", ")));
        // the quick search field moves to the Main bar: a control exists once
        const bool moved = td.addItem(QStringLiteral("search"));
        td.setBar(toolbars::Bar::Search);
        const QStringList searchItems = td.items();
        td.setBar(toolbars::Bar::Format);
        const bool refused = !td.addItem(QStringLiteral("search")) && !td.addItem(QStringLiteral("themePicker"));
        td.apply();
        settle(300);
        check(moved && refused && searchItems == QStringList({QStringLiteral("searchMenu"), QStringLiteral("viewModes")}) && quick
                  && mainBar->isAncestorOf(quick) && !searchBar->isAncestorOf(quick) && as.toolbarMain.contains(QStringLiteral("search"))
                  && as.toolbarSearch == searchItems,
              QStringLiteral("the quick search field added to the Main bar moves there from the Search bar; a control of the main "
                             "toolbars cannot go on a format toolbar"),
              as.toolbarMain.join(QLatin1Char(' ')) + QLatin1String(" / ") + as.toolbarSearch.join(QLatin1Char(' ')));
        shot(&w, QStringLiteral("toolbars-search-on-main"));
        // Standard Set on the Search bar brings it back
        td.setBar(toolbars::Bar::Search);
        td.resetToStandard();
        td.apply();
        settle(300);
        check(quick && searchBar->isAncestorOf(quick) && as.toolbarSearch.isEmpty() && as.toolbarMain.isEmpty(),
              QStringLiteral("Standard Set on the Search bar brings the field back; both bars are the standard again"),
              as.toolbarMain.join(QLatin1Char(' ')) + QLatin1String(" / ") + as.toolbarSearch.join(QLatin1Char(' ')));
        // a command on a format toolbar, and a bar left empty
        td.setBar(toolbars::Bar::Character);
        const bool added = td.addItem(QStringLiteral("snapshot"));
        td.setBar(toolbars::Bar::Themes);
        const bool emptied = td.removeItem(QStringLiteral("themePicker"));
        td.apply();
        settle(300);
        check(added && emptied && has(characterBar, "snapshot") && as.toolbarCharacter.contains(QStringLiteral("snapshot"))
                  && !themesBar->isVisible() && as.toolbarHidden.contains(QStringLiteral("themes")),
              QStringLiteral("a command added to Character and Alignment shows there; a toolbar left empty is switched off"),
              as.toolbarCharacter.join(QLatin1Char(' ')));
        td.setBar(toolbars::Bar::Character);
        td.resetToStandard();
        td.apply();
        if (showThemes && !showThemes->isChecked())
            showThemes->trigger();
        settle(300);
        check(themesBar->isVisible() && picker && themesBar->isAncestorOf(picker) && picker->isVisible() && as.toolbarCharacter.isEmpty()
                  && as.toolbarHidden.isEmpty(),
              QStringLiteral("switched on again, the Themes bar has its standard set"));
    }
    // the settings of the seven bars are written under their own keys; the two old lists are not touched
    as.save();
    QSettings s;
    check(s.value(QStringLiteral("toolbars/split0910")).toBool() && s.contains(QStringLiteral("toolbars/bar-search"))
              && !s.contains(QStringLiteral("toolbars/main")) && !s.contains(QStringLiteral("toolbars/format")),
          QStringLiteral("the bars' lists are kept under keys of their own; the lists of earlier versions are not written"));
    shot(&w, QStringLiteral("toolbars-end"));
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV0910Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV0910Test"));
    QApplication app(argc, argv);
    if (argc > 1) {
        shotDir = QString::fromLocal8Bit(argv[1]);
        QDir().mkpath(shotDir);
    }
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();

    backupChecks(home.path());

    QTemporaryDir work;
    templates::Info novel;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("novel"))
            novel = t;
    QString err;
    auto proj = templates::createProject(novel, work.path(), QStringLiteral("The Harbor"), &err);
    check(proj != nullptr, QStringLiteral("(project made)"), err);
    if (!proj)
        return 1;
    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1800, 950);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(proj)), QStringLiteral("(project open)"));
    settle(400);

    toolbarChecks(w);

    w.close();
    delete wp;
    settle(100);
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
