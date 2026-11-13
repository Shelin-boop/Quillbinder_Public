#pragma once
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <functional>
namespace qb {
class Project;
namespace backup {
QString defaultDirectory();

// 0.70.0 (audit A F-02, B F-3): each project's backups are in a folder of their own inside the backup folder,
// "<title> <key>", where the key is made from the project folder's place on this computer (so a copy of the
// project, or a restored backup, has its own); the folder keeps its name when the title changes.
QString projectKey(const Project *p);
QString projectFolder(const Project *p, const QString &dir);

enum class Where {
    ProjectFolder, // the project's own folder inside dir; the newest `keep` complete backups stay
    AsChosen       // straight into dir (Back Up To...); nothing is deleted
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
// it is kept in the work folder (*outPath names it there) and *error says so (Where::ProjectFolder only: a backup
// to a chosen folder that cannot be put there is removed).
bool backupProject(Project *p, const QString &dir, int keep, QString *outPath, QString *error,
                   Where where = Where::ProjectFolder, bool *waiting = nullptr);
// This project's backups in its folder, oldest first.
QStringList backupsFor(const Project *p, const QString &dir);
// Backups of the time before 0.70.0, named by the title only, straight in dir (never deleted automatically:
// they may be another project's of the same title).
QStringList backupsFor(const QString &projectTitle, const QString &dir);

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
