#include "Compare.h"

#include "compile/CompileFormat.h"
#include "compile/Compiler.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/Snapshots.h"
#include "import/CompareFiles.h"
#include "text/Styles.h"
#include "text/TextProps.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>
#include <functional>

namespace qb {
namespace compare {

// ---------------------------------------------------------------- words

static bool isWordChar(QChar c) { return c.isLetterOrNumber() || c.isMark(); }
static bool isApostrophe(QChar c)
{
    return c == QLatin1Char('\'') || c == QChar(0x2019) || c == QChar(0x2018) || c == QChar(0x02BC);
}
static bool isDash(QChar c)
{
    const ushort u = c.unicode();
    return u == '-' || (u >= 0x2010 && u <= 0x2015) || u == 0x2212;
}
static bool isQuote(QChar c)
{
    const ushort u = c.unicode();
    return u == '"' || u == 0x201C || u == 0x201D || u == 0x201E || u == 0x201F || u == 0x00AB || u == 0x00BB || u == 0x2033;
}

QVector<Token> tokenize(const Para &p)
{
    QVector<Token> out;
    const QString &s = p.text;
    int i = 0;
    while (i < s.size()) {
        const QChar c = s.at(i);
        if (c.isSpace() || c == QChar(0x00A0) || c == QChar(0xFFFC)) {
            ++i;
            continue;
        }
        Token t;
        t.start = i;
        t.fmt = i < p.fmt.size() ? p.fmt.at(i) : 0;
        int j = i + 1;
        if (isWordChar(c)) {
            while (j < s.size() && (isWordChar(s.at(j)) || (isApostrophe(s.at(j)) && j + 1 < s.size() && isWordChar(s.at(j + 1)))))
                ++j;
            QString w = s.mid(i, j - i).normalized(QString::NormalizationForm_KC);
            for (QChar &ch : w)
                if (isApostrophe(ch))
                    ch = QLatin1Char('\'');
            t.key = w;
            t.word = true;
        } else if (isDash(c)) {
            while (j < s.size() && isDash(s.at(j)))
                ++j;
            t.key = QStringLiteral("-");
        } else if (c == QLatin1Char('.') || c == QChar(0x2026)) {
            bool ell = c == QChar(0x2026);
            while (j < s.size() && (s.at(j) == QLatin1Char('.') || s.at(j) == QChar(0x2026))) {
                ell = true;
                ++j;
            }
            t.key = ell ? QStringLiteral("...") : QStringLiteral(".");
        } else if (isQuote(c)) {
            t.key = QStringLiteral("\"");
        } else if (isApostrophe(c) || c == QChar(0x201A) || c == QChar(0x201B) || c == QChar(0x2032)) {
            t.key = QStringLiteral("'");
        } else {
            t.key = QString(c).normalized(QString::NormalizationForm_KC);
        }
        t.end = j;
        out.append(t);
        i = j;
    }
    return out;
}

QStringList normalizedWords(const QString &text)
{
    Para p;
    p.text = text;
    QStringList out;
    for (const Token &t : tokenize(p))
        if (t.word)
            out << t.key.toLower();
    return out;
}

int Para::words() const
{
    int n = 0;
    for (const Token &t : tokenize(*this))
        if (t.word)
            ++n;
    return n;
}

int Pair::words() const
{
    int n = 0;
    for (const Para &p : project)
        n += p.words();
    for (const Para &p : file)
        n += p.words();
    return n;
}

int Pair::changedPlaces() const { return int(changes.size()); }

int Result::changeCount() const
{
    int n = 0;
    for (const Pair &p : pairs)
        n += p.kind == Pair::Matched ? int(p.changes.size()) : 1;
    return n;
}

static QString paraKey(const Para &p)
{
    QString k;
    for (const Token &t : tokenize(p)) {
        k += t.key;
        k += QChar(0x1F);
    }
    return k;
}

// ---------------------------------------------------------------- the project side

QList<Para> projectParagraphs(QTextDocument *doc, const Formatting &f)
{
    QList<Para> out;
    int n = 0;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next(), ++n) {
        Para p;
        p.source = n;
        const QString styleName = b.blockFormat().stringProperty(props::Style);
        const Style *ps = styles::find(f, styleName);
        static const QRegularExpression heading(QStringLiteral("^Heading (\\d)$"));
        const auto m = heading.match(styleName);
        if (m.hasMatch())
            p.heading = m.captured(1).toInt();
        else if (b.blockFormat().headingLevel() > 0)
            p.heading = b.blockFormat().headingLevel();
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            if (!fr.isValid())
                continue;
            const QTextCharFormat cf = fr.charFormat();
            if (cf.isImageFormat() || isInlineNote(NoteKind(cf.intProperty(props::NoteKind))))
                continue;
            const QTextCharFormat own = styles::withoutStyles(cf, ps, f);
            quint8 fl = 0;
            if (own.fontWeight() >= QFont::DemiBold)
                fl |= Bold;
            if (own.fontItalic())
                fl |= Italic;
            if (own.fontUnderline() && !cf.isAnchor())
                fl |= Underline;
            const QString t = fr.text();
            for (int i = 0; i < t.size(); ++i) {
                QChar c = t.at(i);
                if (c == QChar(0xFFFC))
                    continue;
                if (c == QChar(0x2028) || c == QChar(0x2029))
                    c = QLatin1Char('\n');
                p.text += c;
                p.fmt.append(fl);
                p.pos.append(fr.position() + i);
            }
        }
        out << p;
    }
    return out;
}

static QString manuscriptText(const compile::Manuscript &m)
{
    QString out;
    for (const compile::Paragraph &p : m.paragraphs) {
        for (const compile::Run &r : p.runs)
            if (r.kind == compile::Run::Text)
                out += r.text;
        out += QLatin1Char('\n');
    }
    return out;
}

QString compiledText(Project *p, QStringList *docIds)
{
    const compile::Format fmt = compile::formatByName(p->compile.formatName);
    compile::Compiler c(p, fmt, p->compile, {});
    if (docIds) {
        docIds->clear();
        for (const compile::CompileEntry &e : c.entries())
            // front and back matter (title page ...) are made by Compile: not compared
            if (e.included && e.part == compile::CompileEntry::Main && e.item && e.item->hasText() && !docIds->contains(e.item->id))
                *docIds << e.item->id;
    }
    return manuscriptText(c.build());
}

// ---------------------------------------------------------------- differences

struct DOp {
    char type;   // 'e' equal, 'd' only in a, 'i' only in b
    int a, b, n;
};

// Myers' O((N+M)D) difference of two sequences given by an equality test
static QList<DOp> diffSeq(int n, int m, const std::function<bool(int, int)> &eq)
{
    QList<DOp> out;
    auto add = [&](char t, int a, int b, int len) {
        if (len <= 0)
            return;
        if (!out.isEmpty() && out.last().type == t) {
            out.last().n += len;
            return;
        }
        out.append({t, a, b, len});
    };
    int pre = 0;
    while (pre < n && pre < m && eq(pre, pre))
        ++pre;
    int suf = 0;
    while (suf < n - pre && suf < m - pre && eq(n - 1 - suf, m - 1 - suf))
        ++suf;
    add('e', 0, 0, pre);
    const int N = n - pre - suf, M = m - pre - suf;
    if (N == 0 || M == 0) {
        add('d', pre, pre, N);
        add('i', pre, pre, M);
    } else {
        const int max = N + M;
        std::vector<int> v(size_t(2 * max + 2), 0);
        std::vector<std::vector<int>> trace;
        int dEnd = -1;
        for (int d = 0; d <= max; ++d) {
            std::vector<int> snap;
            if (d > 0) {
                snap.resize(size_t(2 * (d - 1) + 1));
                for (int k = -(d - 1); k <= d - 1; ++k)
                    snap[size_t(k + d - 1)] = v[size_t(k + max)];
            }
            trace.push_back(std::move(snap));
            bool done = false;
            for (int k = -d; k <= d; k += 2) {
                int x = (k == -d || (k != d && v[size_t(k - 1 + max)] < v[size_t(k + 1 + max)])) ? v[size_t(k + 1 + max)]
                                                                                                  : v[size_t(k - 1 + max)] + 1;
                int y = x - k;
                while (x < N && y < M && eq(pre + x, pre + y)) {
                    ++x;
                    ++y;
                }
                v[size_t(k + max)] = x;
                if (x >= N && y >= M) {
                    done = true;
                    break;
                }
            }
            if (done) {
                dEnd = d;
                break;
            }
            if (d > 5000)
                break;
        }
        if (dEnd < 0) {
            add('d', pre, pre, N);
            add('i', pre, pre, M);
        } else {
            auto prevV = [&](int d, int k) {
                const std::vector<int> &t = trace[size_t(d)];
                const int idx = k + d - 1;
                return (idx < 0 || idx >= int(t.size())) ? -1 : t[size_t(idx)];
            };
            QList<DOp> rev;
            int x = N, y = M;
            for (int d = dEnd; d > 0; --d) {
                const int k = x - y;
                const int prevK = (k == -d || (k != d && prevV(d, k - 1) < prevV(d, k + 1))) ? k + 1 : k - 1;
                const int px = prevV(d, prevK), py = px - prevK;
                while (x > px && y > py) {
                    rev.append({'e', pre + x - 1, pre + y - 1, 1});
                    --x;
                    --y;
                }
                if (x == px)
                    rev.append({'i', pre + x, pre + y - 1, 1});
                else
                    rev.append({'d', pre + x - 1, pre + y, 1});
                x = px;
                y = py;
            }
            while (x > 0 && y > 0) {
                rev.append({'e', pre + x - 1, pre + y - 1, 1});
                --x;
                --y;
            }
            for (int i = int(rev.size()) - 1; i >= 0; --i)
                add(rev[i].type, rev[i].a, rev[i].b, 1);
        }
    }
    add('e', n - suf, m - suf, suf);
    return out;
}

static QHash<QString, int> wordBag(const Para &p)
{
    QHash<QString, int> bag;
    for (const Token &t : tokenize(p))
        if (t.word)
            ++bag[t.key.toLower()];
    return bag;
}

static double similarity(const QHash<QString, int> &a, const QHash<QString, int> &b)
{
    int na = 0, nb = 0, common = 0;
    for (auto it = a.begin(); it != a.end(); ++it) {
        na += it.value();
        common += qMin(it.value(), b.value(it.key()));
    }
    for (auto it = b.begin(); it != b.end(); ++it)
        nb += it.value();
    return na + nb == 0 ? 0.0 : 2.0 * common / (na + nb);
}

// the word-level operations that turn project paragraph P into file paragraph F
static QList<WordOp> wordOps(const Para &P, const Para &F)
{
    const QVector<Token> tp = tokenize(P), tf = tokenize(F);
    const QList<DOp> ops = diffSeq(int(tp.size()), int(tf.size()), [&](int i, int j) { return tp[i].key == tf[j].key; });
    QList<WordOp> out;
    const int np = int(tp.size()), nf = int(tf.size());
    int i = 0, j = 0;
    for (int o = 0; o < ops.size(); ++o) {
        const DOp &op = ops[o];
        if (op.type == 'e') {
            // the same words: other bold, italic, underline?
            int k = 0;
            while (k < op.n) {
                if (!(tp[i + k].word && tp[i + k].fmt != tf[j + k].fmt)) {
                    ++k;
                    continue;
                }
                int e = k;
                while (e < op.n && (tp[i + e].fmt != tf[j + e].fmt || !tp[i + e].word))
                    ++e;
                while (e > k && !tp[i + e - 1].word)
                    --e;
                WordOp w;
                w.type = WordOp::Format;
                w.pStart = tp[i + k].start;
                w.pEnd = tp[i + e - 1].end;
                w.fStart = tf[j + k].start;
                w.fEnd = tf[j + e - 1].end;
                out << w;
                k = e;
            }
            i += op.n;
            j += op.n;
            continue;
        }
        // a run of deletions and insertions: one place
        int di = 0, dj = 0;
        int o2 = o;
        while (o2 < ops.size() && ops[o2].type != 'e') {
            if (ops[o2].type == 'd')
                di += ops[o2].n;
            else
                dj += ops[o2].n;
            ++o2;
        }
        o = o2 - 1;
        const int i0 = i, i1 = i + di, j0 = j, j1 = j + dj;
        WordOp w;
        w.type = di && dj ? WordOp::Replace : (di ? WordOp::Delete : WordOp::Insert);
        if (i0 > 0 || j0 > 0) {
            w.pStart = tp[i0 - 1].end;
            w.pEnd = i1 > i0 ? tp[i1 - 1].end : w.pStart;
            w.fStart = tf[j0 - 1].end;
            w.fEnd = j1 > j0 ? tf[j1 - 1].end : w.fStart;
        } else {
            w.pStart = 0;
            w.fStart = 0;
            w.pEnd = i1 < np ? tp[i1].start : int(P.text.size());
            w.fEnd = j1 < nf ? tf[j1].start : int(F.text.size());
        }
        out << w;
        i = i1;
        j = j1;
    }
    return out;
}

// pairs of similar paragraphs in a block that differs (in order)
static QList<QPair<int, int>> alignBlock(const QList<Para> &P, int p0, int k, const QList<Para> &F, int f0, int l)
{
    QList<QPair<int, int>> pairs;
    if (k == 0 || l == 0)
        return pairs;
    QVector<QHash<QString, int>> bp(k), bf(l);
    for (int a = 0; a < k; ++a)
        bp[a] = wordBag(P[p0 + a]);
    for (int b = 0; b < l; ++b)
        bf[b] = wordBag(F[f0 + b]);
    const double minSim = 0.4;
    if (qint64(k) * l > 90000) {
        int a = 0, b = 0;
        while (a < k && b < l) {
            if (similarity(bp[a], bf[b]) >= minSim)
                pairs.append({a, b}), ++a, ++b;
            else if (a + 1 < k && similarity(bp[a + 1], bf[b]) >= minSim)
                ++a;
            else
                ++b;
        }
        return pairs;
    }
    std::vector<std::vector<double>> sc(size_t(k + 1), std::vector<double>(size_t(l + 1), 0.0));
    std::vector<std::vector<double>> sim(size_t(k), std::vector<double>(size_t(l), 0.0));
    for (int a = 0; a < k; ++a)
        for (int b = 0; b < l; ++b)
            sim[size_t(a)][size_t(b)] = similarity(bp[a], bf[b]);
    for (int a = 1; a <= k; ++a)
        for (int b = 1; b <= l; ++b) {
            double best = qMax(sc[size_t(a - 1)][size_t(b)], sc[size_t(a)][size_t(b - 1)]);
            const double s = sim[size_t(a - 1)][size_t(b - 1)];
            if (s >= minSim)
                best = qMax(best, sc[size_t(a - 1)][size_t(b - 1)] + s);
            sc[size_t(a)][size_t(b)] = best;
        }
    int a = k, b = l;
    while (a > 0 && b > 0) {
        const double s = sim[size_t(a - 1)][size_t(b - 1)];
        if (s >= minSim && qFuzzyCompare(1 + sc[size_t(a)][size_t(b)], 1 + sc[size_t(a - 1)][size_t(b - 1)] + s)) {
            pairs.prepend({a - 1, b - 1});
            --a;
            --b;
        } else if (qFuzzyCompare(1 + sc[size_t(a)][size_t(b)], 1 + sc[size_t(a - 1)][size_t(b)])) {
            --a;
        } else {
            --b;
        }
    }
    return pairs;
}

static void diffPair(Pair &pair)
{
    pair.changes.clear();
    const QList<Para> &P = pair.project, &F = pair.file;
    QStringList kp, kf;
    for (const Para &p : P)
        kp << paraKey(p);
    for (const Para &f : F)
        kf << paraKey(f);
    const QList<DOp> ops = diffSeq(int(P.size()), int(F.size()), [&](int i, int j) { return kp[i] == kf[j]; });
    int pi = 0, fi = 0;
    for (int o = 0; o < ops.size(); ++o) {
        const DOp &op = ops[o];
        if (op.type == 'e') {
            for (int k = 0; k < op.n; ++k) {
                const QList<WordOp> w = wordOps(P[pi + k], F[fi + k]);
                if (!w.isEmpty()) {
                    Change c;
                    c.kind = Change::Formatting;
                    c.pFirst = pi + k;
                    c.pCount = 1;
                    c.fFirst = fi + k;
                    c.fCount = 1;
                    c.ops = w;
                    pair.changes << c;
                }
            }
            pi += op.n;
            fi += op.n;
            continue;
        }
        int dp = 0, df = 0;
        int o2 = o;
        while (o2 < ops.size() && ops[o2].type != 'e') {
            if (ops[o2].type == 'd')
                dp += ops[o2].n;
            else
                df += ops[o2].n;
            ++o2;
        }
        o = o2 - 1;
        const QList<QPair<int, int>> al = alignBlock(P, pi, dp, F, fi, df);
        int a = 0, b = 0;
        auto flush = [&](int toA, int toB) {
            if (toA > a) {
                Change c;
                c.kind = Change::OnlyInProject;
                c.pFirst = pi + a;
                c.pCount = toA - a;
                c.fFirst = fi + b;
                pair.changes << c;
            }
            if (toB > b) {
                Change c;
                c.kind = Change::OnlyInFile;
                c.fFirst = fi + b;
                c.fCount = toB - b;
                c.pFirst = pi + toA;
                pair.changes << c;
            }
            a = toA;
            b = toB;
        };
        for (const auto &pr : al) {
            flush(pr.first, pr.second);
            Change c;
            c.pFirst = pi + pr.first;
            c.pCount = 1;
            c.fFirst = fi + pr.second;
            c.fCount = 1;
            c.ops = wordOps(P[c.pFirst], F[c.fFirst]);
            bool onlyFormat = true;
            for (const WordOp &w : c.ops)
                if (w.type != WordOp::Format)
                    onlyFormat = false;
            c.kind = onlyFormat ? Change::Formatting : Change::Changed;
            if (!c.ops.isEmpty())
                pair.changes << c;
            a = pr.first + 1;
            b = pr.second + 1;
        }
        flush(dp, df);
        pi += dp;
        fi += df;
    }
}

// ---------------------------------------------------------------- pairing

static quint64 shingleHash(const QStringList &w, int i)
{
    QString s = w[i];
    for (int k = 1; k < 4; ++k) {
        s += QChar(0x1F);
        s += w[i + k];
    }
    return quint64(qHash(s));
}

static QSet<quint64> shingles(const QStringList &w)
{
    QSet<quint64> out;
    for (int i = 0; i + 4 <= w.size(); ++i)
        out.insert(shingleHash(w, i));
    return out;
}

bool defaultIncluded(const Pair &p, Direction d)
{
    switch (p.kind) {
    case Pair::Matched:
        return !p.changes.isEmpty();
    case Pair::OnlyInFile:
        // into the project: a new document (a real chapter, not a title page); into the file: never deleted unasked
        return d == Direction::IntoProject && p.words() >= 100;
    case Pair::OnlyInProject:
        return d == Direction::IntoFile && p.words() >= 100;
    }
    return false;
}

Result compareTexts(Project *p, const QList<QString> &docIds, const QString &compiled, FileText *file)
{
    Result r;
    r.fileKind = file->kind;
    r.canWriteFile = file->canWrite();
    const QList<Para> &fp = file->paras;
    // 1. related at all? The file's four-word sequences found in the compiled text
    QStringList fileWords;
    QVector<QStringList> paraWords(fp.size());
    for (int i = 0; i < fp.size(); ++i) {
        paraWords[i] = normalizedWords(fp[i].text);
        fileWords << paraWords[i];
    }
    const QStringList compWords = normalizedWords(compiled);
    const QSet<quint64> F = shingles(fileWords), M = shingles(compWords);
    // the share of words that stand in a four-word sequence the other text has too
    // (a changed word costs only itself, not the three words around it)
    auto share = [](const QStringList &w, const QSet<quint64> &other) {
        if (w.size() < 4)
            return 0;
        QVector<bool> in(w.size(), false);
        for (int i = 0; i + 4 <= w.size(); ++i)
            if (other.contains(shingleHash(w, i)))
                for (int k = 0; k < 4; ++k)
                    in[i + k] = true;
        return int(qRound(100.0 * std::count(in.begin(), in.end(), true) / w.size()));
    };
    r.fileWords = int(fileWords.size());
    r.compiledWords = int(compWords.size());
    r.related = share(fileWords, M);
    r.covered = share(compWords, F);

    // 2. the documents Compile takes, with their words
    struct Doc {
        QString id, title;
        QList<Para> paras;
        QSet<QString> keys;
    };
    QList<Doc> docs;
    for (const QString &id : docIds) {
        BinderItem *it = p->item(id);
        if (!it)
            continue;
        Doc d;
        d.id = id;
        d.title = it->title;
        p->docs()->editStored(id, false, [&](QTextDocument *doc) {
            for (const Para &para : projectParagraphs(doc, p->formatting))
                if (para.words() > 0)
                    d.paras << para;
            return 0;
        });
        if (d.paras.isEmpty())
            continue;
        for (const Para &para : d.paras)
            d.keys.insert(paraKey(para));
        docs << d;
    }
    QHash<quint64, QVector<int>> index;
    QHash<QString, QVector<int>> keyIndex;
    for (int d = 0; d < docs.size(); ++d) {
        QSet<quint64> sh;
        for (const Para &para : docs[d].paras) {
            sh.unite(shingles(normalizedWords(para.text)));
            QVector<int> &v = keyIndex[paraKey(para)];
            if (!v.contains(d))
                v << d;
        }
        for (quint64 h : sh)
            index[h] << d;
    }
    // 3. each paragraph of the file: which document has its words?
    const int nf = int(fp.size());
    QVector<int> assign(nf, -1), words(nf, 0);
    for (int i = 0; i < nf; ++i) {
        words[i] = int(paraWords[i].size());
        if (!words[i])
            continue;
        QHash<int, int> votes;
        const QSet<quint64> sh = shingles(paraWords[i]);
        for (quint64 h : sh)
            for (int d : index.value(h))
                ++votes[d];
        int best = -1, bestVotes = 0;
        for (auto it = votes.begin(); it != votes.end(); ++it)
            if (it.value() > bestVotes || (it.value() == bestVotes && it.key() < best)) {
                best = it.key();
                bestVotes = it.value();
            }
        if (best >= 0 && bestVotes >= qMax(1, int(0.25 * sh.size())))
            assign[i] = best;
        else {
            // short paragraphs: the same paragraph in exactly one document
            const QVector<int> exact = keyIndex.value(paraKey(fp[i]));
            if (exact.size() == 1)
                assign[i] = exact.first();
        }
    }
    // 4. sections: at headings and where the document changes
    struct Seg {
        QList<int> paras;
        QHash<int, int> weight;
        int doc() const
        {
            int best = -1, w = 0;
            for (auto it = weight.begin(); it != weight.end(); ++it)
                if (it.value() > w) {
                    best = it.key();
                    w = it.value();
                }
            return best;
        }
        bool hasContent(const QList<Para> &fp) const
        {
            for (int i : paras)
                if (fp[i].heading == 0)
                    return true;
            return false;
        }
    };
    QList<Seg> segs;
    Seg cur;
    for (int i = 0; i < nf; ++i) {
        if (!words[i])
            continue;
        const int d = assign[i];
        const int dom = cur.doc();
        if ((fp[i].heading > 0 && cur.hasContent(fp)) || (d >= 0 && dom >= 0 && d != dom)) {
            segs << cur;
            cur = Seg();
        }
        cur.paras << i;
        if (d >= 0)
            cur.weight[d] += words[i];
    }
    if (!cur.paras.isEmpty())
        segs << cur;
    // a section of headings only belongs to the next one; neighbors of one document are one
    QList<Seg> merged;
    for (int s = 0; s < segs.size(); ++s) {
        Seg sg = segs[s];
        if (!sg.hasContent(fp) && s + 1 < segs.size()) {
            segs[s + 1].paras = sg.paras + segs[s + 1].paras;
            continue;
        }
        if (!merged.isEmpty() && sg.doc() >= 0 && merged.last().doc() == sg.doc()) {
            merged.last().paras << sg.paras;
            for (auto it = sg.weight.begin(); it != sg.weight.end(); ++it)
                merged.last().weight[it.key()] += it.value();
            continue;
        }
        merged << sg;
    }
    // 5. pairs in the order of the file
    QHash<int, int> pairOfDoc;
    for (const Seg &sg : merged) {
        const int d = sg.doc();
        if (d >= 0 && pairOfDoc.contains(d)) {
            Pair &pr = r.pairs[pairOfDoc.value(d)];
            for (int i : sg.paras)
                pr.file << fp[i];
            continue;
        }
        Pair pr;
        if (d >= 0) {
            pr.kind = Pair::Matched;
            pr.docId = docs[d].id;
            pr.title = docs[d].title;
            pr.project = docs[d].paras;
            pairOfDoc.insert(d, int(r.pairs.size()));
        } else {
            pr.kind = Pair::OnlyInFile;
        }
        for (int i : sg.paras)
            pr.file << fp[i];
        r.pairs << pr;
    }
    // the titles: headings of the file that the document does not have
    for (int k = 0; k < r.pairs.size(); ++k) {
        Pair &pr = r.pairs[k];
        QList<Para> content;
        const QSet<QString> keys = pr.kind == Pair::Matched ? docs[pairOfDoc.key(k)].keys : QSet<QString>();
        QStringList titles;
        for (const Para &para : pr.file) {
            if (para.heading > 0 && !keys.contains(paraKey(para))) {
                pr.titleParas << para.source;
                titles << para.text.simplified();
            } else {
                content << para;
            }
        }
        pr.file = content;
        pr.fileTitle = titles.join(QStringLiteral(": "));
        if (pr.kind == Pair::OnlyInFile)
            pr.title = pr.fileTitle.isEmpty() ? (pr.file.isEmpty() ? QString() : pr.file.first().text.simplified().left(60)) : pr.fileTitle;
    }
    // documents missing from the file: at their place in the compile order
    for (int d = 0; d < docs.size(); ++d) {
        if (pairOfDoc.contains(d))
            continue;
        Pair pr;
        pr.kind = Pair::OnlyInProject;
        pr.docId = docs[d].id;
        pr.title = docs[d].title;
        pr.project = docs[d].paras;
        int at = 0;
        for (int k = 0; k < r.pairs.size(); ++k) {
            const Pair &o = r.pairs[k];
            if (o.docId.isEmpty())
                continue;
            int od = -1;
            for (int x = 0; x < docs.size(); ++x)
                if (docs[x].id == o.docId)
                    od = x;
            if (od >= 0 && od < d)
                at = k + 1;
        }
        r.pairs.insert(at, pr);
    }
    // moved: documents that are not in the longest run of the compile order
    {
        QList<int> idxs, orders;
        for (int k = 0; k < r.pairs.size(); ++k)
            if (r.pairs[k].kind == Pair::Matched)
                for (int x = 0; x < docs.size(); ++x)
                    if (docs[x].id == r.pairs[k].docId) {
                        idxs << k;
                        orders << x;
                    }
        const int n = int(orders.size());
        QVector<int> len(n, 1), prev(n, -1);
        int bestEnd = -1;
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < i; ++j)
                if (orders[j] < orders[i] && len[j] + 1 > len[i]) {
                    len[i] = len[j] + 1;
                    prev[i] = j;
                }
            if (bestEnd < 0 || len[i] > len[bestEnd])
                bestEnd = i;
        }
        QSet<int> keep;
        for (int i = bestEnd; i >= 0; i = prev[i])
            keep.insert(i);
        for (int i = 0; i < n; ++i)
            r.pairs[idxs[i]].moved = !keep.contains(i);
    }
    // 6. the differences
    for (Pair &pr : r.pairs) {
        if (pr.kind == Pair::Matched)
            diffPair(pr);
        pr.included = defaultIncluded(pr, Direction::IntoProject);
    }
    return r;
}

bool compareWithFile(Project *p, const QString &path, Result *out, QString *error)
{
    std::unique_ptr<FileText> file = openFile(path, p->formatting, error);
    if (!file)
        return false;
    QStringList ids;
    const QString text = compiledText(p, &ids);
    *out = compareTexts(p, ids, text, file.get());
    out->filePath = path;
    return true;
}

QString suggestedCopyPath(const QString &filePath)
{
    const QFileInfo fi(filePath);
    return fi.dir().filePath(fi.completeBaseName() + QStringLiteral(" (Quillbinder).") + fi.suffix());
}

// ---------------------------------------------------------------- into the project

static QTextCharFormat plainFormat(QTextCharFormat cf)
{
    for (int pr : {int(props::NoteKind), int(props::NoteId), int(props::NoteText), int(props::NoteColor), int(props::SearchHit)})
        cf.clearProperty(pr);
    return cf;
}

static void setFlags(QTextCharFormat &cf, quint8 f)
{
    cf.setFontWeight((f & Bold) ? QFont::Bold : QFont::Normal);
    cf.setFontItalic(f & Italic);
    cf.setFontUnderline(f & Underline);
}

static void insertFormatted(QTextCursor &c, const QString &text, const QVector<quint8> &fmt, const QTextCharFormat &base)
{
    int i = 0;
    while (i < text.size()) {
        int j = i + 1;
        while (j < text.size() && fmt.value(j) == fmt.value(i))
            ++j;
        QTextCharFormat cf = base;
        setFlags(cf, fmt.value(i));
        QString piece = text.mid(i, j - i);
        piece.replace(QLatin1Char('\n'), QChar(0x2028));
        c.insertText(piece, cf);
        i = j;
    }
}

// replaces characters [s, e) of project paragraph P with text (positions from the end first)
static void replaceInDocument(QTextDocument *doc, const Para &P, int s, int e, const QString &text, const QVector<quint8> &fmt)
{
    QTextCursor c(doc);
    // the characters to remove, as runs of neighboring positions (notes in between stay)
    QList<QPair<int, int>> runs;
    for (int k = s; k < e; ++k) {
        const int pos = P.pos.at(k);
        if (!runs.isEmpty() && runs.last().second == pos)
            runs.last().second = pos + 1;
        else
            runs.append({pos, pos + 1});
    }
    int at;
    if (s < P.pos.size())
        at = P.pos.at(s);
    else
        at = P.pos.isEmpty() ? doc->findBlockByNumber(P.source).position() : P.pos.last() + 1;
    for (int r = int(runs.size()) - 1; r >= 0; --r) {
        c.setPosition(runs[r].first);
        c.setPosition(runs[r].second, QTextCursor::KeepAnchor);
        c.removeSelectedText();
    }
    if (text.isEmpty())
        return;
    c.setPosition(at);
    QTextCharFormat base = c.charFormat();
    if (at == doc->findBlock(at).position()) {
        QTextCursor next(doc);
        next.setPosition(qMin(at + 1, doc->characterCount() - 1));
        base = next.charFormat();
    }
    insertFormatted(c, text, fmt, plainFormat(base));
}

static void formatInDocument(QTextDocument *doc, const Para &P, int s, int e, const QVector<quint8> &fmt)
{
    QTextCursor c(doc);
    for (int k = s; k < e; ++k) {
        const int pos = P.pos.at(k);
        c.setPosition(pos);
        c.setPosition(pos + 1, QTextCursor::KeepAnchor);
        QTextCharFormat cf;
        setFlags(cf, fmt.value(k - s));
        c.mergeCharFormat(cf);
    }
}

// the characters of target side for a Format op: per token the other side's look
static QVector<quint8> formatTarget(const Para &target, int tStart, int tEnd, const Para &source, int sStart, int sEnd)
{
    QVector<quint8> out;
    Para t = target, s = source;
    t.text = target.text.mid(tStart, tEnd - tStart);
    t.fmt = target.fmt.mid(tStart, tEnd - tStart);
    s.text = source.text.mid(sStart, sEnd - sStart);
    s.fmt = source.fmt.mid(sStart, sEnd - sStart);
    const QVector<Token> tt = tokenize(t), st = tokenize(s);
    out = t.fmt;
    for (int k = 0; k < tt.size() && k < st.size(); ++k) {
        for (int x = tt[k].start; x < tt[k].end; ++x)
            out[x] = st[k].fmt;
        // the gap after the token takes the look of the gap on the other side
        if (k + 1 < tt.size() && k + 1 < st.size()) {
            const quint8 g = st[k].end < s.fmt.size() ? s.fmt.at(st[k].end) : st[k].fmt;
            for (int x = tt[k].end; x < tt[k + 1].start; ++x)
                out[x] = (st[k].fmt == st[k + 1].fmt) ? st[k].fmt : g;
        }
    }
    return out;
}

static bool sameTexts(const QList<Para> &a, const QList<Para> &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i)
        if (a[i].text != b[i].text)
            return false;
    return true;
}

static int applyToDocument(Project *p, Pair &pr, const QString &snapshotTitle, Report *rep, QString *error)
{
    int places = 0;
    bool any = false;
    for (const Change &c : pr.changes)
        any = any || c.accepted;
    if (!any)
        return 0;
    const int res = p->docs()->editStored(pr.docId, false, [&](QTextDocument *doc) {
        QList<Para> now;
        for (const Para &para : projectParagraphs(doc, p->formatting))
            if (para.words() > 0)
                now << para;
        if (!sameTexts(now, pr.project)) {
            rep->warnings << QObject::tr("\"%1\" was changed after the comparison and was left alone.").arg(pr.title);
            return 0;
        }
        pr.project = now; // the positions of now
        if (snapshots::take(p, pr.docId, snapshotTitle, nullptr, doc))
            ++rep->snapshots;
        QTextCursor edit(doc);
        edit.beginEditBlock();
        for (int ci = int(pr.changes.size()) - 1; ci >= 0; --ci) {
            const Change &c = pr.changes[ci];
            if (!c.accepted)
                continue;
            ++places;
            if (c.kind == Change::Changed || c.kind == Change::Formatting) {
                const Para &P = pr.project[c.pFirst];
                const Para &F = pr.file[c.fFirst];
                QList<WordOp> ops = c.ops;
                std::sort(ops.begin(), ops.end(), [](const WordOp &a, const WordOp &b) { return a.pStart > b.pStart; });
                for (const WordOp &w : ops) {
                    if (w.type == WordOp::Format)
                        formatInDocument(doc, P, w.pStart, w.pEnd, formatTarget(P, w.pStart, w.pEnd, F, w.fStart, w.fEnd));
                    else
                        replaceInDocument(doc, P, w.pStart, w.pEnd, F.text.mid(w.fStart, w.fEnd - w.fStart),
                                          F.fmt.mid(w.fStart, w.fEnd - w.fStart));
                }
            } else if (c.kind == Change::OnlyInProject) {
                for (int k = c.pFirst + c.pCount - 1; k >= c.pFirst; --k) {
                    const QTextBlock b = doc->findBlockByNumber(pr.project[k].source);
                    QTextCursor del(doc);
                    if (b.next().isValid()) {
                        del.setPosition(b.position());
                        del.setPosition(b.next().position(), QTextCursor::KeepAnchor);
                    } else if (b.previous().isValid()) {
                        del.setPosition(b.previous().position() + b.previous().length() - 1);
                        del.setPosition(b.position() + b.length() - 1, QTextCursor::KeepAnchor);
                    } else {
                        del.setPosition(b.position());
                        del.setPosition(b.position() + b.length() - 1, QTextCursor::KeepAnchor);
                    }
                    del.removeSelectedText();
                }
            } else if (c.kind == Change::OnlyInFile) {
                QTextCursor ins(doc);
                if (c.pFirst < pr.project.size()) {
                    // before that paragraph: the new text, then the paragraph break
                    const QTextBlock b = doc->findBlockByNumber(pr.project[c.pFirst].source);
                    ins.setPosition(b.position());
                    const QTextCharFormat base = plainFormat(ins.charFormat());
                    for (int k = c.fFirst; k < c.fFirst + c.fCount; ++k) {
                        insertFormatted(ins, pr.file[k].text, pr.file[k].fmt, base);
                        ins.insertBlock(b.blockFormat(), base);
                    }
                } else {
                    const QTextBlock b = pr.project.isEmpty() ? doc->lastBlock() : doc->findBlockByNumber(pr.project.last().source);
                    ins.setPosition(b.position() + b.length() - 1);
                    const QTextCharFormat base = plainFormat(ins.charFormat());
                    for (int k = c.fFirst; k < c.fFirst + c.fCount; ++k) {
                        ins.insertBlock(b.blockFormat(), base);
                        insertFormatted(ins, pr.file[k].text, pr.file[k].fmt, base);
                    }
                }
            }
        }
        edit.endEditBlock();
        return places;
    }, error);
    return res;
}

// ---------------------------------------------------------------- into the file

static void fileEdits(FileText *file, const Pair &pr, Report *rep)
{
    for (int ci = int(pr.changes.size()) - 1; ci >= 0; --ci) {
        const Change &c = pr.changes[ci];
        if (!c.accepted)
            continue;
        ++rep->places;
        if (c.kind == Change::Changed || c.kind == Change::Formatting) {
            const Para &P = pr.project[c.pFirst];
            const Para &F = pr.file[c.fFirst];
            QList<WordOp> ops = c.ops;
            std::sort(ops.begin(), ops.end(), [](const WordOp &a, const WordOp &b) { return a.fStart > b.fStart; });
            for (const WordOp &w : ops) {
                if (w.type == WordOp::Format)
                    file->setFormat(F.source, w.fStart, w.fEnd, formatTarget(F, w.fStart, w.fEnd, P, w.pStart, w.pEnd));
                else
                    file->replace(F.source, w.fStart, w.fEnd, P.text.mid(w.pStart, w.pEnd - w.pStart),
                                  P.fmt.mid(w.pStart, w.pEnd - w.pStart));
            }
        } else if (c.kind == Change::OnlyInFile) {
            for (int k = c.fFirst + c.fCount - 1; k >= c.fFirst; --k)
                file->deleteParagraph(pr.file[k].source);
        } else if (c.kind == Change::OnlyInProject) {
            const QList<Para> ps = pr.project.mid(c.pFirst, c.pCount);
            int after, like;
            if (c.fFirst < pr.file.size()) {
                after = pr.file[c.fFirst].source - 1;
                like = pr.file[c.fFirst].source;
            } else {
                after = pr.file.last().source;
                like = after;
            }
            file->insertParagraphs(after, like, ps);
        }
    }
}

bool apply(Project *p, Result &r, Direction d, const QString &outPath, Report *report, QString *error)
{
    Report dummy;
    Report *rep = report ? report : &dummy;
    *rep = Report();
    const QString snapTitle = QObject::tr("Before Compare and Transfer (%1)").arg(QFileInfo(r.filePath).fileName());
    if (d == Direction::IntoProject) {
        // the new documents' places: after the document before them in the file
        QString prevDoc;
        for (Pair &pr : r.pairs) {
            if (!pr.included) {
                if (!pr.docId.isEmpty() && pr.kind != Pair::OnlyInProject)
                    prevDoc = pr.docId;
                continue;
            }
            if (pr.kind == Pair::Matched) {
                const int n = applyToDocument(p, pr, snapTitle, rep, error);
                if (n < 0)
                    return false;
                if (n > 0) {
                    ++rep->documentsChanged;
                    rep->places += n;
                }
                prevDoc = pr.docId;
            } else if (pr.kind == Pair::OnlyInFile) {
                BinderItem *after = p->item(prevDoc);
                BinderItem *parent = after ? after->parent : p->draft();
                const int row = after ? after->row() + 1 : 0;
                const QString title = pr.title.isEmpty() ? QObject::tr("From %1").arg(QFileInfo(r.filePath).fileName()) : pr.title;
                BinderItem *it = p->model()->createItem(parent, row, ItemType::Text, title);
                if (!it)
                    continue;
                QTextDocument doc;
                QTextCursor c(&doc);
                for (int k = 0; k < pr.file.size(); ++k) {
                    if (k)
                        c.insertBlock();
                    insertFormatted(c, pr.file[k].text, pr.file[k].fmt, QTextCharFormat());
                }
                p->docs()->setContent(it->id, &doc);
                p->notifyItemChanged(it);
                pr.docId = it->id;
                prevDoc = it->id;
                ++rep->documentsAdded;
                ++rep->places;
            } else if (pr.kind == Pair::OnlyInProject) {
                if (BinderItem *it = p->item(pr.docId)) {
                    p->model()->moveToTrash({it});
                    ++rep->documentsTrashed;
                    ++rep->places;
                }
            }
        }
        p->markIndexDirty();
        return true;
    }
    // into a copy of the file
    QString err;
    std::unique_ptr<FileText> file = openFile(r.filePath, p->formatting, &err);
    if (!file || !file->canWrite()) {
        if (error)
            *error = file ? QObject::tr("This kind of file can only be read; take its text into the project instead.") : err;
        return false;
    }
    for (const Pair &pr : r.pairs)
        for (const Para &para : pr.file)
            if (para.source < 0 || para.source >= file->paras.size() || file->paras[para.source].text != para.text) {
                if (error)
                    *error = QObject::tr("The file was changed after the comparison. Compare again.");
                return false;
            }
    for (int k = int(r.pairs.size()) - 1; k >= 0; --k) {
        const Pair &pr = r.pairs[k];
        if (!pr.included)
            continue;
        if (pr.kind == Pair::Matched) {
            fileEdits(file.get(), pr, rep);
            ++rep->documentsChanged;
        } else if (pr.kind == Pair::OnlyInFile) {
            QList<int> all = pr.titleParas;
            for (const Para &para : pr.file)
                all << para.source;
            std::sort(all.begin(), all.end());
            for (int i = int(all.size()) - 1; i >= 0; --i)
                file->deleteParagraph(all[i]);
            ++rep->places;
        } else if (pr.kind == Pair::OnlyInProject) {
            // after the file's paragraphs of the pair before it, else before those of the next;
            // the first scene of a chapter goes after the chapter's heading (the next pair's title)
            int after = -1, like = -1;
            const Pair *prevPair = nullptr, *nextPair = nullptr;
            for (int b = k - 1; b >= 0 && !prevPair; --b)
                if (!r.pairs[b].file.isEmpty())
                    prevPair = &r.pairs[b];
            for (int n = k + 1; n < r.pairs.size() && !nextPair; ++n)
                if (!r.pairs[n].file.isEmpty())
                    nextPair = &r.pairs[n];
            auto parentOf = [&](const Pair *o) -> BinderItem * {
                BinderItem *it = o ? p->item(o->docId) : nullptr;
                return it ? it->parent : nullptr;
            };
            BinderItem *mine = p->item(pr.docId);
            if (mine && nextPair && !nextPair->titleParas.isEmpty() && parentOf(nextPair) == mine->parent
                && parentOf(prevPair) != mine->parent) {
                // right after the heading (a scene separator after it stays between the two scenes)
                like = nextPair->file.first().source;
                after = like - 1;
                int lastTitle = -1;
                for (int t : nextPair->titleParas)
                    if (t < like)
                        lastTitle = qMax(lastTitle, t);
                if (lastTitle >= 0)
                    after = lastTitle;
            } else if (prevPair) {
                after = prevPair->file.last().source;
                like = after;
            }
            if (after < 0 && like < 0)
                for (int n = k + 1; n < r.pairs.size() && like < 0; ++n)
                    if (!r.pairs[n].file.isEmpty()) {
                        int first = r.pairs[n].file.first().source;
                        for (int t : r.pairs[n].titleParas)
                            first = qMin(first, t);
                        after = first - 1;
                        like = r.pairs[n].file.first().source;
                    }
            if (like < 0) {
                after = int(file->paras.size()) - 1;
                like = after;
            }
            file->insertParagraphs(after, like, pr.project);
            ++rep->places;
        }
    }
    if (!file->save(outPath, error))
        return false;
    rep->writtenFile = outPath;
    return true;
}

} // namespace compare
} // namespace qb
