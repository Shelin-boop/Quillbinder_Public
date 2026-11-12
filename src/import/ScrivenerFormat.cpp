#include "import/ScrivenerFormat.h"
#include "core/SafeSave.h"
#include "core/Timing.h"
#include "core/IconCatalog.h"
#include "core/MindMap.h"

#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Paths.h"
#include "core/Project.h"
#include "core/Snapshots.h"
#include "core/SyncSafety.h"
#include "core/WebArchive.h"
#include "core/PathSafety.h"
#include "import/ScrivenerImporter.h"
#include "text/RtfIO.h"
#include "text/Styles.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"

#include "AppInfo.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QSet>
#include <QSysInfo>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextStream>
#include <QUuid>
#include <algorithm>
#include <functional>

namespace qb {
namespace scrivener {

namespace {

QString sha1(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
}

QString sha1File(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

// large files (pictures, PDFs, audio) are compared by size and time
constexpr int kMetaLevel = 2;   // see mergeFromOld

// The operator of a search as Scrivener writes it ("Any" seen in a file; the others a guess, see queryFromScrivener)
QString operatorOf(const SearchQuery &q)
{
    if (q.regex)
        return QStringLiteral("RegEx");
    if (q.exactPhrase)
        return QStringLiteral("Exact");
    if (q.anyWord)
        return QStringLiteral("Any");
    return QStringLiteral("All");
}



// The writing history as Scrivener 3.1.6.0 for Windows writes Files/writing.history (0.90.0): one Day per date, in
// order, with the words and characters written in the draft (dwc, dcc) and elsewhere (owc, occ). What else the file
// says of a day (the first day's totals, dtwc and dtcc) stays while that day is there. "" when the file is as it should be.
QByteArray writingHistory(const QList<WritingDay> &days, const QByteArray &old)
{
    struct Was { QList<QPair<QString, QString>> attrs; };
    QHash<QDate, QList<QPair<QString, QString>>> extra;
    QList<WritingDay> had;
    {
        QDomDocument d;
        if (!old.isEmpty() && d.setContent(old))
            for (QDomElement e = d.documentElement().firstChildElement(QStringLiteral("Day")); !e.isNull(); e = e.nextSiblingElement(QStringLiteral("Day"))) {
                WritingDay w;
                w.date = QDate::fromString(e.text().trimmed(), QStringLiteral("yyyy-MM-dd"));
                w.draftWords = e.attribute(QStringLiteral("dwc")).toInt();
                w.draftChars = e.attribute(QStringLiteral("dcc")).toInt();
                w.otherWords = e.attribute(QStringLiteral("owc")).toInt();
                w.otherChars = e.attribute(QStringLiteral("occ")).toInt();
                had << w;
                for (const char *k : {"dtwc", "dtcc"})
                    if (e.hasAttribute(QLatin1String(k)))
                        extra[w.date].append({QLatin1String(k), e.attribute(QLatin1String(k))});
            }
    }
    QList<WritingDay> want;
    for (const WritingDay &w : days)
        if (w.date.isValid())
            want << w;
    std::sort(want.begin(), want.end(), [](const WritingDay &a, const WritingDay &b) { return a.date < b.date; });
    auto same = [](const WritingDay &a, const WritingDay &b) {
        return a.date == b.date && a.draftWords == b.draftWords && a.draftChars == b.draftChars && a.otherWords == b.otherWords
               && a.otherChars == b.otherChars;
    };
    if (want.size() == had.size() && std::equal(want.cbegin(), want.cend(), had.cbegin(), same))
        return QByteArray();
    if (want.isEmpty() && old.isEmpty())
        return QByteArray();
    QByteArray out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<WritingHistory>\n";
    for (const WritingDay &w : std::as_const(want)) {
        const auto ex = extra.value(w.date);
        auto exAttr = [&](const char *k) {
            for (const auto &a : ex)
                if (a.first == QLatin1String(k))
                    return QByteArray(" ") + k + "=\"" + a.second.toHtmlEscaped().toUtf8() + "\"";
            return QByteArray();
        };
        out += "    <Day dwc=\"" + QByteArray::number(w.draftWords) + "\" dcc=\"" + QByteArray::number(w.draftChars) + "\"" + exAttr("dtwc")
               + " owc=\"" + QByteArray::number(w.otherWords) + "\" occ=\"" + QByteArray::number(w.otherChars) + "\"" + exAttr("dtcc") + ">"
               + w.date.toString(QStringLiteral("yyyy-MM-dd")).toLatin1() + "</Day>\n";
    }
    out += "</WritingHistory>\n";
    return out;
}

QByteArray readFileBytes(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.read(64 * 1024) : QByteArray();
}

QString statHash(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.exists())
        return QString();
    return QStringLiteral("s:%1:%2").arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch());
}

// SHA-1 of a file of this program, read again only when its size or time changed since it
// was last hashed in this session (0.60.5, the quick write of the autosave). "" = no file.
QString knownSha1File(const QString &path)
{
    static QHash<QString, QPair<QString, QString>> known; // path -> (size and time, SHA-1)
    const QString st = statHash(path);
    if (st.isEmpty()) {
        known.remove(path);
        return QString();
    }
    auto it = known.constFind(path);
    if (it != known.constEnd() && it->first == st)
        return it->second;
    const QString h = sha1File(path);
    known.insert(path, {st, h});
    return h;
}

// What the last fingerprint of a .scriv saw (0.60.5): the SHA-1 of its .scrivx and one line
// per file of Files/Data and Snapshots. A quick write brings the lines of the files it wrote
// up to date, so the fingerprint is known without reading the folder again. Files that
// another program changes meanwhile are not seen: the fingerprint then differs at the next
// opening, and the .scriv is read again (the safe way round).
struct FingerprintCache {
    QString scrivDir;
    QString scrivxSha;
    QHash<QString, QString> lines; // relative path -> "path size time"
};

FingerprintCache &fingerprintCache()
{
    static FingerprintCache c;
    return c;
}

QString fingerprintLine(const QString &scrivDir, const QFileInfo &fi)
{
    return QDir(scrivDir).relativeFilePath(fi.absoluteFilePath()) + QLatin1Char(' ') + QString::number(fi.size())
           + QLatin1Char(' ') + QString::number(fi.lastModified().toMSecsSinceEpoch());
}

QString fingerprintOf(const QString &scrivxSha, QStringList lines)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(scrivxSha.toLatin1());
    lines.sort();
    h.addData(lines.join(QLatin1Char('\n')).toUtf8());
    return QString::fromLatin1(h.result().toHex());
}

// the fingerprint after a quick write that wrote or removed these files; "" when the
// cache does not hold this .scriv (then the folder is read)
QString updatedFingerprint(const QString &scrivDir, const QSet<QString> &touched, const QString &scrivxSha)
{
    FingerprintCache &c = fingerprintCache();
    if (c.scrivDir != scrivDir)
        return QString();
    if (!scrivxSha.isEmpty())
        c.scrivxSha = scrivxSha;
    const QString data = scrivDir + QLatin1String("/Files/Data/");
    const QString snaps = scrivDir + QLatin1String("/Snapshots/");
    for (const QString &path : touched) {
        if (!path.startsWith(data) && !path.startsWith(snaps))
            continue; // the .scrivx, the search index: not lines of their own
        if (QFileInfo(path).fileName() == QLatin1String("docs.checksum"))
            continue;
        const QFileInfo fi(path);
        const QString rel = QDir(scrivDir).relativeFilePath(fi.absoluteFilePath());
        if (fi.exists())
            c.lines.insert(rel, fingerprintLine(scrivDir, fi));
        else
            c.lines.remove(rel);
    }
    return fingerprintOf(c.scrivxSha, c.lines.values());
}

bool writeFile(const QString &path, const QByteArray &data, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || safesave::write(f, data) != data.size() || !safesave::commit(f)) {
        if (error)
            *error = QObject::tr("Cannot write %1").arg(path);
        return false;
    }
    return true;
}

// size and time of the .scrivx (0.64: was it written elsewhere since this program read or wrote it?)
QString scrivxStamp(const QString &path)
{
    const QFileInfo fi(path);
    return fi.exists() ? QStringLiteral("%1:%2").arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch()) : QString();
}

// two .scrivx files that differ only in the root's Modified and ModID attributes
// 0.64.2: compared as XML (elements, attributes in any order, text other than white space between elements), not
// byte for byte: a .scrivx that Scrivener wrote differs in its layout from what this program writes (attribute
// order, quotes, indentation), so every full save wrote it anew although nothing in it had changed (C-37). The
// root's Device (which program wrote it last) does not count either.
static QString canonicalScrivx(const QByteArray &x, bool *ok)
{
    QXmlStreamReader r(x);
    QString out;
    out.reserve(x.size());
    int depth = 0;
    bool leaf = false;   // only white space since the last start tag: a title of one space is a title
    QString pendingSpace;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            leaf = true;
            pendingSpace.clear();
            ++depth;
            QStringList attrs;
            for (const QXmlStreamAttribute &a : r.attributes()) {
                const QString n = a.qualifiedName().toString();
                if (depth == 1 && (n == QLatin1String("Modified") || n == QLatin1String("ModID") || n == QLatin1String("Device")))
                    continue;
                attrs << n + QLatin1Char('=') + a.value().toString();
            }
            attrs.sort();
            out += QLatin1Char('<') + r.qualifiedName().toString() + QLatin1Char(' ') + attrs.join(QChar(0x1F)) + QLatin1Char('>');
        } else if (r.isEndElement()) {
            if (leaf)
                out += pendingSpace;
            leaf = false;
            pendingSpace.clear();
            --depth;
            out += QStringLiteral("</>");
        } else if (r.isCharacters()) {
            if (!r.isWhitespace()) {
                out += r.text().toString();
                leaf = false;
            } else if (leaf) {
                pendingSpace += r.text().toString();
            }
        }
    }
    *ok = !r.hasError();
    return out;
}

bool sameExceptStamps(const QByteArray &a, const QByteArray &b)
{
    if (a.isEmpty())
        return false;
    bool okA = false, okB = false;
    const QString ca = canonicalScrivx(a, &okA), cb = canonicalScrivx(b, &okB);
    if (okA && okB)
        return ca == cb;
    // not readable as XML: the old comparison of the bytes, without the stamps in the root
    static const QRegularExpression stamps(QStringLiteral(" (Modified|ModID|Device)=\"[^\"]*\""));
    auto norm = [](const QByteArray &x) {
        const int end = x.indexOf('>', x.indexOf("<ScrivenerProject"));
        QString head = QString::fromUtf8(x.left(end + 1));
        head.remove(stamps);
        return head.toUtf8() + x.mid(end + 1);
    };
    return norm(a) == norm(b);
}

bool copyDir(const QString &from, const QString &to, const QStringList &skipRelative = {})
{
    QDir src(from);
    if (!src.exists())
        return false;
    QDir().mkpath(to);
    QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = src.relativeFilePath(path);
        if (skipRelative.contains(rel))
            continue;
        const QString target = QDir(to).filePath(rel);
        if (it.fileInfo().isDir()) {
            QDir().mkpath(target);
        } else {
            QDir().mkpath(QFileInfo(target).absolutePath());
            // 0.65.0 (audit F05): copied beside the target and renamed over it; the old file stays when it fails
            if (!safesave::copyFile(path, target))
                return false;
        }
    }
    return true;
}

// a color as Scrivener writes it ("r g b", each 0 to 1), as the reader takes it
QColor parseScrivColor(const QString &s)
{
    const QStringList p = s.simplified().split(QLatin1Char(' '));
    if (p.size() < 3)
        return QColor();
    bool ok1, ok2, ok3;
    const double r = p[0].toDouble(&ok1), g = p[1].toDouble(&ok2), b = p[2].toDouble(&ok3);
    if (!ok1 || !ok2 || !ok3)
        return QColor();
    if (r > 1.0 || g > 1.0 || b > 1.0)
        return QColor(int(r), int(g), int(b));
    return QColor::fromRgbF(float(r), float(g), float(b));
}

QString colorText(const QColor &c)
{
    const QColor x = c.isValid() ? c.toRgb() : QColor(150, 150, 150);
    return QStringLiteral("%1 %2 %3")
        .arg(x.redF(), 0, 'f', 6)
        .arg(x.greenF(), 0, 'f', 6)
        .arg(x.blueF(), 0, 'f', 6);
}

// Scrivener's binder ids are UUIDs; ids of this program are as well, other
// ids get a UUID derived from them (always the same one).
QString uuidFor(const QString &id)
{
    const QUuid u = QUuid::fromString(id);
    if (!u.isNull())
        return u.toString(QUuid::WithoutBraces).toUpper();
    static const QUuid ns = QUuid::fromString(QStringLiteral("6b1f4a36-3c55-4b55-9a4e-5d9e3c2b7a10"));
    return QUuid::createUuidV5(ns, id).toString(QUuid::WithoutBraces).toUpper();
}

// The file's ending in the .scriv: a web page goes in as Scrivener for Windows saves one, a MIME archive content.mht
// (0.81.3; this program's own pages, an HTML file with folders, are packed into one)
QString scrivExt(const BinderItem *it)
{
    return it->type == ItemType::WebPage ? QStringLiteral("mht") : it->fileExt.toLower();
}

// The web page is kept here in the form of the .scriv already (Scrivener's .mht), not as this program's HTML
bool isArchived(const BinderItem *it)
{
    const QString e = it->fileExt.toLower();
    return e == QLatin1String("mht") || e == QLatin1String("mhtml");
}

QString typeFor(const BinderItem *it)
{
    switch (it->type) {
    case ItemType::Draft: return QStringLiteral("DraftFolder");
    case ItemType::Research: return QStringLiteral("ResearchFolder");
    case ItemType::Trash: return QStringLiteral("TrashFolder");
    case ItemType::Folder: return QStringLiteral("Folder");
    case ItemType::Image: return QStringLiteral("Image");
    case ItemType::Pdf: return QStringLiteral("PDF");
    // 0.81.2: audio and video as Scrivener 3.1.6.0 for Windows has them (Type "Media", the file content.<ext>);
    // before, they were written as text documents, also those that came from the .scriv
    case ItemType::Media: return QStringLiteral("Media");
    // a web page as Scrivener for Windows saves one (0.81.3: also this program's own, packed into one .mht file)
    case ItemType::WebPage: return QStringLiteral("WebArchive");
    // no verified Scrivener type for these: a text document that says what it
    // is, with the file beside it (read back by this program)
    case ItemType::Text:
    case ItemType::File: return QStringLiteral("Text");
    }
    return QStringLiteral("Text");
}

// A document written as a text that names its file (no verified Scrivener form for it)
bool isPlaceholder(const BinderItem *it)
{
    return it->type == ItemType::File;
}

QDomElement child(QDomElement parent, const QString &name, bool create, const QStringList &before = {})
{
    QDomElement e = parent.firstChildElement(name);
    if (!e.isNull() || !create)
        return e;
    e = parent.ownerDocument().createElement(name);
    for (const QString &b : before) {
        const QDomElement ref = parent.firstChildElement(b);
        if (!ref.isNull()) {
            parent.insertBefore(e, ref);
            return e;
        }
    }
    parent.appendChild(e);
    return e;
}

void setText(QDomElement e, const QString &text)
{
    while (!e.firstChild().isNull())
        e.removeChild(e.firstChild());
    e.appendChild(e.ownerDocument().createTextNode(text));
}

void setChildText(QDomElement parent, const QString &name, const QString &text, const QStringList &before = {})
{
    setText(child(parent, name, true, before), text);
}

void removeChild(QDomElement parent, const QString &name)
{
    QDomElement e = parent.firstChildElement(name);
    if (!e.isNull())
        parent.removeChild(e);
}

// ---------------------------------------------------------------- text

struct Note {
    QString id;
    QString text;
    bool footnote = false;
    QColor color;
};

// Scrivener's styles of the .scriv (Files/styles.xml) and what a write adds to them (0.81.3). A style of this program
// is matched by name with the file's ("Centered" also with Scrivener's "Centered Text" ...); a style the file does not
// have is added when a written text uses it, with the look of its first use (Format, an RTF paragraph). The file is
// written only when something was added, so an unchanged .scriv keeps it as it was.
struct ScrivStyles {
    QString path;
    QDomDocument dom;
    bool changed = false;
    QHash<QString, QString> idByName;   // lower-case name -> ID
    const Formatting *f = nullptr;

    void load(const QString &scrivDir, const Formatting &fmt)
    {
        f = &fmt;
        path = scrivDir + QLatin1String("/Files/styles.xml");
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || !dom.setContent(&file)) {
            dom = QDomDocument();
            dom.appendChild(dom.createProcessingInstruction(QStringLiteral("xml"), QStringLiteral("version=\"1.0\" encoding=\"UTF-8\"")));
            dom.appendChild(dom.createElement(QStringLiteral("Styles")));
        }
        const QDomNodeList list = dom.elementsByTagName(QStringLiteral("Style"));
        for (int i = 0; i < list.size(); ++i) {
            const QDomElement e = list.at(i).toElement();
            const QString name = e.attribute(QStringLiteral("Name")).trimmed().toLower();
            if (!name.isEmpty() && !idByName.contains(name))
                idByName.insert(name, e.attribute(QStringLiteral("ID")).trimmed().toUpper());
        }
    }

    QString idFor(const Style &s, const QTextBlockFormat &bf, const QTextCharFormat &cf)
    {
        QStringList names{s.name.toLower()};
        // this program's names for Scrivener's standard ones (the other way round in ScrivenerImporter::scrivenerStyleName)
        if (s.name == QLatin1String("Centered"))
            names << QStringLiteral("centered text") << QStringLiteral("centred text");
        if (s.name == QLatin1String("Block Quote"))
            names << QStringLiteral("blockquote") << QStringLiteral("quote");
        for (const QString &n : std::as_const(names))
            if (idByName.contains(n))
                return idByName.value(n);
        // new in the file: its look from the text that uses it
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();
        QDomElement e = dom.createElement(QStringLiteral("Style"));
        e.setAttribute(QStringLiteral("ID"), id);
        e.setAttribute(QStringLiteral("Name"), s.name);
        e.setAttribute(QStringLiteral("Type"), s.character ? QStringLiteral("Char")
                                                           : s.hasCharFormatting() ? QStringLiteral("Para+Char")
                                                                                   : QStringLiteral("Para"));
        QStringList change;
        if (!s.chars.family.isEmpty())
            change << QStringLiteral("Face");
        if (s.chars.size > 0 || s.sizeAdjust != 0)
            change << QStringLiteral("Size");
        if (!change.isEmpty())
            e.setAttribute(QStringLiteral("FontChange"), change.join(QLatin1Char('+')));
        QTextDocument sample;
        QTextCursor c(&sample);
        QTextBlockFormat sbf = bf;
        sbf.clearProperty(props::Style);
        c.setBlockFormat(s.character ? QTextBlockFormat() : sbf);
        QTextCharFormat scf = cf;
        text::clearNoteProps(scf);
        scf.setAnchor(false);
        scf.clearProperty(QTextFormat::AnchorHref);
        scf.clearProperty(props::CharStyle);
        c.insertText(s.name, scf);
        QDomElement fmt = dom.createElement(QStringLiteral("Format"));
        fmt.appendChild(dom.createTextNode(QString::fromLatin1(rtf::write(&sample, f ? *f : Formatting()))));
        e.appendChild(fmt);
        dom.documentElement().appendChild(e);
        idByName.insert(s.name.toLower(), id);
        changed = true;
        return id;
    }
};

// The style marks Scrivener keeps in a text (0.81.3): "<$Scr_Ps::n>" ... "<!$Scr_Ps::n>" around paragraphs of a
// paragraph style, "<$Scr_Cs::n>" ... "<!$Scr_Cs::n>" around text of a character style; n is the place of the style's
// ID in the text's content.styles (returned in ids, in the order of first use). As Scrivener 3.1.6.0 for Windows writes
// them: one pair around paragraphs in a row with the same style, the closing mark at the end of the last one, the
// opening mark at the start of the first one, or, when the paragraph before has a style too, right after that one's
// closing mark (before its paragraph break). The marks take the look of the text beside them, without links,
// notes or styles.
void insertStyleMarks(QTextDocument *doc, ScrivStyles &ss, const Formatting &f, QStringList *ids)
{
    struct Run { int start, end, n; };
    struct Block { QTextBlock b; int n; QList<Run> runs; };
    QList<Block> blocks;
    auto indexOf = [&](const QString &id) {
        int i = int(ids->indexOf(id));
        if (i < 0) {
            ids->append(id);
            i = int(ids->size()) - 1;
        }
        return i;
    };
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        Block blk{b, -1, {}};
        const QString pname = b.blockFormat().stringProperty(props::Style);
        if (!pname.isEmpty() && pname != styles::noStyle())
            if (const Style *ps = styles::find(f, pname); ps && !ps->character) {
                QTextCharFormat first = b.charFormat();
                if (b.begin() != b.end())
                    first = b.begin().fragment().charFormat();
                blk.n = indexOf(ss.idFor(*ps, b.blockFormat(), first));
            }
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            const QString cname = fr.charFormat().stringProperty(props::CharStyle);
            if (cname.isEmpty())
                continue;
            const Style *cs = styles::findChar(f, cname);
            if (!cs)
                continue;
            const int n = indexOf(ss.idFor(*cs, b.blockFormat(), fr.charFormat()));
            if (!blk.runs.isEmpty() && blk.runs.last().n == n && blk.runs.last().end == fr.position())
                blk.runs.last().end = fr.position() + fr.length();
            else
                blk.runs.append({fr.position(), fr.position() + fr.length(), n});
        }
        blocks.append(blk);
    }
    // the marks: position and order at one position (a character style closed, a paragraph style closed, one opened,
    // a character style opened)
    struct Mark { int pos, order; QString text; };
    QList<Mark> marks;
    auto frameOf = [](const QTextBlock &b) { return QTextCursor(b).currentFrame(); };
    for (int i = 0; i < blocks.size(); ++i) {
        const Block &blk = blocks.at(i);
        for (const Run &r : blk.runs) {
            marks.append({r.start, 3, QStringLiteral("<$Scr_Cs::%1>").arg(r.n)});
            marks.append({r.end, 0, QStringLiteral("<!$Scr_Cs::%1>").arg(r.n)});
        }
        if (blk.n < 0)
            continue;
        const bool continues = i > 0 && blocks.at(i - 1).n == blk.n && frameOf(blocks.at(i - 1).b) == frameOf(blk.b);
        if (!continues) {
            const bool before = i > 0 && blocks.at(i - 1).n >= 0 && frameOf(blocks.at(i - 1).b) == frameOf(blk.b);
            const QTextBlock &prev = before ? blocks.at(i - 1).b : blk.b;
            marks.append({before ? prev.position() + prev.length() - 1 : blk.b.position(), 2, QStringLiteral("<$Scr_Ps::%1>").arg(blk.n)});
        }
        const bool goesOn = i + 1 < blocks.size() && blocks.at(i + 1).n == blk.n && frameOf(blocks.at(i + 1).b) == frameOf(blk.b);
        if (!goesOn)
            marks.append({blk.b.position() + blk.b.length() - 1, 1, QStringLiteral("<!$Scr_Ps::%1>").arg(blk.n)});
    }
    // from the end: a later mark first; at one position the last in order first (each goes before those put there)
    std::stable_sort(marks.begin(), marks.end(), [](const Mark &x, const Mark &y) {
        return x.pos != y.pos ? x.pos > y.pos : x.order > y.order;
    });
    for (const Mark &m : std::as_const(marks)) {
        QTextCursor c(doc);
        c.setPosition(m.pos);
        QTextCharFormat cf = c.charFormat();
        if (!c.atBlockEnd()) {
            // the look of the character after the mark when there is one in the paragraph
            QTextCursor n(doc);
            n.setPosition(m.pos);
            n.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            cf = n.charFormat();
        }
        text::clearNoteProps(cf);
        cf.setAnchor(false);
        cf.clearProperty(QTextFormat::AnchorHref);
        cf.clearProperty(QTextFormat::AnchorName);
        cf.clearProperty(props::CharStyle);
        if (cf.isImageFormat())
            cf = QTextCharFormat();
        c.insertText(m.text, cf);
    }
}

// A copy of the text as Scrivener keeps it: linked notes as HYPERLINK "scrivcmt://<ID>" on the anchor text, the
// note's text in content.comments; inline footnotes and annotations (0.81.3) as Scrivener for Windows keeps them,
// as text inside the RTF: "{\Scrv_fn=" the note "\end_Scrv_fn}", "{\Scrv_annot \color={\R=..\G=..\B=..} \text="
// the note "\end_Scrv_annot}" (before 0.81.3 they became linked notes on the word before them). With styles, the
// style marks of the text (styleIds: its content.styles).
// oldNotes (0.90.0): the linked notes the .scriv's comments file has now, in the text's order. When the text has as
// many of each kind in the same order, they keep their IDs, so what Scrivener keeps with a note (whether its box is
// folded, the formatting of an unchanged note) stays.
QByteArray scrivRtf(const QTextDocument *src, const Formatting &f, QList<Note> *notes, ScrivStyles *ss = nullptr,
                    QStringList *styleIds = nullptr, const QList<Note> &oldNotes = {})
{
    std::unique_ptr<QTextDocument> doc(src->clone());
    // pictures: make sure the copy has them
    for (QTextBlock b = src->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat cf = it.fragment().charFormat();
            if (!cf.isImageFormat())
                continue;
            const QUrl url(cf.toImageFormat().name());
            const QVariant v = const_cast<QTextDocument *>(src)->resource(QTextDocument::ImageResource, url);
            if (v.isValid())
                doc->addResource(QTextDocument::ImageResource, url, v);
        }

    struct Range {
        int start, end;
        NoteKind kind;
        QString id, text;
        QColor color;
    };
    QList<Range> ranges;
    // links are changed after the walk: a format change invalidates the fragment iterator (0.64.2)
    struct Link { int start, end; QString href; };
    QList<Link> links;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            const QTextCharFormat cf = fr.charFormat();
            const auto kind = NoteKind(cf.intProperty(props::NoteKind));
            if (cf.isAnchor() && cf.anchorHref().startsWith(QLatin1String("qb://")))
                links.append({fr.position(), fr.position() + fr.length(),
                              QStringLiteral("scrivlnk://") + cf.anchorHref().mid(5)});
            if (kind == NoteKind::None)
                continue;
            const QString id = cf.stringProperty(props::NoteId);
            if (!ranges.isEmpty() && ranges.last().id == id && ranges.last().kind == kind
                && ranges.last().end == fr.position()) {
                ranges.last().end = fr.position() + fr.length();
                if (isInlineNote(kind))
                    ranges.last().text += fr.text();
                continue;
            }
            Range r{fr.position(), fr.position() + fr.length(), kind, id,
                    isInlineNote(kind) ? fr.text() : cf.stringProperty(props::NoteText),
                    cf.property(props::NoteColor).value<QColor>()};
            ranges << r;
        }
    }
    for (const Link &l : links) {
        QTextCursor c(doc.get());
        c.setPosition(l.start);
        c.setPosition(l.end, QTextCursor::KeepAnchor);
        QTextCharFormat lf;
        lf.setAnchorHref(l.href);
        c.mergeCharFormat(lf);
    }
    // the format of a range without its note properties (collected first: a format change while walking the
    // fragments invalidates the iterator, 0.64.2)
    auto plainPieces = [&](int from, int to, const QString &anchor) {
        struct Piece { int s, e; QTextCharFormat f; };
        QList<Piece> pieces;
        for (QTextBlock b = doc->findBlock(from); b.isValid() && b.position() < to; b = b.next()) {
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const QTextFragment fr = it.fragment();
                const int fs = qMax(from, fr.position()), fe = qMin(to, fr.position() + fr.length());
                if (fs >= fe)
                    continue;
                QTextCharFormat cf = fr.charFormat();
                text::clearNoteProps(cf);
                if (!anchor.isEmpty()) {
                    cf.setAnchor(true);
                    cf.setAnchorHref(anchor);
                }
                pieces.append({fs, fe, cf});
            }
        }
        for (const Piece &p : pieces) {
            QTextCursor fc(doc.get());
            fc.setPosition(p.s);
            fc.setPosition(p.e, QTextCursor::KeepAnchor);
            fc.setCharFormat(p.f);
        }
    };
    // the IDs of the linked notes: the file's while the notes are the same in number, kind and order
    {
        QList<Range *> linked;
        std::sort(ranges.begin(), ranges.end(), [](const Range &a, const Range &b) { return a.start < b.start; });
        for (Range &r : ranges)
            if (!isInlineNote(r.kind))
                linked << &r;
        bool same = !linked.isEmpty() && linked.size() == oldNotes.size();
        for (int i = 0; same && i < linked.size(); ++i)
            same = oldNotes[i].footnote == (linked[i]->kind == NoteKind::LinkedFootnote) && !oldNotes[i].id.isEmpty();
        for (int i = 0; i < linked.size(); ++i)
            linked[i]->id = same ? oldNotes[i].id : QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();
    }
    // from the end, so that positions stay right
    std::sort(ranges.begin(), ranges.end(), [](const Range &a, const Range &b) { return a.start > b.start; });
    for (const Range &r : ranges) {
        if (isInlineNote(r.kind)) {
            // the note stays where it is, between Scrivener's marks (characters of the text)
            QTextCursor at(doc.get());
            at.setPosition(r.start);
            at.setPosition(r.start + 1, QTextCursor::KeepAnchor);
            QTextCharFormat plain = at.charFormat();
            text::clearNoteProps(plain);
            plainPieces(r.start, r.end, QString());
            QString head;
            if (r.kind == NoteKind::InlineFootnote) {
                head = QStringLiteral("{\\Scrv_fn=");
            } else {
                const QColor c = (r.color.isValid() ? r.color : defaultAnnotationColor()).toRgb();
                head = QStringLiteral("{\\Scrv_annot \\color={\\R=%1\\G=%2\\B=%3} \\text=")
                           .arg(c.redF(), 0, 'f', 6).arg(c.greenF(), 0, 'f', 6).arg(c.blueF(), 0, 'f', 6);
            }
            QTextCursor e(doc.get());
            e.setPosition(r.end);
            e.insertText(r.kind == NoteKind::InlineFootnote ? QStringLiteral("\\end_Scrv_fn}") : QStringLiteral("\\end_Scrv_annot}"),
                         plain);
            QTextCursor s(doc.get());
            s.setPosition(r.start);
            s.insertText(head, plain);
            continue;
        }
        Note n;
        n.id = r.id;
        n.text = r.text.trimmed();
        n.footnote = r.kind == NoteKind::LinkedFootnote;
        n.color = r.color;
        plainPieces(r.start, r.end, QStringLiteral("scrivcmt://") + n.id);
        if (notes)
            notes->prepend(n);
    }
    if (ss && styleIds)
        insertStyleMarks(doc.get(), *ss, f, styleIds);
    // 0.90.0: in the forms Scrivener writes (lists with a list table, links to outside addresses underlined)
    rtf::WriteOptions how;
    how.scrivener = true;
    return rtf::write(doc.get(), f, how);
}

// The linked notes of a comments file as it is, in the file's order (0.90.0; `text` is left empty)
QList<Note> notesOfFile(const QByteArray &xml)
{
    QList<Note> out;
    QDomDocument dom;
    if (xml.isEmpty() || !dom.setContent(xml))
        return out;
    for (QDomElement e = dom.documentElement().firstChildElement(QStringLiteral("Comment")); !e.isNull();
         e = e.nextSiblingElement(QStringLiteral("Comment"))) {
        Note n;
        n.id = e.attribute(QStringLiteral("ID"));
        n.footnote = e.attribute(QStringLiteral("Footnote")).compare(QLatin1String("Yes"), Qt::CaseInsensitive) == 0;
        out << n;
    }
    return out;
}

// The comments file of a text. As Scrivener 3.1.6.0 for Windows writes it (0.90.0): a footnote has Footnote="Yes" and
// no color, a comment its color and no Footnote; both say whether their box is folded (Collapsed). old: the file as it
// is; a note that has its ID keeps what the file says about it, and the file's own text of it while the words are the same
// (this program keeps a note's words only, so the formatting inside an unchanged note stays Scrivener's).
QByteArray commentsXml(const QList<Note> &notes, const Formatting &f, const QByteArray &old = QByteArray())
{
    QHash<QString, QDomElement> was;
    QDomDocument oldDom;
    if (!old.isEmpty() && oldDom.setContent(old))
        for (QDomElement e = oldDom.documentElement().firstChildElement(QStringLiteral("Comment")); !e.isNull();
             e = e.nextSiblingElement(QStringLiteral("Comment")))
            was.insert(e.attribute(QStringLiteral("ID")), e);
    QDomDocument dom;
    dom.appendChild(dom.createProcessingInstruction(QStringLiteral("xml"), QStringLiteral("version=\"1.0\" encoding=\"UTF-8\"")));
    QDomElement root = dom.createElement(QStringLiteral("Comments"));
    root.setAttribute(QStringLiteral("Version"), QStringLiteral("1.0"));
    dom.appendChild(root);
    for (const Note &n : notes) {
        const QDomElement before = was.value(n.id);
        QDomElement e = dom.createElement(QStringLiteral("Comment"));
        e.setAttribute(QStringLiteral("ID"), n.id);
        if (n.footnote)
            e.setAttribute(QStringLiteral("Footnote"), QStringLiteral("Yes"));
        e.setAttribute(QStringLiteral("Collapsed"), before.attribute(QStringLiteral("Collapsed"), QStringLiteral("No")));
        if (!n.footnote) {
            const QColor c = n.color.isValid() ? n.color : QColor(255, 214, 102);
            // the file's own text of the color while it is still that color (this program keeps 8 bits a channel)
            const QString wasColor = before.attribute(QStringLiteral("Color"));
            const QColor o = parseScrivColor(wasColor);
            e.setAttribute(QStringLiteral("Color"), o.isValid() && o.toRgb().rgb() == c.toRgb().rgb() ? wasColor : colorText(c));
        }
        QByteArray body;
        if (!before.isNull() && rtf::toPlainText(before.text().toUtf8()).trimmed() == n.text)
            body = before.text().toUtf8();
        if (body.isEmpty()) {
            QTextDocument d;
            d.setPlainText(n.text);
            body = rtf::write(&d, f);
        }
        body.replace("]]>", "]] >");
        e.appendChild(dom.createCDATASection(QString::fromUtf8(body)));
        root.appendChild(e);
    }
    return dom.toByteArray(4);
}

QByteArray placeholderRtf(const BinderItem *it, const Formatting &f)
{
    QTextDocument d;
    const QString kind = it->type == ItemType::Media     ? QObject::tr("audio or video file")
                         : it->type == ItemType::WebPage ? QObject::tr("web page")
                                                         : QObject::tr("file");
    d.setPlainText(QObject::tr("[%1: this %2 is shown in %3. The file is content.%4 in the folder of this document "
                               "(Files/Data/%5).]")
                       .arg(QStringLiteral(QB_APP_NAME), kind, QStringLiteral(QB_APP_NAME), it->fileExt,
                            uuidFor(it->id)));
    return rtf::write(&d, f);
}

// ---------------------------------------------------------------- binder XML

// How one custom metadata field of this program is written (0.81.2)
struct FieldOut {
    QString id;                        // Scrivener's FieldID
    CustomFieldType type = CustomFieldType::Text;
    QHash<QString, QString> optionId;  // option text -> Scrivener's option ID (the first option of that text)
    QHash<QString, QString> optionText; // option ID -> text
    QList<QPair<QString, QString>> options; // ID, text in this program's order
};

struct XmlCtx {
    Project *p;
    QDomDocument dom;
    QHash<QString, QDomElement> old; // UUID -> element of the existing .scrivx
    QHash<QString, QString> keywordIds; // our keyword id -> Scrivener's (0.81.2)
    QHash<QString, FieldOut> fields;    // our custom field id -> how it is written (0.81.2)
    QString data;                       // the .scriv's Files/Data/ (0.90.0; "" when not known)
};

// The element as a string in which only what Scrivener reads counts (attribute order and white space do not)
QString canonicalElement(const QDomElement &e)
{
    if (e.isNull())
        return QString();
    QString s;
    QTextStream ts(&s);
    e.save(ts, 0);
    bool ok = false;
    const QString c = canonicalScrivx(s.toUtf8(), &ok);
    return ok ? c : s;
}

// Puts a top-level element where Scrivener has it: in place of the old one, else before the first element
// that comes after it, else at the end. The element is inserted as it is (not its children moved: see binderItem)
void placeTop(QDomElement root, QDomElement fresh, const QString &name, const QStringList &before)
{
    const QDomElement old = root.firstChildElement(name);
    if (!old.isNull()) {
        root.replaceChild(fresh, old);
        return;
    }
    for (const QString &b : before) {
        const QDomElement ref = root.firstChildElement(b);
        if (!ref.isNull()) {
            root.insertBefore(fresh, ref);
            return;
        }
    }
    root.appendChild(fresh);
}

bool isScrivNumber(const QString &s)
{
    if (s.isEmpty())
        return false;
    for (const QChar ch : s)
        if (!ch.isDigit() || ch.unicode() > 127)
            return false;
    return true;
}

QString fieldTypeName(CustomFieldType t)
{
    switch (t) {
    case CustomFieldType::Checkbox: return QStringLiteral("Checkbox");
    case CustomFieldType::List: return QStringLiteral("List");
    case CustomFieldType::Date: return QStringLiteral("Date");
    case CustomFieldType::Text: break;
    }
    return QStringLiteral("Text");
}

CustomFieldType fieldTypeOf(const QString &scrivType)
{
    return scrivType == QLatin1String("Checkbox") ? CustomFieldType::Checkbox
           : scrivType == QLatin1String("List")   ? CustomFieldType::List
           : scrivType == QLatin1String("Date")   ? CustomFieldType::Date
                                                  : CustomFieldType::Text;
}

bool truthy(const QString &v)
{
    return v == QLatin1String("1") || v.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
           || v.compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0;
}

bool falsy(const QString &v)
{
    return v == QLatin1String("0") || v.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0
           || v.compare(QLatin1String("no"), Qt::CaseInsensitive) == 0;
}

const QRegularExpression &scrivDatePattern()
{
    static const QRegularExpression re(QStringLiteral("^(\\d{4}-\\d{2}-\\d{2})[ T]\\d{2}:\\d{2}(:\\d{2})?( ?[+-]\\d{2}:?\\d{2}|Z)?$"));
    return re;
}

// A value of this program in Scrivener's form; false: nothing is written for it
bool valueToScrivener(const FieldOut &f, const QString &v, QString *out)
{
    switch (f.type) {
    case CustomFieldType::Text:
        if (v.isEmpty())
            return false;
        *out = v;
        return true;
    case CustomFieldType::Checkbox:
        if (truthy(v))
            *out = QStringLiteral("Yes");
        else if (falsy(v))
            *out = QStringLiteral("No");
        else
            return false;
        return true;
    case CustomFieldType::List:
        if (v.isEmpty() || v == QLatin1String("-1"))
            *out = QStringLiteral("-1");                 // "none", as Scrivener keeps it
        else if (f.optionId.contains(v))
            *out = f.optionId.value(v);
        else if (f.optionText.contains(v))
            *out = v;                                    // an ID kept by a version before 0.81.2
        else
            return false;
        return true;
    case CustomFieldType::Date: {
        const QDate d = QDate::fromString(v, Qt::ISODate);
        if (d.isValid()) {
            *out = dateText(QDateTime(d, QTime(0, 0)));
            return true;
        }
        if (scrivDatePattern().match(v).hasMatch()) {
            *out = v;                                    // as a version before 0.81.2 kept it
            return true;
        }
        return false;
    }
    }
    return false;
}

// Scrivener's ids for the keywords and custom fields of the project (0.81.2). Ours that came from the
// file are Scrivener's own (the reader keeps them); others are matched by name with the file's (a working
// copy of an older version, whose ids were new ones) or get a new id in Scrivener's form. Matching by
// name keeps the ids of a file whose keywords this program never changed, so it is written as it was.
void prepareMeta(XmlCtx &c, const QDomElement &root)
{
    Project *p = c.p;
    // keywords: the file's (id -> title, parent id)
    struct OldKw { QString title, parent; };
    QHash<QString, OldKw> oldKw;
    QStringList oldKwOrder;
    std::function<void(const QDomElement &, const QString &)> walk = [&](const QDomElement &parent, const QString &pid) {
        for (QDomElement k = parent.firstChildElement(QStringLiteral("Keyword")); !k.isNull();
             k = k.nextSiblingElement(QStringLiteral("Keyword"))) {
            const QString id = k.attribute(QStringLiteral("ID"));
            QString title = k.firstChildElement(QStringLiteral("Title")).text();
            if (k.firstChildElement(QStringLiteral("Title")).isNull())
                title = k.attribute(QStringLiteral("Title"));
            oldKw.insert(id, {title, pid});
            oldKwOrder << id;
            walk(k.firstChildElement(QStringLiteral("Children")), id);
        }
    };
    walk(root.firstChildElement(QStringLiteral("Keywords")), QString());
    int maxKw = 0;
    for (const QString &id : std::as_const(oldKwOrder))
        maxKw = qMax(maxKw, id.toInt());
    QSet<QString> claimed;
    for (const Keyword &k : std::as_const(p->keywords))
        if (isScrivNumber(k.id)) {
            c.keywordIds.insert(k.id, k.id);
            claimed.insert(k.id);
            maxKw = qMax(maxKw, k.id.toInt());
        }
    std::function<QString(const QString &, int)> resolve = [&](const QString &qbId, int depth) -> QString {
        if (c.keywordIds.contains(qbId))
            return c.keywordIds.value(qbId);
        const Keyword *k = p->keywordById(qbId);
        if (!k || depth > 64)
            return QString();
        const QString parent = k->parentId.isEmpty() ? QString() : resolve(k->parentId, depth + 1);
        QString id;
        for (const QString &oid : std::as_const(oldKwOrder))
            if (!claimed.contains(oid) && oldKw.value(oid).title == k->name && oldKw.value(oid).parent == parent) {
                id = oid;
                break;
            }
        if (id.isEmpty())
            id = QString::number(++maxKw);
        claimed.insert(id);
        c.keywordIds.insert(qbId, id);
        return id;
    };
    for (const Keyword &k : std::as_const(p->keywords))
        resolve(k.id, 0);

    // custom fields: the file's (ID -> type, title, options)
    struct OldField { CustomFieldType type; QString title; QList<QPair<QString, QString>> options; };
    QHash<QString, OldField> oldF;
    QStringList oldFOrder;
    const QDomElement cms = root.firstChildElement(QStringLiteral("CustomMetaDataSettings"));
    for (QDomElement mf = cms.firstChildElement(QStringLiteral("MetaDataField")); !mf.isNull();
         mf = mf.nextSiblingElement(QStringLiteral("MetaDataField"))) {
        OldField f;
        f.type = fieldTypeOf(mf.attribute(QStringLiteral("Type")));
        f.title = mf.firstChildElement(QStringLiteral("Title")).text();
        const QDomElement opts = mf.firstChildElement(QStringLiteral("ListOptions"));
        for (QDomElement o = opts.firstChildElement(); !o.isNull(); o = o.nextSiblingElement())
            f.options.append({o.attribute(QStringLiteral("ID")), o.text()});
        oldF.insert(mf.attribute(QStringLiteral("ID")), f);
        oldFOrder << mf.attribute(QStringLiteral("ID"));
    }
    QSet<QString> fieldTaken;
    for (const CustomField &f : std::as_const(p->customFields))
        if (oldF.contains(f.id)) {
            c.fields[f.id].id = f.id;
            fieldTaken.insert(f.id);
        }
    for (const CustomField &f : std::as_const(p->customFields)) {
        if (c.fields.contains(f.id))
            continue;
        QString id;
        for (const QString &oid : std::as_const(oldFOrder))
            if (!fieldTaken.contains(oid) && oldF.value(oid).title == f.title && oldF.value(oid).type == f.type) {
                id = oid;
                break;
            }
        if (id.isEmpty()) {
            // Scrivener for Windows names a new field after its title: lower case, letters only, as far as
            // the example shows ("this is a custom metadata text" -> "thisisacustommetadatatext")
            QString base;
            for (const QChar ch : f.title)
                if (ch.unicode() < 128 && ch.isLetterOrNumber())
                    base += ch.toLower();
            if (base.isEmpty() || isScrivNumber(base)) {
                int n = 1;
                while (fieldTaken.contains(QString::number(n)) || oldF.contains(QString::number(n)))
                    ++n;
                id = QString::number(n);
            } else {
                id = base;
                for (int n = 2; fieldTaken.contains(id) || oldF.contains(id); ++n)
                    id = base + QString::number(n);
            }
        }
        fieldTaken.insert(id);
        c.fields[f.id].id = id;
    }
    for (const CustomField &f : std::as_const(p->customFields)) {
        FieldOut &fo = c.fields[f.id];
        fo.type = f.type;
        if (f.type != CustomFieldType::List)
            continue;
        // options keep their ID while their text stays; new ones get the next number
        const QList<QPair<QString, QString>> was = oldF.value(fo.id).options;
        int maxOpt = 0;
        for (const auto &o : was)
            maxOpt = qMax(maxOpt, o.first.toInt());
        QSet<QString> used;
        for (const QString &text : f.options) {
            QString id;
            for (const auto &o : was)
                if (!used.contains(o.first) && o.second == text) {
                    id = o.first;
                    break;
                }
            if (id.isEmpty())
                id = QString::number(++maxOpt);
            used.insert(id);
            if (!fo.optionId.contains(text))
                fo.optionId.insert(text, id);
            fo.optionText.insert(id, text);
            fo.options.append({id, text});
        }
    }
}

// The document's custom metadata values (MetaData/CustomMetaData). A value that still means the same as
// the file's keeps the file's text (a date's time zone); unchanged values leave the element as it is.
void writeCustomMeta(XmlCtx &c, QDomElement meta, const BinderItem *it)
{
    QDomElement cm = meta.firstChildElement(QStringLiteral("CustomMetaData"));
    QList<QPair<QString, QString>> have;
    QHash<QString, QString> haveBy;
    for (QDomElement mi = cm.firstChildElement(QStringLiteral("MetaDataItem")); !mi.isNull();
         mi = mi.nextSiblingElement(QStringLiteral("MetaDataItem"))) {
        const QString fid = mi.firstChildElement(QStringLiteral("FieldID")).text();
        const QString v = mi.firstChildElement(QStringLiteral("Value")).text();
        have.append({fid, v});
        haveBy.insert(fid, v);
    }
    QMap<QString, QString> want; // Scrivener sorts them by FieldID
    for (auto v = it->custom.cbegin(); v != it->custom.cend(); ++v) {
        if (!c.fields.contains(v.key()))
            continue;   // a value of a field that is gone
        const FieldOut &f = c.fields[v.key()];
        if (haveBy.contains(f.id)) {
            const QString was = haveBy.value(f.id);
            if (was == v.value() || valueFromScrivener(f.type, was, f.optionText) == v.value()) {
                want.insert(f.id, was);
                continue;
            }
        }
        QString out;
        if (valueToScrivener(f, v.value(), &out))
            want.insert(f.id, out);
    }
    QList<QPair<QString, QString>> wanted;
    for (auto w = want.cbegin(); w != want.cend(); ++w)
        wanted.append({w.key(), w.value()});
    QList<QPair<QString, QString>> sortedHave = have;
    std::sort(sortedHave.begin(), sortedHave.end());
    if (sortedHave == wanted)
        return;
    if (wanted.isEmpty()) {
        if (!cm.isNull())
            meta.removeChild(cm);
        return;
    }
    QDomElement fresh = c.dom.createElement(QStringLiteral("CustomMetaData"));
    for (const auto &w : std::as_const(wanted)) {
        QDomElement mi = c.dom.createElement(QStringLiteral("MetaDataItem"));
        setChildText(mi, QStringLiteral("FieldID"), w.first);
        setChildText(mi, QStringLiteral("Value"), w.second);
        fresh.appendChild(mi);
    }
    if (cm.isNull())
        meta.appendChild(fresh);   // after FileExtension, as Scrivener has it
    else
        meta.replaceChild(fresh, cm);
}

// The document's keywords (BinderItem/Keywords/KeywordID), in the document's order
void writeItemKeywords(XmlCtx &c, QDomElement e, const BinderItem *it)
{
    QStringList want;
    for (const QString &k : it->keywords) {
        const QString id = c.keywordIds.value(k);
        if (!id.isEmpty() && !want.contains(id))
            want << id;
    }
    QDomElement kw = e.firstChildElement(QStringLiteral("Keywords"));
    QStringList have;
    for (QDomElement k = kw.firstChildElement(); !k.isNull(); k = k.nextSiblingElement())
        have << k.text().trimmed();
    if (have == want)
        return;
    if (want.isEmpty()) {
        e.removeChild(kw);
        return;
    }
    QDomElement fresh = c.dom.createElement(QStringLiteral("Keywords"));
    for (const QString &id : std::as_const(want)) {
        QDomElement k = c.dom.createElement(QStringLiteral("KeywordID"));
        k.appendChild(c.dom.createTextNode(id));
        fresh.appendChild(k);
    }
    if (!kw.isNull()) {
        e.replaceChild(fresh, kw);
        return;
    }
    // 0.90.0: after CorkboardAndOutliner, before Bookmarks, as Scrivener for Windows orders them (0.81.2 put them before
    // CorkboardAndOutliner)
    for (const QString &b : {QStringLiteral("Bookmarks"), QStringLiteral("Children")}) {
        const QDomElement ref = e.firstChildElement(b);
        if (!ref.isNull()) {
            e.insertBefore(fresh, ref);
            return;
        }
    }
    e.appendChild(fresh);
}

// A bookmark as Scrivener 3.1.6.0 for Windows keeps one (0.90.0): a document of the project by its UUID, anything
// else (a web address, a file) as Destination; the title is the element's text. Null for a document that is gone.
QDomElement bookmarkElement(XmlCtx &c, const Bookmark &b, bool titled = true)
{
    QDomElement e = c.dom.createElement(QStringLiteral("Bookmark"));
    QString title = b.title;
    if (b.isInternal()) {
        const BinderItem *target = c.p->item(b.internalId());
        if (!target)
            return QDomElement();
        e.setAttribute(QStringLiteral("BinderUUID"), uuidFor(b.internalId()));
        title = target->title;
        if (!titled)
            return e;
    } else {
        if (b.target.trimmed().isEmpty())
            return QDomElement();
        e.setAttribute(QStringLiteral("Destination"), b.target);
    }
    if (!title.isEmpty())
        e.appendChild(c.dom.createTextNode(title));
    return e;
}

// The same places in the same order (a document's title in the file may be an older one: Scrivener writes it when
// the bookmark is made; the title of another address is the writer's and counts)
bool sameBookmarks(const QDomElement &a, const QDomElement &b)
{
    auto list = [](const QDomElement &parent) {
        QStringList out;
        for (QDomElement e = parent.firstChildElement(QStringLiteral("Bookmark")); !e.isNull(); e = e.nextSiblingElement(QStringLiteral("Bookmark")))
            out << (e.hasAttribute(QStringLiteral("Destination"))
                        ? QStringLiteral("to ") + e.attribute(QStringLiteral("Destination")) + QLatin1Char('\n') + e.text()
                        : QStringLiteral("doc ") + e.attribute(QStringLiteral("BinderUUID"), e.attribute(QStringLiteral("UUID"))).toUpper());
        return out;
    };
    return list(a) == list(b);
}

void writeItemBookmarks(XmlCtx &c, QDomElement &e, const BinderItem *it)
{
    const QDomElement old = e.firstChildElement(QStringLiteral("Bookmarks"));
    QDomElement fresh = c.dom.createElement(QStringLiteral("Bookmarks"));
    for (const Bookmark &b : it->bookmarks) {
        const QDomElement be = bookmarkElement(c, b);
        if (!be.isNull())
            fresh.appendChild(be);
    }
    if (!fresh.hasChildNodes()) {
        if (!old.isNull())
            e.removeChild(old);
        return;
    }
    if (!old.isNull()) {
        if (!sameBookmarks(old, fresh))
            e.replaceChild(fresh, old);
        return;
    }
    const QDomElement ref = e.firstChildElement(QStringLiteral("Children"));
    if (!ref.isNull())
        e.insertBefore(fresh, ref);
    else
        e.appendChild(fresh);
}

// A container's corkboard (0.90.0): its arrangement ("Freeform" / "Linear") and the places of its cards when arranged
// freely, in card widths with the moment each was placed. A place this program did not change keeps the file's text.
void writeItemCorkboard(XmlCtx &c, QDomElement &e, const BinderItem *it)
{
    QDomElement co = e.firstChildElement(QStringLiteral("CorkboardAndOutliner"));
    QDomElement cs = co.firstChildElement(QStringLiteral("CorkboardSettings"));
    const QString want = it->corkArrangement == QLatin1String("freeform") ? QStringLiteral("Freeform")
                         : it->corkArrangement == QLatin1String("grid")   ? QStringLiteral("Linear")
                                                                          : QString();
    struct Place { const BinderItem *card; double x, y; };
    QList<Place> places;
    const double unit = ScrivenerImporter::cardWidth();
    for (const BinderItem *k : it->children) {
        const QStringList xy = k->corkboardPos.split(QLatin1Char(','));
        if (xy.size() == 2)
            places.append({k, xy[0].toDouble(), xy[1].toDouble()});
    }
    QDomElement cards = cs.firstChildElement(QStringLiteral("FreeformCorkboardDocuments"));
    // what would change
    bool arrangementDiffers = !want.isEmpty() && cs.firstChildElement(QStringLiteral("Arrangement")).text().trimmed() != want;
    QList<Place> changed;
    QHash<QString, QDomElement> had;
    for (QDomElement card = cards.firstChildElement(QStringLiteral("IndexCardBinderUUID")); !card.isNull();
         card = card.nextSiblingElement(QStringLiteral("IndexCardBinderUUID")))
        had.insert(card.text().trimmed().toUpper(), card);
    for (const Place &pl : std::as_const(places)) {
        const QDomElement card = had.value(uuidFor(pl.card->id));
        const QStringList fxy = card.attribute(QStringLiteral("Position")).split(QLatin1Char(','));
        if (card.isNull() || fxy.size() != 2 || qRound(fxy[0].toDouble() * unit) != qRound(pl.x) || qRound(fxy[1].toDouble() * unit) != qRound(pl.y))
            changed << pl;
    }
    if (!arrangementDiffers && changed.isEmpty())
        return;
    if (co.isNull()) {
        co = c.dom.createElement(QStringLiteral("CorkboardAndOutliner"));
        QDomElement ref;
        for (const QString &b : {QStringLiteral("Keywords"), QStringLiteral("Bookmarks"), QStringLiteral("Children")})
            if (ref.isNull())
                ref = e.firstChildElement(b);
        if (!ref.isNull())
            e.insertBefore(co, ref);
        else
            e.appendChild(co);
    }
    if (cs.isNull()) {
        cs = c.dom.createElement(QStringLiteral("CorkboardSettings"));
        co.appendChild(cs);
    }
    QDomElement arr = cs.firstChildElement(QStringLiteral("Arrangement"));
    if (arr.isNull()) {
        arr = c.dom.createElement(QStringLiteral("Arrangement"));
        arr.appendChild(c.dom.createTextNode(want.isEmpty() ? QStringLiteral("Linear") : want));
        cs.insertBefore(arr, cs.firstChild());
    } else if (arrangementDiffers) {
        setText(arr, want);
    }
    if (changed.isEmpty())
        return;
    if (cards.isNull()) {
        cards = c.dom.createElement(QStringLiteral("FreeformCorkboardDocuments"));
        cs.appendChild(cards);
    }
    const QString now = dateText(QDateTime::currentDateTime());
    for (const Place &pl : std::as_const(changed)) {
        QDomElement card = had.value(uuidFor(pl.card->id));
        if (card.isNull()) {
            card = c.dom.createElement(QStringLiteral("IndexCardBinderUUID"));
            card.appendChild(c.dom.createTextNode(uuidFor(pl.card->id)));
            cards.appendChild(card);
        }
        card.setAttribute(QStringLiteral("Position"), QStringLiteral("%1,%2").arg(pl.x / unit, 0, 'f', 6).arg(pl.y / unit, 0, 'f', 6));
        card.setAttribute(QStringLiteral("TimeStamp"), now);
    }
}

// A document's target (0.90.0): <Target Type="Words" ...>500</Target> in its TextSettings; what else Scrivener keeps
// there (whether it tells, what its bar shows) stays
void writeItemTarget(XmlCtx &c, QDomElement &e, const BinderItem *it)
{
    QDomElement ts = e.firstChildElement(QStringLiteral("TextSettings"));
    QDomElement t = ts.firstChildElement(QStringLiteral("Target"));
    const int want = qMax(0, it->target);
    if (t.isNull() && want == 0)
        return;
    if (!t.isNull() && t.text().trimmed().toInt() == want
        && (want == 0 || t.attribute(QStringLiteral("Type")).startsWith(QLatin1String("Char")) == it->targetChars))
        return;
    if (ts.isNull()) {
        ts = child(e, QStringLiteral("TextSettings"), true, {QStringLiteral("MediaSettings"), QStringLiteral("Keywords"), QStringLiteral("Bookmarks"),
                                                              QStringLiteral("CorkboardAndOutliner"), QStringLiteral("Children")});
        setChildText(ts, QStringLiteral("TextSelection"), QStringLiteral("0,0"));
    }
    if (t.isNull()) {
        t = c.dom.createElement(QStringLiteral("Target"));
        t.setAttribute(QStringLiteral("Notify"), QStringLiteral("No"));
        t.setAttribute(QStringLiteral("ShowOverrun"), QStringLiteral("Yes"));
        t.setAttribute(QStringLiteral("ShowBuffer"), QStringLiteral("Yes"));
        ts.appendChild(t);
    }
    if (want > 0 || !t.hasAttribute(QStringLiteral("Type")))
        t.setAttribute(QStringLiteral("Type"), it->targetChars ? QStringLiteral("Characters") : QStringLiteral("Words"));
    setText(t, QString::number(want));
}

void indexOld(XmlCtx &c, QDomElement parent)
{
    for (QDomElement e = parent.firstChildElement(QStringLiteral("BinderItem")); !e.isNull();
         e = e.nextSiblingElement(QStringLiteral("BinderItem"))) {
        c.old.insert(e.attribute(QStringLiteral("UUID")).toUpper(), e);
        indexOld(c, e.firstChildElement(QStringLiteral("Children")));
    }
}

// A date attribute that still names the same moment keeps Scrivener's text
// (its time zone); otherwise the moment in local time.
void setDate(QDomElement &e, const QString &attr, const QDateTime &when)
{
    const QString old = e.attribute(attr);
    if (!old.isEmpty() && when.isValid()) {
        QDateTime dt = QDateTime::fromString(old.left(19), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        const QString off = old.mid(20).trimmed();
        if (dt.isValid() && off.size() >= 5 && (off[0] == QLatin1Char('+') || off[0] == QLatin1Char('-'))) {
            const int sign = off[0] == QLatin1Char('-') ? -1 : 1;
            dt.setOffsetFromUtc(sign * (off.mid(1, 2).toInt() * 3600 + off.mid(3, 2).toInt() * 60));
            if (dt.toSecsSinceEpoch() == when.toSecsSinceEpoch())
                return;
        }
    }
    e.setAttribute(attr, dateText(when));
}

QDomElement binderItem(XmlCtx &c, const BinderItem *it)
{
    const QString uuid = uuidFor(it->id);
    // A copy of the existing element (everything Scrivener keeps there stays).
    // Moving the element out of the old tree instead is not safe with QDom: a
    // node whose parent is freed keeps its old siblings, and a list it is
    // appended to then shows them as well (Qt 6.4).
    QDomElement e;
    const QDomElement existing = c.old.value(uuid);
    if (existing.isNull()) {
        e = c.dom.createElement(QStringLiteral("BinderItem"));
        e.setAttribute(QStringLiteral("UUID"), uuid);
    } else {
        e = existing.cloneNode(true).toElement();
    }
    e.setAttribute(QStringLiteral("Type"), typeFor(it));
    setDate(e, QStringLiteral("Created"), it->created);
    setDate(e, QStringLiteral("Modified"), it->modified.isValid() ? it->modified : it->created);
    setChildText(e, QStringLiteral("Title"), it->title, {QStringLiteral("MetaData"), QStringLiteral("TextSettings"),
                                                          QStringLiteral("Children")});
    QDomElement meta = child(e, QStringLiteral("MetaData"), true, {QStringLiteral("TextSettings"), QStringLiteral("MediaSettings"),
                                                                   QStringLiteral("Children")});
    const QStringList metaAfter = {QStringLiteral("IncludeInCompile"), QStringLiteral("NotesTextSelection"),
                                   QStringLiteral("IconFileName"), QStringLiteral("FileExtension")};
    if (!it->sectionType.isEmpty()) {
        QDomElement st = child(meta, QStringLiteral("SectionType"), true,
                               QStringList{QStringLiteral("LabelID"), QStringLiteral("StatusID")} + metaAfter);
        setText(st, uuidFor(it->sectionType));
    } else if (!meta.firstChildElement(QStringLiteral("SectionType")).isNull()) {
        // structure based: the type goes, a default for subdocuments stays
        QDomElement st = meta.firstChildElement(QStringLiteral("SectionType"));
        if (st.hasAttribute(QStringLiteral("ChildDefault")))
            setText(st, QString());
        else
            meta.removeChild(st);
    }
    if (it->label >= 0)
        setChildText(meta, QStringLiteral("LabelID"), QString::number(it->label), QStringList{QStringLiteral("StatusID")} + metaAfter);
    else
        removeChild(meta, QStringLiteral("LabelID"));
    if (it->status >= 0)
        setChildText(meta, QStringLiteral("StatusID"), QString::number(it->status), metaAfter);
    else
        removeChild(meta, QStringLiteral("StatusID"));
    // 0.64.2: an item without the entry is included (that is how it is read), so "Yes" is not added where the
    // file has none (Scrivener leaves it out on its top folders; adding it changed a .scriv opened and closed)
    if (!it->includeInCompile || !meta.firstChildElement(QStringLiteral("IncludeInCompile")).isNull() || existing.isNull())
        setChildText(meta, QStringLiteral("IncludeInCompile"), it->includeInCompile ? QStringLiteral("Yes") : QStringLiteral("No"),
                     {QStringLiteral("NotesTextSelection"), QStringLiteral("IconFileName"), QStringLiteral("FileExtension")});
    // the icon by Scrivener's name ("Notes (Yellow Notepad)", "Front Matter", an own icon's name) (0.17)
    {
        const QString iconName = icons::scrivenerName(it->icon);
        if (!iconName.isEmpty())
            setChildText(meta, QStringLiteral("IconFileName"), iconName, {QStringLiteral("FileExtension"), QStringLiteral("CustomMetaData")});
        else
            removeChild(meta, QStringLiteral("IconFileName"));
    }
    if (it->type == ItemType::Image || it->type == ItemType::Pdf || it->type == ItemType::Media || it->type == ItemType::WebPage) {
        setChildText(meta, QStringLiteral("FileExtension"), scrivExt(it), {QStringLiteral("CustomMetaData")});
    } else {
        // 0.81.2: Scrivener for Windows names the file of a text ("rtf"); that stays (it was taken out, so every
        // save without a change rewrote the binder of such a project)
        const QString ext = meta.firstChildElement(QStringLiteral("FileExtension")).text().trimmed().toLower();
        if (!(it->type == ItemType::Text || it->type == ItemType::Folder) || (ext != QLatin1String("rtf") && ext != QLatin1String("txt")))
            removeChild(meta, QStringLiteral("FileExtension"));
    }
    // the picture of an index card (0.90.0): the switch and the file's ending; a picture or media document's switch
    // means something else (see the reader) and is left alone
    if (it->hasText() || it->type == ItemType::Folder) {
        const QString ext = QFileInfo(it->synopsisImage).suffix().toLower();
        const QStringList beforeImage = {QStringLiteral("NotesTextSelection"), QStringLiteral("IconFileName"), QStringLiteral("FileExtension"),
                                         QStringLiteral("CustomMetaData")};
        const bool on = meta.firstChildElement(QStringLiteral("ShowSynopsisImage")).text().compare(QLatin1String("Yes"), Qt::CaseInsensitive) == 0;
        if (!it->synopsisImage.isEmpty()) {
            if (!on)
                setChildText(meta, QStringLiteral("ShowSynopsisImage"), QStringLiteral("Yes"),
                             QStringList{QStringLiteral("IndexCardImageFileExtension")} + beforeImage);
            if (meta.firstChildElement(QStringLiteral("IndexCardImageFileExtension")).text() != ext)
                setChildText(meta, QStringLiteral("IndexCardImageFileExtension"), ext, beforeImage);
        } else if (on && !c.data.isEmpty()
                   && !QDir(c.data + uuidFor(it->id)).entryList({QStringLiteral("card-image.*")}, QDir::Files).isEmpty()) {
            // the picture taken off the card here: the card shows the synopsis again (the file stays in the .scriv).
            // Only where the .scriv has a picture: a switch without one (a project of Scrivener for Mac has such) is the
            // file's and stays
            setChildText(meta, QStringLiteral("ShowSynopsisImage"), QStringLiteral("No"), QStringList{QStringLiteral("IndexCardImageFileExtension")} + beforeImage);
        }
    }
    // custom metadata values (0.81.2)
    writeCustomMeta(c, meta, it);
    // 0.64.2: an element of the file that has no MetaData keeps having none, and text settings are added only
    // to items new to the file (Scrivener leaves both out on its top folders; adding them changed a .scriv
    // opened and closed without a change)
    if (!existing.isNull() && existing.firstChildElement(QStringLiteral("MetaData")).isNull() && !meta.hasChildNodes())
        e.removeChild(meta);
    if (it->type != ItemType::Image && it->type != ItemType::Pdf && it->type != ItemType::Media && it->type != ItemType::WebPage
        && existing.isNull()
        && e.firstChildElement(QStringLiteral("TextSettings")).isNull()) {
        QDomElement ts = child(e, QStringLiteral("TextSettings"), true, {QStringLiteral("Children")});
        setChildText(ts, QStringLiteral("TextSelection"), QStringLiteral("0,0"));
    }
    if (it->type == ItemType::Image && e.firstChildElement(QStringLiteral("MediaSettings")).isNull()) {
        QDomElement ms = child(e, QStringLiteral("MediaSettings"), true, {QStringLiteral("Children")});
        setChildText(ms, QStringLiteral("ImageScaleFactor"), QStringLiteral("1.0"));
        setChildText(ms, QStringLiteral("ImageRotation"), QStringLiteral("None"));
        setChildText(ms, QStringLiteral("ImageScalesProportionally"), QStringLiteral("Yes"));
    }
    if (it->type == ItemType::Pdf && e.firstChildElement(QStringLiteral("MediaSettings")).isNull()) {
        QDomElement ms = child(e, QStringLiteral("MediaSettings"), true, {QStringLiteral("Children")});
        setChildText(ms, QStringLiteral("CurrentPDFPage"), QStringLiteral("0"));
    }
    if (it->type == ItemType::WebPage) {
        // the page's address, as Scrivener for Windows keeps it (0.81.3)
        QDomElement ms = e.firstChildElement(QStringLiteral("MediaSettings"));
        if (!it->sourceUrl.isEmpty() && ms.firstChildElement(QStringLiteral("OriginalURL")).text() != it->sourceUrl) {
            if (ms.isNull())
                ms = child(e, QStringLiteral("MediaSettings"), true, {QStringLiteral("Keywords"), QStringLiteral("Bookmarks"),
                                                                      QStringLiteral("CorkboardAndOutliner"), QStringLiteral("Children")});
            setChildText(ms, QStringLiteral("OriginalURL"), it->sourceUrl);
        }
    }
    if (it->type == ItemType::Media && e.firstChildElement(QStringLiteral("MediaSettings")).isNull()) {
        // as Scrivener 3.1.6.0 for Windows writes a new audio or video document (0.81.2)
        QDomElement ms = child(e, QStringLiteral("MediaSettings"), true, {QStringLiteral("Keywords"), QStringLiteral("Bookmarks"),
                                                                           QStringLiteral("CorkboardAndOutliner"), QStringLiteral("Children")});
        ms.appendChild(c.dom.createElement(QStringLiteral("PlaybackPosition")));
    }
    // keywords (0.81.2)
    writeItemKeywords(c, e, it);
    // the corkboard, bookmarks and the target (0.90.0)
    writeItemCorkboard(c, e, it);
    writeItemBookmarks(c, e, it);
    writeItemTarget(c, e, it);
    removeChild(e, QStringLiteral("Children"));
    if (!it->children.isEmpty()) {
        QDomElement ch = c.dom.createElement(QStringLiteral("Children"));
        for (const BinderItem *k : it->children)
            ch.appendChild(binderItem(c, k));
        e.appendChild(ch);
    }
    return e;
}

QDomElement replaceTop(QDomElement root, const QString &name, const QStringList &before)
{
    QDomElement e = root.firstChildElement(name);
    QDomElement n = root.ownerDocument().createElement(name);
    if (!e.isNull()) {
        root.replaceChild(n, e);
        return n;
    }
    for (const QString &b : before) {
        const QDomElement ref = root.firstChildElement(b);
        if (!ref.isNull()) {
            root.insertBefore(n, ref);
            return n;
        }
    }
    root.appendChild(n);
    return n;
}

// the order of a project of Scrivener 3.1.6.0 for Windows (Keywords and CustomMetaDataSettings: 0.81.2)
const QStringList kTopOrder = {QStringLiteral("Binder"),          QStringLiteral("Collections"),
                               QStringLiteral("Keywords"),
                               QStringLiteral("SectionTypes"),    QStringLiteral("LabelSettings"),
                               QStringLiteral("StatusSettings"),  QStringLiteral("CustomMetaDataSettings"),
                               QStringLiteral("ProjectBookmarks"),
                               QStringLiteral("ProjectTargets"),  QStringLiteral("RecentWritingHistory"),
                               QStringLiteral("TemplateFolderUUID"), QStringLiteral("BookmarksFolderUUID"),
                               QStringLiteral("PrintSettings")};

QStringList after(const QString &name)
{
    const int i = kTopOrder.indexOf(name);
    return i < 0 ? QStringList() : kTopOrder.mid(i + 1);
}

// A color as the file wrote it, when it is still the same color (this program keeps 8 bits per channel)
QString keptColor(const QString &was, const QColor &now)
{
    if (!was.isEmpty()) {
        const QColor o = parseScrivColor(was);
        if (o.isValid() && now.isValid() && o.toRgb().rgb() == now.toRgb().rgb())
            return was;
    }
    return colorText(now);
}

// The project's keywords (0.81.2): Keyword ID, Title, Color, Children, in this program's order under each
// parent. Left as it is when nothing changed.
void writeKeywords(XmlCtx &c, QDomElement root)
{
    Project *p = c.p;
    const QDomElement old = root.firstChildElement(QStringLiteral("Keywords"));
    if (p->keywords.isEmpty()) {
        if (!old.isNull())
            root.removeChild(old);
        return;
    }
    QHash<QString, QString> oldColor;
    {
        const QDomNodeList list = old.elementsByTagName(QStringLiteral("Keyword"));
        for (int i = 0; i < list.size(); ++i) {
            const QDomElement k = list.at(i).toElement();
            oldColor.insert(k.attribute(QStringLiteral("ID")), k.firstChildElement(QStringLiteral("Color")).text());
        }
    }
    QHash<QString, QList<const Keyword *>> kids;   // our parent id ("" = top) -> keywords
    for (const Keyword &k : std::as_const(p->keywords)) {
        const QString parent = !k.parentId.isEmpty() && p->keywordById(k.parentId) && k.parentId != k.id ? k.parentId : QString();
        kids[parent].append(&k);
    }
    QSet<QString> done;
    std::function<void(QDomElement, const QString &, int)> add = [&](QDomElement into, const QString &parent, int depth) {
        for (const Keyword *k : kids.value(parent)) {
            if (done.contains(k->id) || depth > 64)
                continue;
            done.insert(k->id);
            const QString id = c.keywordIds.value(k->id);
            QDomElement e = c.dom.createElement(QStringLiteral("Keyword"));
            e.setAttribute(QStringLiteral("ID"), id);
            setChildText(e, QStringLiteral("Title"), k->name);
            setChildText(e, QStringLiteral("Color"), keptColor(oldColor.value(id), k->color.isValid() ? k->color : QColor(160, 160, 160)));
            if (!kids.value(k->id).isEmpty()) {
                QDomElement ch = c.dom.createElement(QStringLiteral("Children"));
                add(ch, k->id, depth + 1);
                e.appendChild(ch);
            }
            into.appendChild(e);
        }
    };
    QDomElement fresh = c.dom.createElement(QStringLiteral("Keywords"));
    add(fresh, QString(), 0);
    // a keyword whose parent chain loops is put at the top
    for (const Keyword &k : std::as_const(p->keywords))
        if (!done.contains(k.id)) {
            kids[QStringLiteral("\x01loop")].append(&k);
        }
    add(fresh, QStringLiteral("\x01loop"), 0);
    if (!old.isNull() && canonicalElement(old) == canonicalElement(fresh))
        return;
    placeTop(root, fresh, QStringLiteral("Keywords"), after(QStringLiteral("Keywords")));
}

// The project's custom metadata fields (0.81.2): MetaDataField Type, ID, Title and what the type has (Wraps,
// Align; Default; DateType, DateFormat; ListOptions None, Option ID). A field of the file keeps what this
// program does not know (its element is the starting point).
void writeCustomFields(XmlCtx &c, QDomElement root)
{
    Project *p = c.p;
    const QDomElement old = root.firstChildElement(QStringLiteral("CustomMetaDataSettings"));
    if (p->customFields.isEmpty()) {
        if (!old.isNull())
            root.removeChild(old);
        return;
    }
    QHash<QString, QDomElement> oldField;
    for (QDomElement mf = old.firstChildElement(QStringLiteral("MetaDataField")); !mf.isNull();
         mf = mf.nextSiblingElement(QStringLiteral("MetaDataField")))
        oldField.insert(mf.attribute(QStringLiteral("ID")), mf);
    QDomElement fresh = c.dom.createElement(QStringLiteral("CustomMetaDataSettings"));
    for (const CustomField &f : std::as_const(p->customFields)) {
        const FieldOut &fo = c.fields[f.id];
        const QDomElement was = oldField.value(fo.id);
        QDomElement e;
        if (was.isNull()) {
            e = c.dom.createElement(QStringLiteral("MetaDataField"));
            e.setAttribute(QStringLiteral("Type"), fieldTypeName(f.type));
            e.setAttribute(QStringLiteral("ID"), fo.id);
        } else {
            e = was.cloneNode(true).toElement();
        }
        const bool typeChanged = !was.isNull() && fieldTypeOf(was.attribute(QStringLiteral("Type"))) != f.type;
        if (typeChanged) {
            e.setAttribute(QStringLiteral("Type"), fieldTypeName(f.type));
            for (const QString &a : {QStringLiteral("Wraps"), QStringLiteral("Align"), QStringLiteral("Default"), QStringLiteral("DateType")})
                e.removeAttribute(a);
            removeChild(e, QStringLiteral("ListOptions"));
            removeChild(e, QStringLiteral("DateFormat"));
        }
        if (!f.title.isEmpty() || !e.firstChildElement(QStringLiteral("Title")).isNull()) {
            if (e.firstChildElement(QStringLiteral("Title")).text() != f.title || e.firstChildElement(QStringLiteral("Title")).isNull())
                setText(child(e, QStringLiteral("Title"), true, {QStringLiteral("ListOptions"), QStringLiteral("DateFormat")}), f.title);
        }
        switch (f.type) {
        case CustomFieldType::Text: {
            const QString wraps = f.wrap ? QStringLiteral("Yes") : QStringLiteral("No");
            if (e.attribute(QStringLiteral("Wraps")) != wraps)
                e.setAttribute(QStringLiteral("Wraps"), wraps);
            if (!e.hasAttribute(QStringLiteral("Align")))
                e.setAttribute(QStringLiteral("Align"), QStringLiteral("Left"));
            break;
        }
        case CustomFieldType::Checkbox:
            if (truthy(f.defaultValue)) {
                if (e.attribute(QStringLiteral("Default")) != QLatin1String("Yes"))
                    e.setAttribute(QStringLiteral("Default"), QStringLiteral("Yes"));
            } else {
                e.removeAttribute(QStringLiteral("Default"));
            }
            break;
        case CustomFieldType::Date:
            if (!e.hasAttribute(QStringLiteral("DateType")))
                e.setAttribute(QStringLiteral("DateType"), QStringLiteral("Short"));
            child(e, QStringLiteral("DateFormat"), true);
            break;
        case CustomFieldType::List: {
            const QList<QPair<QString, QString>> want = fo.options;
            QList<QPair<QString, QString>> have;
            QDomElement lo = e.firstChildElement(QStringLiteral("ListOptions"));
            for (QDomElement o = lo.firstChildElement(); !o.isNull(); o = o.nextSiblingElement())
                have.append({o.attribute(QStringLiteral("ID")), o.text()});
            if (lo.isNull() || have != want) {
                QDomElement nlo = c.dom.createElement(QStringLiteral("ListOptions"));
                nlo.setAttribute(QStringLiteral("None"), lo.isNull() ? QStringLiteral("None") : lo.attribute(QStringLiteral("None"), QStringLiteral("None")));
                for (const auto &w : std::as_const(want)) {
                    QDomElement o = c.dom.createElement(QStringLiteral("Option"));
                    o.setAttribute(QStringLiteral("ID"), w.first);
                    o.appendChild(c.dom.createTextNode(w.second));
                    nlo.appendChild(o);
                }
                if (lo.isNull())
                    e.appendChild(nlo);
                else
                    e.replaceChild(nlo, lo);
            }
            break;
        }
        }
        fresh.appendChild(e);
    }
    if (!old.isNull() && canonicalElement(old) == canonicalElement(fresh))
        return;
    placeTop(root, fresh, QStringLiteral("CustomMetaDataSettings"), after(QStringLiteral("CustomMetaDataSettings")));
}

// Arbitrary collections (0.81.2): Collection Type="Arbitrary" ID Color, Title, BinderUUIDs (comma-separated).
// The file's other collections (the binder, search results, Scrivener's saved searches) stay as they are;
// this program's search collections are not written (no verified form).
// A search collection's SearchSettings: the file's element as it is while it still says the same (the words, the
// operator, the case); otherwise those three written, the rest kept (a new one gets the values Scrivener wrote)
void writeSearchSettings(XmlCtx &c, QDomElement col, const SearchQuery &q)
{
    QDomElement ss = col.firstChildElement(QStringLiteral("SearchSettings"));
    if (!ss.isNull()) {
        const SearchQuery was = queryFromScrivener(ss);
        if (was.text == q.text && operatorOf(was) == operatorOf(q) && was.caseSensitive == q.caseSensitive)
            return;
    } else {
        ss = c.dom.createElement(QStringLiteral("SearchSettings"));
        ss.setAttribute(QStringLiteral("Type"), QStringLiteral("Text"));
        ss.setAttribute(QStringLiteral("Scope"), QStringLiteral("All"));
        ss.setAttribute(QStringLiteral("CompileSetting"), q.compile == 1 ? QStringLiteral("Included")
                                                          : q.compile == 2 ? QStringLiteral("Excluded") : QStringLiteral("All"));
        ss.setAttribute(QStringLiteral("IgnoreDiacritics"), QStringLiteral("No"));
        col.appendChild(ss);
    }
    ss.setAttribute(QStringLiteral("Operator"), operatorOf(q));
    ss.setAttribute(QStringLiteral("CaseSensitive"), q.caseSensitive ? QStringLiteral("Yes") : QStringLiteral("No"));
    setText(ss, q.text);
}

void writeCollections(XmlCtx &c, QDomElement cols)
{
    Project *p = c.p;
    // 0.90.0: this program's search collections are Scrivener's saved searches (they stayed in the working copy)
    QList<const Collection *> ours;
    for (const Collection &col : std::as_const(p->collections))
        ours.append(&col);
    QList<QDomElement> theirs;
    QStringList haveIds;
    for (QDomElement e = cols.firstChildElement(QStringLiteral("Collection")); !e.isNull();
         e = e.nextSiblingElement(QStringLiteral("Collection")))
        if (e.attribute(QStringLiteral("Type")) == QLatin1String("Arbitrary") || e.attribute(QStringLiteral("Type")) == QLatin1String("SavedSearch")) {
            theirs.append(e);
            haveIds << e.attribute(QStringLiteral("ID")).toUpper();
        }
    QStringList wantIds;
    for (const Collection *col : std::as_const(ours))
        wantIds << uuidFor(col->id);
    auto update = [&](QDomElement e, const Collection *col) {
        const QString type = col->isSearch ? QStringLiteral("SavedSearch") : QStringLiteral("Arbitrary");
        if (e.attribute(QStringLiteral("Type")) != type)
            e.setAttribute(QStringLiteral("Type"), type);
        if (e.attribute(QStringLiteral("ID")).toUpper() != uuidFor(col->id))
            e.setAttribute(QStringLiteral("ID"), uuidFor(col->id));
        const QString color = keptColor(e.attribute(QStringLiteral("Color")), col->color.isValid() ? col->color
                                                                     : col->isSearch ? QColor(252, 224, 214) : QColor(193, 217, 197));
        if (e.attribute(QStringLiteral("Color")) != color)
            e.setAttribute(QStringLiteral("Color"), color);
        if (e.firstChildElement(QStringLiteral("Title")).isNull() || e.firstChildElement(QStringLiteral("Title")).text() != col->name)
            setText(child(e, QStringLiteral("Title"), true, {QStringLiteral("BinderUUIDs"), QStringLiteral("SearchSettings")}), col->name);
        if (col->isSearch) {
            removeChild(e, QStringLiteral("BinderUUIDs"));
            writeSearchSettings(c, e, col->query);
            return;
        }
        removeChild(e, QStringLiteral("SearchSettings"));
        QStringList want;
        for (const QString &id : col->items)
            if (p->item(id) && !want.contains(uuidFor(id)))
                want << uuidFor(id);
        QDomElement list = e.firstChildElement(QStringLiteral("BinderUUIDs"));
        QStringList have;
        if (!list.isNull()) {
            for (const QString &u : list.text().split(QLatin1Char(','), Qt::SkipEmptyParts))
                have << u.trimmed().toUpper();
        } else {
            const QDomNodeList refs = e.elementsByTagName(QStringLiteral("BinderItem"));
            for (int i = 0; i < refs.size(); ++i)
                have << refs.at(i).toElement().attribute(QStringLiteral("UUID")).toUpper();
        }
        if (have == want)
            return;
        // another form of the list (each document an element of its own): replaced by Scrivener for Windows' form
        for (QDomElement ch = e.firstChildElement(); !ch.isNull();) {
            const QDomElement next = ch.nextSiblingElement();
            if (ch.tagName() == QLatin1String("BinderItem") || !ch.elementsByTagName(QStringLiteral("BinderItem")).isEmpty())
                e.removeChild(ch);
            ch = next;
        }
        if (want.isEmpty()) {
            removeChild(e, QStringLiteral("BinderUUIDs"));
            return;
        }
        setText(child(e, QStringLiteral("BinderUUIDs"), true), want.join(QLatin1Char(',')));
    };
    if (haveIds == wantIds) {
        for (int i = 0; i < theirs.size(); ++i)
            update(theirs[i], ours[i]);
        return;
    }
    // added, removed or in another order: the arbitrary collections and saved searches are put after the file's others
    QHash<QString, QDomElement> byId;
    for (const QDomElement &e : std::as_const(theirs)) {
        byId.insert(e.attribute(QStringLiteral("ID")).toUpper(), e.cloneNode(true).toElement());
        cols.removeChild(e);
    }
    for (const Collection *col : std::as_const(ours)) {
        QDomElement e = byId.value(uuidFor(col->id));
        if (e.isNull())
            e = c.dom.createElement(QStringLiteral("Collection"));
        update(e, col);
        cols.appendChild(e);
    }
}

QByteArray buildScrivx(Project *p, const QByteArray &existing, const QString &identifier, const QString &data = QString())
{
    XmlCtx c;
    c.p = p;
    c.data = data;
    QDomElement root;
    if (!existing.isEmpty() && c.dom.setContent(existing)) {
        root = c.dom.documentElement();
        indexOld(c, root.firstChildElement(QStringLiteral("Binder")));
    } else {
        c.dom = QDomDocument();
        c.dom.appendChild(c.dom.createProcessingInstruction(QStringLiteral("xml"),
                                                            QStringLiteral("version=\"1.0\" encoding=\"UTF-8\"")));
        root = c.dom.createElement(QStringLiteral("ScrivenerProject"));
        c.dom.appendChild(root);
        // the program that wrote the format; Device says who really wrote it
        root.setAttribute(QStringLiteral("Creator"), QStringLiteral("SCRWIN-3.1.5.1"));
    }
    if (!root.hasAttribute(QStringLiteral("Identifier")))
        root.setAttribute(QStringLiteral("Identifier"), identifier);
    root.setAttribute(QStringLiteral("Version"), QStringLiteral("2.0"));
    root.setAttribute(QStringLiteral("Device"), QStringLiteral(QB_APP_NAME " " QB_APP_VERSION " (%1)").arg(QSysInfo::machineHostName()));
    root.setAttribute(QStringLiteral("Modified"), dateText(QDateTime::currentDateTime()));
    root.setAttribute(QStringLiteral("ModID"), QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper());

    // Scrivener's ids for keywords and custom fields, before the binder uses them (0.81.2)
    prepareMeta(c, root);

    // binder
    QDomElement binder = c.dom.createElement(QStringLiteral("Binder"));
    for (const BinderItem *it : p->root()->children)
        binder.appendChild(binderItem(c, it));
    QDomElement oldBinder = root.firstChildElement(QStringLiteral("Binder"));
    if (oldBinder.isNull())
        root.insertBefore(binder, root.firstChild());
    else
        root.replaceChild(binder, oldBinder);

    // collections: the binder and the search results, as Scrivener has them;
    // collections of an existing .scrivx stay as they are
    if (root.firstChildElement(QStringLiteral("Collections")).isNull()) {
        QDomElement cols = replaceTop(root, QStringLiteral("Collections"), after(QStringLiteral("Collections")));
        const QStringList types = {QStringLiteral("Binder"), QStringLiteral("RecentSearch")};
        const QStringList titles = {QStringLiteral("Binder"), QStringLiteral("Search Results")};
        for (int i = 0; i < 2; ++i) {
            QDomElement col = c.dom.createElement(QStringLiteral("Collection"));
            col.setAttribute(QStringLiteral("Type"), types[i]);
            col.setAttribute(QStringLiteral("ID"), QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper());
            col.setAttribute(QStringLiteral("Color"), QStringLiteral("0.901957 0.901984 0.901969"));
            setChildText(col, QStringLiteral("Title"), titles[i]);
            cols.appendChild(col);
        }
    }
    writeCollections(c, root.firstChildElement(QStringLiteral("Collections")));
    writeKeywords(c, root);

    // section types. 0.90.0: a project file without any (Scrivener writes none into some) gets them only once a
    // document has a section type; before, the first write added this program's standard types to such a file
    const QList<BinderItem *> allForTypes = p->allItems();
    if (!root.firstChildElement(QStringLiteral("SectionTypes")).isNull()
        || std::any_of(allForTypes.cbegin(), allForTypes.cend(), [](const BinderItem *it) { return !it->sectionType.isEmpty(); })) {
        QDomElement sts = root.firstChildElement(QStringLiteral("SectionTypes"));
        if (sts.isNull())
            sts = replaceTop(root, QStringLiteral("SectionTypes"), after(QStringLiteral("SectionTypes")));
        QDomElement defs = child(sts, QStringLiteral("TypeDefinitions"), true, {QStringLiteral("LevelTypes")});
        while (!defs.firstChild().isNull())
            defs.removeChild(defs.firstChild());
        for (const SectionType &st : p->sectionTypes) {
            QDomElement t = c.dom.createElement(QStringLiteral("Type"));
            t.setAttribute(QStringLiteral("ID"), uuidFor(st.id));
            t.appendChild(c.dom.createTextNode(st.name));
            defs.appendChild(t);
        }
        QDomElement levels = child(sts, QStringLiteral("LevelTypes"), true);
        while (!levels.firstChild().isNull())
            levels.removeChild(levels.firstChild());
        const QList<QPair<QString, QStringList>> lv = {{QStringLiteral("Folders"), p->structureDefaults.folders},
                                                       {QStringLiteral("Containers"), p->structureDefaults.fileGroups},
                                                       {QStringLiteral("Files"), p->structureDefaults.files}};
        for (const auto &l : lv) {
            QDomElement e = c.dom.createElement(l.first);
            for (const QString &id : l.second) {
                QDomElement t = c.dom.createElement(QStringLiteral("Type"));
                t.appendChild(c.dom.createTextNode(uuidFor(id)));
                e.appendChild(t);
            }
            levels.appendChild(e);
        }
    }

    // labels and statuses
    {
        // 0.64.2: a label's color as the file wrote it stays when it is still the same color (this program
        // keeps 8 bits per channel; writing 0.992157 for Scrivener's 0.993495 changed the file without a change)
        QHash<int, QString> oldColor;
        for (QDomElement l = root.firstChildElement(QStringLiteral("LabelSettings")).firstChildElement(QStringLiteral("Labels"))
                                 .firstChildElement(QStringLiteral("Label"));
             !l.isNull(); l = l.nextSiblingElement(QStringLiteral("Label")))
            if (l.hasAttribute(QStringLiteral("Color")))
                oldColor.insert(l.attribute(QStringLiteral("ID")).toInt(), l.attribute(QStringLiteral("Color")));
        auto labelColor = [&](const Label &l) {
            const QString was = oldColor.value(l.id);
            if (!was.isEmpty()) {
                const QColor o = parseScrivColor(was);
                if (o.isValid() && l.color.isValid() && o.toRgb().rgb() == l.color.toRgb().rgb())
                    return was;
            }
            return colorText(l.color);
        };
        // 0.90.0: the default label and status of new documents (Scrivener's; this program has none) and the name
        // of "none" stay as the file has them. Before, every write set the defaults to none, and under the German
        // interface named the entry "Kein Label" in the file. A new .scriv gets Scrivener's English form.
        auto fileDefault = [&](const char *settings, const char *def, auto exists) {
            const QString was = root.firstChildElement(QLatin1String(settings)).firstChildElement(QLatin1String(def)).text().trimmed();
            bool ok = false;
            const int id = was.toInt(&ok);
            return ok && id >= 0 && exists(id) ? was : QStringLiteral("-1");
        };
        auto fileNone = [&](const char *settings, const char *list, const char *item, const QString &title) {
            for (QDomElement x = root.firstChildElement(QLatin1String(settings)).firstChildElement(QLatin1String(list)).firstChildElement(QLatin1String(item));
                 !x.isNull(); x = x.nextSiblingElement(QLatin1String(item)))
                if (x.attribute(QStringLiteral("ID")) == QLatin1String("-1") && !x.text().trimmed().isEmpty())
                    return x.text();
            return QStringLiteral("No %1").arg(title);
        };
        const QString defaultLabel = fileDefault("LabelSettings", "DefaultLabelID", [&](int id) { return p->labelById(id) != nullptr; });
        const QString defaultStatus = fileDefault("StatusSettings", "DefaultStatusID", [&](int id) { return p->statusById(id) != nullptr; });
        const QString noLabel = fileNone("LabelSettings", "Labels", "Label", p->labelTitle);
        const QString noStatus = fileNone("StatusSettings", "StatusItems", "Status", p->statusTitle);
        QDomElement ls = replaceTop(root, QStringLiteral("LabelSettings"), after(QStringLiteral("LabelSettings")));
        setChildText(ls, QStringLiteral("Title"), p->labelTitle);
        setChildText(ls, QStringLiteral("DefaultLabelID"), defaultLabel);
        QDomElement labels = child(ls, QStringLiteral("Labels"), true);
        QDomElement none = c.dom.createElement(QStringLiteral("Label"));
        none.setAttribute(QStringLiteral("ID"), QStringLiteral("-1"));
        none.appendChild(c.dom.createTextNode(noLabel));
        labels.appendChild(none);
        for (const Label &l : p->labels) {
            QDomElement e = c.dom.createElement(QStringLiteral("Label"));
            e.setAttribute(QStringLiteral("ID"), l.id);
            e.setAttribute(QStringLiteral("Color"), labelColor(l));
            e.appendChild(c.dom.createTextNode(l.name));
            labels.appendChild(e);
        }
        QDomElement ss = replaceTop(root, QStringLiteral("StatusSettings"), after(QStringLiteral("StatusSettings")));
        setChildText(ss, QStringLiteral("Title"), p->statusTitle);
        setChildText(ss, QStringLiteral("DefaultStatusID"), defaultStatus);
        QDomElement items = child(ss, QStringLiteral("StatusItems"), true);
        QDomElement nos = c.dom.createElement(QStringLiteral("Status"));
        nos.setAttribute(QStringLiteral("ID"), QStringLiteral("-1"));
        nos.appendChild(c.dom.createTextNode(noStatus));
        items.appendChild(nos);
        for (const Status &s : p->statuses) {
            QDomElement e = c.dom.createElement(QStringLiteral("Status"));
            e.setAttribute(QStringLiteral("ID"), s.id);
            e.appendChild(c.dom.createTextNode(s.name));
            items.appendChild(e);
        }
    }

    writeCustomFields(c, root);

    // project bookmarks. 0.90.0: all of them, in their order; before, only those to documents were written, so a
    // bookmark to a web address or a file that Scrivener had there was taken out of the .scriv by the next write
    {
        // 0.81.2: Scrivener for Windows writes the document's title in the element; a file that has them so keeps
        // them (they were left out, so every save of such a project changed its binder file)
        const QDomElement oldBm = root.firstChildElement(QStringLiteral("ProjectBookmarks"));
        bool titled = oldBm.isNull() || oldBm.firstChildElement(QStringLiteral("Bookmark")).isNull();
        for (QDomElement b = oldBm.firstChildElement(QStringLiteral("Bookmark")); !b.isNull(); b = b.nextSiblingElement(QStringLiteral("Bookmark")))
            if (!b.hasAttribute(QStringLiteral("Destination")) && !b.text().trimmed().isEmpty())
                titled = true;
        QDomElement fresh = c.dom.createElement(QStringLiteral("ProjectBookmarks"));
        for (const Bookmark &b : p->projectBookmarks) {
            const QDomElement be = bookmarkElement(c, b, titled);
            if (!be.isNull())
                fresh.appendChild(be);
        }
        if (oldBm.isNull() || !sameBookmarks(oldBm, fresh)) {
            QDomElement bm = replaceTop(root, QStringLiteral("ProjectBookmarks"), after(QStringLiteral("ProjectBookmarks")));
            while (!fresh.firstChild().isNull())
                bm.appendChild(fresh.firstChild());
        }
    }

    // targets: the numbers and their kind; the rest of an existing element stays
    {
        QDomElement tg = root.firstChildElement(QStringLiteral("ProjectTargets"));
        if (tg.isNull()) {
            tg = replaceTop(root, QStringLiteral("ProjectTargets"), after(QStringLiteral("ProjectTargets")));
            tg.setAttribute(QStringLiteral("Notify"), QStringLiteral("No"));
        }
        QDomElement dt = child(tg, QStringLiteral("DraftTarget"), true, {QStringLiteral("SessionTarget")});
        dt.setAttribute(QStringLiteral("Type"), p->targets.draftChars ? QStringLiteral("Characters") : QStringLiteral("Words"));
        if (!dt.hasAttribute(QStringLiteral("CountIncludedOnly")))
            dt.setAttribute(QStringLiteral("CountIncludedOnly"), QStringLiteral("Yes"));
        if (p->targets.deadline.isValid()) {
            // 0.90.0: the file's own text of the day stays while it is that day (its time of day, its zone)
            if (!dt.attribute(QStringLiteral("Deadline")).startsWith(p->targets.deadline.toString(QStringLiteral("yyyy-MM-dd"))))
                dt.setAttribute(QStringLiteral("Deadline"), dateText(QDateTime(p->targets.deadline, QTime(23, 59))));
            dt.setAttribute(QStringLiteral("IgnoreDeadline"), QStringLiteral("No"));
        } else if (dt.hasAttribute(QStringLiteral("Deadline"))) {
            dt.setAttribute(QStringLiteral("IgnoreDeadline"), QStringLiteral("Yes"));
        }
        setText(dt, QString::number(p->targets.draftTarget));
        QDomElement st = child(tg, QStringLiteral("SessionTarget"), true);
        st.setAttribute(QStringLiteral("Type"), p->targets.sessionChars ? QStringLiteral("Characters") : QStringLiteral("Words"));
        setText(st, QString::number(p->targets.sessionTarget));
        // 0.90.0: whether only the draft counts, and the target from the deadline (written where they differ, so a file
        // without them stays without them while they are this program's standard)
        const bool draftOnly = !p->targets.countAnywhere;
        if (st.hasAttribute(QStringLiteral("CountDraftOnly")) || !draftOnly)
            if ((st.attribute(QStringLiteral("CountDraftOnly")).compare(QLatin1String("Yes"), Qt::CaseInsensitive) == 0) != draftOnly
                || !st.hasAttribute(QStringLiteral("CountDraftOnly")))
                st.setAttribute(QStringLiteral("CountDraftOnly"), draftOnly ? QStringLiteral("Yes") : QStringLiteral("No"));
        if (st.hasAttribute(QStringLiteral("DeterminedFromDeadline")) || p->targets.autoFromDeadline)
            if ((st.attribute(QStringLiteral("DeterminedFromDeadline")).compare(QLatin1String("Yes"), Qt::CaseInsensitive) == 0) != p->targets.autoFromDeadline
                || !st.hasAttribute(QStringLiteral("DeterminedFromDeadline")))
                st.setAttribute(QStringLiteral("DeterminedFromDeadline"), p->targets.autoFromDeadline ? QStringLiteral("Yes") : QStringLiteral("No"));
    }

    auto folderRef = [&](const QString &name, const QString &id) {
        QDomElement e = root.firstChildElement(name);
        if (!id.isEmpty() && p->item(id)) {
            if (e.isNull())
                e = replaceTop(root, name, after(name));
            setText(e, uuidFor(id));
        } else if (!e.isNull()) {
            root.removeChild(e);
        }
    };
    folderRef(QStringLiteral("TemplateFolderUUID"), p->templatesFolderId);
    folderRef(QStringLiteral("BookmarksFolderUUID"), p->notesFolderId);
    return c.dom.toByteArray(4);
}

QByteArray projectPreferences(const Project *p)
{
    QTextDocument d;
    d.setPlainText(QStringLiteral("Attributes"));
    const QByteArray fmt = rtf::write(&d, p->formatting);
    QDomDocument dom;
    dom.appendChild(dom.createProcessingInstruction(QStringLiteral("xml"), QStringLiteral("version=\"1.0\" encoding=\"UTF-8\"")));
    QDomElement root = dom.createElement(QStringLiteral("ProjectPreferences"));
    root.setAttribute(QStringLiteral("Version"), QStringLiteral("1.0"));
    dom.appendChild(root);
    setChildText(root, QStringLiteral("UseProjectPreferences"), QStringLiteral("Yes"));
    QDomElement t = dom.createElement(QStringLiteral("TextFormatRTFData"));
    t.appendChild(dom.createCDATASection(QString::fromUtf8(fmt)));
    root.appendChild(t);
    return dom.toByteArray(4);
}

QString xmlEscape(const QString &s)
{
    QString o = s.toHtmlEscaped();
    // control characters are not allowed in XML 1.0
    for (QChar &ch : o)
        if (ch.unicode() < 0x20 && ch != QLatin1Char('\n') && ch != QLatin1Char('\t'))
            ch = QLatin1Char(' ');
    return o;
}

QByteArray searchIndexes(Project *p)
{
    QString o = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<SearchIndexes Version=\"1.0\">\n    <Documents>\n");
    for (BinderItem *it : p->allItems()) {
        o += QStringLiteral("        <Document ID=\"%1\">\n").arg(uuidFor(it->id));
        o += QStringLiteral("            <Title>%1</Title>\n").arg(xmlEscape(it->title));
        if (it->hasText()) {
            const QString t = p->docs()->plainText(it->id);
            if (!t.trimmed().isEmpty())
                o += QStringLiteral("            <Text>%1</Text>\n").arg(xmlEscape(t));
            const QString n = p->docs()->notesPlainText(it->id);
            if (!n.trimmed().isEmpty())
                o += QStringLiteral("            <Notes>%1</Notes>\n").arg(xmlEscape(n));
        }
        if (!it->synopsis.isEmpty())
            o += QStringLiteral("            <Synopsis>%1</Synopsis>\n").arg(xmlEscape(it->synopsis));
        o += QStringLiteral("        </Document>\n");
    }
    o += QStringLiteral("    </Documents>\n</SearchIndexes>\n");
    return o.toUtf8();
}

QString snapshotFileName(const QDateTime &dt)
{
    // "2025-10-27 02:20:40 -0700" -> "2025-10-27-02-20-40-0700"
    const QString d = dateText(dt);
    return d.left(19).replace(QLatin1Char(' '), QLatin1Char('-')).replace(QLatin1Char(':'), QLatin1Char('-')) + d.mid(20)
           + QLatin1String(".rtf");
}

// "2025-10-27-02-20-40-0700(.rtf)" (a file name) or "2025-10-27 02:20:40 -0700" (index.xml)
// -> the moment; without an offset local time; invalid when it is neither (0.61.1)
QDateTime snapshotMoment(const QString &nameOrDate)
{
    QString t = nameOrDate.trimmed();
    if (t.endsWith(QLatin1String(".rtf"), Qt::CaseInsensitive))
        t.chop(4);
    if (t.size() < 19)
        return QDateTime();
    QString clock = t.mid(11, 8);
    clock.replace(QLatin1Char('-'), QLatin1Char(':'));
    QDateTime d = QDateTime::fromString(t.left(10) + QLatin1Char(' ') + clock, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (!d.isValid())
        return d;
    const QString off = t.mid(19).trimmed();
    if (off.size() >= 5 && (off.at(0) == QLatin1Char('+') || off.at(0) == QLatin1Char('-'))) {
        const int secs = off.mid(1, 2).toInt() * 3600 + off.mid(3, 2).toInt() * 60;
        d.setOffsetFromUtc(off.at(0) == QLatin1Char('-') ? -secs : secs);
    }
    return d;
}

} // namespace

// ---------------------------------------------------------------- public helpers

SearchQuery queryFromScrivener(const QDomElement &settings)
{
    SearchQuery q;
    q.text = settings.text();
    const QString op = settings.attribute(QStringLiteral("Operator"));
    q.anyWord = op.compare(QLatin1String("Any"), Qt::CaseInsensitive) == 0;
    q.exactPhrase = op.compare(QLatin1String("Exact"), Qt::CaseInsensitive) == 0 || op.compare(QLatin1String("Phrase"), Qt::CaseInsensitive) == 0;
    q.regex = op.compare(QLatin1String("RegEx"), Qt::CaseInsensitive) == 0;
    q.allWords = !q.anyWord && !q.exactPhrase && !q.regex;
    q.caseSensitive = settings.attribute(QStringLiteral("CaseSensitive")).compare(QLatin1String("Yes"), Qt::CaseInsensitive) == 0;
    const QString cs = settings.attribute(QStringLiteral("CompileSetting"));
    q.compile = cs.compare(QLatin1String("Included"), Qt::CaseInsensitive) == 0 ? 1 : cs.compare(QLatin1String("Excluded"), Qt::CaseInsensitive) == 0 ? 2 : 0;
    return q;
}

QString valueFromScrivener(CustomFieldType type, const QString &text, const QHash<QString, QString> &optionText)
{
    switch (type) {
    case CustomFieldType::Checkbox:
        if (truthy(text))
            return QStringLiteral("1");
        if (falsy(text))
            return QStringLiteral("0");
        return text;
    case CustomFieldType::List:
        if (text.isEmpty() || text == QLatin1String("-1"))
            return QString();
        return optionText.value(text.trimmed());   // an option that is gone: none, as Scrivener shows it
    case CustomFieldType::Date: {
        const QRegularExpressionMatch m = scrivDatePattern().match(text.trimmed());
        return m.hasMatch() ? m.captured(1) : text;
    }
    case CustomFieldType::Text: break;
    }
    return text;
}

QString dateText(const QDateTime &in)
{
    const QDateTime d = (in.isValid() ? in : QDateTime::currentDateTime()).toLocalTime();
    int off = d.offsetFromUtc();
    const QChar sign = off < 0 ? QLatin1Char('-') : QLatin1Char('+');
    off = qAbs(off);
    return d.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss ")) + sign
           + QStringLiteral("%1%2").arg(off / 3600, 2, 10, QLatin1Char('0')).arg((off % 3600) / 60, 2, 10, QLatin1Char('0'));
}

QString stateFile(const Project *p) { return p->settingsDir() + QLatin1String("/scrivener-sync.json"); }

QString scrivFolder(const QString &path)
{
    QFileInfo fi(path);
    if (fi.isFile() && fi.suffix().compare(QLatin1String("scrivx"), Qt::CaseInsensitive) == 0)
        return fi.absolutePath();
    return fi.absoluteFilePath();
}

bool isScrivenerPath(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.suffix().compare(QLatin1String("scrivx"), Qt::CaseInsensitive) == 0)
        return true;
    if (fi.isDir() && fi.suffix().compare(QLatin1String("scriv"), Qt::CaseInsensitive) == 0)
        return true;
    return false;
}

QString scrivxIn(const QString &scrivDir)
{
    const QString found = ScrivenerImporter::findScrivx(scrivDir);
    if (!found.isEmpty())
        return found;
    QString base = QFileInfo(scrivDir).fileName();
    if (base.endsWith(QLatin1String(".scriv"), Qt::CaseInsensitive))
        base.chop(6);
    return QDir(scrivDir).filePath(base + QLatin1String(".scrivx"));
}

QString fingerprint(const QString &scrivDirIn)
{
    const QString scrivDir = QFileInfo(scrivDirIn).absoluteFilePath();
    FingerprintCache c;
    c.scrivDir = scrivDir;
    c.scrivxSha = sha1File(scrivxIn(scrivDir));
    for (const QString &sub : {QStringLiteral("Files/Data"), QStringLiteral("Snapshots")}) {
        const QString base = QDir(scrivDir).filePath(sub);
        QDirIterator it(base, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (fi.fileName() == QLatin1String("docs.checksum"))
                continue;
            c.lines.insert(QDir(scrivDir).relativeFilePath(fi.absoluteFilePath()), fingerprintLine(scrivDir, fi));
        }
    }
    const QString result = fingerprintOf(c.scrivxSha, c.lines.values());
    fingerprintCache() = std::move(c);
    return result;
}

bool lockedByScrivener(const QString &scrivDir, QString *who)
{
    for (const QString &rel : {QStringLiteral("Files/user.lock"), QStringLiteral("user.lock")}) {
        QFile f(QDir(scrivDir).filePath(rel));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        // 0.64.2: "key=value" lines or XML elements, the keys in any case ("user", "userName", "host",
        // "hostName", "computer", "computerName"); the exact form Scrivener writes was not verified here
        QString user, host;
        const QString all = QString::fromUtf8(f.read(64 * 1024));
        static const QRegularExpression kv(QStringLiteral("^\\s*(user|username|host|hostname|computer|computername)\\s*[=:]\\s*(.+?)\\s*$"),
                                           QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
        static const QRegularExpression el(QStringLiteral("<(user|username|host|hostname|computer|computername)>\\s*([^<]+?)\\s*</"),
                                           QRegularExpression::CaseInsensitiveOption);
        for (const QRegularExpression *re : {&kv, &el}) {
            auto it = re->globalMatch(all);
            while (it.hasNext()) {
                const auto m = it.next();
                const QString k = m.captured(1).toLower();
                QString &target = k.startsWith(QLatin1String("user")) ? user : host;
                if (target.isEmpty())
                    target = m.captured(2).left(100);
            }
        }
        if (who)
            *who = (user.isEmpty() && host.isEmpty()) ? QObject::tr("unknown")
                   : user.isEmpty()                    ? host
                   : host.isEmpty()                    ? user
                                                       : QObject::tr("%1 on %2").arg(user, host);
        return true;
    }
    return false;
}

QString scrivenerLockStamp(const QString &scrivDir)
{
    for (const QString &rel : {QStringLiteral("Files/user.lock"), QStringLiteral("user.lock")}) {
        const QString file = QDir(scrivDir).filePath(rel);
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QFileInfo fi(file);
        const QByteArray hash = QCryptographicHash::hash(f.read(64 * 1024), QCryptographicHash::Sha1).toHex();
        return QStringLiteral("%1|%2|%3|%4")
            .arg(rel, QString::number(fi.size()), QString::number(fi.lastModified().toMSecsSinceEpoch()), QString::fromLatin1(hash));
    }
    return QString();
}

QString workingCopiesDir() { return dataDir() + QLatin1String("/ScrivenerProjects"); }

bool SyncState::load(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    scrivPath = o.value(QStringLiteral("scriv")).toString();
    identifier = o.value(QStringLiteral("identifier")).toString();
    fingerprint = o.value(QStringLiteral("fingerprint")).toString();
    indexStale = o.value(QStringLiteral("indexStale")).toBool();
    ours.clear();
    theirs.clear();
    const QJsonObject a = o.value(QStringLiteral("ours")).toObject();
    for (auto it = a.begin(); it != a.end(); ++it)
        ours.insert(it.key(), it.value().toString());
    const QJsonObject b = o.value(QStringLiteral("theirs")).toObject();
    for (auto it = b.begin(); it != b.end(); ++it)
        theirs.insert(it.key(), it.value().toString());
    snapshots.clear();
    for (const QJsonValue &v : o.value(QStringLiteral("snapshots")).toArray())
        snapshots << v.toString();
    binder.clear();
    binderKnown = o.contains(QStringLiteral("binder"));
    for (const QJsonValue &v : o.value(QStringLiteral("binder")).toArray())
        binder << v.toString();
    meta = o.value(QStringLiteral("meta")).toInt();
    return true;
}

bool SyncState::save(const QString &file) const
{
    QJsonObject o;
    o[QStringLiteral("scriv")] = scrivPath;
    o[QStringLiteral("identifier")] = identifier;
    o[QStringLiteral("fingerprint")] = fingerprint;
    if (indexStale)
        o[QStringLiteral("indexStale")] = true;
    QJsonObject a, b;
    for (auto it = ours.cbegin(); it != ours.cend(); ++it)
        a[it.key()] = it.value();
    for (auto it = theirs.cbegin(); it != theirs.cend(); ++it)
        b[it.key()] = it.value();
    o[QStringLiteral("ours")] = a;
    o[QStringLiteral("theirs")] = b;
    o[QStringLiteral("snapshots")] = QJsonArray::fromStringList(snapshots);
    if (binderKnown)
        o[QStringLiteral("binder")] = QJsonArray::fromStringList(binder);
    if (meta > 0)
        o[QStringLiteral("meta")] = meta;
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    safesave::write(f, QJsonDocument(o).toJson());
    return safesave::commit(f);
}

// ---------------------------------------------------------------- write

bool write(Project *p, const QString &scrivDirIn, SyncState &state, WriteReport *reportIn, QString *error, bool quick)
{
    timing::Laps laps;
    WriteReport localReport;
    WriteReport &rep = reportIn ? *reportIn : localReport;
    const QString scrivDir = QFileInfo(scrivDirIn).absoluteFilePath();
    // 0.64.2: a .scriv this program wrote before and that is gone now (renamed, moved, its drive not connected)
    // is not made anew: the new one held only the changed documents (the others counted as changed elsewhere),
    // and its name kept the original from being put back. The changes stay in the working copy.
    if (!QFileInfo::exists(scrivDir) && !state.theirs.isEmpty()
        && QFileInfo(state.scrivPath).absoluteFilePath() == scrivDir) {
        if (error)
            *error = QObject::tr("The Scrivener project is no longer at %1 (renamed, moved, or its drive is not connected). "
                                 "Nothing was written into it; your changes are kept here. Put the project back and save again.")
                         .arg(scrivDir);
        return false;
    }
    if (!QDir().mkpath(scrivDir + QLatin1String("/Files/Data")) || !QDir().mkpath(scrivDir + QLatin1String("/Settings"))
        || !QDir().mkpath(scrivDir + QLatin1String("/Snapshots"))) {
        if (error)
            *error = QObject::tr("Cannot create the Scrivener project folder:\n%1").arg(scrivDir);
        return false;
    }
    const QString scrivx = scrivxIn(scrivDir);
    QByteArray existing;
    {
        QFile f(scrivx);
        if (f.open(QIODevice::ReadOnly))
            existing = f.readAll();
    }
    const bool fresh = existing.isEmpty();
    if (fresh)
        quick = false; // no .scrivx (yet, or no longer): everything is written
    if (state.identifier.isEmpty()) {
        QDomDocument d;
        if (!existing.isEmpty() && d.setContent(existing))
            state.identifier = d.documentElement().attribute(QStringLiteral("Identifier"));
        if (state.identifier.isEmpty())
            state.identifier = QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();
    }
    state.scrivPath = scrivDir;
    const QString data = scrivDir + QLatin1String("/Files/Data/");

    // docs.checksum: SHA-1 of the files Scrivener checks. 0.81.3: written in the file's own form. Scrivener 3.1.6.0 for
    // Windows writes, sorted, "UUID/<file>=<SHA-1>" for a document's texts (content.rtf, content.styles,
    // content.comments, notes.rtf, synopsis.txt), the UUID in capitals, and for most documents a line with the UUID
    // alone before them (not for documents made since, and also for some audio and picture documents: its meaning is
    // not known, so those lines are kept as they are and none is added). A file with lower-case keys keeps that form
    // (before 0.81.3 every file was written so: Scrivener for Windows' UUID lines were lost, and a text written here
    // got a second, lower-case line beside its old one). A new .scriv gets Windows' form. Keys here: UUIDs in capitals.
    QMap<QString, QString> checksums;   // "UUID/name" -> SHA-1
    QSet<QString> checksumGroups;        // the UUID lines
    const QString checksumFile = data + QLatin1String("docs.checksum");
    const bool hadChecksums = QFile::exists(checksumFile);
    bool checksumUpper = true, checksumAllKinds = true;
    {
        QFile f(checksumFile);
        bool lowerSeen = false, upperSeen = false, extraKinds = false;
        if (f.open(QIODevice::ReadOnly))
            for (const QByteArray &raw : f.readAll().split('\n')) {
                const QString line = QString::fromUtf8(raw).trimmed();
                if (line.isEmpty())
                    continue;
                const int eq = int(line.indexOf(QLatin1Char('=')));
                const QString key = eq > 0 ? line.left(eq) : line;
                const QString uuid = key.section(QLatin1Char('/'), 0, 0);
                for (const QChar c : uuid) {
                    lowerSeen = lowerSeen || (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
                    upperSeen = upperSeen || (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
                }
                if (eq > 0) {
                    const QString name = key.section(QLatin1Char('/'), 1);
                    extraKinds = extraKinds || name == QLatin1String("content.styles") || name == QLatin1String("content.comments");
                    checksums.insert(uuid.toUpper() + QLatin1Char('/') + name, line.mid(eq + 1).trimmed());
                } else if (!key.contains(QLatin1Char('/'))) {
                    checksumGroups.insert(uuid.toUpper());
                }
            }
        if (!checksums.isEmpty() || !checksumGroups.isEmpty()) {
            checksumUpper = upperSeen || !lowerSeen;
            checksumAllKinds = checksumUpper || extraKinds;
        }
    }
    auto checksummed = [&](const QString &name) {
        return name == QLatin1String("content.rtf") || name == QLatin1String("notes.rtf") || name == QLatin1String("synopsis.txt")
               || (checksumAllKinds && (name == QLatin1String("content.styles") || name == QLatin1String("content.comments")));
    };
    bool textChanged = fresh;
    QSet<QString> touched; // files written or removed (for the fingerprint of a quick write)
    auto put = [&](const QString &uuid, const QString &name, const QByteArray &bytes) -> bool {
        if (!writeFile(data + uuid + QLatin1Char('/') + name, bytes, error))
            return false;
        touched.insert(data + uuid + QLatin1Char('/') + name);
        ++rep.written;
        state.theirs.insert(uuid + QLatin1Char('/') + name, sha1(bytes));
        if (checksummed(name)) {
            checksums.insert(uuid.toUpper() + QLatin1Char('/') + name, sha1(bytes));
        }
        textChanged = true;
        return true;
    };
    auto drop = [&](const QString &uuid, const QString &name) {
        const QString path = data + uuid + QLatin1Char('/') + name;
        if (QFile::exists(path)) {
            QFile::remove(path);
            touched.insert(path);
            textChanged = true;
        }
        state.theirs.remove(uuid + QLatin1Char('/') + name);
        checksums.remove(uuid.toUpper() + QLatin1Char('/') + name);
    };
    // Decides whether one file must be written: what this program has
    // changed since the last write. Files changed in Scrivener meanwhile stay.
    // 0.64: a file changed both here and in the .scriv (Scrivener, or this program on another
    // computer through a sync program): the other version is kept before this one is written
    // (a copy in the working copy's settings/conflicts and, for a text, a snapshot)
    auto keepTheirs = [&](const QString &ourKey, const QString &theirPath) -> bool {
        const QString id = ourKey.section(QLatin1Char('/'), 0, 0);
        const QString part = ourKey.section(QLatin1Char('/'), 1);
        BinderItem *it = p->item(id);
        const QString title = it ? it->title : id;
        const QString copy = syncsafe::keepCopy(theirPath, p->settingsDir(), QStringLiteral("%1 %2").arg(title, part));
        bool snap = false;
        if (part == QLatin1String("text") && it && it->hasText()) {
            QFile f(theirPath);
            if (f.open(QIODevice::ReadOnly)) {
                QTextDocument other;
                p->docs()->deserialize(id, f.readAll(), &other, true);
                const QString when = QFileInfo(theirPath).lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
                snap = snapshots::take(p, id, QObject::tr("Other version (changed elsewhere, %1)").arg(when), nullptr, &other);
            }
        }
        // 0.64.1: nothing kept: this version must not replace that one
        if (copy.isEmpty() && !snap) {
            rep.keepFailed << QStringLiteral("%1 (%2)").arg(title, part);
            return false;
        }
        rep.conflicts << title;
        emit p->conflictKept(id, part, copy, snap);
        return true;
    };
    auto mustWrite = [&](const QString &ourKey, const QString &ourHash, const QString &theirKey,
                         const QString &theirPath) -> bool {
        const bool ourSame = state.ours.contains(ourKey) && state.ours.value(ourKey) == ourHash;
        if (quick && ourSame)
            return false; // nothing of ours to write: Scrivener's file is not even read
        const QString theirNow = sha1File(theirPath);
        const bool theirSame = state.theirs.value(theirKey) == theirNow;
        if (ourSame && theirSame)
            return false;
        if (ourSame && !theirSame) {
            ++rep.keptTheirs;
            return false;
        }
        if (!theirSame && !fresh && state.ours.contains(ourKey) && QFileInfo::exists(theirPath) && !keepTheirs(ourKey, theirPath))
            return false; // kept for the next write; the sync reports it
        return true;
    };

    laps.lap("start");
    // Scrivener's styles (0.81.3): the marks of every text written, styles made here added to Files/styles.xml
    ScrivStyles scrivStyles;
    scrivStyles.load(scrivDir, p->formatting);
    QSet<QString> present;
    for (BinderItem *it : p->allItems()) {
        const QString uuid = uuidFor(it->id);
        present.insert(uuid);
        const QString dir = data + uuid + QLatin1Char('/');
        bool any = false;
        if (it->hasText() || isPlaceholder(it)) {
            // text
            const QString ourKey = it->id + QLatin1String("/text");
            const QString textFile = p->docs()->textPath(it->id);
            QString ourHash = isPlaceholder(it) ? statHash(p->mediaFilePath(it))
                              : quick                       ? knownSha1File(textFile)
                                                            : sha1File(textFile);
            if (mustWrite(ourKey, ourHash, uuid + QLatin1String("/content.rtf"), dir + QLatin1String("content.rtf"))) {
                QList<Note> notes;
                QStringList styleIds;
                QByteArray rtfBytes;
                bool empty = true;
                if (isPlaceholder(it)) {
                    rtfBytes = placeholderRtf(it, p->formatting);
                    empty = false;
                } else {
                    p->docs()->editStored(it->id, false, [&](QTextDocument *d) {
                        empty = d->isEmpty();
                        if (!empty)
                            rtfBytes = scrivRtf(d, p->formatting, &notes, &scrivStyles, &styleIds,
                                                notesOfFile(readFileBytes(dir + QLatin1String("content.comments"))));
                        return 0;
                    });
                }
                if (empty && !QFile::exists(dir + QLatin1String("content.rtf"))) {
                    // Scrivener has no file for an empty text either
                } else {
                    if (rtfBytes.isEmpty()) {
                        QTextDocument blank;
                        rtfBytes = rtf::write(&blank, p->formatting);
                    }
                    if (!put(uuid, QStringLiteral("content.rtf"), rtfBytes))
                        return false;
                    if (!notes.isEmpty()) {
                        if (!put(uuid, QStringLiteral("content.comments"),
                                 commentsXml(notes, p->formatting, readFileBytes(dir + QLatin1String("content.comments")))))
                            return false;
                    } else {
                        drop(uuid, QStringLiteral("content.comments"));
                    }
                    // the styles the marks count in (0.81.3): their IDs, comma-separated, as Scrivener writes them
                    if (!styleIds.isEmpty()) {
                        if (!put(uuid, QStringLiteral("content.styles"), styleIds.join(QLatin1Char(',')).toUtf8()))
                            return false;
                    } else {
                        drop(uuid, QStringLiteral("content.styles"));
                    }
                    any = true;
                }
                state.ours.insert(ourKey, ourHash);
            } else {
                ++rep.unchanged;
            }
        }
        // notes (0.81.3: of every document; before only of texts, so the notes of a picture, a PDF, audio or video
        // did not reach the .scriv)
        if (!it->isRootFolder() || it->hasText()) {
            const QString notesKey = it->id + QLatin1String("/notes");
            const QString notesPath = p->docs()->notesPath(it->id);
            const QString notesHash = quick ? knownSha1File(notesPath) : sha1File(notesPath);
            if (mustWrite(notesKey, notesHash, uuid + QLatin1String("/notes.rtf"), dir + QLatin1String("notes.rtf"))) {
                QByteArray nb;
                bool empty = true;
                p->docs()->editStored(it->id, true, [&](QTextDocument *d) {
                    empty = d->isEmpty();
                    if (!empty)
                        nb = scrivRtf(d, p->formatting, nullptr);
                    return 0;
                });
                if (empty)
                    drop(uuid, QStringLiteral("notes.rtf"));
                else if (!put(uuid, QStringLiteral("notes.rtf"), nb))
                    return false;
                state.ours.insert(notesKey, notesHash);
            }
        }
        // synopsis
        {
            const QString key = it->id + QLatin1String("/synopsis");
            const QString h = sha1(it->synopsis.toUtf8());
            if (mustWrite(key, h, uuid + QLatin1String("/synopsis.txt"), dir + QLatin1String("synopsis.txt"))) {
                if (it->synopsis.isEmpty())
                    drop(uuid, QStringLiteral("synopsis.txt"));
                else if (!put(uuid, QStringLiteral("synopsis.txt"), it->synopsis.toUtf8()))
                    return false;
                state.ours.insert(key, h);
            }
        }
        // the picture of an index card (0.90.0), with the full write: "card-image.<ext>" beside the text's files
        if (!quick && (it->hasText() || it->type == ItemType::Folder) && !it->synopsisImage.isEmpty()) {
            const QString src = p->contentDir(it->id) + QLatin1Char('/') + it->synopsisImage;
            const QString ext = QFileInfo(src).suffix().toLower();
            const QString target = dir + QLatin1String("card-image.") + ext;
            if (QFile::exists(src) && readFileBytes(src) != readFileBytes(target)) {
                QDir().mkpath(dir);
                QString why;
                if (!safesave::copyFile(src, target, &why)) {
                    if (error)
                        *error = QObject::tr("Cannot copy %1 into the Scrivener project: %2").arg(src, why);
                    return false;
                }
                ++rep.written;
                any = true;
            }
            // a picture of another kind before it goes
            for (const QFileInfo &fi : QDir(dir).entryInfoList({QStringLiteral("card-image.*")}, QDir::Files))
                if (fi.absoluteFilePath() != QFileInfo(target).absoluteFilePath() && QFile::exists(target))
                    QFile::remove(fi.absoluteFilePath());
        }
        // the file of a picture, PDF, audio or video document (a quick write leaves it for the next full one)
        if (!quick && isMediaType(it->type)) {
            const QString src = p->mediaFilePath(it);
            const QString key = it->id + QLatin1String("/file");
            const QString h = statHash(src);
            const QString target = dir + QLatin1String("content.") + scrivExt(it);
            const QString theirKey = uuid + QLatin1String("/content.") + scrivExt(it);
            const bool ourSame = state.ours.value(key) == h && !h.isEmpty();
            const bool theirThere = QFile::exists(target);
            const bool theirKnown = state.theirs.contains(theirKey) && !state.theirs.value(theirKey).isEmpty();
            const bool theirSame = state.theirs.value(theirKey) == statHash(target) && theirThere;
            if (ourSame && !theirSame && theirKnown && theirThere) {
                // 0.65.0 (audit F05): changed only in the .scriv (Scrivener, or this program on another computer):
                // it stays, as a text does; the next opening reads it
                ++rep.keptTheirs;
            } else if (!(ourSame && theirSame) && QFile::exists(src)) {
                // changed on both sides: the other version is kept first (a copy in settings/conflicts)
                const bool mayWrite = ourSame || theirSame || !theirKnown || !theirThere || fresh || keepTheirs(key, target);
                if (mayWrite) {
                    QDir().mkpath(dir);
                    if (it->type == ItemType::WebPage && !isArchived(it)) {
                        // 0.81.3: this program's page (HTML with folders) packed into one file, as Scrivener keeps it
                        const QByteArray mht = webarchive::pack(src, it->sourceUrl);
                        if (mht.isEmpty()) {
                            if (error)
                                *error = QObject::tr("Cannot read %1").arg(src);
                            return false;
                        }
                        if (!writeFile(target, mht, error))
                            return false;
                    } else {
                    // 0.65.0 (audit F05): copied beside the target, then renamed over it (before: deleted first, so a
                    // failed copy left the .scriv without the file)
                    QString why;
                    if (!safesave::copyFile(src, target, &why)) {
                        if (error)
                            *error = QObject::tr("Cannot copy %1 into the Scrivener project: %2").arg(src, why);
                        return false;
                    }
                    }
                    ++rep.written;
                    state.ours.insert(key, h);
                    state.theirs.insert(theirKey, statHash(target));
                    any = true;
                }
            }
            // 0.81.3: what versions before wrote for an audio, video or web page document, which Scrivener does not
            // know: the stand-in text that named the file, and a page's HTML file with its folder
            if (!it->hasText()) {
                const QString standIn = dir + QLatin1String("content.rtf");
                if (QFile::exists(standIn) && readFileBytes(standIn).contains(QByteArray("Files/Data/") + uuid.toUtf8()))
                    drop(uuid, QStringLiteral("content.rtf"));
            }
            if (it->type == ItemType::WebPage && QFile::exists(target)) {
                for (const QString &old : {QStringLiteral("content.html"), QStringLiteral("content.htm")})
                    drop(uuid, old);
                if (QDir(dir + QLatin1String("page-files")).exists())
                    pathsafety::removeTreeInside(dir + QLatin1String("page-files"), scrivDir);
            }
        }
        // the mind map of this program (0.17): beside Scrivener's files of the document
        if (!quick) {
            const QString src = mindmap::pathFor(p->contentDir(it->id));
            const QString key = it->id + QLatin1String("/mindmap");
            const QString theirKey = uuid + QLatin1Char('/') + mindmap::scrivenerFileName();
            const QString target = dir + mindmap::scrivenerFileName();
            const bool theirThere = QFile::exists(target);
            const bool theirKnown = state.theirs.contains(theirKey);
            const bool theirSame = state.theirs.value(theirKey) == sha1File(target);
            if (QFile::exists(src)) {
                const QString h = sha1File(src);
                const bool ourSame = state.ours.value(key) == h;
                if (ourSame && !theirSame && theirKnown && theirThere) {
                    ++rep.keptTheirs; // 0.65.0 (found while checking audit F25): changed only in the .scriv: it stays
                } else if (!ourSame || !theirSame) {
                    const bool mayWrite = ourSame || theirSame || !theirKnown || !theirThere || keepTheirs(key, target);
                    if (mayWrite) {
                        // 0.65.0 (audit F25): a board that cannot be read or written fails the sync (it was passed over)
                        QFile f(src);
                        if (!f.open(QIODevice::ReadOnly)) {
                            if (error)
                                *error = QObject::tr("Cannot read %1").arg(src);
                            return false;
                        }
                        if (!writeFile(target, f.readAll(), error))
                            return false;
                        ++rep.written;
                        state.ours.insert(key, h);
                        state.theirs.insert(theirKey, sha1File(target));
                    }
                }
            } else if (state.ours.contains(key)) {
                // the board was emptied here; a board changed in the .scriv meanwhile is kept first
                if (theirThere && theirKnown && !theirSame && !keepTheirs(key, target)) {
                    ; // not kept: not removed either
                } else {
                    QFile::remove(target);
                    state.ours.remove(key);
                    state.theirs.remove(theirKey);
                }
            }
        }
        Q_UNUSED(any);
    }
    // styles made here that a written text uses (0.81.3): into Files/styles.xml
    if (scrivStyles.changed) {
        if (!writeFile(scrivStyles.path, scrivStyles.dom.toByteArray(4), error))
            return false;
        touched.insert(scrivStyles.path);
    }

    laps.lap("items");
    // own icons of the project in use: the Icons folder of the .scriv (a guess
    // of where Scrivener keeps the icons of a project)
    if (!quick) {
        QSet<QString> used;
        for (BinderItem *it : p->allItems())
            if (icons::isCustom(it->icon))
                used.insert(icons::customName(it->icon));
        for (const QString &n : std::as_const(used)) {
            const QString src = icons::customPath(p, n);
            const QString target = scrivDir + QLatin1String("/Icons/") + n + QLatin1String(".png");
            if (QFile::exists(src) && sha1File(src) != sha1File(target)) {
                QDir().mkpath(scrivDir + QLatin1String("/Icons"));
                // 0.65.0 (audit F25): replaced in one step, and a failure fails the sync (it was not looked at)
                QString why;
                if (!safesave::copyFile(src, target, &why)) {
                    if (error)
                        *error = QObject::tr("Cannot copy the icon %1 into the Scrivener project: %2").arg(n, why);
                    return false;
                }
            }
        }
    }

    // documents that are no longer there (emptied trash): their folders go,
    // as far as they belong to documents of this project or of the old binder
    // 0.65.0 (external audit of 2026-11-10, F10): only documents this program knew at its last reading or writing of
    // the .scriv count as deleted here. Before, every document of the .scrivx on disk that was not in this
    // program's binder had its folder deleted: documents added in Scrivener on another computer (through a sync
    // folder) while the project was open here were lost at the next full save. Those are kept now, and the
    // binder that names them is not replaced (below).
    // documents of the .scrivx on disk that this program never knew: made elsewhere
    auto madeElsewhere = [&]() {
        QSet<QString> known;
        for (auto it = state.theirs.cbegin(); it != state.theirs.cend(); ++it)
            known.insert(it.key().section(QLatin1Char('/'), 0, 0).toUpper());
        for (const QString &u : std::as_const(state.binder))
            known.insert(u.toUpper());
        QStringList out;
        QDomDocument d;
        if (!existing.isEmpty() && d.setContent(existing)) {
            const QDomNodeList items = d.elementsByTagName(QStringLiteral("BinderItem"));
            for (int i = 0; i < items.size(); ++i) {
                const QString uuid = items.at(i).toElement().attribute(QStringLiteral("UUID")).toUpper();
                if (!uuid.isEmpty() && !present.contains(uuid) && !known.contains(uuid))
                    out << uuid;
            }
        }
        return out;
    };
    if (!quick) {
        QSet<QString> known;
        for (auto it = state.theirs.cbegin(); it != state.theirs.cend(); ++it)
            known.insert(it.key().section(QLatin1Char('/'), 0, 0));
        for (const QString &uuid : std::as_const(known)) {
            if (uuid.isEmpty() || present.contains(uuid))
                continue;
            const QString dir = data + uuid;
            if (QDir(dir).exists()) {
                QDir(dir).removeRecursively();
                ++rep.removed;
                textChanged = true;
            }
            for (auto it = checksums.begin(); it != checksums.end();)
                it = it.key().startsWith(uuid.toUpper() + QLatin1Char('/')) ? checksums.erase(it) : std::next(it);
            checksumGroups.remove(uuid.toUpper());
            for (auto it = state.theirs.begin(); it != state.theirs.end();)
                it = it.key().startsWith(uuid + QLatin1Char('/')) ? state.theirs.erase(it) : std::next(it);
            QDir(scrivDir + QLatin1String("/Snapshots/") + uuid + QLatin1String(".snapshots")).removeRecursively();
        }
        for (auto it = state.ours.begin(); it != state.ours.end();)
            it = p->item(it.key().section(QLatin1Char('/'), 0, 0)) ? std::next(it) : state.ours.erase(it);
    }

    laps.lap("icons+removed");
    // snapshots deleted in this program (one in the inspector, or Project > Delete Old Snapshots)
    // since they came from the .scriv or went there: out of the .scriv too (0.61.1). Only those
    // the sync state knows are taken out; a snapshot Scrivener has that was never read stays.
    if (!quick && !state.snapshots.isEmpty()) {
        QHash<QString, BinderItem *> byUuid;
        for (BinderItem *it : p->allItems())
            if (it->hasText())
                byUuid.insert(uuidFor(it->id), it);
        QHash<QString, QList<qint64>> gone; // uuid -> moments (seconds since the epoch) deleted here
        QHash<QString, QSet<QString>> current;
        QStringList keep;
        for (const QString &key : std::as_const(state.snapshots)) {
            const QString uuid = key.section(QLatin1Char('/'), 0, 0);
            const QString name = key.section(QLatin1Char('/'), 1);
            if (!current.contains(uuid)) {
                QSet<QString> names;
                if (BinderItem *it = byUuid.value(uuid))
                    for (const Snapshot &s : snapshots::list(p, it->id))
                        names.insert(snapshotFileName(s.date));
                current.insert(uuid, names);
            }
            if (current.value(uuid).contains(name) || !byUuid.contains(uuid)) {
                keep << key;   // still here (a removed document's folder went above)
                continue;
            }
            const QDateTime when = snapshotMoment(name);
            if (when.isValid())
                gone[uuid] << when.toSecsSinceEpoch();
            else
                keep << key;
        }
        for (auto g = gone.cbegin(); g != gone.cend(); ++g) {
            const QString sdir = scrivDir + QLatin1String("/Snapshots/") + g.key() + QLatin1String(".snapshots/");
            auto isGone = [&g](const QDateTime &d) {
                if (!d.isValid())
                    return false;
                for (const qint64 t : g.value())
                    if (d.toSecsSinceEpoch() == t)   // to the second: two snapshots may be one second apart
                        return true;
                return false;
            };
            int left = 0;
            for (const QFileInfo &fi : QDir(sdir).entryInfoList({QStringLiteral("*.rtf")}, QDir::Files)) {
                QDateTime d = snapshotMoment(fi.completeBaseName());
                if (!d.isValid())
                    d = fi.lastModified(); // as the import dates a file whose name is no date
                if (isGone(d)) {
                    QFile::remove(fi.absoluteFilePath());
                    touched.insert(fi.absoluteFilePath());
                    ++rep.snapshotsRemoved;
                } else {
                    ++left;
                }
            }
            if (left == 0) {
                QDir(sdir).removeRecursively();
                continue;
            }
            QFile f(sdir + QLatin1String("index.xml"));
            QDomDocument d;
            if (!f.open(QIODevice::ReadOnly) || !d.setContent(&f))
                continue;
            f.close();
            const QDomNodeList ns = d.elementsByTagName(QStringLiteral("Snapshot"));
            for (int i = ns.size() - 1; i >= 0; --i) {
                QDomElement e = ns.at(i).toElement();
                if (isGone(snapshotMoment(e.firstChildElement(QStringLiteral("Date")).text().trimmed())))
                    e.parentNode().removeChild(e);
            }
            if (!writeFile(sdir + QLatin1String("index.xml"), d.toByteArray(4), error))
                return false;
        }
        state.snapshots = keep;
    }
    laps.lap("snapshots removed");
    // snapshots of this program that Scrivener does not have yet
    for (BinderItem *it : quick ? QList<BinderItem *>() : p->allItems()) {
        if (!it->hasText() || !it->hasSnapshots)
            continue;
        const QString uuid = uuidFor(it->id);
        const QString sdir = scrivDir + QLatin1String("/Snapshots/") + uuid + QLatin1String(".snapshots/");
        const QList<Snapshot> list = snapshots::list(p, it->id);
        bool added = false;
        // index.xml: what Scrivener has, plus ours
        QList<QPair<QString, QString>> entries; // date text, title
        {
            QFile f(sdir + QLatin1String("index.xml"));
            QDomDocument d;
            if (f.open(QIODevice::ReadOnly) && d.setContent(&f)) {
                const QDomNodeList ns = d.elementsByTagName(QStringLiteral("Snapshot"));
                for (int i = 0; i < ns.size(); ++i) {
                    const QDomElement s = ns.at(i).toElement();
                    entries.append({s.firstChildElement(QStringLiteral("Date")).text(),
                                    s.firstChildElement(QStringLiteral("Title")).text()});
                }
            }
        }
        for (const Snapshot &s : list) {
            const QString name = snapshotFileName(s.date);
            const QString key = uuid + QLatin1Char('/') + name;
            if (state.snapshots.contains(key) || QFile::exists(sdir + name))
                continue;
            QTextDocument doc;
            snapshots::load(p, s, &doc);
            if (!writeFile(sdir + name, scrivRtf(&doc, p->formatting, nullptr), error))
                return false;
            state.snapshots << key;
            entries.append({dateText(s.date), s.title.isEmpty() ? QObject::tr("Snapshot") : s.title});
            ++rep.snapshots;
            added = true;
        }
        if (added) {
            std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
                return QDateTime::fromString(a.first.left(19), QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                       < QDateTime::fromString(b.first.left(19), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            });
            QString x = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Snapshots Version=\"1.0\">\n");
            for (const auto &e : entries)
                x += QStringLiteral("    <Snapshot>\n        <Title>%1</Title>\n        <Date>%2</Date>\n    </Snapshot>\n")
                         .arg(xmlEscape(e.second), xmlEscape(e.first));
            x += QStringLiteral("</Snapshots>\n");
            if (!writeFile(sdir + QLatin1String("index.xml"), x.toUtf8(), error))
                return false;
        }
    }

    laps.lap("snapshots");
    // the project file (a quick write: only when the binder or the settings changed)
    const bool ownScriv = QFileInfo(p->scrivenerPath).absoluteFilePath() == scrivDir;
    QString scrivxSha;
    if (!quick || fresh || !ownScriv || p->binderChangedSinceSync) {
        const QByteArray scrivxBytes = text::xmlSafeBytes(buildScrivx(p, existing, state.identifier, data));
        // 0.64: the binder was written elsewhere since this program read or wrote it: kept first
        const QString stampNow = scrivxStamp(scrivx);
        bool binderKept = false;
        const bool binderElsewhere = !fresh && state.theirs.contains(QStringLiteral(".scrivx"))
                                     && state.theirs.value(QStringLiteral(".scrivx")) != stampNow && QFileInfo::exists(scrivx);
        const QStringList addedElsewhere = binderElsewhere ? madeElsewhere() : QStringList();
        if (binderElsewhere && !addedElsewhere.isEmpty()) {
            // 0.65.0 (audit F10): the binder on disk names documents made elsewhere: replacing it would drop them.
            // It stays; the next opening reads it and keeps what was made here (mergeFromOld)
            binderKept = true;
            ++rep.keptTheirs;
            if (p->scrivenerBinderTold != stampNow) {
                p->scrivenerBinderTold = stampNow;
                emit p->conflictKept(QString(),
                                     QObject::tr("the binder of the Scrivener project: %n document(s) were added to it "
                                                 "elsewhere while it was open here. It was not replaced, so they are kept; "
                                                 "close the project and open it again to see them with your changes",
                                                 nullptr, int(addedElsewhere.size())),
                                     QString(), false);
            }
        } else if (binderElsewhere) {
            const QString copy = syncsafe::keepCopy(scrivx, p->settingsDir(), QStringLiteral("binder (scrivx)"));
            if (copy.isEmpty()) {
                // 0.64.1: not kept, so not replaced; the binder waits for the next write
                rep.keepFailed << QFileInfo(scrivx).fileName();
            } else {
                rep.conflicts << QFileInfo(scrivx).fileName();
                emit p->conflictKept(QString(), QStringLiteral("scrivx"), copy, false);
            }
        }
        // the same binder as in the file (only the time and the change id would differ): not written,
        // so a full save without changes leaves the .scriv as it is (0.64)
        if (binderKept || rep.keepFailed.contains(QFileInfo(scrivx).fileName())) {
            ; // not written (above)
        } else if (fresh || !sameExceptStamps(existing, scrivxBytes)) {
            if (!writeFile(scrivx, scrivxBytes, error))
                return false;
            scrivxSha = sha1(scrivxBytes);
            // Scrivener for Windows' copy of the binder (a ZIP of the .scrivx it saved last) is older than this one now.
            // 0.81.3: only then (it was removed at every write, also when nothing was written)
            QFile::remove(scrivDir + QLatin1String("/Files/binder.autosave"));
        }
        if (!binderKept && !rep.keepFailed.contains(QFileInfo(scrivx).fileName())) {
            state.theirs.insert(QStringLiteral(".scrivx"), scrivxStamp(scrivx));
            state.binder = QStringList(present.cbegin(), present.cend());
            state.binderKnown = true;
            state.meta = kMetaLevel;   // keywords, custom metadata, collections (0.81.2), bookmarks and targets (0.90.0)
            if (ownScriv)
                p->binderChangedSinceSync = false;
        }
    }
    if (!QFile::exists(scrivDir + QLatin1String("/Files/version.txt")))
        writeFile(scrivDir + QLatin1String("/Files/version.txt"), "23", nullptr);
    if (fresh && !QFile::exists(scrivDir + QLatin1String("/Settings/projectpreferences.xml")))
        writeFile(scrivDir + QLatin1String("/Settings/projectpreferences.xml"), projectPreferences(p), nullptr);
    // the compile metadata (0.90.0), with the full write: in Scrivener's compile settings, only where they differ
    if (!quick) {
        const QString cfile = scrivDir + QLatin1String("/Settings/compile.xml");
        const QByteArray was = readFileBytes(cfile);
        QDomDocument cd;
        if (!was.isEmpty() && cd.setContent(was)) {
            QDomElement md = cd.documentElement().firstChildElement(QStringLiteral("ProjectSettings")).firstChildElement(QStringLiteral("MetaData"));
            bool changed = false;
            auto put = [&](const char *name, const QString &value) {
                QDomElement e = md.firstChildElement(QLatin1String(name));
                if (e.isNull() || e.text().trimmed() == value)
                    return;
                setText(e, value);
                changed = true;
            };
            if (!md.isNull()) {
                const QString title = md.firstChildElement(QStringLiteral("ProjectTitle")).text().trimmed();
                if (!p->compile.titleOverride.isEmpty() && !title.contains(QLatin1String("<$")))
                    put("ProjectTitle", p->compile.titleOverride);
                const QString author = p->compile.authorOverride.trimmed();
                const QString fileAuthor = (md.firstChildElement(QStringLiteral("Forename")).text().trimmed() + QLatin1Char(' ')
                                            + md.firstChildElement(QStringLiteral("Surname")).text().trimmed()).trimmed();
                if (!author.isEmpty() && author != fileAuthor && !fileAuthor.contains(QLatin1String("<$"))) {
                    const int cut = author.lastIndexOf(QLatin1Char(' '));
                    put("Forename", cut > 0 ? author.left(cut) : QString());
                    put("Surname", cut > 0 ? author.mid(cut + 1) : author);
                }
                if (!p->compile.language.isEmpty())
                    put("EbookLanguage", p->compile.language);
            }
            if (changed && !writeFile(cfile, cd.toByteArray(4), error))
                return false;
        }
    }
    // the writing history (0.90.0), with the full write; a day without any writing is not added (opening a project on
    // a new day and closing it again changes nothing). Only into a .scriv that keeps one (Scrivener for Windows does;
    // a project of Scrivener for Mac may keep its history elsewhere) or a new one
    if (!quick && (fresh || QFile::exists(scrivDir + QLatin1String("/Files/writing.history")))) {
        const QString hfile = scrivDir + QLatin1String("/Files/writing.history");
        const QByteArray had = readFileBytes(hfile);
        QList<WritingDay> days;
        for (const WritingDay &w : p->history)
            if (w.draftWords || w.draftChars || w.otherWords || w.otherChars || had.contains(w.date.toString(QStringLiteral("yyyy-MM-dd")).toLatin1()))
                days << w;
        const QByteArray hist = writingHistory(days, had);
        if (!hist.isEmpty() && !writeFile(hfile, hist, error))
            return false;
    }
    laps.lap("scrivx");
    // the search index holds every text: a quick write leaves it for the next full one
    if (quick) {
        state.indexStale = state.indexStale || textChanged;
    } else if (textChanged || state.indexStale || (fresh && !QFile::exists(scrivDir + QLatin1String("/Files/search.indexes")))) {
        // 0.64.2: a missing index is made only with a change or for a new .scriv (Scrivener makes its own):
        // a .scriv opened and closed without a change stays as it was
        // 0.64.2: a failed write is reported (as for docs.checksum): on a full disk the save looked fine when the
        // texts had gone out with an earlier automatic save and only the index was left (S-D06)
        if (!writeFile(scrivDir + QLatin1String("/Files/search.indexes"), searchIndexes(p), error)) {
            state.indexStale = true;
            return false;
        }
        state.indexStale = false;
    }
    laps.lap("search index");
    if ((hadChecksums || fresh) && (!quick || textChanged)) {
        QByteArray cs;
        QMap<QString, QStringList> byGroup;   // sorted as Scrivener sorts: by UUID, then by name
        for (auto it = checksums.cbegin(); it != checksums.cend(); ++it)
            byGroup[it.key().section(QLatin1Char('/'), 0, 0)] << it.key();
        for (const QString &g : std::as_const(checksumGroups))
            byGroup[g];
        for (auto g = byGroup.cbegin(); g != byGroup.cend(); ++g) {
            const QString uuid = checksumUpper ? g.key() : g.key().toLower();
            if (checksumGroups.contains(g.key()))
                cs += uuid.toUtf8() + '\n';
            QStringList keys = g.value();
            keys.sort();
            for (const QString &k : std::as_const(keys))
                cs += uuid.toUtf8() + '/' + k.section(QLatin1Char('/'), 1).toUtf8() + '=' + checksums.value(k).toUtf8() + '\n';
        }
        QFile old(checksumFile);
        const bool same = old.open(QIODevice::ReadOnly) && old.readAll() == cs;
        old.close();
        // 0.64.1: a failed write is reported (the system test's C-34: a disk error here was passed over)
        if (!same && !writeFile(checksumFile, cs, error))
            return false;
    }
    laps.lap("checksums");
    QString fp = quick ? updatedFingerprint(scrivDir, touched, scrivxSha) : QString();
    if (fp.isEmpty())
        fp = fingerprint(scrivDir);
    // 0.64.2: files changed in the .scriv while the project was open stay there (keptTheirs); a fingerprint
    // taken now would cover them, so the next opening would not read them and the program kept showing the
    // old text (S-D02). Without a fingerprint the next opening reads the .scriv again and takes them.
    // A quick write does not look at Scrivener's files: it keeps an empty fingerprint empty.
    if (rep.keptTheirs > 0 || (quick && state.fingerprint.isEmpty()))
        fp.clear();
    state.fingerprint = fp;
    laps.lap("fingerprint");
    laps.print(quick ? "scrivener write (quick)" : "scrivener write");
    return true;
}

// ---------------------------------------------------------------- open, sync, convert

static QString keyFor(const QString &identifier, const QString &scrivDir)
{
    QString id = identifier;
    id.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9-]")));
    if (id.isEmpty())
        id = QStringLiteral("project");
    return id + QLatin1Char('-') + sha1(QFileInfo(scrivDir).absoluteFilePath().toUtf8()).left(8);
}

static QString projectIn(const QString &dir)
{
    const QStringList l = QDir(dir).entryList({QStringLiteral("*" QB_PROJECT_SUFFIX)}, QDir::Dirs | QDir::NoDotAndDotDot);
    return l.isEmpty() ? QString() : QDir(dir).filePath(l.first());
}

static QString baseName(const QString &scrivDir)
{
    QString n = QFileInfo(scrivDir).fileName();
    if (n.endsWith(QLatin1String(".scriv"), Qt::CaseInsensitive))
        n.chop(6);
    return n;
}

// Records what the .scriv holds now as "written by us", so that the next write
// only touches what is changed afterwards.
static void recordAll(Project *p, const QString &scrivDir, SyncState &st)
{
    const QString data = scrivDir + QLatin1String("/Files/Data/");
    st.ours.clear();
    st.theirs.clear();
    for (BinderItem *it : p->allItems()) {
        const QString uuid = uuidFor(it->id);
        for (const QString &name : {QStringLiteral("content.rtf"), QStringLiteral("notes.rtf"), QStringLiteral("synopsis.txt"),
                                    QStringLiteral("content.comments")})
            st.theirs.insert(uuid + QLatin1Char('/') + name, sha1File(data + uuid + QLatin1Char('/') + name));
        st.ours.insert(it->id + QLatin1String("/text"), isPlaceholder(it) ? statHash(p->mediaFilePath(it))
                                                                                    : sha1File(p->docs()->textPath(it->id)));
        st.ours.insert(it->id + QLatin1String("/notes"), sha1File(p->docs()->notesPath(it->id)));
        st.ours.insert(it->id + QLatin1String("/synopsis"), sha1(it->synopsis.toUtf8()));
        if (isMediaType(it->type)) {
            st.ours.insert(it->id + QLatin1String("/file"), statHash(p->mediaFilePath(it)));
            const QString ext = scrivExt(it);
            st.theirs.insert(uuid + QLatin1String("/content.") + ext, statHash(data + uuid + QLatin1String("/content.") + ext));
        }
    }
    // the snapshots came from the .scriv (or went there before)
    st.snapshots.clear();
    for (BinderItem *it : p->allItems())
        if (it->hasText() && it->hasSnapshots)
            for (const Snapshot &s : snapshots::list(p, it->id))
                st.snapshots << uuidFor(it->id) + QLatin1Char('/') + snapshotFileName(s.date);
    st.theirs.insert(QStringLiteral(".scrivx"), scrivxStamp(scrivxIn(scrivDir)));
    st.binder.clear();
    for (BinderItem *it : p->allItems())
        if (!it->isRootFolder())
            st.binder << uuidFor(it->id);
    st.binderKnown = true;
    st.fingerprint = fingerprint(scrivDir);
}

// Keeps what only this program knows from the working copy of the last time.
// Keywords, custom metadata fields and collections of the working copy (op) matched with those just read
// from the .scriv (np), 0.81.2. join: the working copy is of a version before 0.81.2, which never wrote them
// into the .scriv: what only the working copy has is added to np (once; *added tells). Otherwise the .scriv
// holds everything written here, so np's are the ones that count; ours are only matched, for documents of
// the working copy that never reached the .scriv. This program's search collections are never in the .scriv:
// they always come along.
struct MetaMaps {
    QHash<QString, QString> keyword, field;   // op id -> np id
};

static MetaMaps matchMeta(Project *np, Project *op, bool join, bool *added, bool joinSearches = false)
{
    MetaMaps m;
    // keywords, parents first
    std::function<QString(const Keyword &, int)> mapKw = [&](const Keyword &k, int depth) -> QString {
        if (m.keyword.contains(k.id))
            return m.keyword.value(k.id);
        QString parent;
        if (!k.parentId.isEmpty() && depth < 64)
            if (const Keyword *pk = op->keywordById(k.parentId))
                parent = mapKw(*pk, depth + 1);
        QString to;
        if (const Keyword *same = np->keywordById(k.id); same && same->name == k.name)
            to = same->id;
        for (int pass = 0; pass < 2 && to.isEmpty(); ++pass)
            for (const Keyword &n : std::as_const(np->keywords))
                if ((pass == 0 ? n.name == k.name : n.name.compare(k.name, Qt::CaseInsensitive) == 0) && n.parentId == parent
                    && std::find(m.keyword.cbegin(), m.keyword.cend(), n.id) == m.keyword.cend()) {
                    to = n.id;
                    break;
                }
        if (to.isEmpty() && join) {
            Keyword copy = k;
            if (np->keywordById(copy.id))
                copy.id = newId();
            copy.parentId = parent;
            np->keywords << copy;
            to = copy.id;
            *added = true;
        }
        m.keyword.insert(k.id, to);
        return to;
    };
    for (const Keyword &k : std::as_const(op->keywords))
        mapKw(k, 0);
    // custom metadata fields
    for (const CustomField &f : std::as_const(op->customFields)) {
        QString to;
        if (const CustomField *same = np->customFieldById(f.id); same && same->type == f.type)
            to = same->id;
        for (int pass = 0; pass < 2 && to.isEmpty(); ++pass)
            for (const CustomField &n : std::as_const(np->customFields))
                if (n.type == f.type && (pass == 0 ? n.title == f.title : n.title.compare(f.title, Qt::CaseInsensitive) == 0)
                    && std::find(m.field.cbegin(), m.field.cend(), n.id) == m.field.cend()) {
                    to = n.id;
                    break;
                }
        if (to.isEmpty() && join) {
            CustomField copy = f;
            if (np->customFieldById(copy.id))
                copy.id = newId();
            np->customFields << copy;
            to = copy.id;
            *added = true;
        }
        m.field.insert(f.id, to);
    }
    // collections
    for (const Collection &c : std::as_const(op->collections)) {
        if (c.isSearch) {
            // 0.90.0: the .scriv holds the saved searches; a working copy of an older version had them alone (joined once)
            if (joinSearches && std::none_of(np->collections.cbegin(), np->collections.cend(), [&](const Collection &x) {
                    return x.id == c.id || (x.isSearch && x.name.compare(c.name, Qt::CaseInsensitive) == 0);
                })) {
                np->collections << c;
                *added = true;
            }
            continue;
        }
        if (!join)
            continue;
        auto same = std::find_if(np->collections.begin(), np->collections.end(), [&](const Collection &x) {
            return !x.isSearch && (x.id == c.id || x.name.compare(c.name, Qt::CaseInsensitive) == 0);
        });
        if (same == np->collections.end()) {
            Collection copy = c;
            copy.items.clear();
            for (const QString &id : c.items)
                if (np->item(id))
                    copy.items << id;
            if (std::any_of(np->collections.cbegin(), np->collections.cend(), [&](const Collection &x) { return x.id == copy.id; }))
                copy.id = newId();
            np->collections << copy;
            *added = true;
        } else {
            for (const QString &id : c.items)
                if (np->item(id) && !same->items.contains(id)) {
                    same->items << id;
                    *added = true;
                }
        }
    }
    return m;
}

// A value of the working copy in np's field (versions before 0.81.2 kept Scrivener's own text)
// What of a working copy has reached the .scriv, as a level kept in its state (SyncState::meta): 1 = keywords, custom
// metadata and collections (0.81.2); 2 = also the bookmarks of documents, the project's bookmarks to outside addresses,
// document targets and the deadline (0.90.0). A state below it is read again once and joined with the .scriv.
static bool takeValue(const Project *np, const QString &fieldId, const QString &v, QString *out)
{
    const CustomField *f = np->customFieldById(fieldId);
    if (!f)
        return false;
    *out = valueFromScrivener(f->type, v, {});
    if (f->type == CustomFieldType::List && !out->isEmpty() && !f->options.contains(*out)) {
        if (!f->options.contains(v))
            return false;   // an option ID of the old form: its text is not known here
        *out = v;
    }
    return true;
}

static int mergeFromOld(Project *np, Project *op, const QString &scrivDir, const SyncState &st, int *keptNew = nullptr,
                        bool *metaAdded = nullptr)
{
    const QString data = scrivDir + QLatin1String("/Files/Data/");
    int fromScriv = 0;
    // section types that went through uuidFor get their old ids back
    QHash<QString, QString> stIds;
    for (const SectionType &t : op->sectionTypes)
        stIds.insert(uuidFor(t.id), t.id);
    auto mapSt = [&](const QString &id) { return stIds.value(id.toUpper(), id); };
    for (SectionType &t : np->sectionTypes)
        t.id = mapSt(t.id);
    for (QStringList *l : {&np->structureDefaults.folders, &np->structureDefaults.fileGroups, &np->structureDefaults.files})
        for (QString &s : *l)
            s = mapSt(s);
    // keywords, custom metadata, collections (0.81.2: the .scriv holds them; a working copy of an older
    // version is joined with it once)
    const bool join = st.meta < 1;
    // 0.90.0: bookmarks of documents, bookmarks of the project to outside addresses, document targets and the
    // deadline are in the .scriv too. A working copy of an older version kept them for itself: joined once.
    const bool joinMarks = st.meta < 2;
    bool added = false;
    const MetaMaps maps = matchMeta(np, op, join, &added, joinMarks);
    auto hasMark = [](const QList<Bookmark> &list, const Bookmark &b) {
        return std::any_of(list.cbegin(), list.cend(), [&](const Bookmark &x) { return x.target == b.target; });
    };
    if (joinMarks) {
        for (const Bookmark &b : op->projectBookmarks)
            if (!b.isInternal() && !hasMark(np->projectBookmarks, b)) {
                np->projectBookmarks << b;
                added = true;
            }
        if (!np->targets.deadline.isValid() && op->targets.deadline.isValid()) {
            np->targets.deadline = op->targets.deadline;
            added = true;
        }
    }
    {
        // the working copy's formatting and styles count; styles the reader made from Scrivener's (0.81.3) that it
        // lacks are added, so the texts just read keep theirs
        const StyleSheet fromScriv = styles::sheet(np->formatting);
        StyleSheet merged = styles::sheet(op->formatting);
        bool added = false;
        for (const Style &st : fromScriv.styles)
            if (merged.indexOf(st.name) < 0) {
                merged.styles.append(st);
                added = true;
            }
        np->formatting = added ? styles::withSheet(op->formatting, merged) : op->formatting;
    }
    {
        // 0.90.0: the working copy's compile settings count, but the title, the author and the language of the compiled
        // file are in the .scriv's compile settings too (Scrivener may have changed them)
        const QString title = np->compile.titleOverride, author = np->compile.authorOverride, lang = np->compile.language;
        np->compile = op->compile;
        if (!title.isEmpty())
            np->compile.titleOverride = title;
        if (!author.isEmpty())
            np->compile.authorOverride = author;
        if (!lang.isEmpty())
            np->compile.language = lang;
    }
    np->uiState = op->uiState;
    np->compositionBackground = op->compositionBackground;
    // 0.90.0: the writing history is in the .scriv (Files/writing.history); a day only the working copy has is added
    // (a write that failed, or a working copy of an older version), a day both have is the .scriv's
    for (const WritingDay &w : op->history)
        if (w.date.isValid() && std::none_of(np->history.cbegin(), np->history.cend(), [&](const WritingDay &x) { return x.date == w.date; }))
            np->history << w;
    std::sort(np->history.begin(), np->history.end(), [](const WritingDay &a, const WritingDay &b) { return a.date < b.date; });
    if (np->templatesFolderId.isEmpty())
        np->templatesFolderId = op->templatesFolderId;
    if (np->notesFolderId.isEmpty())
        np->notesFolderId = op->notesFolderId;
    np->docs()->setPageSetup(op->docs()->pageSetup());

    struct TellAdded {
        bool *out;
        const bool &added;
        ~TellAdded()
        {
            if (out)
                *out = added;
        }
    } tellAdded{metaAdded, added};
    for (BinderItem *it : np->allItems()) {
        it->sectionType = mapSt(it->sectionType);
        BinderItem *old = op->item(it->id);
        if (!old)
            continue;
        const QString uuid = uuidFor(it->id);
        // unchanged since the last write or read ("" = no file then and now)
        auto same = [&](const QString &name) {
            const QString key = uuid + QLatin1Char('/') + name;
            return st.theirs.value(key) == sha1File(data + key);
        };
        const bool textSame = same(QStringLiteral("content.rtf")) && same(QStringLiteral("content.comments"));
        if (isPlaceholder(old) && textSame) {
            // the stand-in text of an audio, video or other file: the file again
            it->type = old->type;
            it->fileExt = old->fileExt;
            it->originalFileName = old->originalFileName;
            it->sourceUrl = old->sourceUrl;
            copyDir(op->contentDir(old->id), np->contentDir(it->id));
            np->docs()->removeDocument(it->id);
        } else if (it->hasText() && old->hasText()) {
            if (textSame) {
                np->docs()->setContent(it->id, op->docs()->text(old->id));
                // 0.81.2: inline notes that an older version read as text
                if (np->docs()->text(it->id)->toRawText().contains(QLatin1String("{\\Scrv_")))
                    ScrivenerImporter::convertInlineNotes(np->docs()->text(it->id));
            } else {
                ++fromScriv;
            }
            if (same(QStringLiteral("notes.rtf"))) {
                np->docs()->setNotesContent(it->id, op->docs()->notes(old->id));
                if (np->docs()->notes(it->id)->toRawText().contains(QLatin1String("{\\Scrv_")))
                    ScrivenerImporter::convertInlineNotes(np->docs()->notes(it->id));
            }
        }
        it->scriptMode = old->scriptMode && it->hasText();
        if (joinMarks) {
            for (const Bookmark &b : old->bookmarks)
                if (!hasMark(it->bookmarks, b) && (!b.isInternal() || np->item(b.internalId()))) {
                    it->bookmarks << b;
                    added = true;
                }
            if (it->target <= 0 && old->target > 0) {
                it->target = old->target;
                it->targetChars = old->targetChars;
                added = true;
            }
        }
        it->defaultTemplate = old->defaultTemplate;
        // 0.90.0: the places of cards and the corkboard's arrangement are in the .scriv; a working copy of an older
        // version had them alone
        if (joinMarks && it->corkboardPos.isEmpty() && !old->corkboardPos.isEmpty()) {
            it->corkboardPos = old->corkboardPos;
            added = true;
        }
        if (joinMarks && it->corkArrangement.isEmpty() && !old->corkArrangement.isEmpty()) {
            it->corkArrangement = old->corkArrangement;
            added = true;
        }
        if (it->icon.isEmpty())
            it->icon = old->icon;
        if (join) {
            // 0.81.2, once: what the working copy had and the .scriv has not (made here, never written)
            for (const QString &k : old->keywords) {
                const QString to = maps.keyword.value(k);
                if (!to.isEmpty() && !it->keywords.contains(to)) {
                    it->keywords << to;
                    added = true;
                }
            }
            for (auto v = old->custom.cbegin(); v != old->custom.cend(); ++v) {
                const QString to = maps.field.value(v.key());
                QString value;
                if (!to.isEmpty() && !it->custom.contains(to) && takeValue(np, to, v.value(), &value)) {
                    it->custom.insert(to, value);
                    added = true;
                }
            }
        }
        // a mind map only the working copy has (0.17)
        {
            const QString oldMap = mindmap::pathFor(op->contentDir(old->id));
            const QString newMap = mindmap::pathFor(np->contentDir(it->id));
            if (QFile::exists(oldMap) && !QFile::exists(newMap)) {
                QDir().mkpath(np->contentDir(it->id));
                QFile::copy(oldMap, newMap);
            }
        }
        if (joinMarks && !old->synopsisImage.isEmpty() && it->synopsisImage.isEmpty()) {   // 0.90.0: in the .scriv now
            it->synopsisImage = old->synopsisImage;
            QDir().mkpath(np->contentDir(it->id));
            QFile::copy(op->contentDir(old->id) + QLatin1Char('/') + old->synopsisImage,
                        np->contentDir(it->id) + QLatin1Char('/') + old->synopsisImage);
        }
    }
    // documents of the working copy that never reached the .scriv (a crash or a failed
    // save between their texts and the .scrivx; 0.20): kept at their place. Documents
    // the .scriv had once and has no longer were deleted in Scrivener and stay away.
    {
        QSet<QString> synced;
        if (st.binderKnown) {
            // 0.65.0 (audit F10): only the documents the .scriv's binder had when this program last read or wrote
            // it; a document made here while the binder could not be written (documents added elsewhere) has its
            // texts in the .scriv already, and is kept
            for (const QString &u : st.binder)
                synced.insert(u.toUpper());
        } else {
            for (auto it = st.theirs.cbegin(); it != st.theirs.cend(); ++it)
                synced.insert(it.key().section(QLatin1Char('/'), 0, 0).toUpper());
        }
        QHash<const BinderItem *, BinderItem *> placed;
        auto rootOf = [np](const BinderItem *old) -> BinderItem * {
            switch (old->type) {
            case ItemType::Draft: return np->draft();
            case ItemType::Research: return np->research();
            case ItemType::Trash: return np->trash();
            default: return nullptr;
            }
        };
        int kept = 0;
        for (BinderItem *old : op->allItems()) { // parents come before their children
            if (old->isRootFolder() || np->item(old->id) || synced.contains(uuidFor(old->id)))
                continue;
            BinderItem *parent = nullptr;
            if (old->parent) {
                parent = old->parent->isRootFolder() ? rootOf(old->parent) : np->item(old->parent->id);
                if (!parent)
                    parent = placed.value(old->parent);
            }
            if (!parent)
                parent = np->draft();
            // after the sibling it followed, if that is there
            int row = -1;
            if (old->parent) {
                const int at = old->parent->children.indexOf(old);
                for (int i = at - 1; i >= 0 && row < 0; --i) {
                    BinderItem *prev = np->item(old->parent->children.at(i)->id);
                    if (!prev)
                        prev = placed.value(old->parent->children.at(i));
                    if (prev && prev->parent == parent)
                        row = parent->children.indexOf(prev) + 1;
                }
                if (row < 0)
                    row = 0;
            }
            auto *copy = new BinderItem;
            copy->id = old->id;
            copy->type = old->type;
            copy->title = old->title;
            copy->label = old->label;
            copy->status = old->status;
            copy->includeInCompile = old->includeInCompile;
            copy->sectionType = old->sectionType;
            for (const QString &k : old->keywords) {
                const QString to = maps.keyword.value(k);
                if (!to.isEmpty() && !copy->keywords.contains(to))
                    copy->keywords << to;
            }
            for (auto v = old->custom.cbegin(); v != old->custom.cend(); ++v) {
                QString value;
                const QString to = maps.field.value(v.key());
                if (!to.isEmpty() && takeValue(np, to, v.value(), &value))
                    copy->custom.insert(to, value);
            }
            copy->created = old->created;
            copy->modified = old->modified;
            copy->icon = old->icon;
            copy->synopsis = old->synopsis;
            copy->synopsisImage = old->synopsisImage;
            copy->fileExt = old->fileExt;
            copy->originalFileName = old->originalFileName;
            copy->sourceUrl = old->sourceUrl;
            copy->target = old->target;
            copy->targetChars = old->targetChars;
            copy->defaultTemplate = old->defaultTemplate;
            copy->scriptMode = old->scriptMode;
            copy->bookmarks = old->bookmarks;
            copy->corkboardPos = old->corkboardPos;
            copy->corkArrangement = old->corkArrangement;
            copy->hasSnapshots = old->hasSnapshots;
            np->model()->insertExisting(parent, row, copy);
            placed.insert(old, copy);
            if (QDir(op->contentDir(old->id)).exists())
                copyDir(op->contentDir(old->id), np->contentDir(old->id));
            if (QDir(op->snapshotsDir(old->id)).exists())
                copyDir(op->snapshotsDir(old->id), np->snapshotsDir(old->id));
            ++kept;
        }
        if (keptNew)
            *keptNew = kept;
    }
    // the objects of the project (0.20): only the working copy has them
    if (QDir(op->folderPath() + QLatin1String("/objects")).exists())
        copyDir(op->folderPath() + QLatin1String("/objects"), np->folderPath() + QLatin1String("/objects"));
    // own icons of the working copy
    for (const QString &n : icons::customNames(op))
        if (!icons::customNames(np).contains(n)) {
            QDir().mkpath(icons::customDir(np));
            QFile::copy(icons::customPath(op, n), icons::customPath(np, n));
        }
    np->docs()->recountAll();
    np->markIndexDirty();
    if (metaAdded)
        *metaAdded = added;
    return fromScriv;
}

QString workingCopyFor(const QString &path)
{
    const QString scrivDir = scrivFolder(path);
    QString identifier;
    QFile f(ScrivenerImporter::findScrivx(scrivDir));
    QDomDocument d;
    if (f.open(QIODevice::ReadOnly) && d.setContent(&f))
        identifier = d.documentElement().attribute(QStringLiteral("Identifier"));
    return projectIn(workingCopiesDir() + QLatin1Char('/') + keyFor(identifier, scrivDir));
}

OpenResult open(const QString &path, QString *error)
{
    OpenResult res;
    const QString scrivDir = scrivFolder(path);
    const QString scrivx = ScrivenerImporter::findScrivx(scrivDir);
    if (scrivx.isEmpty()) {
        if (error)
            *error = QObject::tr("No Scrivener project (.scrivx) was found in:\n%1").arg(scrivDir);
        return res;
    }
    QString identifier;
    {
        QFile f(scrivx);
        QDomDocument d;
        if (f.open(QIODevice::ReadOnly) && d.setContent(&f))
            identifier = d.documentElement().attribute(QStringLiteral("Identifier"));
    }
    const QString wcDir = workingCopiesDir() + QLatin1Char('/') + keyFor(identifier, scrivDir);
    // a working copy moved aside by a re-read that did not end (0.20): back into its place,
    // or away when the new one got there
    for (const QFileInfo &fi : QDir(wcDir).entryInfoList({QStringLiteral("*" QB_PROJECT_SUFFIX ".old")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString orig = fi.absoluteFilePath().chopped(4);
        if (projectIn(wcDir).isEmpty())
            QDir().rename(fi.absoluteFilePath(), orig);
        else
            QDir(fi.absoluteFilePath()).removeRecursively();
    }
    const QString wcProject = projectIn(wcDir);
    SyncState st;
    const bool haveOld = !wcProject.isEmpty() && st.load(wcProject + QLatin1String("/settings/scrivener-sync.json"));
    // 0.81.2: a working copy of an older version (st.meta 0) is read again once, so its keywords, custom metadata
    // and collections are joined with the .scriv's and its values take this version's form
    if (haveOld && st.fingerprint == fingerprint(scrivDir) && st.meta >= kMetaLevel) {
        res.project = Project::open(wcProject, error);
        if (res.project) {
            res.project->scrivenerPath = scrivDir;
            res.project->scrivenerLockAtOpen = scrivenerLockStamp(scrivDir);
            // the state was written with the working copy; nothing to read
        }
        return res;
    }

    // read the .scriv (again)
    res.reread = true;
    const QString tmp = wcDir + QLatin1String("/.reading");
    QDir(tmp).removeRecursively();
    QDir().mkpath(tmp);
    std::unique_ptr<Project> oldP;
    if (haveOld) {
        QString e;
        oldP = Project::open(wcProject, &e);
    }
    ScrivenerImportReport rep;
    int keptNew = 0;
    bool metaAdded = false;
    auto np = ScrivenerImporter::importProject(scrivDir, tmp, baseName(scrivDir),
                                               oldP ? oldP->storage : StorageFormat::Rtf, &rep, error);
    if (!np) {
        QDir(tmp).removeRecursively();
        return res;
    }
    res.warnings = rep.warnings;
    if (oldP)
        res.changedElsewhere = mergeFromOld(np.get(), oldP.get(), scrivDir, st, &keptNew, &metaAdded);
    if (keptNew > 0)
        res.warnings << QObject::tr("%n document(s) that had not reached the Scrivener project yet (the last save did not end) "
                                    "were kept and are saved into it now.", nullptr, keptNew);
    if (!np->save(error)) {
        QDir(tmp).removeRecursively();
        return res;
    }
    const QString newFolder = np->folderPath();
    np.reset();
    oldP.reset();
    // the new working copy takes the place of the old one: the old one is moved aside
    // first and removed only when the new one is in place (a crash in between loses
    // nothing; the next opening finds the old one again)
    const QString aside = wcProject.isEmpty() ? QString() : wcProject + QLatin1String(".old");
    if (!aside.isEmpty()) {
        QDir(aside).removeRecursively();
        QDir().rename(wcProject, aside);
    }
    const QString target = wcDir + QLatin1Char('/') + QFileInfo(newFolder).fileName();
    QDir(target).removeRecursively();
    if (!QDir().rename(newFolder, target)) {
        if (!aside.isEmpty())
            QDir().rename(aside, wcProject);
        if (error)
            *error = QObject::tr("Cannot set up the working copy in %1").arg(wcDir);
        return res;
    }
    if (!aside.isEmpty())
        QDir(aside).removeRecursively();
    QDir(tmp).removeRecursively();
    res.project = Project::open(target, error);
    if (!res.project)
        return res;
    res.project->scrivenerPath = scrivDir;
    res.project->scrivenerLockAtOpen = scrivenerLockStamp(scrivDir);
    SyncState fresh;
    fresh.scrivPath = scrivDir;
    fresh.identifier = identifier;
    recordAll(res.project.get(), scrivDir, fresh);
    // 0.81.2: what the working copy alone had is put into the .scriv by the next write; until then a
    // re-read joins again
    fresh.meta = metaAdded ? qMin(haveOld ? st.meta : 0, kMetaLevel - 1) : kMetaLevel;
    fresh.save(stateFile(res.project.get()));
    res.project->markIndexDirty();
    res.project->saveIndex();
    return res;
}

bool sync(Project *p, QString *error, bool quick)
{
    if (p->scrivenerPath.isEmpty())
        return true;
    // 0.64.1 (audit C-4): Scrivener opened the project while it is open here: nothing more is
    // written into the .scriv (the working copy keeps every change) until Scrivener has closed it
    const QString lock = scrivenerLockStamp(p->scrivenerPath);
    if (lock.isEmpty()) {
        p->scrivenerLockAtOpen.clear();
    } else if (lock != p->scrivenerLockAtOpen) {
        QString who;
        lockedByScrivener(p->scrivenerPath, &who);
        p->scrivenerLockRefused = lock;
        p->scrivenerLockWho = who;
        if (error)
            *error = QObject::tr("Scrivener has opened this project (%1) while it is open here. So that the two programs do "
                                 "not write over each other's changes, nothing more is written into the Scrivener project "
                                 "%2. Your changes are kept in this program's working copy: close the project in Scrivener, "
                                 "then save here again (File > Save).")
                         .arg(who, QDir::toNativeSeparators(p->scrivenerPath));
        return false;
    }
    p->scrivenerLockRefused.clear();
    p->scrivenerLockWho.clear();
    timing::Laps laps;
    SyncState st;
    st.load(stateFile(p));
    laps.lap("state read");
    WriteReport rep;
    if (!write(p, p->scrivenerPath, st, &rep, error, quick)) {
        // 0.64.2: what was written before the failure is recorded (each file's hash goes into the state only once it
        // is written): without it the next save took this program's own files for changes made elsewhere
        st.save(stateFile(p));
        return false;
    }
    laps.lap("write");
    const bool ok = st.save(stateFile(p));
    // 0.64.1: files changed in the .scriv elsewhere whose version could not be kept were not written
    if (!rep.keepFailed.isEmpty()) {
        if (error)
            *error = QObject::tr("Some files of the Scrivener project were changed elsewhere (another computer or program), and "
                                 "those versions could not be kept in %1, so they were not replaced: %2. Check that the "
                                 "project folder is writable and the disk is not full.")
                         .arg(QDir::toNativeSeparators(p->settingsDir() + QStringLiteral("/conflicts")), rep.keepFailed.join(QStringLiteral(", ")));
        return false;
    }
    laps.lap("state written");
    laps.print("scrivener sync");
    return ok;
}

std::unique_ptr<Project> convertToScrivener(Project *p, const QString &scrivDirIn, QString *error)
{
    const QString scrivDir = QFileInfo(scrivDirIn).absoluteFilePath();
    if (QFileInfo::exists(scrivxIn(scrivDir)) && QFileInfo(scrivxIn(scrivDir)).isFile()) {
        if (error)
            *error = QObject::tr("There is a Scrivener project at this place already:\n%1").arg(scrivDir);
        return nullptr;
    }
    if (!p->save(error))
        return nullptr;
    const QString identifier = QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();
    const QString wcDir = workingCopiesDir() + QLatin1Char('/') + keyFor(identifier, scrivDir);
    const QString target = wcDir + QLatin1Char('/') + QFileInfo(p->folderPath()).fileName();
    QDir(target).removeRecursively();
    if (!copyDir(p->folderPath(), target, {QStringLiteral("settings/lock"), QStringLiteral("settings/scrivener-sync.json")})) {
        if (error)
            *error = QObject::tr("Cannot copy the project to %1").arg(target);
        return nullptr;
    }
    auto wc = Project::open(target, error);
    if (!wc)
        return nullptr;
    wc->scrivenerPath = scrivDir;
    SyncState st;
    st.identifier = identifier;
    WriteReport rep;
    if (!write(wc.get(), scrivDir, st, &rep, error))
        return nullptr;
    st.save(stateFile(wc.get()));
    wc->markIndexDirty();
    wc->saveIndex();
    return wc;
}

bool saveCopyAsScrivener(Project *p, const QString &scrivDir, WriteReport *report, QString *error)
{
    if (QFileInfo(scrivxIn(scrivDir)).isFile()) {
        if (error)
            *error = QObject::tr("There is a Scrivener project at this place already:\n%1").arg(scrivDir);
        return false;
    }
    if (!p->save(error))
        return false;
    SyncState st; // a copy: nothing is recorded in the project
    return write(p, scrivDir, st, report, error);
}

std::unique_ptr<Project> copyAsQuillbinder(Project *p, const QString &parentDir, const QString &name, QString *error)
{
    if (!p->save(error))
        return nullptr;
    const QString safe = text::safeFileName(name);
    const QString target = QDir(parentDir).filePath(safe + QLatin1String(QB_PROJECT_SUFFIX));
    if (QFileInfo::exists(target)) {
        if (error)
            *error = QObject::tr("A project called \"%1\" already exists in this folder.").arg(name);
        return nullptr;
    }
    const QString oldIndex = QFileInfo(p->indexPath()).fileName();
    if (!copyDir(p->folderPath(), target, {QStringLiteral("settings/lock"), QStringLiteral("settings/scrivener-sync.json"),
                                           oldIndex})) {
        if (error)
            *error = QObject::tr("Cannot copy the project to %1").arg(target);
        return nullptr;
    }
    QFile::copy(p->indexPath(), QDir(target).filePath(safe + QLatin1String(QB_PROJECT_FILE_SUFFIX)));
    auto np = Project::open(target, error);
    if (!np)
        return nullptr;
    np->scrivenerPath.clear();
    np->markIndexDirty();
    if (!np->save(error))
        return nullptr;
    return np;
}

} // namespace scrivener
} // namespace qb
