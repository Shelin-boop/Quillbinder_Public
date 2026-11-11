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
// Zips the whole project folder. True with *error set: written, but files could not be read; such a backup is
// named "... (incomplete).zip" and no older backup is deleted for it (0.65.0). The settings/lock file is left
// out (0.70.0, audit A U-09).
bool backupProject(Project *p, const QString &dir, int keep, QString *outPath, QString *error,
                   Where where = Where::ProjectFolder);
// This project's backups in its folder, oldest first.
QStringList backupsFor(const Project *p, const QString &dir);
// Backups of the time before 0.70.0, named by the title only, straight in dir (never deleted automatically:
// they may be another project's of the same title).
QStringList backupsFor(const QString &projectTitle, const QString &dir);

// 0.70.0 (audit A U-06): while a backup is packed and checked (in a thread of its own) the calling thread hands
// over to this handler every 50 ms: what is done (a title; "" when it is over), bytes done, bytes in all, and
// *stop to cancel. The window shows a progress window with it.
void setProgressHandler(std::function<void(const QString &what, qint64 done, qint64 total, bool *stop)> handler);

// 0.70.0 (audit A U-05): unpacks a backup into a new folder "<name> (restored <date time>)" inside intoParent;
// that folder, or "" on failure. *projectFile: what to open (the .qbx, or the .scriv folder).
QString restore(const QString &zipFile, const QString &intoParent, QString *projectFile, QString *error);
} // namespace backup
} // namespace qb
