#include "core/Replace.h"
#include "core/BinderItem.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/Search.h"
#include "core/Snapshots.h"
#include "text/TextProps.h"

#include <QCoreApplication>
#include <QSet>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace qb {
namespace replace {

QRegularExpression pattern(const ReplaceOptions &o) { return search::pattern(o.find, o.caseSensitive, o.wholeWord, o.regex); }

QString replacement(const QRegularExpressionMatch &m, const QString &with, bool regex)
{
    if (!regex)
        return with;
    QString out;
    out.reserve(with.size());
    for (int i = 0; i < with.size(); ++i) {
        const QChar c = with[i];
        if (c == QLatin1Char('\\') && i + 1 < with.size()) {
            const QChar n = with[i + 1];
            if (n.isDigit()) {
                out += m.captured(n.digitValue());
                ++i;
                continue;
            }
            if (n == QLatin1Char('\\')) {
                out += QLatin1Char('\\');
                ++i;
                continue;
            }
            if (n == QLatin1Char('n')) {
                out += QLatin1Char('\n');
                ++i;
                continue;
            }
            if (n == QLatin1Char('t')) {
                out += QLatin1Char('\t');
                ++i;
                continue;
            }
        }
        out += c;
    }
    return out;
}

namespace {
struct Hit {
    int start, length;
    QString text;
};

QList<Hit> hitsIn(const QString &s, const QRegularExpression &re, const QString &with, bool regex)
{
    QList<Hit> hits;
    if (!re.isValid())
        return hits;
    auto it = re.globalMatch(s);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (m.capturedLength() == 0)
            continue; // an empty match would only insert text everywhere
        hits.append({int(m.capturedStart()), int(m.capturedLength()), replacement(m, with, regex)});
    }
    return hits;
}
} // namespace

int inString(QString *s, const QRegularExpression &re, const QString &with, bool regex, bool countOnly)
{
    const QList<Hit> hits = hitsIn(*s, re, with, regex);
    if (!countOnly)
        for (int i = int(hits.size()) - 1; i >= 0; --i)
            s->replace(hits[i].start, hits[i].length, hits[i].text);
    return int(hits.size());
}

int inDocument(QTextDocument *doc, const QRegularExpression &re, const QString &with, bool regex, bool countOnly)
{
    int n = 0;
    QTextCursor edit(doc);
    bool editing = false;
    // from the end, so that the positions of earlier blocks stay valid
    for (QTextBlock b = doc->lastBlock(); b.isValid(); b = b.previous()) {
        const QList<Hit> hits = hitsIn(b.text(), re, with, regex);
        n += int(hits.size());
        if (countOnly || hits.isEmpty())
            continue;
        if (!editing) {
            edit.beginEditBlock();
            editing = true;
        }
        const int base = b.position();
        for (int i = int(hits.size()) - 1; i >= 0; --i) {
            const Hit &h = hits[i];
            QTextCursor c(doc);
            c.setPosition(base + h.start + 1);
            const QTextCharFormat cf = c.charFormat(); // format of the first character of the match
            c.setPosition(base + h.start);
            c.setPosition(base + h.start + h.length, QTextCursor::KeepAnchor);
            QString t = h.text;
            t.replace(QLatin1Char('\n'), QChar(0x2028)); // a line break, not a new paragraph
            c.insertText(t, cf);
        }
    }
    if (editing)
        edit.endEditBlock();
    return n;
}

int inNoteTexts(QTextDocument *doc, const QRegularExpression &re, const QString &with, bool regex, bool countOnly)
{
    struct Piece {
        int start, end;
        QString id;
        QString newText;
    };
    QList<Piece> pieces;
    QSet<QString> counted;
    int n = 0;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            const QTextCharFormat cf = fr.charFormat();
            if (!isLinkedNote(NoteKind(cf.intProperty(props::NoteKind))))
                continue;
            QString text = cf.stringProperty(props::NoteText);
            const int hits = inString(&text, re, with, regex, false);
            if (hits == 0)
                continue;
            const QString id = cf.stringProperty(props::NoteId);
            // a note may be spread over several fragments: count its text once
            if (id.isEmpty() || !counted.contains(id)) {
                n += hits;
                counted.insert(id);
            }
            pieces.append({fr.position(), fr.position() + fr.length(), id, text});
        }
    }
    if (countOnly || pieces.isEmpty())
        return n;
    QTextCursor edit(doc);
    edit.beginEditBlock();
    for (const Piece &p : pieces) {
        QTextCursor c(doc);
        c.setPosition(p.start);
        c.setPosition(p.end, QTextCursor::KeepAnchor);
        QTextCharFormat f;
        f.setProperty(props::NoteText, p.newText);
        c.mergeCharFormat(f);
    }
    edit.endEditBlock();
    return n;
}

ReplaceResult run(Project *p, const ReplaceOptions &o, bool countOnly, const std::function<bool(int, int)> &progress)
{
    ReplaceResult r;
    if (o.find.isEmpty())
        return r;
    const QRegularExpression re = pattern(o);
    if (!re.isValid()) {
        r.error = QCoreApplication::translate("qb::replace", "The regular expression has a mistake: %1").arg(re.errorString());
        return r;
    }
    QSet<BinderItem *> scope;
    for (const QString &id : o.scopeIds)
        if (BinderItem *s = p->item(id))
            s->forEach([&scope](BinderItem *i) { scope.insert(i); });
    QList<BinderItem *> items;
    for (BinderItem *it : p->allItems()) {
        if (!o.includeTrash && (p->isInTrash(it) || it->type == ItemType::Trash))
            continue;
        if (!o.scopeIds.isEmpty() && !scope.contains(it))
            continue;
        items << it;
    }
    QSet<QString> textFields;
    for (const CustomField &f : p->customFields)
        if (f.type == CustomFieldType::Text)
            textFields.insert(f.id);

    DocumentStore *docs = p->docs();
    int done = 0;
    for (BinderItem *it : items) {
        if (progress && !progress(done, int(items.size())))
            break;
        ++done;
        int here = 0;
        bool metaChanged = false;
        if (o.inTitles && !it->isRootFolder()) {
            const int n = inString(&it->title, re, o.with, o.regex, countOnly);
            r.titles += n;
            here += n;
            metaChanged |= n > 0;
        }
        if (o.inSynopses) {
            const int n = inString(&it->synopsis, re, o.with, o.regex, countOnly);
            r.synopses += n;
            here += n;
            metaChanged |= n > 0;
        }
        if (o.inCustom) {
            for (auto c = it->custom.begin(); c != it->custom.end(); ++c) {
                if (!textFields.contains(c.key()))
                    continue;
                const int n = inString(&c.value(), re, o.with, o.regex, countOnly);
                r.custom += n;
                here += n;
                metaChanged |= n > 0;
            }
        }
        if ((o.inText || o.inComments) && it->hasText()) {
            // look at the plain text first: most documents have no match and need not be read as rich text
            const bool maybeText = o.inText && re.match(docs->plainText(it->id)).hasMatch();
            if (maybeText || o.inComments) {
                int nText = 0, nComments = 0;
                bool snapshotTaken = false;
                const int res = docs->editStored(it->id, false, [&](QTextDocument *doc) {
                    if (o.inText)
                        nText = inDocument(doc, re, o.with, o.regex, true);
                    if (o.inComments)
                        nComments = inNoteTexts(doc, re, o.with, o.regex, true);
                    if (countOnly || nText + nComments == 0)
                        return nText + nComments;
                    if (o.snapshotFirst && snapshots::take(p, it->id, o.snapshotTitle, nullptr, doc))
                        snapshotTaken = true;
                    if (nText)
                        inDocument(doc, re, o.with, o.regex, false);
                    if (nComments)
                        inNoteTexts(doc, re, o.with, o.regex, false);
                    return nText + nComments;
                }, &r.error);
                if (res >= 0) {
                    r.texts += nText;
                    r.comments += nComments;
                    here += nText + nComments;
                    if (snapshotTaken)
                        ++r.snapshots;
                }
            }
        }
        if (o.inNotes && re.match(docs->notesPlainText(it->id)).hasMatch()) {
            const int res = docs->editStored(it->id, true, [&](QTextDocument *doc) {
                return inDocument(doc, re, o.with, o.regex, countOnly);
            }, &r.error);
            if (res > 0) {
                r.notes += res;
                here += res;
            }
        }
        if (here > 0)
            ++r.documents;
        if (metaChanged && !countOnly)
            p->notifyItemChanged(it, true);
    }
    if (progress)
        progress(int(items.size()), int(items.size()));
    if (!countOnly && r.total() > 0)
        p->scheduleSave();
    return r;
}

} // namespace replace
} // namespace qb
