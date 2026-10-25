#pragma once
// The look of a compiled paragraph: what the style says (as in the DOCX
// writer's style sheet), with the formatting set by hand on top. One place for
// the built-in PDF and EPUB writers, so that they match the Word file.
#include "compile/ExporterApi.h"

#include <QFont>
#include <QString>

namespace qb {
namespace compile {
namespace look {

struct Para {
    QString family;
    double size = 12;               // points
    bool bold = false, italic = false;
    Qt::Alignment align = Qt::AlignLeft;
    double leftMm = 0, rightMm = 0, firstMm = 0;
    double beforePt = 0, afterPt = 0;
    double lineSpacing = 1;         // multiple (0 = see lineHeightPt)
    double lineHeightPt = 0;
    bool lineAtLeast = false;
    bool keepWithNext = false;
    QList<TabStop> tabs;
};

Para paragraph(const Manuscript &m, const Paragraph &p);
// The font of a run in a paragraph of this look.
QFont runFont(const Para &para, const Run &r);
double footnoteSize(const Manuscript &m);
double headerSize(const Manuscript &m);
// "i", "IV", "12" for page numbers
QString pageNumberText(int n, int style);

} // namespace look
} // namespace compile
} // namespace qb
