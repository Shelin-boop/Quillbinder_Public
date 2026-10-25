#include "core/DocumentStore.h"

#include "text/Script.h"
#include "core/BinderItem.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "text/DirectFormat.h"
#include "text/DisplayFormat.h"
#include "text/MarkdownIO.h"
#include "text/RtfIO.h"
#include "text/Styles.h"
#include "text/TextUtil.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFragment>
#include <QUrl>
#include <QUuid>

namespace qb {

DocumentStore::DocumentStore(Project *project) : QObject(project), m_project(project)
{
    m_zoom = project->uiState.value(QStringLiteral("zoom"), 1.0).toDouble();
    if (m_zoom < 0.3 || m_zoom > 5)
        m_zoom = 1.0;
    m_pageView = project->uiState.value(QStringLiteral("pageView"), false).toBool();
    m_pageSetup = PageSetup::fromVariant(project->uiState.value(QStringLiteral("pageSetup")).toMap(), PageSetup::standard());
    m_countTimer.setSingleShot(true);
    m_countTimer.setInterval(600);
    connect(&m_countTimer, &QTimer::timeout, this, [this] {
        const QSet<QString> ids = m_countPending;
        m_countPending.clear();
        for (const QString &id : ids)
            recount(id);
    });
}

DocumentStore::~DocumentStore()
{
    for (Entry &e : m_entries) {
        emit aboutToDeleteDocument(e.doc);
        delete e.doc;
    }
}

QString DocumentStore::extension() const
{
    return m_project->storage == StorageFormat::Rtf ? QStringLiteral("rtf") : QStringLiteral("md");
}

QString DocumentStore::textPath(const QString &id) const
{
    return m_project->contentDir(id) + QLatin1String("/text.") + extension();
}

QString DocumentStore::notesPath(const QString &id) const
{
    return m_project->contentDir(id) + QLatin1String("/notes.") + extension();
}

QTextDocument *DocumentStore::loadedText(const QString &id) const
{
    auto it = m_entries.constFind(id);
    return it == m_entries.constEnd() ? nullptr : it->doc;
}

QTextDocument *DocumentStore::text(const QString &id) { return load(id, false); }
QTextDocument *DocumentStore::notes(const QString &id) { return load(id, true); }
QTextDocument *DocumentStore::projectNotes() { return load(QStringLiteral("PROJECT"), true); }

void DocumentStore::preloadImages(QTextDocument *doc, const QString &dir)
{
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat cf = it.fragment().charFormat();
            if (!cf.isImageFormat())
                continue;
            const QString name = cf.toImageFormat().name();
            const QUrl url(name);
            if (doc->resource(QTextDocument::ImageResource, url).isValid())
                continue;
            QString path = name;
            if (QFileInfo(path).isRelative())
                path = QDir(dir).filePath(name);
            QImage img(path);
            if (!img.isNull())
                doc->addResource(QTextDocument::ImageResource, url, img);
        }
    }
}

void DocumentStore::readFile(const QString &id, bool notes, QTextDocument *doc)
{
    const QString dir = m_project->contentDir(id);
    const QString base = dir + (notes ? QLatin1String("/notes.") : QLatin1String("/text."));
    // prefer the project format, but read the other one if that is what exists
    QString path = base + extension();
    bool rtfFile = m_project->storage == StorageFormat::Rtf;
    if (!QFile::exists(path)) {
        const QString other = base + (rtfFile ? QStringLiteral("md") : QStringLiteral("rtf"));
        if (QFile::exists(other)) {
            path = other;
            rtfFile = !rtfFile;
        }
    }
    QFile f(path);
    if (f.exists() && !f.open(QIODevice::ReadOnly)) {
        // there, but not readable: shown empty, never saved over (0.20)
        const QString key = keyFor(id, notes);
        if (!m_unreadable.contains(key)) {
            m_unreadable.insert(key);
            emit unreadableFile(id, notes, path, f.errorString());
        }
    }
    if (f.isOpen()) {
        const QByteArray data = f.readAll();
        if (rtfFile || rtf::looksLikeRtf(data)) {
            rtf::ReadOptions o;
            o.keepFormatting = m_project->storage == StorageFormat::Rtf;
            rtf::read(data, doc, m_project->formatting, o);
            if (m_project->storage == StorageFormat::Markdown)
                display::normalizeDocument(doc, m_project->formatting);
        } else {
            md::read(QString::fromUtf8(data), doc, m_project->formatting);
            preloadImages(doc, dir);
        }
    } else {
        QTextCursor c(doc);
        c.setBlockFormat(display::normalBlockFormat(m_project->formatting));
    }
    doc->setModified(false);
}

QTextDocument *DocumentStore::load(const QString &id, bool notes)
{
    const QString key = keyFor(id, notes);
    auto it = m_entries.find(key);
    if (it != m_entries.end())
        return it->doc;
    auto *doc = new QTextDocument;
    display::prepareDocument(doc, m_project->formatting, zoomFor(notes));
    doc->setProperty("qbZoom", zoomFor(notes)); // explicit font sizes are zoomed by the editor's highlighter
    doc->setBaseUrl(QUrl::fromLocalFile(m_project->contentDir(id) + QLatin1Char('/')));
    doc->setUndoRedoEnabled(false);
    readFile(id, notes, doc);
    doc->setUndoRedoEnabled(true);
    Entry e;
    e.doc = doc;
    e.isNotes = notes;
    m_entries.insert(key, e);
    connect(doc, &QTextDocument::contentsChanged, this, [this, key, id, notes] {
        auto it = m_entries.find(key);
        if (it == m_entries.end())
            return;
        it->dirty = true;
        m_project->scheduleSave();
        if (!notes) {
            m_plainCache.remove(id);
            m_countPending.insert(id);
            m_countTimer.start();
            emit textEdited(id);
        }
    });
    return doc;
}

bool DocumentStore::hasDirty() const
{
    for (const Entry &e : m_entries)
        if (e.dirty)
            return true;
    return false;
}

QByteArray DocumentStore::serialize(const QString &id, const QTextDocument *doc)
{
    if (m_project->storage == StorageFormat::Rtf)
        return rtf::write(doc, m_project->formatting);
    Entry *entry = nullptr;
    auto it = m_entries.find(id);
    if (it != m_entries.end())
        entry = &it.value();
    const QString dir = m_project->contentDir(id);
    auto saver = [&](const QTextImageFormat &fmt) -> QString {
        const QString name = fmt.name();
        if (entry && entry->savedImages.contains(name))
            return entry->savedImages.value(name);
        if (name.startsWith(QLatin1String("images/")) && QFile::exists(QDir(dir).filePath(name)))
            return name;
        QVariant v = const_cast<QTextDocument *>(doc)->resource(QTextDocument::ImageResource, QUrl(name));
        QImage img;
        if (v.canConvert<QImage>())
            img = v.value<QImage>();
        else if (v.canConvert<QByteArray>())
            img.loadFromData(v.toByteArray());
        if (img.isNull() && QFileInfo(name).isAbsolute())
            img.load(name);
        if (img.isNull())
            return name;
        QDir().mkpath(dir + QLatin1String("/images"));
        const QString rel = QStringLiteral("images/%1.png").arg(QUuid::createUuid().toString(QUuid::Id128).left(12));
        img.save(QDir(dir).filePath(rel), "PNG");
        if (entry)
            entry->savedImages.insert(name, rel);
        return rel;
    };
    return md::write(doc, m_project->formatting, saver).toUtf8();
}

void DocumentStore::deserialize(const QString &id, const QByteArray &data, QTextDocument *doc, bool rtfData)
{
    // a document of the store keeps its own zoom (notes have the inspector's, 0.21)
    const QVariant z = doc->property("qbZoom");
    display::prepareDocument(doc, m_project->formatting, z.isValid() ? z.toDouble() : m_zoom);
    doc->setBaseUrl(QUrl::fromLocalFile(m_project->contentDir(id) + QLatin1Char('/')));
    if (rtfData || rtf::looksLikeRtf(data)) {
        rtf::ReadOptions o;
        o.keepFormatting = m_project->storage == StorageFormat::Rtf;
        rtf::read(data, doc, m_project->formatting, o);
    } else {
        md::read(QString::fromUtf8(data), doc, m_project->formatting);
        preloadImages(doc, m_project->contentDir(id));
    }
}

bool DocumentStore::writeEntry(const QString &key, Entry &e, QString *error)
{
    const bool notes = e.isNotes;
    const QString id = notes ? key.mid(2) : key;
    const QString dir = m_project->contentDir(id);
    const QString path = notes ? notesPath(id) : textPath(id);
    if (m_unreadable.contains(key)) {
        // its file could not be read: what is shown is not its text, so it is not written
        e.dirty = false;
        return true;
    }
    const bool empty = e.doc->isEmpty();
    if (empty && !QFile::exists(path)) {
        e.dirty = false;
        return true;
    }
    QDir().mkpath(dir);
    const QByteArray data = serialize(id, e.doc);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        if (error)
            *error = tr("Could not save %1: %2").arg(path, f.errorString());
        return false;
    }
    // remove a file of the other storage format if one is still there
    const QString other = (notes ? dir + QLatin1String("/notes.") : dir + QLatin1String("/text."))
                          + (m_project->storage == StorageFormat::Rtf ? QStringLiteral("md") : QStringLiteral("rtf"));
    if (QFile::exists(other))
        QFile::remove(other);
    e.dirty = false;
    return true;
}

bool DocumentStore::saveDirty(QString *error)
{
    bool ok = true;
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (!it->dirty)
            continue;
        if (!writeEntry(it.key(), it.value(), error))
            ok = false;
    }
    // counts that are still pending
    if (!m_countPending.isEmpty()) {
        m_countTimer.stop();
        const QSet<QString> ids = m_countPending;
        m_countPending.clear();
        for (const QString &id : ids)
            recount(id);
    }
    return ok;
}

bool DocumentStore::saveDocument(const QString &id, QString *error)
{
    auto it = m_entries.find(id);
    if (it == m_entries.end() || !it->dirty)
        return true;
    return writeEntry(id, it.value(), error);
}

QString DocumentStore::plainText(const QString &id)
{
    if (QTextDocument *d = loadedText(id))
        return text::plainText(d);
    auto it = m_plainCache.constFind(id);
    if (it != m_plainCache.constEnd())
        return *it;
    QTextDocument tmp;
    readFile(id, false, &tmp);
    const QString p = text::plainText(&tmp);
    m_plainCache.insert(id, p);
    return p;
}

QString DocumentStore::notesPlainText(const QString &id)
{
    auto it = m_entries.constFind(keyFor(id, true));
    if (it != m_entries.constEnd())
        return text::plainText(it->doc, true);
    const QString key = keyFor(id, true);
    auto c = m_plainCache.constFind(key);
    if (c != m_plainCache.constEnd())
        return *c;
    QTextDocument tmp;
    readFile(id, true, &tmp);
    const QString p = text::plainText(&tmp, true);
    m_plainCache.insert(key, p);
    return p;
}

void DocumentStore::removeDocument(const QString &id)
{
    for (const QString &key : {id, keyFor(id, true)}) {
        auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            emit aboutToDeleteDocument(it->doc);
            it->doc->deleteLater();
            m_entries.erase(it);
        }
        m_plainCache.remove(key);
    }
    QDir(m_project->contentDir(id)).removeRecursively();
}

static void copyDir(const QString &from, const QString &to)
{
    QDir src(from);
    if (!src.exists())
        return;
    QDir().mkpath(to);
    for (const QFileInfo &fi : src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (fi.isDir())
            copyDir(fi.absoluteFilePath(), to + QLatin1Char('/') + fi.fileName());
        else
            QFile::copy(fi.absoluteFilePath(), to + QLatin1Char('/') + fi.fileName());
    }
}

void DocumentStore::copyDocument(const QString &fromId, const QString &toId)
{
    saveDocument(fromId);
    saveDocument(keyFor(fromId, true));
    copyDir(m_project->contentDir(fromId), m_project->contentDir(toId));
}

void DocumentStore::setContent(const QString &id, const QTextDocument *source)
{
    QTextDocument *doc = text(id);
    doc->setUndoRedoEnabled(false);
    // copy the source's image resources
    for (QTextBlock b = source->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat cf = it.fragment().charFormat();
            if (cf.isImageFormat()) {
                const QUrl url(cf.toImageFormat().name());
                const QVariant v = const_cast<QTextDocument *>(source)->resource(QTextDocument::ImageResource, url);
                if (v.isValid())
                    doc->addResource(QTextDocument::ImageResource, url, v);
            }
        }
    QTextCursor c(doc);
    c.select(QTextCursor::Document);
    c.insertFragment(QTextDocumentFragment(source));
    // insertFragment keeps the first block format of the target: fix it
    QTextCursor first(doc);
    first.setBlockFormat(source->begin().blockFormat());
    first.setBlockCharFormat(source->begin().charFormat());
    doc->setUndoRedoEnabled(true);
    auto it = m_entries.find(id);
    if (it != m_entries.end())
        it->dirty = true;
    recount(id);
    m_project->scheduleSave();
}

void DocumentStore::setNotesContent(const QString &id, const QTextDocument *source)
{
    QTextDocument *doc = notes(id);
    doc->setUndoRedoEnabled(false);
    QTextCursor c(doc);
    c.select(QTextCursor::Document);
    c.insertFragment(QTextDocumentFragment(source));
    doc->setUndoRedoEnabled(true);
    auto it = m_entries.find(keyFor(id, true));
    if (it != m_entries.end())
        it->dirty = true;
    m_project->scheduleSave();
}

QString DocumentStore::importImage(const QString &id, const QString &filePath)
{
    QImage img(filePath);
    if (img.isNull())
        return QString();
    return importImage(id, img);
}

QString DocumentStore::importImage(const QString &id, const QImage &img)
{
    const QString dir = m_project->contentDir(id);
    QDir().mkpath(dir + QLatin1String("/images"));
    const QString rel = QStringLiteral("images/%1.png").arg(QUuid::createUuid().toString(QUuid::Id128).left(12));
    img.save(QDir(dir).filePath(rel), "PNG");
    if (QTextDocument *d = loadedText(id))
        d->addResource(QTextDocument::ImageResource, QUrl(rel), img);
    return rel;
}

void DocumentStore::setZoom(double z)
{
    z = qBound(0.5, z, 4.0);
    if (qFuzzyCompare(z, m_zoom))
        return;
    m_zoom = z;
    m_project->uiState[QStringLiteral("zoom")] = z;
    const QFont f = display::baseFont(m_project->formatting, m_zoom);
    for (Entry &e : m_entries) {
        if (e.isNotes)
            continue; // notes follow the inspector's zoom (setNotesZoom)
        e.doc->setDefaultFont(f);
        e.doc->setProperty("qbZoom", m_zoom);
    }
    emit zoomChanged(z);
}

void DocumentStore::setNotesZoom(double z)
{
    z = qBound(0.5, z, 4.0);
    if (qFuzzyCompare(z, m_notesZoom))
        return;
    m_notesZoom = z;
    const QFont f = display::baseFont(m_project->formatting, m_notesZoom);
    for (Entry &e : m_entries) {
        if (!e.isNotes)
            continue;
        e.doc->setDefaultFont(f);
        e.doc->setProperty("qbZoom", m_notesZoom);
    }
    emit notesZoomChanged(z);
}

void DocumentStore::setPageView(bool on)
{
    if (m_pageView == on)
        return;
    m_pageView = on;
    m_project->uiState[QStringLiteral("pageView")] = on;
    m_project->markIndexDirty();
    if (m_pageViewSuspended == 0)
        emit pageViewChanged();
}

void DocumentStore::suspendPageView(bool on)
{
    const bool before = pageView();
    m_pageViewSuspended = qMax(0, m_pageViewSuspended + (on ? 1 : -1));
    if (before != pageView())
        emit pageViewChanged();
}

void DocumentStore::setPageSetup(const PageSetup &s)
{
    const PageSetup n = s.normalized();
    if (n == m_pageSetup)
        return;
    m_pageSetup = n;
    m_project->uiState[QStringLiteral("pageSetup")] = n.toVariant();
    m_project->markIndexDirty();
    emit pageViewChanged();
}

void DocumentStore::applyFormatting()
{
    for (Entry &e : m_entries) {
        const bool wasDirty = e.dirty;
        e.doc->setDefaultFont(display::baseFont(m_project->formatting, zoomFor(e.isNotes)));
        if (m_project->storage == StorageFormat::Markdown) {
            const bool undo = e.doc->isUndoRedoEnabled();
            e.doc->setUndoRedoEnabled(false);
            display::normalizeDocument(e.doc, m_project->formatting);
            e.doc->setUndoRedoEnabled(undo);
            e.dirty = wasDirty; // formatting is not stored in Markdown
        }
    }
}

bool DocumentStore::convertStorage(StorageFormat to, QString *error)
{
    if (to == m_project->storage)
        return true;
    // load everything in the old format first
    QList<QString> ids;
    for (BinderItem *it : m_project->allItems())
        if (it->hasText())
            ids << it->id;
    ids << QStringLiteral("PROJECT");
    for (const QString &id : ids) {
        if (id != QStringLiteral("PROJECT"))
            text(id);
        notes(id);
    }
    m_project->storage = to;
    bool ok = true;
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        const bool empty = it->doc->isEmpty();
        const bool notes = it->isNotes;
        const QString id = notes ? it.key().mid(2) : it.key();
        const QString oldPath = m_project->contentDir(id) + (notes ? QLatin1String("/notes.") : QLatin1String("/text."))
                                + (to == StorageFormat::Rtf ? QStringLiteral("md") : QStringLiteral("rtf"));
        if (empty && !QFile::exists(oldPath))
            continue;
        if (to == StorageFormat::Markdown) {
            // paragraph formatting that differs from the project's becomes
            // formatting set by hand; the rest follows the project from now on
            const bool undo = it->doc->isUndoRedoEnabled();
            it->doc->setUndoRedoEnabled(false);
            direct::markDifferences(it->doc, m_project->formatting);
            display::normalizeDocument(it->doc, m_project->formatting);
            it->doc->setUndoRedoEnabled(undo);
        }
        it->dirty = true;
        if (!writeEntry(it.key(), it.value(), error))
            ok = false;
    }
    m_plainCache.clear();
    m_project->markIndexDirty();
    return ok && m_project->saveIndex(error);
}

bool DocumentStore::restyleAll(const Formatting &oldF, const Formatting &newF, const styles::Changes &changes,
                               const std::function<bool(int, int)> &progress, QString *error)
{
    const bool rtf = m_project->storage == StorageFormat::Rtf;
    // Markdown stores the names of styles only: its files change when a name
    // changes or a style goes away. RTF stores the look as well.
    bool removed = false;
    for (const Style &s : styles::sheet(oldF).styles)
        if (!changes.renamed.contains(s.name) && styles::sheet(newF).indexOf(s.name) < 0)
            removed = true;
    // the same for the elements of the script format
    for (const Style &s : script::data(oldF).sheet.styles)
        if (!changes.renamed.contains(s.name) && !styles::find(newF, s.name))
            removed = true;
    const bool filesChange = rtf || !changes.renamed.isEmpty() || removed;
    bool ok = true;

    // documents in memory (shown in editors)
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        const bool wasDirty = it->dirty;
        const bool undo = it->doc->isUndoRedoEnabled();
        it->doc->setUndoRedoEnabled(false);
        const bool names = styles::restyle(it->doc, oldF, newF, changes);
        it->doc->setUndoRedoEnabled(undo);
        if (!rtf && !names)
            it->dirty = wasDirty; // the stored text is the same
    }

    // documents in files only
    if (filesChange) {
        struct Ref {
            QString id;
            bool notes;
        };
        QList<Ref> refs;
        for (BinderItem *item : m_project->allItems()) {
            if (item->hasText())
                refs.append({item->id, false});
            refs.append({item->id, true});
        }
        refs.append({QStringLiteral("PROJECT"), true});
        int done = 0;
        for (const Ref &r : refs) {
            if (progress)
                progress(done, refs.size());
            ++done;
            const QString key = keyFor(r.id, r.notes);
            if (m_entries.contains(key))
                continue;
            const QString base = m_project->contentDir(r.id) + (r.notes ? QLatin1String("/notes.") : QLatin1String("/text."));
            if (!QFile::exists(base + QLatin1String("md")) && !QFile::exists(base + QLatin1String("rtf")))
                continue;
            QTextDocument doc;
            display::prepareDocument(&doc, oldF, 1.0);
            doc.setBaseUrl(QUrl::fromLocalFile(m_project->contentDir(r.id) + QLatin1Char('/')));
            doc.setUndoRedoEnabled(false);
            m_project->formatting = oldF;
            readFile(r.id, r.notes, &doc);
            if (styles::usedStyles(&doc).isEmpty())
                continue;
            const bool names = styles::restyle(&doc, oldF, newF, changes);
            if (!names && (!rtf || !doc.isModified()))
                continue;
            m_project->formatting = newF;
            Entry e;
            e.doc = &doc;
            e.isNotes = r.notes;
            e.dirty = true;
            if (!writeEntry(key, e, error))
                ok = false;
        }
        if (progress)
            progress(refs.size(), refs.size());
    }
    m_project->formatting = newF;
    return ok;
}

int DocumentStore::editStored(const QString &id, bool notes, const std::function<int(QTextDocument *)> &op, QString *error)
{
    const QString key = keyFor(id, notes);
    auto it = m_entries.find(key);
    if (it != m_entries.end())
        return op(it->doc); // contentsChanged marks it as changed and schedules the save
    const QString base = m_project->contentDir(id) + (notes ? QLatin1String("/notes.") : QLatin1String("/text."));
    if (!QFile::exists(base + QLatin1String("md")) && !QFile::exists(base + QLatin1String("rtf")))
        return 0;
    QTextDocument doc;
    display::prepareDocument(&doc, m_project->formatting, 1.0);
    doc.setBaseUrl(QUrl::fromLocalFile(m_project->contentDir(id) + QLatin1Char('/')));
    doc.setUndoRedoEnabled(false);
    readFile(id, notes, &doc);
    doc.setModified(false);
    const int n = op(&doc);
    if (n <= 0 || !doc.isModified())
        return n;
    Entry e;
    e.doc = &doc;
    e.isNotes = notes;
    e.dirty = true;
    if (!writeEntry(key, e, error))
        return -1;
    m_plainCache.remove(key);
    if (!notes)
        recount(id);
    return n;
}

void DocumentStore::recount(const QString &id)
{
    BinderItem *it = m_project->item(id);
    if (!it)
        return;
    const text::Counts c = text::count(plainText(id));
    const bool changed = c.words != it->wordCount || c.chars != it->charCount;
    const bool hadText = it->charCount > 0;
    it->wordCount = c.words;
    it->charCount = c.chars;
    if (changed) {
        m_project->recordHistory();
        m_project->notifyItemChanged(it, true);
        if (hadText != (c.chars > 0))
            icons::clearCache();
        emit countsUpdated(id);
        emit m_project->countsChanged();
    }
}

void DocumentStore::recountAll()
{
    for (BinderItem *it : m_project->allItems())
        if (it->hasText()) {
            const text::Counts c = text::count(plainText(it->id));
            it->wordCount = c.words;
            it->charCount = c.chars;
        }
    emit m_project->countsChanged();
}

bool convertFile(const QString &inPath, const QString &outPath, const Formatting &f, QString *error, bool standardMarkdown)
{
    QFile in(inPath);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot read %1").arg(inPath);
        return false;
    }
    const QByteArray data = in.readAll();
    QTextDocument doc;
    doc.setBaseUrl(QUrl::fromLocalFile(QFileInfo(inPath).absolutePath() + QLatin1Char('/')));
    const bool toRtf = outPath.endsWith(QLatin1String(".rtf"), Qt::CaseInsensitive);
    if (rtf::looksLikeRtf(data)) {
        rtf::ReadOptions o;
        o.keepFormatting = toRtf;
        rtf::read(data, &doc, f, o);
    } else {
        QString text = QString::fromUtf8(data);
        // files from other programs use blank lines between paragraphs
        if (text.contains(QLatin1String("\n\n")))
            text = md::fromStandardMarkdown(text);
        md::read(text, &doc, f);
    }
    QByteArray out;
    if (toRtf) {
        out = rtf::write(&doc, f);
    } else {
        const QString outDir = QFileInfo(outPath).absolutePath();
        const QString imgDir = QFileInfo(outPath).completeBaseName() + QStringLiteral("_images");
        auto saver = [&](const QTextImageFormat &fmt) -> QString {
            const QVariant v = doc.resource(QTextDocument::ImageResource, QUrl(fmt.name()));
            QImage img = v.value<QImage>();
            if (img.isNull())
                return fmt.name();
            QDir().mkpath(QDir(outDir).filePath(imgDir));
            const QString rel = imgDir + QStringLiteral("/%1.png").arg(QUuid::createUuid().toString(QUuid::Id128).left(10));
            img.save(QDir(outDir).filePath(rel), "PNG");
            return rel;
        };
        QString text = md::write(&doc, f, saver);
        if (standardMarkdown)
            text = md::toStandardMarkdown(text);
        out = text.toUtf8();
    }
    QSaveFile o(outPath);
    if (!o.open(QIODevice::WriteOnly) || o.write(out) != out.size() || !o.commit()) {
        if (error)
            *error = QObject::tr("Cannot write %1").arg(outPath);
        return false;
    }
    return true;
}

} // namespace qb
