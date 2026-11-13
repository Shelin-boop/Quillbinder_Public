#pragma once
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <functional>
namespace qb {
class Project;
namespace backup {
QString defaultDirectory();

// 0.91.0 (the writer's decision of 2026-11-13: "that's why i chose a backup folder, that all the backups go in
// there"): the backups of every project are straight in the backup folder, named "<title> bak <date time>.zip" as
// always. A project's backups are the files that carry its title: two projects with the same title (a folder copied
// by hand, a backup unpacked by hand) share their backups and the number kept, as before 0.70.0. Restore From Backup
// renames what it restores, so it does not mix.
// 0.70.0 to 0.90.4 kept each project's backups in a folder of their own inside the backup folder, "<title> <key>" (the
// key made from the project folder's place on this computer). Such a folder is left as it is (the writer's choice):
// nothing is written, counted or deleted there any more; Restore From Backup still offers its backups.
QString projectKey(const Project *p);
// That folder of 0.70.0 to 0.90.4; "" when the backup folder holds none for this project.
QString formerProjectFolder(const Project *p, const QString &dir);

enum class Where {
    BackupFolder, // the backup folder of the options; of this project's backups the newest `keep` complete ones stay
    AsChosen      // a folder chosen for this one backup (Back Up To...); nothing is deleted
};

// 0.90.3: a backup is packed and checked outside the backup folder, in a folder of the program's own
// (<data>/backup-work/<a key of the backup folder>), and only the finished file is put into the backup folder, in one
// step and under its final name: renamed there when both folders are on one drive, else copied straight to the final
// name and compared with the original. So a backup folder that a sync program watches (or that is on a drive of its
// own) never sees a file that is still being written, and no temporary name. A finished backup that cannot be put
// into the backup folder (not there, not writable, full) waits in the work folder, and the next backup into the same
// folder puts it in place.
QString workDirectory();
// The work folder of one backup folder (it holds target.txt with that folder's place).
QString workFolderFor(const QString &backupFolder);

// 0.90.3: only files named as this program names its backups count for the clean-up and can be deleted by it:
// "<title> bak <yyyy-MM-dd HH-mm-ss>[ (n)][ (incomplete[ n])].zip" (n from 2). Any other file in a project's backup
// folder is left alone, such as the copies a sync program makes of a backup ("...-<computer>-safeBackup-0001.zip",
// "... (conflicted copy).zip"): counted as backups of that moment, they pushed the real older backups out.
bool isOwnBackupName(const QString &fileName);

// Zips the whole project folder. True with *error set: written, but files could not be read; such a backup is
// named "... (incomplete).zip" and no older backup is deleted for it (0.65.0). The settings/lock file is left
// out (0.70.0, audit A U-09).
// 0.90.3: false with *waiting true: the backup was made and checked but could not be put into the backup folder;
// it is kept in the work folder (*outPath names it there) and *error says so (Where::BackupFolder only: a backup
// to a chosen folder that cannot be put there is removed).
bool backupProject(Project *p, const QString &dir, int keep, QString *outPath, QString *error,
                   Where where = Where::BackupFolder, bool *waiting = nullptr);
// The backups in dir that carry this project's title, oldest first: those in the program's own name form, and other
// files that begin "<title> bak " (a sync program's copy of a backup), which Restore From Backup offers too.
QStringList backupsFor(const Project *p, const QString &dir);
// The backups in this project's folder of 0.70.0 to 0.90.4 (see formerProjectFolder), oldest first.
QStringList formerBackupsFor(const Project *p, const QString &dir);

// 0.70.0 (audit A U-06): while a backup is packed and checked (in a thread of its own) the calling thread hands
// over to this handler every 50 ms: what is done (a title; "" when it is over), bytes done, bytes in all, and
// *stop to cancel. The window shows a progress window with it.
void setProgressHandler(std::function<void(const QString &what, qint64 done, qint64 total, bool *stop)> handler);

// 0.81.0 (audit of 0.80.0, S-03): a backup that the closing would have made but the program was ended by a signal
// (a kill command, a shutdown that does not wait): the project is noted in <data>/backups-due.txt, and the next
// opening makes the backup and says so. takeDue: true when the project was noted (the note goes).
void markDue(const QString &projectFile);
bool takeDue(const QString &projectFile);

// 0.70.0 (audit A U-05): unpacks a backup into a new folder "<name> (restored <date time>)" inside intoParent;
// that folder, or "" on failure. *projectFile: what to open (the .qbx, or the .scriv folder).
QString restore(const QString &zipFile, const QString &intoParent, QString *projectFile, QString *error);
} // namespace backup
} // namespace qb
