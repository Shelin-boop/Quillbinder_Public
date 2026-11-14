#pragma once
// The toolbars' places in the window, and the drag that changes them (0.91.1). The writer's report of 2026-11-13 on
// 0.91.0 (Kubuntu, Wayland): the toolbars could not change their order, a toolbar could not go from one row into the
// other ("Nothing moves"), and with the Search toolbar pulled to the far right the row "got stuck" (no toolbar to the
// left of it could be moved: 0.91.0 took room only from the row's last toolbar, never from the free space after the
// dragged one).
//
// The rules are in ui/BarPlaces.* (no widgets). This class keeps the places (settings: window/barRows), puts the
// toolbars into the QMainWindow in that order, shows the free space between them (GripBar::setGap) and turns a drag
// of a grip into a new place. Qt's own toolbar drag is not used at all (see GripBar.h).
//
// A drag by the grip has two kinds of result, decided by where the mouse is:
//  - Shift: the mouse is in the toolbar's own row and between the middles of its two neighbors. The toolbar follows
//    the mouse along the row at once; free space after it is used up first, then the toolbars after it are pushed, as
//    far as they can stand together at the row's end.
//  - Place: the mouse is past a neighbor's middle, in another row, just under the top rows or at the bottom edge. The
//    toolbar stays where it was and a marker (a line in the highlight color) shows where it will stand; it moves when
//    the button is released. Nothing in the window's layout changes while the button is down, so the drag does not
//    depend on how a desktop treats windows that change under a pressed mouse.
// Esc ends a drag without a change.
#include "ui/BarPlaces.h"

#include <QList>
#include <QObject>
#include <QPoint>
#include <QRect>

class QMainWindow;
class QTimer;
class QWidget;

namespace qb {

class GripBar;

class BarDock : public QObject
{
    Q_OBJECT
public:
    // the window's grip bars and their standard places: rows of object names at the top edge
    BarDock(QMainWindow *window, const QList<GripBar *> &bars, const QList<QStringList> &standardTopRows);

    // At the program's start, after the window's state was restored: the places the settings keep. A program that has
    // none yet (it ran 0.91.0 before) takes the rows the window shows and the free space 0.91.0 kept (window/barGaps).
    void load();
    // the standard places, without free space
    void reset();
    barplaces::Places places() const { return m_places; }
    barplaces::Places standardPlaces() const { return m_standard; }
    void setPlaces(const barplaces::Places &p);
    // the rows as the QMainWindow has them (what load() starts from after 0.91.0; the tests compare it with places())
    barplaces::Places placesOfWindow() const;
    // The free space as it fits the window now. Runs by itself after a resize, when a toolbar is switched on or off or
    // gets other buttons.
    void refit();

    enum class Mode { None, Shift, Place };
    bool dragging() const { return m_drag.started; }
    Mode dragMode() const { return m_drag.started ? m_drag.mode : Mode::None; }
    // where the marker shows (the window's coordinates); empty while there is none
    QRect markerRect() const;
    void cancelDrag();

signals:
    void placesChanged();

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    struct BandBar
    {
        GripBar *bar = nullptr;
        int x = 0, natural = 0; // left edge in the window and the width its buttons need, as the drag began
    };
    struct Band // one row as it shows
    {
        bool bottom = false;
        int row = 0; // its number in m_places
        QRect rect;  // in the window, over the window's whole width
        QList<BandBar> bars;
    };
    struct Drag
    {
        GripBar *bar = nullptr;
        bool started = false;
        QPoint press; // in the window
        int grab = 0; // from the bar's left edge to the mouse
        Mode mode = Mode::Shift;
        QList<Band> bands;
        int own = -1;   // the bar's band
        int index = -1; // its number among the band's bars
        QList<barplaces::Shown> shown;
        QList<int> startLeads, leads;
        barplaces::Target target;
        QRect marker;
    };

    GripBar *barNamed(const QString &name) const;
    QSet<QString> hiddenBars() const;
    int smallestLead() const;
    void arrange();
    void save();
    void scheduleRefit();
    QList<Band> bandsNow() const;
    void pressed(GripBar *bar, const QPoint &pos);
    void movedTo(GripBar *bar, const QPoint &pos);
    void released(GripBar *bar, const QPoint &pos);
    void beginDrag();
    void follow(const QPoint &p);
    void endDrag();
    void showLeads(const QList<int> &leads);
    barplaces::Target targetAt(const QPoint &p, QRect *marker) const;

    QMainWindow *m_window;
    QList<GripBar *> m_bars;
    barplaces::Places m_standard, m_places;
    QWidget *m_marker;
    QTimer *m_refitTimer;
    Drag m_drag;
    bool m_cursorSet = false, m_arranging = false, m_hinted = false;
};

} // namespace qb
