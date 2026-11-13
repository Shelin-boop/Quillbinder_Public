#pragma once
// A toolbar with a grip the writer can see and use (0.91.0, the writer's wish: "a seperator pattern in front of each
// seperate toolbar that the user can grab and reposition", "and add adjustable seperators as well, if the layout is
// something like left-sided").
//
// Qt's QToolBar has a grip already: dragged out of its row, the bar goes to another row or edge (QMainWindow does
// that, and this class leaves it to Qt). Dragged along its row, Qt changes the border between the bar and the one
// before it: the bar itself shrinks by what it moves, and its buttons go into the overflow menu while the bars after
// it stay where they are. A writer who pulls a bar to the right expects the bars after it to make room. So the
// sideways part is done here: every bar can have free space after its last button (its gap), and a bar dragged
// sideways by its grip changes the gap of the bar before it. The bars after it move along, because a row is laid out
// from the left, and the row's last bar, which Qt stretches to the window's edge, gives or takes the room.
//
// The owner (MainWindow) says which bar stands before another and keeps the gaps (window/barGaps).
#include <QToolBar>
#include <functional>

class QWidgetAction;

namespace qb {

class GripBar : public QToolBar
{
    Q_OBJECT
public:
    explicit GripBar(const QString &title, QWidget *parent = nullptr);

    // free space after the bar's last button, in pixels
    int gap() const { return m_gap; }
    void setGap(int px);
    // where the grip is (the style's place for it); empty for a bar that cannot be moved
    QRect gripRect() const;
    // Called while the grip is dragged sideways: dx pixels to the right (negative: to the left). Returns how far the
    // bar could go.
    void setMover(std::function<int(GripBar *, int)> mover) { m_mover = std::move(mover); }
    // Called when a sideways drag ends.
    void setDropped(std::function<void()> dropped) { m_dropped = std::move(dropped); }

protected:
    bool event(QEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void actionEvent(QActionEvent *e) override;

private:
    void keepGapLast();

    QWidget *m_gapWidget = nullptr;
    QWidgetAction *m_gapAction = nullptr;
    std::function<int(GripBar *, int)> m_mover;
    std::function<void()> m_dropped;
    int m_gap = 0;
    int m_lastX = 0;        // the mouse's place (screen) the bar has followed up to
    bool m_onGrip = false;  // the left button went down on the grip
    bool m_sideways = false;
    bool m_qtDrag = false;  // the mouse left the row: Qt moves the bar
    bool m_fixing = false, m_queued = false;
};

} // namespace qb
