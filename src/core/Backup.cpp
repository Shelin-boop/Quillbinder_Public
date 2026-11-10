#include "core/Backup.h"
#include "AppInfo.h"
#include "core/Paths.h"
#include "core/Project.h"
#include "core/Zip.h"
#include "text/TextUtil.h"

#include <QDir>

#include <algorithm>
#include <QFileInfo>
#include <QStandardPaths>

namespace qb {
namespace backup {

QString defaultDirectory()
{
    return dataDir() + QLatin1String("/Backups");
}

QStringList backupsFor(const QString &projectTitle, const QString &dir)
{
    const QString prefix = text::safeFileName(projectTitle) + QLatin1String(" bak ");
    QStringList out;
    QFileInfoList files = QDir(dir).entryInfoList({prefix + QLatin1String("*.zip")}, QDir::Files);
    // oldest first: by the date in the name; "... 12-24-56 (2)" (a second backup in that second) after "... 12-24-56"
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) { return a.completeBaseName() < b.completeBaseName(); });
    for (const QFileInfo &fi : files)
        out << fi.absoluteFilePath();
    return out;
}

bool backupProject(Project *p, const QString &dirIn, int keep, QString *outPath, QString *error)
{
    const QString dir = dirIn.isEmpty() ? defaultDirectory() : dirIn;
    if (!QDir().mkpath(dir)) {
        if (error)
            *error = QObject::tr("Cannot create the backup folder %1").arg(dir);
        return false;
    }
    // 0.64.1: a backup is made only of a project whose latest changes were saved (before, a failed save
    // was passed over and the backup reported success without them)
    {
        QString why;
        if (!p->save(&why)) {
            if (error)
                *error = QObject::tr("The project could not be saved first, so no backup was made: %1").arg(why);
            return false;
        }
    }
    const QString stem = text::safeFileName(p->title()) + QLatin1String(" bak ")
                         + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH-mm-ss"));
    // two backups in the same second (File > Save twice with "back up with each manual save",
    // 0.21): the second gets a number instead of failing
    QString path = QDir(dir).filePath(stem + QLatin1String(".zip"));
    for (int n = 2; QFile::exists(path) && n < 100; ++n)
        path = QDir(dir).filePath(stem + QStringLiteral(" (%1).zip").arg(n));
    // written under a temporary name, checked, then renamed: a backup that was cut off
    // (a crash, a full disk) never looks like a finished one (0.20)
    const QString part = path + QLatin1String(".part");
    QStringList zwSkipped;
    {
        ZipWriter zw(part);
        if (!zw.isOpen()) {
            if (error)
                *error = zw.errorString();
            return false;
        }
        // a project in Scrivener format: the .scriv is the project; the working copy holds
        // what only this program knows (keywords, collections, styles, objects ...)
        const QString folderName = QFileInfo(p->displayFolder()).fileName();
        bool ok = zw.addDirectory(p->displayFolder(), folderName);
        if (!p->scrivenerPath.isEmpty())
            ok = zw.addDirectory(p->folderPath(), QObject::tr("%1 - working copy of %2").arg(QFileInfo(p->displayFolder()).completeBaseName(),
                                                                                          QLatin1String(QB_APP_NAME))
                                                      + QLatin1Char('/') + QFileInfo(p->folderPath()).fileName())
                 && ok;
        zwSkipped = zw.skipped();
        if (!zw.close() || !ok) {
            const QString why = zw.errorString();
            QFile::remove(part);
            if (error)
                *error = QObject::tr("Could not write the backup %1%2").arg(path, why.isEmpty() ? QString() : QStringLiteral(": ") + why);
            return false;
        }
    }
    QString why;
    if (ZipWriter::verify(part, &why) < 0 || !QFile::rename(part, path)) {
        QFile::remove(part);
        if (error)
            *error = QObject::tr("The backup %1 could not be checked (%2), so it was removed.").arg(path, why.isEmpty() ? QObject::tr("rename failed") : why);
        return false;
    }
    // 0.65.0 (audit F03): a backup with files missing says so in its name, and never counts as one of the backups
    // to keep: older complete backups are deleted only after a complete one
    if (!zwSkipped.isEmpty()) {
        QString named = QDir(dir).filePath(QFileInfo(path).completeBaseName() + QLatin1String(" (incomplete).zip"));
        for (int n = 2; QFile::exists(named) && n < 100; ++n)
            named = QDir(dir).filePath(QFileInfo(path).completeBaseName() + QStringLiteral(" (incomplete %1).zip").arg(n));
        if (QFile::rename(path, named))
            path = named;
    }
    if (outPath)
        *outPath = path;
    if (error && !zwSkipped.isEmpty())
        *error = QObject::tr("The backup is incomplete: %n file(s) could not be read and are missing from it: %1. Older "
                             "backups were kept.", nullptr, int(zwSkipped.size()))
                     .arg(zwSkipped.join(QLatin1String(", ")));
    if (keep > 0 && zwSkipped.isEmpty()) {
        QStringList complete, incomplete;
        for (const QString &f : backupsFor(p->title(), dir))
            (QFileInfo(f).completeBaseName().contains(QLatin1String(" (incomplete")) ? incomplete : complete) << f;
        while (complete.size() > keep)
            QFile::remove(complete.takeFirst());
        // incomplete ones older than the oldest complete backup that stays are of no more use
        if (!complete.isEmpty()) {
            const QString oldest = QFileInfo(complete.first()).completeBaseName();
            for (const QString &f : std::as_const(incomplete))
                if (QFileInfo(f).completeBaseName() < oldest)
                    QFile::remove(f);
        }
    }
    return true;
}

} // namespace backup
} // namespace qb
