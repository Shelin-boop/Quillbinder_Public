#pragma once
// Custom QTextFormat properties used by the editor and the (de)serializers.

#include <QTextFormat>
#include <QColor>
#include <QString>

namespace qb {
namespace props {
enum : int {
    Style = QTextFormat::UserProperty + 1, // block: QString paragraph style name ("" = Normal)
    NoteKind,                              // char: int (qb::NoteKind)
    NoteId,                                // char: QString, groups fragments of one note
    NoteText,                              // char: QString, text of a linked comment / footnote
    NoteColor,                             // char: QColor of a comment / annotation
    Revision,                              // char: int revision level (1..n)
    ListLevel,                             // block: int, only used while (de)serializing
    ScriptElement,                         // block: int (script mode, reserved)
    PageBreak,                             // block: bool (page break before)
    Highlight,                             // char: bool, marks a user highlight (vs. note bubble)
    SearchHit,                             // char: bool, temporary search highlight (never saved)
    ParaDirect,                            // block: int, bit set of direct::ParaBit (formatting set by hand)
    CharStyle,                             // char: QString character style name
    // char: bool, not bold / not italic set by hand where the style is bold / italic (0.40.3):
    // the style's look is not given back over it (styles::applyChars)
    NotBold,
    NotItalic
};
} // namespace props

enum class NoteKind : int {
    None = 0,
    InlineAnnotation = 1,  // text in the flow, not compiled (or exported as comment)
    InlineFootnote = 2,    // text in the flow, becomes a footnote when compiled
    Comment = 3,           // anchor text in the flow, comment text in the inspector
    LinkedFootnote = 4     // anchor text in the flow, footnote text in the inspector
};

inline bool isLinkedNote(NoteKind k) { return k == NoteKind::Comment || k == NoteKind::LinkedFootnote; }
inline bool isInlineNote(NoteKind k) { return k == NoteKind::InlineAnnotation || k == NoteKind::InlineFootnote; }

// Colours used to display notes in the editor.
QColor defaultCommentColor();      // yellow-ish
QColor defaultAnnotationColor();   // red
QColor footnoteBubbleColor();      // grey

} // namespace qb
