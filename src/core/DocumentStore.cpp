#include "core/PathSafety.h"
#include "core/SafeSave.h"
#include "core/SaveBatch.h"
#include "core/DocumentStore.h"

#include "text/Script.h"
#include "core/BinderItem.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "core/Snapshots.h"
#include "text/DirectFormat.h"
#include "text/DisplayFormat.h"
#include "text/MarkdownIO.h"
#include "text/RtfIO.h"
#include "text/Styles.h"
#include "text/TextUtil.h"
#include "import/FileImporter.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>
#include <QSet>
#include <QStringDecoder>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
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

// 0.70.0 (audit A F-04): the program writes its Markdown files in UTF-8. A file in another encoding (saved by
// another editor as Windows-1252 or UTF-16) is decoded as well as possible for showing, and *damage says why it
// must not be written over.
QString DocumentStore::decodeStored(const QByteArray &data, QString *damage)
{
    if (data.startsWith("\xFF\xFE") || data.startsWith("\xFE\xFF")) {
        *damage = tr("it was saved by another program as UTF-16, not as UTF-8");
        QStringDecoder dec(QStringConverter::Utf16);
        return dec(data);
    }
    if (data.contains('\0')) {
        int oddZero = 0, evenZero = 0;
        for (int i = 0; i < data.size(); ++i)
            if (data.at(i) == '\0')
                ++((i % 2) ? oddZero : evenZero);
        *damage = tr("it holds zero bytes (saved by another program as UTF-16, or damaged)");
        if (oddZero > evenZero * 4) {
            QStringDecoder dec(QStringConverter::Utf16LE);
            return dec(data);
        }
        if (evenZero > oddZero * 4) {
            QStringDecoder dec(QStringConverter::Utf16BE);
            return dec(data);
        }
        QByteArray copy = data;
        copy.replace('\0', "");
        return QString::fromUtf8(copy);
    }
    QStringDecoder utf8(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    const QString s = utf8(data);
    if (!utf8.hasError())
        return s;
    *damage = tr("it is not in UTF-8 (saved by another program in another character encoding, such as Windows-1252)");
    return importer::decodeText(data, QStringLiteral("windows-1252"));
}

void DocumentStore::readFile(const QString &id, bool notes, QTextDocument *doc, QByteArray *bytes, bool *rtf)
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
    QByteArray data;
    if (f.isOpen()) {
        data = f.readAll();
        // 0.65.0 (audit F23): a read cut short (an I/O error, a file of a sync folder that is not all there)
        // is not the text: shown empty and never saved over, as a file that cannot be opened
        const bool regular = QFileInfo(path).isFile();
        if (f.error() != QFileDevice::NoError || (regular && data.size() != f.size())) {
            const QString key = keyFor(id, notes);
            const QString why = f.error() != QFileDevice::NoError ? f.errorString()
                                                                   : tr("only %1 of %2 bytes could be read").arg(data.size()).arg(f.size());
            f.close();
            if (!m_unreadable.contains(key)) {
                m_unreadable.insert(key);
                emit unreadableFile(id, notes, path, why);
            }
        }
    }
    if (f.isOpen()) {
        if (bytes)
            *bytes = data;
        if (rtf)
            *rtf = rtfFile || rtf::looksLikeRtf(data);
        // 0.70.0 (audit A F-05, F-04): a file that is there but damaged, or written by another program in
        // another character encoding, is shown as well as it can be read, read-only, and never written:
        // before, it was read as an empty text or with replacement characters and the next save wrote that
        // over the file
        QString damage;
        const bool blank = data.trimmed().isEmpty();
        if (rtfFile || rtf::looksLikeRtf(data)) {
            QByteArray shown = data;
            if (!blank && !rtf::wellFormed(data, &damage))
                shown = rtf::mended(data);
            rtf::ReadOptions o;
            o.keepFormatting = m_project->storage == StorageFormat::Rtf;
            rtf::read(shown, doc, m_project->formatting, o);
            if (m_project->storage == StorageFormat::Markdown)
                display::normalizeDocument(doc, m_project->formatting);
        } else {
            md::read(decodeStored(data, &damage), doc, m_project->formatting);
            preloadImages(doc, dir);
        }
        if (!damage.isEmpty()) {
            const QString key = keyFor(id, notes);
            m_damaged.insert(key);
            if (!m_unreadable.contains(key)) {
                m_unreadable.insert(key);
                emit damagedFile(id, notes, path, damage);
            }
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
    Entry e;
    readFile(id, notes, doc, &e.read, &e.readRtf);
    doc->setUndoRedoEnabled(true);
    e.doc = doc;
    e.isNotes = notes;
    // 0.64: what the file was when it was read; a different file at the next write means someone
    // else (another computer through a sync program) wrote it meanwhile
    const QString path = notes ? notesPath(id) : textPath(id);
    e.stamp = syncsafe::stampOf(path);
    if (!notes && !e.stamp.exists && id != QLatin1String("PROJECT")) {
        // the project file says the document has text, but its file is not there: most likely it
        // has not arrived yet; shown empty, read-only and never written, so it cannot be lost
        const QString other = m_project->contentDir(id) + (m_project->storage == StorageFormat::Rtf ? QStringLiteral("/text.md") : QStringLiteral("/text.rtf"));
        BinderItem *it = m_project->item(id);
        if (it && it->charCount > 0 && !QFileInfo::exists(other) && m_project->scrivenerPath.isEmpty()) {
            m_missing.insert(id);
            m_unreadable.insert(key);
            emit missingFile(id, path);
        }
    }
    // 0.81.1 (audit of 0.81.0, F-01): a text that is never written is remembered as it was read, so that a change a
    // command makes to it is told and kept aside, not dropped without a word at the next save
    if (m_unreadable.contains(key))
        m_shown.insert(key, Shown{shownBytes(doc, m_project->formatting, m_project->storage), m_project->formatting,
                                  m_project->storage});
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

QStringList DocumentStore::dirtyTitles(int max) const
{
    QStringList out;
    for (auto it = m_entries.cbegin(); it != m_entries.cend() && out.size() < max; ++it) {
        if (!it->dirty || m_unreadable.contains(it.key()))
            continue;
        const QString id = it->isNotes ? it.key().mid(2) : it.key();
        const QString t = it->isNotes ? tr("notes of %1").arg(titleOf(id)) : titleOf(id);
        if (!out.contains(t))
            out << t;
    }
    return out;
}

QByteArray DocumentStore::serialize(const QString &id, const QTextDocument *doc, bool notes)
{
    if (m_project->storage == StorageFormat::Rtf)
        return rtf::write(doc, m_project->formatting);
    // 0.81.1 (audit of 0.81.0, F-02): the pictures already written are looked up under the entry's own key; notes are
    // kept under "n:" + id, and the lookup by the id alone never found theirs, so a picture in notes whose name is not
    // a file (pasted from a word processor) was written again with every save
    const QString key = keyFor(id, notes);
    Entry *entry = nullptr;
    auto it = m_entries.find(key);
    if (it != m_entries.end())
        entry = &it.value();
    const QString dir = m_project->contentDir(id);
    m_serializeError.clear();
    auto saver = [&](const QTextImageFormat &fmt) -> QString {
        const QString name = fmt.name();
        if (entry && entry->savedImages.contains(name) && QFile::exists(QDir(dir).filePath(entry->savedImages.value(name))))
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
        // 0.65.0 (audit F04): the picture's file is checked like the text's, and remembered only once it is
        // in place; before, a failed write left the text pointing at a file that was never there
        QByteArray png;
        QBuffer buf(&png);
        QString why;
        if (!buf.open(QIODevice::WriteOnly) || !img.save(&buf, "PNG")) {
            if (m_serializeError.isEmpty())
                m_serializeError = tr("A picture of %1 could not be converted for saving.").arg(titleOf(id));
            return rel;
        }
        if (!savebatch::put(QDir(dir).filePath(rel), png, &why, tr("picture in %1").arg(titleOf(id)), placeOf(id),
                            [this, key, name, rel] {
                                auto e = m_entries.find(key);
                                if (e != m_entries.end())
                                    e->savedImages.insert(name, rel);
                            })) {
            if (m_serializeError.isEmpty())
                m_serializeError = tr("A picture of %1 could not be saved: %2").arg(titleOf(id), why);
        }
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
        // 0.81.1 (audit of 0.81.0, F-01): a text only shown stays as it is. One that a command changed all the same is
        // not lost without a word (before, the change was dropped and the save counted as done): the changed text is
        // kept as an RTF file in settings/conflicts and the writer is told; the damaged file stays as it is
        auto s = m_shown.find(key);
        if (s != m_shown.end() && e.doc && shownBytes(e.doc, s->formatting, s->storage) != s->bytes) {
            const QString title = notes ? tr("%1 (notes)").arg(titleOf(id)) : titleOf(id);
            const QString keep = m_project->settingsDir() + QStringLiteral("/conflicts");
            const QString base = text::safeFileName(tr("%1 (changed, not saved)").arg(title)) + QLatin1Char(' ')
                                 + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH-mm-ss"));
            QString copy = keep + QLatin1Char('/') + base + QStringLiteral(".rtf");
            for (int n = 2; QFileInfo::exists(copy); ++n)
                copy = keep + QLatin1Char('/') + base + QStringLiteral(" %1.rtf").arg(n);
            if (QDir().mkpath(keep) && safesave::writeFile(copy, rtf::write(e.doc, m_project->formatting))) {
                s->bytes = shownBytes(e.doc, s->formatting, s->storage); // told once per change
                m_toldNotKept.remove(key);
                emit changeKeptAside(id, notes, copy);
            } else {
                e.dirty = true; // nothing kept: it stays unsaved, and the close question names it
                if (!m_toldNotKept.contains(key)) {
                    m_toldNotKept.insert(key);
                    emit changeKeptAside(id, notes, QString());
                }
            }
        }
        return true;
    }
    const bool empty = e.doc->isEmpty();
    if (empty && !QFile::exists(path)) {
        e.dirty = false;
        return true;
    }
    QDir().mkpath(dir);
    // 0.70.0 (audit B F-4): control characters (from pasted data) cannot be stored and are left out, as since
    // 0.20; now the writer is told once per document
    if (!m_toldControl.contains(key)) {
        int count = 0;
        const QString plain = e.doc->toRawText();
        for (const QChar c : plain) {
            const ushort u = c.unicode();
            if (u < 0x20 && u != '\t' && u != '\n' && u != '\r')
                ++count;
        }
        if (count > 0) {
            m_toldControl.insert(key);
            emit charactersLeftOut(id, notes, count);
        }
    }
    const QByteArray data = serialize(id, e.doc, notes);
    if (!m_serializeError.isEmpty()) {
        // 0.65.0 (audit F04): a text whose picture could not be written is not saved either; it stays unsaved
        if (error)
            *error = m_serializeError;
        return false;
    }
    const QString other = (notes ? dir + QLatin1String("/notes.") : dir + QLatin1String("/text."))
                          + (m_project->storage == StorageFormat::Rtf ? QStringLiteral("md") : QStringLiteral("rtf"));
    // 0.64: the file holds exactly this already (a document marked as changed by showing it, or
    // the same text written elsewhere): not written again, so a sync program has nothing to carry
    if (!QFile::exists(other)) {
        QFile cur(path);
        if (cur.open(QIODevice::ReadOnly) && cur.size() == data.size() && cur.readAll() == data) {
            e.dirty = false;
            if (e.stamp.known)
                e.stamp = syncsafe::stampOf(path);
            return true;
        }
    }
    bool elsewhere = syncsafe::changedSince(e.stamp, path) && QFileInfo::exists(path);
    // 0.65.0 (audit F14): the same size and time as read or written here, but other bytes (a sync program that sets
    // times of its own, a file system with times in 2 s steps): changed elsewhere too
    if (!elsewhere && e.stamp.known && !e.read.isNull() && QFileInfo::exists(path)) {
        QFile cur(path);
        if (cur.open(QIODevice::ReadOnly) && cur.readAll() != e.read)
            elsewhere = true;
    }
    if (elsewhere) {
        // 0.64.1: written elsewhere, and not edited here (only shown, which marks it as changed):
        // the other version stays as it is, nothing is kept and nothing is written
        if (unchangedSinceRead(id, e, data)) {
            e.dirty = false;
            return true;
        }
        // 0.64.1: this version replaces the other one only when that one was kept
        if (!keepOtherVersion(id, notes, path)) {
            if (error)
                *error = tr("%1 was changed elsewhere (another computer or program), and that version could not be kept "
                            "(no copy in %2, no snapshot), so it was not replaced with this computer's version. Check that "
                            "the project folder is writable and the disk is not full.")
                             .arg(QDir::toNativeSeparators(path), QDir::toNativeSeparators(m_project->settingsDir() + QStringLiteral("/conflicts")));
            return false;
        }
    }
    // tables and pictures beside the text (0.62): older versions would turn them into text when they
    // save, so the project names 0.62.0 as the version it needs (they ask before opening it)
    if (!e.doc->rootFrame()->childFrames().isEmpty()
        && Project::compareVersionText(m_project->needsVersion, QStringLiteral("0.62.0")) < 0) {
        m_project->needsVersion = QStringLiteral("0.62.0");
        m_project->markIndexDirty();
    }
    // 0.65.0 (audit F19): into the save that runs (all its files land together), else at once; the entry
    // counts as saved only once the file is in place
    const bool rtfNow = m_project->storage == StorageFormat::Rtf;
    QString why;
    const bool put = savebatch::put(path, data, &why, notes ? tr("%1 (notes)").arg(titleOf(id)) : titleOf(id), placeOf(id),
                                    [this, key, path, other, data, rtfNow] {
                                        // remove a file of the other storage format if one is still there
                                        if (QFile::exists(other))
                                            QFile::remove(other);
                                        auto it = m_entries.find(key);
                                        if (it == m_entries.end())
                                            return;
                                        it->dirty = false;
                                        if (it->stamp.known)
                                            it->stamp = syncsafe::stampOf(path);
                                        it->read = data;
                                        it->readRtf = rtfNow;
                                    });
    if (!put) {
        if (error)
            *error = tr("Could not save %1: %2").arg(path, why);
        return false;
    }
    return true;
}

int DocumentStore::writeUnsaved(const QString &dir, QString *error)
{
    int n = 0;
    QSet<QString> used;
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (!it->dirty || m_unreadable.contains(it.key()))
            continue;
        const QString id = it->isNotes ? it.key().mid(2) : it.key();
        QString name = text::safeFileName(it->isNotes ? tr("%1 (notes)").arg(titleOf(id)) : titleOf(id));
        if (name.isEmpty())
            name = id;
        QString base = name;
        for (int k = 2; used.contains(base.toLower()); ++k)
            base = QStringLiteral("%1 (%2)").arg(name).arg(k);
        used.insert(base.toLower());
        QString why;
        if (safesave::writeFile(QDir(dir).filePath(base + QStringLiteral(".rtf")), rtf::write(it->doc, m_project->formatting), &why))
            ++n;
        else if (error && error->isEmpty())
            *error = why;
    }
    return n;
}

QString DocumentStore::titleOf(const QString &id) const
{
    if (id == QLatin1String("PROJECT"))
        return tr("project notes");
    const BinderItem *it = m_project->item(id);
    return it ? it->title : id;
}

QString DocumentStore::placeOf(const QString &id) const
{
    QStringList path;
    const BinderItem *it = m_project->item(id);
    for (const BinderItem *p = it ? it->parent : nullptr; p && p->parent; p = p->parent)
        path.prepend(p->title);
    return path.join(QStringLiteral(" / "));
}

bool DocumentStore::unchangedSinceRead(const QString &id, const Entry &e, const QByteArray &data)
{
    if (e.read.isNull())
        return false; // not read from a file here: unknown
    if (e.read == data)
        return true;
    // the same text read again and written the way this program writes it
    QTextDocument tmp;
    tmp.setProperty("qbZoom", e.doc->property("qbZoom"));
    deserialize(id, e.read, &tmp, e.readRtf);
    if (e.readRtf && m_project->storage == StorageFormat::Markdown)
        display::normalizeDocument(&tmp, m_project->formatting);
    return serialize(id, &tmp, e.isNotes) == data;
}

QByteArray DocumentStore::shownBytes(const QTextDocument *doc, const Formatting &f, StorageFormat storage) const
{
    if (storage == StorageFormat::Rtf)
        return rtf::write(doc, f);
    // the pictures by the names they have (nothing is written: this is only for comparing)
    return md::write(doc, f, [](const QTextImageFormat &fmt) { return fmt.name(); }).toUtf8();
}

QString DocumentStore::whyNotEditable(const QString &id, bool notes) const
{
    const QString key = keyFor(id, notes);
    if (!m_unreadable.contains(key))
        return QString();
    if (m_damaged.contains(key))
        return tr("its file is damaged or was saved by another program in another character encoding");
    if (!notes && m_missing.contains(id))
        return tr("its file has not arrived on this computer yet");
    return tr("its file cannot be read");
}

void DocumentStore::noteSkipped(const QString &id, bool notes)
{
    const QString t = notes ? tr("%1 (notes)").arg(titleOf(id)) : titleOf(id);
    if (!m_skippedUnreadable.contains(t))
        m_skippedUnreadable << t;
}

QStringList DocumentStore::takeSkippedUnreadable()
{
    QStringList out;
    out.swap(m_skippedUnreadable);
    return out;
}

bool DocumentStore::releaseDamaged(const QString &id, bool notes, QString *copy, QString *error)
{
    const QString key = keyFor(id, notes);
    if (!m_unreadable.contains(key))
        return true;
    const QString title = notes ? tr("%1 (notes)").arg(titleOf(id)) : titleOf(id);
    if (!m_damaged.contains(key)) {
        if (error)
            *error = (!notes && m_missing.contains(id))
                         ? tr("\"%1\" cannot be changed here: %2, and its place is kept for it. Close the project and open "
                              "it again once the sync program is done.")
                               .arg(title, whyNotEditable(id, notes))
                         : tr("\"%1\" cannot be changed here: %2, and such a file is never written over. Check the file's "
                              "permissions or the drive it is on, then close and reopen the project.")
                               .arg(title, whyNotEditable(id, notes));
        return false;
    }
    // the file that was read: the one of the project's format, or the other one when only that is there
    const QString base = m_project->contentDir(id) + (notes ? QLatin1String("/notes.") : QLatin1String("/text."));
    QString path = base + extension();
    if (!QFile::exists(path))
        path = base + (m_project->storage == StorageFormat::Rtf ? QStringLiteral("md") : QStringLiteral("rtf"));
    const QString kept = QFile::exists(path)
                             ? syncsafe::keepCopy(path, m_project->settingsDir(),
                                                  notes ? tr("%1 notes (damaged)").arg(titleOf(id)) : tr("%1 text (damaged)").arg(titleOf(id)))
                             : QString();
    if (kept.isEmpty()) {
        if (error)
            *error = tr("The damaged file of \"%1\" could not be kept as a copy in %2, so it was not replaced. Check that "
                        "the project folder is writable and the disk is not full.")
                         .arg(title, QDir::toNativeSeparators(m_project->settingsDir() + QStringLiteral("/conflicts")));
        return false;
    }
    m_unreadable.remove(key);
    m_damaged.remove(key);
    m_shown.remove(key);
    m_toldNotKept.remove(key);
    if (copy)
        *copy = kept;
    emit editableAgain(id, notes);
    return true;
}

bool DocumentStore::keepOtherVersion(const QString &id, bool notes, const QString &path)
{
    BinderItem *it = m_project->item(id);
    const QString title = it ? it->title : id;
    const QString what = notes ? tr("%1 notes").arg(title) : tr("%1 text").arg(title);
    const QString copy = syncsafe::keepCopy(path, m_project->settingsDir(), what);
    bool snap = false;
    if (!notes && it && it->hasText()) {
        // the other version also as a snapshot of the document: it can be compared and taken back
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            QTextDocument other;
            deserialize(id, f.readAll(), &other, path.endsWith(QLatin1String(".rtf")));
            const QString when = QFileInfo(path).lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
            snap = snapshots::take(m_project, id, tr("Other version (changed elsewhere, %1)").arg(when), nullptr, &other);
        }
    }
    if (copy.isEmpty() && !snap)
        return false; // nothing kept: no notice that says so (0.64.1)
    emit m_project->conflictKept(id, notes ? tr("notes") : tr("text"), copy, snap);
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
    forgetDocument(id);
    removeDocumentFiles(id);
}

void DocumentStore::removeDocumentFiles(const QString &id)
{
    pathsafety::removeTreeInside(m_project->contentDir(id), m_project->folderPath()); // (0.64.1, audit S-1)
}

void DocumentStore::forgetDocument(const QString &id)
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
}

// 0.65.0 (audit F06): every step checked; false with the file that failed
static bool copyDir(const QString &from, const QString &to, QString *error)
{
    QDir src(from);
    if (!src.exists())
        return true;
    if (!QDir().mkpath(to)) {
        if (error)
            *error = QObject::tr("Cannot create %1").arg(QDir::toNativeSeparators(to));
        return false;
    }
    for (const QFileInfo &fi : src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        if (savebatch::isStaged(fi.fileName()))
            continue;
        if (fi.isDir()) {
            if (!copyDir(fi.absoluteFilePath(), to + QLatin1Char('/') + fi.fileName(), error))
                return false;
        } else if (!safesave::copyFile(fi.absoluteFilePath(), to + QLatin1Char('/') + fi.fileName(), error)) {
            return false;
        }
    }
    return true;
}

bool DocumentStore::copyDocument(const QString &fromId, const QString &toId, QString *error)
{
    if (!saveDocument(fromId, error) || !saveDocument(keyFor(fromId, true), error))
        return false;
    if (!copyDir(m_project->contentDir(fromId), m_project->contentDir(toId), error)) {
        pathsafety::removeTreeInside(m_project->contentDir(toId), m_project->folderPath());
        return false;
    }
    return true;
}

// copy the source's image resources (a picture read from RTF is held by its document only)
void DocumentStore::carryPictures(const QTextDocument *namesIn, const QTextDocument *from, QTextDocument *to)
{
    // 0.70.0 (audit A U-03): a picture of a text is a file in its document's folder, named relative to it; the
    // picture is looked up by its name resolved against the target's folder, so it is added there under that
    // name too (before, only under the bare name, which the target never looked up: Merge, Split at Selection and
    // new documents from a template saved the text without its pictures)
    QSet<QString> done;
    auto walk = [&](const QTextFrame *frame, const auto &self) -> void {
        for (auto fit = frame->begin(); !fit.atEnd(); ++fit) {
            if (QTextFrame *child = fit.currentFrame()) {
                self(child, self);
                continue;
            }
            const QTextBlock b = fit.currentBlock();
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const QTextCharFormat cf = it.fragment().charFormat();
                if (!cf.isImageFormat())
                    continue;
                const QString name = cf.toImageFormat().name();
                if (name.isEmpty() || done.contains(name))
                    continue;
                done.insert(name);
                const QUrl url(name);
                const QVariant v = const_cast<QTextDocument *>(from)->resource(QTextDocument::ImageResource, url);
                if (!v.isValid() || (v.canConvert<QByteArray>() && v.toByteArray().isEmpty() && !v.canConvert<QImage>()))
                    continue;
                to->addResource(QTextDocument::ImageResource, url, v);
                const QUrl resolved = to->baseUrl().resolved(url);
                if (resolved != url)
                    to->addResource(QTextDocument::ImageResource, resolved, v);
            }
        }
    };
    walk(namesIn->rootFrame(), walk);
}

static void copyImageResources(const QTextDocument *source, QTextDocument *doc)
{
    DocumentStore::carryPictures(source, source, doc);
}

void DocumentStore::setContent(const QString &id, const QTextDocument *source)
{
    QTextDocument *doc = text(id);
    doc->setUndoRedoEnabled(false);
    copyImageResources(source, doc);
    QTextCursor c(doc);
    c.select(QTextCursor::Document);
    // 0.65.0: an empty source empties the text (insertFragment does nothing with an empty fragment, so a roll back to
    // an empty snapshot kept the text)
    if (source->isEmpty())
        c.removeSelectedText();
    else
        c.insertFragment(QTextDocumentFragment(source));
    // insertFragment keeps the first block format of the target: fix it
    QTextCursor first(doc);
    first.setBlockFormat(source->begin().blockFormat());
    first.setBlockCharFormat(source->begin().charFormat());
    doc->setUndoRedoEnabled(true);
    // 0.81.1 (found while checking audit F-01): the whole text is laid out anew. Under Qt 6.12, an editor showing the
    // text after a whole replacement (Roll Back above all) kept the old layout of its first paragraph and showed an
    // empty page until something else made it lay the text out again.
    doc->markContentsDirty(0, doc->characterCount());
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
    // 0.64.2: pictures in notes read from a .scriv were saved without their picture and lost
    copyImageResources(source, doc);
    QTextCursor c(doc);
    c.select(QTextCursor::Document);
    c.insertFragment(QTextDocumentFragment(source));
    doc->setUndoRedoEnabled(true);
    doc->markContentsDirty(0, doc->characterCount()); // 0.81.1: laid out anew, as in setContent
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
    // 0.65.0 (audit F04): written checked; when it fails the picture stays in the document's memory and the
    // next save writes it (or says that it cannot)
    QByteArray png;
    QBuffer buf(&png);
    if (buf.open(QIODevice::WriteOnly) && img.save(&buf, "PNG"))
        safesave::writeFile(QDir(dir).filePath(rel), png);
    if (QTextDocument *d = loadedText(id)) {
        d->addResource(QTextDocument::ImageResource, QUrl(rel), img);
        // a document looks a picture up by its name resolved against the document's folder: kept under that
        // name too, so it is shown, and the save finds it in memory when its file could not be written
        if (d->baseUrl().isValid())
            d->addResource(QTextDocument::ImageResource, d->baseUrl().resolved(QUrl(rel)), img);
    }
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
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        Entry &e = it.value();
        const bool wasDirty = e.dirty;
        e.doc->setDefaultFont(display::baseFont(m_project->formatting, zoomFor(e.isNotes)));
        // 0.81.1 (F-01): a text that is never written is left as it was read
        if (m_project->storage == StorageFormat::Markdown && !m_unreadable.contains(it.key())) {
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
    // 0.81.1 (audit of 0.81.0, F-03): not over the save of another computer that may still be arriving (its journal has
    // the name this batch's journal gets)
    if (!m_project->finishInterruptedSave(error))
        return false;
    QList<QString> ids;
    for (BinderItem *it : m_project->allItems())
        if (it->hasText())
            ids << it->id;
    ids << QStringLiteral("PROJECT");
    // 0.81.0 (audit of 0.80.0, U-04): one document at a time, read in the old format and written in the new one;
    // those that were not loaded before leave the memory once written (before, every text and note of the project
    // was loaded first and kept)
    QSet<QString> loadedBefore;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
        loadedBefore.insert(it.key());
    const StorageFormat was = m_project->storage;
    bool ok = true;
    // 0.65.0 (audit F15): all texts and the project file in one save batch: either every text is in the new format
    // (and the old files are removed after that), or nothing changed on disk and the project keeps its format
    savebatch::Batch batch(m_project->folderPath(), m_project->journalFile());
    savebatch::Scope scope(&batch);
    QSet<QString> converted;
    auto convertOne = [&](const QString &key) {
        auto it = m_entries.find(key);
        if (it == m_entries.end() || converted.contains(key))
            return true;
        converted.insert(key);
        // 0.81.1 (audit of 0.81.0, F-01): a text whose file cannot be read stays in its file as it is (it is read from
        // there in the old format, which the store still reads)
        if (m_unreadable.contains(key))
            return true;
        const bool empty = it->doc->isEmpty();
        const bool notes = it->isNotes;
        const QString id = notes ? it.key().mid(2) : it.key();
        const QString oldPath = m_project->contentDir(id) + (notes ? QLatin1String("/notes.") : QLatin1String("/text."))
                                + (to == StorageFormat::Rtf ? QStringLiteral("md") : QStringLiteral("rtf"));
        if (empty && !QFile::exists(oldPath))
            return true;
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
        return writeEntry(it.key(), it.value(), error);
    };
    for (const QString &id : std::as_const(ids)) {
        m_project->storage = was; // read in the old format
        if (id != QStringLiteral("PROJECT"))
            text(id);
        notes(id);
        m_project->storage = to;
        ok = convertOne(id) && convertOne(keyFor(id, true));
        if (!ok)
            break;
        if (!loadedBefore.contains(id) && !loadedBefore.contains(keyFor(id, true)))
            forgetDocument(id); // its file is staged; the done step finds no entry and only removes the old file
    }
    // documents loaded before that are not among the texts (as before)
    if (ok) {
        const QStringList keys = m_entries.keys();
        for (const QString &key : keys)
            if (!convertOne(key)) {
                ok = false;
                break;
            }
    }
    m_plainCache.clear();
    m_project->markIndexDirty();
    if (ok && !m_project->saveIndex(error))
        ok = false;
    if (ok && !batch.commit(error))
        ok = false;
    if (!ok) {
        batch.drop();
        m_project->storage = was;
        m_project->markIndexDirty();
    }
    return ok;
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
        // 0.81.1 (audit of 0.81.0, F-01): a text whose file cannot be read is never written: it keeps the look it was
        // read with, and the writer is told it was left out
        if (m_unreadable.contains(it.key())) {
            noteSkipped(it->isNotes ? it.key().mid(2) : it.key(), it->isNotes);
            continue;
        }
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
            if (m_unreadable.contains(key)) { // 0.81.1 (F-01): never written, told as left out
                noteSkipped(r.id, r.notes);
                continue;
            }
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
    // 0.81.1 (audit of 0.81.0, F-01): a text whose file cannot be read is left as it is (it is never written, so a
    // change would exist only in memory while the command counted it as done); the caller can name it
    // (takeSkippedUnreadable)
    if (it != m_entries.end() && m_unreadable.contains(key)) {
        noteSkipped(id, notes);
        return 0;
    }
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
    if (m_unreadable.contains(key)) {
        noteSkipped(id, notes);
        return 0;
    }
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

QStringList DocumentStore::textsNotArrived() const
{
    QStringList out;
    if (!m_project->scrivenerPath.isEmpty())
        return out; // the working copy of a .scriv is on this computer
    for (BinderItem *it : m_project->allItems()) {
        if (!it->hasText() || it->charCount <= 0)
            continue;
        const QString base = m_project->contentDir(it->id) + QLatin1String("/text.");
        if (!QFileInfo::exists(base + QLatin1String("md")) && !QFileInfo::exists(base + QLatin1String("rtf")))
            out << it->id;
    }
    return out;
}

void DocumentStore::recount(const QString &id)
{
    BinderItem *it = m_project->item(id);
    if (!it || m_missing.contains(id))
        return;
    const text::Counts c = text::count(plainText(id));
    const bool changed = c.words != it->wordCount || c.chars != it->charCount;
    const bool hadText = it->charCount > 0;
    it->wordCount = c.words;
    it->charCount = c.chars;
    if (changed) {
        m_project->recordHistory();
        m_project->notifyItemCounted(it); // the project file follows with the next full save or a minute later (0.60.5)
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
    QString pictureError;
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
            QByteArray png;
            QBuffer buf(&png);
            QString why;
            if (!buf.open(QIODevice::WriteOnly) || !img.save(&buf, "PNG") || !safesave::writeFile(QDir(outDir).filePath(rel), png, &why))
                if (pictureError.isEmpty())
                    pictureError = why.isEmpty() ? QObject::tr("a picture could not be converted") : why;
            return rel;
        };
        QString text = md::write(&doc, f, saver);
        if (!pictureError.isEmpty()) { // 0.65.0 (audit F04)
            if (error)
                *error = QObject::tr("Not converted: %1").arg(pictureError);
            return false;
        }
        if (standardMarkdown)
            text = md::toStandardMarkdown(text);
        out = text.toUtf8();
    }
    QSaveFile o(outPath);
    if (!o.open(QIODevice::WriteOnly) || safesave::write(o, out) != out.size() || !safesave::commit(o)) {
        if (error)
            *error = QObject::tr("Cannot write %1").arg(outPath);
        return false;
    }
    return true;
}

} // namespace qb
