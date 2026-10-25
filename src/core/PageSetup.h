#pragma once
// Paper and margins of the page view (View > Text Editing > Page View).
// The pages are a way of looking at the text; what a compiled file looks
// like is decided by the compile format.

#include <QList>
#include <QSizeF>
#include <QString>
#include <QVariantMap>

namespace qb {

struct PageSetup {
    QString paper = QStringLiteral("A4"); // name of a preset, or "Custom"
    double width = 210, height = 297;     // mm, portrait
    bool landscape = false;
    double left = 25, right = 25, top = 25, bottom = 25; // mm

    QSizeF sizeMm() const { return landscape ? QSizeF(height, width) : QSizeF(width, height); }
    // Brings every value into a range a page can be drawn with.
    PageSetup normalized() const;
    bool operator==(const PageSetup &o) const;
    bool operator!=(const PageSetup &o) const { return !(*this == o); }

    QVariantMap toVariant() const;
    static PageSetup fromVariant(const QVariantMap &m, const PageSetup &fallback);

    struct Paper {
        QString name;
        double w, h; // mm, portrait
    };
    static QList<Paper> papers();
    // A4 with 25 mm margins, or US Letter with 1 inch where that is the usual paper.
    static PageSetup forLocale();
    // The page of new projects (File > Options or "Use for New Projects").
    static PageSetup standard();
    static void setStandard(const PageSetup &s);
};

// A page in pixels at a zoom factor. Pages follow one another in the text
// layout every step() pixels; gap is the space between two sheets.
struct PageMetrics {
    int paperW = 794, paperH = 1123;
    int left = 94, right = 94, top = 94, bottom = 94;
    int gap = 20;
    int step() const { return paperH + gap; }
    int gapAbove() const { return gap / 2; }          // above a sheet
    int gapBelow() const { return gap - gap / 2; }    // below a sheet
    bool operator==(const PageMetrics &o) const
    {
        return paperW == o.paperW && paperH == o.paperH && left == o.left && right == o.right && top == o.top
               && bottom == o.bottom && gap == o.gap;
    }
    static PageMetrics from(const PageSetup &s, double zoom);
};

} // namespace qb
