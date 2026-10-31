#include "import/ScrivenerImporter.h"
#include "core/IconCatalog.h"
#include "core/MindMap.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/Snapshots.h"
#include "text/DisplayFormat.h"
#include "text/RtfIO.h"
#include "text/Styles.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QRegularExpression>

namespace qb {

namespace {

// File > Options > Editing > Formatting, handed over by AppSettings (0.21.2)
bool s_haveNewFormatting = false;
Formatting s_newFormatting;

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

QDateTime parseScrivDate(const QString &s)
{
    if (s.isEmpty())
        return QDateTime();
    QDateTime dt = QDateTime::fromString(s.left(19), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (!dt.isValid())
        dt = QDateTime::fromString(s, Qt::ISODate);
    if (!dt.isValid())
        return QDateTime();
    const QString off = s.mid(20).trimmed();
    if (off.size() >= 5 && (off[0] == QLatin1Char('+') || off[0] == QLatin1Char('-'))) {
        const int sign = off[0] == QLatin1Char('-') ? -1 : 1;
        const int secs = sign * (off.mid(1, 2).toInt() * 3600 + off.mid(3, 2).toInt() * 60);
        dt.setOffsetFromUtc(secs);
        dt = dt.toLocalTime();
    }
    return dt;
}

QString childText(const QDomElement &e, const QString &name)
{
    return e.firstChildElement(name).text();
}

struct Ctx;
QString customIconFromScriv(Ctx &c, const QString &name);

struct Ctx {
    Project *p = nullptr;
    QString scrivDir;
    bool v3 = true;
    ScrivenerImportReport *report = nullptr;
    ScrivenerImporter::Progress progress;
    int total = 0;
    int done = 0;
    QHash<QString, QString> sectionTypeMap; // scrivener id -> our id
    QHash<QString, QString> keywordMap;
    QHash<QString, QString> customFieldMap;
    QHash<QString, int> fontUsage;
    QHash<int, int> sizeUsage;
    QHash<QString, QString> styleNames;            // Files/styles.xml: style id -> name
    QList<QPair<QString, QString>> pendingContent; // our id, scrivener content key
    QHash<BinderItem *, QDateTime> modifiedAt;     // Scrivener's dates, set again at the end
};

// an own icon of the Scrivener project (its Icons folder, a guess of where Scrivener keeps them)
QString customIconFromScriv(Ctx &c, const QString &name)
{
    if (icons::customNames(c.p).contains(name))
        return QStringLiteral("custom:") + name;
    for (const QString &folder : {QStringLiteral("Icons"), QStringLiteral("icons")}) {
        const QDir d(c.scrivDir + QLatin1Char('/') + folder);
        if (!d.exists())
            continue;
        for (const QFileInfo &fi : d.entryInfoList(QDir::Files)) {
            if (fi.completeBaseName().compare(name, Qt::CaseInsensitive) != 0)
                continue;
            const QImage img(fi.absoluteFilePath());
            if (img.isNull())
                continue;
            return icons::addCustom(c.p, name, img);
        }
    }
    return QString();
}

QString contentBase(const Ctx &c, const QString &scrivId)
{
    if (c.v3)
        return c.scrivDir + QLatin1String("/Files/Data/") + scrivId + QLatin1Char('/');
    return c.scrivDir + QLatin1String("/Files/Docs/") + scrivId;
}

QString findFile(const Ctx &c, const QString &scrivId, const QString &v3Name, const QString &v2Suffix)
{
    if (c.v3) {
        const QString p = contentBase(c, scrivId) + v3Name;
        if (QFile::exists(p))
            return p;
        // case differences
        QDir d(contentBase(c, scrivId));
        for (const QString &f : d.entryList(QDir::Files))
            if (f.compare(v3Name, Qt::CaseInsensitive) == 0)
                return d.filePath(f);
        return QString();
    }
    const QString p = contentBase(c, scrivId) + v2Suffix;
    return QFile::exists(p) ? p : QString();
}

QHash<QString, QPair<QString, bool>> readComments(const QString &path, QHash<QString, QColor> *colors)
{
    QHash<QString, QPair<QString, bool>> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return out;
    QDomDocument dom;
    if (!dom.setContent(&f))
        return out;
    const QDomNodeList list = dom.elementsByTagName(QStringLiteral("Comment"));
    for (int i = 0; i < list.size(); ++i) {
        const QDomElement e = list.at(i).toElement();
        const QString id = e.attribute(QStringLiteral("ID"));
        if (id.isEmpty())
            continue;
        const bool fn = e.attribute(QStringLiteral("Footnote")).compare(QLatin1String("Yes"), Qt::CaseInsensitive) == 0
                        || e.attribute(QStringLiteral("Type")).contains(QLatin1String("Footnote"), Qt::CaseInsensitive);
        const QString raw = e.text();
        const QString txt = rtf::looksLikeRtf(raw.toUtf8()) ? rtf::toPlainText(raw.toUtf8()).trimmed() : raw.trimmed();
        out.insert(id, {txt, fn});
        const QColor col = parseScrivColor(e.attribute(QStringLiteral("Color")));
        if (col.isValid() && colors)
            colors->insert(id, col);
    }
    return out;
}

void resolveLinkedNotes(QTextDocument *doc, const QHash<QString, QPair<QString, bool>> &notes,
                        const QHash<QString, QColor> &colors, int *count)
{
    QTextCursor c(doc);
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            QTextCharFormat cf = f.charFormat();
            if (NoteKind(cf.intProperty(props::NoteKind)) != NoteKind::Comment)
                continue;
            const QString id = cf.stringProperty(props::NoteId);
            if (!notes.contains(id))
                continue;
            const auto n = notes.value(id);
            if (cf.stringProperty(props::NoteText) == n.first)
                continue;
            text::clearNoteProps(cf);
            cf.merge(text::noteFormat(n.second ? NoteKind::LinkedFootnote : NoteKind::Comment, id, n.first,
                                      colors.value(id)));
            QTextCursor fc(doc);
            fc.setPosition(f.position());
            fc.setPosition(f.position() + f.length(), QTextCursor::KeepAnchor);
            fc.setCharFormat(cf);
            if (count)
                ++*count;
        }
    }
}

// Files/styles.xml of a Scrivener 3 project: <Style ID="..." Name="Heading 1" Type="Para+Char">;
// id (upper case) -> name
QHash<QString, QString> readStyleNames(const QString &scrivDir)
{
    QHash<QString, QString> out;
    QFile f(scrivDir + QLatin1String("/Files/styles.xml"));
    if (!f.open(QIODevice::ReadOnly))
        return out;
    QDomDocument dom;
    if (!dom.setContent(&f))
        return out;
    const QDomNodeList list = dom.elementsByTagName(QStringLiteral("Style"));
    for (int i = 0; i < list.size(); ++i) {
        const QDomElement e = list.at(i).toElement();
        const QString id = e.attribute(QStringLiteral("ID")).trimmed().toUpper();
        const QString name = e.attribute(QStringLiteral("Name")).trimmed();
        if (!id.isEmpty() && !name.isEmpty())
            out.insert(id, name);
    }
    return out;
}

// The styles a text file uses, in the order its "<$Scr_Ps::n>" marks count them: the file
// "content.styles" beside "content.rtf" (one line, ids separated by commas; seen in a project
// of Scrivener 3.1.6 for Windows, 2026-10-24), as names.
QStringList paragraphStylesOf(const QString &rtfPath, const QHash<QString, QString> &names)
{
    QStringList out;
    if (names.isEmpty())
        return out;
    const QFileInfo fi(rtfPath);
    QFile f(fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QLatin1String(".styles"));
    if (!f.open(QIODevice::ReadOnly))
        return out;
    static const QRegularExpression sep(QStringLiteral("[,\\s]+"));
    for (const QString &id : QString::fromUtf8(f.readAll()).split(sep, Qt::SkipEmptyParts))
        out << names.value(id.trimmed().toUpper());
    return out;
}

bool readRtfFile(Ctx &c, const QString &path, QTextDocument *doc)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    rtf::ReadOptions o;
    o.keepFormatting = c.p->storage == StorageFormat::Rtf;
    const rtf::ReadResult r = rtf::read(f.readAll(), doc, c.p->formatting, o);
    for (auto it = r.fontUsage.cbegin(); it != r.fontUsage.cend(); ++it)
        c.fontUsage[it.key()] += it.value();
    for (auto it = r.sizeUsage.cbegin(); it != r.sizeUsage.cend(); ++it)
        c.sizeUsage[it.key()] += it.value();
    ScrivenerImporter::stripMarkers(doc, c.p->formatting, paragraphStylesOf(path, c.styleNames));
    if (c.p->storage == StorageFormat::Markdown)
        display::normalizeDocument(doc, c.p->formatting);
    return r.ok;
}

ItemType mapType(const QString &t, const QString &ext)
{
    if (t == QLatin1String("DraftFolder"))
        return ItemType::Draft;
    if (t == QLatin1String("ResearchFolder"))
        return ItemType::Research;
    if (t == QLatin1String("TrashFolder"))
        return ItemType::Trash;
    if (t == QLatin1String("Folder"))
        return ItemType::Folder;
    if (t == QLatin1String("Text"))
        return ItemType::Text;
    if (t == QLatin1String("Image"))
        return ItemType::Image;
    if (t == QLatin1String("PDF"))
        return ItemType::Pdf;
    if (t == QLatin1String("WebArchive"))
        return ItemType::WebPage;
    const QString e = ext.toLower();
    static const QStringList media = {QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("m4a"),
                                      QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("avi"),
                                      QStringLiteral("ogg"), QStringLiteral("aac")};
    if (media.contains(e))
        return ItemType::Media;
    return ItemType::File;
}

int countItems(const QDomElement &parent)
{
    int n = 0;
    for (QDomElement e = parent.firstChildElement(QStringLiteral("BinderItem")); !e.isNull();
         e = e.nextSiblingElement(QStringLiteral("BinderItem"))) {
        ++n;
        n += countItems(e.firstChildElement(QStringLiteral("Children")));
    }
    return n;
}

void importItem(Ctx &c, const QDomElement &e, BinderItem *parent)
{
    const QString scrivId = e.attribute(QStringLiteral("UUID"), e.attribute(QStringLiteral("ID")));
    const QDomElement meta = e.firstChildElement(QStringLiteral("MetaData"));
    const QString ext = childText(meta, QStringLiteral("FileExtension"));
    const ItemType type = mapType(e.attribute(QStringLiteral("Type")), ext);
    const QString title = childText(e, QStringLiteral("Title"));
    Project *p = c.p;
    ++c.done;
    if (c.progress)
        c.progress(c.done, c.total, title);

    BinderItem *it = nullptr;
    if (type == ItemType::Draft || type == ItemType::Research || type == ItemType::Trash) {
        it = type == ItemType::Draft ? p->draft() : type == ItemType::Research ? p->research() : p->trash();
        // the special folders keep Scrivener's ids as well (written back unchanged)
        const QString uid = scrivId.toUpper();
        if (c.v3 && !uid.isEmpty() && it->id != uid && !p->item(uid)) {
            p->unregisterItem(it);
            it->id = uid;
            p->registerItem(it);
        }
    } else {
        it = new BinderItem;
        it->id = (c.v3 && !scrivId.isEmpty() && !p->item(scrivId.toUpper())) ? scrivId.toUpper() : newId();
        it->type = type;
        p->model()->insertExisting(parent, -1, it);
    }
    if (!title.isEmpty())
        it->title = title;
    it->created = parseScrivDate(e.attribute(QStringLiteral("Created")));
    it->modified = parseScrivDate(e.attribute(QStringLiteral("Modified")));
    if (!it->created.isValid())
        it->created = QDateTime::currentDateTime();
    if (!it->modified.isValid())
        it->modified = it->created;
    c.modifiedAt.insert(it, it->modified);
    if (!meta.isNull()) {
        const QString inc = childText(meta, QStringLiteral("IncludeInCompile"));
        if (!inc.isEmpty())
            it->includeInCompile = inc.compare(QLatin1String("Yes"), Qt::CaseInsensitive) == 0;
        bool ok = false;
        const int label = childText(meta, QStringLiteral("LabelID")).toInt(&ok);
        if (ok && p->labelById(label))
            it->label = label;
        const int status = childText(meta, QStringLiteral("StatusID")).toInt(&ok);
        if (ok && p->statusById(status))
            it->status = status;
        const QString st = childText(meta, QStringLiteral("SectionType")).trimmed();
        if (!st.isEmpty())
            it->sectionType = c.sectionTypeMap.value(st.toUpper());
        // Scrivener's icon names: "Notes (Yellow Notepad)", "Front Matter" ... (0.17: the icon set of
        // Scrivener's menu); own icons of the project from its Icons folder; others are kept by name
        const QString scrivIcon = childText(meta, QStringLiteral("IconFileName")).trimmed();
        if (!scrivIcon.isEmpty()) {
            QString icon = icons::fromScrivener(scrivIcon);
            if (icon.isEmpty())
                icon = customIconFromScriv(c, scrivIcon);
            if (icon.isEmpty())
                icon = QStringLiteral("scriv:") + scrivIcon;
            it->icon = icon;
        }
        // custom metadata
        const QDomElement cm = meta.firstChildElement(QStringLiteral("CustomMetaData"));
        for (QDomElement mi = cm.firstChildElement(QStringLiteral("MetaDataItem")); !mi.isNull();
             mi = mi.nextSiblingElement(QStringLiteral("MetaDataItem"))) {
            const QString fid = c.customFieldMap.value(childText(mi, QStringLiteral("FieldID")));
            if (!fid.isEmpty())
                it->custom.insert(fid, childText(mi, QStringLiteral("Value")));
        }
    }
    const QDomElement kws = e.firstChildElement(QStringLiteral("Keywords"));
    for (QDomElement k = kws.firstChildElement(); !k.isNull(); k = k.nextSiblingElement()) {
        const QString kid = c.keywordMap.value(k.text().trimmed());
        if (!kid.isEmpty() && !it->keywords.contains(kid))
            it->keywords << kid;
    }

    // --- a mind map of this program (0.17), kept beside Scrivener's files of the document
    if (!scrivId.isEmpty() && c.v3) {
        const QString map = contentBase(c, scrivId) + mindmap::scrivenerFileName();
        if (QFile::exists(map)) {
            QDir().mkpath(c.p->contentDir(it->id));
            const QString target = mindmap::pathFor(c.p->contentDir(it->id));
            QFile::remove(target);
            QFile::copy(map, target);
        }
    }

    // --- a text document with a file of its own (written so by this program for
    // audio, video, web pages and other files that Scrivener has no type for here)
    if (!scrivId.isEmpty() && c.v3 && type == ItemType::Text) {
        const QDir d(contentBase(c, scrivId));
        for (const QFileInfo &fi : d.entryInfoList({QStringLiteral("content.*")}, QDir::Files)) {
            const QString ext = fi.suffix().toLower();
            if (ext == QLatin1String("rtf") || ext == QLatin1String("comments") || ext == QLatin1String("styles")
                || ext == QLatin1String("txt"))
                continue;
            it->type = mediaSuffixes().contains(ext) ? ItemType::Media
                       : (ext == QLatin1String("html") || ext == QLatin1String("htm")) ? ItemType::WebPage
                                                                                        : ItemType::File;
            it->fileExt = ext;
            QDir().mkpath(p->contentDir(it->id));
            QFile::copy(fi.absoluteFilePath(), p->mediaFilePath(it));
            const QString pages = d.filePath(QStringLiteral("page-files"));
            if (QDir(pages).exists()) {
                QDir().mkpath(p->contentDir(it->id) + QLatin1String("/page-files"));
                for (const QFileInfo &pf : QDir(pages).entryInfoList(QDir::Files))
                    QFile::copy(pf.absoluteFilePath(), p->contentDir(it->id) + QLatin1String("/page-files/") + pf.fileName());
            }
            ++c.report->mediaFiles;
            break;
        }
    }

    // --- content
    if (!scrivId.isEmpty()) {
        const QString syn = findFile(c, scrivId, QStringLiteral("synopsis.txt"), QStringLiteral("_synopsis.txt"));
        if (!syn.isEmpty()) {
            QFile f(syn);
            if (f.open(QIODevice::ReadOnly))
                it->synopsis = QString::fromUtf8(f.readAll()).trimmed();
        }
        if (it->hasText()) {
            const QString content = findFile(c, scrivId, QStringLiteral("content.rtf"), QStringLiteral(".rtf"));
            if (!content.isEmpty()) {
                QTextDocument doc;
                if (readRtfFile(c, content, &doc)) {
                    const QString cmtPath = findFile(c, scrivId, QStringLiteral("content.comments"), QStringLiteral(".comments"));
                    if (!cmtPath.isEmpty()) {
                        QHash<QString, QColor> colors;
                        const auto notes = readComments(cmtPath, &colors);
                        resolveLinkedNotes(&doc, notes, colors, &c.report->comments);
                    }
                    p->docs()->setContent(it->id, &doc);
                    ++c.report->textDocuments;
                }
            }
            const QString notes = findFile(c, scrivId, QStringLiteral("notes.rtf"), QStringLiteral("_notes.rtf"));
            if (!notes.isEmpty()) {
                QTextDocument nd;
                if (readRtfFile(c, notes, &nd) && !nd.isEmpty())
                    p->docs()->setNotesContent(it->id, &nd);
            }
        } else {
            // media file
            QString media;
            if (c.v3) {
                QDir d(contentBase(c, scrivId));
                for (const QFileInfo &fi : d.entryInfoList(QDir::Files)) {
                    const QString n = fi.fileName().toLower();
                    if (n.startsWith(QLatin1String("content.")) && !n.endsWith(QLatin1String(".comments"))
                        && !n.endsWith(QLatin1String(".rtf")) && !n.endsWith(QLatin1String(".styles"))) {
                        media = fi.absoluteFilePath();
                        break;
                    }
                }
            } else if (!ext.isEmpty()) {
                const QString cand = contentBase(c, scrivId) + QLatin1Char('.') + ext;
                if (QFile::exists(cand))
                    media = cand;
            }
            if (!media.isEmpty()) {
                it->fileExt = QFileInfo(media).suffix().toLower();
                QDir().mkpath(p->contentDir(it->id));
                QFile::copy(media, p->mediaFilePath(it));
                ++c.report->mediaFiles;
            } else {
                c.report->warnings << QObject::tr("Media file of \"%1\" was not found.").arg(it->title);
            }
            const QString notes = findFile(c, scrivId, QStringLiteral("notes.rtf"), QStringLiteral("_notes.rtf"));
            if (!notes.isEmpty()) {
                QTextDocument nd;
                if (readRtfFile(c, notes, &nd) && !nd.isEmpty())
                    p->docs()->setNotesContent(it->id, &nd);
            }
        }
        // snapshots (Scrivener 3: Snapshots/<UUID>.snapshots/)
        if (c.v3 && it->hasText()) {
            QDir sd(c.scrivDir + QLatin1String("/Snapshots/") + scrivId + QLatin1String(".snapshots"));
            if (sd.exists()) {
                // index.xml: <Snapshot><Title/><Date>2025-10-27 02:20:40 -0700</Date></Snapshot>,
                // the files are named after the date: 2025-10-27-02-20-40-0700.rtf
                QHash<QString, QString> titles;
                QList<QPair<QDateTime, QString>> byDate;
                QFile idx(sd.filePath(QStringLiteral("index.xml")));
                if (idx.open(QIODevice::ReadOnly)) {
                    QDomDocument dom;
                    if (dom.setContent(&idx)) {
                        const QDomNodeList list = dom.elementsByTagName(QStringLiteral("Snapshot"));
                        for (int i = 0; i < list.size(); ++i) {
                            const QDomElement s = list.at(i).toElement();
                            QString file = s.attribute(QStringLiteral("File"), childText(s, QStringLiteral("File")));
                            if (file.isEmpty())
                                file = childText(s, QStringLiteral("Filename"));
                            if (!file.isEmpty())
                                titles.insert(QFileInfo(file).completeBaseName(), childText(s, QStringLiteral("Title")));
                            const QDateTime d = parseScrivDate(childText(s, QStringLiteral("Date")).trimmed());
                            if (d.isValid())
                                byDate.append({d, childText(s, QStringLiteral("Title"))});
                        }
                    }
                }
                const QFileInfoList snapFiles = sd.entryInfoList({QStringLiteral("*.rtf")}, QDir::Files, QDir::Name);
                int order = 0;
                for (const QFileInfo &fi : snapFiles) {
                    QTextDocument sdoc;
                    if (!readRtfFile(c, fi.absoluteFilePath(), &sdoc))
                        continue;
                    const QString bn = fi.completeBaseName();
                    QDateTime when;
                    if (bn.size() >= 19 && bn.at(10) == QLatin1Char('-')) {
                        // "2025-10-27-02-20-40-0700" -> "2025-10-27 02:20:40 -0700"
                        QString t = bn.left(10) + QLatin1Char(' ') + bn.mid(11, 8).replace(QLatin1Char('-'), QLatin1Char(':'));
                        if (bn.size() > 19)
                            t += QLatin1Char(' ') + bn.mid(19);
                        when = parseScrivDate(t);
                    }
                    if (!when.isValid())
                        when = parseScrivDate(QString(bn).replace(QLatin1Char('_'), QLatin1Char(':')));
                    if (!titles.contains(bn)) {
                        for (const auto &e : byDate)
                            if (when.isValid() && qAbs(e.first.secsTo(when)) < 2)
                                titles.insert(bn, e.second);
                        if (!titles.contains(bn) && byDate.size() == snapFiles.size())
                            titles.insert(bn, byDate.value(order).second); // same order as the files
                    }
                    ++order;
                    if (!when.isValid())
                        when = fi.lastModified();
                    const QString dir = p->snapshotsDir(it->id);
                    QDir().mkpath(dir);
                    QString name;
                    do {
                        name = when.toString(QStringLiteral("yyyyMMdd-HHmmss")) + QLatin1Char('.') + p->docs()->extension();
                        when = when.addSecs(1);
                    } while (QFile::exists(QDir(dir).filePath(name)));
                    QSaveFile out(QDir(dir).filePath(name));
                    if (out.open(QIODevice::WriteOnly)) {
                        out.write(p->docs()->serialize(it->id, &sdoc));
                        out.commit();
                        Snapshot snap;
                        snap.id = it->id;
                        snap.file = QDir(dir).filePath(name);
                        QString t = titles.value(fi.completeBaseName());
                        if (t.isEmpty())
                            t = QObject::tr("Imported snapshot");
                        snapshots::setTitle(p, snap, t);
                        it->hasSnapshots = true;
                        ++c.report->snapshots;
                    }
                }
            }
        }
    }

    const QDomElement children = e.firstChildElement(QStringLiteral("Children"));
    for (QDomElement ch = children.firstChildElement(QStringLiteral("BinderItem")); !ch.isNull();
         ch = ch.nextSiblingElement(QStringLiteral("BinderItem")))
        importItem(c, ch, it);
}

void importKeywords(Ctx &c, const QDomElement &parentEl, const QString &parentId)
{
    for (QDomElement k = parentEl.firstChildElement(QStringLiteral("Keyword")); !k.isNull();
         k = k.nextSiblingElement(QStringLiteral("Keyword"))) {
        Keyword kw;
        kw.id = newId();
        kw.name = k.firstChildElement(QStringLiteral("Title")).isNull() ? k.attribute(QStringLiteral("Title"), k.text())
                                                                          : childText(k, QStringLiteral("Title"));
        kw.color = parseScrivColor(k.firstChildElement(QStringLiteral("Color")).isNull()
                                       ? k.attribute(QStringLiteral("Color"))
                                       : childText(k, QStringLiteral("Color")));
        if (!kw.color.isValid())
            kw.color = QColor(160, 160, 160);
        kw.parentId = parentId;
        const QString sid = k.attribute(QStringLiteral("ID"));
        c.keywordMap.insert(sid, kw.id);
        c.p->keywords << kw;
        const QDomElement ch = k.firstChildElement(QStringLiteral("Children"));
        if (!ch.isNull())
            importKeywords(c, ch, kw.id);
    }
}

} // namespace

QString ScrivenerImporter::findScrivx(const QString &path)
{
    QFileInfo fi(path);
    if (fi.isFile() && fi.suffix().compare(QLatin1String("scrivx"), Qt::CaseInsensitive) == 0)
        return fi.absoluteFilePath();
    if (fi.isDir()) {
        const QStringList l = QDir(path).entryList({QStringLiteral("*.scrivx")}, QDir::Files);
        if (!l.isEmpty())
            return QDir(path).filePath(l.first());
    }
    return QString();
}

std::unique_ptr<Project> ScrivenerImporter::importProject(const QString &path, const QString &targetParentDir,
                                                          const QString &name, StorageFormat storage,
                                                          ScrivenerImportReport *reportIn, QString *error,
                                                          const Progress &progress)
{
    ScrivenerImportReport localReport;
    ScrivenerImportReport *report = reportIn ? reportIn : &localReport;
    const QString scrivx = findScrivx(path);
    if (scrivx.isEmpty()) {
        if (error)
            *error = QObject::tr("No Scrivener project (.scrivx) was found in:\n%1").arg(path);
        return nullptr;
    }
    QFile f(scrivx);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot read %1").arg(scrivx);
        return nullptr;
    }
    QDomDocument dom;
    QString msg;
    int line = 0;
    if (!dom.setContent(&f, &msg, &line)) {
        if (error)
            *error = QObject::tr("The Scrivener project file could not be read (%1, line %2).").arg(msg).arg(line);
        return nullptr;
    }
    const QDomElement root = dom.documentElement();
    report->scrivenerVersion = root.attribute(QStringLiteral("Version")) + QLatin1Char(' ')
                               + root.attribute(QStringLiteral("Creator"));

    auto p = Project::create(targetParentDir, name, error);
    if (!p)
        return nullptr;
    p->storage = storage;
    if (s_haveNewFormatting) {
        // the writer's formatting for new projects (0.21.2); the font family is found below
        p->formatting.fontFamily = s_newFormatting.fontFamily;
        p->formatting.fontSize = s_newFormatting.fontSize;
        p->formatting.lineSpacing = s_newFormatting.lineSpacing;
        p->formatting.firstLineIndent = s_newFormatting.firstLineIndent;
        p->formatting.paragraphSpacing = s_newFormatting.paragraphSpacing;
        p->formatting.align = s_newFormatting.align;
    }

    Ctx c;
    c.p = p.get();
    c.scrivDir = QFileInfo(scrivx).absolutePath();
    c.v3 = QDir(c.scrivDir + QLatin1String("/Files/Data")).exists();
    c.styleNames = readStyleNames(c.scrivDir);
    if (!c.v3 && !QDir(c.scrivDir + QLatin1String("/Files/Docs")).exists())
        report->warnings << QObject::tr("The content folder (Files/Data or Files/Docs) was not found; only the binder "
                                        "structure can be imported.");
    c.report = report;
    c.progress = progress;

    // labels
    const QDomElement labelSettings = root.firstChildElement(QStringLiteral("LabelSettings"));
    if (!labelSettings.isNull()) {
        const QString t = childText(labelSettings, QStringLiteral("Title"));
        if (!t.isEmpty())
            p->labelTitle = t;
        p->labels.clear();
        const QDomElement labels = labelSettings.firstChildElement(QStringLiteral("Labels"));
        for (QDomElement l = labels.firstChildElement(QStringLiteral("Label")); !l.isNull();
             l = l.nextSiblingElement(QStringLiteral("Label"))) {
            const int id = l.attribute(QStringLiteral("ID")).toInt();
            if (id < 0)
                continue;
            QColor col = parseScrivColor(l.attribute(QStringLiteral("Color")));
            if (!col.isValid())
                col = QColor(150, 150, 150);
            p->labels.append({id, l.text().trimmed(), col});
        }
    }
    // statuses
    const QDomElement statusSettings = root.firstChildElement(QStringLiteral("StatusSettings"));
    if (!statusSettings.isNull()) {
        const QString t = childText(statusSettings, QStringLiteral("Title"));
        if (!t.isEmpty())
            p->statusTitle = t;
        p->statuses.clear();
        const QDomElement items = statusSettings.firstChildElement(QStringLiteral("StatusItems"));
        for (QDomElement s = items.firstChildElement(QStringLiteral("Status")); !s.isNull();
             s = s.nextSiblingElement(QStringLiteral("Status"))) {
            const int id = s.attribute(QStringLiteral("ID")).toInt();
            if (id < 0)
                continue;
            p->statuses.append({id, s.text().trimmed()});
        }
    }
    // keywords
    const QDomElement kwRoot = root.firstChildElement(QStringLiteral("Keywords"));
    if (!kwRoot.isNull())
        importKeywords(c, kwRoot, QString());
    // custom metadata fields
    const QDomElement cms = root.firstChildElement(QStringLiteral("CustomMetaDataSettings"));
    for (QDomElement mf = cms.firstChildElement(QStringLiteral("MetaDataField")); !mf.isNull();
         mf = mf.nextSiblingElement(QStringLiteral("MetaDataField"))) {
        CustomField cf;
        cf.id = newId();
        cf.title = childText(mf, QStringLiteral("Title"));
        const QString type = mf.attribute(QStringLiteral("Type"));
        cf.type = type == QLatin1String("Checkbox") ? CustomFieldType::Checkbox
                  : type == QLatin1String("List")   ? CustomFieldType::List
                  : type == QLatin1String("Date")   ? CustomFieldType::Date
                                                    : CustomFieldType::Text;
        const QDomElement opts = mf.firstChildElement(QStringLiteral("ListOptions"));
        for (QDomElement o = opts.firstChildElement(); !o.isNull(); o = o.nextSiblingElement())
            cf.options << o.text();
        c.customFieldMap.insert(mf.attribute(QStringLiteral("ID")), cf.id);
        p->customFields << cf;
    }
    // section types: SectionTypes/TypeDefinitions/Type ID="..." (Scrivener 3);
    // the ids are kept, so the project can be written back with them
    QList<SectionType> types;
    const QDomElement stRoot = root.firstChildElement(QStringLiteral("SectionTypes"));
    const QDomElement defs = stRoot.firstChildElement(QStringLiteral("TypeDefinitions"));
    for (QDomElement se = defs.firstChildElement(QStringLiteral("Type")); !se.isNull();
         se = se.nextSiblingElement(QStringLiteral("Type"))) {
        const QString sid = se.attribute(QStringLiteral("ID")).toUpper();
        if (sid.isEmpty() || se.text().trimmed().isEmpty() || c.sectionTypeMap.contains(sid))
            continue;
        SectionType st{sid, se.text().trimmed()};
        c.sectionTypeMap.insert(sid, st.id);
        types << st;
    }
    if (types.isEmpty()) {
        // other layouts seen in older notes: <SectionType ID="...">Name</SectionType>
        const QDomNodeList stNodes = root.elementsByTagName(QStringLiteral("SectionType"));
        for (int i = 0; i < stNodes.size(); ++i) {
            const QDomElement se = stNodes.at(i).toElement();
            const QString sid = se.attribute(QStringLiteral("ID")).toUpper();
            if (sid.isEmpty() || se.text().trimmed().isEmpty() || c.sectionTypeMap.contains(sid))
                continue;
            SectionType st{newId(), se.text().trimmed()};
            c.sectionTypeMap.insert(sid, st.id);
            types << st;
        }
    }
    const QDomElement levelTypes = stRoot.firstChildElement(QStringLiteral("LevelTypes"));
    if (!types.isEmpty() && !levelTypes.isNull()) {
        p->sectionTypes = types;
        auto read = [&](const QString &name) {
            QStringList out;
            const QDomElement l = levelTypes.firstChildElement(name);
            for (QDomElement t = l.firstChildElement(QStringLiteral("Type")); !t.isNull(); t = t.nextSiblingElement(QStringLiteral("Type")))
                out << c.sectionTypeMap.value(t.text().trimmed().toUpper(), types.first().id);
            return out;
        };
        p->structureDefaults.folders = read(QStringLiteral("Folders"));
        p->structureDefaults.fileGroups = read(QStringLiteral("Containers"));
        p->structureDefaults.files = read(QStringLiteral("Files"));
    } else if (!types.isEmpty()) {
        p->sectionTypes = types;
        auto find = [&](const QStringList &names) -> QString {
            for (const QString &n : names)
                for (const SectionType &s : types)
                    if (s.name.compare(n, Qt::CaseInsensitive) == 0)
                        return s.id;
            return types.first().id;
        };
        p->structureDefaults.folders = {find({QStringLiteral("Chapter"), QStringLiteral("Heading"), QStringLiteral("Part")})};
        const QString fileType = find({QStringLiteral("Scene"), QStringLiteral("Section"), QStringLiteral("Text")});
        p->structureDefaults.files = {fileType};
        p->structureDefaults.fileGroups = {fileType};
    }
    // targets
    const QDomElement tg = root.firstChildElement(QStringLiteral("ProjectTargets"));
    if (!tg.isNull()) {
        const QDomElement dt = tg.firstChildElement(QStringLiteral("DraftTarget"));
        p->targets.draftTarget = dt.text().toInt();
        p->targets.draftChars = dt.attribute(QStringLiteral("Type")).startsWith(QLatin1String("Char"));
        const QDomElement st = tg.firstChildElement(QStringLiteral("SessionTarget"));
        p->targets.sessionTarget = st.text().toInt();
        p->targets.sessionChars = st.attribute(QStringLiteral("Type")).startsWith(QLatin1String("Char"));
    }

    // Sample some documents first to find the body font, so that text in
    // that font is stored without explicit font attributes.
    {
        QStringList samples;
        if (c.v3) {
            const QDir data(c.scrivDir + QLatin1String("/Files/Data"));
            for (const QString &d : data.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                const QString f = data.filePath(d + QLatin1String("/content.rtf"));
                if (QFile::exists(f))
                    samples << f;
                if (samples.size() >= 40)
                    break;
            }
        } else {
            const QDir docs(c.scrivDir + QLatin1String("/Files/Docs"));
            for (const QString &f : docs.entryList({QStringLiteral("*.rtf")}, QDir::Files)) {
                if (!f.contains(QLatin1Char('_')))
                    samples << docs.filePath(f);
                if (samples.size() >= 40)
                    break;
            }
        }
        QHash<QString, int> fonts;
        QHash<int, int> sizes;
        for (const QString &path : samples) {
            QFile sf(path);
            if (!sf.open(QIODevice::ReadOnly))
                continue;
            QTextDocument tmp;
            const rtf::ReadResult r = rtf::read(sf.readAll(), &tmp, p->formatting);
            for (auto it = r.fontUsage.cbegin(); it != r.fontUsage.cend(); ++it)
                fonts[it.key()] += it.value();
            for (auto it = r.sizeUsage.cbegin(); it != r.sizeUsage.cend(); ++it)
                sizes[it.key()] += it.value();
        }
        int best = 0;
        for (auto it = fonts.cbegin(); it != fonts.cend(); ++it)
            if (it.value() > best) {
                best = it.value();
                p->formatting.fontFamily = it.key();
            }
        // the size: the writer's size for new projects when there is one (0.21.2), else the one
        // most used in the Scrivener project
        best = 0;
        if (!s_haveNewFormatting)
            for (auto it = sizes.cbegin(); it != sizes.cend(); ++it)
                if (it.value() > best && it.key() >= 12 && it.key() <= 48) {
                    best = it.value();
                    p->formatting.fontSize = it.key() / 2.0;
                }
    }

    // binder
    const QDomElement binder = root.firstChildElement(QStringLiteral("Binder"));
    c.total = countItems(binder);
    // Scrivener's order of the top level is kept (a project may have documents
    // before the Draft); the new project starts with Draft, Research, Trash.
    // Found in 0.14.0: written back in this program's order, the binder of the
    // .scriv changed its order at the first save.
    QList<BinderItem *> topOrder;
    for (QDomElement e = binder.firstChildElement(QStringLiteral("BinderItem")); !e.isNull();
         e = e.nextSiblingElement(QStringLiteral("BinderItem"))) {
        const QList<BinderItem *> before = p->root()->children;
        importItem(c, e, p->root());
        const ItemType t = mapType(e.attribute(QStringLiteral("Type")),
                                   childText(e.firstChildElement(QStringLiteral("MetaData")), QStringLiteral("FileExtension")));
        BinderItem *added = t == ItemType::Draft ? p->draft() : t == ItemType::Research ? p->research()
                                                                  : t == ItemType::Trash ? p->trash() : nullptr;
        for (BinderItem *x : p->root()->children)
            if (!added && !before.contains(x))
                added = x;
        if (added && !topOrder.contains(added))
            topOrder << added;
    }
    {
        QList<BinderItem *> ordered = topOrder;
        for (BinderItem *x : p->root()->children)
            if (!ordered.contains(x))
                ordered << x;
        p->root()->children = ordered;
    }

    // arbitrary collections
    const QDomElement cols = root.firstChildElement(QStringLiteral("Collections"));
    for (QDomElement ce = cols.firstChildElement(QStringLiteral("Collection")); !ce.isNull();
         ce = ce.nextSiblingElement(QStringLiteral("Collection"))) {
        const QString type = ce.attribute(QStringLiteral("Type"));
        if (type != QLatin1String("Arbitrary"))
            continue;
        Collection col;
        col.id = newId();
        col.name = childText(ce, QStringLiteral("Title"));
        col.color = parseScrivColor(ce.attribute(QStringLiteral("Color")));
        const QDomNodeList refs = ce.elementsByTagName(QStringLiteral("BinderItem"));
        for (int i = 0; i < refs.size(); ++i) {
            const QString uid = refs.at(i).toElement().attribute(QStringLiteral("UUID")).toUpper();
            if (p->item(uid))
                col.items << uid;
        }
        if (!col.name.isEmpty())
            p->collections << col;
    }
    // project bookmarks (best effort: any UUID references)
    const QDomElement bms = root.firstChildElement(QStringLiteral("ProjectBookmarks"));
    const QDomNodeList bmRefs = bms.elementsByTagName(QStringLiteral("Bookmark"));
    for (int i = 0; i < bmRefs.size(); ++i) {
        const QDomElement b = bmRefs.at(i).toElement();
        QString target = b.attribute(QStringLiteral("BinderUUID"), b.attribute(QStringLiteral("UUID"), b.text())).trimmed();
        if (p->item(target.toUpper()))
            p->projectBookmarks.append({p->item(target.toUpper())->title, QStringLiteral("qb://") + target.toUpper()});
    }

    {
        const QString tf = childText(root, QStringLiteral("TemplateFolderUUID")).trimmed().toUpper();
        if (p->item(tf))
            p->templatesFolderId = tf;
        const QString bf = childText(root, QStringLiteral("BookmarksFolderUUID")).trimmed().toUpper();
        if (p->item(bf))
            p->notesFolderId = bf;
    }
    p->docs()->applyFormatting();
    p->docs()->recountAll();
    p->model()->customFieldsChanged();
    p->startSession(true);
    // reading texts, notes and comments counts as a change of the documents; the
    // dates stay those of Scrivener (otherwise the .scrivx of a project in
    // Scrivener format got new dates for untouched documents; 0.14.0 checks)
    for (auto it = c.modifiedAt.cbegin(); it != c.modifiedAt.cend(); ++it)
        if (p->item(it.key()->id) == it.key())
            it.key()->modified = it.value();
    if (!p->save(error))
        report->warnings << QObject::tr("Saving the imported project reported a problem.");
    return p;
}

void ScrivenerImporter::setNewProjectFormatting(const Formatting &f)
{
    s_newFormatting = f;
    s_haveNewFormatting = true;
}

void ScrivenerImporter::clearNewProjectFormatting()
{
    s_newFormatting = Formatting();
    s_haveNewFormatting = false;
}

QString ScrivenerImporter::scrivenerStyleName(const Formatting &f, const QString &scrivenerName)
{
    const QString n = scrivenerName.trimmed();
    if (n.isEmpty())
        return QString();
    if (const Style *s = styles::find(f, n))
        return s->name;
    // Scrivener's standard names that this program calls otherwise
    static const QHash<QString, QString> other = {{QStringLiteral("centered text"), QStringLiteral("Centered")},
                                                  {QStringLiteral("centred text"), QStringLiteral("Centered")},
                                                  {QStringLiteral("blockquote"), QStringLiteral("Block Quote")},
                                                  {QStringLiteral("quote"), QStringLiteral("Block Quote")}};
    const QString mapped = other.value(n.toLower());
    if (!mapped.isEmpty())
        if (const Style *s = styles::find(f, mapped))
            return s->name;
    return QString();
}

int ScrivenerImporter::stripMarkers(QTextDocument *doc, const Formatting &f, const QStringList &paragraphStyles)
{
    if (!doc || !doc->toPlainText().contains(QLatin1String("$Scr")))
        return 0;
    // <$Scr_H::1>, <!$Scr_H::1>, <$Scr_Ps::0>, <!$Scr_Ps::0>, <$Scr_Cs::0>, <$ScrKeepWithNext> ...
    static const QRegularExpression re(QStringLiteral("<(!?)\\$Scr([A-Za-z_]*)(?:::([0-9]+))?>"));
    int removed = 0;
    int openHeading = 0;  // a heading mark open at the end of the paragraph before
    int openStyle = -1;   // a paragraph style mark open ...
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QString t = b.text();
        int heading = openHeading;   // what this paragraph is in
        int style = openStyle;
        bool headingHere = false, styleHere = false; // opened in this paragraph
        QList<QPair<int, int>> spans;
        if (t.contains(QLatin1String("$Scr"))) {
            auto it = re.globalMatch(t);
            int visibleBefore = 0; // characters of text (not marks, not spaces) before the mark
            int last = 0;
            while (it.hasNext()) {
                const auto m = it.next();
                visibleBefore += t.mid(last, m.capturedStart() - last).trimmed().size();
                last = m.capturedEnd();
                const bool close = !m.captured(1).isEmpty();
                const QString kind = m.captured(2);
                const int n = m.captured(3).isEmpty() ? -1 : m.captured(3).toInt();
                if (kind == QLatin1String("_H")) {
                    if (!close) {
                        openHeading = n;
                        if (!headingHere && (heading <= 0 || visibleBefore == 0)) {
                            heading = n;
                            headingHere = true;
                        }
                    } else {
                        // a mark closed at the very start belongs to the paragraph before
                        if (!headingHere && visibleBefore == 0)
                            heading = 0;
                        openHeading = 0;
                    }
                } else if (kind == QLatin1String("_Ps")) {
                    if (!close) {
                        openStyle = n;
                        if (!styleHere && (style < 0 || visibleBefore == 0)) {
                            style = n;
                            styleHere = true;
                        }
                    } else {
                        if (!styleHere && visibleBefore == 0)
                            style = -1;
                        openStyle = -1;
                    }
                }
                spans.append({m.capturedStart(), m.capturedLength()});
            }
        }
        for (int i = int(spans.size()) - 1; i >= 0; --i) {
            QTextCursor cur(doc);
            cur.setPosition(b.position() + spans[i].first);
            cur.setPosition(b.position() + spans[i].first + spans[i].second, QTextCursor::KeepAnchor);
            cur.removeSelectedText();
            ++removed;
        }
        if (!b.blockFormat().stringProperty(props::Style).isEmpty())
            continue;
        QString name;
        if (heading >= 1 && heading <= 6)
            name = styles::forHeadingLevel(heading);
        else if (style >= 0 && style < paragraphStyles.size())
            name = scrivenerStyleName(f, paragraphStyles.at(style));
        if (name.isEmpty())
            continue;
        QTextCursor cur(doc);
        cur.setPosition(b.position());
        QTextBlockFormat bf = b.blockFormat();
        bf.setProperty(props::Style, name);
        cur.setBlockFormat(bf);
    }
    return removed;
}

int ScrivenerImporter::removeLeftoverMarkers(Project *p, QString *error)
{
    if (!p || !p->scrivenerPath.isEmpty())
        return 0;
    int changed = 0;
    for (BinderItem *it : p->allItems()) {
        bool itemChanged = false;
        for (const bool notes : {false, true}) {
            QString path = notes ? p->docs()->notesPath(it->id) : p->docs()->textPath(it->id);
            if (!QFile::exists(path)) {
                // the other storage format (a project converted between Markdown and RTF)
                const QString other = path.endsWith(QLatin1String(".md")) ? path.left(path.size() - 2) + QLatin1String("rtf")
                                                                           : path.left(path.size() - 3) + QLatin1String("md");
                if (!QFile::exists(other))
                    continue;
                path = other;
            }
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QByteArray data = f.readAll();
            f.close();
            // also as Markdown writes them ("\<$Scr\_Ps::0\>")
            if (!data.contains("$Scr") && !data.contains("Scr_") && !data.contains("Scr\\_"))
                continue;
            const int n = p->docs()->editStored(
                it->id, notes,
                [p](QTextDocument *d) {
                    const int k = stripMarkers(d, p->formatting);
                    if (k > 0 && p->storage == StorageFormat::Markdown)
                        display::normalizeDocument(d, p->formatting);
                    return k;
                },
                error);
            itemChanged = itemChanged || n > 0;
        }
        if (itemChanged)
            ++changed;
    }
    return changed;
}

} // namespace qb
