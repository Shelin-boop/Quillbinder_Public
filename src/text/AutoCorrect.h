#pragma once
// Corrections while typing (File > Options > Corrections, 0.21; as in Scrivener).
//
// What the writer sees: when a word is finished (a space, a Return or a
// punctuation mark is typed after it), the word may be corrected: the first
// word of a sentence gets a capital letter, a lone "i" becomes "I", a typed
// text of the substitution list becomes its replacement ("(c)" -> "©"), a web
// address becomes a link. While typing, quotes become typographic quotes in
// the chosen style, two hyphens an em dash, three periods an ellipsis.
//
// How it works: plain functions on a QTextCursor, without any window, so that
// the editor (ui/TextEditor.cpp) and the tests can use them alike. The editor
// decides when to call them and with which options (from AppSettings).
//
// Why each correction is one edit step: Ctrl+Z right after a correction takes
// back the correction only, as in word processors.

#include <QChar>
#include <QList>
#include <QPair>
#include <QString>

class QTextCursor;

namespace qb {
namespace autocorrect {

struct Options {
    bool capSentences = false;
    bool capI = false;
    bool urls = false;
    bool substitutions = false;
    QList<QPair<QString, QString>> substitutionList;   // typed text -> replacement
};

// The quote characters of a style ("en", "de", "de2", "fr", "sv"): opening
// double, closing double, opening single, closing single.
QString quoteChars(const QString &style);
// The character for a typed " or ' after the character `before` (a space or
// nothing: opening). An apostrophe inside a word is always ’.
QChar smartQuote(QChar typed, QChar before, const QString &style);
// The same with the paragraph's text before the cursor (0.64.2): after a word, a ' closes a single mark open in
// it when the style's closing single mark is not the apostrophe (German, French, ...).
QChar smartQuote(QChar typed, const QString &before, const QString &style);
// When `typed` (a letter or digit) follows a closing single mark that follows a letter, the position in `before`
// of that mark, which then was an apostrophe (it becomes ’); else -1.
int apostropheBeforeLetter(const QString &before, QChar typed, const QString &style);

// Quote marks of existing text (File > Options > Corrections, Convert Quotes, 0.61.1): the
// positions in `text` that change and their new characters. typographic: straight " and '
// become the marks of the style, opening after nothing, a space, a bracket, a dash or an
// opening mark, else closing; ' inside a word, after a letter or a number with no single
// mark open, or before a number ('90s), becomes the apostrophe ’; a straight mark with a space
// or nothing on both sides stays.
// Not typographic: every typographic quote mark and apostrophe (“ ” „ ‟ « » ‘ ’ ‚ ‛ ‹ ›)
// becomes " or '.
QList<QPair<int, QChar>> convertQuotes(const QString &text, bool typographic, const QString &style);

// Corrections of the word that ends at the cursor (no selection). Returns what
// was done, for the tests ("substitution", "link", "capital I", "capital"),
// empty when nothing changed.
QStringList afterWord(QTextCursor &cursor, const Options &o);

// Is the position the beginning of a sentence (start of the paragraph, or
// after . ! ? that do not end an abbreviation)?
bool sentenceStartsAt(const QString &blockText, int pos);

} // namespace autocorrect
} // namespace qb
