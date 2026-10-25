#include "text/TextUtil.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>

namespace qb {
namespace text {

// Backgrounds are drawn translucent so that they work on light and dark
// editor backgrounds alike (the stored colours stay opaque).
static QColor tint(QColor c, int alpha)
{
    c.setAlpha(alpha);
    return c;
}

void applyNoteDisplay(QTextCharFormat &cf)
{
    const auto kind = NoteKind(cf.property(props::NoteKind).toInt());
    QColor c = cf.property(props::NoteColor).value<QColor>();
    switch (kind) {
    case NoteKind::InlineAnnotation:
        if (!c.isValid())
            c = defaultAnnotationColor();
        cf.setForeground(c);
        cf.setBackground(tint(c, 28));
        break;
    case NoteKind::InlineFootnote:
        cf.setBackground(tint(footnoteBubbleColor(), 110));
        cf.clearForeground();
        break;
    case NoteKind::Comment:
        if (!c.isValid())
            c = defaultCommentColor();
        cf.setBackground(tint(c, 170));
        cf.clearForeground();
        break;
    case NoteKind::LinkedFootnote:
        cf.setBackground(tint(QColor(150, 150, 150), 90));
        cf.setUnderlineStyle(QTextCharFormat::DotLine);
        cf.clearForeground();
        break;
    case NoteKind::None:
        break;
    }
}

int convertNotes(QTextDocument *doc, bool commentsInline, bool footnotesInline, bool annotationsAreComments)
{
    struct Range {
        NoteKind kind;
        QString id, noteText;
        QColor color;
        int start, end;
    };
    QList<Range> ranges;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            const QTextCharFormat cf = fr.charFormat();
            const auto kind = NoteKind(cf.intProperty(props::NoteKind));
            if (kind == NoteKind::None)
                continue;
            const QString id = cf.stringProperty(props::NoteId);
            if (!ranges.isEmpty() && ranges.last().id == id && !id.isEmpty() && ranges.last().end == fr.position()) {
                ranges.last().end = fr.position() + fr.length();
                continue;
            }
            ranges.append({kind, id, cf.stringProperty(props::NoteText), cf.property(props::NoteColor).value<QColor>(), fr.position(),
                           fr.position() + fr.length()});
        }
    int changed = 0;
    QTextCursor all(doc);
    all.beginEditBlock();
    // from the end, so that the positions before stay right
    for (int i = int(ranges.size()) - 1; i >= 0; --i) {
        const Range &r = ranges.at(i);
        QTextCursor c(doc);
        c.setPosition(r.start);
        c.setPosition(r.end, QTextCursor::KeepAnchor);
        const bool toInline = (r.kind == NoteKind::Comment && commentsInline) || (r.kind == NoteKind::LinkedFootnote && footnotesInline);
        const bool toLinked = (r.kind == NoteKind::InlineFootnote && !footnotesInline)
                              || (r.kind == NoteKind::InlineAnnotation && !commentsInline && annotationsAreComments);
        if (toInline) {
            // the anchor text stays plain; the note's text follows it in the flow
            QTextCharFormat plain = c.charFormat();
            clearNoteProps(plain);
            c.setCharFormat(plain);
            if (!r.noteText.trimmed().isEmpty()) {
                QTextCharFormat nf = plain;
                nf.merge(noteFormat(r.kind == NoteKind::Comment ? NoteKind::InlineAnnotation : NoteKind::InlineFootnote, r.id, QString(),
                                    r.kind == NoteKind::Comment ? r.color : QColor()));
                QTextCursor at(doc);
                at.setPosition(r.end);
                at.insertText(r.noteText.trimmed(), nf);
            }
            ++changed;
        } else if (toLinked) {
            // the word before the note becomes the anchor of a linked note with its text
            const QString noteText = c.selectedText().trimmed();
            QTextCursor word(doc);
            word.setPosition(r.start);
            const int blockStart = word.block().position();
            int end = r.start;
            while (end > blockStart && doc->characterAt(end - 1).isSpace())
                --end;
            int start = end;
            while (start > blockStart && !doc->characterAt(start - 1).isSpace())
                --start;
            if (start == end || noteText.isEmpty())
                continue;   // no word to hang it on: it stays inline
            c.removeSelectedText();
            word.setPosition(start);
            word.setPosition(end, QTextCursor::KeepAnchor);
            QTextCharFormat cf = word.charFormat();
            clearNoteProps(cf);
            cf.merge(noteFormat(r.kind == NoteKind::InlineFootnote ? NoteKind::LinkedFootnote : NoteKind::Comment, r.id, noteText,
                                r.kind == NoteKind::InlineAnnotation ? r.color : QColor()));
            word.setCharFormat(cf);
            ++changed;
        }
    }
    all.endEditBlock();
    return changed;
}

QTextCharFormat noteFormat(NoteKind kind, const QString &id, const QString &noteText, const QColor &color)
{
    QTextCharFormat cf;
    cf.setProperty(props::NoteKind, int(kind));
    if (!id.isEmpty())
        cf.setProperty(props::NoteId, id);
    if (isLinkedNote(kind))
        cf.setProperty(props::NoteText, noteText);
    if (color.isValid())
        cf.setProperty(props::NoteColor, color);
    applyNoteDisplay(cf);
    return cf;
}

QTextCharFormat highlightFormat(const QColor &color)
{
    QTextCharFormat cf;
    cf.setProperty(props::Highlight, true);
    cf.setBackground(tint(color.isValid() ? color : QColor(255, 240, 80), 160));
    return cf;
}

QTextCharFormat linkFormat(const QString &href)
{
    QTextCharFormat cf;
    cf.setAnchor(true);
    cf.setAnchorHref(href);
    cf.setFontUnderline(true);
    cf.setForeground(QColor(30, 90, 200));
    return cf;
}

namespace {
QList<QColor> &ownRevisionColors()
{
    static QList<QColor> c; // File > Options > Editing > Revisions (0.21); empty = the standard colors
    return c;
}
} // namespace

QList<QColor> standardRevisionColors()
{
    return {QColor(200, 0, 0), QColor(0, 90, 200), QColor(0, 140, 60), QColor(150, 0, 170), QColor(210, 110, 0)};
}

void setRevisionColors(const QList<QColor> &colors) { ownRevisionColors() = colors.size() == 5 ? colors : QList<QColor>(); }

QColor revisionColor(int level)
{
    if (level < 1)
        return QColor();
    const QList<QColor> &own = ownRevisionColors();
    return own.size() == 5 ? own[(level - 1) % 5] : standardRevisionColors()[(level - 1) % 5];
}

QTextCharFormat revisionFormat(int level)
{
    QTextCharFormat cf;
    cf.setProperty(props::Revision, level);
    cf.setForeground(revisionColor(level));
    return cf;
}

void clearNoteProps(QTextCharFormat &cf)
{
    const bool had = cf.hasProperty(props::NoteKind) || cf.hasProperty(props::Highlight);
    cf.clearProperty(props::NoteKind);
    cf.clearProperty(props::NoteId);
    cf.clearProperty(props::NoteText);
    cf.clearProperty(props::NoteColor);
    cf.clearProperty(props::Highlight);
    if (had) {
        cf.clearBackground();
        cf.clearForeground();
        if (cf.underlineStyle() == QTextCharFormat::DotLine)
            cf.setUnderlineStyle(QTextCharFormat::NoUnderline);
    }
}

static bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('\'') || c == QChar(0x2019) || c == QLatin1Char('-');
}

Counts count(const QString &plain)
{
    Counts r;
    bool inWord = false;
    bool wordHasAlnum = false;
    bool paraHasText = false;
    for (int i = 0; i < plain.size(); ++i) {
        const QChar c = plain.at(i);
        if (c == QChar(0xFFFC))
            continue; // object replacement (images)
        if (c == QLatin1Char('\n') || c == QChar::ParagraphSeparator) {
            if (paraHasText)
                ++r.paragraphs;
            paraHasText = false;
        } else {
            ++r.chars;
            if (!c.isSpace()) {
                ++r.charsNoSpaces;
                paraHasText = true;
            }
        }
        if (isWordChar(c)) {
            if (!inWord) {
                inWord = true;
                wordHasAlnum = false;
            }
            if (c.isLetterOrNumber())
                wordHasAlnum = true;
        } else {
            if (inWord && wordHasAlnum)
                ++r.words;
            inWord = false;
        }
    }
    if (inWord && wordHasAlnum)
        ++r.words;
    if (paraHasText)
        ++r.paragraphs;
    return r;
}

int countWords(const QString &plain) { return count(plain).words; }

QString plainText(const QTextDocument *doc, bool includeAnnotations)
{
    QString out;
    out.reserve(doc->characterCount());
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (b != doc->begin())
            out += QLatin1Char('\n');
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid())
                continue;
            if (!includeAnnotations
                && NoteKind(f.charFormat().property(props::NoteKind).toInt()) == NoteKind::InlineAnnotation)
                continue;
            out += f.text();
        }
    }
    out.replace(QChar::LineSeparator, QLatin1Char('\n'));
    return out;
}

Counts countDocument(const QTextDocument *doc) { return count(plainText(doc)); }

QString previewText(const QString &plain, int maxChars)
{
    QString s = plain.left(maxChars * 2);
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    s = s.simplified();
    if (s.size() > maxChars) {
        s.truncate(maxChars);
        const int sp = s.lastIndexOf(QLatin1Char(' '));
        if (sp > maxChars / 2)
            s.truncate(sp);
        s += QChar(0x2026);
    }
    return s;
}

QString safeFileName(const QString &title)
{
    QString s = title.trimmed();
    static const QRegularExpression bad(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1F]"));
    s.replace(bad, QStringLiteral("_"));
    if (s.isEmpty())
        s = QStringLiteral("Untitled");
    if (s.size() > 80)
        s.truncate(80);
    return s;
}

QByteArray xmlSafeBytes(const QByteArray &utf8, int *removed)
{
    int n = 0;
    QByteArray out;
    out.reserve(utf8.size());
    const int size = int(utf8.size());
    for (int i = 0; i < size; ++i) {
        const uchar c = uchar(utf8.at(i));
        if (c < 0x20 && c != 0x09 && c != 0x0A && c != 0x0D) {
            ++n;
            if (c == 0x0B || c == 0x0C)
                out.append(' ');
            continue;
        }
        // U+FFFE and U+FFFF: EF BF BE / EF BF BF
        if (c == 0xEF && i + 2 < size && uchar(utf8.at(i + 1)) == 0xBF
            && (uchar(utf8.at(i + 2)) == 0xBE || uchar(utf8.at(i + 2)) == 0xBF)) {
            ++n;
            i += 2;
            continue;
        }
        out.append(char(c));
    }
    if (removed)
        *removed = n;
    return n ? out : utf8;
}

} // namespace text
} // namespace qb
