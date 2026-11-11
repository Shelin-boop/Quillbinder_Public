#include "core/PathSafety.h"
#include "core/SafeSave.h"
#include "core/Snapshots.h"
#include "core/BinderItem.h"
#include "core/DocumentStore.h"
#include "core/Project.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextDocument>
#include <functional>
#include <vector>

namespace qb {
namespace snapshots {

// 0.81.0 (audit of 0.80.0, F-04): a list of titles that is there but cannot be read is told from one that is missing.
// Before, a damaged index.json counted as empty, and the next snapshot wrote a new list with only its own title:
// the titles of all older snapshots were lost. Now the titles that can still be read are rescued from the damaged
// file, the damaged file is kept beside it, and a list that cannot be written is reported.
static QJsonObject readIndex(const QString &dir, bool *damaged = nullptr)
{
    if (damaged)
        *damaged = false;
    QFile f(QDir(dir).filePath(QStringLiteral("index.json")));
    if (!f.exists())
        return QJsonObject();
    QByteArray data;
    if (f.open(QIODevice::ReadOnly))
        data = f.readAll();
    QJsonParseError pe{};
    const QJsonDocument d = QJsonDocument::fromJson(data, &pe);
    if (f.error() == QFileDevice::NoError && pe.error == QJsonParseError::NoError && d.isObject())
        return d.object();
    if (damaged)
        *damaged = true;
    // what can still be read: "<snapshot file>": "<title>" pairs
    QJsonObject rescued;
    static const QRegularExpression pair(QStringLiteral("\"(\\d{8}-\\d{6}\\.(?:md|rtf))\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
    QRegularExpressionMatchIterator it = pair.globalMatch(QString::fromUtf8(data));
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QJsonDocument v = QJsonDocument::fromJson("[\"" + m.captured(2).toUtf8() + "\"]");
        if (v.isArray() && v.array().size() == 1 && v.array().at(0).isString())
            rescued.insert(m.captured(1), v.array().at(0).toString());
    }
    return rescued;
}

static bool writeIndex(const QString &dir, const QJsonObject &o, QString *error)
{
    QSaveFile f(QDir(dir).filePath(QStringLiteral("index.json")));
    const QByteArray data = QJsonDocument(o).toJson();
    if (!f.open(QIODevice::WriteOnly) || safesave::write(f, data) != data.size() || !safesave::commit(f)) {
        if (error)
            *error = f.errorString().isEmpty() || f.errorString() == QLatin1String("Unknown error")
                         ? QObject::tr("Cannot write %1").arg(QDir::toNativeSeparators(QDir(dir).filePath(QStringLiteral("index.json"))))
                         : f.errorString();
        return false;
    }
    return true;
}

// reads the list, changes it and writes it back; a damaged list is first kept as "index.damaged-<time>.json"
// (never overwritten without a copy)
static bool updateIndex(const QString &dir, const std::function<void(QJsonObject &)> &change, QString *error)
{
    bool damaged = false;
    QJsonObject idx = readIndex(dir, &damaged);
    if (damaged) {
        const QString copy = QDir(dir).filePath(QStringLiteral("index.damaged-%1.json")
                                                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
        QString why;
        if (!QFile::exists(copy) && !safesave::copyFile(QDir(dir).filePath(QStringLiteral("index.json")), copy, &why)) {
            if (error)
                *error = QObject::tr("The list of snapshot titles is damaged and could not be kept: %1").arg(why);
            return false;
        }
    }
    change(idx);
    return writeIndex(dir, idx, error);
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
    if (!f.open(QIODevice::WriteOnly) || safesave::write(f, data) != data.size() || !safesave::commit(f)) {
        if (error)   // 0.64.2: the file's name when Qt gives no reason
            *error = f.errorString().isEmpty() || f.errorString() == QLatin1String("Unknown error")
                         ? QObject::tr("Cannot write %1").arg(QDir::toNativeSeparators(QDir(dir).filePath(name)))
                         : f.errorString();
        return false;
    }
    QString why;
    const QString shown = title.isEmpty() ? QObject::tr("Untitled Snapshot") : title;
    // the snapshot is there even when its title could not be stored: true, with the problem in *error
    if (!updateIndex(dir, [&](QJsonObject &idx) { idx.insert(name, shown); }, &why) && error)
        *error = QObject::tr("The snapshot was taken, but its title could not be stored: %1").arg(why);
    it->hasSnapshots = true;
    p->notifyItemChanged(it, false);
    return true;
}

void load(Project *p, const Snapshot &s, QTextDocument *into)
{
    load(p, s, into, nullptr);
}

bool load(Project *p, const Snapshot &s, QTextDocument *into, QString *error)
{
    QFile f(s.file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("The snapshot's file cannot be read: %1").arg(f.exists() ? f.errorString() : QObject::tr("it is gone"));
        return false;
    }
    const QByteArray data = f.readAll();
    if (f.error() != QFileDevice::NoError || data.size() != f.size()) {
        if (error)
            *error = QObject::tr("The snapshot's file could not be read whole (%1 of %2 bytes).").arg(data.size()).arg(f.size());
        return false;
    }
    p->docs()->deserialize(s.id, data, into, s.file.endsWith(QLatin1String(".rtf")));
    return true;
}

bool remove(Project *p, const Snapshot &s)
{
    const QString dir = QFileInfo(s.file).absolutePath();
    const bool ok = QFile::remove(s.file);
    if (ok) // a title left in the list for a file that is gone does no harm
        updateIndex(dir, [&](QJsonObject &idx) { idx.remove(QFileInfo(s.file).fileName()); }, nullptr);
    if (BinderItem *it = p->item(s.id)) {
        it->hasSnapshots = !list(p, s.id).isEmpty();
        p->notifyItemChanged(it, false);
    }
    return ok;
}

Totals totals(Project *p)
{
    Totals t;
    QDir root(p->snapshotsDir(QString()));
    for (const QFileInfo &d : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QFileInfoList files = QDir(d.absoluteFilePath()).entryInfoList(QDir::Files | QDir::Hidden);
        int n = 0;
        for (const QFileInfo &f : files) {
            t.bytes += f.size();
            if (f.suffix() == QLatin1String("md") || f.suffix() == QLatin1String("rtf"))
                ++n;
        }
        t.snapshots += n;
        if (n > 0)
            ++t.documents;
    }
    return t;
}

int removeAll(Project *p, QString *error)
{
    const Totals before = totals(p);
    QDir root(p->snapshotsDir(QString()));
    QStringList failed;
    for (const QFileInfo &d : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (!pathsafety::removeTreeInside(d.absoluteFilePath(), p->folderPath())) // (0.64.1, audit S-1)
            failed << d.fileName();
    for (BinderItem *it : p->allItems()) {
        if (!it->hasSnapshots)
            continue;
        it->hasSnapshots = !list(p, it->id).isEmpty();
        p->notifyItemChanged(it, false);
    }
    if (error && !failed.isEmpty())
        *error = QObject::tr("Some snapshots could not be deleted:\n%1").arg(failed.join(QLatin1Char('\n')));
    return before.snapshots - totals(p).snapshots;
}

bool setTitle(Project *, const Snapshot &s, const QString &title, QString *error)
{
    const QString dir = QFileInfo(s.file).absolutePath();
    return updateIndex(dir, [&](QJsonObject &idx) { idx.insert(QFileInfo(s.file).fileName(), title); }, error);
}

bool rollBack(Project *p, const Snapshot &s, QString *error, bool allowEmpty)
{
    QTextDocument tmp;
    if (!load(p, s, &tmp, error))
        return false;
    // a snapshot that gives no text replaces the text only when the writer said so (it may be damaged)
    if (tmp.isEmpty() && !allowEmpty) {
        if (error)
            *error = QObject::tr("The snapshot holds no text (it may be damaged). The current text was kept.");
        return false;
    }
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
        // pathological input: give up with a coarse diff. 0.81.0 (audit of 0.80.0, U-04): at 2,500 steps (the table
        // then holds about 6 million numbers, 25 MB; 6,000 steps before, about 144 MB for two long, very different
        // paragraphs)
        if (d > 2500)
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
