#include "text/FormatFind.h"
#include "text/DirectFormat.h"
#include "text/Styles.h"
#include "text/TextProps.h"

#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>

namespace qb {
namespace fmtfind {

namespace {
bool sameColor(const QColor &a, const QColor &b) { return a.isValid() && b.isValid() && a.rgb() == b.rgb(); }

// Does the fragment have the formatting, and which run does it belong to
// (fragments next to each other with the same key are one range)?
bool fragmentMatches(const QTextCharFormat &cf, const QTextBlock &block, const Query &q, const Formatting &f, QString *key)
{
    const NoteKind note = NoteKind(cf.intProperty(props::NoteKind));
    key->clear();
    switch (q.kind) {
    case Kind::Highlight:
        if (!cf.boolProperty(props::Highlight))
            return false;
        return !q.color.isValid() || sameColor(cf.background().color(), q.color);
    case Kind::CommentOrFootnote:
        if (!isLinkedNote(note))
            return false;
        *key = cf.stringProperty(props::NoteId);
        if (q.color.isValid()) {
            const QColor c = cf.colorProperty(props::NoteColor);
            if (!sameColor(c.isValid() ? c : (note == NoteKind::Comment ? defaultCommentColor() : footnoteBubbleColor()), q.color))
                return false;
        }
        return true;
    case Kind::InlineAnnotation:
        if (note != NoteKind::InlineAnnotation)
            return false;
        if (q.color.isValid()) {
            const QColor c = cf.colorProperty(props::NoteColor);
            if (!sameColor(c.isValid() ? c : defaultAnnotationColor(), q.color))
                return false;
        }
        return true;
    case Kind::InlineFootnote:
        return note == NoteKind::InlineFootnote;
    case Kind::Revision: {
        const int level = cf.intProperty(props::Revision);
        if (level <= 0 || (q.revision > 0 && level != q.revision))
            return false;
        *key = QString::number(level);
        return true;
    }
    case Kind::ColoredText: {
        if (cf.intProperty(props::Revision) > 0 || cf.isAnchor() || isInlineNote(note) || cf.isImageFormat())
            return false;
        const Style *ps = styles::find(f, block.blockFormat().stringProperty(props::Style));
        const QColor c = direct::charSpec(styles::withoutStyles(cf, ps, f)).color;
        if (!c.isValid() || (q.color.isValid() && !sameColor(c, q.color)))
            return false;
        *key = c.name();
        return true;
    }
    case Kind::Link:
        if (!cf.isAnchor() || cf.anchorHref().isEmpty())
            return false;
        *key = cf.anchorHref();
        return true;
    case Kind::CharFormat: {
        if (cf.isImageFormat())
            return false;
        const Style *ps = styles::find(f, block.blockFormat().stringProperty(props::Style));
        const QTextCharFormat own = styles::withoutStyles(cf, ps, f);
        if ((q.charFormat & Bold) && own.fontWeight() < QFont::DemiBold)
            return false;
        if ((q.charFormat & Italic) && !own.fontItalic())
            return false;
        if ((q.charFormat & Underline) && (!own.fontUnderline() || cf.isAnchor()))
            return false;
        if ((q.charFormat & Strikethrough) && !own.fontStrikeOut())
            return false;
        return (q.charFormat & (Bold | Italic | Underline | Strikethrough)) != 0;
    }
    case Kind::Style: {
        const QString ps = block.blockFormat().stringProperty(props::Style);
        const QString cs = cf.stringProperty(props::CharStyle);
        if (q.style.isEmpty()) {
            if (ps.isEmpty() && cs.isEmpty())
                return false;
            *key = ps + QLatin1Char('|') + cs;
            return true;
        }
        if (ps == q.style) {
            *key = QStringLiteral("p");
            return true;
        }
        if (cs == q.style) {
            *key = QStringLiteral("c");
            return true;
        }
        return false;
    }
    case Kind::Image:
        return cf.isImageFormat();
    }
    return false;
}
} // namespace

QList<Range> all(const QTextDocument *doc, const Query &q, const Formatting &f)
{
    QList<Range> out;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        Range cur;
        QString curKey, curNote;
        auto flush = [&] {
            if (!cur.isValid())
                return;
            bool ok = true;
            if (!q.containing.isEmpty()) {
                const QString t = b.text().mid(cur.start - b.position(), cur.end - cur.start);
                ok = t.contains(q.containing, Qt::CaseInsensitive) || curNote.contains(q.containing, Qt::CaseInsensitive);
            }
            if (ok)
                out << cur;
            cur = Range();
        };
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            const QTextCharFormat cf = fr.charFormat();
            QString key;
            if (!fragmentMatches(cf, b, q, f, &key)) {
                flush();
                continue;
            }
            if (cur.isValid() && key == curKey && cur.end == fr.position() && q.kind != Kind::Image) {
                cur.end = fr.position() + fr.length();
            } else {
                flush();
                cur.start = fr.position();
                cur.end = fr.position() + fr.length();
                curKey = key;
                curNote = q.kind == Kind::CommentOrFootnote ? cf.stringProperty(props::NoteText) : QString();
            }
        }
        flush();
    }
    return out;
}

Range next(const QTextDocument *doc, const Query &q, const Formatting &f, int from, bool backward)
{
    const QList<Range> ranges = all(doc, q, f);
    if (backward) {
        for (int i = int(ranges.size()) - 1; i >= 0; --i)
            if (ranges[i].end <= from)
                return ranges[i];
    } else {
        for (const Range &r : ranges)
            if (r.start >= from)
                return r;
    }
    return Range();
}

} // namespace fmtfind
} // namespace qb
