#pragma once
// A project in a folder that a sync program keeps the same on several computers (OneDrive,
// Dropbox, Nextcloud, Syncthing ...): what the program does so that two computers do not
// overwrite each other's work (0.64). It works on the level of files only, so it needs to know
// nothing about the sync program:
//  - the lock: a small file in the project names the computer that has it open and is renewed
//    every few minutes; another computer sees it once the sync program has brought it there;
//  - stamps: the size and time of every file the program read or wrote; a file whose stamp
//    changed meanwhile was written by someone else (another computer, another program), and the
//    program keeps that version before it writes its own (a copy in settings/conflicts, and a
//    snapshot for the text of a document).
// Qt Core only (every system).
#include <QDateTime>
#include <QString>

#include <functional>

namespace qb {
namespace syncsafe {

// The name of this computer in lock files and in the names of kept copies.
QString deviceName();
QString userName();
// One per running program (a new one at every start).
QString instanceId();

// ---------------------------------------------------------------- stamps

struct Stamp {
    bool known = false;   // false: nothing was recorded (no check)
    bool exists = false;
    qint64 size = -1;
    qint64 modifiedMs = 0;
    bool sameAs(const Stamp &o) const { return exists == o.exists && size == o.size && modifiedMs == o.modifiedMs; }
};
Stamp stampOf(const QString &path);
// The file was written by someone else since `before` was taken.
bool changedSince(const Stamp &before, const QString &path);

// Copies a file another computer or program wrote into <settingsDir>/conflicts as
// "<what> (<device or 'other'> yyyy-mm-dd hh-mm-ss).<ext>"; returns the copy's path ("" if it
// could not be made). what: a readable name ("Chapter 3 text", "project file").
QString keepCopy(const QString &file, const QString &settingsDir, const QString &what);

// ---------------------------------------------------------------- the lock

// The first line stays "<host> <pid>" as versions before 0.64 wrote and read it (they see the
// other lines as part of the pid, which then counts as not running).
struct LockInfo {
    bool exists = false;
    QString device;
    QString user;
    QString instance;
    QString program;
    qint64 pid = 0;
    QDateTime opened;   // UTC
    QDateTime alive;    // UTC: the last renewal (old lock files: the file's time)
};
LockInfo readLock(const QString &file);
// Writes the lock of this program (opened: when the project was opened here; alive = now).
bool writeLock(const QString &file, const QDateTime &opened);
// Removes the lock only when it is this program's (another computer may have taken it).
void removeOwnLock(const QString &file);

enum class LockState {
    None,             // no lock, or an empty / unreadable one
    Ours,             // this program wrote it
    OtherWindow,      // this computer, another program that is still running
    LeftOverHere,     // this computer, a program that is no longer running (crash, power cut)
    OtherDevice,      // another computer, renewed within staleAfter
    OtherDeviceStale  // another computer, not renewed for longer (not closed there, or switched off)
};
// staleAfterSecs: how long a lock of another computer counts as active without renewal.
LockState judge(const LockInfo &l, qint64 staleAfterSecs, const std::function<bool(qint64)> &processAlive);

// The renewal interval and the time after which a lock of another computer counts as left
// over (QB_LOCK_RENEW_MS and QB_LOCK_STALE_S change them, for the tests).
int renewIntervalMs();
qint64 staleAfterSeconds();

} // namespace syncsafe
} // namespace qb
