#pragma once
#include "text/TextProps.h"

#include <QColor>
#include <QList>
#include <QString>
#include <QTextCharFormat>

class QTextDocument;
class QTextCursor;

namespace qb {
namespace text {

// "1 theme" / "3 themes" (the interface is English only, so no translation files)
inline QString plural(int n, const QString &one, const QString &many)
{
    return QString::number(n) + QLatin1Char(' ') + (n == 1 ? one : many);
}

// Visual + semantic format for a note run.
QTextCharFormat noteFormat(NoteKind kind, const QString &id = QString(), const QString &noteText = QString(),
                           const QColor &color = QColor());
// Update only the display properties of a format that carries a note.
void applyNoteDisplay(QTextCharFormat &cf);
// Imported texts (File > Options > Sharing > Import, 0.21): comments become inline
// annotations (commentsInline) and linked footnotes inline footnotes (footnotesInline),
// or the other way round: an inline footnote (and, when annotationsAreComments, an
// inline annotation) becomes a linked one on the word before it. Returns how many
// notes were changed.
int convertNotes(QTextDocument *doc, bool commentsInline, bool footnotesInline, bool annotationsAreComments);
QTextCharFormat highlightFormat(const QColor &color = QColor());
QTextCharFormat linkFormat(const QString &href);
QTextCharFormat revisionFormat(int level);
QColor revisionColor(int level);
// the five colors of the revisions: the standard ones, and those of File > Options > Editing (0.21)
QList<QColor> standardRevisionColors();
void setRevisionColors(const QList<QColor> &colors); // not five colors: back to the standard

// Remove every note / highlight / link property from a format.
void clearNoteProps(QTextCharFormat &cf);

struct Counts {
    int words = 0;
    int chars = 0;          // including spaces
    int charsNoSpaces = 0;
    int paragraphs = 0;
};

Counts count(const QString &plain);
int countWords(const QString &plain);

// Plain text of a document. Inline annotations are excluded unless asked.
QString plainText(const QTextDocument *doc, bool includeAnnotations = false);
Counts countDocument(const QTextDocument *doc);

// First few lines of text for index cards / outliner (no titles given).
QString previewText(const QString &plain, int maxChars = 300);

// A title from the first line of a text that has words (0.40.3): without inline annotations,
// inline footnotes and pictures, spaces simplified, at most maxChars, cut at a word
// boundary. Empty when the text has no words.
QString firstLineTitle(const QTextDocument *doc, int maxChars = 60);
// A document title that means "no name": empty or "Untitled".
bool isUntitled(const QString &title);

// Placeholder-safe file name from a title.
QString safeFileName(const QString &title);

// UTF-8 text of an XML file without the characters XML 1.0 does not allow (control
// characters other than tab, line feed and carriage return; U+FFFE, U+FFFF). A vertical
// tab or form feed (a line or page break pasted from a word processor) becomes a space,
// the others go. `removed` counts them. (0.20: such a character in a title made the
// project file unreadable.)
QByteArray xmlSafeBytes(const QByteArray &utf8, int *removed = nullptr);

} // namespace text
} // namespace qb
