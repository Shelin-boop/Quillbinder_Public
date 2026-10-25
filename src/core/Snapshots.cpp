#include "core/Snapshots.h"
#include "core/BinderItem.h"
#include "core/DocumentStore.h"
#include "core/Project.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextDocument>
#include <vector>

namespace qb {
namespace snapshots {

static QJsonObject readIndex(const QString &dir)
{
    QFile f(QDir(dir).filePath(QStringLiteral("index.json")));
    if (!f.open(QIODevice::ReadOnly))
        return QJsonObject();
    return QJsonDocument::fromJson(f.readAll()).object();
}

static void writeIndex(const QString &dir, const QJsonObject &o)
{
    QSaveFile f(QDir(dir).filePath(QStringLiteral("index.json")));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson());
        f.commit();
    }
}

QList<Snapshot> list(Project *p, const QString &docId)
{
    QList<Snapshot> out;
    const QString dir = p->snapshotsDir(docId);
    const QJsonObject idx = readIndex(dir);
    const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.md"), QStringLiteral("*.rtf")}, QDir::Files,
                                                        QDir::Name | QDir::Reversed);
    for (const QFileInfo &fi : files) {
        Snapshot s;
        s.id = docId;
        s.file = fi.absoluteFilePath();
        s.date = QDateTime::fromString(fi.completeBaseName(), QStringLiteral("yyyyMMdd-HHmmss"));
        if (!s.date.isValid())
            s.date = fi.lastModified();
        s.title = idx.value(fi.fileName()).toString();
        out << s;
    }
    return out;
}

bool take(Project *p, const QString &docId, const QString &title, QString *error, QTextDocument *from)
{
    BinderItem *it = p->item(docId);
    if (!it || !it->hasText())
        return false;
    const QString dir = p->snapshotsDir(docId);
    QDir().mkpath(dir);
    QTextDocument *doc = from ? from : p->docs()->text(docId);
    const QByteArray data = p->docs()->serialize(docId, doc);
    QDateTime now = QDateTime::currentDateTime();
    QString name;
    do {
        name = now.toString(QStringLiteral("yyyyMMdd-HHmmss")) + QLatin1Char('.') + p->docs()->extension();
        now = now.addSecs(1);
    } while (QFile::exists(QDir(dir).filePath(name)));
    QSaveFile f(QDir(dir).filePath(name));
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QJsonObject idx = readIndex(dir);
    idx.insert(name, title.isEmpty() ? QObject::tr("Untitled Snapshot") : title);
    writeIndex(dir, idx);
    it->hasSnapshots = true;
    p->notifyItemChanged(it, false);
    return true;
}

void load(Project *p, const Snapshot &s, QTextDocument *into)
{
    QFile f(s.file);
    if (!f.open(QIODevice::ReadOnly))
        return;
    p->docs()->deserialize(s.id, f.readAll(), into, s.file.endsWith(QLatin1String(".rtf")));
}

bool remove(Project *p, const Snapshot &s)
{
    const QString dir = QFileInfo(s.file).absolutePath();
    QJsonObject idx = readIndex(dir);
    idx.remove(QFileInfo(s.file).fileName());
    writeIndex(dir, idx);
    const bool ok = QFile::remove(s.file);
    if (BinderItem *it = p->item(s.id)) {
        it->hasSnapshots = !list(p, s.id).isEmpty();
        p->notifyItemChanged(it, false);
    }
    return ok;
}

bool setTitle(Project *, const Snapshot &s, const QString &title)
{
    const QString dir = QFileInfo(s.file).absolutePath();
    QJsonObject idx = readIndex(dir);
    idx.insert(QFileInfo(s.file).fileName(), title);
    writeIndex(dir, idx);
    return true;
}

bool rollBack(Project *p, const Snapshot &s)
{
    QTextDocument tmp;
    load(p, s, &tmp);
    p->docs()->setContent(s.id, &tmp);
    return true;
}

} // namespace snapshots

// ---------------------------------------------------------------- diff

static QStringList tokenize(const QString &s, DiffGranularity g)
{
    QStringList out;
    if (g == DiffGranularity::Character) {
        for (const QChar c : s)
            out << QString(c);
        return out;
    }
    if (g == DiffGranularity::Paragraph) {
        int start = 0;
        for (int i = 0; i < s.size(); ++i) {
            if (s.at(i) == QLatin1Char('\n')) {
                out << s.mid(start, i - start + 1);
                start = i + 1;
            }
        }
        if (start < s.size())
            out << s.mid(start);
        return out;
    }
    if (g == DiffGranularity::Sentence) {
        static const QRegularExpression re(QStringLiteral("[^.!?\\n]*[.!?]+[\"'\\x{201D}\\x{2019})]*\\s*|[^.!?\\n]+\\s*|\\n"));
        auto it = re.globalMatch(s);
        int last = 0;
        while (it.hasNext()) {
            const auto m = it.next();
            if (m.capturedLength() == 0)
                break;
            out << m.captured();
            last = m.capturedEnd();
        }
        if (last < s.size())
            out << s.mid(last);
        return out;
    }
    // words: runs of word characters, runs of whitespace, single punctuation
    int i = 0;
    while (i < s.size()) {
        const QChar c = s.at(i);
        int j = i + 1;
        if (c.isLetterOrNumber()) {
            while (j < s.size() && (s.at(j).isLetterOrNumber() || s.at(j) == QLatin1Char('\'')))
                ++j;
        } else if (c.isSpace() && c != QLatin1Char('\n')) {
            while (j < s.size() && s.at(j).isSpace() && s.at(j) != QLatin1Char('\n'))
                ++j;
        }
        out << s.mid(i, j - i);
        i = j;
    }
    return out;
}

// Myers O((N+M)D) diff on token lists. The trace only keeps the diagonals
// that were reachable at each step, so memory is O(D^2).
static QList<DiffOp> myers(const QStringList &a, const QStringList &b)
{
    const int n = a.size(), m = b.size();
    const int max = n + m;
    QList<DiffOp> ops;
    if (max == 0)
        return ops;
    std::vector<int> v(2 * max + 2, 0);
    std::vector<std::vector<int>> trace; // trace[d] = v after step d-1, k in [-(d-1), d-1]
    int dFinal = -1;
    for (int d = 0; d <= max; ++d) {
        std::vector<int> snap;
        if (d > 0) {
            snap.resize(2 * (d - 1) + 1);
            for (int k = -(d - 1); k <= d - 1; ++k)
                snap[k + d - 1] = v[k + max];
        }
        trace.push_back(std::move(snap));
        bool done = false;
        for (int k = -d; k <= d; k += 2) {
            int x;
            if (k == -d || (k != d && v[k - 1 + max] < v[k + 1 + max]))
                x = v[k + 1 + max];
            else
                x = v[k - 1 + max] + 1;
            int y = x - k;
            while (x < n && y < m && a[x] == b[y]) {
                ++x;
                ++y;
            }
            v[k + max] = x;
            if (x >= n && y >= m) {
                done = true;
                break;
            }
        }
        if (done) {
            dFinal = d;
            break;
        }
        if (d > 6000) // pathological input: give up with a coarse diff
            break;
    }
    if (dFinal < 0) {
        ops.append({DiffOp::Delete, a.join(QString())});
        ops.append({DiffOp::Insert, b.join(QString())});
        return ops;
    }
    auto prevV = [&](int d, int k) { // value of v[k] after step d-1
        const std::vector<int> &t = trace[d];
        const int idx = k + d - 1;
        if (idx < 0 || idx >= int(t.size()))
            return -1;
        return t[idx];
    };
    int x = n, y = m;
    QList<DiffOp> rev;
    for (int d = dFinal; d > 0; --d) {
        const int k = x - y;
        int prevK;
        if (k == -d || (k != d && prevV(d, k - 1) < prevV(d, k + 1)))
            prevK = k + 1;
        else
            prevK = k - 1;
        const int prevX = prevV(d, prevK);
        const int prevY = prevX - prevK;
        while (x > prevX && y > prevY) {
            rev.append({DiffOp::Equal, a[x - 1]});
            --x;
            --y;
        }
        if (x == prevX)
            rev.append({DiffOp::Insert, b[y - 1]});
        else
            rev.append({DiffOp::Delete, a[x - 1]});
        x = prevX;
        y = prevY;
    }
    while (x > 0 && y > 0) {
        rev.append({DiffOp::Equal, a[x - 1]});
        --x;
        --y;
    }
    for (int i = rev.size() - 1; i >= 0; --i) {
        if (!ops.isEmpty() && ops.last().type == rev[i].type)
            ops.last().text += rev[i].text;
        else
            ops.append(rev[i]);
    }
    return ops;
}

static void appendOp(QList<DiffOp> &ops, const DiffOp &op)
{
    if (op.text.isEmpty())
        return;
    if (!ops.isEmpty() && ops.last().type == op.type)
        ops.last().text += op.text;
    else
        ops.append(op);
}

QList<DiffOp> diffTexts(const QString &oldText, const QString &newText, DiffGranularity g)
{
    if (g == DiffGranularity::Paragraph)
        return myers(tokenize(oldText, g), tokenize(newText, g));
    // paragraphs first, then refine changed paragraphs
    const QList<DiffOp> coarse = myers(tokenize(oldText, DiffGranularity::Paragraph),
                                       tokenize(newText, DiffGranularity::Paragraph));
    QList<DiffOp> out;
    for (int i = 0; i < coarse.size(); ++i) {
        const DiffOp &op = coarse[i];
        if (op.type == DiffOp::Delete && i + 1 < coarse.size() && coarse[i + 1].type == DiffOp::Insert) {
            const QList<DiffOp> fine = myers(tokenize(op.text, g), tokenize(coarse[i + 1].text, g));
            for (const DiffOp &f : fine)
                appendOp(out, f);
            ++i;
        } else {
            appendOp(out, op);
        }
    }
    return out;
}

} // namespace qb
