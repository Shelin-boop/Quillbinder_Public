#pragma once
// Where the toolbars stand (0.91.1, the writer's request of 2026-11-13: "make them so that they can be rearanged freely
// within the toolbar- and format bar rows, also in sequence"). The arithmetic only; no widgets here, so the rules can
// be checked one by one (tests/v0911_tests.cpp). BarDock (ui/BarDock.*) applies them to the window.
//
// The places are rows of toolbars at the window's top edge and at its bottom edge, each row in its order from the
// left. A toolbar can have free space in front of it (its lead; the writer's "adjustable separators" of 0.91.0). A
// row's first toolbar has none: it starts at the window's edge.
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace qb {
namespace barplaces {

struct Slot
{
    QString name; // the toolbar's object name
    int lead = 0; // free space in front of it, in pixels
    bool operator==(const Slot &o) const { return name == o.name && lead == o.lead; }
};
using Row = QList<Slot>;

struct Places
{
    QList<Row> top, bottom; // the rows as they show, from the top down
    bool operator==(const Places &o) const { return top == o.top && bottom == o.bottom; }
    bool operator!=(const Places &o) const { return !(*this == o); }
};

constexpr int kMaxLead = 4000;

// Rows of names without free space (the standard places are given this way).
Places fromRows(const QList<QStringList> &topRows, const QList<QStringList> &bottomRows = {});
// One line per row: "top|mainToolbar:0 themesBar:0 searchBar:120" (what the settings keep).
QStringList toText(const Places &p);
Places fromText(const QStringList &lines);
// The same for a person: "top: mainToolbar themesBar +120 searchBar / formatBar; bottom: paragraphBar".
QString describe(const Places &p);

// Whatever the text said: every toolbar of the standard places stands exactly once, none that is unknown, no empty
// row, leads between 0 and kMaxLead and none for a row's first toolbar. A toolbar the places do not name (one a newer
// version added) goes behind the toolbar that stands before it in the standard places.
Places normalized(const Places &p, const Places &standard);

struct Where
{
    bool found = false;
    bool bottom = false;
    int row = -1, index = -1;
};
Where find(const Places &p, const QString &name);

// Where a toolbar is put: into the row `row` of the top or bottom edge, in front of the toolbar `before` (at the row's
// end when that is empty or not in the row); or, with newRow, into a row of its own that becomes row number `row`.
struct Target
{
    bool valid = false;
    bool bottom = false;
    int row = 0;
    bool newRow = false;
    QString before;
};
// The toolbar `name` taken out of its place and put where the target says. The toolbars after its old place close up
// by its width, and free space that was in front of it stays there (the next toolbar that shows takes it over: bars in
// `hidden` do not show). At its new place it stands straight after the toolbar before it. Rows are counted as in `p`.
Places moved(const Places &p, const QString &name, const Target &to, const QSet<QString> &hidden = {});

// ---- one row as it shows: the toolbars that are visible, in order
struct Shown
{
    QString name;
    int width = 0; // what the toolbar needs for its buttons
    int lead = 0;
};

// The free space each toolbar can have in a row that is `rowWidth` wide: none for the first; when the toolbars and
// their free space need more than the row has, the free space shrinks in proportion (the toolbars keep their buttons
// as long as they can). Free space of `minLead` pixels or less is none (a toolbar cannot show so little).
QList<int> fitted(const QList<Shown> &row, int rowWidth, int minLead = 0);
// The left edges that follow from the leads (the first toolbar at 0).
QList<int> lefts(const QList<Shown> &row);

// The toolbar number `index` of the row is dragged along the row so that its left edge comes to `left` (0 = the row's
// start). It goes left as far as the toolbar before it ends, and right as far as the toolbars after it can be pushed
// together at the row's end. The others stay where they are until it touches them: free space after it is used up
// first, then they are pushed. The row's first toolbar stays. Returns the new leads of all toolbars of the row.
QList<int> shifted(const QList<Shown> &row, int rowWidth, int index, int left, int minLead = 0);

} // namespace barplaces
} // namespace qb
