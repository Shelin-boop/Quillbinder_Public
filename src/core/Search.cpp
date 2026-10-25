#include "core/Search.h"
#include "core/BinderItem.h"
#include "core/DocumentStore.h"
#include "core/Project.h"

#include "text/TextUtil.h"

#include <QRegularExpression>
#include <QSet>

namespace qb {

namespace {
const QRegularExpression::PatternOptions kUnicode = QRegularExpression::UseUnicodePropertiesOption;

QString wholeWordPattern(const QString &pat)
{
    // not \b: a term may begin or end with something that is not a letter
    return QStringLiteral("(?<![\\w])(?:") + pat + QStringLiteral(")(?![\\w])");
}

QStringList termPatterns(const SearchQuery &q)
{
    QStringList out;
    if (q.regex) {
        if (!q.text.isEmpty())
            out << q.text;
        return out;
    }
    QStringList terms;
    if (q.exactPhrase)
        terms << q.text.trimmed();
    else
        terms = q.text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &t : terms) {
        if (t.isEmpty())
            continue;
        const QString pat = QRegularExpression::escape(t);
        out << (q.wholeWord ? wholeWordPattern(pat) : pat);
    }
    return out;
}

QRegularExpression::PatternOptions optionsFor(bool caseSensitive)
{
    return caseSensitive ? kUnicode : (kUnicode | QRegularExpression::CaseInsensitiveOption);
}

struct Matcher {
    QList<QRegularExpression> res;
    bool any = false;

    Matcher(const SearchQuery &q)
    {
        for (const QString &pat : termPatterns(q)) {
            QRegularExpression re(pat, optionsFor(q.caseSensitive));
            if (re.isValid())
                res << re;
        }
        any = q.anyWord && !q.exactPhrase;
    }
    // returns bitmask of matched terms
    quint64 match(const QString &s) const
    {
        quint64 m = 0;
        for (int i = 0; i < res.size() && i < 64; ++i)
            if (res[i].match(s).hasMatch())
                m |= (quint64(1) << i);
        return m;
    }
    bool complete(quint64 m) const
    {
        if (res.isEmpty())
            return false;
        if (any)
            return m != 0;
        const int n = qMin(64, int(res.size()));
        const quint64 all = (n == 64) ? ~quint64(0) : ((quint64(1) << n) - 1);
        return (m & all) == all;
    }
};
} // namespace

QList<BinderItem *> searchProject(Project *p, const SearchQuery &q)
{
    QList<BinderItem *> out;
    if (q.text.trimmed().isEmpty())
        return out;
    const Matcher m(q);
    if (m.res.isEmpty())
        return out;
    QSet<BinderItem *> scope;
    for (const QString &id : q.scopeIds)
        if (BinderItem *s = p->item(id))
            s->forEach([&scope](BinderItem *i) { scope.insert(i); });
    const bool scoped = !q.scopeIds.isEmpty();
    for (BinderItem *it : p->allItems()) {
        if (!q.includeTrash && p->isInTrash(it))
            continue;
        if (it->isRootFolder())
            continue;
        if (scoped && !scope.contains(it))
            continue;
        if (q.draftOnly && !(p->draft() && it->isDescendantOf(p->draft())))
            continue;
        if ((q.compile == 1 && !it->includeInCompile) || (q.compile == 2 && it->includeInCompile))
            continue;
        quint64 found = 0;
        if (q.inTitle)
            found |= m.match(it->title);
        if (q.inSynopsis && !m.complete(found))
            found |= m.match(it->synopsis);
        if (q.inKeywords && !m.complete(found)) {
            for (const QString &k : it->keywords)
                if (const Keyword *kw = p->keywordById(k))
                    found |= m.match(kw->name);
        }
        if (q.inLabel && !m.complete(found))
            if (const Label *l = p->labelById(it->label))
                found |= m.match(l->name);
        if (q.inStatus && !m.complete(found))
            if (const Status *s = p->statusById(it->status))
                found |= m.match(s->name);
        if (q.inCustom && !m.complete(found))
            for (const QString &v : it->custom)
                found |= m.match(v);
        if (q.inText && it->hasText() && !m.complete(found))
            found |= m.match(p->docs()->plainText(it->id));
        if (q.inNotes && !m.complete(found))
            found |= m.match(p->docs()->notesPlainText(it->id));
        if (m.complete(found) != q.invert)
            out << it;
    }
    return out;
}

QList<BinderItem *> quickSearch(Project *p, const QString &text, int max)
{
    QList<BinderItem *> titleHits, otherHits;
    const QString t = text.trimmed();
    if (t.isEmpty())
        return titleHits;
    for (BinderItem *it : p->allItems()) {
        if (p->isInTrash(it) || it->isRootFolder())
            continue;
        if (it->title.contains(t, Qt::CaseInsensitive))
            titleHits << it;
        else if (it->synopsis.contains(t, Qt::CaseInsensitive))
            otherHits << it;
        if (titleHits.size() >= max)
            break;
    }
    if (titleHits.size() < max) {
        for (BinderItem *it : p->allItems()) {
            if (titleHits.size() + otherHits.size() >= max)
                break;
            if (p->isInTrash(it) || it->isRootFolder() || titleHits.contains(it) || otherHits.contains(it))
                continue;
            if (it->hasText() && p->docs()->plainText(it->id).contains(t, Qt::CaseInsensitive))
                otherHits << it;
        }
    }
    return (titleHits + otherHits).mid(0, max);
}

QList<QuickHit> quickSearchGrouped(Project *p, const QString &text, int perKind)
{
    QList<QuickHit> titles, synopses, texts;
    SearchQuery q;
    q.text = text;
    const Matcher m(q);
    if (m.res.isEmpty())
        return titles;
    const QRegularExpression first = m.res.first();
    const QList<BinderItem *> items = p->allItems();
    QSet<BinderItem *> taken;
    auto usable = [&](BinderItem *it) { return !it->isRootFolder() && !p->isInTrash(it) && !taken.contains(it); };
    for (BinderItem *it : items) {
        if (titles.size() >= perKind)
            break;
        if (usable(it) && m.complete(m.match(it->title))) {
            titles.append({it, QuickHit::Title, QString()});
            taken.insert(it);
        }
    }
    for (BinderItem *it : items) {
        if (synopses.size() >= perKind)
            break;
        if (usable(it) && m.complete(m.match(it->synopsis))) {
            synopses.append({it, QuickHit::Synopsis, search::excerpt(it->synopsis, first)});
            taken.insert(it);
        }
    }
    for (BinderItem *it : items) {
        if (texts.size() >= perKind)
            break;
        if (!usable(it) || !it->hasText())
            continue;
        const QString plain = p->docs()->plainText(it->id);
        if (m.complete(m.match(plain))) {
            texts.append({it, QuickHit::Text, search::excerpt(plain, first)});
            taken.insert(it);
        }
    }
    return titles + synopses + texts;
}

namespace search {

QRegularExpression pattern(const SearchQuery &q)
{
    const QStringList pats = termPatterns(q);
    if (pats.isEmpty())
        return QRegularExpression(QStringLiteral("(?"));   // invalid: finds nothing
    QStringList parts;
    for (const QString &pt : pats)
        parts << QStringLiteral("(?:") + pt + QLatin1Char(')');
    return QRegularExpression(parts.join(QLatin1Char('|')), optionsFor(q.caseSensitive));
}

QString patternError(const SearchQuery &q)
{
    if (!q.regex || q.text.isEmpty())
        return QString();
    const QRegularExpression re(q.text, optionsFor(q.caseSensitive));
    return re.isValid() ? QString() : re.errorString();
}

QRegularExpression pattern(const QString &text, bool caseSensitive, bool wholeWord, bool regex)
{
    if (text.isEmpty())
        return QRegularExpression(QStringLiteral("(?"));   // invalid: finds nothing
    QString pat = regex ? text : QRegularExpression::escape(text);
    if (wholeWord)
        pat = wholeWordPattern(pat);
    return QRegularExpression(pat, optionsFor(caseSensitive));
}

QString excerpt(const QString &text, const QRegularExpression &re, int context)
{
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch())
        return QString();
    const int from = qMax(0, int(m.capturedStart()) - context);
    const int to = qMin(int(text.size()), int(m.capturedEnd()) + context);
    QString s = text.mid(from, to - from).simplified();
    if (from > 0)
        s.prepend(QStringLiteral("…"));
    if (to < text.size())
        s.append(QStringLiteral("…"));
    return s;
}

QVariantMap toMap(const SearchQuery &q)
{
    QVariantMap m;
    m[QStringLiteral("text")] = q.text;
    QString in;
    auto f = [&in](bool v, QChar c) { if (v) in += c; };
    f(q.inTitle, QLatin1Char('t'));
    f(q.inText, QLatin1Char('x'));
    f(q.inSynopsis, QLatin1Char('s'));
    f(q.inNotes, QLatin1Char('n'));
    f(q.inKeywords, QLatin1Char('k'));
    f(q.inLabel, QLatin1Char('l'));
    f(q.inStatus, QLatin1Char('u'));
    f(q.inCustom, QLatin1Char('c'));
    f(q.wholeWord, QLatin1Char('w'));
    f(q.caseSensitive, QLatin1Char('C'));
    f(q.exactPhrase, QLatin1Char('p'));
    f(q.includeTrash, QLatin1Char('T'));
    f(q.anyWord, QLatin1Char('a'));
    f(q.regex, QLatin1Char('r'));
    f(q.draftOnly, QLatin1Char('D'));
    f(q.compile == 1, QLatin1Char('I'));
    f(q.compile == 2, QLatin1Char('E'));
    f(q.invert, QLatin1Char('V'));
    m[QStringLiteral("flags")] = in;
    if (!q.scopeIds.isEmpty())
        m[QStringLiteral("scope")] = q.scopeIds;
    return m;
}

SearchQuery fromMap(const QVariantMap &m)
{
    SearchQuery q;
    q.text = m.value(QStringLiteral("text")).toString();
    if (!m.contains(QStringLiteral("flags")))
        return q;
    const QString flags = m.value(QStringLiteral("flags")).toString();
    q.inTitle = flags.contains(QLatin1Char('t'));
    q.inText = flags.contains(QLatin1Char('x'));
    q.inSynopsis = flags.contains(QLatin1Char('s'));
    q.inNotes = flags.contains(QLatin1Char('n'));
    q.inKeywords = flags.contains(QLatin1Char('k'));
    q.inLabel = flags.contains(QLatin1Char('l'));
    q.inStatus = flags.contains(QLatin1Char('u'));
    q.inCustom = flags.contains(QLatin1Char('c'));
    q.wholeWord = flags.contains(QLatin1Char('w'));
    q.caseSensitive = flags.contains(QLatin1Char('C'));
    q.exactPhrase = flags.contains(QLatin1Char('p'));
    q.allWords = !q.exactPhrase;
    q.includeTrash = flags.contains(QLatin1Char('T'));
    q.anyWord = flags.contains(QLatin1Char('a')) && !q.exactPhrase;
    q.regex = flags.contains(QLatin1Char('r'));
    q.draftOnly = flags.contains(QLatin1Char('D'));
    q.compile = flags.contains(QLatin1Char('I')) ? 1 : flags.contains(QLatin1Char('E')) ? 2 : 0;
    q.invert = flags.contains(QLatin1Char('V'));
    q.scopeIds = m.value(QStringLiteral("scope")).toStringList();
    return q;
}

bool matches(Project *p, BinderItem *it, const ItemFilter &f)
{
    if (!f.isActive())
        return true;
    bool ok = true;
    if (f.label != -2 && it->label != f.label)
        ok = false;
    if (ok && f.status != -2 && it->status != f.status)
        ok = false;
    if (ok && !f.keyword.isEmpty() && !it->keywords.contains(f.keyword))
        ok = false;
    if (ok && ((f.compile == 1 && !it->includeInCompile) || (f.compile == 2 && it->includeInCompile)))
        ok = false;
    const QStringList words = f.text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (ok && !words.isEmpty()) {
        // every word must be found in one of the chosen places
        QString hay;
        if (f.inTitle)
            hay += it->title + QLatin1Char('\n');
        if (f.inSynopsis)
            hay += it->synopsis + QLatin1Char('\n');
        if (f.inKeywords)
            for (const QString &k : it->keywords)
                if (const Keyword *kw = p->keywordById(k))
                    hay += kw->name + QLatin1Char('\n');
        if (f.inText && it->hasText())
            hay += p->docs()->plainText(it->id);
        for (const QString &w : words)
            if (!hay.contains(w, Qt::CaseInsensitive)) {
                ok = false;
                break;
            }
    }
    return ok != f.invert;
}

} // namespace search

} // namespace qb
