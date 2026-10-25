#pragma once
#include "core/PageSetup.h"
#include "core/ProjectTypes.h"

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
    void copyDocument(const QString &fromId, const QString &toId);
    // Replace the content of a document (import, templates, split/merge).
    void setContent(const QString &id, const QTextDocument *source);
    void setNotesContent(const QString &id, const QTextDocument *source);

    // Serialize / deserialize in the project storage format (snapshots)
    QByteArray serialize(const QString &id, const QTextDocument *doc);
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

    // The text (or notes) of this document could not be read: it must not be edited.
    bool isUnreadable(const QString &id, bool notes = false) const { return m_unreadable.contains(keyFor(id, notes)); }

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

private:
    struct Entry {
        QTextDocument *doc = nullptr;
        bool dirty = false;
        bool isNotes = false;
        QHash<QString, QString> savedImages; // resource name -> relative path
    };
    QTextDocument *load(const QString &id, bool notes);
    void readFile(const QString &id, bool notes, QTextDocument *doc);
    bool writeEntry(const QString &key, Entry &e, QString *error);
    QString keyFor(const QString &id, bool notes) const { return notes ? QStringLiteral("n:") + id : id; }
    void preloadImages(QTextDocument *doc, const QString &dir);

    Project *m_project;
    QHash<QString, Entry> m_entries;
    QSet<QString> m_unreadable; // keys of files that are there but could not be read
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
