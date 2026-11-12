// The system test: Scrivener round trips with real projects (C-37, S-P08) and the long run over many
// sessions (S-E05). The .scriv is read here with a reader of the test's own (RTF to words, the .scrivx as a
// tree), not with the program's, so that a fault of the program's reader cannot hide in both sides.
// No Scrivener file is part of the repository: the projects come from outside at run time (FX-3, the public
// sample, and the writer's own projects named in QB_CS_SCRIV_SAMPLES).
#include "cs.h"

#include "core/Backup.h"
#include "core/Snapshots.h"
#include "core/Zip.h"
#include "import/ScrivenerFormat.h"
#include "text/TextProps.h"
#include "ui/MainWindow.h"
#include "ui/TextEditor.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUuid>
#include <QXmlStreamReader>

#include <random>

namespace cs {

using namespace qb;

namespace {

// ---------------------------------------------------------------- RTF to text (the test's own reader)
QChar cp1252(int v)
{
    static const ushort high[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                    0x2039, 0x0152, 0x8D,   0x017D, 0x8F,   0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};
    if (v >= 0x80 && v < 0xA0)
        return QChar(high[v - 0x80]);
    return QChar(ushort(v & 0xff));
}

struct RtfText {
    QString text;          // what a reader sees (field results kept, field instructions left out)
    QStringList pictures;  // "pict pngblip picw200 pich120 <bytes>"
};

RtfText rtfPlain(const QByteArray &b)
{
    static const QSet<QByteArray> skipDest = {"fonttbl",   "colortbl",  "expandedcolortbl", "stylesheet", "info",       "listtable",
                                              "listoverridetable", "rsidtbl", "generator", "xmlnstbl",  "themedata",  "colorschememapping",
                                              "latentstyles", "datastore", "fldinst", "filetbl", "revtbl", "pgdsctbl", "objdata", "listtext"};
    struct G {
        bool skip = false;
        int uc = 1;
        bool pict = false;
        bool nonshp = false;
    };
    RtfText o;
    QList<G> st;
    G cur;
    int pendingSkip = 0;
    QString picInfo;
    int picHex = 0;
    int picDepth = -1;
    const int n = int(b.size());
    for (int i = 0; i < n; ++i) {
        const char c = b[i];
        if (c == '{') {
            st.push_back(cur);
            if (i + 2 < n && b[i + 1] == '\\' && b[i + 2] == '*') {
                cur.skip = true;
                i += 2;
            }
            continue;
        }
        if (c == '}') {
            if (cur.pict && picDepth == int(st.size())) {
                o.pictures << picInfo + QStringLiteral(" %1").arg(picHex / 2);
                picDepth = -1;
            }
            if (!st.isEmpty())
                cur = st.takeLast();
            pendingSkip = 0;
            continue;
        }
        if (c == '\\') {
            if (i + 1 >= n)
                break;
            const char nx = b[i + 1];
            if (!std::isalpha(uchar(nx))) {
                ++i;
                if (nx == '\'') {
                    const int v = i + 2 < n ? b.mid(i + 1, 2).toInt(nullptr, 16) : 0;
                    i += 2;
                    if (pendingSkip > 0) {
                        --pendingSkip;
                        continue;
                    }
                    if (!cur.skip)
                        o.text += cp1252(v);
                    continue;
                }
                if (pendingSkip > 0) {
                    --pendingSkip;
                    continue;
                }
                if (cur.skip)
                    continue;
                if (nx == '\\' || nx == '{' || nx == '}')
                    o.text += QLatin1Char(nx);
                else if (nx == '~')
                    o.text += QLatin1Char(' ');
                else if (nx == '_')
                    o.text += QLatin1Char('-');
                else if (nx == '\n' || nx == '\r')
                    o.text += QLatin1Char('\n');
                continue;
            }
            int j = i + 1;
            while (j < n && std::isalpha(uchar(b[j])))
                ++j;
            const QByteArray word = b.mid(i + 1, j - i - 1);
            int k = j;
            bool neg = false;
            if (k < n && b[k] == '-') {
                neg = true;
                ++k;
            }
            const int ds = k;
            while (k < n && std::isdigit(uchar(b[k])))
                ++k;
            const bool hasNum = k > ds;
            if (!hasNum && neg)
                k = j;
            const long num = hasNum ? b.mid(ds, k - ds).toLong() * (neg ? -1 : 1) : 0;
            if (k < n && b[k] == ' ')
                ++k;
            i = k - 1;
            if (word == "nonshppict") {
                cur.nonshp = true;
                cur.skip = true;
                continue;
            }
            if (word == "pict") {
                if (!cur.nonshp) {
                    cur.pict = true;
                    picDepth = int(st.size());
                    picInfo = QStringLiteral("pict");
                    picHex = 0;
                }
                cur.skip = true;
                continue;
            }
            if (cur.pict) {
                if (word == "picw" || word == "pich" || word.endsWith("blip"))
                    picInfo += QLatin1Char(' ') + QString::fromLatin1(word) + (hasNum ? QString::number(num) : QString());
                continue;
            }
            if (skipDest.contains(word)) {
                cur.skip = true;
                continue;
            }
            if (word == "uc") {
                cur.uc = int(num);
                continue;
            }
            if (word == "u") {
                if (!cur.skip)
                    o.text += QChar(ushort(num < 0 ? num + 65536 : num));
                pendingSkip = cur.uc;
                continue;
            }
            if (pendingSkip > 0) {
                --pendingSkip;
                continue;
            }
            if (cur.skip)
                continue;
            if (word == "par" || word == "line" || word == "sect" || word == "page" || word == "row")
                o.text += QLatin1Char('\n');
            else if (word == "tab" || word == "cell")
                o.text += QLatin1Char('\t');
            else if (word == "emdash")
                o.text += QChar(0x2014);
            else if (word == "endash")
                o.text += QChar(0x2013);
            else if (word == "lquote")
                o.text += QChar(0x2018);
            else if (word == "rquote")
                o.text += QChar(0x2019);
            else if (word == "ldblquote")
                o.text += QChar(0x201C);
            else if (word == "rdblquote")
                o.text += QChar(0x201D);
            else if (word == "bullet")
                o.text += QChar(0x2022);
            continue;
        }
        if (c == '\r' || c == '\n')
            continue;
        if (cur.pict) {
            if (std::isxdigit(uchar(c)))
                ++picHex;
            continue;
        }
        if (pendingSkip > 0) {
            --pendingSkip;
            continue;
        }
        if (cur.skip)
            continue;
        o.text += uchar(c) < 128 ? QChar(c) : cp1252(uchar(c));
    }
    return o;
}

using Words = QHash<QString, int>;

Words wordsOf(const QString &s0)
{
    Words w;
    // Scrivener's style marks (<$Scr_Ps::0>, <!$Scr_Cs::1>, <$Scr_H::2> ...) are not words of the text: the program takes
    // them out and gives the paragraphs their styles (2026-11-12, the writer's projects of the full run)
    static const QRegularExpression marks(QStringLiteral("<!?\\$Scr_[A-Za-z]+(::[0-9]+)?>"));
    // and the syntax of Scrivener for Windows' inline notes kept as text ({\Scrv_fn=...\end_Scrv_fn},
    // {\Scrv_annot \color={\R=..\G=..\B=..} \text=...\end_Scrv_annot}): the program makes them inline notes
    static const QRegularExpression notes(QStringLiteral(
        "\\{?\\\\Scrv_annot\\s*\\\\color=\\{[^}]*\\}\\s*\\\\text=|\\\\end_Scrv_(annot|fn)\\}?|\\{?\\\\Scrv_fn="));
    QString s = s0;
    s.replace(marks, QStringLiteral(" "));
    s.replace(notes, QStringLiteral(" "));
    static const QRegularExpression re(QStringLiteral("[\\p{L}\\p{N}]+"));
    for (auto it = re.globalMatch(s); it.hasNext();)
        ++w[it.next().captured(0)];
    return w;
}

// the words of a that b lacks (as a multiset), at most `limit` named
QStringList missingWords(const Words &a, const Words &b, int limit = 12)
{
    QStringList out;
    for (auto it = a.begin(); it != a.end(); ++it) {
        const int m = it.value() - b.value(it.key());
        if (m > 0)
            out << (m > 1 ? QStringLiteral("%1 x%2").arg(it.key()).arg(m) : it.key());
    }
    std::sort(out.begin(), out.end());
    if (out.size() > limit)
        out = out.mid(0, limit) << QStringLiteral("... %1 more").arg(out.size() - limit);
    return out;
}

Words plus(Words a, const Words &b)
{
    for (auto it = b.begin(); it != b.end(); ++it)
        a[it.key()] += it.value();
    return a;
}

// ---------------------------------------------------------------- XML as a tree (the .scrivx)
struct XNode {
    QString name;
    QList<QPair<QString, QString>> attrs;
    QString text;
    QList<XNode> kids;
    const XNode *kid(const QString &n) const
    {
        for (const XNode &k : kids)
            if (k.name == n)
                return &k;
        return nullptr;
    }
    QString attr(const QString &n) const
    {
        for (const auto &a : attrs)
            if (a.first == n)
                return a.second;
        return QString();
    }
};

XNode readNode(QXmlStreamReader &r)
{
    XNode n;
    n.name = r.name().toString();
    for (const QXmlStreamAttribute &a : r.attributes())
        n.attrs << qMakePair(a.qualifiedName().toString(), a.value().toString());
    std::sort(n.attrs.begin(), n.attrs.end());
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement())
            n.kids << readNode(r);
        else if (r.isCharacters() && !r.isWhitespace())
            n.text += r.text().toString();
        else if (r.isEndElement())
            break;
    }
    n.text = n.text.trimmed();
    return n;
}

XNode parseXml(const QByteArray &data, QString *err)
{
    QXmlStreamReader r(data);
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement())
            return readNode(r);
    }
    if (err)
        *err = r.errorString();
    return XNode();
}

QString canon(const XNode &n, const QSet<QString> &skipAttrs = {}, const QSet<QString> &skipKids = {})
{
    QString s = QLatin1Char('<') + n.name;
    for (const auto &a : n.attrs)
        if (!skipAttrs.contains(a.first))
            s += QLatin1Char(' ') + a.first + QStringLiteral("=\"") + a.second + QLatin1Char('"');
    s += QLatin1Char('>') + n.text;
    for (const XNode &k : n.kids)
        if (!skipKids.contains(k.name))
            s += canon(k, skipAttrs, {});
    return s + QStringLiteral("</") + n.name + QLatin1Char('>');
}

// ---------------------------------------------------------------- a .scriv as the test sees it
struct SDoc {
    QString uuid, type, title, parent;
    QString meta;      // the item without its children and without Modified
    QString labelId, statusId;
    QString text;      // content.rtf as text
    Words words;       // of the text
    Words noteWords;   // of content.comments (comments and linked footnotes)
    int comments = 0, footnotes = 0;
    QStringList pictures;
    QString synopsis;
    Words notesRtf;    // notes.rtf
    int snapshots = 0;
};

struct SInv {
    QString error;
    QString scrivxRel;
    QMap<QString, QByteArray> files;   // path inside the .scriv -> SHA-1
    QStringList order;                 // parent/uuid in binder order
    QMap<QString, SDoc> docs;          // by uuid
    QMap<QString, QString> parts;      // the .scrivx's other top-level parts, as canonical text
    QString rootAttrs;
    QStringList features() const
    {
        int withText = 0, words = 0, comments = 0, footnotes = 0, pictures = 0, snaps = 0, meta = 0, synopses = 0;
        for (const SDoc &d : docs) {
            withText += d.words.isEmpty() ? 0 : 1;
            for (int v : d.words)
                words += v;
            comments += d.comments;
            footnotes += d.footnotes;
            pictures += int(d.pictures.size());
            snaps += d.snapshots;
            meta += d.meta.contains(QLatin1String("<MetaDataItem")) ? 1 : 0;
            synopses += d.synopsis.isEmpty() ? 0 : 1;
        }
        return {QStringLiteral("%1 items").arg(docs.size()), QStringLiteral("%1 with text").arg(withText), QStringLiteral("%1 words").arg(words),
                QStringLiteral("%1 comments").arg(comments), QStringLiteral("%1 footnotes").arg(footnotes), QStringLiteral("%1 pictures").arg(pictures),
                QStringLiteral("%1 snapshots").arg(snaps), QStringLiteral("%1 with custom metadata").arg(meta), QStringLiteral("%1 synopses").arg(synopses),
                QStringLiteral("parts: %1").arg(parts.keys().join(QLatin1Char(',')))};
    }
};

QByteArray sha1Of(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(&f);
    return h.result().toHex();
}

void walkBinder(const XNode &n, const QString &parent, SInv &inv)
{
    for (const XNode &k : n.kids) {
        if (k.name != QLatin1String("BinderItem"))
            continue;
        SDoc d;
        d.uuid = k.attr(QStringLiteral("UUID")).toUpper();
        d.type = k.attr(QStringLiteral("Type"));
        d.parent = parent;
        if (const XNode *t = k.kid(QStringLiteral("Title")))
            d.title = t->text;
        if (const XNode *m = k.kid(QStringLiteral("MetaData"))) {
            if (const XNode *l = m->kid(QStringLiteral("LabelID")))
                d.labelId = l->text;
            if (const XNode *s = m->kid(QStringLiteral("StatusID")))
                d.statusId = s->text;
        }
        d.meta = canon(k, {QStringLiteral("Modified")}, {QStringLiteral("Children")});
        inv.order << parent + QLatin1Char('/') + d.uuid;
        inv.docs.insert(d.uuid, d);
        if (const XNode *c = k.kid(QStringLiteral("Children")))
            walkBinder(*c, d.uuid, inv);
    }
}

SInv inventory(const QString &scriv)
{
    SInv inv;
    QDirIterator it(scriv, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        inv.files.insert(QDir(scriv).relativeFilePath(f), sha1Of(f));
    }
    for (const QString &f : QDir(scriv).entryList({QStringLiteral("*.scrivx")}, QDir::Files))
        inv.scrivxRel = f;
    if (inv.scrivxRel.isEmpty()) {
        inv.error = QStringLiteral("no .scrivx");
        return inv;
    }
    QFile x(scriv + QLatin1Char('/') + inv.scrivxRel);
    x.open(QIODevice::ReadOnly);
    const XNode root = parseXml(x.readAll(), &inv.error);
    inv.rootAttrs = canon(XNode{root.name, root.attrs, QString(), {}}, {QStringLiteral("Modified"), QStringLiteral("ModID")});
    for (const XNode &k : root.kids) {
        if (k.name == QLatin1String("Binder"))
            walkBinder(k, QString(), inv);
        else
            inv.parts.insert(k.name, canon(k));
    }
    const QString data = scriv + QStringLiteral("/Files/Data/");
    for (SDoc &d : inv.docs) {
        const QString dir = data + d.uuid + QLatin1Char('/');
        QString dirReal = dir;
        if (!QFileInfo::exists(dir))   // the folder may be named in another case
            for (const QString &e : QDir(data).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                if (e.compare(d.uuid, Qt::CaseInsensitive) == 0)
                    dirReal = data + e + QLatin1Char('/');
        QFile c(dirReal + QStringLiteral("content.rtf"));
        if (c.open(QIODevice::ReadOnly)) {
            const RtfText r = rtfPlain(c.readAll());
            d.text = r.text;
            d.words = wordsOf(r.text);
            d.pictures = r.pictures;
        }
        QFile cm(dirReal + QStringLiteral("content.comments"));
        if (cm.open(QIODevice::ReadOnly)) {
            QXmlStreamReader r(cm.readAll());
            while (!r.atEnd()) {
                r.readNext();
                if (r.isStartElement() && r.name() == QLatin1String("Comment")) {
                    const bool fn = r.attributes().value(QStringLiteral("Footnote")) == QLatin1String("Yes");
                    (fn ? d.footnotes : d.comments)++;
                    d.noteWords = plus(d.noteWords, wordsOf(rtfPlain(r.readElementText(QXmlStreamReader::IncludeChildElements).toUtf8()).text));
                }
            }
        }
        d.synopsis = readText(dirReal + QStringLiteral("synopsis.txt")).trimmed();
        QFile nr(dirReal + QStringLiteral("notes.rtf"));
        if (nr.open(QIODevice::ReadOnly))
            d.notesRtf = wordsOf(rtfPlain(nr.readAll()).text);
        const QString snaps = scriv + QStringLiteral("/Snapshots/") + d.uuid + QStringLiteral(".snapshots");
        d.snapshots = int(QDir(snaps).entryList({QStringLiteral("*.rtf")}, QDir::Files).size());
    }
    return inv;
}

// what a change is allowed to touch
struct Expect {
    QString textDoc;            // text appended (words of textMark)
    QString textMark;
    QString synopsisDoc, synopsisMark;
    QString labelDoc;           // its LabelID changes
    QString renamedDoc, newTitle;
    QString movedDoc;
    QString pictureDoc;         // one picture more
    QString snapshotDoc;        // one snapshot more
    bool strict = false;        // nothing may change at all (opening and closing)
};

QStringList compareInv(const SInv &o, const SInv &s, const Expect &e, QStringList *notes)
{
    QStringList d;
    if (!s.error.isEmpty())
        d << QStringLiteral("the saved .scrivx cannot be read: ") + s.error;
    // files
    auto allowed = [&](const QString &rel) {
        if (e.strict)
            return false;
        if (rel == o.scrivxRel || rel == QLatin1String("Files/docs.checksum") || rel == QLatin1String("Files/Data/docs.checksum")
            || rel == QLatin1String("Files/search.indexes") || rel.startsWith(QLatin1String("Files/binder.")))
            return true;
        // 0.90.0: the writing history counts the day's words, as Scrivener does, when a text changed
        if (rel == QLatin1String("Files/writing.history") && !e.textDoc.isEmpty())
            return true;
        for (const QString &u : {e.textDoc, e.pictureDoc})
            if (!u.isEmpty() && rel.startsWith(QStringLiteral("Files/Data/") + u + QLatin1Char('/'), Qt::CaseInsensitive))
                return true;
        if (!e.synopsisDoc.isEmpty() && rel.compare(QStringLiteral("Files/Data/%1/synopsis.txt").arg(e.synopsisDoc), Qt::CaseInsensitive) == 0)
            return true;
        if (!e.snapshotDoc.isEmpty() && rel.startsWith(QStringLiteral("Snapshots/%1.snapshots/").arg(e.snapshotDoc), Qt::CaseInsensitive))
            return true;
        return false;
    };
    QStringList changed, gone, added;
    for (auto it = o.files.begin(); it != o.files.end(); ++it) {
        if (!s.files.contains(it.key())) {
            // 0.81.3: Scrivener for Windows' copy of the last .scrivx goes when the .scrivx is written (it would be stale)
            if (!e.strict && it.key() == QLatin1String("Files/binder.autosave") && s.files.value(o.scrivxRel) != o.files.value(o.scrivxRel))
                continue;
            gone << it.key();
        }
        else if (s.files.value(it.key()) != it.value() && !allowed(it.key()))
            changed << it.key();
    }
    for (auto it = s.files.begin(); it != s.files.end(); ++it)
        if (!o.files.contains(it.key()) && !allowed(it.key()))
            added << it.key();
    if (!gone.isEmpty())
        d << QStringLiteral("files gone: ") + gone.mid(0, 8).join(QLatin1String(", "));
    if (!changed.isEmpty())
        d << QStringLiteral("files changed that the change did not touch: ") + changed.mid(0, 8).join(QLatin1String(", "));
    if (!added.isEmpty())
        d << QStringLiteral("new files: ") + added.mid(0, 8).join(QLatin1String(", "));
    // the binder
    for (const SDoc &a : o.docs) {
        if (!s.docs.contains(a.uuid)) {
            d << QStringLiteral("item gone: %1 (%2)").arg(a.title, a.uuid);
            continue;
        }
        const SDoc &b = s.docs.value(a.uuid);
        QString ma = a.meta, mb = b.meta;
        if (a.uuid == e.labelDoc) {
            if (a.labelId == b.labelId)
                d << QStringLiteral("%1: the label change is not in the .scrivx").arg(a.title);
            ma.replace(QStringLiteral("<LabelID>%1</LabelID>").arg(a.labelId), QString());
            mb.replace(QStringLiteral("<LabelID>%1</LabelID>").arg(b.labelId), QString());
        }
        if (a.uuid == e.renamedDoc) {
            if (b.title != e.newTitle)
                d << QStringLiteral("%1: the new title is \"%2\", not \"%3\"").arg(a.title, b.title, e.newTitle);
            ma.replace(QStringLiteral("<Title>%1</Title>").arg(a.title), QString());
            mb.replace(QStringLiteral("<Title>%1</Title>").arg(b.title), QString());
        }
        if (ma != mb)
            d << QStringLiteral("%1: its entry in the .scrivx differs: %2").arg(a.title, firstDifference(ma, mb, 60));
        // text and notes: the words of text and comments together (an inline note may become a linked one)
        Words wantAll = plus(a.words, a.noteWords), gotAll = plus(b.words, b.noteWords);
        if (a.uuid == e.textDoc)
            wantAll = plus(wantAll, wordsOf(e.textMark));
        const QStringList lost = missingWords(wantAll, gotAll), extra = missingWords(gotAll, wantAll);
        if (!lost.isEmpty())
            d << QStringLiteral("%1: words lost: %2").arg(a.title, lost.join(QLatin1String(", ")));
        if (!extra.isEmpty())
            d << QStringLiteral("%1: words added: %2").arg(a.title, extra.join(QLatin1String(", ")));
        if (a.comments + a.footnotes != b.comments + b.footnotes || a.footnotes != b.footnotes)
            notes->append(QStringLiteral("%1: comments %2 -> %3, footnotes %4 -> %5").arg(a.title).arg(a.comments).arg(b.comments).arg(a.footnotes).arg(b.footnotes));
        const int wantPics = int(a.pictures.size()) + (a.uuid == e.pictureDoc ? 1 : 0);
        if (int(b.pictures.size()) != wantPics)
            d << QStringLiteral("%1: %2 pictures, %3 expected").arg(a.title).arg(b.pictures.size()).arg(wantPics);
        else if (a.uuid != e.pictureDoc && a.pictures != b.pictures)
            notes->append(QStringLiteral("%1: pictures written anew (%2 -> %3)").arg(a.title, a.pictures.join(QLatin1String("; ")).left(120), b.pictures.join(QLatin1String("; ")).left(120)));
        const QString wantSyn = a.uuid == e.synopsisDoc ? (a.synopsis + QLatin1Char(' ') + e.synopsisMark).trimmed() : a.synopsis;
        if (b.synopsis.simplified() != wantSyn.simplified())
            d << QStringLiteral("%1: synopsis \"%2\", expected \"%3\"").arg(a.title, b.synopsis.left(80), wantSyn.left(80));
        const QStringList notesLost = missingWords(a.notesRtf, b.notesRtf);
        if (!notesLost.isEmpty())
            d << QStringLiteral("%1: words of the notes lost: %2").arg(a.title, notesLost.join(QLatin1String(", ")));
        const int wantSnaps = a.snapshots + (a.uuid == e.snapshotDoc ? 1 : 0);
        if (b.snapshots != wantSnaps)
            d << QStringLiteral("%1: %2 snapshots, %3 expected").arg(a.title).arg(b.snapshots).arg(wantSnaps);
    }
    for (const SDoc &b : s.docs)
        if (!o.docs.contains(b.uuid))
            d << QStringLiteral("new item: %1").arg(b.title);
    // the order, apart from the item moved
    QStringList oo = o.order, so = s.order;
    if (!e.movedDoc.isEmpty()) {
        oo.erase(std::remove_if(oo.begin(), oo.end(), [&](const QString &x) { return x.endsWith(e.movedDoc); }), oo.end());
        so.erase(std::remove_if(so.begin(), so.end(), [&](const QString &x) { return x.endsWith(e.movedDoc); }), so.end());
        if (o.order == s.order)
            d << QStringLiteral("the move is not in the .scrivx");
    }
    if (oo != so)
        d << QStringLiteral("the binder's order differs");
    // the other parts of the .scrivx: labels, status, keywords, metadata fields, collections ...
    for (auto it = o.parts.begin(); it != o.parts.end(); ++it) {
        if (!s.parts.contains(it.key()))
            d << QStringLiteral("part gone from the .scrivx: ") + it.key();
        else if (s.parts.value(it.key()) != it.value())
            d << QStringLiteral("part of the .scrivx changed: %1 (%2)").arg(it.key(), firstDifference(it.value(), s.parts.value(it.key()), 60));
    }
    for (auto it = s.parts.begin(); it != s.parts.end(); ++it)
        if (!o.parts.contains(it.key()))
            notes->append(QStringLiteral("new part in the .scrivx: ") + it.key());
    if (o.rootAttrs != s.rootAttrs)
        notes->append(QStringLiteral("the project's attributes: %1").arg(firstDifference(o.rootAttrs, s.rootAttrs, 60)));
    return d;
}

QStringList realSamples()
{
    QStringList out;
    QString fx3 = qEnvironmentVariable("QB_SCRIV_SAMPLE");
    if (fx3.isEmpty())
        fx3 = ctx().fixtures + QStringLiteral("/fx3/sample-project.scriv");
    if (QFileInfo::exists(fx3))
        out << fx3;
    for (const QString &w : qEnvironmentVariable("QB_CS_SCRIV_SAMPLES").split(QLatin1Char(':'), Qt::SkipEmptyParts))
        if (QFileInfo::exists(w))
            out << w;
    return out;
}

BinderItem *itemOfUuid(Project *p, const QString &uuid)
{
    for (BinderItem *it : p->allItems())
        if (scrivUuidOf(it->id).compare(uuid, Qt::CaseInsensitive) == 0)
            return it;
    return nullptr;
}

// the document with the most to lose: comments, footnotes, pictures, then words
QString richest(const SInv &o, const QSet<QString> &avoid = {})
{
    QString best;
    int score = -1;
    for (const SDoc &d : o.docs) {
        if (d.words.isEmpty() || avoid.contains(d.uuid))
            continue;
        const int sc = (d.comments + d.footnotes) * 1000 + int(d.pictures.size()) * 500 + int(d.words.size());
        if (sc > score) {
            score = sc;
            best = d.uuid;
        }
    }
    return best;
}

// every word the program holds for a document: its text (inline notes are in the flow), the texts of its
// linked comments and footnotes, its notes
Words programWords(Project *p, BinderItem *it)
{
    QTextDocument *d = p->docs()->text(it->id);
    QString s = d->toPlainText() + QLatin1Char(' ') + p->docs()->notes(it->id)->toPlainText();
    QSet<QString> seen;
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
        for (auto f = b.begin(); !f.atEnd(); ++f) {
            const QTextCharFormat cf = f.fragment().charFormat();
            const QString t = cf.property(props::NoteText).toString();
            const QString id = cf.property(props::NoteId).toString();
            if (!t.isEmpty() && !seen.contains(id + QLatin1Char('|') + t)) {
                seen.insert(id + QLatin1Char('|') + t);
                s += QLatin1Char(' ') + t;
            }
        }
    return wordsOf(s);
}

std::unique_ptr<Project> openScriv(const QString &scriv, QString *err)
{
    return std::move(scrivener::open(scriv, err).project);
}

// read the .scriv as another computer would: a fresh place for the working copy
std::unique_ptr<Project> openAfresh(const QString &scriv, QString *err, QString *home)
{
    *home = ctx().work + QStringLiteral("/afresh-%1").arg(QUuid::createUuid().toString(QUuid::Id128).left(8));
    QDir().mkpath(*home);
    qputenv("XDG_DATA_HOME", home->toUtf8());
    return openScriv(scriv, err);
}

QString copyOf(const QString &sample, const QString &tag)
{
    const QString copy = ctx().work + QLatin1Char('/') + tag + QLatin1Char('/') + QFileInfo(sample).fileName();
    QDir(QFileInfo(copy).absolutePath()).removeRecursively();
    QDir().mkpath(QFileInfo(copy).absolutePath());
    copyTree(sample, copy);
    return copy;
}

} // namespace

// ================================================================ C-37 real Scrivener projects: open, save, compare
void caseC37()
{
    const QStringList samples = realSamples();
    if (samples.isEmpty()) {
        note(QStringLiteral("[N] no real Scrivener project (QB_SCRIV_SAMPLE, QB_CS_SCRIV_SAMPLES): left out"));
        return;
    }
    const QByteArray home0 = qgetenv("XDG_DATA_HOME");
    int n = 0;
    for (const QString &sample : samples) {
        ++n;
        const QString name = n == 1 ? QStringLiteral("FX-3") : QStringLiteral("writer's project %1").arg(n - 1);
        step(name);
        const QString copy = copyOf(sample, QStringLiteral("c37-%1").arg(n));
        const SInv o = inventory(copy);
        check(o.error.isEmpty(), QStringLiteral("%1: the test reads the original").arg(name), o.error);
        if (!o.error.isEmpty())
            continue;
        fact(QStringLiteral("C-37 %1 holds").arg(name), o.features().join(QLatin1String(", ")));
        QStringList notes;
        // 1. opening: everything is there
        QString err;
        auto p = openScriv(copy, &err);
        check(p != nullptr, QStringLiteral("%1: it opens").arg(name), err);
        if (!p)
            continue;
        int found = 0;
        QStringList lostOnOpen;
        for (const SDoc &d : o.docs) {
            BinderItem *it = itemOfUuid(p.get(), d.uuid);
            if (!it)
                continue;
            ++found;
            if (it->hasText() && !(d.words.isEmpty() && d.noteWords.isEmpty() && d.notesRtf.isEmpty())) {
                const QStringList lost = missingWords(plus(plus(d.words, d.noteWords), d.notesRtf), programWords(p.get(), it), 6);
                if (!lost.isEmpty())
                    lostOnOpen << QStringLiteral("%1: %2").arg(d.title, lost.join(QLatin1String(", ")));
            }
        }
        check(found == o.docs.size(), QStringLiteral("%1: every binder item is in the program (%2 of %3)").arg(name).arg(found).arg(o.docs.size()));
        check(lostOnOpen.isEmpty(), QStringLiteral("%1: the program shows every word of every text").arg(name), lostOnOpen.mid(0, 6).join(QLatin1String("; ")));
        // 2. opening and closing without a change writes nothing
        p->save(&err);
        p.reset();
        {
            const SInv s0 = inventory(copy);
            Expect none;
            none.strict = true;
            const QStringList d = compareInv(o, s0, none, &notes);
            // 0.64.2: the first lines of the .scrivx that differ (both versions are kept in the case's folder)
            QString scrivxDiff;
            if (!d.isEmpty()) {
                const QString orig = scrivener::scrivxIn(sample), now = scrivener::scrivxIn(copy);
                QFile::copy(orig, ctx().dir + QStringLiteral("/c37-%1-original.scrivx").arg(n));
                QFile::copy(now, ctx().dir + QStringLiteral("/c37-%1-after.scrivx").arg(n));
                const QStringList a = readText(orig).split(QLatin1Char('\n')), b = readText(now).split(QLatin1Char('\n'));
                int shown = 0;
                for (int i = 0; i < qMax(a.size(), b.size()) && shown < 4; ++i)
                    if (a.value(i) != b.value(i)) {
                        scrivxDiff += QStringLiteral("\nline %1: %2 | %3").arg(i + 1).arg(a.value(i).trimmed().left(160), b.value(i).trimmed().left(160));
                        ++shown;
                    }
            }
            check(d.isEmpty(), QStringLiteral("%1: opened and closed without a change, the .scriv is as it was (byte for byte)").arg(name), d.mid(0, 8).join(QLatin1Char('\n')) + scrivxDiff);
        }
        // 3. a change in three places, a full save
        p = openScriv(copy, &err);
        if (!p)
            continue;
        Expect e;
        e.textDoc = richest(o);
        e.textMark = QStringLiteral("Roundtrip mark C37 written by the system test.");
        e.synopsisDoc = richest(o, {e.textDoc});
        e.synopsisMark = QStringLiteral("Synopsis mark C37.");
        for (const SDoc &d : o.docs)
            if (d.uuid != e.textDoc && d.uuid != e.synopsisDoc && !d.words.isEmpty() && e.labelDoc.isEmpty())
                e.labelDoc = d.uuid;
        if (BinderItem *a = itemOfUuid(p.get(), e.textDoc)) {
            QTextCursor c(p->docs()->text(a->id));
            c.movePosition(QTextCursor::End);
            c.insertText(QLatin1Char(' ') + e.textMark);
        }
        if (BinderItem *b = itemOfUuid(p.get(), e.synopsisDoc)) {
            b->synopsis = (b->synopsis + QLatin1Char(' ') + e.synopsisMark).trimmed();
            p->notifyItemChanged(b);
        }
        int newLabel = -1;
        if (BinderItem *c = itemOfUuid(p.get(), e.labelDoc)) {
            for (const Label &l : p->labels)
                if (l.id != c->label && newLabel < 0)
                    newLabel = l.id;
            if (newLabel >= 0) {
                c->label = newLabel;
                p->notifyItemChanged(c);
            } else
                e.labelDoc.clear();
        }
        check(p->save(&err), QStringLiteral("%1: the full save").arg(name), err);
        p.reset();
        const SInv s1 = inventory(copy);
        const QStringList d1 = compareInv(o, s1, e, &notes);
        check(d1.isEmpty(), QStringLiteral("%1: after a change of text, synopsis and label, everything else is as it was").arg(name), d1.mid(0, 10).join(QLatin1Char('\n')));
        // 4. read afresh, as another computer would
        QString home;
        auto q = openAfresh(copy, &err, &home);
        qputenv("XDG_DATA_HOME", home0);
        if (q) {
            BinderItem *a = itemOfUuid(q.get(), e.textDoc);
            BinderItem *b = itemOfUuid(q.get(), e.synopsisDoc);
            BinderItem *c = itemOfUuid(q.get(), e.labelDoc);
            check(a && q->docs()->text(a->id)->toPlainText().contains(e.textMark), QStringLiteral("%1: read afresh, the text has the change").arg(name));
            check(b && b->synopsis.contains(e.synopsisMark), QStringLiteral("%1: read afresh, the synopsis has the change").arg(name));
            if (!e.labelDoc.isEmpty())
                check(c && c->label == newLabel, QStringLiteral("%1: read afresh, the label is the new one").arg(name), c ? QString::number(c->label) : QString());
            q.reset();
        } else
            check(false, QStringLiteral("%1: read afresh").arg(name), err);
        QDir(home).removeRecursively();
        // 5. a second round without a change: no drift
        p = openScriv(copy, &err);
        if (p) {
            p->save(&err);
            p.reset();
        }
        Expect none;
        none.strict = true;
        const QStringList d2 = compareInv(s1, inventory(copy), none, &notes);
        check(d2.isEmpty(), QStringLiteral("%1: a second opening and closing changes nothing").arg(name), d2.mid(0, 8).join(QLatin1Char('\n')));
        fact(QStringLiteral("C-37 %1 differences of form").arg(name), notes.isEmpty() ? QStringLiteral("none") : notes.mid(0, 20).join(QLatin1String("; ")));
    }
    if (samples.size() == 1)
        note(QStringLiteral("[W] only the public sample: comments, footnotes, pictures or snapshots it lacks are not covered (see the features above); "
                            "QB_CS_SCRIV_SAMPLES=<a.scriv>:<b.scriv> adds copies of the writer's own projects"));
}

// ================================================================ S-P08 a real Scrivener project through the window, compared afterwards
void caseSP08()
{
    const QStringList samples = realSamples();
    if (samples.isEmpty()) {
        note(QStringLiteral("[N] no real Scrivener project (QB_SCRIV_SAMPLE, QB_CS_SCRIV_SAMPLES): left out"));
        return;
    }
    const QStringList use = ctx().quick ? samples.mid(0, 1) : samples;
    int n = 0;
    for (const QString &sample : use) {
        ++n;
        const QString name = n == 1 ? QStringLiteral("FX-3") : QStringLiteral("writer's project %1").arg(n - 1);
        step(name);
        const QString copy = copyOf(sample, QStringLiteral("sp08-%1").arg(n));
        const SInv o = inventory(copy);
        if (!o.error.isEmpty()) {
            check(false, QStringLiteral("%1: the test reads the original").arg(name), o.error);
            continue;
        }
        MainWindow *w = openWindow(copy + QLatin1Char('/') + o.scrivxRel);
        Project *p = w->project();
        check(p != nullptr, QStringLiteral("%1: it opens in the window").arg(name));
        if (!p) {
            delete w;
            continue;
        }
        Expect e;
        e.textDoc = e.pictureDoc = e.snapshotDoc = richest(o);
        e.textMark = QStringLiteral("Typed in the window for SPeight.");
        BinderItem *a = itemOfUuid(p, e.textDoc);
        const int ourSnapsBefore = a ? int(snapshots::list(p, a->id).size()) : 0;
        if (TextEditor *ed = a ? showDoc(w, a) : nullptr) {
            QTextCursor c = ed->textCursor();
            c.movePosition(QTextCursor::End);
            ed->setTextCursor(c);
            ed->setFocus(Qt::OtherFocusReason);
            typeInto(ed, QLatin1Char(' ') + e.textMark);
            insertPicture(p, a, p->docs()->text(a->id), p->docs()->text(a->id)->characterCount() - 1, testPicture(160, 100));
            settle(300);
            selectItems(w, {a});
            chooseByMenu(w, QStringLiteral("snapshot"));
            settle(800);
        } else
            check(false, QStringLiteral("%1: the richest document is shown").arg(name));
        // a rename through the binder's model (as typing a new name does) and a move
        for (const SDoc &d : o.docs)
            if (d.uuid != e.textDoc && !d.words.isEmpty() && e.renamedDoc.isEmpty())
                e.renamedDoc = d.uuid;
        if (BinderItem *b = itemOfUuid(p, e.renamedDoc)) {
            e.newTitle = b->title + QStringLiteral(" (renamed)");
            p->model()->setData(p->model()->indexFor(b), e.newTitle, Qt::EditRole);
        }
        for (const SDoc &d : o.docs) {
            BinderItem *m = itemOfUuid(p, d.uuid);
            if (m && d.uuid != e.textDoc && d.uuid != e.renamedDoc && m->parent && m->parent->children.size() >= 2 && m->hasText() && e.movedDoc.isEmpty()) {
                e.movedDoc = d.uuid;
                p->model()->moveItems({m}, m->parent, m->row() == 0 ? int(m->parent->children.size()) : 0);
            }
        }
        settle(500);
        check(w->closeProject(), QStringLiteral("%1: Close Project").arg(name));
        settle(500);
        QStringList notes;
        const SInv s = inventory(copy);
        const QStringList d = compareInv(o, s, e, &notes);
        check(d.isEmpty(), QStringLiteral("%1: after typing, a picture, a snapshot, a rename and a move, everything else is as it was").arg(name),
              d.mid(0, 10).join(QLatin1Char('\n')));
        check(!QFileInfo::exists(copy + QStringLiteral("/quillbinder.lock")), QStringLiteral("%1: the program's lock is gone after closing").arg(name));
        fact(QStringLiteral("S-P08 %1 differences of form").arg(name), notes.isEmpty() ? QStringLiteral("none") : notes.mid(0, 20).join(QLatin1String("; ")));
        // open again: everything as the writer left it
        w->openScrivenerProject(copy + QLatin1Char('/') + o.scrivxRel);
        settle(1500);
        p = w->project();
        if (p) {
            BinderItem *a2 = itemOfUuid(p, e.textDoc);
            int pictures = 0;
            if (a2)
                for (QTextBlock b = p->docs()->text(a2->id)->begin(); b.isValid(); b = b.next())
                    for (auto f = b.begin(); !f.atEnd(); ++f)
                        pictures += f.fragment().charFormat().isImageFormat() ? 1 : 0;
            check(a2 && docText(p, a2).contains(e.textMark), QStringLiteral("%1: the typed text is there after opening again").arg(name));
            check(pictures == int(o.docs.value(e.textDoc).pictures.size()) + 1, QStringLiteral("%1: the picture is there").arg(name), QString::number(pictures));
            check(a2 && int(snapshots::list(p, a2->id).size()) == ourSnapsBefore + 1, QStringLiteral("%1: the snapshot is listed").arg(name));
            BinderItem *b2 = itemOfUuid(p, e.renamedDoc);
            check(!b2 || b2->title == e.newTitle, QStringLiteral("%1: the new title").arg(name));
            w->closeProject();
            Expect none;
            none.strict = true;
            const QStringList d2 = compareInv(s, inventory(copy), none, &notes);
            check(d2.isEmpty(), QStringLiteral("%1: opening and closing again changes nothing").arg(name), d2.mid(0, 8).join(QLatin1Char('\n')));
        } else
            check(false, QStringLiteral("%1: it opens again").arg(name));
        delete w;
    }
}

// ================================================================ S-E05 the long run: many sessions with backups and restores
namespace {

struct Exp {
    QString title, text, notes, synopsis;
};

QString keyOf(Project *p, BinderItem *it) { return p->scrivenerPath.isEmpty() ? it->id : scrivUuidOf(it->id); }

QHash<QString, Exp> expectationOf(Project *p)
{
    QHash<QString, Exp> e;
    for (BinderItem *it : p->allItems()) {
        if (!it->parent)
            continue;
        Exp x;
        x.title = it->title;
        x.synopsis = it->synopsis;
        if (it->hasText()) {
            x.text = p->docs()->text(it->id)->toPlainText();
            x.notes = p->docs()->notes(it->id)->toPlainText();
        }
        e.insert(keyOf(p, it), x);
    }
    return e;
}

QStringList compareExp(Project *p, const QHash<QString, Exp> &want)
{
    QStringList d;
    QSet<QString> seen;
    for (BinderItem *it : p->allItems()) {
        if (!it->parent)
            continue;
        const QString k = keyOf(p, it);
        seen.insert(k);
        if (!want.contains(k)) {
            d << QStringLiteral("new: %1").arg(it->title);
            continue;
        }
        const Exp &x = want.value(k);
        if (it->title != x.title)
            d << QStringLiteral("title \"%1\", expected \"%2\"").arg(it->title, x.title);
        if (it->synopsis != x.synopsis)
            d << QStringLiteral("%1: synopsis changed").arg(it->title);
        if (it->hasText()) {
            const QString t = p->docs()->text(it->id)->toPlainText();
            if (t != x.text)
                d << QStringLiteral("%1: text: %2").arg(it->title, firstDifference(x.text, t, 40));
            if (p->docs()->notes(it->id)->toPlainText() != x.notes)
                d << QStringLiteral("%1: notes changed").arg(it->title);
        }
    }
    for (auto it = want.begin(); it != want.end(); ++it)
        if (!seen.contains(it.key()))
            d << QStringLiteral("missing: %1").arg(it.value().title);
    return d;
}

QString sentence(std::mt19937 &r, int cycle, int k)
{
    static const QStringList w = {QStringLiteral("lantern"), QStringLiteral("harbor"), QStringLiteral("quietly"), QStringLiteral("winter"),
                                  QStringLiteral("letters"), QStringLiteral("between"), QStringLiteral("orchard"), QStringLiteral("the"),
                                  QStringLiteral("small"), QStringLiteral("forgotten"), QStringLiteral("road"), QStringLiteral("and")};
    QStringList s{QStringLiteral("Cycle%1n%2").arg(cycle).arg(k)};
    for (int i = 0; i < 6 + int(r() % 8); ++i)
        s << w.at(int(r() % w.size()));
    return s.join(QLatin1Char(' ')) + QLatin1Char('.');
}

int tempFilesIn(const QString &folder)
{
    int n = 0;
    static const QRegularExpression re(QStringLiteral("\\.[A-Za-z0-9]{6}$|\\.part$"));
    QDirIterator it(folder, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        // 0.64.2: a project's own ".scrivx" has six letters after its dot too (S-E05 counted it in every cycle)
        if (re.match(f).hasMatch() && !f.endsWith(QLatin1String(".scrivx"), Qt::CaseInsensitive))
            ++n;
    }
    return n;
}

qint64 sizeOf(const QString &folder, int *files)
{
    qint64 s = 0;
    *files = 0;
    QDirIterator it(folder, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        s += it.fileInfo().size();
        ++*files;
    }
    return s;
}

struct Kept {
    QString zip;
    QHash<QString, Exp> exp;
    QJsonObject model;
    int cycle = 0;
};

void longRun(const QString &fixture)
{
    const bool scriv = fixture.endsWith(QLatin1String("scriv"));
    const QString what = scriv ? QStringLiteral("Scrivener format") : QStringLiteral("Quillbinder format");
    QString path = fixtureCopy(fixture);
    const int cycles = ctx().smoke ? 2 : ctx().quick ? 12 : 60;
    std::mt19937 r(ctx().seed + (scriv ? 7u : 3u));
    QString err;
    auto openIt = [&](const QString &pp) { return pp.endsWith(QLatin1String(".scriv")) ? openScriv(pp, &err) : Project::open(pp, &err); };
    std::unique_ptr<Project> p = openIt(path);
    if (!p) {
        check(false, QStringLiteral("%1: the large project opens").arg(what), err);
        return;
    }
    QHash<QString, Exp> exp = expectationOf(p.get());
    QJsonObject model = modelOf(p.get(), scriv);
    const QString backups = ctx().work + QStringLiteral("/se05-backups-") + fixture;
    QList<Kept> kept;
    QStringList problems, times, afterEdits;
    int restores = 0, switches = 0, totalEdits = 0;
    QElapsedTimer t;
    for (int cycle = 1; cycle <= cycles; ++cycle) {
        step(QStringLiteral("%1, cycle %2 of %3").arg(what).arg(cycle).arg(cycles));
        // edits, each also made in the expectation
        QList<BinderItem *> docs = textItems(p.get());
        auto pick = [&]() { return docs.value(int(r() % qMax(1, int(docs.size())))); };
        for (int k = 0; k < 3; ++k) {   // sentences at the end
            BinderItem *it = pick();
            const QString s = QLatin1Char(' ') + sentence(r, cycle, k);
            QTextCursor c(p->docs()->text(it->id));
            c.movePosition(QTextCursor::End);
            c.insertText(s);
            exp[keyOf(p.get(), it)].text += s;
        }
        auto plain = [&](BinderItem *it) {   // a text without tables or figures (positions as in the plain text)
            for (int tries = 0; tries < 8 && it; ++tries, it = pick())
                if (p->docs()->text(it->id)->rootFrame()->childFrames().isEmpty())
                    return it;
            return static_cast<BinderItem *>(nullptr);
        };
        if (BinderItem *it = plain(pick())) {   // a paragraph in the middle
            QTextDocument *d = p->docs()->text(it->id);
            const int blocks = d->blockCount();
            const QTextBlock b = d->findBlockByNumber(int(r() % qMax(1, blocks)));
            const QString s = sentence(r, cycle, 9) + QLatin1Char('\n');
            QTextCursor c(d);
            c.setPosition(b.position());
            c.insertText(s);
            exp[keyOf(p.get(), it)].text.insert(b.position(), s);
        }
        if (BinderItem *it = plain(pick())) {   // a piece taken out
            QTextDocument *d = p->docs()->text(it->id);
            if (d->characterCount() > 300) {
                const int at = 50 + int(r() % (d->characterCount() - 120));
                QTextCursor c(d);
                c.setPosition(at);
                c.setPosition(at + 25, QTextCursor::KeepAnchor);
                c.removeSelectedText();
                exp[keyOf(p.get(), it)].text.remove(at, 25);
            }
        }
        {   // bold on a few words (the model compares the format runs)
            BinderItem *it = pick();
            QTextCursor c(p->docs()->text(it->id));
            c.setPosition(qMin(10, p->docs()->text(it->id)->characterCount() - 1));
            c.movePosition(QTextCursor::NextWord, QTextCursor::KeepAnchor, 3);
            QTextCharFormat f;
            f.setFontWeight(cycle % 2 ? QFont::Bold : QFont::Normal);
            c.mergeCharFormat(f);
        }
        {   // title, synopsis, notes
            BinderItem *a = pick(), *b = pick(), *c = pick();
            a->title = a->title.section(QStringLiteral(" ~"), 0, 0) + QStringLiteral(" ~%1").arg(cycle);
            p->notifyItemChanged(a);
            exp[keyOf(p.get(), a)].title = a->title;
            b->synopsis += QStringLiteral(" Syn%1.").arg(cycle);
            p->notifyItemChanged(b);
            exp[keyOf(p.get(), b)].synopsis = b->synopsis;
            QTextCursor nc(p->docs()->notes(c->id));
            nc.movePosition(QTextCursor::End);
            nc.insertText(QStringLiteral(" Note%1.").arg(cycle));
            exp[keyOf(p.get(), c)].notes += QStringLiteral(" Note%1.").arg(cycle);
        }
        if (cycle % 2 == 0) {   // a new document
            BinderItem *folder = p->draft()->children.value(int(r() % qMax(1, int(p->draft()->children.size()))));
            if (!folder || !folder->isFolder())
                folder = p->draft();
            BinderItem *it = p->model()->createItem(folder, -1, ItemType::Text, QStringLiteral("New in cycle %1").arg(cycle));
            const QString s = sentence(r, cycle, 20);
            QTextCursor(p->docs()->text(it->id)).insertText(s);
            Exp x;
            x.title = it->title;
            x.text = s;
            exp.insert(keyOf(p.get(), it), x);
        }
        if (cycle % 3 == 0) {   // a move
            BinderItem *it = pick();
            BinderItem *to = p->draft()->children.value(int(r() % qMax(1, int(p->draft()->children.size()))));
            if (it && to && to->isFolder() && to != it && !to->isDescendantOf(it))
                p->model()->moveItems({it}, to, int(to->children.size()));
        }
        if (cycle % 5 == 0)   // into the Trash (still in the project)
            if (BinderItem *it = pick())
                p->model()->moveToTrash({it});
        if (cycle % 10 == 0) {   // Empty Trash
            for (BinderItem *it : p->allItems())
                if (it->parent && p->root())
                    for (BinderItem *x = it->parent; x; x = x->parent)
                        if (x->type == ItemType::Trash) {
                            exp.remove(keyOf(p.get(), it));
                            break;
                        }
            p->model()->emptyTrash();
        }
        totalEdits += 9;
        // autosave, the full save, sometimes a backup, close
        p->autosave();
        t.start();
        const bool saved = p->save(&err);
        const qint64 saveMs = t.elapsed();
        if (!saved)
            problems << QStringLiteral("cycle %1: the save failed: %2").arg(cycle).arg(err);
        // the texts in memory after saving are what the edits made (the test's own account of them)
        const QStringList now = compareExp(p.get(), exp);
        if (!now.isEmpty())
            afterEdits << QStringLiteral("cycle %1: %2").arg(cycle).arg(now.mid(0, 3).join(QLatin1String("; ")));
        model = modelOf(p.get(), scriv);
        exp = expectationOf(p.get());   // ids of the new items, moves: from here the program's state is the expectation
        if (cycle % 3 == 0) {
            QString zip;
            if (backup::backupProject(p.get(), backups, 5, &zip, &err) && !zip.isEmpty())
                kept << Kept{zip, exp, model, cycle};
            else
                problems << QStringLiteral("cycle %1: the backup failed: %2").arg(cycle).arg(err);
        }
        const QString folder = scriv ? p->scrivenerPath : p->folderPath();
        p.reset();
        // reopen and compare
        t.start();
        p = openIt(path);
        const qint64 openMs = t.elapsed();
        if (!p) {
            problems << QStringLiteral("cycle %1: it does not open: %2").arg(cycle).arg(err);
            break;
        }
        const QStringList d = compareExp(p.get(), exp);
        const QString md = modelDifference(model, modelOf(p.get(), scriv));
        if (!d.isEmpty())
            problems << QStringLiteral("cycle %1, after reopening: %2").arg(cycle).arg(d.mid(0, 4).join(QLatin1String("; ")));
        if (!md.isEmpty())
            problems << QStringLiteral("cycle %1, after reopening, the model: %2").arg(cycle).arg(md);
        int files = 0;
        const qint64 bytes = sizeOf(folder, &files);
        const int temps = tempFilesIn(folder);
        if (temps > 0)
            problems << QStringLiteral("cycle %1: %2 temporary files left").arg(cycle).arg(temps);
        if (cycle == 1 || cycle == cycles || cycle % 10 == 0)
            times << QStringLiteral("cycle %1: save %2 ms, open %3 ms, %4 files, %5 KiB").arg(cycle).arg(saveMs).arg(openMs).arg(files).arg(bytes / 1024);
        // restore the newest backup into a new folder and compare it with what it should hold
        if (cycle % 4 == 0 && !kept.isEmpty()) {
            const Kept &k = kept.last();
            const QString into = ctx().work + QStringLiteral("/se05-restore-%1-%2").arg(fixture).arg(cycle);
            QDir(into).removeRecursively();
            ZipReader z(k.zip);
            if (!z.isValid() || !z.extractAll(into)) {
                problems << QStringLiteral("cycle %1: the backup of cycle %2 cannot be unpacked").arg(cycle).arg(k.cycle);
                continue;
            }
            QString restored;
            QDirIterator di(into, {scriv ? QStringLiteral("*.scrivx") : QStringLiteral("*.qbx")}, QDir::Files, QDirIterator::Subdirectories);
            while (di.hasNext() && restored.isEmpty()) {
                const QString f = di.next();
                // 0.64.2: never the spare copy in settings (opened, it showed every text empty)
                if (!scriv && QFileInfo(f).absoluteDir().dirName() == QLatin1String("settings"))
                    continue;
                if (!scriv || !f.contains(QLatin1String(" - working copy of ")))
                    restored = scriv ? QFileInfo(f).absolutePath() : f;
            }
            ++restores;
            auto q = openIt(restored);
            if (!q) {
                problems << QStringLiteral("cycle %1: the restored backup of cycle %2 does not open: %3").arg(cycle).arg(k.cycle).arg(err);
                continue;
            }
            const QStringList rd = compareExp(q.get(), k.exp);
            if (!rd.isEmpty())
                problems << QStringLiteral("cycle %1, the backup of cycle %2: %3").arg(cycle).arg(k.cycle).arg(rd.mid(0, 4).join(QLatin1String("; ")));
            const QString rmd = modelDifference(scriv ? k.model : k.model, modelOf(q.get(), scriv));
            if (!rmd.isEmpty())
                problems << QStringLiteral("cycle %1, the backup of cycle %2, the model: %3").arg(cycle).arg(k.cycle).arg(rmd);
            if (cycle % 8 == 0) {   // the writer goes on with the restored copy
                p = std::move(q);
                path = scriv ? restored : p->indexPath();
                exp = k.exp;
                model = k.model;
                ++switches;
            }
        }
    }
    fact(QStringLiteral("S-E05 %1").arg(what), QStringLiteral("%1 cycles, %2 edits, %3 backups, %4 restores, %5 times on with a restored copy")
                                                    .arg(cycles).arg(totalEdits).arg(kept.size()).arg(restores).arg(switches));
    fact(QStringLiteral("S-E05 %1 times").arg(what), times.join(QLatin1String("; ")));
    fact(QStringLiteral("S-E05 %1 the test's own account of the edits against memory").arg(what),
         afterEdits.isEmpty() ? QStringLiteral("the same in every cycle") : afterEdits.mid(0, 6).join(QLatin1String(" | ")));
    check(problems.isEmpty(), QStringLiteral("%1: after every cycle and every restore, nothing missing or altered").arg(what),
          problems.mid(0, 10).join(QLatin1Char('\n')) + (problems.size() > 10 ? QStringLiteral("\n... %1 more").arg(problems.size() - 10) : QString()));
    p.reset();
}

} // namespace

void caseSE05()
{
    longRun(QStringLiteral("fx2-qb"));
    longRun(QStringLiteral("fx2-scriv"));
}

} // namespace cs
