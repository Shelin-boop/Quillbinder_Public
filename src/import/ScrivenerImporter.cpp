#include "core/PathSafety.h"
#include "core/SafeSave.h"
#include "import/ScrivenerImporter.h"
#include "import/ScrivenerFormat.h"
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
#include <QUuid>
#include <algorithm>

namespace qb {

namespace {

// File > Options > Editing > Formatting, handed over by AppSettings (0.21.2)
bool s_haveNewFormatting = false;
Formatting s_newFormatting;

// 0.64.2: the temporary file of a write that was cut off (Qt's QSaveFile adds ".XXXXXX" to the name)
bool isSaveLeftover(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^content\\.(rtf|comments|styles|txt)\\.[A-Za-z0-9]{6}$"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re.match(name).hasMatch();
}

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
    QHash<QString, CustomFieldType> fieldType;                 // our field id -> type (0.81.2)
    QHash<QString, QHash<QString, QString>> fieldOptions;      // our field id -> option ID -> text (0.81.2)
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

// 0.81.3: Scrivener's styles that the project has no style of that name for become styles of the project, with the look
// of their Format (an RTF paragraph in Files/styles.xml): paragraph styles ("Para", "Para+Char") and character styles
// ("Char"). So the marks of a text keep their style, and a text written back keeps them.
void importStyles(Ctx &c)
{
    QFile f(c.scrivDir + QLatin1String("/Files/styles.xml"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    QDomDocument dom;
    if (!dom.setContent(&f))
        return;
    StyleSheet sheet = styles::sheet(c.p->formatting);
    bool added = false;
    const QDomNodeList list = dom.elementsByTagName(QStringLiteral("Style"));
    for (int i = 0; i < list.size(); ++i) {
        const QDomElement e = list.at(i).toElement();
        const QString name = e.attribute(QStringLiteral("Name")).trimmed();
        const QString type = e.attribute(QStringLiteral("Type"));
        const bool character = type == QLatin1String("Char");
        if (name.isEmpty() || !styles::validName(name) || styles::isFixedName(name) || sheet.indexOf(name) >= 0)
            continue;
        if (character ? styles::findChar(c.p->formatting, name) != nullptr
                      : !ScrivenerImporter::scrivenerStyleName(c.p->formatting, name).isEmpty())
            continue;
        QTextDocument d;
        rtf::ReadOptions o;
        o.keepFormatting = true;
        rtf::read(e.firstChildElement(QStringLiteral("Format")).text().toUtf8(), &d, c.p->formatting, o);
        const QTextBlock b = d.begin();
        const QTextCharFormat cf = b.begin() != b.end() ? b.begin().fragment().charFormat() : b.charFormat();
        const QString change = e.attribute(QStringLiteral("FontChange"));
        styles::CaptureOptions opt;
        opt.character = character;
        opt.withChars = character || type.contains(QLatin1String("Char"));
        opt.withFamily = change.contains(QLatin1String("Face"));
        opt.withSize = change.contains(QLatin1String("Size"));
        sheet.styles.append(styles::capture(name, b.blockFormat(), cf, c.p->formatting, opt));
        added = true;
    }
    if (added)
        c.p->formatting = styles::withSheet(c.p->formatting, sheet);
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
    ScrivenerImporter::convertInlineNotes(doc);
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
    // 0.64.1 (audit S-1): the id names the item's folder in the .scriv; one that could act as a path
    // ("../x") reads nothing there and is not taken over
    QString scrivId = e.attribute(QStringLiteral("UUID"), e.attribute(QStringLiteral("ID")));
    if (!pathsafety::isSafeId(scrivId))
        scrivId.clear();
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
        // 0.64.1 (audit S-1): an id that could act as a path is not taken over
        if (c.v3 && !uid.isEmpty() && pathsafety::isSafeId(uid) && it->id != uid && !p->item(uid)) {
            p->unregisterItem(it);
            it->id = uid;
            p->registerItem(it);
        }
    } else {
        it = new BinderItem;
        it->id = (c.v3 && !scrivId.isEmpty() && pathsafety::isSafeId(scrivId) && !p->item(scrivId.toUpper())) ? scrivId.toUpper() : newId();
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
                // 0.81.2: in this program's form ("Yes" -> "1", an option's ID -> its text, a date -> yyyy-MM-dd)
                it->custom.insert(fid, scrivener::valueFromScrivener(c.fieldType.value(fid), childText(mi, QStringLiteral("Value")),
                                                                     c.fieldOptions.value(fid)));
        }
    }
    const QDomElement kws = e.firstChildElement(QStringLiteral("Keywords"));
    for (QDomElement k = kws.firstChildElement(); !k.isNull(); k = k.nextSiblingElement()) {
        const QString kid = c.keywordMap.value(k.text().trimmed());
        if (!kid.isEmpty() && !it->keywords.contains(kid))
            it->keywords << kid;
    }
    // 0.90.0: the picture of an index card (Scrivener for Windows: "card-image.<ext>" beside the text's files, the switch
    // ShowSynopsisImage and the ending in IndexCardImageFileExtension); a picture, PDF or media document's switch means
    // something else there (its own file on the card) and is left alone
    if (!scrivId.isEmpty() && c.v3 && (it->hasText() || type == ItemType::Folder)
        && childText(meta, QStringLiteral("ShowSynopsisImage")).compare(QLatin1String("Yes"), Qt::CaseInsensitive) == 0) {
        QString ext = childText(meta, QStringLiteral("IndexCardImageFileExtension")).trimmed().toLower();
        QString src = contentBase(c, scrivId) + QLatin1String("card-image.") + ext;
        if (ext.isEmpty() || !QFile::exists(src)) {
            const QFileInfoList found = QDir(contentBase(c, scrivId)).entryInfoList({QStringLiteral("card-image.*")}, QDir::Files);
            src = found.isEmpty() ? QString() : found.first().absoluteFilePath();
            ext = found.isEmpty() ? QString() : found.first().suffix().toLower();
        }
        if (!src.isEmpty() && pathsafety::isSafeExtension(ext)) {
            QDir().mkpath(p->contentDir(it->id));
            const QString name = QStringLiteral("card.") + ext;
            QFile::remove(p->contentDir(it->id) + QLatin1Char('/') + name);
            if (QFile::copy(src, p->contentDir(it->id) + QLatin1Char('/') + name))
                it->synopsisImage = name;
        }
    }
    // 0.81.3: a web page's address (Scrivener for Windows keeps it with the page)
    const QString originalUrl = childText(e.firstChildElement(QStringLiteral("MediaSettings")), QStringLiteral("OriginalURL")).trimmed();
    if (!originalUrl.isEmpty())
        it->sourceUrl = originalUrl;

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
            if (isSaveLeftover(fi.fileName()))
                continue;   // 0.64.2: a write cut off (content.rtf.Ab12Cd) is not a file of another kind
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
            {
                // 0.64.2: as written, except line breaks at the end (it was trimmed: a synopsis starting
                // with a space lost it on every save into the .scriv)
                QString text = QString::fromUtf8(f.readAll());
                while (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char('\r')))
                    text.chop(1);
                it->synopsis = text;
            }
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
                    if (isSaveLeftover(fi.fileName()))
                        continue;
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
                        safesave::write(out, p->docs()->serialize(it->id, &sdoc));
                        safesave::commit(out);
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
        // 0.81.2: Scrivener's id is kept as ours, so the keyword is written back under it
        const QString scrivId = k.attribute(QStringLiteral("ID")).trimmed();
        kw.id = !scrivId.isEmpty() && !c.p->keywordById(scrivId) ? scrivId : newId();
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
    importStyles(c);
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
        // 0.81.2: Scrivener's ID is kept as ours, so the field is written back under it
        const QString scrivId = mf.attribute(QStringLiteral("ID")).trimmed();
        cf.id = !scrivId.isEmpty() && !p->customFieldById(scrivId) ? scrivId : newId();
        cf.title = childText(mf, QStringLiteral("Title"));
        const QString type = mf.attribute(QStringLiteral("Type"));
        cf.type = type == QLatin1String("Checkbox") ? CustomFieldType::Checkbox
                  : type == QLatin1String("List")   ? CustomFieldType::List
                  : type == QLatin1String("Date")   ? CustomFieldType::Date
                                                    : CustomFieldType::Text;
        QHash<QString, QString> optionText;
        const QDomElement opts = mf.firstChildElement(QStringLiteral("ListOptions"));
        for (QDomElement o = opts.firstChildElement(); !o.isNull(); o = o.nextSiblingElement()) {
            cf.options << o.text();
            if (o.hasAttribute(QStringLiteral("ID")))
                optionText.insert(o.attribute(QStringLiteral("ID")).trimmed(), o.text());
        }
        if (cf.type == CustomFieldType::Text && mf.hasAttribute(QStringLiteral("Wraps")))
            cf.wrap = mf.attribute(QStringLiteral("Wraps")) == QLatin1String("Yes");
        if (cf.type == CustomFieldType::Checkbox && mf.hasAttribute(QStringLiteral("Default")))
            cf.defaultValue = scrivener::valueFromScrivener(cf.type, mf.attribute(QStringLiteral("Default")), {});
        c.customFieldMap.insert(scrivId, cf.id);
        c.fieldType.insert(cf.id, cf.type);
        c.fieldOptions.insert(cf.id, optionText);
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
        // 0.90.0: the deadline (it was never read, and the next write of the project file switched Scrivener's off)
        if (dt.hasAttribute(QStringLiteral("Deadline"))
            && dt.attribute(QStringLiteral("IgnoreDeadline")).compare(QLatin1String("Yes"), Qt::CaseInsensitive) != 0) {
            const QDate day = QDate::fromString(dt.attribute(QStringLiteral("Deadline")).left(10), QStringLiteral("yyyy-MM-dd"));
            if (day.isValid())
                p->targets.deadline = day;
        }
        const QDomElement st = tg.firstChildElement(QStringLiteral("SessionTarget"));
        p->targets.sessionTarget = st.text().toInt();
        p->targets.sessionChars = st.attribute(QStringLiteral("Type")).startsWith(QLatin1String("Char"));
        // 0.90.0: two options of the session target: whether only the draft counts, and whether the target is worked
        // out from the deadline (the other options of Scrivener stay in the file as they are)
        if (st.hasAttribute(QStringLiteral("CountDraftOnly")))
            p->targets.countAnywhere = st.attribute(QStringLiteral("CountDraftOnly")).compare(QLatin1String("Yes"), Qt::CaseInsensitive) != 0;
        if (st.hasAttribute(QStringLiteral("DeterminedFromDeadline")))
            p->targets.autoFromDeadline = st.attribute(QStringLiteral("DeterminedFromDeadline")).compare(QLatin1String("Yes"), Qt::CaseInsensitive) == 0;
    }

    // 0.90.0: the writing history (Files/writing.history: <Day dwc=".." dcc=".." owc=".." occ="..">yyyy-MM-dd</Day>, the
    // words and characters written that day in the draft and elsewhere); taken over at the end, after the texts were
    // read (reading them counts as writing them)
    QList<WritingDay> fileHistory;
    if (c.v3) {
        QFile hf(c.scrivDir + QLatin1String("/Files/writing.history"));
        QDomDocument hd;
        if (hf.open(QIODevice::ReadOnly) && hd.setContent(hf.readAll())) {
            QList<WritingDay> days;
            for (QDomElement d = hd.documentElement().firstChildElement(QStringLiteral("Day")); !d.isNull(); d = d.nextSiblingElement(QStringLiteral("Day"))) {
                WritingDay w;
                w.date = QDate::fromString(d.text().trimmed(), QStringLiteral("yyyy-MM-dd"));
                w.draftWords = d.attribute(QStringLiteral("dwc")).toInt();
                w.draftChars = d.attribute(QStringLiteral("dcc")).toInt();
                w.otherWords = d.attribute(QStringLiteral("owc")).toInt();
                w.otherChars = d.attribute(QStringLiteral("occ")).toInt();
                if (w.date.isValid() && std::none_of(days.cbegin(), days.cend(), [&](const WritingDay &x) { return x.date == w.date; }))
                    days << w;
            }
            std::sort(days.begin(), days.end(), [](const WritingDay &a, const WritingDay &b) { return a.date < b.date; });
            fileHistory = days;
        }
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
        if (type == QLatin1String("SavedSearch")) {
            // 0.90.0: a saved search (Collection Type="SavedSearch" with its SearchSettings) is a search collection here
            Collection col;
            const QString scrivId = ce.attribute(QStringLiteral("ID")).trimmed().toUpper();
            const bool idFree = !scrivId.isEmpty()
                                && std::none_of(p->collections.cbegin(), p->collections.cend(), [&](const Collection &x) { return x.id == scrivId; });
            col.id = idFree ? scrivId : newId();
            col.name = childText(ce, QStringLiteral("Title"));
            col.color = parseScrivColor(ce.attribute(QStringLiteral("Color")));
            col.isSearch = true;
            col.query = scrivener::queryFromScrivener(ce.firstChildElement(QStringLiteral("SearchSettings")));
            p->collections << col;
            continue;
        }
        if (type != QLatin1String("Arbitrary"))
            continue;
        Collection col;
        // 0.81.2: Scrivener's ID is kept as ours, so the collection is written back under it
        const QString scrivId = ce.attribute(QStringLiteral("ID")).trimmed().toUpper();
        const bool idFree = !scrivId.isEmpty()
                            && std::none_of(p->collections.cbegin(), p->collections.cend(), [&](const Collection &x) { return x.id == scrivId; });
        col.id = idFree ? scrivId : newId();
        col.name = childText(ce, QStringLiteral("Title"));
        col.color = parseScrivColor(ce.attribute(QStringLiteral("Color")));
        // Scrivener 3.1.6.0 for Windows lists the documents in one element, comma-separated (0.81.2); a list of
        // BinderItem elements is read as before
        QStringList uids;
        const QDomElement list = ce.firstChildElement(QStringLiteral("BinderUUIDs"));
        for (const QString &u : list.text().split(QLatin1Char(','), Qt::SkipEmptyParts))
            uids << u.trimmed().toUpper();
        const QDomNodeList refs = ce.elementsByTagName(QStringLiteral("BinderItem"));
        for (int i = 0; i < refs.size(); ++i)
            uids << refs.at(i).toElement().attribute(QStringLiteral("UUID")).toUpper();
        for (const QString &uid : std::as_const(uids))
            if (p->item(uid) && !col.items.contains(uid))
                col.items << uid;
        p->collections << col;
    }
    // bookmarks. 0.90.0: every bookmark in its order, those to outside addresses too (Scrivener for Windows:
    // <Bookmark BinderUUID="...">title</Bookmark> for a document, <Bookmark Destination="...">title</Bookmark> for
    // anything else), for the project and for each document (those were never read)
    auto readBookmarks = [&](const QDomElement &parent) {
        QList<Bookmark> out;
        for (QDomElement b = parent.firstChildElement(QStringLiteral("Bookmark")); !b.isNull(); b = b.nextSiblingElement(QStringLiteral("Bookmark"))) {
            if (b.hasAttribute(QStringLiteral("Destination"))) {
                const QString to = b.attribute(QStringLiteral("Destination")).trimmed();
                if (!to.isEmpty())
                    out.append({b.text().trimmed().isEmpty() ? to : b.text().trimmed(), to});
                continue;
            }
            const QString target = b.attribute(QStringLiteral("BinderUUID"), b.attribute(QStringLiteral("UUID"), b.text())).trimmed().toUpper();
            if (p->item(target))
                out.append({p->item(target)->title, QStringLiteral("qb://") + target});
        }
        return out;
    };
    p->projectBookmarks.append(readBookmarks(root.firstChildElement(QStringLiteral("ProjectBookmarks"))));
    {
        // each document's bookmarks and its target, once every document is there to point at
        const QDomNodeList all = binder.elementsByTagName(QStringLiteral("BinderItem"));
        for (int i = 0; i < all.size(); ++i) {
            const QDomElement e = all.at(i).toElement();
            BinderItem *it = p->item(e.attribute(QStringLiteral("UUID"), e.attribute(QStringLiteral("ID"))).toUpper());
            if (!it)
                continue;
            const QDomElement marks = e.firstChildElement(QStringLiteral("Bookmarks"));
            if (!marks.isNull())
                it->bookmarks = readBookmarks(marks);
            const QDomElement t = e.firstChildElement(QStringLiteral("TextSettings")).firstChildElement(QStringLiteral("Target"));
            if (!t.isNull() && t.text().trimmed().toInt() > 0) {
                it->target = t.text().trimmed().toInt();
                it->targetChars = t.attribute(QStringLiteral("Type")).startsWith(QLatin1String("Char"));
            }
            // the corkboard of a container: its arrangement, and the places of the cards on it when arranged freely
            // (0.90.0; in card widths, see ScrivenerImporter::setCardWidth)
            const QDomElement cs = e.firstChildElement(QStringLiteral("CorkboardAndOutliner")).firstChildElement(QStringLiteral("CorkboardSettings"));
            const QString arrangement = cs.firstChildElement(QStringLiteral("Arrangement")).text().trimmed();
            if (arrangement == QLatin1String("Freeform"))
                it->corkArrangement = QStringLiteral("freeform");
            else if (arrangement == QLatin1String("Linear"))
                it->corkArrangement = QStringLiteral("grid");
            const QDomElement cards = cs.firstChildElement(QStringLiteral("FreeformCorkboardDocuments"));
            for (QDomElement card = cards.firstChildElement(QStringLiteral("IndexCardBinderUUID")); !card.isNull();
                 card = card.nextSiblingElement(QStringLiteral("IndexCardBinderUUID"))) {
                BinderItem *k = p->item(card.text().trimmed().toUpper());
                const QStringList xy = card.attribute(QStringLiteral("Position")).split(QLatin1Char(','));
                bool okX = false, okY = false;
                const double x = xy.value(0).toDouble(&okX), y = xy.value(1).toDouble(&okY);
                if (k && xy.size() == 2 && okX && okY && qAbs(x) < 1000 && qAbs(y) < 1000)
                    k->corkboardPos = QStringLiteral("%1,%2").arg(qRound(x * cardWidth())).arg(qRound(y * cardWidth()));
            }
        }
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
    {
        // 0.90.0: reading the texts is no writing. The history is the project's own (before, an import added a day with
        // the whole project as written that day), and today's counting goes on from what the project says of today.
        p->history = fileHistory;
        const QDate today = QDate::currentDate();
        WritingDay done;
        for (const WritingDay &w : std::as_const(fileHistory))
            if (w.date == today)
                done = w;
        const int draftW = p->draftWordCount(), totalW = p->totalWordCount(), draftC = p->draftCharCount(), totalC = p->totalCharCount();
        p->uiState[QStringLiteral("dayStartDate")] = today;
        p->uiState[QStringLiteral("dayStartDraft")] = draftW - done.draftWords;
        p->uiState[QStringLiteral("dayStartTotal")] = totalW - done.draftWords - done.otherWords;
        p->uiState[QStringLiteral("dayStartDraftChars")] = draftC - done.draftChars;
        p->uiState[QStringLiteral("dayStartTotalChars")] = totalC - done.draftChars - done.otherChars;
    }
    if (c.v3) {
        // 0.90.0: the binder as Scrivener left it (Settings/ui-common.xml: its open folders, the selection, whether the
        // collections show), read once: the project opens as it was closed in Scrivener. Never written back.
        QFile uf(c.scrivDir + QLatin1String("/Settings/ui-common.xml"));
        QDomDocument ud;
        if (uf.open(QIODevice::ReadOnly) && ud.setContent(uf.readAll())) {
            const QDomElement b = ud.documentElement().firstChildElement(QStringLiteral("Binder"));
            auto ids = [&](const QDomElement &list) {
                QStringList out;
                for (QDomElement i = list.firstChildElement(QStringLiteral("ItemID")); !i.isNull(); i = i.nextSiblingElement(QStringLiteral("ItemID")))
                    if (p->item(i.text().trimmed().toUpper()))
                        out << i.text().trimmed().toUpper();
                return out;
            };
            const QStringList open = ids(b.firstChildElement(QStringLiteral("ExpandedItems")));
            const QStringList sel = ids(b.firstChildElement(QStringLiteral("Selection")));
            if (!open.isEmpty())
                p->uiState[QStringLiteral("expanded")] = open;
            if (!sel.isEmpty())
                p->uiState[QStringLiteral("binderSelection")] = sel;
            const QString cols = b.firstChildElement(QStringLiteral("ShowCollections")).text().trimmed();
            if (!cols.isEmpty())
                p->uiState[QStringLiteral("collections")] = cols.compare(QLatin1String("Yes"), Qt::CaseInsensitive) == 0;
        }
        // the compile metadata of Scrivener's compile settings: the title and the author of the compiled file, its
        // language (Settings/compile.xml, ProjectSettings/MetaData); the formats stay Scrivener's
        QFile cf(c.scrivDir + QLatin1String("/Settings/compile.xml"));
        QDomDocument cd;
        if (cf.open(QIODevice::ReadOnly) && cd.setContent(cf.readAll())) {
            const QDomElement md = cd.documentElement().firstChildElement(QStringLiteral("ProjectSettings")).firstChildElement(QStringLiteral("MetaData"));
            if (!md.isNull()) {
                const QString title = md.firstChildElement(QStringLiteral("ProjectTitle")).text().trimmed();
                const QString author = (md.firstChildElement(QStringLiteral("Forename")).text().trimmed() + QLatin1Char(' ')
                                        + md.firstChildElement(QStringLiteral("Surname")).text().trimmed()).trimmed();
                const QString lang = md.firstChildElement(QStringLiteral("EbookLanguage")).text().trimmed();
                if (!title.isEmpty() && !title.contains(QLatin1String("<$")) && title != p->title())
                    p->compile.titleOverride = title;
                if (!author.isEmpty() && !author.contains(QLatin1String("<$")))
                    p->compile.authorOverride = author;
                if (!lang.isEmpty())
                    p->compile.language = lang;
            }
        }
    }
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

static int s_cardWidth = 210;
void ScrivenerImporter::setCardWidth(int pixels)
{
    if (pixels >= 50 && pixels <= 2000)
        s_cardWidth = pixels;
}
int ScrivenerImporter::cardWidth()
{
    return s_cardWidth;
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

int ScrivenerImporter::fixOldCustomValues(Project *p)
{
    if (!p || !p->scrivenerPath.isEmpty())
        return 0;
    int changed = 0;
    for (BinderItem *it : p->allItems()) {
        bool any = false;
        for (auto v = it->custom.begin(); v != it->custom.end(); ++v) {
            const CustomField *f = p->customFieldById(v.key());
            if (!f || (f->type != CustomFieldType::Checkbox && f->type != CustomFieldType::Date))
                continue;
            const QString now = scrivener::valueFromScrivener(f->type, v.value(), {});
            if (now != v.value()) {
                v.value() = now;
                any = true;
            }
        }
        if (any)
            ++changed;
    }
    if (changed > 0)
        p->markIndexDirty();
    return changed;
}

int ScrivenerImporter::convertInlineNotes(QTextDocument *doc)
{
    if (!doc)
        return 0;
    static const QString fnStart = QStringLiteral("{\\Scrv_fn="), fnEnd = QStringLiteral("\\end_Scrv_fn}");
    static const QString anStart = QStringLiteral("{\\Scrv_annot"), anEnd = QStringLiteral("\\end_Scrv_annot}");
    static const QString textMark = QStringLiteral("\\text=");
    static const QRegularExpression rgb(QStringLiteral("\\\\R=([0-9.]+)\\\\G=([0-9.]+)\\\\B=([0-9.]+)"));
    if (!doc->toRawText().contains(QLatin1String("Scrv_")))
        return 0;
    int found = 0;
    int pos = 0;
    for (;;) {
        const QTextCursor fn = doc->find(fnStart, pos, QTextDocument::FindCaseSensitively);
        const QTextCursor an = doc->find(anStart, pos, QTextDocument::FindCaseSensitively);
        if (fn.isNull() && an.isNull())
            break;
        const bool footnote = !fn.isNull() && (an.isNull() || fn.selectionStart() < an.selectionStart());
        const QTextCursor start = footnote ? fn : an;
        const QTextCursor end = doc->find(footnote ? fnEnd : anEnd, start.selectionEnd(), QTextDocument::FindCaseSensitively);
        if (end.isNull())
            break;   // no end mark: left as it is
        int headEnd = start.selectionEnd();
        QColor color;
        if (!footnote) {
            // the head: " \color={\R=..\G=..\B=..} \text=" up to the text
            const QTextCursor t = doc->find(textMark, start.selectionEnd(), QTextDocument::FindCaseSensitively);
            if (!t.isNull() && t.selectionEnd() <= end.selectionStart()) {
                QTextCursor head(doc);
                head.setPosition(start.selectionEnd());
                head.setPosition(t.selectionStart(), QTextCursor::KeepAnchor);
                const QRegularExpressionMatch m = rgb.match(head.selectedText());
                if (m.hasMatch())
                    color = QColor::fromRgbF(float(m.captured(1).toDouble()), float(m.captured(2).toDouble()),
                                             float(m.captured(3).toDouble()));
                headEnd = t.selectionEnd();
            }
        }
        const int bodyStart = headEnd, bodyEnd = end.selectionStart();
        // the note's format over its text, then the marks out (the end first: positions before it stay)
        if (bodyEnd > bodyStart) {
            QTextCursor body(doc);
            body.setPosition(bodyStart);
            body.setPosition(bodyEnd, QTextCursor::KeepAnchor);
            body.mergeCharFormat(text::noteFormat(footnote ? NoteKind::InlineFootnote : NoteKind::InlineAnnotation,
                                                  QUuid::createUuid().toString(QUuid::WithoutBraces), QString(), color));
        }
        QTextCursor e(doc);
        e.setPosition(end.selectionStart());
        e.setPosition(end.selectionEnd(), QTextCursor::KeepAnchor);
        e.removeSelectedText();
        QTextCursor h(doc);
        h.setPosition(start.selectionStart());
        h.setPosition(headEnd, QTextCursor::KeepAnchor);
        h.removeSelectedText();
        pos = start.selectionStart() + qMax(0, bodyEnd - bodyStart);
        ++found;
    }
    return found;
}

int ScrivenerImporter::stripMarkers(QTextDocument *doc, const Formatting &f, const QStringList &paragraphStyles)
{
    if (!doc || !doc->toPlainText().contains(QLatin1String("$Scr")))
        return 0;
    // <$Scr_H::1>, <!$Scr_H::1>, <$Scr_Ps::0>, <!$Scr_Ps::0>, <$Scr_Cs::0>, <$ScrKeepWithNext> ...
    static const QRegularExpression re(QStringLiteral("<(!?)\\$Scr([A-Za-z_]*)(?:::([0-9]+))?>"));
    int removed = 0;
    // character styles (0.81.3; before, their marks were only taken out): the text between "<$Scr_Cs::n>" and
    // "<!$Scr_Cs::n>" gets the project's character style of that name (its look stays as the RTF has it)
    {
        static const QRegularExpression open(QStringLiteral("<\\$Scr_Cs::([0-9]+)>"));
        int pos = 0;
        for (;;) {
            const QTextCursor o = doc->find(open, pos);
            if (o.isNull())
                break;
            const QString n = open.match(o.selectedText()).captured(1);
            const int start = o.selectionStart();
            const int markLen = o.selectionEnd() - o.selectionStart();
            const QTextCursor cl = doc->find(QStringLiteral("<!$Scr_Cs::%1>").arg(n), o.selectionEnd(), QTextDocument::FindCaseSensitively);
            int bodyEnd = -1;
            if (!cl.isNull()) {
                bodyEnd = cl.selectionStart() - markLen;
                QTextCursor c2(doc);
                c2.setPosition(cl.selectionStart());
                c2.setPosition(cl.selectionEnd(), QTextCursor::KeepAnchor);
                c2.removeSelectedText();
                ++removed;
            }
            QTextCursor c1(doc);
            c1.setPosition(start);
            c1.setPosition(start + markLen, QTextCursor::KeepAnchor);
            c1.removeSelectedText();
            ++removed;
            const int idx = n.toInt();
            if (bodyEnd > start && idx < paragraphStyles.size())
                if (const Style *cs = styles::findChar(f, paragraphStyles.at(idx).trimmed())) {
                    QTextCursor b(doc);
                    b.setPosition(start);
                    b.setPosition(bodyEnd, QTextCursor::KeepAnchor);
                    QTextCharFormat cf;
                    cf.setProperty(props::CharStyle, cs->name);
                    b.mergeCharFormat(cf);
                }
            pos = start;
        }
    }
    int openHeading = 0;  // a heading mark open at the end of the paragraph before
    int openStyle = -1;   // a paragraph style mark open ...
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QString t = b.text();
        int heading = openHeading;   // what this paragraph is in
        int style = openStyle;
        const int carried = openStyle;
        int pairHere = -1;           // a paragraph style opened and closed in this paragraph
        bool headingHere = false, styleHere = false; // opened in this paragraph
        QList<QPair<int, int>> spans;
        bool visibleAny = false;
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
                        if (styleHere)
                            pairHere = style;
                        if (!styleHere && visibleBefore == 0)
                            style = -1;
                        openStyle = -1;
                    }
                }
                spans.append({m.capturedStart(), m.capturedLength()});
            }
            visibleAny = visibleBefore > 0 || !t.mid(last).trimmed().isEmpty();
            // 0.81.3: a paragraph that holds only marks. Scrivener for Windows puts the opening mark of a paragraph style
            // at the end of the paragraph before (here: before an empty one) and closes it at the end of the last
            // paragraph (here: in an empty one): such a paragraph keeps the style it is in, unless a style is opened
            // and closed in it
            if (!visibleAny)
                style = pairHere >= 0 ? pairHere : carried;
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
            // also as Markdown writes them ("\<$Scr\_Ps::0\>"); 0.81.2: inline notes as Scrivener for Windows keeps them
            if (!data.contains("$Scr") && !data.contains("Scr_") && !data.contains("Scr\\_") && !data.contains("Scrv_")
                && !data.contains("Scrv\\_"))
                continue;
            const int n = p->docs()->editStored(
                it->id, notes,
                [p](QTextDocument *d) {
                    const int k = stripMarkers(d, p->formatting) + convertInlineNotes(d);
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
