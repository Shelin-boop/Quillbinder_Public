#pragma once
// A save that lands as a whole (0.65.0, external audit of 2026-11-10, F19).
//
// Before 0.65 every file of a save (texts, notes, pictures, the Objects sheet, the project file) was
// replaced on its own: each file was safe, but a save cut off in the middle left some files of the new
// state beside others of the old one. Now a save first writes every new file under a temporary name
// beside its target ("<file>.qbstage", synced), then a journal that lists them (settings/journal.json:
// for each file its target, the SHA-256 of the new and of the replaced content, its title and its place
// in the binder), and only then renames them over their targets and removes the journal.
//
// At the next opening, `recover()` looks for a journal: its files are put in place (when the target is
// still the version the save replaced), or kept as copies in settings/conflicts (when something else
// changed the target meanwhile). Temporary files without a journal belong to a save that was cut off
// before it was complete: they are removed, and the project stays as it was at the save before.
//
// A save of one file needs no journal: the rename alone is all or nothing.
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace qb {
namespace savebatch {

constexpr const char *kStageSuffix = ".qbstage";

// A file name of a save in progress (never read as project data, never backed up)
bool isStaged(const QString &fileName);
// 0.70.0: a folder's entries (a new or renamed file in it) reach the disk (POSIX; nothing elsewhere)
void syncDir(const QString &dir);

class Batch
{
public:
    // folder: the project folder (journal paths are relative to it); journal: the journal's file
    Batch(const QString &folder, const QString &journal);
    ~Batch(); // a batch that was not committed is dropped: its temporary files go
    Batch(const Batch &) = delete;
    Batch &operator=(const Batch &) = delete;

    // The new content of `target`, written beside it and synced. False with a reason (nothing changed).
    bool stage(const QString &target, const QByteArray &data, const QString &title, const QString &place,
               QString *error);
    // Runs after the commit put every file in place (marks cleared, stamps taken)
    void after(std::function<void()> done);
    int size() const { return m_items.size(); }
    QString folder() const { return m_folder; }
    bool contains(const QString &target) const;

    // Journal (2 files or more), renames, journal removed, the `after` steps. False: nothing was renamed when
    // the journal could not be written; when a rename failed, the journal stays for the next opening.
    bool commit(QString *error);
    void drop();

private:
    struct Item {
        QString target, staged, title, place;
        QByteArray sha, before;
        qint64 size = 0;
    };
    QString m_folder, m_journal;
    QVector<Item> m_items;
    QVector<std::function<void()>> m_after;
    bool m_done = false;
    bool m_journalWritten = false; // its temporary files belong to the journal now: recover() handles them
};

// The batch of the save that runs now (nullptr outside a save; one at a time, main thread only)
Batch *current();

class Scope
{
public:
    explicit Scope(Batch *b);
    ~Scope();

private:
    Batch *m_prev;
};

// Writes `data` to `path`: into the running batch when there is one, else at once (QSaveFile, checked).
// `done` runs once the file is in place (at once, or after the batch's commit).
bool put(const QString &path, const QByteArray &data, QString *error, const QString &title = QString(),
         const QString &place = QString(), std::function<void()> done = nullptr);

// Replaces `to` with `from` in one step (POSIX rename; MoveFileEx on Windows)
bool replace(const QString &from, const QString &to);

struct Recovery {
    QStringList finished; // put in place now: "title (file)"
    QStringList kept;     // the target had changed: the new version kept as a copy
    QStringList lost;     // listed in the journal, but the temporary file was not there
    int dropped = 0;      // temporary files of a save cut off before its journal
    bool any() const { return !finished.isEmpty() || !kept.isEmpty() || !lost.isEmpty() || dropped > 0; }
    QString text() const; // for the writer
};

// At opening, before anything is read (also after a failed commit in this session)
Recovery recover(const QString &folder, const QString &journal, const QString &settingsDir);

} // namespace savebatch
} // namespace qb
