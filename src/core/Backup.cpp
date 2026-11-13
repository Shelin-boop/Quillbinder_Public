#include "core/Backup.h"
#include "AppInfo.h"
#include "core/PathSafety.h"
#include "core/Paths.h"
#include "core/Project.h"
#include "core/SafeSave.h"
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
#include <cerrno>
#include <cstring>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#ifdef Q_OS_UNIX
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <io.h>
#endif

#include "core/TestSwitches.h"

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

// 0.81.0 (audit of 0.80.0, F-01): the age of a backup is the date and time in its name, "<title> bak yyyy-MM-dd
// HH-mm-ss", then the number of a second backup in that second ("... (2)"), then the file's time. Before, the whole
// name was compared, title first: after the title changed to one that sorts earlier ("Zebra" to "Apple"), every new
// backup counted as the oldest and the clean-up deleted it at once, while the backup reported success.
QString ageKey(const QFileInfo &fi)
{
    static const QRegularExpression stamp(QStringLiteral(" bak (\\d{4}-\\d{2}-\\d{2} \\d{2}-\\d{2}-\\d{2})(?: \\((\\d+)\\))?"));
    const QString name = fi.completeBaseName();
    QString date, number = QStringLiteral("1");
    QRegularExpressionMatchIterator it = stamp.globalMatch(name);
    while (it.hasNext()) { // the last one: a title may hold " bak <date>" itself
        const QRegularExpressionMatch m = it.next();
        date = m.captured(1);
        number = m.captured(2).isEmpty() ? QStringLiteral("1") : m.captured(2);
    }
    if (date.isEmpty())
        date = fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH-mm-ss"));
    return date + QLatin1Char(' ') + number.rightJustified(3, QLatin1Char('0')) + QLatin1Char(' ')
           + fi.lastModified().toString(QStringLiteral("yyyyMMddHHmmsszzz"));
}

QStringList zipsIn(const QString &dir, const QString &pattern)
{
    QStringList out;
    QFileInfoList files = QDir(dir).entryInfoList({pattern}, QDir::Files);
    // oldest first
    QHash<QString, QString> key;
    for (const QFileInfo &fi : std::as_const(files))
        key.insert(fi.absoluteFilePath(), ageKey(fi));
    std::sort(files.begin(), files.end(), [&key](const QFileInfo &a, const QFileInfo &b) {
        const QString &ka = key[a.absoluteFilePath()], &kb = key[b.absoluteFilePath()];
        return ka != kb ? ka < kb : a.fileName() < b.fileName();
    });
    for (const QFileInfo &fi : std::as_const(files))
        out << fi.absoluteFilePath();
    return out;
}

// ------------------------------------------------------------------ 0.90.3: names, the work folder, putting in place

// "<title> bak <date time>[ (n)][ (incomplete[ n])].zip": 1 = up to the time, 2 = n, 3 = the incomplete note, 4 = its n
const QRegularExpression &ownForm()
{
    static const QRegularExpression own(QStringLiteral(
        "^(.+ bak \\d{4}-\\d{2}-\\d{2} \\d{2}-\\d{2}-\\d{2})(?: \\((\\d+)\\))?( \\(incomplete(?: (\\d+))?\\))?\\.zip$"));
    return own;
}

// The title in a backup's name of the program's own form ("<title> bak <date time>..."); "" for any other name.
QString ownTitle(const QString &fileName)
{
    const QRegularExpressionMatch m = ownForm().match(fileName);
    if (!m.hasMatch())
        return QString();
    const QString upToTime = m.captured(1); // "<title> bak yyyy-MM-dd HH-mm-ss"
    return upToTime.left(upToTime.size() - 24);
}

// 0.91.0: the backups in dir that this program made of the project with this title (its safe file name form), oldest
// first: the only files the clean-up counts and may delete.
QStringList ownBackupsOf(const QString &dir, const QString &title)
{
    QStringList out;
    const QStringList all = zipsIn(dir, QStringLiteral("* bak *.zip"));
    for (const QString &f : all) {
        const QString name = QFileInfo(f).fileName();
        if (isOwnBackupName(name) && ownTitle(name) == title)
            out << f;
    }
    return out;
}

// The backup's name with another number of its second ("... (3).zip"; the note "(incomplete)" stays behind it).
QString numberedName(const QString &fileName, int n)
{
    const QRegularExpressionMatch m = ownForm().match(fileName);
    if (!m.hasMatch())
        return QFileInfo(fileName).completeBaseName() + QStringLiteral(" (%1).zip").arg(n);
    return m.captured(1) + QStringLiteral(" (%1)").arg(n) + m.captured(3) + QLatin1String(".zip");
}

QByteArray sha1Of(const QString &file, qint64 *size)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Sha1);
    if (!h.addData(&f) || f.error() != QFileDevice::NoError)
        return {};
    if (size)
        *size = f.size();
    return h.result();
}

bool syncFile(QFile &f)
{
#ifdef Q_OS_UNIX
    return ::fsync(f.handle()) == 0 || errno == EINVAL || errno == ENOTSUP || errno == EOPNOTSUPP || errno == ENOSYS;
#elif defined(Q_OS_WIN)
    return ::_commit(f.handle()) == 0;
#else
    Q_UNUSED(f);
    return true;
#endif
}

enum class Put { Done, Failed, Cancelled };

// Copies a finished backup straight to its final name (never over a file that is there: NewOnly), synced, then read
// again and compared with what was sent. No temporary name in the backup folder: a program that watches the folder
// sees one file that is written once and closed. A copy that fails, differs or is cancelled is removed again.
Put copyInto(const QString &staged, const QString &target, QString *why, const std::atomic<bool> *cancel)
{
    QFile in(staged);
    if (!in.open(QIODevice::ReadOnly)) {
        *why = in.errorString();
        return Put::Failed;
    }
    QFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        *why = out.errorString();
        return Put::Failed;
    }
    QCryptographicHash sent(QCryptographicHash::Sha1);
    qint64 copied = 0;
    bool ok = true, stopped = false;
    for (;;) {
        if (cancel && cancel->load()) {
            stopped = true;
            break;
        }
        const QByteArray piece = in.read(1 << 20);
        if (piece.isEmpty())
            break;
        if (out.write(piece) != piece.size()) {
            ok = false;
            *why = out.errorString();
            break;
        }
        sent.addData(piece);
        copied += piece.size();
    }
    if (!stopped && ok) {
        if (in.error() != QFileDevice::NoError) {
            ok = false;
            *why = in.errorString();
        } else if (copied != in.size()) {
            ok = false;
            *why = QObject::tr("the copy differs from the backup");
        } else if (!out.flush()) {
            ok = false;
            *why = out.errorString();
        } else if (!syncFile(out)) {
            ok = false;
            *why = QString::fromLocal8Bit(std::strerror(errno));
        }
    }
    out.close();
    if (!stopped && ok && out.error() != QFileDevice::NoError) {
        ok = false;
        *why = out.errorString();
    }
    if (!stopped && ok) {
        qint64 size = -1;
        if (sha1Of(target, &size) != sent.result() || size != copied) {
            ok = false;
            *why = QObject::tr("the copy differs from the backup");
        }
    }
    if (stopped || !ok) {
        QFile::remove(target);
        return stopped ? Put::Cancelled : Put::Failed;
    }
    return Put::Done;
}

// Puts a finished backup of the work folder into the backup folder, in one step: renamed there (one drive), else
// copied straight to the final name. *finalPath: where it is now. The waiting file is gone only when the backup is
// in the folder, whole.
Put putInPlace(const QString &staged, const QString &dir, QString *finalPath, QString *why, const std::atomic<bool> *cancel)
{
    if (!QDir().mkpath(dir)) {
        *why = QObject::tr("the folder cannot be created");
        return Put::Failed;
    }
    const QString ownName = QFileInfo(staged).fileName();
    QString target = QDir(dir).filePath(ownName);
    if (QFileInfo::exists(target)) {
        // a file of this name came into the folder after the name was chosen
        qint64 a = -1, b = -2;
        const QByteArray ours = sha1Of(staged, &a);
        if (!ours.isEmpty() && ours == sha1Of(target, &b) && a == b) {
            // this very backup: it was put in place before, and the program ended before the waiting file was removed
            QFile::remove(staged);
            *finalPath = target;
            return Put::Done;
        }
        if (ZipWriter::verify(target) < 0) {
            // not a whole ZIP file: what an earlier copy of this backup left when the program ended in the middle
            if (!QFile::remove(target)) {
                *why = QObject::tr("a file of this name is in the way and cannot be removed");
                return Put::Failed;
            }
        } else {
            // another whole backup of this name stays; ours gets the next free number of its second
            for (int n = 2; QFileInfo::exists(target) && n < 100; ++n)
                target = QDir(dir).filePath(numberedName(ownName, n));
            if (QFileInfo::exists(target)) {
                *why = QObject::tr("a file of this name is in the way and cannot be removed");
                return Put::Failed;
            }
        }
    }
    // (test builds: QB_TEST_BACKUP_COPY takes the way of two drives on one)
    if (QB_TEST_ENV_SET("QB_TEST_BACKUP_COPY") || !savebatch::replace(staged, target)) {
        const Put r = copyInto(staged, target, why, cancel);
        if (r != Put::Done)
            return r;
        QFile::remove(staged);
    }
    // 0.70.0 (audit A F-11): the new backup's name reaches the disk before older backups are deleted
    savebatch::syncDir(dir);
    *finalPath = target;
    return Put::Done;
}

// The work folder says where its backups belong (for the writer who finds one waiting there).
void noteTarget(const QString &work, const QString &dir)
{
    const QString note = work + QLatin1String("/target.txt");
    const QByteArray text = QDir::toNativeSeparators(QFileInfo(dir).absoluteFilePath()).toUtf8() + '\n';
    QFile f(note);
    if (f.open(QIODevice::ReadOnly) && f.readAll() == text)
        return;
    f.close();
    safesave::writeFile(note, text);
}

// What a backup left that was cut off while it was packed (a crash, the power): removed once it is an hour old (a
// younger one may be the backup of another window into the same folder, still running).
void sweepWork(const QString &work)
{
    const QDateTime limit = QDateTime::currentDateTime().addSecs(-3600);
    const QFileInfoList parts = QDir(work).entryInfoList({QStringLiteral("*.part")}, QDir::Files);
    for (const QFileInfo &fi : parts)
        if (fi.lastModified() < limit)
            QFile::remove(fi.absoluteFilePath());
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

QString workDirectory()
{
    return dataDir() + QLatin1String("/backup-work");
}

QString workFolderFor(const QString &backupFolder)
{
    const QString place = QDir::cleanPath(QFileInfo(backupFolder).absoluteFilePath());
    return workDirectory() + QLatin1Char('/')
           + QString::fromLatin1(QCryptographicHash::hash(place.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
}

bool isOwnBackupName(const QString &fileName)
{
    const QRegularExpressionMatch m = ownForm().match(fileName);
    if (!m.hasMatch())
        return false;
    // the second backup of a second is "(2)" and the second incomplete one "(incomplete 2)": a "(1)" was made elsewhere
    for (int group : {2, 4})
        if (!m.captured(group).isEmpty() && m.captured(group).toInt() < 2)
            return false;
    return true;
}

QString projectKey(const Project *p)
{
    QString place = QFileInfo(p->displayFolder()).absoluteFilePath();
    const QString canonical = QFileInfo(place).canonicalFilePath();
    if (!canonical.isEmpty())
        place = canonical;
    return QString::fromLatin1(QCryptographicHash::hash(QDir::cleanPath(place).toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
}

QString formerProjectFolder(const Project *p, const QString &dirIn)
{
    const QString dir = dirIn.isEmpty() ? defaultDirectory() : dirIn;
    // the folder kept its name when the project's title changed: it is found by its key
    const QStringList found = QDir(dir).entryList({QStringLiteral("* ") + projectKey(p)}, QDir::Dirs | QDir::NoDotAndDotDot);
    return found.isEmpty() ? QString() : QDir(dir).filePath(found.first());
}

QStringList backupsFor(const Project *p, const QString &dirIn)
{
    const QString dir = dirIn.isEmpty() ? defaultDirectory() : dirIn;
    const QString mine = text::safeFileName(p->title());
    const QString begins = mine + QLatin1String(" bak ");
    QStringList out;
    const QStringList all = zipsIn(dir, QStringLiteral("* bak *.zip"));
    for (const QString &f : all) {
        const QString name = QFileInfo(f).fileName();
        // a name in the program's own form says exactly which title it is of; any other one (a sync program's copy)
        // is taken by how it begins
        const QString title = ownTitle(name);
        if (title.isEmpty() ? name.startsWith(begins) : title == mine)
            out << f;
    }
    return out;
}

QStringList formerBackupsFor(const Project *p, const QString &dir)
{
    const QString former = formerProjectFolder(p, dir);
    return former.isEmpty() ? QStringList() : zipsIn(former, QStringLiteral("* bak *.zip"));
}

bool backupProject(Project *p, const QString &dirIn, int keep, QString *outPath, QString *error, Where where, bool *waiting)
{
    if (waiting)
        *waiting = false;
    // (0.90.3: an empty *error after a backup means "complete": a text left in it from an earlier step is not kept)
    if (error)
        error->clear();
    // 0.91.0: every backup goes straight into the folder given (see Backup.h); the backups of this project are those
    // that carry its title, and only they are counted and cleaned up
    const QString dir = dirIn.isEmpty() ? defaultDirectory() : dirIn;
    const QString mine = text::safeFileName(p->title());
    // Backups that waited for the project's own folder of 0.70.0 to 0.90.4 (it could not be reached then) wait in
    // that folder's work folder; they are put into the backup folder with this backup.
    QStringList formerWork;
    if (where == Where::BackupFolder) {
        const QString wouldBe = QDir(dir).filePath(mine + QLatin1Char(' ') + projectKey(p));
        formerWork << workFolderFor(wouldBe);
        const QString former = formerProjectFolder(p, dir);
        if (!former.isEmpty() && QDir::cleanPath(former) != QDir::cleanPath(wouldBe))
            formerWork << workFolderFor(former);
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
    // 0.90.3: packed and checked in the program's own work folder for this backup folder; the backup folder gets
    // only the finished file (see Backup.h). Before, the backup was written as "<name>.zip.part" inside the backup
    // folder and renamed there, in view of every program that watches the folder.
    const QString work = workFolderFor(dir);
    if (!QDir().mkpath(work)) {
        if (error)
            *error = QObject::tr("Cannot create the work folder for backups %1").arg(QDir::toNativeSeparators(work));
        return false;
    }
    noteTarget(work, dir);
    sweepWork(work);
    const QString stem = text::safeFileName(p->title()) + QLatin1String(" bak ")
                         + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH-mm-ss"));
    // two backups in the same second (File > Save twice with "back up with each manual save",
    // 0.21): the second gets a number instead of failing. The name must be free in the backup folder and among
    // the backups that wait in the work folder.
    auto taken = [&dir, &work](const QString &name) {
        return QFile::exists(QDir(dir).filePath(name)) || QFile::exists(QDir(work).filePath(name));
    };
    QString name = stem + QLatin1String(".zip");
    for (int n = 2; taken(name) && n < 100; ++n)
        name = stem + QStringLiteral(" (%1).zip").arg(n);
    // written under a temporary name, checked, then renamed: a backup that was cut off
    // (a crash, a full disk) never looks like a finished one (0.20)
    const QString part = QDir(work).filePath(name) + QLatin1String(".part");
    // what the work needs from the project, taken here in the window's thread
    const QString display = p->displayFolder();
    const QString folderName = QFileInfo(display).fileName();
    const QString working = p->scrivenerPath.isEmpty() ? QString() : p->folderPath();
    const QString workingName = working.isEmpty() ? QString()
                                                  : QObject::tr("%1 - working copy of %2").arg(QFileInfo(display).completeBaseName(),
                                                                                              QLatin1String(QB_APP_NAME))
                                                        + QLatin1Char('/') + QFileInfo(working).fileName();
    struct Result {
        bool packed = false; // finished and checked in the work folder
        bool cancelled = false;
        QStringList skipped;
        QString why;            // why it could not be packed
        QString staged;         // the finished backup in the work folder
        Put put = Put::Failed;  // whether it reached the backup folder
        QString putWhy;
        QString path;           // where it is in the backup folder
    } res;
    std::atomic<qint64> done{0};
    std::atomic<bool> cancel{false};
    // 0.70.0 (audit A U-06): packing and checking run in a thread of their own; the window's thread waits with
    // the progress window of the handler (drawn, its Cancel button working, the freeze watchdog fed)
    auto work_ = [&] {
        {
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
        }
        QString why;
        if (ZipWriter::verify(part, &why) < 0) {
            QFile::remove(part);
            res.why = why;
            return;
        }
        // 0.65.0 (audit F03): a backup with files missing says so in its name, and never counts as one of the backups
        // to keep: older complete backups are deleted only after a complete one
        QString finished = name;
        if (!res.skipped.isEmpty()) {
            const QString base = QFileInfo(name).completeBaseName();
            finished = base + QLatin1String(" (incomplete).zip");
            for (int n = 2; taken(finished) && n < 100; ++n)
                finished = base + QStringLiteral(" (incomplete %1).zip").arg(n);
        }
        res.staged = QDir(work).filePath(finished);
        if (!savebatch::replace(part, res.staged)) {
            QFile::remove(part);
            res.staged.clear();
            return;
        }
        res.packed = true;
        // backups that could not be put into the backup folder before go first, oldest first (those of every
        // project: they all belong into this folder)
        bool takes = true;
        const QStringList waitingOnes = zipsIn(work, QStringLiteral("*.zip"));
        for (const QString &w : waitingOnes) {
            if (QFileInfo(w).fileName() == finished)
                continue;
            QString at, whyNot;
            if (putInPlace(w, dir, &at, &whyNot, &cancel) != Put::Done) {
                takes = false;
                break; // the folder does not take them now; the new backup says why
            }
        }
        // 0.91.0: and those that waited for this project's own folder of 0.70.0 to 0.90.4
        for (const QString &fw : std::as_const(formerWork)) {
            if (!takes)
                break;
            const QStringList formerOnes = zipsIn(fw, QStringLiteral("*.zip"));
            for (const QString &w : formerOnes) {
                QString at, whyNot;
                if (putInPlace(w, dir, &at, &whyNot, &cancel) != Put::Done) {
                    takes = false;
                    break;
                }
            }
            if (takes && QDir(fw).exists()) { // nothing waits there any more: the folder goes with its note
                QFile::remove(fw + QLatin1String("/target.txt"));
                QDir().rmdir(fw);
            }
        }
        if (cancel.load()) {
            res.cancelled = true;
            return;
        }
        res.put = putInPlace(res.staged, dir, &res.path, &res.putWhy, &cancel);
        if (res.put == Put::Cancelled)
            res.cancelled = true;
    };
    const qint64 total = sizeOf(display) + (working.isEmpty() ? 0 : sizeOf(working));
    if (progressHandler()) {
        QThread *t = QThread::create(work_);
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
        work_();
    }
    if (res.cancelled) {
        QFile::remove(part);
        if (!res.staged.isEmpty())
            QFile::remove(res.staged);
        if (error)
            *error = QObject::tr("The backup was cancelled.");
        return false;
    }
    if (!res.packed) {
        const QString shown = QDir(work).filePath(name);
        if (error)
            *error = res.why.isEmpty() ? QObject::tr("Could not write the backup %1").arg(shown)
                                       : QObject::tr("Could not write or check the backup %1: %2").arg(shown, res.why);
        return false;
    }
    if (res.put != Put::Done) {
        const QString shownDir = QDir::toNativeSeparators(dir);
        if (where == Where::AsChosen) {
            // the writer chose this folder a moment ago and can choose another: nothing is kept for it
            QFile::remove(res.staged);
            if (error)
                *error = QObject::tr("The backup could not be put into %1: %2").arg(shownDir, res.putWhy);
            return false;
        }
        // The backup is whole and stays in the work folder; the next backup into this folder puts it in place. Of
        // those of this project that wait, the newest `keep` stay (as in the backup folder).
        if (keep > 0) {
            QStringList waitingOnes = ownBackupsOf(work, mine); // oldest first
            while (waitingOnes.size() > keep) {
                const QString oldest = waitingOnes.takeFirst();
                if (QFileInfo(oldest).fileName() != QFileInfo(res.staged).fileName())
                    QFile::remove(oldest);
            }
        }
        if (waiting)
            *waiting = true;
        if (outPath)
            *outPath = res.staged;
        if (error)
            *error = QObject::tr("The backup was made, but it could not be put into the backup folder %1: %2\n\nIt is kept "
                                 "in %3, and the next backup will put it in place.")
                         .arg(shownDir, res.putWhy, QDir::toNativeSeparators(work));
        return false;
    }
    const QString path = res.path;
    if (outPath)
        *outPath = path;
    if (error && !res.skipped.isEmpty())
        *error = QObject::tr("The backup is incomplete: %n file(s) could not be read and are missing from it: %1. Older "
                             "backups were kept.", nullptr, int(res.skipped.size()))
                     .arg(res.skipped.join(QLatin1String(", ")));
    if (keep > 0 && where == Where::BackupFolder) {
        QStringList complete, incomplete; // oldest first
        // 0.90.3: only files named as this program names its backups; a copy a sync program made of a backup
        // ("... bak <date>-<computer>-safeBackup-0001.zip") counted as a backup of that moment, and six of them pushed
        // every real older backup out of a folder that keeps five
        // 0.91.0: and of those only the ones with this project's title (the folder holds every project's backups)
        const QStringList found = ownBackupsOf(dir, mine);
        for (const QString &f : found)
            (isIncomplete(f) ? incomplete : complete) << f;
        const QString made = QFileInfo(path).absoluteFilePath();
        // the backup just made is never one of those deleted, whatever its name says (0.81.0)
        auto trim = [&made](QStringList &list, int keepCount) {
            for (int i = 0; list.size() > keepCount && i < list.size();) {
                if (list.at(i) == made) {
                    ++i;
                    continue;
                }
                QFile::remove(list.takeAt(i));
            }
        };
        if (res.skipped.isEmpty()) {
            trim(complete, keep);
            // incomplete ones older than the oldest complete backup that stays are of no more use
            if (!complete.isEmpty()) {
                const QString oldest = ageKey(QFileInfo(complete.first()));
                for (const QString &f : std::as_const(incomplete))
                    if (f != made && ageKey(QFileInfo(f)) < oldest)
                        QFile::remove(f);
            }
        } else {
            // 0.81.0: older complete backups stay until a complete one is made; of the incomplete ones, the newest
            // `keep` stay (before, they were never deleted, so a project with a file that cannot be read collected
            // incomplete backups without end)
            trim(incomplete, keep);
        }
        // 0.81.0 (audit of 0.80.0, F-01): the clean-up must have left the new backup in place
        if (!QFileInfo::exists(made)) {
            if (outPath)
                outPath->clear();
            if (error)
                *error = QObject::tr("The new backup %1 is not there after older backups were cleaned up. No backup was made; "
                                     "please make one with File > Back Up > Back Up To...")
                             .arg(QDir::toNativeSeparators(made));
            return false;
        }
    }
    return true;
}

namespace {
QString dueFile() { return dataDir() + QLatin1String("/backups-due.txt"); }
QStringList dueList()
{
    QFile f(dueFile());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}
QString placeOf(const QString &projectFile)
{
    const QString canonical = QFileInfo(projectFile).canonicalFilePath();
    return canonical.isEmpty() ? QFileInfo(projectFile).absoluteFilePath() : canonical;
}
} // namespace

void markDue(const QString &projectFile)
{
    QStringList list = dueList();
    const QString place = placeOf(projectFile);
    if (list.contains(place))
        return;
    list << place;
    QDir().mkpath(QFileInfo(dueFile()).absolutePath());
    safesave::writeFile(dueFile(), list.join(QLatin1Char('\n')).toUtf8() + '\n');
}

bool takeDue(const QString &projectFile)
{
    QStringList list = dueList();
    if (!list.removeAll(placeOf(projectFile)))
        return false;
    if (list.isEmpty())
        QFile::remove(dueFile());
    else
        safesave::writeFile(dueFile(), list.join(QLatin1Char('\n')).toUtf8() + '\n');
    return true;
}

QString restore(const QString &zipFile, const QString &intoParent, QString *projectFile, QString *error)
{
    ZipReader zr(zipFile);
    if (!zr.isValid()) {
        // 0.81.0 (audit of 0.80.0, U-01): the reader knows ZIP64 now (backups of 2 GiB and more, 65,535 files and
        // more), so this is a damaged file; the reason is given, and the way that may still help
        if (error)
            *error = QObject::tr("%1 could not be read as a ZIP file (%2). If the file is whole, an archive program of "
                                 "your system may still unpack it.")
                         .arg(QDir::toNativeSeparators(zipFile), zr.errorString());
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
    // 0.81.0 (audit of 0.80.0, F-02): a restore that cannot fit is not started
    const quint64 needed = zr.totalSize();
    const QStorageInfo disk(intoParent);
    if (disk.isValid() && disk.bytesAvailable() >= 0 && needed > quint64(disk.bytesAvailable())) {
        if (error)
            *error = QObject::tr("The backup needs %1 MB, but only %2 MB are free on the drive of %3. Nothing was restored.")
                         .arg(QLocale().toString(double(needed) / 1e6, 'f', 1), QLocale().toString(double(disk.bytesAvailable()) / 1e6, 'f', 1),
                              QDir::toNativeSeparators(intoParent));
        return QString();
    }
    if (!QDir().mkpath(dest)) {
        if (error)
            *error = QObject::tr("Cannot create %1").arg(QDir::toNativeSeparators(dest));
        return QString();
    }
    // unpacked in a thread of its own, as a backup is made: the window shows the progress and Cancel
    QStringList failed;
    bool unpacked = false;
    std::atomic<qint64> done{0};
    std::atomic<bool> cancel{false};
    zr.setProgress(&done, &cancel);
    auto work = [&] { unpacked = zr.extractAll(dest, &failed); };
    if (progressHandler()) {
        QThread *t = QThread::create(work);
        t->start();
        while (!t->wait(50)) {
            bool stop = false;
            progressHandler()(QObject::tr("Restoring \"%1\"...").arg(QFileInfo(zipFile).completeBaseName()), done.load(),
                              qint64(needed), &stop);
            if (stop)
                cancel.store(true);
        }
        delete t;
        progressHandler()(QString(), qint64(needed), qint64(needed), nullptr);
    } else {
        work();
    }
    if (zr.cancelled()) {
        pathsafety::removeTreeInside(dest, intoParent);
        if (error)
            *error = QObject::tr("The restore was cancelled; the files written so far were removed.");
        return QString();
    }
    if (!unpacked) {
        // 0.81.0 (audit of 0.80.0, F-02, F-03): the files that are missing are named, with the reason
        constexpr int kNamed = 20;
        QStringList named = failed.mid(0, kNamed);
        if (failed.size() > kNamed)
            named << QObject::tr("and %n more", nullptr, int(failed.size() - kNamed));
        if (error)
            *error = QObject::tr("Not every file of the backup could be restored into %1. Not restored (%n):\n%2", nullptr,
                                 int(failed.size()))
                         .arg(QDir::toNativeSeparators(dest), named.join(QLatin1Char('\n')));
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
