#include "core/PageSetup.h"
#include "compile/CompileFormat.h"

#include <QLocale>
#include <QtMath>

namespace qb {

namespace {
const double kPxPerMm = 96.0 / 25.4;
PageSetup *g_standard = nullptr;
} // namespace

QList<PageSetup::Paper> PageSetup::papers()
{
    QList<Paper> out;
    for (const compile::PaperSize &p : compile::paperSizes())
        out << Paper{p.name, p.w, p.h};
    out << Paper{QStringLiteral("B5"), 176, 250} << Paper{QStringLiteral("A6"), 105, 148}
        << Paper{QStringLiteral("US Legal"), 215.9, 355.6};
    return out;
}

PageSetup PageSetup::normalized() const
{
    PageSetup s = *this;
    s.width = qBound(50.0, s.width, 1000.0);
    s.height = qBound(50.0, s.height, 1500.0);
    const QSizeF size = s.sizeMm();
    // at least 30 mm of text in each direction
    auto fit = [](double &a, double &b, double total) {
        a = qBound(0.0, a, total);
        b = qBound(0.0, b, total);
        if (a + b > total - 30) {
            const double f = qMax(0.0, total - 30) / qMax(1.0, a + b);
            a *= f;
            b *= f;
        }
    };
    fit(s.left, s.right, size.width());
    fit(s.top, s.bottom, size.height());
    if (s.paper.isEmpty())
        s.paper = QStringLiteral("Custom");
    return s;
}

bool PageSetup::operator==(const PageSetup &o) const
{
    auto same = [](double a, double b) { return qAbs(a - b) < 0.005; };
    return paper == o.paper && landscape == o.landscape && same(width, o.width) && same(height, o.height)
           && same(left, o.left) && same(right, o.right) && same(top, o.top) && same(bottom, o.bottom);
}

QVariantMap PageSetup::toVariant() const
{
    return {{QStringLiteral("paper"), paper},   {QStringLiteral("width"), width},   {QStringLiteral("height"), height},
            {QStringLiteral("landscape"), landscape}, {QStringLiteral("left"), left}, {QStringLiteral("right"), right},
            {QStringLiteral("top"), top},       {QStringLiteral("bottom"), bottom}};
}

PageSetup PageSetup::fromVariant(const QVariantMap &m, const PageSetup &fallback)
{
    if (m.isEmpty())
        return fallback;
    PageSetup s = fallback;
    s.paper = m.value(QStringLiteral("paper"), s.paper).toString();
    s.width = m.value(QStringLiteral("width"), s.width).toDouble();
    s.height = m.value(QStringLiteral("height"), s.height).toDouble();
    s.landscape = m.value(QStringLiteral("landscape"), s.landscape).toBool();
    s.left = m.value(QStringLiteral("left"), s.left).toDouble();
    s.right = m.value(QStringLiteral("right"), s.right).toDouble();
    s.top = m.value(QStringLiteral("top"), s.top).toDouble();
    s.bottom = m.value(QStringLiteral("bottom"), s.bottom).toDouble();
    // a preset keeps its size even if the stored numbers were edited by hand
    for (const Paper &p : papers())
        if (p.name == s.paper) {
            s.width = p.w;
            s.height = p.h;
        }
    return s.normalized();
}

PageSetup PageSetup::forLocale()
{
    PageSetup s;
    if (QLocale().measurementSystem() != QLocale::MetricSystem) {
        s.paper = QStringLiteral("US Letter");
        s.width = 215.9;
        s.height = 279.4;
        s.left = s.right = s.top = s.bottom = 25.4;
    }
    return s;
}

PageSetup PageSetup::standard() { return g_standard ? *g_standard : forLocale(); }

void PageSetup::setStandard(const PageSetup &s)
{
    if (!g_standard)
        g_standard = new PageSetup;
    *g_standard = s.normalized();
}

PageMetrics PageMetrics::from(const PageSetup &setup, double zoom)
{
    const PageSetup s = setup.normalized();
    const QSizeF size = s.sizeMm();
    auto px = [zoom](double mm) { return qRound(mm * kPxPerMm * zoom); };
    PageMetrics m;
    m.paperW = px(size.width());
    m.paperH = px(size.height());
    m.left = px(s.left);
    m.right = px(s.right);
    m.top = px(s.top);
    m.bottom = px(s.bottom);
    m.gap = 20;
    return m;
}

} // namespace qb
