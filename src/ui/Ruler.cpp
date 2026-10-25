#include "ui/Ruler.h"

#include "ui/AppSettings.h"
#include "ui/TextEditor.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTextDocument>
#include <QToolTip>
#include <cmath>

namespace qb {

namespace {
const int kHeight = 24;
const int kGrab = 6;
}

Ruler::Ruler(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("ruler"));
    setFixedHeight(kHeight);
    setMouseTracking(true);
    setToolTip(tr("Drag the markers to change indents. Click to add a tab stop, drag a tab stop away to remove it. "
                  "Double-click for all paragraph settings."));
}

QSize Ruler::sizeHint() const { return QSize(200, kHeight); }

void Ruler::setEditor(TextEditor *editor)
{
    if (m_editor == editor)
        return;
    if (m_editor) {
        disconnect(m_editor, nullptr, this, nullptr);
        m_editor->viewport()->removeEventFilter(this);
    }
    m_editor = editor;
    if (m_editor) {
        connect(m_editor, &QTextEdit::cursorPositionChanged, this, qOverload<>(&QWidget::update));
        connect(m_editor, &QTextEdit::textChanged, this, qOverload<>(&QWidget::update));
        m_editor->viewport()->installEventFilter(this); // follows resizes and moves of the column
    }
    update();
}

bool Ruler::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() == QEvent::Resize || e->type() == QEvent::Move || e->type() == QEvent::Show)
        update();
    return QWidget::eventFilter(o, e);
}

direct::ParaSpec Ruler::spec() const
{
    return m_editor ? m_editor->paraSpecAtCursor() : direct::ParaSpec();
}

int Ruler::originX() const
{
    if (!m_editor)
        return 0;
    const QPoint p = m_editor->viewport()->mapTo(window(), QPoint(0, 0)) - mapTo(window(), QPoint(0, 0));
    return p.x() + m_editor->textInsetLeft();
}

int Ruler::textWidth() const
{
    if (!m_editor)
        return 0;
    return m_editor->viewport()->width() - m_editor->textInsetLeft() - m_editor->textInsetRight();
}

int Ruler::markerX(Marker m, int tabIndex) const
{
    const direct::ParaSpec s = spec();
    const int x0 = originX();
    switch (m) {
    case LeftIndent: return x0 + qRound(s.left);
    case FirstLine: return x0 + qRound(s.left + s.first);
    case RightIndent: return x0 + textWidth() - qRound(s.right);
    case Tab:
        if (tabIndex >= 0 && tabIndex < s.tabs.size())
            return x0 + qRound(s.left + s.first + s.tabs[tabIndex].position);
        break;
    default: break;
    }
    return x0;
}

Ruler::Marker Ruler::hit(const QPoint &p, int *tabIndex) const
{
    if (tabIndex)
        *tabIndex = -1;
    if (!m_editor)
        return None;
    const direct::ParaSpec s = spec();
    const bool upper = p.y() < height() / 2;
    // tab stops sit in the lower half
    if (!upper) {
        for (int i = 0; i < s.tabs.size(); ++i)
            if (qAbs(p.x() - markerX(Tab, i)) <= kGrab) {
                if (tabIndex)
                    *tabIndex = i;
                return Tab;
            }
    }
    const int fx = markerX(FirstLine), lx = markerX(LeftIndent), rx = markerX(RightIndent);
    if (qAbs(p.x() - rx) <= kGrab)
        return RightIndent;
    if (upper && qAbs(p.x() - fx) <= kGrab)
        return FirstLine;
    if (!upper && qAbs(p.x() - lx) <= kGrab)
        return LeftIndent;
    // the two left markers overlap when the first line is not indented
    if (qAbs(p.x() - fx) <= kGrab)
        return FirstLine;
    if (qAbs(p.x() - lx) <= kGrab)
        return LeftIndent;
    return None;
}

double Ruler::snap(double px, bool free) const
{
    if (free)
        return px;
    const double st = units::step();
    return std::round(px / st) * st;
}

void Ruler::apply(const direct::ParaSpec &s)
{
    if (m_editor)
        m_editor->applyParaSpec(s);
    update();
}

void Ruler::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QPalette pal = palette();
    p.fillRect(rect(), pal.color(QPalette::Window));
    if (!m_editor) {
        return;
    }
    const int x0 = originX(), w = textWidth();
    const QRect band(x0, 4, w, height() - 9);
    p.fillRect(band, pal.color(QPalette::Base));
    QColor line = pal.color(QPalette::WindowText);
    line.setAlpha(110);
    p.setPen(line);
    p.drawRect(band.adjusted(0, 0, -1, -1));

    // scale: a number at every unit, ticks at halves and quarters
    QFont f = font();
    f.setPointSizeF(qMax(6.0, f.pointSizeF() * 0.75));
    p.setFont(f);
    const double unit = units::toPx(1.0);
    const int subdivisions = units::metric() ? 4 : 8;
    const double st = unit / subdivisions;
    const int cy = band.center().y();
    QColor num = pal.color(QPalette::WindowText);
    num.setAlpha(170);
    for (int i = 1; x0 + i * st < x0 + w - 1; ++i) {
        const int x = x0 + qRound(i * st);
        if (i % subdivisions == 0) {
            p.setPen(num);
            const QString label = QString::number(i / subdivisions);
            const int tw = p.fontMetrics().horizontalAdvance(label);
            p.drawText(QRect(x - tw / 2 - 2, band.top(), tw + 4, band.height()), Qt::AlignCenter, label);
        } else {
            p.setPen(line);
            const int h = (i * 2 % subdivisions == 0) ? 3 : 1;
            p.drawLine(x, cy - h, x, cy + h);
        }
    }

    direct::ParaSpec s = spec();
    // while dragging show where the marker will land
    double left = s.left, first = s.first, right = s.right;
    QList<QTextOption::Tab> tabs = s.tabs;
    if (m_drag == LeftIndent)
        left = m_dragValue;
    else if (m_drag == FirstLine)
        first = m_dragValue - left;
    else if (m_drag == RightIndent)
        right = m_dragValue;
    else if (m_drag == Tab && m_dragTab >= 0 && m_dragTab < tabs.size())
        tabs[m_dragTab].position = m_dragValue;

    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor marker = pal.color(QPalette::Highlight);
    p.setPen(Qt::NoPen);
    p.setBrush(marker);
    auto triangle = [&](double x, bool down) {
        QPainterPath path;
        if (down) {
            path.moveTo(x - 5, 0.5);
            path.lineTo(x + 5, 0.5);
            path.lineTo(x, 8.5);
        } else {
            path.moveTo(x - 5, height() - 0.5);
            path.lineTo(x + 5, height() - 0.5);
            path.lineTo(x, height() - 8.5);
        }
        path.closeSubpath();
        p.drawPath(path);
    };
    triangle(x0 + left + first, true);       // first line
    triangle(x0 + left, false);              // left indent
    triangle(x0 + w - right, false);         // right indent

    // tab stops
    QPen tp(pal.color(QPalette::WindowText), 2);
    tp.setCapStyle(Qt::FlatCap);
    for (int i = 0; i < tabs.size(); ++i) {
        const bool removing = m_drag == Tab && i == m_dragTab && m_dragRemove;
        QColor c = pal.color(QPalette::WindowText);
        if (removing)
            c.setAlpha(60);
        tp.setColor(c);
        p.setPen(tp);
        const double x = x0 + left + first + tabs[i].position;
        const double top = height() - 11, bottom = height() - 3;
        p.drawLine(QPointF(x, top), QPointF(x, bottom));
        switch (tabs[i].type) {
        case QTextOption::RightTab:
            p.drawLine(QPointF(x - 5, bottom), QPointF(x + 1, bottom));
            break;
        case QTextOption::CenterTab:
            p.drawLine(QPointF(x - 4, bottom), QPointF(x + 4, bottom));
            break;
        case QTextOption::DelimiterTab:
            p.drawLine(QPointF(x - 4, bottom), QPointF(x + 4, bottom));
            p.setBrush(c);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(x + 4, top + 2), 1.5, 1.5);
            break;
        default:
            p.drawLine(QPointF(x - 1, bottom), QPointF(x + 5, bottom));
            break;
        }
    }
}

void Ruler::mousePressEvent(QMouseEvent *e)
{
    if (!m_editor || e->button() != Qt::LeftButton)
        return;
    m_pressPos = e->pos();
    m_moved = false;
    m_dragRemove = false;
    const direct::ParaSpec s = spec();
    m_drag = hit(e->pos(), &m_dragTab);
    switch (m_drag) {
    case LeftIndent: m_dragValue = s.left; break;
    case FirstLine: m_dragValue = s.left + s.first; break;
    case RightIndent: m_dragValue = s.right; break;
    case Tab: m_dragValue = s.tabs.value(m_dragTab).position; break;
    default: break;
    }
}

void Ruler::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_editor)
        return;
    if (m_drag == None) {
        setCursor(hit(e->pos(), nullptr) != None ? Qt::SizeHorCursor : Qt::ArrowCursor);
        return;
    }
    if (!m_moved && (e->pos() - m_pressPos).manhattanLength() < 3)
        return;
    m_moved = true;
    const direct::ParaSpec s = spec();
    const bool free = e->modifiers() & Qt::ShiftModifier;
    const int x0 = originX(), w = textWidth();
    const double x = e->pos().x() - x0;
    switch (m_drag) {
    case LeftIndent:
        m_dragValue = qBound(0.0, snap(x, free), double(w) - s.right - 40);
        break;
    case FirstLine:
        m_dragValue = qBound(0.0, snap(x, free), double(w) - s.right - 40);
        break;
    case RightIndent:
        m_dragValue = qBound(0.0, snap(w - x, free), double(w) - s.left - 40);
        break;
    case Tab:
        m_dragValue = qBound(units::step() / 2, snap(x - s.left - s.first, free), double(w));
        m_dragRemove = e->pos().y() < -12 || e->pos().y() > height() + 12;
        break;
    default: break;
    }
    const double shown = m_drag == FirstLine ? m_dragValue - s.left : m_dragValue;
    QToolTip::showText(e->globalPosition().toPoint() + QPoint(0, 18),
                       m_dragRemove ? tr("Remove tab stop") : units::format(shown), this);
    update();
}

void Ruler::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_editor || e->button() != Qt::LeftButton)
        return;
    const Marker drag = m_drag;
    const int tab = m_dragTab;
    m_drag = None;
    m_dragTab = -1;
    QToolTip::hideText();
    direct::ParaSpec s = spec();
    direct::ParaSpec out;
    if (drag == None) {
        // a click on a free place in the lower half adds a left tab stop
        const int x0 = originX();
        if (e->pos().y() >= height() / 2 && e->pos().x() > x0 && e->pos().x() < x0 + textWidth()
            && (e->pos() - m_pressPos).manhattanLength() < 4) {
            const double pos = snap(e->pos().x() - x0 - s.left - s.first, e->modifiers() & Qt::ShiftModifier);
            if (pos > 1) {
                out.bits = direct::Tabs;
                out.tabs = s.tabs;
                for (int i = out.tabs.size() - 1; i >= 0; --i)
                    if (qAbs(out.tabs[i].position - pos) < 1.0)
                        out.tabs.removeAt(i);
                out.tabs.append(QTextOption::Tab(pos, QTextOption::LeftTab));
                apply(out);
            }
        }
        return;
    }
    if (!m_moved) {
        update();
        return;
    }
    switch (drag) {
    case LeftIndent:
        // the first line keeps its distance to the left indent
        out.bits = direct::Left;
        out.left = m_dragValue;
        break;
    case FirstLine:
        out.bits = direct::First;
        out.first = m_dragValue - s.left;
        break;
    case RightIndent:
        out.bits = direct::Right;
        out.right = m_dragValue;
        break;
    case Tab:
        out.bits = direct::Tabs;
        out.tabs = s.tabs;
        if (tab >= 0 && tab < out.tabs.size()) {
            if (m_dragRemove)
                out.tabs.removeAt(tab);
            else
                out.tabs[tab].position = m_dragValue;
        }
        break;
    default: break;
    }
    m_dragRemove = false;
    apply(out);
    m_editor->setFocus();
}

void Ruler::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (!m_editor || e->button() != Qt::LeftButton)
        return;
    m_drag = None;
    emit paragraphDialogRequested();
}

void Ruler::contextMenuEvent(QContextMenuEvent *e)
{
    if (!m_editor)
        return;
    int tab = -1;
    const Marker m = hit(e->pos(), &tab);
    const direct::ParaSpec s = spec();
    QMenu menu(this);
    if (m == Tab && tab >= 0) {
        const struct {
            QTextOption::TabType type;
            QString name;
        } types[] = {{QTextOption::LeftTab, tr("Left Tab Stop")},
                     {QTextOption::CenterTab, tr("Centered Tab Stop")},
                     {QTextOption::RightTab, tr("Right Tab Stop")},
                     {QTextOption::DelimiterTab, tr("Decimal Point Tab Stop")}};
        for (const auto &t : types) {
            QAction *ac = menu.addAction(t.name);
            ac->setCheckable(true);
            ac->setChecked(s.tabs[tab].type == t.type);
            connect(ac, &QAction::triggered, this, [this, s, tab, t] {
                direct::ParaSpec out;
                out.bits = direct::Tabs;
                out.tabs = s.tabs;
                out.tabs[tab] = QTextOption::Tab(s.tabs[tab].position, t.type,
                                                 t.type == QTextOption::DelimiterTab ? QLatin1Char('.') : QChar());
                apply(out);
            });
        }
        menu.addSeparator();
        connect(menu.addAction(tr("Remove Tab Stop")), &QAction::triggered, this, [this, s, tab] {
            direct::ParaSpec out;
            out.bits = direct::Tabs;
            out.tabs = s.tabs;
            out.tabs.removeAt(tab);
            apply(out);
        });
        menu.addSeparator();
    }
    if (!s.tabs.isEmpty())
        connect(menu.addAction(tr("Remove All Tab Stops")), &QAction::triggered, this, [this] {
            direct::ParaSpec out;
            out.bits = direct::Tabs;
            apply(out);
        });
    connect(menu.addAction(tr("Paragraph…")), &QAction::triggered, this, &Ruler::paragraphDialogRequested);
    menu.addSeparator();
    QAction *cm = menu.addAction(tr("Centimeters"));
    cm->setCheckable(true);
    cm->setChecked(units::metric());
    QAction *in = menu.addAction(tr("Inches"));
    in->setCheckable(true);
    in->setChecked(!units::metric());
    connect(cm, &QAction::triggered, this, [] {
        AppSettings::instance().rulerUnit = QStringLiteral("cm");
        AppSettings::instance().notifyChanged();
    });
    connect(in, &QAction::triggered, this, [] {
        AppSettings::instance().rulerUnit = QStringLiteral("in");
        AppSettings::instance().notifyChanged();
    });
    menu.exec(e->globalPos());
}

} // namespace qb
