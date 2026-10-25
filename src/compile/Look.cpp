#include "compile/Look.h"

#include "compile/Compiler.h"

namespace qb {
namespace compile {
namespace look {

Para paragraph(const Manuscript &m, const Paragraph &p)
{
    Para a;
    a.family = m.fontFamily;
    a.size = m.fontSize;
    a.lineSpacing = m.lineSpacing;
    a.firstMm = m.firstLineIndentMm;
    a.afterPt = m.paragraphSpacingPt;
    a.align = m.justify ? Qt::AlignJustify : Qt::AlignLeft;
    const double hs = m.headingScale;
    const QString hfont = m.headingFontFamily.isEmpty() ? m.fontFamily : m.headingFontFamily;
    auto heading = [&](double factor, bool italic) {
        a.family = hfont;
        a.size = m.fontSize * factor;
        a.bold = m.headingBold;
        a.italic = italic;
        a.align = Qt::AlignHCenter;
        a.firstMm = 0;
        a.keepWithNext = true;
        a.beforePt = hs > 1.01 ? 18 : 0;
        a.afterPt = hs > 1.01 ? 9 : m.paragraphSpacingPt;
    };
    switch (p.style) {
    case Paragraph::Heading1: heading(hs, false); break;
    case Paragraph::Heading2: heading(1 + (hs - 1) * 0.6, false); break;
    case Paragraph::Heading3: heading(1 + (hs - 1) * 0.3, hs <= 1.01); break;
    case Paragraph::Title: heading(qMax(1.2, hs * 1.3), false); break;
    case Paragraph::Subtitle:
        a.italic = true;
        a.size = m.fontSize * qMax(1.1, hs * 0.9);
        a.align = Qt::AlignHCenter;
        a.firstMm = 0;
        break;
    case Paragraph::BlockQuote:
        a.leftMm = a.rightMm = 12.7;
        a.firstMm = 0;
        break;
    case Paragraph::Centered:
        a.align = Qt::AlignHCenter;
        a.firstMm = 0;
        break;
    case Paragraph::Separator:
        a.align = Qt::AlignHCenter;
        a.firstMm = 0;
        a.keepWithNext = true;
        break;
    case Paragraph::Caption:
        a.align = Qt::AlignHCenter;
        a.italic = true;
        a.firstMm = 0;
        break;
    case Paragraph::Synopsis:
        a.italic = true;
        a.firstMm = 0;
        break;
    case Paragraph::ListBullet:
    case Paragraph::ListNumber:
        a.leftMm = 6.35 * (p.listLevel + 1);
        a.firstMm = -5;
        break;
    case Paragraph::Normal: break;
    }
    if (p.align != Qt::Alignment() && !(p.direct & Paragraph::DAlign)) {
        a.align = p.align;
        if (p.style == Paragraph::Normal && (p.align & (Qt::AlignHCenter | Qt::AlignRight)))
            a.firstMm = 0;
    }
    if (p.direct & Paragraph::DAlign)
        a.align = p.align == Qt::Alignment() ? Qt::AlignLeft : p.align;
    if (p.direct & Paragraph::DLeft)
        a.leftMm = p.leftIndentMm;
    if (p.direct & Paragraph::DRight)
        a.rightMm = p.rightIndentMm;
    if (p.direct & Paragraph::DFirst)
        a.firstMm = p.firstLineMm;
    if (p.direct & Paragraph::DBefore)
        a.beforePt = p.spaceBeforePt;
    if (p.direct & Paragraph::DAfter)
        a.afterPt = p.spaceAfterPt;
    if (p.direct & Paragraph::DLine) {
        a.lineSpacing = p.lineSpacing;
        a.lineHeightPt = p.lineHeightPt;
        a.lineAtLeast = p.lineAtLeast;
    }
    if (p.direct & Paragraph::DTabs)
        a.tabs = p.tabs;
    if (p.noIndent && a.firstMm > 0 && p.style != Paragraph::ListBullet && p.style != Paragraph::ListNumber)
        a.firstMm = 0;
    if (p.keepWithNext)
        a.keepWithNext = true;
    return a;
}

QFont runFont(const Para &para, const Run &r)
{
    QFont f(r.fontFamily.isEmpty() ? para.family : r.fontFamily);
    double size = r.fontSize > 0 ? r.fontSize : para.size;
    f.setPointSizeF(qMax(1.0, size));
    if (r.fontWeight > 0)
        f.setWeight(QFont::Weight(r.fontWeight));
    else
        f.setBold(r.bold || para.bold);
    f.setItalic(r.italic || para.italic);
    f.setUnderline(r.underline);
    f.setStrikeOut(r.strike);
    if (r.capitals == 1)
        f.setCapitalization(QFont::SmallCaps);
    else if (r.capitals == 2)
        f.setCapitalization(QFont::AllUppercase);
    if (r.letterSpacingEm != 0)
        f.setLetterSpacing(QFont::PercentageSpacing, 100.0 + r.letterSpacingEm * 100.0);
    return f;
}

double footnoteSize(const Manuscript &m)
{
    return m.options.footnoteSize > 0 ? m.options.footnoteSize : qMax(7.0, m.fontSize - 2);
}

double headerSize(const Manuscript &m)
{
    return m.page.headerFontSize > 0 ? m.page.headerFontSize : qMax(7.0, m.fontSize - 2);
}

QString pageNumberText(int n, int style)
{
    if (style == 1)
        return Placeholders::toRoman(n).toLower();
    if (style == 2)
        return Placeholders::toRoman(n);
    return QString::number(n);
}

} // namespace look
} // namespace compile
} // namespace qb
