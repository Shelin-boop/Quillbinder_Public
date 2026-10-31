#pragma once
// Direct formatting: fonts, colours and paragraph metrics that the writer set
// by hand on a piece of text (as opposed to the project's default formatting
// and the paragraph styles).
//
// Characters: a direct attribute is simply a property of the char format
// (font family, point size, colour ...). Paragraphs: the block format always
// carries complete metrics, so the block property props::ParaDirect records
// which of them were set by hand; everything else follows the project
// formatting and the style and is recomputed by display::normalizeDocument.
//
// All lengths in block formats are pixels at 96 dpi ("pixels at 100 %").

#include "core/ProjectTypes.h"

#include <QColor>
#include <QList>
#include <QString>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextOption>

class QTextDocument;

namespace qb {
namespace direct {

constexpr double kPxPerMm = 96.0 / 25.4;
constexpr double kPxPerPt = 96.0 / 72.0;

enum CharMember : int { Family = 1, Size = 2, Weight = 4, Color = 8, Spacing = 16, Caps = 32, AllChar = 63 };

enum ParaBit : int {
    Align = 1,
    Left = 2,
    Right = 4,
    First = 8,
    Before = 16,
    After = 32,
    Line = 64,
    Tabs = 128,
    AllPara = 255
};

// ---------------------------------------------------------------- characters
struct CharSpec {
    QString family;    // empty = font of the project
    double size = 0;   // points; 0 = size of the project
    int weight = 0;    // QFont weight other than normal / bold; 0 = not set
    QColor color;      // invalid = automatic
    int spacing = 0;   // letter spacing in percent of normal; 0 = normal
    int caps = 0;      // 0 = as typed, 1 = small caps, 2 = all caps

    bool isEmpty() const
    {
        return family.isEmpty() && size <= 0 && weight == 0 && !color.isValid() && spacing == 0 && caps == 0;
    }
    bool operator==(const CharSpec &o) const
    {
        return family == o.family && qFuzzyCompare(size + 1, o.size + 1) && weight == o.weight && color == o.color
               && spacing == o.spacing && caps == o.caps;
    }
    bool operator!=(const CharSpec &o) const { return !(*this == o); }
};

// The direct attributes of a char format (colours that only display a note,
// a link or a revision level are not direct formatting).
CharSpec charSpec(const QTextCharFormat &cf);
// Set the attributes of the spec on the format; empty members are cleared.
void applyCharSpec(QTextCharFormat &cf, const CharSpec &s);
// Set only the members of the spec that are not empty.
void mergeCharSpec(QTextCharFormat &cf, const CharSpec &s);
void clearCharSpec(QTextCharFormat &cf);
// CSS declarations as used in <span style="..."> of the Markdown dialect.
QString toCss(const CharSpec &s);
CharSpec charSpecFromCss(const QString &css);
// Letter spacing: percent of the normal advance <-> typographic units.
double spacingToEm(int percent);
int spacingFromEm(double em);

// ---------------------------------------------------------------- paragraphs
struct ParaSpec {
    int bits = 0;                           // which members are meaningful
    Qt::Alignment align = Qt::AlignLeft;
    double left = 0, right = 0, first = 0;  // indents, px
    double before = 0, after = 0;           // spacing, px
    int lineType = QTextBlockFormat::ProportionalHeight;
    double line = 100;                      // percent (proportional) or px (fixed / minimum)
    QList<QTextOption::Tab> tabs;           // positions in px
};

// The attributes marked as direct in the block format.
ParaSpec paraSpec(const QTextBlockFormat &bf);
// All attributes of the block format, direct or not (what the reader sees).
ParaSpec effectiveParaSpec(const QTextBlockFormat &bf);
// Set the attributes named in s.bits and mark them as direct.
void applyParaSpec(QTextBlockFormat &bf, const ParaSpec &s);
// "align=right left=10mm first=0mm before=6pt line=1.5 tabs=20mm,50mmR"
QString toText(const ParaSpec &s);
ParaSpec paraSpecFromText(const QString &text);
// The block format of a paragraph in a document that follows the project
// formatting (Markdown projects): cur with the attributes of s set by hand.
// Attributes that equal what project and style give anyway are not marked,
// so the paragraph keeps following them.
QTextBlockFormat withParaSpec(const QTextBlockFormat &cur, const ParaSpec &s, const Formatting &f, bool inList);
// Bits of the attributes in which the two formats differ.
int differences(const QTextBlockFormat &a, const QTextBlockFormat &b);

// Mark the paragraph attributes (of mask) that differ from the project's
// formatting and the paragraph's style as direct. Used when RTF text becomes
// Markdown; line spacing, first line indent and paragraph spacing are the
// look of the body text and follow the project unless asked for.
void markDifferences(QTextDocument *doc, const Formatting &f, int mask = Align | Left | Right | Tabs);
// Remove direct formatting. chars: fonts, sizes, colours, spacing, capitals;
// paraBits: the paragraph attributes to give back to the style.
void removeDirect(QTextDocument *doc, const Formatting &f, bool chars, int paraBits, int from = 0, int to = -1);

// The project formatting changed from oldF to newF (Project Settings ▸
// Formatting): text that showed the old value of a changed attribute takes
// the new one, also where the value is stored with the text itself (RTF
// projects, imported text). charMembers: Family and Size; paraBits: First,
// Line, After, Align. With ownToo, text in the main font, size or spacing of
// its document follows as well although it was set by hand (imported text);
// values that differ from it (larger headings, a passage in another font, an
// unindented first paragraph) are kept. Returns the number of changed pieces.
int followProject(QTextDocument *doc, const Formatting &oldF, const Formatting &newF, int charMembers, int paraBits,
                  bool ownToo);
// A style got a new font or size (format bar ▸ Change Style): text of that
// style (paragraph style, or character style with character = true) that has
// the old or the new value set by hand drops it and shows the style. f is the
// new formatting. Returns the number of changed pieces.
int followStyle(QTextDocument *doc, const Formatting &f, const QString &style, bool character, int charMembers,
                const CharSpec &oldValues, const CharSpec &newValues);
// A style was changed (0.40.2): all text of the style (paragraph style, or character
// style with character = true) takes over what changed, also where the text had its own
// value (set by hand, or brought along by an import): charMembers (CharMember: Family,
// Size, Weight, Color, Spacing, Caps) are no longer set by hand in its pieces, paraBits
// (ParaBit) no longer in its paragraphs. Other formatting set by hand stays. f is the new
// formatting. Returns the number of changed pieces and paragraphs.
int takeStyle(QTextDocument *doc, const Formatting &f, const QString &style, bool character, int charMembers, int paraBits);
// The character members and paragraph bits in which two definitions of a style differ.
int charMembersBetween(const CharSpec &a, const CharSpec &b);
int paraBitsBetween(const ParaSpec &a, const ParaSpec &b);
// The members of the font and the paragraph bits in which two project
// formattings differ (for followProject).
int changedCharMembers(const Formatting &a, const Formatting &b);
int changedParaBits(const Formatting &a, const Formatting &b);

// Number formatting helpers shared by the dialogs and the ruler.
inline double pxToMm(double px) { return px / kPxPerMm; }
inline double mmToPx(double mm) { return mm * kPxPerMm; }
inline double pxToPt(double px) { return px / kPxPerPt; }
inline double ptToPx(double pt) { return pt * kPxPerPt; }

} // namespace direct
} // namespace qb
