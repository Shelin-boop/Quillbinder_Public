#include "ui/ColorPickButton.h"
#include "core/ItemIcons.h"
#include "ui/ColorPalette.h"

#include <QColorDialog>
#include <QCursor>
#include <QEnterEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QScreen>

namespace qb {

QList<QPair<QString, QColor>> ColorPickButton::palette()
{
    // the six fixed colors of the shared palette (ColorPalette.h), in their order of 0.17
    QList<QPair<QString, QColor>> out;
    for (const palette::Entry &e : palette::standard())
        out.append({e.name, e.color});
    return out;
}

ColorPickButton::ColorPickButton(Kind kind, QWidget *parent) : QToolButton(parent), m_kind(kind)
{
    setObjectName(kind == TextColor ? QStringLiteral("textColorButton") : QStringLiteral("highlightButton"));
    setAutoRaise(true);
    setFocusPolicy(Qt::NoFocus);
    setToolTip(kind == TextColor ? tr("Text color: a click colors the selected text (or what you type next); "
                                      "rest the mouse here for other colors")
                                 : tr("Highlight: a click highlights the selected text (or what you type next); "
                                      "rest the mouse here for other colors"));
    m_color = kind == TextColor ? QColor(214, 39, 40) : QColor(255, 221, 0);
    m_showTimer.setSingleShot(true);
    m_showTimer.setInterval(350);
    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(450);
    connect(&m_showTimer, &QTimer::timeout, this, &ColorPickButton::showPopup);
    connect(&m_hideTimer, &QTimer::timeout, this, [this] {
        // gone only when the mouse is neither on the button nor on the colors
        const QPoint g = QCursor::pos();
        if (rect().contains(mapFromGlobal(g)) || (m_popup && m_popup->isVisible() && m_popup->geometry().contains(g)))
            return;
        hidePopup();
    });
    connect(this, &QToolButton::clicked, this, [this] {
        hidePopup();
        emit applyColor(m_color);
    });
    refreshIcon();
}

void ColorPickButton::setColor(const QColor &c)
{
    if (c == m_color)
        return;
    m_color = c;
    refreshIcon();
    emit colorChanged(c);
}

void ColorPickButton::refreshIcon()
{
    // the symbol with a bar of the color below it (several sizes, the bar keeps its share)
    const QIcon base = icons::action(m_kind == TextColor ? QStringLiteral("format-text-color") : QStringLiteral("draw-highlight"));
    QIcon icon;
    for (int s : {16, 18, 22, 24, 32}) {
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const int bar = qMax(3, s / 5);
        base.paint(&p, QRect(0, 0, s, s - bar), Qt::AlignCenter);
        const QRectF br(1, s - bar, s - 2, bar - 0.5);
        p.setPen(QPen(QColor(90, 90, 90), 0.8));
        p.setBrush(m_color.isValid() ? QBrush(m_color) : QBrush(Qt::NoBrush));
        p.drawRect(br);
        if (!m_color.isValid()) {
            p.setPen(QPen(QColor(200, 40, 40), 1));
            p.drawLine(br.bottomLeft(), br.topRight());
        }
        p.end();
        icon.addPixmap(pm);
    }
    setIcon(icon);
}

void ColorPickButton::enterEvent(QEnterEvent *e)
{
    QToolButton::enterEvent(e);
    m_hideTimer.stop();
    if (isEnabled())
        m_showTimer.start();
}

void ColorPickButton::leaveEvent(QEvent *e)
{
    QToolButton::leaveEvent(e);
    m_showTimer.stop();
    m_hideTimer.start();
}

void ColorPickButton::showPopup()
{
    if (!isEnabled() || !isVisible())
        return;
    // made anew each time it opens: the label colors of the project may have changed (0.21)
    if (m_popup && !m_popup->isVisible()) {
        m_popup->deleteLater();
        m_popup = nullptr;
    }
    if (!m_popup) {
        m_popup = new ColorPickPopup(this);
        connect(m_popup, &ColorPickPopup::chosen, this, [this](const QColor &c) {
            setColor(c);
            hidePopup();
            emit applyColor(c);
        });
        connect(m_popup, &ColorPickPopup::otherRequested, this, [this] {
            hidePopup();
            const QColor c = QColorDialog::getColor(m_color.isValid() ? m_color : Qt::black, window(),
                                                    m_kind == TextColor ? tr("Text Color") : tr("Highlight Color"));
            if (!c.isValid())
                return;
            setColor(c);
            emit applyColor(c);
        });
    }
    m_popup->adjustSize();
    QPoint at = mapToGlobal(QPoint(0, height()));
    if (QScreen *scr = screen()) {
        const QRect avail = scr->availableGeometry();
        at.setX(qMin(at.x(), avail.right() - m_popup->width()));
    }
    m_popup->move(at);
    m_popup->show();
    m_popup->raise();
}

void ColorPickButton::hidePopup()
{
    m_showTimer.stop();
    if (m_popup)
        m_popup->hide();
}

// ------------------------------------------------------------------ popup

ColorPickPopup::ColorPickPopup(ColorPickButton *owner)
    // a tool tip window: it takes no keyboard focus, so the selection in the text stays
    : QFrame(owner, Qt::ToolTip | Qt::FramelessWindowHint), m_owner(owner)
{
    setObjectName(owner->kind() == ColorPickButton::TextColor ? QStringLiteral("textColorPopup") : QStringLiteral("highlightPopup"));
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);
    // Row 1: the six fixed colors, "Automatic" / "No Highlight" and "...";
    // row 2 (0.21, the shared palette): the colors of the project's labels.
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setSpacing(3);
    auto *lay = new QHBoxLayout;
    lay->setSpacing(3);
    grid->addLayout(lay, 0, 0);
    auto swatch = [&](const QString &name, const QColor &c, QHBoxLayout *row) {
        auto *b = new QToolButton(this);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(name);
        b->setObjectName(QStringLiteral("swatch_") + name);
        b->setIcon(palette::swatch(c, 18));
        b->setIconSize(QSize(18, 18));
        connect(b, &QToolButton::clicked, this, [this, c] { emit chosen(c); });
        row->addWidget(b);
        m_swatches << b;
        return b;
    };
    for (const auto &e : ColorPickButton::palette())
        swatch(e.first, e.second, lay);
    swatch(owner->kind() == ColorPickButton::TextColor ? tr("Automatic") : tr("No Highlight"), QColor(), lay);
    auto *other = new QToolButton(this);
    other->setText(tr("..."));
    other->setToolTip(tr("Other color..."));
    other->setAutoRaise(true);
    other->setFocusPolicy(Qt::NoFocus);
    other->setObjectName(QStringLiteral("swatch_other"));
    connect(other, &QToolButton::clicked, this, &ColorPickPopup::otherRequested);
    lay->addWidget(other);
    lay->addStretch(1);
    // the label colors are read each time the popup is made (labels can change)
    const QList<palette::Entry> fromLabels = palette::labels(nullptr);
    if (!fromLabels.isEmpty()) {
        auto *row = new QHBoxLayout;
        row->setSpacing(3);
        for (const palette::Entry &e : fromLabels)
            swatch(e.name, e.color, row);
        row->addStretch(1);
        grid->addLayout(row, 1, 0);
    }
}

void ColorPickPopup::enterEvent(QEnterEvent *e)
{
    QFrame::enterEvent(e);
}

void ColorPickPopup::leaveEvent(QEvent *e)
{
    QFrame::leaveEvent(e);
    // the button decides (the mouse may be on its way back to it)
    QTimer::singleShot(450, m_owner, [o = QPointer<ColorPickButton>(m_owner)] {
        if (!o)
            return;
        const QPoint g = QCursor::pos();
        if (o->rect().contains(o->mapFromGlobal(g)) || (o->popup() && o->popup()->geometry().contains(g)))
            return;
        o->hidePopup();
    });
}

} // namespace qb
