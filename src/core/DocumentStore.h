#pragma once
#include "core/PageSetup.h"
#include "core/ProjectTypes.h"
#include "core/SyncSafety.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <functional>

class QTextDocument;
class QImage;

namespace qb {

class Project;
namespace styles {
struct Changes;
}

// Owns the QTextDocuments of a project. Documents are loaded lazily and
// shared by every view that shows them (editors, combined text, quick
// reference panels), so edits appear everywhere at once.
class DocumentStore : public QObject
{
    Q_OBJECT
public:
    explicit DocumentStore(Project *project);
    ~DocumentStore() override;

    QTextDocument *text(const QString &id);
    QTextDocument *notes(const QString &id);
    QTextDocument *projectNotes();
    QTextDocument *loadedText(const QString &id) const;

    bool hasDirty() const;
    // the titles of the documents with changes not saved, at most `max` (0.70.0, for the failed-save notice)
    QStringList dirtyTitles(int max = 8) const;
    // 0.70.0 (audit A U-03): the pictures named by the text of `namesIn`, looked up in `from` (its resources or its
    // folder), become resources of `to` under the names `to` looks them up by, so that its next save writes them
    // into its own folder (text moved between documents: Merge, Split, templates)
    static void carryPictures(const QTextDocument *namesIn, const QTextDocument *from, QTextDocument *to);
    // 0.65.0: every text and notes not saved yet, as RTF files (pictures inside) into `dir`; how many were written
    int writeUnsaved(const QString &dir, QString *error = nullptr);
    bool saveDirty(QString *error);
    bool saveDocument(const QString &id, QString *error = nullptr);

    QString plainText(const QString &id);
    QString notesPlainText(const QString &id);
    void invalidatePlain(const QString &id) { m_plainCache.remove(id); }

    // file level operations
    QString textPath(const QString &id) const;
    QString notesPath(const QString &id) const;
    QString extension() const;
    void removeDocument(const QString &id);
    // 0.81.0: the two halves of removeDocument: out of the memory (nothing written for it any more), and its files
    void forgetDocument(const QString &id);
    void removeDocumentFiles(const QString &id);
    // 0.65.0 (audit F06): false with a reason when the source could not be saved or its files not copied
    bool copyDocument(const QString &fromId, const QString &toId, QString *error = nullptr);
    // Replace the content of a document (import, templates, split/merge).
    void setContent(const QString &id, const QTextDocument *source);
    void setNotesContent(const QString &id, const QTextDocument *source);

    // Serialize / deserialize in the project storage format (snapshots). 0.81.1 (audit of 0.81.0, F-02): `notes` says
    // whose pictures `doc` holds (the text's or the notes'), so that a picture written once is found again
    QByteArray serialize(const QString &id, const QTextDocument *doc, bool notes = false);
    // 0.65.0 (audit F04): why the last serialize could not write a picture of the text ("" when it could)
    QString serializeError() const { return m_serializeError; }
    // the document's title and its place in the binder ("Draft / Part One"), for the save journal (0.65.0)
    QString titleOf(const QString &id) const;
    QString placeOf(const QString &id) const;
    void deserialize(const QString &id, const QByteArray &data, QTextDocument *doc, bool rtf);

    QString importImage(const QString &id, const QString &filePath);
    QString importImage(const QString &id, const QImage &img);

    double zoom() const { return m_zoom; }
    void setZoom(double z);
    // Notes (document notes and project notes) have a zoom of their own since
    // 0.21: they are shown in the inspector, whose zoom is set apart from the
    // editors' (Ctrl+mouse wheel over the inspector). The text documents keep zoom().
    double notesZoom() const { return m_notesZoom; }
    void setNotesZoom(double z);
    // Page view: all texts of the project are shown on pages (not notes, not
    // composition mode, which suspends it while it is open).
    bool pageView() const { return m_pageView && m_pageViewSuspended == 0; }
    bool pageViewSetting() const { return m_pageView; }
    void setPageView(bool on);
    void suspendPageView(bool on);
    PageSetup pageSetup() const { return m_pageSetup; }
    void setPageSetup(const PageSetup &s);
    PageMetrics pageMetrics() const { return PageMetrics::from(m_pageSetup, m_zoom); }
    void applyFormatting();
    bool convertStorage(StorageFormat to, QString *error);
    // The project's styles change from those of oldF to those of newF: give
    // every text of the project the new formatting and style names. Leaves
    // the project with newF.
    bool restyleAll(const Formatting &oldF, const Formatting &newF, const styles::Changes &changes,
                    const std::function<bool(int, int)> &progress, QString *error);

    // Change a text of the project where it is: in the shared document if it
    // is open somewhere (the change can be undone there), otherwise in a copy
    // read from its file, which is written back if op changed it. Returns what
    // op returned (the number of changes), -1 if the file could not be written.
    int editStored(const QString &id, bool notes, const std::function<int(QTextDocument *)> &op, QString *error = nullptr);

    void recount(const QString &id);
    void recountAll();

    // The text (or notes) of this document could not be read: it must not be edited. Also a text
    // whose file has not arrived yet (0.64: the project file says the document has text, but its
    // file is missing: most likely the sync program is not finished).
    bool isUnreadable(const QString &id, bool notes = false) const { return m_unreadable.contains(keyFor(id, notes)); }
    bool isMissing(const QString &id) const { return m_missing.contains(id); }
    // 0.70.0: the file is there and was read, but it is damaged or in another character encoding: shown as read,
    // read-only, never written
    bool isDamaged(const QString &id, bool notes = false) const { return m_damaged.contains(keyFor(id, notes)); }
    // 0.81.1 (audit of 0.81.0, F-01): why a command must not change the text (or notes) of this document, as a part
    // of a sentence ("its file cannot be read"); "" when it may. Only documents read so far are known.
    QString whyNotEditable(const QString &id, bool notes = false) const;
    // 0.81.1 (F-01, Roll Back): a damaged text becomes editable again; its damaged file is kept as a copy in
    // settings/conflicts first (*copy). False with a reason when the copy could not be made, or when the file was
    // not read at all (cannot be read, has not arrived): such a text stays read-only.
    bool releaseDamaged(const QString &id, bool notes, QString *copy, QString *error);
    // 0.81.1 (F-01): the titles of the documents editStored and restyleAll left as they were because their file
    // cannot be read, since the last call
    QStringList takeSkippedUnreadable();
    // Documents the project file lists with text whose text file is not there (0.64; a project in
    // Scrivener format: none, its working copy is on this computer).
    QStringList textsNotArrived() const;

signals:
    // Emitted right before a shared document is deleted (project closed,
    // document removed): editors showing it must let go of it.
    void aboutToDeleteDocument(QTextDocument *doc);
    void textEdited(const QString &id);
    void countsUpdated(const QString &id);
    void zoomChanged(double zoom);
    void notesZoomChanged(double zoom);
    // page view switched on or off, or other paper / margins
    void pageViewChanged();
    // A text file that is there but cannot be read (permissions, a drive that went
    // away): the document is shown empty and is never saved over the file (0.20).
    void unreadableFile(const QString &id, bool notes, const QString &path, const QString &error);
    // 0.64: the project file lists text for this document, but its file is not there (yet): shown
    // empty, cannot be edited, never written, until the project is opened again.
    void missingFile(const QString &id, const QString &path);
    // 0.70.0 (audit A F-04, F-05): the file is damaged (RTF) or not in UTF-8 (Markdown): shown as well as it
    // could be read, read-only, and never written over
    void damagedFile(const QString &id, bool notes, const QString &path, const QString &why);
    // 0.70.0 (audit B F-4): the text holds control characters, which are left out when it is stored (once per document)
    void charactersLeftOut(const QString &id, bool notes, int count);
    // 0.81.1 (audit of 0.81.0, F-01): a text that was read-only because its file was damaged can be edited again
    void editableAgain(const QString &id, bool notes);
    // 0.81.1 (F-01): a document whose file cannot be read was changed in memory all the same (by a command that does
    // not ask first). Its file is not written over; the changed text was kept as an RTF file `copy` in
    // settings/conflicts ("" when that failed too: then it stays unsaved).
    void changeKeptAside(const QString &id, bool notes, const QString &copy);

private:
    static QString decodeStored(const QByteArray &data, QString *damage);
    struct Entry {
        QTextDocument *doc = nullptr;
        bool dirty = false;
        bool isNotes = false;
        QHash<QString, QString> savedImages; // resource name -> relative path
        syncsafe::Stamp stamp; // the file as read or last written here (0.64)
        // 0.64.1: the bytes as read or last written here, and whether they are RTF: tells a text
        // only shown here (marked as changed by showing it) from one the writer edited
        QByteArray read;
        bool readRtf = false;
    };
    // the file of this entry was written by someone else: keep that version (0.64); false when
    // neither a copy nor a snapshot could be kept (0.64.1: then this one must not replace it)
    bool keepOtherVersion(const QString &id, bool notes, const QString &path);
    // what would be written is what was read (or last written) here: nothing was edited (0.64.1)
    bool unchangedSinceRead(const QString &id, const Entry &e, const QByteArray &data);
    QTextDocument *load(const QString &id, bool notes);
    void readFile(const QString &id, bool notes, QTextDocument *doc, QByteArray *bytes = nullptr, bool *rtf = nullptr);
    bool writeEntry(const QString &key, Entry &e, QString *error);
    QString keyFor(const QString &id, bool notes) const { return notes ? QStringLiteral("n:") + id : id; }
    void preloadImages(QTextDocument *doc, const QString &dir);

    Project *m_project;
    QHash<QString, Entry> m_entries;
    QSet<QString> m_unreadable; // keys of files that are there but could not be read
    QString m_serializeError;   // set by serialize when a picture could not be written (0.65.0)
    QSet<QString> m_missing;    // ids of texts whose file has not arrived (0.64)
    QSet<QString> m_toldControl; // keys told about control characters left out (0.70.0)
    QSet<QString> m_damaged;    // keys of files read but damaged or in another encoding (0.70.0)
    // 0.81.1 (audit of 0.81.0, F-01): an unreadable entry as it was read (written the way the store writes, with the
    // formatting and storage of that moment): a text only shown is told from one a command changed
    struct Shown {
        QByteArray bytes;
        Formatting formatting;
        StorageFormat storage = StorageFormat::Markdown;
    };
    QByteArray shownBytes(const QTextDocument *doc, const Formatting &f, StorageFormat storage) const;
    void noteSkipped(const QString &id, bool notes);
    QHash<QString, Shown> m_shown;
    QSet<QString> m_toldNotKept; // keys whose changed text could not be kept either (told once)
    QStringList m_skippedUnreadable;
    QHash<QString, QString> m_plainCache;
    QSet<QString> m_countPending;
    QTimer m_countTimer;
    double m_zoom = 1.0;
    double m_notesZoom = 1.0;
    double zoomFor(bool notes) const { return notes ? m_notesZoom : m_zoom; }
    bool m_pageView = false;
    int m_pageViewSuspended = 0;
    PageSetup m_pageSetup;
};

// Convert a single file between Markdown and RTF (Tools menu).
// Converts between Markdown and RTF (decided by the file endings). With
// standardMarkdown, Markdown output separates paragraphs by blank lines
// (for other programs) instead of using the one-line-per-paragraph dialect.
bool convertFile(const QString &inPath, const QString &outPath, const Formatting &f, QString *error,
                 bool standardMarkdown = false);

} // namespace qb
