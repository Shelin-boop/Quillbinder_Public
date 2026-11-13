#include "ui/GripBar.h"

#include <QActionEvent>
#include <QApplication>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionToolBar>
#include <QTimer>
#include <QWidgetAction>

namespace qb {

GripBar::GripBar(const QString &title, QWidget *parent) : QToolBar(title, parent)
{
    m_gapWidget = new QWidget;
    m_gapWidget->setObjectName(QStringLiteral("barGap"));
    m_gapWidget->setFixedWidth(0);
    m_gapWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_gapAction = new QWidgetAction(this);
    m_gapAction->setDefaultWidget(m_gapWidget);
    setAttribute(Qt::WA_Hover); // the pointer over the grip shows that the bar can be moved
}

QRect GripBar::gripRect() const
{
    if (!isMovable())
        return QRect();
    QStyleOptionToolBar opt;
    initStyleOption(&opt);
    return style()->subElementRect(QStyle::SE_ToolBarHandle, &opt, this);
}

void GripBar::setGap(int px)
{
    // The gap is a widget after the last button, and the bar's layout puts its usual space between the two: the
    // widget is that much narrower, and a gap no wider than that space is no gap.
    const int spacing = qMax(0, style()->pixelMetric(QStyle::PM_ToolBarItemSpacing, nullptr, this));
    px = px <= spacing ? 0 : px;
    if (px == m_gap)
        return;
    m_gap = px;
    m_gapWidget->setFixedWidth(qMax(0, px - spacing));
    keepGapLast();
}

// The gap is a widget of the bar after its last button, and on the bar only while there is a gap (a bar's list of
// actions is its buttons otherwise).
void GripBar::keepGapLast()
{
    m_queued = false;
    const QList<QAction *> all = actions();
    const bool there = all.contains(m_gapAction);
    if (m_gap <= 0 || all.size() == (there ? 1 : 0)) { // no gap, or nothing on the bar to stand after
        if (there) {
            m_fixing = true;
            removeAction(m_gapAction);
            m_fixing = false;
        }
        return;
    }
    if (there && all.last() == m_gapAction)
        return;
    m_fixing = true;
    if (there)
        removeAction(m_gapAction);
    addAction(m_gapAction);
    m_fixing = false;
}

void GripBar::actionEvent(QActionEvent *e)
{
    QToolBar::actionEvent(e);
    // a button added or the bar filled anew: the gap goes behind the last button again
    if (!m_fixing && e->action() != m_gapAction && m_gap > 0 && !m_queued
        && (e->type() == QEvent::ActionAdded || e->type() == QEvent::ActionRemoved)) {
        m_queued = true;
        QTimer::singleShot(0, this, &GripBar::keepGapLast);
    }
}

bool GripBar::event(QEvent *e)
{
    switch (e->type()) {
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(e);
        m_sideways = false;
        m_qtDrag = false;
        m_onGrip = me->button() == Qt::LeftButton && gripRect().contains(me->position().toPoint());
        m_lastX = me->globalPosition().toPoint().x();
        break; // Qt notes the press too: it moves the bar once the mouse leaves the row
    }
    case QEvent::MouseMove: {
        auto *me = static_cast<QMouseEvent *>(e);
        if (!m_onGrip || m_qtDrag)
            break;
        const QPoint at = me->position().toPoint();
        if (at.y() < 0 || at.y() >= height()) {
            m_qtDrag = true; // out of the row: another row or the other edge, which is Qt's part
            break;
        }
        // in the row: the bar follows the mouse sideways (Qt would shrink this bar instead, see GripBar.h)
        const int x = me->globalPosition().toPoint().x();
        if (!m_sideways && qAbs(x - m_lastX) < QApplication::startDragDistance())
            return true;
        m_sideways = true;
        if (m_mover)
            m_lastX += m_mover(this, x - m_lastX);
        return true;
    }
    case QEvent::MouseButtonRelease:
        if (m_onGrip) {
            const bool moved = m_sideways && !m_qtDrag;
            m_onGrip = false;
            m_sideways = false;
            if (moved && m_dropped)
                m_dropped();
        }
        break;
    case QEvent::HoverMove: {
        const bool grip = gripRect().contains(static_cast<QHoverEvent *>(e)->position().toPoint());
        if (grip && cursor().shape() != Qt::SizeAllCursor)
            setCursor(Qt::SizeAllCursor);
        else if (!grip && testAttribute(Qt::WA_SetCursor))
            unsetCursor();
        break;
    }
    case QEvent::HoverLeave:
        if (testAttribute(Qt::WA_SetCursor))
            unsetCursor();
        break;
    default:
        break;
    }
    return QToolBar::event(e);
}

// The grip as a pattern that shows in every theme: two columns of dots over the style's own (in Fusion, faint) marks.
void GripBar::paintEvent(QPaintEvent *e)
{
    QToolBar::paintEvent(e);
    const QRect r = gripRect();
    if (!r.isValid() || r.height() < 12)
        return;
    QPainter p(this);
    QColor dot = palette().color(QPalette::WindowText);
    dot.setAlpha(150);
    const int x = r.center().x() - 2;
    for (int y = r.top() + 6; y + 2 <= r.bottom() - 5; y += 4) {
        p.fillRect(QRect(x, y, 2, 2), dot);
        p.fillRect(QRect(x + 4, y, 2, 2), dot);
    }
}

} // namespace qb
