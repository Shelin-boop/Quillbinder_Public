#pragma once
// 0.81.0 (audit of 0.80.0, S-01): lines that explain an option. Before, they were labels switched off, which Qt
// draws in its gray for unavailable controls (#BEBEBE on #EFEFEF: a contrast of 1.62:1, hard to read for anyone and
// unreadable with weak eyesight). Now they are drawn in the text color moved a little toward the background, so they
// still look secondary but keep a contrast of at least 4.5:1 (WCAG 2, success criterion 1.4.3) in every look,
// light or dark; the color is worked out again when the look changes.
#include <QColor>

class QLabel;

namespace qb {
namespace hintlabel {
// makes l a line of explanation (word wrap, the color above, property "qbHint")
void style(QLabel *l);
// the contrast ratio of two colors as WCAG 2 defines it (1 to 21)
double contrast(const QColor &a, const QColor &b);
} // namespace hintlabel
} // namespace qb
