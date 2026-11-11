#include "ui/HintLabel.h"

#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QPalette>
#include <cmath>

namespace qb {
namespace hintlabel {

namespace {
double luminance(const QColor &c)
{
    auto lin = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
}

QColor mix(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

class Restyler : public QObject
{
public:
    explicit Restyler(QLabel *l) : QObject(l), m_label(l) { apply(); }

    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == m_label && !m_busy && (e->type() == QEvent::PaletteChange || e->type() == QEvent::ParentChange))
            apply();
        return false;
    }

    void apply()
    {
        m_busy = true;
        const QPalette from = m_label->parentWidget() ? m_label->parentWidget()->palette() : QApplication::palette();
        QPalette p = m_label->palette();
        for (QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive}) {
            const QColor text = from.color(g, QPalette::WindowText);
            const QColor back = from.color(g, QPalette::Window);
            QColor shown = text;
            // toward the background as far as the contrast allows (at most a third of the way)
            for (double t = 0.35; t > 0.0; t -= 0.05) {
                const QColor c = mix(text, back, t);
                if (contrast(c, back) >= 4.5) {
                    shown = c;
                    break;
                }
            }
            p.setColor(g, QPalette::WindowText, shown);
        }
        m_label->setPalette(p);
        m_busy = false;
    }

private:
    QLabel *m_label;
    bool m_busy = false;
};
} // namespace

double contrast(const QColor &a, const QColor &b)
{
    const double la = luminance(a), lb = luminance(b);
    return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
}

void style(QLabel *l)
{
    if (!l || l->property("qbHint").toBool())
        return;
    l->setWordWrap(true);
    l->setEnabled(true);
    l->setProperty("qbHint", true);
    l->installEventFilter(new Restyler(l));
}

} // namespace hintlabel
} // namespace qb
