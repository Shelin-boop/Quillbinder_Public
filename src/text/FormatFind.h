#pragma once
// Find by Formatting: ranges of a text that have a certain kind of
// formatting (highlight, comment, revision colour, style ...).
#include "core/ProjectTypes.h"

#include <QColor>
#include <QList>
#include <QString>

class QTextDocument;

namespace qb {
namespace fmtfind {

enum class Kind {
    Highlight = 0,
    CommentOrFootnote,   // inspector comments and footnotes
    InlineAnnotation,
    InlineFootnote,
    Revision,            // text typed in a revision colour
    ColoredText,         // text colour set by hand
    Link,
    CharFormat,          // bold, italic, underline, strikethrough set by hand
    Style,               // paragraph or character style
    Image
};
const int kKindCount = 10;

enum CharBits { Bold = 1, Italic = 2, Underline = 4, Strikethrough = 8 };

struct Query {
    Kind kind = Kind::Highlight;
    QString containing;        // only ranges whose text (or note text) contains this
    QColor color;              // highlight, comment, text colour: only this colour (invalid = any)
    int revision = 0;          // 0 = any revision level
    int charFormat = Bold;     // CharBits; a range must have all of them
    QString style;             // empty = any style
};

struct Range {
    int start = -1;
    int end = -1;
    bool isValid() const { return start >= 0 && end > start; }
};

// All ranges of the document, in order.
QList<Range> all(const QTextDocument *doc, const Query &q, const Formatting &f);
// The next range that starts at or after `from` (backward: the last one that ends at or before it).
Range next(const QTextDocument *doc, const Query &q, const Formatting &f, int from, bool backward);

} // namespace fmtfind
} // namespace qb
