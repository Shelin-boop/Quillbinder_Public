// Corrections while typing; see AutoCorrect.h for what and why.
#include "text/AutoCorrect.h"
#include "text/TextUtil.h"

#include <QRegularExpression>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace qb {
namespace autocorrect {

namespace {

// Characters that may stand before a word and still belong to it ("“Well").
bool isOpening(QChar c)
{
    static const QString open = QStringLiteral("\"'“‘„‚«»‹›([{");
    return open.contains(c);
}

// Characters that may follow a word or a sentence's end (”, ), ...).
bool isClosing(QChar c)
{
    static const QString close = QStringLiteral("\"'”’“‘»«›‹)]}");
    return close.contains(c);
}

bool isTrailingPunctuation(QChar c)
{
    static const QString punct = QStringLiteral(".,;:!?…");
    return punct.contains(c) || isClosing(c);
}

// Abbreviations after which a period does not end the sentence (English and
// German, lower case). Words with an inner period ("e.g.") and single letters
// ("J.") are recognized without the list.
bool isAbbreviation(const QString &token)
{
    static const QStringList list = {QStringLiteral("etc."),  QStringLiteral("vs."),  QStringLiteral("mr."),  QStringLiteral("mrs."),
                                     QStringLiteral("ms."),   QStringLiteral("dr."),  QStringLiteral("prof."), QStringLiteral("st."),
                                     QStringLiteral("no."),   QStringLiteral("nr."),  QStringLiteral("ca."),  QStringLiteral("bzw."),
                                     QStringLiteral("vgl."),  QStringLiteral("hr."),  QStringLiteral("fr."),  QStringLiteral("jr."),
                                     QStringLiteral("sr."),   QStringLiteral("fig."), QStringLiteral("p."),   QStringLiteral("pp."),
                                     QStringLiteral("usw."),  QStringLiteral("evtl."), QStringLiteral("inkl."), QStringLiteral("ggf.")};
    QString t = token;
    while (!t.isEmpty() && isOpening(t.at(0)))
        t.remove(0, 1);
    if (t.size() <= 2)   // "J." and "a."
        return true;
    if (t.left(t.size() - 1).contains(QLatin1Char('.')))   // "e.g.", "z.B.", "a.m."
        return true;
    bool digits = true;
    for (int i = 0; i < t.size() - 1; ++i)
        digits = digits && t.at(i).isDigit();
    if (digits)   // "3." (a German ordinal) or the end of a number
        return true;
    return list.contains(t.toLower());
}

void replaceRange(QTextCursor &doc, int from, int to, const QString &with)
{
    QTextCursor r(doc.document());
    r.setPosition(from);
    r.setPosition(to, QTextCursor::KeepAnchor);
    // the replacement keeps the formatting of the replaced text (a bold first letter stays bold)
    const QTextCharFormat f = r.charFormat();
    r.insertText(with, f);
}

} // namespace

QString quoteChars(const QString &style)
{
    if (style == QLatin1String("de"))
        return QStringLiteral("„“‚‘");
    if (style == QLatin1String("de2"))
        return QStringLiteral("»«›‹");
    if (style == QLatin1String("fr"))
        return QStringLiteral("«»‹›");
    if (style == QLatin1String("sv"))
        return QStringLiteral("””’’");
    return QStringLiteral("“”‘’");
}

QChar smartQuote(QChar typed, QChar before, const QString &style)
{
    const QString q = quoteChars(style);
    const bool opening = before.isNull() || before.isSpace() || before == QLatin1Char('(') || before == QLatin1Char('[')
                         || before == QChar(0x2014) || before == QChar(0x2013) || before == q.at(0) || before == q.at(2);
    if (typed == QLatin1Char('"'))
        return opening ? q.at(0) : q.at(1);
    if (typed == QLatin1Char('\'')) {
        // "don't", "Peter's": an apostrophe, whatever the style
        if (before.isLetterOrNumber())
            return QChar(0x2019);
        return opening ? q.at(2) : q.at(3);
    }
    return typed;
}

bool sentenceStartsAt(const QString &text, int pos)
{
    int i = pos - 1;
    while (i >= 0 && text.at(i).isSpace())
        --i;
    if (i < 0)
        return true;   // the first word of the paragraph
    if (i == pos - 1)
        return false;  // no space before: inside a word
    while (i >= 0 && isClosing(text.at(i)))
        --i;
    if (i < 0)
        return false;
    const QChar end = text.at(i);
    if (end == QLatin1Char('!') || end == QLatin1Char('?') || end == QChar(0x2026))
        return true;
    if (end != QLatin1Char('.'))
        return false;
    int ts = i;
    while (ts > 0 && !text.at(ts - 1).isSpace())
        --ts;
    return !isAbbreviation(text.mid(ts, i - ts + 1));
}

QStringList afterWord(QTextCursor &cursor, const Options &o)
{
    QStringList done;
    if (cursor.hasSelection() || !(o.capSentences || o.capI || o.urls || o.substitutions))
        return done;
    const QTextBlock block = cursor.block();
    const int base = block.position();
    QString text = block.text();
    int end = cursor.positionInBlock();
    int start = end;
    while (start > 0 && !text.at(start - 1).isSpace() && text.at(start - 1) != QChar::ObjectReplacementCharacter)
        --start;
    if (start == end)
        return done;
    {
        // nothing is corrected inside a link
        QTextCursor at(cursor.document());
        at.setPosition(base + start + 1);
        if (at.charFormat().isAnchor())
            return done;
    }
    QTextCursor edit(cursor);
    edit.beginEditBlock();

    // 1. the substitution list: the longest typed text that ends the word
    if (o.substitutions) {
        const QString token = text.mid(start, end - start);
        int bestAt = -1, bestLen = 0;
        QString bestWith;
        for (const auto &s : o.substitutionList) {
            const QString &from = s.first;
            if (from.isEmpty() || from.size() <= bestLen || !token.endsWith(from))
                continue;
            const int at = end - int(from.size());
            const bool boundary = at == start || !text.at(at - 1).isLetterOrNumber() || !from.at(0).isLetterOrNumber();
            if (!boundary)
                continue;
            bestAt = at;
            bestLen = int(from.size());
            bestWith = s.second;
        }
        if (bestAt >= 0 && text.mid(bestAt, bestLen) != bestWith) {
            replaceRange(edit, base + bestAt, base + bestAt + bestLen, bestWith);
            text = cursor.block().text();
            end = bestAt + int(bestWith.size());
            done << QStringLiteral("substitution");
        }
    }

    // 2. a web address becomes a link (the punctuation after it stays outside)
    if (o.urls) {
        int len = end - start;
        while (len > 0 && isTrailingPunctuation(text.at(start + len - 1)))
            --len;
        const QString url = text.mid(start, len);
        static const QRegularExpression re(QStringLiteral("^(https?://\\S+|www\\.[^\\s.]+\\.\\S+)$"), QRegularExpression::CaseInsensitiveOption);
        if (len > 0 && re.match(url).hasMatch()) {
            const QString href = url.startsWith(QLatin1String("www."), Qt::CaseInsensitive) ? QStringLiteral("http://") + url : url;
            QTextCursor r(cursor.document());
            r.setPosition(base + start);
            r.setPosition(base + start + len, QTextCursor::KeepAnchor);
            r.mergeCharFormat(text::linkFormat(href));
            done << QStringLiteral("link");
            edit.endEditBlock();
            return done;   // an address is not a word to capitalize
        }
    }

    // the word without quotes and brackets before it and punctuation after it
    int cs = start;
    while (cs < end && isOpening(text.at(cs)))
        ++cs;
    int ce = end;
    while (ce > cs && isTrailingPunctuation(text.at(ce - 1)))
        --ce;
    const QString core = text.mid(cs, ce - cs);

    // 3. "i" -> "I" (also "i'm", "i’ve" ...)
    if (o.capI && !core.isEmpty()) {
        static const QRegularExpression iRe(QStringLiteral("^i(['’](m|d|ll|ve))?$"));
        if (iRe.match(core).hasMatch()) {
            replaceRange(edit, base + cs, base + cs + 1, QStringLiteral("I"));
            done << QStringLiteral("capital I");
        }
    }

    // 4. the first word of a sentence gets a capital letter
    if (o.capSentences && !core.isEmpty() && core.at(0).isLower() && done.indexOf(QStringLiteral("capital I")) < 0) {
        bool plain = true;
        for (const QChar ch : core)
            if (ch.isDigit() || ch == QLatin1Char('.') || ch == QLatin1Char('@') || ch == QLatin1Char('/') || ch == QLatin1Char('\\')
                || ch == QLatin1Char('_'))
                plain = false;
        if (plain && sentenceStartsAt(text, start)) {
            replaceRange(edit, base + cs, base + cs + 1, core.left(1).toUpper());
            done << QStringLiteral("capital");
        }
    }
    edit.endEditBlock();
    return done;
}

} // namespace autocorrect
} // namespace qb
