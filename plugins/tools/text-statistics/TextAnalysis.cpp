#include "TextAnalysis.h"

#include <QObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace textstats {

// Words: letters, marks and digits, with an apostrophe or hyphen inside (don't, well-known).
static const QRegularExpression &wordRe()
{
    static const QRegularExpression re(QStringLiteral("[\\p{L}\\p{M}\\p{N}]+(?:['\\x{2019}\\-][\\p{L}\\p{M}\\p{N}]+)*"),
                                       QRegularExpression::UseUnicodePropertiesOption);
    return re;
}

QStringList tokenize(const QString &text)
{
    QStringList out;
    for (auto it = wordRe().globalMatch(text); it.hasNext();)
        out << it.next().captured(0);
    return out;
}

// Abbreviations after which a full stop does not end a sentence (lower case, without the stop).
static const QSet<QString> &abbreviations()
{
    static const QSet<QString> s = {
        // English
        QStringLiteral("mr"), QStringLiteral("mrs"), QStringLiteral("ms"), QStringLiteral("dr"), QStringLiteral("prof"),
        QStringLiteral("st"), QStringLiteral("vs"), QStringLiteral("etc"), QStringLiteral("e.g"), QStringLiteral("i.e"),
        QStringLiteral("no"), QStringLiteral("jr"), QStringLiteral("sr"), QStringLiteral("cf"), QStringLiteral("approx"),
        QStringLiteral("fig"), QStringLiteral("vol"), QStringLiteral("ch"), QStringLiteral("p"), QStringLiteral("pp"),
        // German
        QStringLiteral("z.b"), QStringLiteral("z"), QStringLiteral("b"), QStringLiteral("u.a"), QStringLiteral("d.h"),
        QStringLiteral("usw"), QStringLiteral("bzw"), QStringLiteral("ca"), QStringLiteral("vgl"), QStringLiteral("nr"),
        QStringLiteral("hr"), QStringLiteral("fr"), QStringLiteral("str"), QStringLiteral("evtl"), QStringLiteral("ggf"),
        QStringLiteral("inkl"), QStringLiteral("bspw"), QStringLiteral("s"), QStringLiteral("abs"), QStringLiteral("sog"),
        QStringLiteral("u"), QStringLiteral("d"), QStringLiteral("a"), QStringLiteral("o"), QStringLiteral("dgl")};
    return s;
}

QStringList sentences(const QString &para)
{
    QStringList out;
    const int n = int(para.size());
    int start = 0;
    auto isEnd = [](QChar c) {
        return c == QLatin1Char('.') || c == QLatin1Char('!') || c == QLatin1Char('?') || c == QChar(0x2026);
    };
    auto isCloser = [](QChar c) {
        return c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QChar(0x201D) || c == QChar(0x2019)
            || c == QChar(0x00BB) || c == QChar(0x00AB) || c == QChar(0x201C) || c == QLatin1Char(')') || c == QLatin1Char(']');
    };
    int i = 0;
    while (i < n) {
        if (!isEnd(para.at(i))) {
            ++i;
            continue;
        }
        int j = i;
        while (j < n && isEnd(para.at(j)))
            ++j;
        while (j < n && isCloser(para.at(j)))
            ++j;
        const bool atEnd = j >= n;
        const bool space = !atEnd && para.at(j).isSpace();
        if (!atEnd && !space) { // "3.5", "e.g.x", "www.example.org"
            i = j;
            continue;
        }
        // the word before a single full stop: an abbreviation?
        if (para.at(i) == QLatin1Char('.') && j == i + 1) {
            int w = i - 1;
            while (w >= 0 && (para.at(w).isLetter() || para.at(w) == QLatin1Char('.')))
                --w;
            const QString before = para.mid(w + 1, i - w - 1).toLower();
            if (!before.isEmpty() && (abbreviations().contains(before) || (before.size() == 1 && before.at(0).isUpper()))) {
                i = j;
                continue;
            }
            // a single capital letter (initials: "J. R. R. Tolkien")
            if (before.size() == 1) {
                i = j;
                continue;
            }
        }
        // a sentence goes on when the next word starts in lower case:
        // "Wait... what", "\"Wait!\" he said", "\"Where?\" she asked"
        int k = j;
        while (k < n && para.at(k).isSpace())
            ++k;
        if (k < n && para.at(k).isLower()) {
            i = j;
            continue;
        }
        const QString s = para.mid(start, j - start).trimmed();
        if (!s.isEmpty())
            out << s;
        start = j;
        i = j;
    }
    const QString rest = para.mid(start).trimmed();
    if (!rest.isEmpty())
        out << rest;
    return out;
}

int syllables(const QString &word, Language lang)
{
    const QString w = word.toLower();
    QString vowels = QStringLiteral("aeiouy");
    if (lang == Language::German)
        vowels = QStringLiteral("aeiouyäöü");
    else if (lang == Language::Other)
        vowels = QStringLiteral("aeiouyäöüáéíóúàèìòùâêîôûåæøœ");
    int count = 0;
    bool prev = false;
    int letters = 0;
    for (const QChar c : w) {
        if (!c.isLetter())
            continue;
        ++letters;
        const bool v = vowels.contains(c);
        if (v && !prev)
            ++count;
        prev = v;
    }
    if (lang == Language::English && count > 1) {
        // silent final e ("make"), but not "-le" after a consonant ("table") or "-ee"
        if (w.endsWith(QLatin1Char('e')) && !w.endsWith(QLatin1String("le")) && !w.endsWith(QLatin1String("ee")))
            --count;
        // "-ed" after other letters than t/d is mostly not a syllable ("walked")
        else if (w.endsWith(QLatin1String("ed")) && w.size() > 3 && !w.endsWith(QLatin1String("ted")) && !w.endsWith(QLatin1String("ded")))
            --count;
        // "-es" after most letters ("makes"), not after s, x, z, ch, sh ("boxes")
        else if (w.endsWith(QLatin1String("es")) && w.size() > 3 && !w.endsWith(QLatin1String("ses")) && !w.endsWith(QLatin1String("xes"))
                 && !w.endsWith(QLatin1String("zes")) && !w.endsWith(QLatin1String("ches")) && !w.endsWith(QLatin1String("shes"))
                 && !w.endsWith(QLatin1String("ces")) && !w.endsWith(QLatin1String("ges")))
            --count;
    }
    if (letters == 0)
        return 0;
    return qMax(1, count);
}

const QStringList &commonWords(Language lang)
{
    static const QStringList en = QStringLiteral(
        "a about above after again against all also am an and any are aren't as at be because been before being below "
        "between both but by can can't cannot could couldn't did didn't do does doesn't doing don't down during each few "
        "for from further had hadn't has hasn't have haven't having he he'd he'll he's her here here's hers herself him "
        "himself his how how's i i'd i'll i'm i've if in into is isn't it it's its itself just let's me more most mustn't "
        "my myself no nor not now of off on once only or other ought our ours ourselves out over own same shan't she "
        "she'd she'll she's should shouldn't so some such than that that's the their theirs them themselves then there "
        "there's these they they'd they'll they're they've this those through to too under until up upon us very was "
        "wasn't we we'd we'll we're we've were weren't what what's when when's where where's which while who who's whom "
        "why why's will with won't would wouldn't yet you you'd you'll you're you've your yours yourself yourselves "
        "one two said like back still even much many may might must shall again around into onto")
                                         .split(QLatin1Char(' '), Qt::SkipEmptyParts);
    static const QStringList de = QStringLiteral(
        "aber alle allem allen aller alles als also am an ander andere anderem anderen anderer anderes anders auch auf aus "
        "bei beim bin bis bist da dabei dadurch dafür dagegen daher damit dann daran darauf darum das dass dein deine "
        "deinem deinen deiner dem den denn der derselbe des desselben dessen dich die dies diese diesem diesen dieser "
        "dieses dir doch dort du durch ein eine einem einen einer eines einige einmal er es etwas euch euer eure für gegen "
        "gewesen hab habe haben hat hatte hatten hier hin hinter ich ihm ihn ihnen ihr ihre ihrem ihren ihrer ihres im in "
        "indem ins ist jede jedem jeden jeder jedes jene jenem jenen jener jenes jetzt kann kein keine keinem keinen keiner "
        "können könnte machen man manche manchem manchen mancher manches mein meine meinem meinen meiner mich mir mit "
        "muss musste nach nicht nichts noch nun nur ob oder ohne schon sehr sein seine seinem seinen seiner selbst sich "
        "sie sind so solche solchem solchen solcher soll sollte sondern sonst über um und uns unser unsere unter viel vom "
        "von vor war waren warst was weg weil weiter welche welchem welchen welcher welches wenn werde werden wie wieder "
        "will wir wird wirst wo wollen wollte würde würden zu zum zur zwar zwischen")
                                         .split(QLatin1Char(' '), Qt::SkipEmptyParts);
    static const QStringList none;
    if (lang == Language::English)
        return en;
    if (lang == Language::German)
        return de;
    return none;
}

bool isCommon(const QString &lowerWord, Language lang)
{
    static QSet<QString> en, de;
    if (en.isEmpty()) {
        for (const QString &w : commonWords(Language::English))
            en.insert(w);
        for (const QString &w : commonWords(Language::German))
            de.insert(w);
    }
    QString w = lowerWord;
    w.replace(QChar(0x2019), QLatin1Char('\''));
    if (lang == Language::English)
        return en.contains(w);
    if (lang == Language::German)
        return de.contains(w);
    return false;
}

Language detect(const QString &sample)
{
    const QStringList toks = tokenize(sample.left(40000));
    if (toks.size() < 5)
        return Language::Other;
    int en = 0, de = 0;
    for (const QString &t : toks) {
        const QString l = t.toLower();
        if (isCommon(l, Language::English))
            ++en;
        if (isCommon(l, Language::German))
            ++de;
    }
    const double n = toks.size();
    if (en / n < 0.08 && de / n < 0.08)
        return Language::Other;
    return en >= de ? Language::English : Language::German;
}

Result analyze(const QList<Input> &inputs, Language lang, const std::function<int(const QString &)> &countWords)
{
    Result r;
    QString sample;
    for (const Input &in : inputs) {
        if (sample.size() > 40000)
            break;
        sample += in.text + QLatin1Char('\n');
    }
    r.language = lang == Language::Auto ? detect(sample) : lang;
    for (const Input &in : inputs) {
        DocStats d;
        d.id = in.id;
        d.title = in.title;
        d.path = in.path;
        d.words = countWords ? countWords(in.text) : int(tokenize(in.text).size());
        for (const QChar c : in.text)
            if (c != QLatin1Char('\n')) {
                ++d.chars;
                if (!c.isSpace())
                    ++r.charsNoSpaces;
            }
        r.words += d.words;
        r.chars += d.chars;
        const QStringList paras = in.text.split(QLatin1Char('\n'));
        for (const QString &p : paras) {
            if (p.trimmed().isEmpty())
                continue;
            ++r.paragraphs;
            for (const QString &s : sentences(p)) {
                const QStringList toks = tokenize(s);
                if (toks.isEmpty())
                    continue;
                ++d.sentences;
                if (toks.size() > r.longestSentence) {
                    r.longestSentence = int(toks.size());
                    r.longestSentenceText = s.size() > 160 ? s.left(157) + QStringLiteral("…") : s;
                }
                for (const QString &t : toks) {
                    ++r.tokens;
                    int letters = 0;
                    for (const QChar c : t)
                        if (c.isLetter())
                            ++letters;
                    r.letters += letters;
                    if (letters > 6)
                        ++r.longWords;
                    const int sy = syllables(t, r.language);
                    r.syllables += sy;
                    if (sy == 1)
                        ++r.monosyllables;
                    if (sy >= 3)
                        ++r.polysyllables;
                    const QString low = t.toLower();
                    ++r.counts[low];
                    ++r.exact[t];
                }
            }
        }
        r.sentences += d.sentences;
        r.docs << d;
    }
    // the spelling that is used most for each word
    for (auto it = r.exact.cbegin(); it != r.exact.cend(); ++it) {
        const QString low = it.key().toLower();
        const QString cur = r.shown.value(low);
        if (cur.isEmpty() || r.exact.value(cur) < it.value())
            r.shown[low] = it.key();
    }
    r.uniqueWords = int(r.counts.size());
    if (r.tokens >= 10 && r.sentences > 0) {
        const double wps = double(r.tokens) / r.sentences;
        const double spw = double(r.syllables) / r.tokens;
        r.fleschEase = 206.835 - 1.015 * wps - 84.6 * spw;
        r.fleschKincaid = 0.39 * wps + 11.8 * spw - 15.59;
        r.amstad = 180.0 - wps - 58.5 * spw;
        const double ms = 100.0 * r.polysyllables / r.tokens;
        const double iw = 100.0 * r.longWords / r.tokens;
        const double es = 100.0 * r.monosyllables / r.tokens;
        r.wstf = 0.1935 * ms + 0.1672 * wps + 0.1297 * iw - 0.0327 * es - 0.875;
        r.lix = wps + 100.0 * r.longWords / r.tokens;
        r.hasReadability = true;
    }
    return r;
}

QList<QPair<QString, int>> frequency(const Result &r, bool hideCommon, int minLength, bool caseSensitive)
{
    QList<QPair<QString, int>> out;
    const QHash<QString, int> &src = caseSensitive ? r.exact : r.counts;
    out.reserve(src.size());
    for (auto it = src.cbegin(); it != src.cend(); ++it) {
        const QString low = it.key().toLower();
        if (hideCommon && isCommon(low, r.language))
            continue;
        if (it.key().size() < minLength)
            continue;
        bool digitsOnly = true;
        for (const QChar c : it.key())
            if (!c.isDigit())
                digitsOnly = false;
        if (hideCommon && digitsOnly)
            continue;
        out << qMakePair(caseSensitive ? it.key() : r.shown.value(low, it.key()), it.value());
    }
    std::sort(out.begin(), out.end(), [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
        if (a.second != b.second)
            return a.second > b.second;
        return a.first.localeAwareCompare(b.first) < 0;
    });
    return out;
}

QList<QPair<QString, int>> phrases(const QList<Input> &inputs, Language lang, int minCount, int maxWords, int limit)
{
    maxWords = qBound(2, maxWords, 6);
    QHash<QString, int> counts;
    QHash<QString, QString> shownForm;
    for (const Input &in : inputs) {
        for (const QString &p : in.text.split(QLatin1Char('\n'))) {
            if (p.trimmed().isEmpty())
                continue;
            for (const QString &s : sentences(p)) {
                const QStringList toks = tokenize(s);
                QStringList low;
                low.reserve(toks.size());
                for (const QString &t : toks)
                    low << t.toLower();
                for (int n = 2; n <= maxWords; ++n) {
                    for (int i = 0; i + n <= toks.size(); ++i) {
                        bool allCommon = true;
                        for (int k = i; k < i + n; ++k)
                            if (!isCommon(low.at(k), lang)) {
                                allCommon = false;
                                break;
                            }
                        if (allCommon)
                            continue;
                        const QString key = low.mid(i, n).join(QLatin1Char(' '));
                        if (++counts[key] == 1)
                            shownForm.insert(key, toks.mid(i, n).join(QLatin1Char(' ')));
                    }
                }
            }
        }
    }
    QList<QPair<QString, int>> kept;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        if (it.value() >= minCount)
            kept << qMakePair(it.key(), it.value());
    // drop a phrase that only ever occurs inside a longer one ("the old" inside "the old man")
    QSet<QString> drop;
    for (const auto &longer : kept) {
        const QStringList w = longer.first.split(QLatin1Char(' '));
        if (w.size() < 3)
            continue;
        for (int n = 2; n < w.size(); ++n)
            for (int i = 0; i + n <= w.size(); ++i) {
                const QString sub = w.mid(i, n).join(QLatin1Char(' '));
                if (counts.value(sub) == longer.second)
                    drop.insert(sub);
            }
    }
    QList<QPair<QString, int>> out;
    for (const auto &k : kept)
        if (!drop.contains(k.first))
            out << qMakePair(shownForm.value(k.first, k.first), k.second);
    std::sort(out.begin(), out.end(), [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
        if (a.second != b.second)
            return a.second > b.second;
        const int la = int(a.first.count(QLatin1Char(' '))), lb = int(b.first.count(QLatin1Char(' ')));
        if (la != lb)
            return la > lb;
        return a.first.localeAwareCompare(b.first) < 0;
    });
    if (out.size() > limit)
        out = out.mid(0, limit);
    return out;
}

QString ease(double f)
{
    if (f >= 90)
        return QObject::tr("very easy");
    if (f >= 80)
        return QObject::tr("easy");
    if (f >= 70)
        return QObject::tr("fairly easy");
    if (f >= 60)
        return QObject::tr("standard");
    if (f >= 50)
        return QObject::tr("fairly difficult");
    if (f >= 30)
        return QObject::tr("difficult");
    return QObject::tr("very difficult");
}

} // namespace textstats
