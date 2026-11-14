// Checks of 0.91.1 beta (the writer's report of 2026-11-13 on 0.91.0, Kubuntu with Wayland): "the toolbars cannot be
// changed in the sequence, neither can they be switches from toolbar to format bar. make them so that they can be
// rearanged freely within the toolbar- and format bar rows, also in sequence. also, there are situations (e.g. when i
// moved the search bar as right as possible), the toolbar row got stuck somehow and could not get further adjusted".
//  - the rules (ui/BarPlaces.*): the kept text, repairs of a broken text, a toolbar taken to another place, the free
//    space in a row that is too narrow, a toolbar shifted along its row;
//  - the window (ui/BarDock.*): Qt's own toolbar drag never starts; with Search at the far right the toolbars before
//    it still move (the stuck row); order in a row; from one row into the other; a row of its own; the bottom edge;
//    the marker; Esc; a drop on nothing; what the program keeps and finds again; the step from 0.91.0; Reset; a
//    toolbar that is switched off; a narrow window; many drags in a row leave every toolbar movable.
//
//   xvfb-run -a ./qb_v0911_tests /tmp/shots

#include "compile/PluginManager.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "ui/AppSettings.h"
#include "ui/BarDock.h"
#include "ui/BarPlaces.h"
#include "ui/GripBar.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "ui/ToolbarConfig.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QRandomGenerator>
#include <QSettings>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <algorithm>
#include <cstdio>

using namespace qb;
using namespace qb::barplaces;

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

// ------------------------------------------------------------------ the rules

static Places std8()
{
    return fromRows({{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")},
                     {QStringLiteral("e"), QStringLiteral("f"), QStringLiteral("g"), QStringLiteral("h")}});
}

static Target into(bool bottom, int row, const QString &before = QString(), bool newRow = false)
{
    Target t;
    t.valid = true;
    t.bottom = bottom;
    t.row = row;
    t.before = before;
    t.newRow = newRow;
    return t;
}

static void ruleChecks()
{
    std::printf("== The rules for the toolbars' places (0.91.1)\n");
    const Places st = std8();
    // the kept text
    Places p = st;
    p.top[0][2].lead = 120;
    p.bottom << Row{Slot{QStringLiteral("h"), 0}};
    p.top[1].removeLast();
    const QStringList text = toText(p);
    check(text == QStringList({QStringLiteral("top|a:0 b:0 c:120 d:0"), QStringLiteral("top|e:0 f:0 g:0"), QStringLiteral("bottom|h:0")})
              && fromText(text) == p && normalized(p, st) == p,
          QStringLiteral("the places are kept as one line per row, with each toolbar's free space, and read back the same"),
          text.join(QLatin1String(" ; ")));
    // a text that is broken, old or from a newer version is repaired
    {
        const Places odd = fromText({QStringLiteral("top|c:-5 a:99999  zzz:3,a:7"), QStringLiteral("top|"), QStringLiteral("nonsense"),
                                     QStringLiteral("bottom|f:40,e:10"), QStringLiteral("left|g:1")});
        const Places n = normalized(odd, st);
        // c first (no lead), a (lead cut to the limit), b behind a (its standard neighbor), d behind c; g and h behind f
        check(describe(n) == QLatin1String("top: c d +4000 a b; bottom: f g h +10 e"),
              QStringLiteral("a broken text is repaired: every toolbar once, unknown ones dropped, no empty row, no free space "
                             "before a row's first toolbar, a missing toolbar behind its standard neighbor"),
              describe(n));
        check(normalized(Places(), st) == st && normalized(fromText({QStringLiteral("top|h:0")}), st).top.size() == 2,
              QStringLiteral("no text at all gives the standard places"), describe(normalized(fromText({QStringLiteral("top|h:0")}), st)));
    }
    // a toolbar taken to another place
    {
        Places q = moved(st, QStringLiteral("a"), into(false, 0, QStringLiteral("d")));
        const bool order = describe(q) == QLatin1String("top: b c a d / e f g h");
        q = moved(q, QStringLiteral("d"), into(false, 0, QStringLiteral("b")));
        check(order && describe(q) == QLatin1String("top: d b c a / e f g h"),
              QStringLiteral("a toolbar is taken to another place of its row: the order changes, to the right and to the left"), describe(q));
        q = moved(st, QStringLiteral("f"), into(false, 0, QStringLiteral("b")));
        const bool up = describe(q) == QLatin1String("top: a f b c d / e g h");
        q = moved(q, QStringLiteral("c"), into(false, 1));
        check(up && describe(q) == QLatin1String("top: a f b d / e g h c"),
              QStringLiteral("from one row into the other: in front of a toolbar there, or at the row's end"), describe(q));
        q = moved(st, QStringLiteral("c"), into(false, 2, QString(), true));
        const bool third = describe(q) == QLatin1String("top: a b d / e f g h / c");
        q = moved(q, QStringLiteral("g"), into(true, 0, QString(), true));
        q = moved(q, QStringLiteral("a"), into(true, 1, QString(), true));
        check(third && describe(q) == QLatin1String("top: b d / e f h / c; bottom: g / a"),
              QStringLiteral("into a row of its own under the top rows, and to the bottom edge"), describe(q));
        // a row that loses its last toolbar is gone; a place that is the same is no change
        q = moved(q, QStringLiteral("c"), into(false, 0));
        check(describe(q) == QLatin1String("top: b d c / e f h; bottom: g / a")
                  && moved(st, QStringLiteral("d"), into(false, 0)) == st && moved(q, QStringLiteral("a"), into(true, 2, QString(), true)) == q
                  && moved(st, QStringLiteral("nobody"), into(false, 0)) == st,
              QStringLiteral("a row without toolbars is dropped; putting a toolbar where it stands changes nothing"), describe(q));
        // free space stays where it was
        Places gap = st;
        gap.top[0][1].lead = 50;  // a +50 b
        gap.top[0][2].lead = 200; // b +200 c
        q = moved(gap, QStringLiteral("b"), into(false, 1));
        const bool merged = describe(q) == QLatin1String("top: a +250 c d / e f g h b");
        q = moved(gap, QStringLiteral("b"), into(false, 1), {QStringLiteral("c")});
        check(merged && describe(q) == QLatin1String("top: a +200 c +50 d / e f g h b")
                  && describe(moved(gap, QStringLiteral("c"), into(false, 0, QStringLiteral("a")))) == QLatin1String("top: c a +50 b +200 d / e f g h"),
              QStringLiteral("free space in front of a toolbar that leaves stays in the row: the next toolbar that shows takes it "
                             "over; the toolbar itself arrives without free space"),
              describe(q));
    }
    // free space in a row
    {
        const QList<Shown> row = {{QStringLiteral("a"), 300, 70}, {QStringLiteral("b"), 200, 100}, {QStringLiteral("c"), 100, 300}};
        check(fitted(row, 1200) == QList<int>({0, 100, 300}) && lefts(row) == QList<int>({0, 400, 900}),
              QStringLiteral("a row with room shows the free space as it is kept; the first toolbar has none"));
        const QList<int> tight = fitted(row, 800); // 200 px of room for 400 px of free space
        check(tight == QList<int>({0, 50, 150}) && fitted(row, 500) == QList<int>({0, 0, 0}) && fitted(row, 700, 30) == QList<int>({0, 0, 75}),
              QStringLiteral("a row that is too narrow shrinks the free space in proportion, down to none, before a toolbar "
                             "loses buttons; very little free space is none"),
              QStringLiteral("%1 %2 %3").arg(tight.value(0)).arg(tight.value(1)).arg(tight.value(2)));
    }
    // a toolbar shifted along its row
    {
        // a(0..300) b(300..500) [600 free] c(1100..1200) d(1200..1400), the row is 1400 wide: c and d stand at its end
        const QList<Shown> row = {{QStringLiteral("a"), 300, 0}, {QStringLiteral("b"), 200, 0}, {QStringLiteral("c"), 100, 600},
                                  {QStringLiteral("d"), 200, 0}};
        const QList<int> right = shifted(row, 1400, 1, 450);
        check(right == QList<int>({0, 150, 450, 0}),
              QStringLiteral("THE REPORT: with a toolbar at the far right of a full row, a toolbar before it still moves to the "
                             "right: it takes the free space after it (0.91.0 looked for room at the row's end only: stuck)"),
              QStringLiteral("%1 %2 %3 %4").arg(right.value(0)).arg(right.value(1)).arg(right.value(2)).arg(right.value(3)));
        const QList<int> touch = shifted(row, 1400, 1, 2000);
        check(touch == QList<int>({0, 600, 0, 0}) && shifted(row, 1400, 2, 0) == QList<int>({0, 0, 0, 600})
                  && shifted(row, 1400, 2, 5000) == QList<int>({0, 0, 600, 0}) && shifted(row, 1400, 0, 500) == QList<int>({0, 0, 600, 0}),
              QStringLiteral("it goes right until the toolbars after it stand together at the row's end, left until the toolbar "
                             "before it; the toolbars after it stay until it reaches them; a row's first toolbar stays"),
              QStringLiteral("%1 %2 %3 %4").arg(touch.value(0)).arg(touch.value(1)).arg(touch.value(2)).arg(touch.value(3)));
        // room at the end: the next toolbar is pushed, the one after it keeps its place as long as there is space
        const QList<Shown> open = {{QStringLiteral("a"), 300, 0}, {QStringLiteral("b"), 200, 0}, {QStringLiteral("c"), 100, 0},
                                   {QStringLiteral("d"), 200, 150}};
        const QList<int> push = shifted(open, 2000, 1, 400);
        check(push == QList<int>({0, 100, 0, 50}) && shifted(open, 2000, 1, 303, 6) == QList<int>({0, 0, 0, 150}),
              QStringLiteral("a toolbar pushed against the next one moves it along and uses up the free space behind that one"),
              QStringLiteral("%1 %2 %3 %4").arg(push.value(0)).arg(push.value(1)).arg(push.value(2)).arg(push.value(3)));
    }
}

// ------------------------------------------------------------------ the window

struct Bars
{
    MainWindow *w = nullptr;
    BarDock *dock = nullptr;
    QList<GripBar *> all; // Main, Themes, Search, Tools, Style, Character, Paragraph, icon bar
    GripBar *named(const char *name) const
    {
        for (GripBar *b : all)
            if (b->objectName() == QLatin1String(name))
                return b;
        return nullptr;
    }
    QPoint at(QWidget *bar) const { return bar->mapTo(w, QPoint(0, 0)); }
    // the rows as they show: the visible toolbars by where they stand
    QString seen() const
    {
        QList<GripBar *> v;
        for (GripBar *b : all)
            if (b->isVisible())
                v << b;
        std::sort(v.begin(), v.end(), [this](GripBar *x, GripBar *y) {
            const QPoint px = at(x), py = at(y);
            return px.y() != py.y() ? px.y() < py.y() : px.x() < py.x();
        });
        QString s;
        for (int i = 0; i < v.size(); ++i) {
            if (i > 0)
                s += at(v[i]).y() != at(v[i - 1]).y() ? QLatin1String(" / ") : QLatin1String(" ");
            s += v[i]->objectName();
        }
        return s;
    }
    // the same from the places (visible toolbars only), top rows then bottom rows
    QString kept(const Places &p) const
    {
        QStringList rows;
        for (const QList<Row> *list : {&p.top, &p.bottom})
            for (const Row &row : *list) {
                QStringList names;
                for (const Slot &s : row)
                    if (GripBar *b = w->findChild<GripBar *>(s.name); b && b->isVisible())
                        names << s.name;
                if (!names.isEmpty())
                    rows << names.join(QLatin1Char(' '));
            }
        return rows.join(QLatin1String(" / "));
    }
    static QString names(const Places &p)
    {
        Places q = p;
        for (QList<Row> *list : {&q.top, &q.bottom})
            for (Row &row : *list)
                for (Slot &s : row)
                    s.lead = 0;
        return describe(q);
    }
    // everything agrees: the places, what the QMainWindow holds and what shows
    bool consistent(QString *why = nullptr) const
    {
        const Places p = dock->places();
        const bool ok = names(p) == names(dock->placesOfWindow()) && kept(p) == seen() && normalized(p, dock->standardPlaces()) == p;
        if (why)
            *why = QStringLiteral("places: %1 | window: %2 | seen: %3").arg(describe(p), names(dock->placesOfWindow()), seen());
        return ok;
    }
    int rowY(GripBar *bar) const { return at(bar).y() + bar->height() / 2; }

    // The left button goes down on the bar's grip and the mouse goes to `to` (the window's coordinates) in steps.
    // Each step is told from where the bar stands at that moment (it may follow the mouse).
    void press(GripBar *bar, const QPoint &to, int stepPx = 12) const
    {
        const QPoint grip = bar->gripRect().center();
        const QPoint from = bar->mapTo(w, grip);
        QTest::mousePress(bar, Qt::LeftButton, Qt::NoModifier, grip);
        const int steps = qMax(2, qMax(qAbs(to.x() - from.x()), qAbs(to.y() - from.y())) / stepPx);
        for (int k = 1; k <= steps; ++k) {
            settle(8);
            const QPoint p = from + (to - from) * k / steps;
            QTest::mouseMove(bar, bar->mapFrom(w, p), 1);
        }
        settle(60);
    }
    void release(GripBar *bar, const QPoint &to) const
    {
        QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, bar->mapFrom(w, to));
        settle(250);
    }
    void drag(GripBar *bar, const QPoint &to) const
    {
        press(bar, to);
        release(bar, to);
    }
    // along its row by dx pixels
    void slide(GripBar *bar, int dx) const
    {
        const QPoint from = bar->mapTo(w, bar->gripRect().center());
        drag(bar, from + QPoint(dx, 0));
    }
};

static void windowChecks(MainWindow &w)
{
    std::printf("== Toolbars rearranged freely by their grips (0.91.1)\n");
    Bars b;
    b.w = &w;
    b.dock = w.findChild<BarDock *>();
    for (toolbars::Bar bar : toolbars::allBars())
        b.all << w.findChild<GripBar *>(toolbars::barObjectName(bar));
    b.all << w.findChild<GripBar *>(QStringLiteral("iconBar"));
    check(b.dock && !b.all.contains(nullptr) && b.all.size() == 8, QStringLiteral("(the window has its eight grip bars and their place keeper)"));
    if (!b.dock || b.all.contains(nullptr))
        return;
    if (QAction *icons = w.findChild<QAction *>(QStringLiteral("showIconBar")); icons && !icons->isChecked()) {
        icons->trigger();
        settle(300);
    }
    GripBar *mainBar = b.all[0], *themes = b.all[1], *search = b.all[2], *tools = b.all[3], *style = b.all[4], *character = b.all[5],
            *paragraph = b.all[6], *iconBar = b.all[7];
    QAction *reset = w.findChild<QAction *>(QStringLiteral("resetToolbarPlaces"));
    const QString standard = QStringLiteral("mainToolbar themesBar searchBar toolsBar / formatBar characterBar paragraphBar iconBar");
    QString why;
    check(b.seen() == standard && b.consistent(&why) && b.dock->places() == b.dock->standardPlaces()
              && QSettings().value(QStringLiteral("window/barRows")).toStringList().size() == 2,
          QStringLiteral("a fresh program shows the standard places and keeps them as rows (window/barRows)"), why);
    const int rowOne = b.rowY(mainBar), rowTwo = b.rowY(style);

    // ---- Qt's own drag is out of the game: a toolbar never leaves the window while it is dragged
    {
        b.press(character, QPoint(b.at(character).x() + 40, rowOne));
        const bool inside = !character->isWindow() && character->parentWidget() == &w && b.at(character).y() == b.at(style).y();
        check(inside && b.dock->dragging() && b.dock->dragMode() == BarDock::Mode::Place && b.dock->markerRect().height() >= mainBar->height() - 2
                  && b.dock->markerRect().width() == 3 && qAbs(b.dock->markerRect().center().y() - rowOne) <= 3,
              QStringLiteral("dragged by its grip toward the other row, a toolbar stays in the window where it was (Qt does not "
                             "lift it out, so no desktop's own drag and drop is needed); a marker in the first row shows where it will stand"),
              QStringLiteral("window %1, mode %2, marker %3,%4 %5x%6").arg(character->isWindow()).arg(int(b.dock->dragMode()))
                  .arg(b.dock->markerRect().x()).arg(b.dock->markerRect().y()).arg(b.dock->markerRect().width()).arg(b.dock->markerRect().height()));
        shot(&w, QStringLiteral("bars-marker-other-row"));
        // Esc: no change
        QTest::keyClick(QApplication::focusWidget() ? QApplication::focusWidget() : &w, Qt::Key_Escape);
        settle(100);
        const bool ended = !b.dock->dragging() && b.dock->markerRect().isEmpty();
        b.release(character, QPoint(b.at(character).x() + 40, rowOne));
        check(ended && b.seen() == standard && b.consistent(&why) && QApplication::overrideCursor() == nullptr,
              QStringLiteral("Esc ends the drag: the marker goes and every toolbar is where it was"), why);
    }

    // ---- the report: Search as far right as possible, then the row must not be stuck
    {
        const int themesX = b.at(themes).x(), toolsNeed = tools->naturalWidth();
        const int room = w.width() - (b.at(tools).x() + toolsNeed);
        b.slide(search, room);
        const int searchFar = b.at(search).x(), toolsFar = b.at(tools).x();
        const bool far = room > 150 && searchFar - (b.at(themes).x() + themes->naturalWidth()) >= room - 8
                         && toolsFar + toolsNeed >= w.width() - 8 && tools->width() >= toolsNeed && b.seen() == standard;
        check(far, QStringLiteral("(Search dragged right as far as it goes: Search and Tools stand at the row's end, Tools with all its buttons)"),
              QStringLiteral("room %1, Search x %2, Tools x %3 w %4 needs %5, window %6").arg(room).arg(searchFar).arg(toolsFar).arg(tools->width())
                  .arg(toolsNeed).arg(w.width()));
        shot(&w, QStringLiteral("bars-search-far-right"));
        b.slide(themes, 150);
        const int themesNow = b.at(themes).x();
        check(themesNow >= themesX + 140 && themesNow <= themesX + 160 && b.at(search).x() == searchFar && b.at(tools).x() == toolsFar
                  && b.consistent(&why),
              QStringLiteral("THE REPORT: with Search at the far right the row is not stuck: Themes, dragged right, moves into the "
                             "free space after it, and Search and Tools stay where they are"),
              QStringLiteral("Themes x %1 -> %2; Search x %3 -> %4; Tools x %5 -> %6; %7").arg(themesX).arg(themesNow).arg(searchFar)
                  .arg(b.at(search).x()).arg(toolsFar).arg(b.at(tools).x()).arg(why));
        shot(&w, QStringLiteral("bars-themes-into-free-space"));
        // and everything else in that row still answers: Search back to the left until it meets Themes, Tools follows only when dragged
        // (the mouse ends 30 px left of where Themes ends: right of Themes' middle, so this is no change of order)
        b.slide(search, -(b.at(search).x() - (themesNow + themes->naturalWidth()) + 30));
        const bool searchBack = b.at(search).x() <= themesNow + themes->naturalWidth() + 8 && b.at(tools).x() == toolsFar;
        b.slide(tools, -100);
        check(searchBack && b.at(tools).x() <= toolsFar - 90 && b.at(tools).x() >= toolsFar - 110 && b.seen() == standard,
              QStringLiteral("Search goes back left until it meets Themes (it does not jump over it while the mouse stays right "
                             "of Themes' middle), and Tools moves on its own"),
              QStringLiteral("Search x %1, Themes ends %2, Tools x %3 (was %4)").arg(b.at(search).x()).arg(themesNow + themes->naturalWidth())
                  .arg(b.at(tools).x()).arg(toolsFar));
        const Places p = b.dock->places();
        const Where th = find(p, QStringLiteral("themesBar")), to = find(p, QStringLiteral("toolsBar"));
        check(p.top[th.row][th.index].lead >= 140 && p.top[to.row][to.index].lead > 100
                  && fromText(QSettings().value(QStringLiteral("window/barRows")).toStringList()) == p
                  && QSettings().value(QStringLiteral("window/barGaps")).toMap().value(QStringLiteral("mainToolbar")).toInt() == p.top[th.row][th.index].lead,
              QStringLiteral("the free space is kept with the toolbar it stands in front of (window/barRows; window/barGaps for 0.91.0)"),
              describe(p));
        if (reset)
            reset->trigger();
        settle(300);
    }

    // ---- the order in a row
    {
        // Tools in front of Themes: the mouse goes to the left half of Themes
        const QPoint to(b.at(themes).x() + 10, rowOne);
        b.press(tools, to);
        const QRect marker = b.dock->markerRect();
        const bool shown = b.dock->dragMode() == BarDock::Mode::Place && qAbs(marker.center().x() - b.at(themes).x()) <= 3
                           && b.seen() == standard;
        shot(&w, QStringLiteral("bars-marker-order"));
        b.release(tools, to);
        check(shown && b.seen() == QLatin1String("mainToolbar toolsBar themesBar searchBar / formatBar characterBar paragraphBar iconBar")
                  && b.consistent(&why) && b.at(tools).x() == b.at(mainBar).x() + mainBar->width() && tools->width() >= tools->naturalWidth(),
              QStringLiteral("the order in a row: Tools, dragged onto the front half of Themes, shows a marker between Main and "
                             "Themes and stands there after the release, straight after Main"),
              why + QStringLiteral(" marker x %1").arg(marker.center().x()));
        shot(&w, QStringLiteral("bars-order-changed"));
        // the row's first toolbar to the row's end, and the last one to the front
        b.drag(mainBar, QPoint(w.width() - 30, rowOne));
        const bool mainLast = b.seen() == QLatin1String("toolsBar themesBar searchBar mainToolbar / formatBar characterBar paragraphBar iconBar")
                              && b.at(tools).x() == 0;
        b.drag(search, QPoint(3, rowOne));
        check(mainLast && b.seen() == QLatin1String("searchBar toolsBar themesBar mainToolbar / formatBar characterBar paragraphBar iconBar")
                  && b.consistent(&why) && b.at(search).x() == 0,
              QStringLiteral("a row's first toolbar can go to the row's end and another one to the front: any order"), why);
        // the second row the same, the icon bar included
        b.drag(iconBar, QPoint(b.at(style).x() + 5, rowTwo));
        b.drag(style, QPoint(b.at(paragraph).x() + paragraph->naturalWidth() - 10, rowTwo));
        check(b.seen() == QLatin1String("searchBar toolsBar themesBar mainToolbar / iconBar characterBar paragraphBar formatBar")
                  && b.consistent(&why),
              QStringLiteral("the format toolbars and the icon bar change their order in the same way"), why);
        shot(&w, QStringLiteral("bars-both-rows-reordered"));
        if (reset)
            reset->trigger();
        settle(300);
    }

    // ---- from one row into the other
    {
        b.drag(character, QPoint(b.at(themes).x() + 10, rowOne));
        const bool up = b.seen() == QLatin1String("mainToolbar characterBar themesBar searchBar toolsBar / formatBar paragraphBar iconBar")
                        && b.consistent(&why) && w.findChild<QAction *>(QStringLiteral("bold"))
                        && character->widgetForAction(w.findChild<QAction *>(QStringLiteral("bold")))->isVisible();
        check(up, QStringLiteral("THE REPORT: a format toolbar goes into the toolbar row: Character and Alignment, dragged up between "
                                 "Main and Themes, stands there with its buttons; the others of its old row close up"),
              why);
        shot(&w, QStringLiteral("bars-character-in-row-one"));
        b.drag(search, QPoint(w.width() - 20, b.rowY(style)));
        b.drag(mainBar, QPoint(b.at(paragraph).x() + 10, b.rowY(style)));
        check(b.seen() == QLatin1String("characterBar themesBar toolsBar / formatBar mainToolbar paragraphBar iconBar searchBar")
                  && b.consistent(&why),
              QStringLiteral("THE REPORT: a main toolbar goes into the format row: Search to its end, Main between two format toolbars"),
              why);
        shot(&w, QStringLiteral("bars-mixed-rows"));
        // the rows' switches still switch the toolbars they always did, wherever they stand
        QAction *formatRow = w.findChild<QAction *>(QStringLiteral("showFormatBar"));
        if (formatRow)
            formatRow->trigger();
        settle(300);
        const bool off = !character->isVisible() && !style->isVisible() && !paragraph->isVisible() && mainBar->isVisible() && search->isVisible()
                         && b.seen() == QLatin1String("themesBar toolsBar / mainToolbar iconBar searchBar");
        if (formatRow)
            formatRow->trigger();
        settle(300);
        check(off && b.seen() == QLatin1String("characterBar themesBar toolsBar / formatBar mainToolbar paragraphBar iconBar searchBar")
                  && b.consistent(&why),
              QStringLiteral("Show Format Bar still switches the three format toolbars, wherever they stand, and they come back to "
                             "their places"),
              why);
        if (reset)
            reset->trigger();
        settle(300);
    }

    // ---- a row of its own, the bottom edge, a drop on nothing
    {
        QWidget *central = w.centralWidget();
        const int under = central->geometry().top() + 8;
        b.press(search, QPoint(300, under));
        const QRect marker = b.dock->markerRect();
        const bool line = b.dock->dragMode() == BarDock::Mode::Place && marker.width() == w.width() && marker.height() == 3;
        shot(&w, QStringLiteral("bars-marker-new-row"));
        b.release(search, QPoint(300, under));
        const bool third = b.seen() == QLatin1String("mainToolbar themesBar toolsBar / formatBar characterBar paragraphBar iconBar / searchBar")
                           && b.consistent(&why) && b.at(search).x() == 0;
        check(line && third, QStringLiteral("dropped just under the toolbar rows (a line across the window marks it), a toolbar gets "
                                            "a row of its own"),
              why);
        // into that new row, and the bottom edge
        b.drag(tools, QPoint(w.width() / 2, b.rowY(search)));
        const bool joined = b.seen() == QLatin1String("mainToolbar themesBar / formatBar characterBar paragraphBar iconBar / searchBar toolsBar");
        b.drag(paragraph, QPoint(400, w.statusBar()->mapTo(&w, QPoint(0, 0)).y() + 4));
        const bool bottom = w.toolBarArea(paragraph) == Qt::BottomToolBarArea && b.at(paragraph).y() > w.height() / 2 && paragraph->isVisible()
                            && b.dock->places().bottom.size() == 1;
        check(joined && bottom && b.consistent(&why)
                  && b.seen() == QLatin1String("mainToolbar themesBar / formatBar characterBar iconBar / searchBar toolsBar / paragraphBar"),
              QStringLiteral("another toolbar joins that row; and a toolbar dropped on the window's bottom edge stands there"), why);
        shot(&w, QStringLiteral("bars-three-rows-and-bottom"));
        // a second one down there: beside it, and one in a row above it
        b.drag(iconBar, QPoint(w.width() - 40, b.rowY(paragraph)));
        b.drag(themes, QPoint(200, central->geometry().bottom() - 6));
        check(b.seen() == QLatin1String("mainToolbar / formatBar characterBar / searchBar toolsBar / themesBar / paragraphBar iconBar")
                  && b.consistent(&why) && b.dock->places().bottom.size() == 2 && w.toolBarArea(themes) == Qt::BottomToolBarArea,
              QStringLiteral("the bottom edge takes toolbars side by side and in rows, like the top"), why);
        shot(&w, QStringLiteral("bars-bottom-rows"));
        // a drop on the editor: nothing
        const QString before = b.seen();
        const Places keptBefore = b.dock->places();
        b.press(mainBar, QPoint(w.width() / 2, w.height() / 2));
        const bool none = b.dock->dragging() && b.dock->dragMode() == BarDock::Mode::None && b.dock->markerRect().isEmpty();
        b.release(mainBar, QPoint(w.width() / 2, w.height() / 2));
        check(none && b.seen() == before && b.dock->places() == keptBefore && !b.dock->dragging(),
              QStringLiteral("a drop in the middle of the window is no place: no marker, and nothing changes"), b.seen());
    }

    // ---- what the program keeps, and finds again at its next start
    {
        const Places kept = b.dock->places();
        const QString seen = b.seen();
        const QStringList text = QSettings().value(QStringLiteral("window/barRows")).toStringList();
        // (the settings file itself, read anew from a copy: what the next start of the program will find)
        QSettings().sync();
        QTemporaryDir copyDir;
        const QString copy = copyDir.path() + QStringLiteral("/settings.conf");
        const bool copied = QFile::copy(QSettings().fileName(), copy);
        const QStringList fromFile = QSettings(copy, QSettings::NativeFormat).value(QStringLiteral("window/barRows")).toStringList();
        check(copied && fromText(fromFile) == kept && kept.bottom.size() == 2 && kept.top.size() == 3,
              QStringLiteral("the settings file holds the rows of both edges, and they read back the same from the file"),
              fromFile.join(QLatin1String(" ; ")));
        // (as if another start had shown something else: the toolbars thrown about with the window's own means)
        w.addToolBar(Qt::TopToolBarArea, paragraph);
        w.addToolBar(Qt::BottomToolBarArea, mainBar);
        w.insertToolBar(style, tools);
        settle(200);
        const bool thrown = b.seen() != seen;
        b.dock->load();
        settle(300);
        check(fromText(text) == kept && thrown && b.dock->places() == kept && b.seen() == seen && b.consistent(&why),
              QStringLiteral("the program keeps rows, order and edges (window/barRows) and puts every toolbar there again at its start"),
              why);
        // Reset Toolbar Places
        if (reset)
            reset->trigger();
        settle(300);
        check(b.seen() == standard && b.dock->places() == b.dock->standardPlaces() && b.consistent(&why)
                  && QSettings().value(QStringLiteral("window/barGaps")).toMap().isEmpty() && w.toolBarArea(paragraph) == Qt::TopToolBarArea
                  && w.statusBar()->currentMessage().contains(QLatin1String("standard places")),
              QStringLiteral("View > Toolbars > Reset Toolbar Places: the two standard rows in their order, nothing at the bottom, no free space"),
              why);
    }

    // ---- the step from 0.91.0: rows from the window's state, free space from window/barGaps
    {
        QSettings s;
        s.remove(QStringLiteral("window/barRows"));
        QVariantMap gaps;
        gaps.insert(QStringLiteral("themesBar"), 180); // 0.91.0: free space after Themes = in front of Search
        s.setValue(QStringLiteral("window/barGaps"), gaps);
        w.insertToolBar(themes, character); // what a drag on X11 could do in 0.91.0
        w.addToolBar(Qt::BottomToolBarArea, iconBar);
        settle(200);
        b.dock->load();
        settle(300);
        const Places p = b.dock->places();
        const Where se = find(p, QStringLiteral("searchBar"));
        check(b.seen() == QLatin1String("mainToolbar characterBar themesBar searchBar toolsBar / formatBar paragraphBar / iconBar")
                  && b.consistent(&why) && se.found && p.top[se.row][se.index].lead == 180
                  && b.at(search).x() == b.at(themes).x() + themes->naturalWidth() + 180 && s.contains(QStringLiteral("window/barRows")),
              QStringLiteral("after 0.91.0: the rows, the order and the bottom edge its window state shows are taken over, and its "
                             "free space after a toolbar becomes free space in front of the next one"),
              why + QStringLiteral(" ; ") + describe(p));
        if (reset)
            reset->trigger();
        settle(300);
    }

    // ---- a toolbar that is switched off keeps its place; a narrow window
    {
        QAction *themesSwitch = w.findChild<QAction *>(toolbars::barCommand(toolbars::Bar::Themes));
        b.slide(search, 200); // free space in front of Search
        const int lead = b.at(search).x() - (b.at(themes).x() + themes->naturalWidth());
        if (themesSwitch)
            themesSwitch->trigger();
        settle(300);
        const bool hiddenOk = !themes->isVisible() && b.at(search).x() == b.at(mainBar).x() + mainBar->naturalWidth() + lead;
        b.drag(tools, QPoint(b.at(mainBar).x() + 20, rowOne)); // Tools to the front while Themes is off
        if (themesSwitch)
            themesSwitch->trigger();
        settle(300);
        check(lead >= 190 && lead <= 210 && hiddenOk && b.seen() == QLatin1String("toolsBar mainToolbar themesBar searchBar / formatBar characterBar paragraphBar iconBar")
                  && b.consistent(&why) && b.at(search).x() == b.at(themes).x() + themes->naturalWidth() + lead,
              QStringLiteral("a toolbar that is switched off keeps its place among the others and comes back there; the free "
                             "space in front of the next toolbar stays"),
              why + QStringLiteral(" lead %1").arg(lead));
        // a window too narrow for the free space: it shrinks before a toolbar loses buttons, and comes back
        const QSize was = w.size();
        int need = 0;
        for (GripBar *bar : {tools, mainBar, themes, search})
            need += bar->naturalWidth();
        w.resize(need + 60, was.height());
        settle(400);
        const int narrowLead = b.at(search).x() - (b.at(themes).x() + themes->naturalWidth());
        const bool narrow = narrowLead <= 60 && narrowLead >= 30 && search->width() >= search->naturalWidth()
                            && b.at(search).x() + search->naturalWidth() <= w.width() + 1;
        shot(&w, QStringLiteral("bars-narrow-window"));
        w.resize(was);
        settle(400);
        const int wideLead = b.at(search).x() - (b.at(themes).x() + themes->naturalWidth());
        check(narrow && wideLead == lead,
              QStringLiteral("in a window too narrow for it the free space shrinks and the last toolbar keeps its buttons; in the "
                             "wide window the free space is as it was"),
              QStringLiteral("lead %1, narrow %2 (window %3, needed %4), wide again %5").arg(lead).arg(narrowLead).arg(need + 60).arg(need).arg(wideLead));
        if (reset)
            reset->trigger();
        settle(300);
    }

    // ---- many drags, one after the other: everything stays consistent, and no toolbar is ever stuck
    {
        QRandomGenerator rng(20261113);
        bool ok = true;
        QString firstBad;
        QWidget *central = w.centralWidget();
        int changes = 0;
        for (int n = 0; n < 60 && ok; ++n) {
            QList<GripBar *> visible;
            for (GripBar *bar : std::as_const(b.all))
                if (bar->isVisible())
                    visible << bar;
            GripBar *bar = visible[rng.bounded(int(visible.size()))];
            QPoint to;
            const int kind = rng.bounded(10);
            if (kind < 5) { // somewhere in a row that shows
                GripBar *other = visible[rng.bounded(int(visible.size()))];
                to = QPoint(rng.bounded(w.width()), b.rowY(other));
            } else if (kind < 7) { // along its own row
                to = QPoint(qBound(1, b.at(bar).x() + rng.bounded(-400, 400), w.width() - 2), b.rowY(bar));
            } else if (kind == 7) {
                to = QPoint(rng.bounded(w.width()), central->geometry().top() + 5);
            } else if (kind == 8) {
                to = QPoint(rng.bounded(w.width()), rng.bounded(2) ? central->geometry().bottom() - 5 : w.height() - 5);
            } else {
                to = QPoint(rng.bounded(w.width()), w.height() / 2);
            }
            const Places beforeDrag = b.dock->places();
            b.press(bar, to, 40);
            const bool lifted = bar->isWindow();
            b.release(bar, to);
            changes += b.dock->places() != beforeDrag ? 1 : 0;
            QString state;
            bool fits = true; // no toolbar pushed out of the window by free space
            for (GripBar *v : std::as_const(visible))
                fits = fits && b.at(v).x() >= 0;
            if (lifted || !b.consistent(&state) || b.dock->dragging() || !b.dock->markerRect().isEmpty() || !fits) {
                ok = false;
                firstBad = QStringLiteral("drag %1: %2 to %3,%4: %5").arg(n).arg(bar->objectName()).arg(to.x()).arg(to.y()).arg(state);
            }
        }
        check(ok && changes >= 20, QStringLiteral("60 drags of any toolbar to any place: the places, the window and what shows always "
                                                  "agree, every toolbar is there once, none leaves the window"),
              firstBad + QStringLiteral(" changes %1").arg(changes));
        info(QStringLiteral("after 60 drags (%1 changed something): %2").arg(changes).arg(describe(b.dock->places())));
        shot(&w, QStringLiteral("bars-after-60-drags"));
        // not stuck: every toolbar can still be taken to the front of another row
        bool movable = true;
        QString stuck;
        for (GripBar *bar : std::as_const(b.all)) {
            if (!bar->isVisible())
                continue;
            GripBar *other = nullptr;
            for (GripBar *cand : std::as_const(b.all))
                if (cand->isVisible() && b.at(cand).y() != b.at(bar).y())
                    other = cand;
            if (!other) { // (all in one row: to a row of its own)
                b.drag(bar, QPoint(100, central->geometry().top() + 5));
                movable = movable && b.at(bar).x() == 0;
                continue;
            }
            b.drag(bar, QPoint(2, b.rowY(other)));
            if (b.at(bar).x() != 0 || b.at(bar).y() != b.at(other).y() || !b.consistent()) {
                movable = false;
                stuck += bar->objectName() + QLatin1Char(' ');
            }
        }
        check(movable, QStringLiteral("after all that every toolbar can still be dragged to the front of another row: nothing is stuck"), stuck);
        if (reset)
            reset->trigger();
        settle(300);
        check(b.seen() == standard && b.consistent(&why), QStringLiteral("and Reset Toolbar Places brings the standard places back"), why);
    }
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV0911Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV0911Test"));
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

    ruleChecks();

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

    windowChecks(w);

    w.close();
    delete wp;
    settle(100);
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
