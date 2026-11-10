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
#include "import/ScrivenerImporter.h"
#include "text/RtfIO.h"
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
#include <QUuid>
#include <algorithm>

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

QString typeFor(const BinderItem *it)
{
    switch (it->type) {
    case ItemType::Draft: return QStringLiteral("DraftFolder");
    case ItemType::Research: return QStringLiteral("ResearchFolder");
    case ItemType::Trash: return QStringLiteral("TrashFolder");
    case ItemType::Folder: return QStringLiteral("Folder");
    case ItemType::Image: return QStringLiteral("Image");
    case ItemType::Pdf: return QStringLiteral("PDF");
    // no verified Scrivener type for these: a text document that says what it
    // is, with the file beside it (read back by this program)
    case ItemType::Text:
    case ItemType::Media:
    case ItemType::WebPage:
    case ItemType::File: return QStringLiteral("Text");
    }
    return QStringLiteral("Text");
}

bool isPlaceholderType(ItemType t) { return t == ItemType::Media || t == ItemType::WebPage || t == ItemType::File; }

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

// A copy of the text in which every note is a linked note: HYPERLINK
// "scrivcmt://<ID>" on the anchor text, the note's text in content.comments.
// Inline annotations and inline footnotes become linked notes on the word
// before them (the format of Scrivener's inline notes is not known here).
QByteArray scrivRtf(const QTextDocument *src, const Formatting &f, QList<Note> *notes)
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
    // from the end, so that positions stay right
    std::sort(ranges.begin(), ranges.end(), [](const Range &a, const Range &b) { return a.start > b.start; });
    for (const Range &r : ranges) {
        Note n;
        n.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();
        n.text = r.text.trimmed();
        n.footnote = r.kind == NoteKind::LinkedFootnote || r.kind == NoteKind::InlineFootnote;
        n.color = r.color;
        int aStart = r.start, aEnd = r.end;
        if (isInlineNote(r.kind)) {
            QTextCursor c(doc.get());
            c.setPosition(r.start);
            c.setPosition(r.end, QTextCursor::KeepAnchor);
            c.removeSelectedText();
            // the word before the note, else the word after it
            const QTextBlock b = doc->findBlock(r.start);
            const QString t = b.text();
            int p = r.start - b.position();
            int e = p;
            while (e > 0 && t.at(e - 1).isSpace())
                --e;
            int s = e;
            while (s > 0 && !t.at(s - 1).isSpace())
                --s;
            if (s == e) {
                s = p;
                while (s < t.size() && t.at(s).isSpace())
                    ++s;
                e = s;
                while (e < t.size() && !t.at(e).isSpace())
                    ++e;
            }
            if (s == e) {
                // nothing to hang it on: a mark of its own
                QTextCursor ins(doc.get());
                ins.setPosition(r.start);
                QTextCharFormat plain = ins.charFormat();
                text::clearNoteProps(plain);
                ins.insertText(QStringLiteral("*"), plain);
                s = p;
                e = p + 1;
            }
            aStart = b.position() + s;
            aEnd = b.position() + e;
        }
        QTextCursor c(doc.get());
        c.setPosition(aStart);
        c.setPosition(aEnd, QTextCursor::KeepAnchor);
        // keep bold, italics, fonts; take the note properties away (collected first, 0.64.2)
        struct Piece { int s, e; QTextCharFormat f; };
        QList<Piece> pieces;
        for (QTextBlock b = doc->findBlock(aStart); b.isValid() && b.position() < aEnd; b = b.next()) {
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const QTextFragment fr = it.fragment();
                const int fs = qMax(aStart, fr.position()), fe = qMin(aEnd, fr.position() + fr.length());
                if (fs >= fe)
                    continue;
                QTextCharFormat cf = fr.charFormat();
                text::clearNoteProps(cf);
                cf.setAnchor(true);
                cf.setAnchorHref(QStringLiteral("scrivcmt://") + n.id);
                pieces.append({fs, fe, cf});
            }
        }
        for (const Piece &p : pieces) {
            QTextCursor fc(doc.get());
            fc.setPosition(p.s);
            fc.setPosition(p.e, QTextCursor::KeepAnchor);
            fc.setCharFormat(p.f);
        }
        if (notes)
            notes->prepend(n);
    }
    return rtf::write(doc.get(), f);
}

QByteArray commentsXml(const QList<Note> &notes, const Formatting &f)
{
    QDomDocument dom;
    dom.appendChild(dom.createProcessingInstruction(QStringLiteral("xml"), QStringLiteral("version=\"1.0\" encoding=\"UTF-8\"")));
    QDomElement root = dom.createElement(QStringLiteral("Comments"));
    dom.appendChild(root);
    for (const Note &n : notes) {
        QDomElement e = dom.createElement(QStringLiteral("Comment"));
        e.setAttribute(QStringLiteral("ID"), n.id);
        e.setAttribute(QStringLiteral("Footnote"), n.footnote ? QStringLiteral("Yes") : QStringLiteral("No"));
        e.setAttribute(QStringLiteral("Color"), colorText(n.color.isValid() ? n.color
                                                                            : (n.footnote ? QColor(200, 200, 200)
                                                                                          : QColor(255, 214, 102))));
        QTextDocument d;
        d.setPlainText(n.text);
        QByteArray body = rtf::write(&d, f);
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

struct XmlCtx {
    Project *p;
    QDomDocument dom;
    QHash<QString, QDomElement> old; // UUID -> element of the existing .scrivx
};

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
            setChildText(meta, QStringLiteral("IconFileName"), iconName, {QStringLiteral("FileExtension")});
        else
            removeChild(meta, QStringLiteral("IconFileName"));
    }
    if (it->type == ItemType::Image || it->type == ItemType::Pdf)
        setChildText(meta, QStringLiteral("FileExtension"), it->fileExt.toLower());
    else
        removeChild(meta, QStringLiteral("FileExtension"));
    // 0.64.2: an element of the file that has no MetaData keeps having none, and text settings are added only
    // to items new to the file (Scrivener leaves both out on its top folders; adding them changed a .scriv
    // opened and closed without a change)
    if (!existing.isNull() && existing.firstChildElement(QStringLiteral("MetaData")).isNull() && !meta.hasChildNodes())
        e.removeChild(meta);
    if (it->type != ItemType::Image && it->type != ItemType::Pdf && existing.isNull()
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

const QStringList kTopOrder = {QStringLiteral("Binder"),          QStringLiteral("Collections"),
                               QStringLiteral("SectionTypes"),    QStringLiteral("LabelSettings"),
                               QStringLiteral("StatusSettings"),  QStringLiteral("ProjectBookmarks"),
                               QStringLiteral("ProjectTargets"),  QStringLiteral("RecentWritingHistory"),
                               QStringLiteral("TemplateFolderUUID"), QStringLiteral("BookmarksFolderUUID"),
                               QStringLiteral("PrintSettings")};

QStringList after(const QString &name)
{
    const int i = kTopOrder.indexOf(name);
    return i < 0 ? QStringList() : kTopOrder.mid(i + 1);
}

QByteArray buildScrivx(Project *p, const QByteArray &existing, const QString &identifier)
{
    XmlCtx c;
    c.p = p;
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

    // section types
    {
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
        QDomElement ls = replaceTop(root, QStringLiteral("LabelSettings"), after(QStringLiteral("LabelSettings")));
        setChildText(ls, QStringLiteral("Title"), p->labelTitle);
        setChildText(ls, QStringLiteral("DefaultLabelID"), QStringLiteral("-1"));
        QDomElement labels = child(ls, QStringLiteral("Labels"), true);
        QDomElement none = c.dom.createElement(QStringLiteral("Label"));
        none.setAttribute(QStringLiteral("ID"), QStringLiteral("-1"));
        none.appendChild(c.dom.createTextNode(QObject::tr("No %1").arg(p->labelTitle)));
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
        setChildText(ss, QStringLiteral("DefaultStatusID"), QStringLiteral("-1"));
        QDomElement items = child(ss, QStringLiteral("StatusItems"), true);
        QDomElement nos = c.dom.createElement(QStringLiteral("Status"));
        nos.setAttribute(QStringLiteral("ID"), QStringLiteral("-1"));
        nos.appendChild(c.dom.createTextNode(QObject::tr("No %1").arg(p->statusTitle)));
        items.appendChild(nos);
        for (const Status &s : p->statuses) {
            QDomElement e = c.dom.createElement(QStringLiteral("Status"));
            e.setAttribute(QStringLiteral("ID"), s.id);
            e.appendChild(c.dom.createTextNode(s.name));
            items.appendChild(e);
        }
    }

    // project bookmarks (documents of the project only)
    {
        QDomElement bm = replaceTop(root, QStringLiteral("ProjectBookmarks"), after(QStringLiteral("ProjectBookmarks")));
        for (const Bookmark &b : p->projectBookmarks) {
            if (!b.isInternal())
                continue;
            const QString id = b.internalId();
            if (!p->item(id))
                continue;
            QDomElement e = c.dom.createElement(QStringLiteral("Bookmark"));
            e.setAttribute(QStringLiteral("BinderUUID"), uuidFor(id));
            bm.appendChild(e);
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
            dt.setAttribute(QStringLiteral("Deadline"), dateText(QDateTime(p->targets.deadline, QTime(23, 59))));
            dt.setAttribute(QStringLiteral("IgnoreDeadline"), QStringLiteral("No"));
        } else if (dt.hasAttribute(QStringLiteral("Deadline"))) {
            dt.setAttribute(QStringLiteral("IgnoreDeadline"), QStringLiteral("Yes"));
        }
        setText(dt, QString::number(p->targets.draftTarget));
        QDomElement st = child(tg, QStringLiteral("SessionTarget"), true);
        st.setAttribute(QStringLiteral("Type"), p->targets.sessionChars ? QStringLiteral("Characters") : QStringLiteral("Words"));
        setText(st, QString::number(p->targets.sessionTarget));
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

    // docs.checksum: SHA-1 of the files Scrivener checks, keys in lower case
    QMap<QString, QString> checksums;
    const QString checksumFile = data + QLatin1String("docs.checksum");
    const bool hadChecksums = QFile::exists(checksumFile);
    {
        QFile f(checksumFile);
        if (f.open(QIODevice::ReadOnly))
            for (const QByteArray &line : f.readAll().split('\n')) {
                const int eq = line.indexOf('=');
                if (eq > 0)
                    checksums.insert(QString::fromUtf8(line.left(eq)), QString::fromUtf8(line.mid(eq + 1)).trimmed());
            }
    }
    bool textChanged = fresh;
    QSet<QString> touched; // files written or removed (for the fingerprint of a quick write)
    auto put = [&](const QString &uuid, const QString &name, const QByteArray &bytes) -> bool {
        if (!writeFile(data + uuid + QLatin1Char('/') + name, bytes, error))
            return false;
        touched.insert(data + uuid + QLatin1Char('/') + name);
        ++rep.written;
        state.theirs.insert(uuid + QLatin1Char('/') + name, sha1(bytes));
        if (name == QLatin1String("content.rtf") || name == QLatin1String("notes.rtf") || name == QLatin1String("synopsis.txt"))
            checksums.insert(uuid.toLower() + QLatin1Char('/') + name, sha1(bytes));
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
        checksums.remove(uuid.toLower() + QLatin1Char('/') + name);
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
    QSet<QString> present;
    for (BinderItem *it : p->allItems()) {
        const QString uuid = uuidFor(it->id);
        present.insert(uuid);
        const QString dir = data + uuid + QLatin1Char('/');
        bool any = false;
        if (it->hasText() || isPlaceholderType(it->type)) {
            // text
            const QString ourKey = it->id + QLatin1String("/text");
            const QString textFile = p->docs()->textPath(it->id);
            QString ourHash = isPlaceholderType(it->type) ? statHash(p->mediaFilePath(it))
                              : quick                       ? knownSha1File(textFile)
                                                            : sha1File(textFile);
            if (mustWrite(ourKey, ourHash, uuid + QLatin1String("/content.rtf"), dir + QLatin1String("content.rtf"))) {
                QList<Note> notes;
                QByteArray rtfBytes;
                bool empty = true;
                if (isPlaceholderType(it->type)) {
                    rtfBytes = placeholderRtf(it, p->formatting);
                    empty = false;
                } else {
                    p->docs()->editStored(it->id, false, [&](QTextDocument *d) {
                        empty = d->isEmpty();
                        if (!empty)
                            rtfBytes = scrivRtf(d, p->formatting, &notes);
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
                        if (!put(uuid, QStringLiteral("content.comments"), commentsXml(notes, p->formatting)))
                            return false;
                    } else {
                        drop(uuid, QStringLiteral("content.comments"));
                    }
                    any = true;
                }
                state.ours.insert(ourKey, ourHash);
            } else {
                ++rep.unchanged;
            }
            // notes
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
        // the file of a picture, PDF, audio or video document (a quick write leaves it for the next full one)
        if (!quick && isMediaType(it->type)) {
            const QString src = p->mediaFilePath(it);
            const QString key = it->id + QLatin1String("/file");
            const QString h = statHash(src);
            const QString target = dir + QLatin1String("content.") + it->fileExt.toLower();
            const QString theirKey = uuid + QLatin1String("/content.") + it->fileExt.toLower();
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
                    // 0.65.0 (audit F05): copied beside the target, then renamed over it (before: deleted first, so a
                    // failed copy left the .scriv without the file)
                    QString why;
                    if (!safesave::copyFile(src, target, &why)) {
                        if (error)
                            *error = QObject::tr("Cannot copy %1 into the Scrivener project: %2").arg(src, why);
                        return false;
                    }
                    ++rep.written;
                    state.ours.insert(key, h);
                    state.theirs.insert(theirKey, statHash(target));
                    any = true;
                }
            }
            // pictures of a web page document
            const QString pageFiles = p->contentDir(it->id) + QLatin1String("/page-files");
            if (it->type == ItemType::WebPage && QDir(pageFiles).exists() && any)
                copyDir(pageFiles, dir + QLatin1String("page-files"));
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
                it = it.key().startsWith(uuid.toLower() + QLatin1Char('/')) ? checksums.erase(it) : std::next(it);
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
        const QByteArray scrivxBytes = text::xmlSafeBytes(buildScrivx(p, existing, state.identifier));
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
        }
        if (!binderKept && !rep.keepFailed.contains(QFileInfo(scrivx).fileName())) {
            state.theirs.insert(QStringLiteral(".scrivx"), scrivxStamp(scrivx));
            state.binder = QStringList(present.cbegin(), present.cend());
            state.binderKnown = true;
            if (ownScriv)
                p->binderChangedSinceSync = false;
        }
    }
    // Scrivener's autosave copy of the binder would be older than this one
    QFile::remove(scrivDir + QLatin1String("/Files/binder.autosave"));
    if (!QFile::exists(scrivDir + QLatin1String("/Files/version.txt")))
        writeFile(scrivDir + QLatin1String("/Files/version.txt"), "23", nullptr);
    if (fresh && !QFile::exists(scrivDir + QLatin1String("/Settings/projectpreferences.xml")))
        writeFile(scrivDir + QLatin1String("/Settings/projectpreferences.xml"), projectPreferences(p), nullptr);
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
        for (auto it = checksums.cbegin(); it != checksums.cend(); ++it)
            cs += it.key().toUtf8() + '=' + it.value().toUtf8() + '\n';
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
        st.ours.insert(it->id + QLatin1String("/text"), isPlaceholderType(it->type) ? statHash(p->mediaFilePath(it))
                                                                                    : sha1File(p->docs()->textPath(it->id)));
        st.ours.insert(it->id + QLatin1String("/notes"), sha1File(p->docs()->notesPath(it->id)));
        st.ours.insert(it->id + QLatin1String("/synopsis"), sha1(it->synopsis.toUtf8()));
        if (isMediaType(it->type)) {
            st.ours.insert(it->id + QLatin1String("/file"), statHash(p->mediaFilePath(it)));
            const QString ext = it->fileExt.toLower();
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
static int mergeFromOld(Project *np, Project *op, const QString &scrivDir, const SyncState &st, int *keptNew = nullptr)
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
    if (np->keywords.isEmpty())
        np->keywords = op->keywords;
    if (np->customFields.isEmpty())
        np->customFields = op->customFields;
    if (np->collections.isEmpty())
        np->collections = op->collections;
    for (const Bookmark &b : op->projectBookmarks)
        if (!b.isInternal())
            np->projectBookmarks << b;
    np->formatting = op->formatting;
    np->compile = op->compile;
    np->uiState = op->uiState;
    np->compositionBackground = op->compositionBackground;
    np->history = op->history;
    if (np->templatesFolderId.isEmpty())
        np->templatesFolderId = op->templatesFolderId;
    if (np->notesFolderId.isEmpty())
        np->notesFolderId = op->notesFolderId;
    np->docs()->setPageSetup(op->docs()->pageSetup());

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
        if (isPlaceholderType(old->type) && textSame) {
            // the stand-in text of an audio, video or other file: the file again
            it->type = old->type;
            it->fileExt = old->fileExt;
            it->originalFileName = old->originalFileName;
            copyDir(op->contentDir(old->id), np->contentDir(it->id));
            np->docs()->removeDocument(it->id);
        } else if (it->hasText() && old->hasText()) {
            if (textSame)
                np->docs()->setContent(it->id, op->docs()->text(old->id));
            else
                ++fromScriv;
            if (same(QStringLiteral("notes.rtf")))
                np->docs()->setNotesContent(it->id, op->docs()->notes(old->id));
        }
        it->scriptMode = old->scriptMode && it->hasText();
        it->bookmarks = old->bookmarks;
        it->target = old->target;
        it->targetChars = old->targetChars;
        it->defaultTemplate = old->defaultTemplate;
        it->corkboardPos = old->corkboardPos;
        if (it->icon.isEmpty())
            it->icon = old->icon;
        if (it->keywords.isEmpty())
            it->keywords = old->keywords;
        if (it->custom.isEmpty())
            it->custom = old->custom;
        // a mind map only the working copy has (0.17)
        {
            const QString oldMap = mindmap::pathFor(op->contentDir(old->id));
            const QString newMap = mindmap::pathFor(np->contentDir(it->id));
            if (QFile::exists(oldMap) && !QFile::exists(newMap)) {
                QDir().mkpath(np->contentDir(it->id));
                QFile::copy(oldMap, newMap);
            }
        }
        if (!old->synopsisImage.isEmpty() && it->synopsisImage.isEmpty()) {
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
            copy->keywords = old->keywords;
            copy->custom = old->custom;
            copy->created = old->created;
            copy->modified = old->modified;
            copy->icon = old->icon;
            copy->synopsis = old->synopsis;
            copy->synopsisImage = old->synopsisImage;
            copy->fileExt = old->fileExt;
            copy->originalFileName = old->originalFileName;
            copy->target = old->target;
            copy->targetChars = old->targetChars;
            copy->defaultTemplate = old->defaultTemplate;
            copy->scriptMode = old->scriptMode;
            copy->bookmarks = old->bookmarks;
            copy->corkboardPos = old->corkboardPos;
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
    if (haveOld && st.fingerprint == fingerprint(scrivDir)) {
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
    auto np = ScrivenerImporter::importProject(scrivDir, tmp, baseName(scrivDir),
                                               oldP ? oldP->storage : StorageFormat::Rtf, &rep, error);
    if (!np) {
        QDir(tmp).removeRecursively();
        return res;
    }
    res.warnings = rep.warnings;
    if (oldP)
        res.changedElsewhere = mergeFromOld(np.get(), oldP.get(), scrivDir, st, &keptNew);
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
