#include "import/ScrivenerFormat.h"
#include "core/IconCatalog.h"
#include "core/MindMap.h"

#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Paths.h"
#include "core/Project.h"
#include "core/Snapshots.h"
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

bool writeFile(const QString &path, const QByteArray &data, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        if (error)
            *error = QObject::tr("Cannot write %1").arg(path);
        return false;
    }
    return true;
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
            QFile::remove(target);
            if (!QFile::copy(path, target))
                return false;
        }
    }
    return true;
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
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            const QTextCharFormat cf = fr.charFormat();
            const auto kind = NoteKind(cf.intProperty(props::NoteKind));
            if (cf.isAnchor() && cf.anchorHref().startsWith(QLatin1String("qb://"))) {
                QTextCursor c(doc.get());
                c.setPosition(fr.position());
                c.setPosition(fr.position() + fr.length(), QTextCursor::KeepAnchor);
                QTextCharFormat lf;
                lf.setAnchorHref(QStringLiteral("scrivlnk://") + cf.anchorHref().mid(5));
                c.mergeCharFormat(lf);
            }
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
        // keep bold, italics, fonts; take the note properties away
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
                QTextCursor fc(doc.get());
                fc.setPosition(fs);
                fc.setPosition(fe, QTextCursor::KeepAnchor);
                fc.setCharFormat(cf);
            }
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
    if (it->type != ItemType::Image && it->type != ItemType::Pdf
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
            e.setAttribute(QStringLiteral("Color"), colorText(l.color));
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

QString fingerprint(const QString &scrivDir)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(sha1File(scrivxIn(scrivDir)).toLatin1());
    QStringList lines;
    for (const QString &sub : {QStringLiteral("Files/Data"), QStringLiteral("Snapshots")}) {
        const QString base = QDir(scrivDir).filePath(sub);
        QDirIterator it(base, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (fi.fileName() == QLatin1String("docs.checksum"))
                continue;
            lines << QDir(scrivDir).relativeFilePath(fi.absoluteFilePath()) + QLatin1Char(' ') + QString::number(fi.size())
                         + QLatin1Char(' ') + QString::number(fi.lastModified().toMSecsSinceEpoch());
        }
    }
    lines.sort();
    h.addData(lines.join(QLatin1Char('\n')).toUtf8());
    return QString::fromLatin1(h.result().toHex());
}

bool lockedByScrivener(const QString &scrivDir, QString *who)
{
    for (const QString &rel : {QStringLiteral("Files/user.lock"), QStringLiteral("user.lock")}) {
        QFile f(QDir(scrivDir).filePath(rel));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        QString user, host;
        for (const QByteArray &line : f.readAll().split('\n')) {
            const QString l = QString::fromUtf8(line).trimmed();
            if (l.startsWith(QLatin1String("user=")))
                user = l.mid(5);
            else if (l.startsWith(QLatin1String("host=")))
                host = l.mid(5);
        }
        if (who)
            *who = (user.isEmpty() && host.isEmpty()) ? QObject::tr("unknown") : QObject::tr("%1 on %2").arg(user, host);
        return true;
    }
    return false;
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
    return true;
}

bool SyncState::save(const QString &file) const
{
    QJsonObject o;
    o[QStringLiteral("scriv")] = scrivPath;
    o[QStringLiteral("identifier")] = identifier;
    o[QStringLiteral("fingerprint")] = fingerprint;
    QJsonObject a, b;
    for (auto it = ours.cbegin(); it != ours.cend(); ++it)
        a[it.key()] = it.value();
    for (auto it = theirs.cbegin(); it != theirs.cend(); ++it)
        b[it.key()] = it.value();
    o[QStringLiteral("ours")] = a;
    o[QStringLiteral("theirs")] = b;
    o[QStringLiteral("snapshots")] = QJsonArray::fromStringList(snapshots);
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson());
    return f.commit();
}

// ---------------------------------------------------------------- write

bool write(Project *p, const QString &scrivDirIn, SyncState &state, WriteReport *reportIn, QString *error)
{
    WriteReport localReport;
    WriteReport &rep = reportIn ? *reportIn : localReport;
    const QString scrivDir = QFileInfo(scrivDirIn).absoluteFilePath();
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
    auto put = [&](const QString &uuid, const QString &name, const QByteArray &bytes) -> bool {
        if (!writeFile(data + uuid + QLatin1Char('/') + name, bytes, error))
            return false;
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
            textChanged = true;
        }
        state.theirs.remove(uuid + QLatin1Char('/') + name);
        checksums.remove(uuid.toLower() + QLatin1Char('/') + name);
    };
    // Decides whether one file must be written: what this program has
    // changed since the last write. Files changed in Scrivener meanwhile stay.
    auto mustWrite = [&](const QString &ourKey, const QString &ourHash, const QString &theirKey,
                         const QString &theirPath) -> bool {
        const QString theirNow = sha1File(theirPath);
        const bool ourSame = state.ours.contains(ourKey) && state.ours.value(ourKey) == ourHash;
        const bool theirSame = state.theirs.value(theirKey) == theirNow;
        if (ourSame && theirSame)
            return false;
        if (ourSame && !theirSame) {
            ++rep.keptTheirs;
            return false;
        }
        return true;
    };

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
            QString ourHash = isPlaceholderType(it->type) ? statHash(p->mediaFilePath(it)) : sha1File(textFile);
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
            const QString notesHash = sha1File(p->docs()->notesPath(it->id));
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
        // the file of a picture, PDF, audio or video document
        if (isMediaType(it->type)) {
            const QString src = p->mediaFilePath(it);
            const QString key = it->id + QLatin1String("/file");
            const QString h = statHash(src);
            const QString target = dir + QLatin1String("content.") + it->fileExt.toLower();
            const QString theirKey = uuid + QLatin1String("/content.") + it->fileExt.toLower();
            const bool ourSame = state.ours.value(key) == h && !h.isEmpty();
            const bool theirSame = state.theirs.value(theirKey) == statHash(target) && QFile::exists(target);
            if (!(ourSame && theirSame) && QFile::exists(src)) {
                QDir().mkpath(dir);
                QFile::remove(target);
                if (!QFile::copy(src, target)) {
                    if (error)
                        *error = QObject::tr("Cannot copy %1").arg(src);
                    return false;
                }
                ++rep.written;
                state.ours.insert(key, h);
                state.theirs.insert(theirKey, statHash(target));
                any = true;
            }
            // pictures of a web page document
            const QString pageFiles = p->contentDir(it->id) + QLatin1String("/page-files");
            if (it->type == ItemType::WebPage && QDir(pageFiles).exists() && any)
                copyDir(pageFiles, dir + QLatin1String("page-files"));
        }
        // the mind map of this program (0.17): beside Scrivener's files of the document
        {
            const QString src = mindmap::pathFor(p->contentDir(it->id));
            const QString key = it->id + QLatin1String("/mindmap");
            const QString theirKey = uuid + QLatin1Char('/') + mindmap::scrivenerFileName();
            const QString target = dir + mindmap::scrivenerFileName();
            if (QFile::exists(src)) {
                const QString h = sha1File(src);
                if (state.ours.value(key) != h || state.theirs.value(theirKey) != sha1File(target)) {
                    QFile f(src);
                    if (f.open(QIODevice::ReadOnly) && writeFile(target, f.readAll(), error)) {
                        ++rep.written;
                        state.ours.insert(key, h);
                        state.theirs.insert(theirKey, sha1File(target));
                    }
                }
            } else if (state.ours.contains(key)) {
                // the board was emptied here
                QFile::remove(target);
                state.ours.remove(key);
                state.theirs.remove(theirKey);
            }
        }
        Q_UNUSED(any);
    }

    // own icons of the project in use: the Icons folder of the .scriv (a guess
    // of where Scrivener keeps the icons of a project)
    {
        QSet<QString> used;
        for (BinderItem *it : p->allItems())
            if (icons::isCustom(it->icon))
                used.insert(icons::customName(it->icon));
        for (const QString &n : std::as_const(used)) {
            const QString src = icons::customPath(p, n);
            const QString target = scrivDir + QLatin1String("/Icons/") + n + QLatin1String(".png");
            if (QFile::exists(src) && sha1File(src) != sha1File(target)) {
                QDir().mkpath(scrivDir + QLatin1String("/Icons"));
                QFile::remove(target);
                QFile::copy(src, target);
            }
        }
    }

    // documents that are no longer there (emptied trash): their folders go,
    // as far as they belong to documents of this project or of the old binder
    {
        QSet<QString> known;
        for (auto it = state.theirs.cbegin(); it != state.theirs.cend(); ++it)
            known.insert(it.key().section(QLatin1Char('/'), 0, 0));
        QDomDocument d;
        if (!existing.isEmpty() && d.setContent(existing)) {
            const QDomNodeList items = d.elementsByTagName(QStringLiteral("BinderItem"));
            for (int i = 0; i < items.size(); ++i)
                known.insert(items.at(i).toElement().attribute(QStringLiteral("UUID")).toUpper());
        }
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

    // snapshots of this program that Scrivener does not have yet
    for (BinderItem *it : p->allItems()) {
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

    // the project file
    const QByteArray scrivxBytes = text::xmlSafeBytes(buildScrivx(p, existing, state.identifier));
    if (!writeFile(scrivx, scrivxBytes, error))
        return false;
    // Scrivener's autosave copy of the binder would be older than this one
    QFile::remove(scrivDir + QLatin1String("/Files/binder.autosave"));
    if (!QFile::exists(scrivDir + QLatin1String("/Files/version.txt")))
        writeFile(scrivDir + QLatin1String("/Files/version.txt"), "23", nullptr);
    if (fresh && !QFile::exists(scrivDir + QLatin1String("/Settings/projectpreferences.xml")))
        writeFile(scrivDir + QLatin1String("/Settings/projectpreferences.xml"), projectPreferences(p), nullptr);
    if (textChanged || !QFile::exists(scrivDir + QLatin1String("/Files/search.indexes")))
        writeFile(scrivDir + QLatin1String("/Files/search.indexes"), searchIndexes(p), nullptr);
    if (hadChecksums || fresh) {
        QByteArray cs;
        for (auto it = checksums.cbegin(); it != checksums.cend(); ++it)
            cs += it.key().toUtf8() + '=' + it.value().toUtf8() + '\n';
        writeFile(checksumFile, cs, nullptr);
    }
    state.fingerprint = fingerprint(scrivDir);
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
        for (auto it = st.theirs.cbegin(); it != st.theirs.cend(); ++it)
            synced.insert(it.key().section(QLatin1Char('/'), 0, 0).toUpper());
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
    SyncState fresh;
    fresh.scrivPath = scrivDir;
    fresh.identifier = identifier;
    recordAll(res.project.get(), scrivDir, fresh);
    fresh.save(stateFile(res.project.get()));
    res.project->markIndexDirty();
    res.project->saveIndex();
    return res;
}

bool sync(Project *p, QString *error)
{
    if (p->scrivenerPath.isEmpty())
        return true;
    SyncState st;
    st.load(stateFile(p));
    WriteReport rep;
    if (!write(p, p->scrivenerPath, st, &rep, error))
        return false;
    return st.save(stateFile(p));
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
