#include "core/Backup.h"
#include "AppInfo.h"
#include "core/Paths.h"
#include "core/Project.h"
#include "core/SaveBatch.h"
#include "core/Zip.h"
#include "text/TextUtil.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <QFileInfo>
#include <QStandardPaths>

namespace qb {
namespace backup {

namespace {
std::function<void(const QString &, qint64, qint64, bool *)> &progressHandler()
{
    static std::function<void(const QString &, qint64, qint64, bool *)> h;
    return h;
}

qint64 sizeOf(const QString &dir)
{
    qint64 n = 0;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        n += it.fileInfo().size();
    }
    return n;
}

bool isIncomplete(const QString &file) { return QFileInfo(file).completeBaseName().contains(QLatin1String(" (incomplete")); }

QStringList zipsIn(const QString &dir, const QString &pattern)
{
    QStringList out;
    QFileInfoList files = QDir(dir).entryInfoList({pattern}, QDir::Files);
    // oldest first: by the date in the name; "... 12-24-56 (2)" (a second backup in that second) after "... 12-24-56"
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) { return a.completeBaseName() < b.completeBaseName(); });
    for (const QFileInfo &fi : files)
        out << fi.absoluteFilePath();
    return out;
}
} // namespace

void setProgressHandler(std::function<void(const QString &, qint64, qint64, bool *)> handler)
{
    progressHandler() = std::move(handler);
}

QString defaultDirectory()
{
    return dataDir() + QLatin1String("/Backups");
}

QString projectKey(const Project *p)
{
    QString place = QFileInfo(p->displayFolder()).absoluteFilePath();
    const QString canonical = QFileInfo(place).canonicalFilePath();
    if (!canonical.isEmpty())
        place = canonical;
    return QString::fromLatin1(QCryptographicHash::hash(QDir::cleanPath(place).toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
}

QString projectFolder(const Project *p, const QString &dirIn)
{
    const QString dir = dirIn.isEmpty() ? defaultDirectory() : dirIn;
    const QString key = projectKey(p);
    // the folder keeps its name when the project's title changes: it is found by its key
    const QStringList found = QDir(dir).entryList({QStringLiteral("* ") + key}, QDir::Dirs | QDir::NoDotAndDotDot);
    if (!found.isEmpty())
        return QDir(dir).filePath(found.first());
    return QDir(dir).filePath(text::safeFileName(p->title()) + QLatin1Char(' ') + key);
}

QStringList backupsFor(const Project *p, const QString &dir)
{
    return zipsIn(projectFolder(p, dir), QStringLiteral("* bak *.zip"));
}

QStringList backupsFor(const QString &projectTitle, const QString &dir)
{
    return zipsIn(dir, text::safeFileName(projectTitle) + QLatin1String(" bak *.zip"));
}

bool backupProject(Project *p, const QString &dirIn, int keep, QString *outPath, QString *error, Where where)
{
    const QString root = dirIn.isEmpty() ? defaultDirectory() : dirIn;
    // 0.70.0 (audit A F-02, B F-3): the backups of a project go into a folder of its own, found by the project
    // folder's place, and only they are counted and cleaned up; another project with the same title (a copy, a
    // restored backup) has its own folder. Back Up To... writes into the chosen folder as it is.
    const QString dir = where == Where::ProjectFolder ? projectFolder(p, root) : root;
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
    // what the work needs from the project, taken here in the window's thread
    const QString display = p->displayFolder();
    const QString folderName = QFileInfo(display).fileName();
    const QString working = p->scrivenerPath.isEmpty() ? QString() : p->folderPath();
    const QString workingName = working.isEmpty() ? QString()
                                                  : QObject::tr("%1 - working copy of %2").arg(QFileInfo(display).completeBaseName(),
                                                                                              QLatin1String(QB_APP_NAME))
                                                        + QLatin1Char('/') + QFileInfo(working).fileName();
    struct Result {
        bool ok = false;
        bool cancelled = false;
        QStringList skipped;
        QString why;
    } res;
    std::atomic<qint64> done{0};
    std::atomic<bool> cancel{false};
    // 0.70.0 (audit A U-06): packing and checking run in a thread of their own; the window's thread waits with
    // the progress window of the handler (drawn, its Cancel button working, the freeze watchdog fed)
    auto work = [&] {
        ZipWriter zw(part);
        if (!zw.isOpen()) {
            res.why = zw.errorString();
            return;
        }
        zw.setProgress(&done, &cancel);
        // a project in Scrivener format: the .scriv is the project; the working copy holds
        // what only this program knows (keywords, collections, styles, objects ...)
        bool ok = zw.addDirectory(display, folderName);
        if (!working.isEmpty() && !zw.cancelled())
            ok = zw.addDirectory(working, workingName) && ok;
        res.skipped = zw.skipped();
        res.cancelled = zw.cancelled();
        if (!zw.close() || !ok) {
            res.why = zw.errorString();
            QFile::remove(part);
            return;
        }
        QString why;
        if (ZipWriter::verify(part, &why) < 0) {
            QFile::remove(part);
            res.why = why;
            return;
        }
        res.ok = true;
    };
    const qint64 total = sizeOf(display) + (working.isEmpty() ? 0 : sizeOf(working));
    if (progressHandler()) {
        QThread *t = QThread::create(work);
        t->start();
        QElapsedTimer clock;
        clock.start();
        while (!t->wait(50)) {
            bool stop = false;
            progressHandler()(QObject::tr("Backing up \"%1\"...").arg(p->title()), done.load(), total, &stop);
            if (stop)
                cancel.store(true);
        }
        delete t;
        progressHandler()(QString(), total, total, nullptr); // done: the progress window goes
    } else {
        work();
    }
    if (res.cancelled) {
        QFile::remove(part);
        if (error)
            *error = QObject::tr("The backup was cancelled.");
        return false;
    }
    if (!res.ok) {
        if (error)
            *error = res.why.isEmpty() ? QObject::tr("Could not write the backup %1").arg(path)
                                       : QObject::tr("Could not write or check the backup %1: %2").arg(path, res.why);
        return false;
    }
    if (!QFile::rename(part, path)) {
        QFile::remove(part);
        if (error)
            *error = QObject::tr("The backup %1 could not be put in place, so it was removed.").arg(path);
        return false;
    }
    // 0.65.0 (audit F03): a backup with files missing says so in its name, and never counts as one of the backups
    // to keep: older complete backups are deleted only after a complete one
    if (!res.skipped.isEmpty()) {
        QString named = QDir(dir).filePath(QFileInfo(path).completeBaseName() + QLatin1String(" (incomplete).zip"));
        for (int n = 2; QFile::exists(named) && n < 100; ++n)
            named = QDir(dir).filePath(QFileInfo(path).completeBaseName() + QStringLiteral(" (incomplete %1).zip").arg(n));
        if (QFile::rename(path, named))
            path = named;
    }
    // 0.70.0 (audit A F-11): the new backup's name reaches the disk before older backups are deleted
    savebatch::syncDir(dir);
    if (outPath)
        *outPath = path;
    if (error && !res.skipped.isEmpty())
        *error = QObject::tr("The backup is incomplete: %n file(s) could not be read and are missing from it: %1. Older "
                             "backups were kept.", nullptr, int(res.skipped.size()))
                     .arg(res.skipped.join(QLatin1String(", ")));
    if (keep > 0 && res.skipped.isEmpty() && where == Where::ProjectFolder) {
        QStringList complete, incomplete;
        for (const QString &f : zipsIn(dir, QStringLiteral("* bak *.zip")))
            (isIncomplete(f) ? incomplete : complete) << f;
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

QString restore(const QString &zipFile, const QString &intoParent, QString *projectFile, QString *error)
{
    ZipReader zr(zipFile);
    if (!zr.isValid()) {
        if (error)
            *error = QObject::tr("%1 is not a complete ZIP file.").arg(QDir::toNativeSeparators(zipFile));
        return QString();
    }
    // the backup's first folder is the project (a .scriv or a project folder); a project in Scrivener format
    // has its working copy beside it
    QString top;
    for (const QString &n : zr.fileNames()) {
        const QString first = n.section(QLatin1Char('/'), 0, 0);
        if (!first.isEmpty() && n.contains(QLatin1Char('/'))) {
            top = first;
            break;
        }
    }
    if (top.isEmpty()) {
        if (error)
            *error = QObject::tr("The backup holds no project folder.");
        return QString();
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH-mm"));
    QString dest = QDir(intoParent).filePath(QObject::tr("%1 (restored %2)").arg(QFileInfo(top).completeBaseName(), stamp));
    for (int n = 2; QFileInfo::exists(dest) && n < 100; ++n)
        dest = QDir(intoParent).filePath(QObject::tr("%1 (restored %2, %3)").arg(QFileInfo(top).completeBaseName(), stamp).arg(n));
    if (!QDir().mkpath(dest)) {
        if (error)
            *error = QObject::tr("Cannot create %1").arg(QDir::toNativeSeparators(dest));
        return QString();
    }
    if (!zr.extractAll(dest)) {
        if (error)
            *error = QObject::tr("Not every file of the backup could be restored into %1.").arg(QDir::toNativeSeparators(dest));
        // what was written stays: the writer can look at it
    }
    const QString projectDir = QDir(dest).filePath(top);
    if (projectFile) {
        if (top.endsWith(QLatin1String(".scriv"), Qt::CaseInsensitive)) {
            *projectFile = projectDir;
        } else {
            const QStringList qbx = QDir(projectDir).entryList({QStringLiteral("*") + QLatin1String(QB_PROJECT_FILE_SUFFIX)}, QDir::Files);
            *projectFile = qbx.isEmpty() ? QString() : QDir(projectDir).filePath(qbx.first());
        }
    }
    return dest;
}

} // namespace backup
} // namespace qb
