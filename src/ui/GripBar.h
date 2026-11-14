#pragma once
// A toolbar with a grip the writer can see and use (0.91.0, the writer's wish: "a seperator pattern in front of each
// seperate toolbar that the user can grab and reposition", "and add adjustable seperators as well, if the layout is
// something like left-sided"; 0.91.1: "make them so that they can be rearanged freely within the toolbar- and format
// bar rows, also in sequence").
//
// Qt's QToolBar has a grip and a drag of its own. It is not used (0.91.1): on Wayland Qt hands a toolbar's way into
// another row to the desktop's drag and drop (QMainWindowLayout::needsPlatformDrag), and on the writer's desktop
// nothing moved; and along a row Qt shrinks the dragged bar instead of moving the bars after it. So this class keeps
// every press, move and release on the grip to itself and only tells where the mouse is (gripPressed, gripMoved,
// gripReleased, in the bar's own coordinates). BarDock (ui/BarDock.*) decides what that means: a new place in the
// row, in another row or at the other edge. Nothing here needs the screen's coordinates or a window of its own, so
// it works the same on X11, Wayland and Windows.
//
// Free space between two bars (the writer's adjustable separators) is a spacer after the last button of the bar on
// the left: its gap. BarDock sets it.
#include <QToolBar>

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
    // what the bar needs for its grip and buttons, without the gap
    int naturalWidth() const;
    // free space of this many pixels or less cannot show (the layout's own space between two items)
    int smallestGap() const;
    // where the grip is (the style's place for it); empty for a bar that cannot be moved
    QRect gripRect() const;
    // true while the left button is down on the grip
    bool gripDown() const { return m_onGrip; }

signals:
    // the left button on the grip (positions in the bar's coordinates, as the bar stands at that moment)
    void gripPressed(const QPoint &pos);
    void gripMoved(const QPoint &pos);
    void gripReleased(const QPoint &pos);
    // buttons were added or taken away: the bar needs another width
    void contentsChanged();

protected:
    bool event(QEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void actionEvent(QActionEvent *e) override;

private:
    void keepGapLast();

    QWidget *m_gapWidget = nullptr;
    QWidgetAction *m_gapAction = nullptr;
    int m_gap = 0;
    bool m_onGrip = false; // the left button went down on the grip
    bool m_fixing = false, m_queued = false;
};

} // namespace qb
