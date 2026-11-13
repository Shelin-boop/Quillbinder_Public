// 0.90.3: backups are packed outside the backup folder, and the clean-up knows its own files.
//
// Reported by the writer on 2026-11-13: a backup folder inside a synced folder filled with copies of one backup
// ("<name>-<computer>-safeBackup-0001.zip" ... "-0006.zip"). The sync program made them, after its upload of the
// backup was refused; its log showed that the backup's temporary file was not the cause. Two things were Quillbinder's:
//
// - The clean-up counted every "* bak *.zip" in a project's backup folder as a backup of that project, so such copies
//   would have pushed the real older backups out (five are kept). Now only files named as the program names its
//   backups count and can be deleted (backup::isOwnBackupName); everything else in the folder is left alone.
// - The backup was written as "<name>.zip.part" inside the backup folder and renamed there. Now it is packed and
//   checked in the program's own folder (<data>/backup-work/<key of the backup folder>) and only the finished file
//   is put into the backup folder, in one step: renamed there on one drive, copied straight to the final name and
//   compared on two. A finished backup that cannot be put into the folder waits in the work folder, and the next
//   backup puts it in place.
//
//   xvfb-run -a ./qb_v0903_tests /tmp/shots
// Exit code = number of failed checks.

#include "AppInfo.h"
#include "core/Backup.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/Zip.h"
#include "ui/AppSettings.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QProcess>
#include <QRandomGenerator>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
#ifdef Q_OS_LINUX
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace qb;

static int failures = 0;
static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail.left(3000)));
    }
    std::fflush(stdout);
}
static void info(const QString &s)
{
    std::printf("      %s\n", qPrintable(s));
    std::fflush(stdout);
}

static void settle(int ms = 150)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }
}

static QByteArray readAll(const QString &f)
{
    QFile file(f);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

static void writeAll(const QString &f, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(f).absolutePath());
    QFile file(f);
    if (file.open(QIODevice::WriteOnly))
        file.write(data);
}

static BinderItem *textDoc(Project *p, BinderItem *parent, const QString &title, const QString &text)
{
    BinderItem *it = p->model()->createItem(parent, -1, ItemType::Text, title);
    QTextCursor c(p->docs()->text(it->id));
    c.insertText(text);
    return it;
}

static void waitNextSecond()
{
    const qint64 s = QDateTime::currentSecsSinceEpoch();
    while (QDateTime::currentSecsSinceEpoch() == s)
        QTest::qWait(20);
}

// a folder replaced by a plain file of the same name (the session's root ignores permissions): it cannot be made
struct Block {
    QString path, away;
    explicit Block(const QString &dir) : path(dir), away(dir + QStringLiteral(".away"))
    {
        if (QFileInfo(path).isDir())
            QDir().rename(path, away);
        else
            away.clear();
        writeAll(path, "blocked");
    }
    ~Block() { undo(); }
    void undo()
    {
        if (path.isEmpty())
            return;
        QFile::remove(path);
        if (!away.isEmpty())
            QDir().rename(away, path);
        away.clear();
        path.clear();
    }
};

static QStringList filesIn(const QString &dir, const QString &pattern = QStringLiteral("*"))
{
    return QDir(dir).entryList({pattern}, QDir::Files, QDir::Name);
}

// every file below a folder, relative
static QStringList allFilesBelow(const QString &dir)
{
    QStringList out;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext())
        out << QDir(dir).relativeFilePath(it.next());
    out.sort();
    return out;
}

// What a program that watches a folder is told (as the sync programs on Linux are): "create <name>",
// "written <name>" (closed after writing), "movedFrom <name>", "movedTo <name>", "deleted <name>", in order.
struct Watch {
    int fd = -1;
#ifdef Q_OS_WIN
    // 0.90.4 (Windows requirement W-11): what Windows tells a program that watches the folder (ReadDirectoryChangesW,
    // as the sync programs there use it). Windows has no "closed after writing": every change of a file's size or
    // time is "written", told several times for one copy (the same line in a row is kept once), and a file moved in
    // from another folder is "create" (Windows knows "moved" only for a rename inside the folder).
    HANDLE dirHandle = INVALID_HANDLE_VALUE;
    HANDLE signal = nullptr;
    OVERLAPPED ov;
    bool armed = false;
    alignas(DWORD) char changes[65536];
    void arm()
    {
        ResetEvent(signal);
        ZeroMemory(&ov, sizeof ov);
        ov.hEvent = signal;
        armed = ReadDirectoryChangesW(dirHandle, changes, sizeof changes, FALSE,
                                      FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE, nullptr, &ov, nullptr)
                != 0;
    }
#endif
    explicit Watch(const QString &dir)
    {
#ifdef Q_OS_WIN
        dirHandle = CreateFileW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(dir).utf16()), FILE_LIST_DIRECTORY,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        signal = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (dirHandle != INVALID_HANDLE_VALUE && signal) {
            arm();
            if (armed)
                fd = 1;
        }
#endif
#ifdef Q_OS_LINUX
        fd = inotify_init1(IN_NONBLOCK);
        if (fd >= 0 && inotify_add_watch(fd, QFile::encodeName(dir).constData(), IN_CREATE | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE) < 0) {
            ::close(fd);
            fd = -1;
        }
#else
        Q_UNUSED(dir);
#endif
    }
    ~Watch()
    {
#ifdef Q_OS_WIN
        if (dirHandle != INVALID_HANDLE_VALUE) {
            CancelIo(dirHandle);
            CloseHandle(dirHandle);
        }
        if (signal)
            CloseHandle(signal);
#endif
#ifdef Q_OS_LINUX
        if (fd >= 0)
            ::close(fd);
#endif
    }
    bool works() const { return fd >= 0; }
    QStringList take()
    {
        QStringList out;
#ifdef Q_OS_WIN
        // the changes arrive a moment after the call that made them: wait for each batch, until none comes for 300 ms
        while (armed && WaitForSingleObject(signal, 300) == WAIT_OBJECT_0) {
            DWORD n = 0;
            if (!GetOverlappedResult(dirHandle, &ov, &n, FALSE))
                break;
            for (DWORD i = 0; n > 0;) {
                const auto *e = reinterpret_cast<const FILE_NOTIFY_INFORMATION *>(changes + i);
                const QString name = QString::fromWCharArray(e->FileName, int(e->FileNameLength / sizeof(WCHAR)));
                QString line;
                switch (e->Action) {
                case FILE_ACTION_ADDED:
                    line = QStringLiteral("create ") + name;
                    break;
                case FILE_ACTION_REMOVED:
                    line = QStringLiteral("deleted ") + name;
                    break;
                case FILE_ACTION_MODIFIED:
                    line = QStringLiteral("written ") + name;
                    break;
                case FILE_ACTION_RENAMED_OLD_NAME:
                    line = QStringLiteral("movedFrom ") + name;
                    break;
                case FILE_ACTION_RENAMED_NEW_NAME:
                    line = QStringLiteral("movedTo ") + name;
                    break;
                default:
                    break;
                }
                if (!line.isEmpty() && (out.isEmpty() || out.last() != line))
                    out << line;
                if (!e->NextEntryOffset)
                    break;
                i += e->NextEntryOffset;
            }
            arm();
        }
#endif
#ifdef Q_OS_LINUX
        alignas(inotify_event) char buf[32768];
        for (;;) {
            const ssize_t n = ::read(fd, buf, sizeof buf);
            if (n <= 0)
                break;
            for (ssize_t i = 0; i < n;) {
                const auto *e = reinterpret_cast<const inotify_event *>(buf + i);
                const QString name = e->len ? QFile::decodeName(e->name) : QString();
                if (e->mask & IN_CREATE)
                    out << QStringLiteral("create ") + name;
                if (e->mask & IN_CLOSE_WRITE)
                    out << QStringLiteral("written ") + name;
                if (e->mask & IN_MOVED_FROM)
                    out << QStringLiteral("movedFrom ") + name;
                if (e->mask & IN_MOVED_TO)
                    out << QStringLiteral("movedTo ") + name;
                if (e->mask & IN_DELETE)
                    out << QStringLiteral("deleted ") + name;
                i += ssize_t(sizeof(inotify_event)) + e->len;
            }
        }
#endif
        return out;
    }
};

// Did the watcher see one finished file arrive by a move, and after it only what is listed in "then"? Linux:
// "movedTo <name>". Windows (0.90.4) tells a file moved in from another folder as created, perhaps with a change of
// its time after it; what counts is the same: one name, the final one, no temporary file and no rename.
static bool arrivedOnce(const QStringList &seen, const QString &name, const QStringList &then = QStringList())
{
#ifdef Q_OS_WIN
    if (seen.isEmpty() || seen.first() != QStringLiteral("create ") + name)
        return false;
    int i = 1;
    while (i < seen.size() && seen.at(i) == QStringLiteral("written ") + name)
        ++i;
    return seen.mid(i) == then;
#else
    return seen == QStringList{QStringLiteral("movedTo ") + name} + then;
#endif
}

static bool onTwoDrives(const QString &a, const QString &b)
{
#ifdef Q_OS_LINUX
    struct stat sa, sb;
    if (::stat(QFile::encodeName(a).constData(), &sa) != 0 || ::stat(QFile::encodeName(b).constData(), &sb) != 0)
        return false;
    return sa.st_dev != sb.st_dev;
#else
    Q_UNUSED(a);
    Q_UNUSED(b);
    return false;
#endif
}

// a small whole ZIP file that is not a backup of the test's project
static void writeOtherZip(const QString &zip)
{
    QDir().mkpath(QFileInfo(zip).absolutePath());
    ZipWriter zw(zip);
    zw.addFile(QStringLiteral("other/a.txt"), QByteArray(900, 'x'), false);
    zw.close();
}

// ------------------------------------------------------------------ child run under the fault library

// opens the project and backs it up into the folder; prints the result
static int childRun(const QString &folder, const QString &dir)
{
    QString err;
    auto p = Project::open(folder, &err);
    if (!p) {
        std::printf("result=open-failed err=%s\n", qPrintable(err));
        return 3;
    }
    QString out;
    bool waits = false;
    const bool ok = backup::backupProject(p.get(), dir, 5, &out, &err, backup::Where::BackupFolder, &waits);
    std::printf("result=%d waiting=%d out=%s err=%s\n", ok ? 1 : 0, waits ? 1 : 0, qPrintable(out), qPrintable(err));
    std::fflush(stdout);
    return ok ? 0 : 1;
}

// ------------------------------------------------------------------ the checks

static void nameChecks()
{
    const QString t = QStringLiteral("Harbor Lights - Part 3 bak 2026-05-04 10-20-30");
    QStringList wrong;
    for (const QString &n : {t + QStringLiteral(".zip"), t + QStringLiteral(" (2).zip"), t + QStringLiteral(" (14).zip"),
                             t + QStringLiteral(" (incomplete).zip"), t + QStringLiteral(" (incomplete 2).zip"),
                             t + QStringLiteral(" (3) (incomplete).zip"),
                             QStringLiteral("A bak 2020-01-01 00-00-00 again bak 2026-05-04 10-20-30.zip")})
        if (!backup::isOwnBackupName(n))
            wrong << n;
    check(wrong.isEmpty(), QStringLiteral("names the program gives its backups are known as its own (plain, a second one of the second, "
                                          "incomplete ones, a title that holds \" bak <date>\" itself)"),
          wrong.join(QLatin1String(" | ")));
    wrong.clear();
    for (const QString &n : {t + QStringLiteral("-LittleBox-safeBackup-0001.zip"), t + QStringLiteral("-LittleBox-safeBackup-0006.zip"),
                             t + QStringLiteral(" (conflicted copy 2026-05-04).zip"), t + QStringLiteral("-DESKTOP-AB12CD.zip"),
                             t + QStringLiteral(" (1).zip"), t + QStringLiteral(" - Copy.zip"), t + QStringLiteral(" (Kopie).zip"),
                             t + QStringLiteral(".sync-conflict-20260504-102030-ABCDEFG.zip"), t + QStringLiteral(".zip.part"),
                             t + QStringLiteral(" (incomplete 1).zip"), QStringLiteral("Harbor Lights - Part 3 bak 2026-05-04.zip"),
                             QStringLiteral("Harbor Lights - Part 3.zip")})
        if (backup::isOwnBackupName(n))
            wrong << n;
    check(wrong.isEmpty(), QStringLiteral("copies made by sync programs or by hand, and other files, are not taken for the program's own "
                                          "backups (\"-<computer>-safeBackup-0001\", \"(conflicted copy ...)\", \"(1)\", \"- Copy\", \".part\")"),
          wrong.join(QLatin1String(" | ")));
}

// the reported case: copies of a backup in the folder must never push real backups out
static void foreignCopyChecks(const QString &H)
{
    QString err;
    auto p = Project::create(H + QStringLiteral("/projects"), QStringLiteral("Copies"), &err);
    textDoc(p.get(), p->draft(), QStringLiteral("Scene"), QStringLiteral("The text of the scene."));
    p->save(&err);
    const QString dir = H + QStringLiteral("/bk-copies");
    QStringList real;
    bool ok = true;
    for (int i = 0; i < 3; ++i) {
        QString out, why;
        ok = backup::backupProject(p.get(), dir, 5, &out, &why) && why.isEmpty() && ok;
        real << out;
        waitNextSecond();
    }
    const QString pf = dir; // 0.91.0: the backups are straight in the backup folder
    // what the writer found: six copies of the newest backup, named by the sync program; and copies of other kinds
    const QString newest = real.last();
    const QString base = QFileInfo(newest).completeBaseName();
    QStringList foreign;
    for (int i = 1; i <= 6; ++i)
        foreign << pf + QLatin1Char('/') + base + QStringLiteral("-LittleBox-safeBackup-%1.zip").arg(i, 4, 10, QLatin1Char('0'));
    foreign << pf + QLatin1Char('/') + base + QStringLiteral(" (conflicted copy 2026-05-04).zip") << pf + QLatin1Char('/') + base + QStringLiteral(" (1).zip")
            << pf + QLatin1Char('/') + base + QStringLiteral(" - Copy.zip");
    for (const QString &f : std::as_const(foreign))
        QFile::copy(newest, f);
    check(ok && real.size() == 3 && filesIn(pf, QStringLiteral("*.zip")).size() == 12,
          QStringLiteral("(three backups and nine copies of the newest one are in the project's backup folder)"),
          filesIn(pf).join(QLatin1String(" | ")));
    // the next backup, five kept: four real backups are fewer than five, so nothing may go
    QString out4, why4;
    ok = backup::backupProject(p.get(), dir, 5, &out4, &why4);
    real << out4;
    QStringList gone;
    for (const QString &f : std::as_const(real))
        if (!QFile::exists(f))
            gone << QFileInfo(f).fileName();
    check(ok && why4.isEmpty() && gone.isEmpty(),
          QStringLiteral("with nine copies of a backup in the folder, the next backup deletes no real backup (four of five kept; "
                         "before, the copies counted as backups and the older real ones were deleted for them)"),
          QStringLiteral("gone: ") + gone.join(QLatin1String(" | ")));
    // two more: six real backups, five kept: only the oldest real one goes; no copy is ever deleted
    for (int i = 0; i < 2; ++i) {
        waitNextSecond();
        QString o, w;
        ok = backup::backupProject(p.get(), dir, 5, &o, &w) && w.isEmpty() && ok;
        real << o;
    }
    QStringList left, copiesGone;
    for (const QString &f : std::as_const(real))
        if (QFile::exists(f))
            left << QFileInfo(f).fileName();
    for (const QString &f : std::as_const(foreign))
        if (!QFile::exists(f))
            copiesGone << QFileInfo(f).fileName();
    check(ok && real.size() == 6 && left.size() == 5 && !QFile::exists(real.first()),
          QStringLiteral("... and with six real backups the oldest real one goes and the five newest stay"), left.join(QLatin1String(" | ")));
    check(copiesGone.isEmpty(), QStringLiteral("... the clean-up deletes none of the copies (they are not its files)"),
          copiesGone.join(QLatin1String(" | ")));
    const QStringList listed = backup::backupsFor(p.get(), dir);
    check(listed.contains(foreign.first()) && listed.contains(real.last()),
          QStringLiteral("... Restore From Backup still lists the copies beside the backups (a copy can be the one that is whole)"),
          QString::number(listed.size()));
}

// what the backup folder sees while a backup is made
static void placeChecks(const QString &H)
{
    QString err;
    auto p = Project::create(H + QStringLiteral("/projects"), QStringLiteral("Watched"), &err);
    textDoc(p.get(), p->draft(), QStringLiteral("Scene"), QStringLiteral("The text of the scene."));
    writeAll(p->folderPath() + QStringLiteral("/Files/picture.bin"), QByteArray(300000, 'p'));
    p->save(&err);
    const QString dir = H + QStringLiteral("/bk-watched");
    const QString pf = dir; // 0.91.0: the backups are straight in the backup folder
    const QString work = backup::workFolderFor(pf);
    QString out, why;
    bool ok = backup::backupProject(p.get(), dir, 0, &out, &why);
    check(ok && why.isEmpty() && QFileInfo(out).absolutePath() == QFileInfo(pf).absoluteFilePath() && ZipWriter::verify(out) > 0,
          QStringLiteral("a backup is made, checked, and is in the project's backup folder"), why + QLatin1Char(' ') + out);
    check(work.startsWith(backup::workDirectory() + QLatin1Char('/')) && !work.startsWith(QFileInfo(dir).absoluteFilePath())
              && QString::fromUtf8(readAll(work + QStringLiteral("/target.txt"))).trimmed() == QDir::toNativeSeparators(QFileInfo(pf).absoluteFilePath()),
          QStringLiteral("it was packed in the program's own work folder, outside the backup folder; the work folder names the "
                         "backup folder it serves (target.txt)"),
          work + QLatin1String(" | ") + QString::fromUtf8(readAll(work + QStringLiteral("/target.txt"))));
    check(filesIn(work) == QStringList{QStringLiteral("target.txt")}, QStringLiteral("... and nothing of the backup is left in the work folder"),
          filesIn(work).join(QLatin1String(" | ")));

    // one drive: the backup folder is told of one finished file, moved in
    {
        waitNextSecond();
        Watch watch(pf);
        ok = backup::backupProject(p.get(), dir, 0, &out, &why);
        const QStringList seen = watch.take();
        if (watch.works())
            check(ok && arrivedOnce(seen, QFileInfo(out).fileName()),
                  QStringLiteral("a program that watches the backup folder sees one thing: the finished backup arriving under its final "
                                 "name (before: a \".zip.part\" file created, written and renamed there)"),
                  seen.join(QLatin1String(" | ")));
        else
            info(QStringLiteral("(no folder watching on this system: the events of a backup are not checked)"));
    }
    // two drives (the copy): one file, created under its final name, written once
    {
        waitNextSecond();
        qputenv("QB_TEST_BACKUP_COPY", "1");
        Watch watch(pf);
        ok = backup::backupProject(p.get(), dir, 0, &out, &why);
        const QStringList seen = watch.take();
        qunsetenv("QB_TEST_BACKUP_COPY");
        const QString n = QFileInfo(out).fileName();
        if (watch.works())
            check(ok && seen == QStringList{QStringLiteral("create ") + n, QStringLiteral("written ") + n},
                  QStringLiteral("when the backup folder is on another drive, the watcher sees the backup created under its final name and "
                                 "closed once: no temporary name, no rename"),
                  seen.join(QLatin1String(" | ")));
        check(ok && why.isEmpty() && ZipWriter::verify(out) > 0 && filesIn(work) == QStringList{QStringLiteral("target.txt")},
              QStringLiteral("... the copy is whole, and the work folder is empty again"), why + filesIn(work).join(QLatin1String(" | ")));
    }
    // really two drives, where the system has a second one
    {
        const QString shm = QStringLiteral("/dev/shm");
        QDir().mkpath(backup::workDirectory());
        if (QFileInfo(shm).isDir() && QFileInfo(shm).isWritable() && onTwoDrives(shm, backup::workDirectory())) {
            QTemporaryDir other(shm + QStringLiteral("/qb-v0903-XXXXXX"));
            waitNextSecond();
            QString o, w;
            ok = backup::backupProject(p.get(), other.path(), 0, &o, &w);
            check(ok && w.isEmpty() && o.startsWith(other.path()) && ZipWriter::verify(o) > 0
                      && filesIn(QFileInfo(o).absolutePath(), QStringLiteral("*.zip")).size() == 1
                      && filesIn(backup::workFolderFor(QFileInfo(o).absolutePath())) == QStringList{QStringLiteral("target.txt")},
                  QStringLiteral("a backup folder on a real second drive (the system's memory drive) gets the whole backup"), w + QLatin1Char(' ') + o);
        } else {
            info(QStringLiteral("(no second drive here: the real two-drive run is left out; the copy itself is checked above)"));
        }
    }
    check(filesIn(pf, QStringLiteral("*.part")).isEmpty() && filesIn(pf, QStringLiteral("*.zip")).size() == 3,
          QStringLiteral("the backup folder holds the three backups and no \".part\" file"), filesIn(pf).join(QLatin1String(" | ")));

    // the clean-up: the watcher sees the new backup and the deletion of the oldest, nothing else
    {
        waitNextSecond();
        const QStringList before = backup::backupsFor(p.get(), dir);
        Watch watch(pf);
        ok = backup::backupProject(p.get(), dir, 3, &out, &why);
        const QStringList seen = watch.take();
        if (watch.works())
            check(ok && arrivedOnce(seen, QFileInfo(out).fileName(), {QStringLiteral("deleted ") + QFileInfo(before.value(0)).fileName()}),
                  QStringLiteral("with three kept, a fourth backup arrives and the oldest is deleted; the watcher sees just that"),
                  seen.join(QLatin1String(" | ")));
    }

    // a file that cannot be read: the name says "incomplete" already when the backup arrives
    {
        waitNextSecond();
        writeAll(p->folderPath() + QStringLiteral("/Files/secret.bin"), "cannot be read");
        qputenv("QB_TEST_UNREADABLE", "/secret.bin");
        Watch watch(pf);
        QString o, w;
        ok = backup::backupProject(p.get(), dir, 0, &o, &w);
        const QStringList seen = watch.take();
        qunsetenv("QB_TEST_UNREADABLE");
        QFile::remove(p->folderPath() + QStringLiteral("/Files/secret.bin"));
        check(ok && w.contains(QLatin1String("incomplete")) && QFileInfo(o).fileName().endsWith(QLatin1String(" (incomplete).zip"))
                  && backup::isOwnBackupName(QFileInfo(o).fileName()) && QFile::exists(o)
                  && (!watch.works() || arrivedOnce(seen, QFileInfo(o).fileName())),
              QStringLiteral("an incomplete backup arrives under its \"(incomplete)\" name in one step (before: put in place, then renamed)"),
              w.left(200) + QLatin1String(" | ") + seen.join(QLatin1String(" | ")));
    }

    // Cancel: nothing is left, neither in the backup folder nor in the work folder
    {
        QByteArray noise(24 * 1024 * 1024, '\0');
        QRandomGenerator rng(3);
        for (int i = 0; i < noise.size(); i += 4) {
            const quint32 v = rng.generate();
            memcpy(noise.data() + i, &v, 4);
        }
        writeAll(p->folderPath() + QStringLiteral("/Files/noise.bin"), noise);
        const QStringList before = filesIn(pf);
        int calls = 0;
        backup::setProgressHandler([&calls](const QString &what, qint64, qint64, bool *stop) {
            if (!what.isEmpty()) {
                ++calls;
                if (calls == 1 && stop)
                    *stop = true;
            }
        });
        QString o, w;
        ok = backup::backupProject(p.get(), dir, 0, &o, &w);
        backup::setProgressHandler(nullptr);
        check(calls >= 1 && !ok && w.contains(QLatin1String("cancelled")) && filesIn(pf) == before
                  && filesIn(work) == QStringList{QStringLiteral("target.txt")},
              QStringLiteral("a cancelled backup leaves nothing behind, in the backup folder or in the work folder"),
              w + QLatin1String(" | ") + filesIn(work).join(QLatin1String(" | ")));
        // ... and a backup with the progress window (packing and putting in place run beside the window's thread)
        calls = 0;
        backup::setProgressHandler([&calls](const QString &what, qint64, qint64, bool *) {
            if (!what.isEmpty())
                ++calls;
        });
        waitNextSecond();
        qputenv("QB_TEST_BACKUP_COPY", "1");
        ok = backup::backupProject(p.get(), dir, 0, &o, &w);
        qunsetenv("QB_TEST_BACKUP_COPY");
        backup::setProgressHandler(nullptr);
        check(ok && w.isEmpty() && calls >= 1 && ZipWriter::verify(o) > 0 && QFileInfo(o).size() > 20 * 1024 * 1024,
              QStringLiteral("a 24 MB backup with the progress window is packed, copied into the folder and whole"),
              w + QStringLiteral(" calls %1 size %2").arg(calls).arg(QFileInfo(o).size()));
        QFile::remove(p->folderPath() + QStringLiteral("/Files/noise.bin"));
    }

    // what a backup cut off while it was packed left in the work folder goes once it is an hour old
    {
        const QString oldPart = work + QStringLiteral("/Old bak 2026-01-01 10-00-00.zip.part");
        const QString youngPart = work + QStringLiteral("/Young bak 2026-01-01 10-00-00.zip.part");
        writeAll(oldPart, "cut off");
        writeAll(youngPart, "still being written by another window");
        {
            QFile f(oldPart);
            f.open(QIODevice::ReadWrite);
            f.setFileTime(QDateTime::currentDateTime().addSecs(-2 * 3600), QFileDevice::FileModificationTime);
        }
        waitNextSecond();
        QString o, w;
        ok = backup::backupProject(p.get(), dir, 0, &o, &w);
        check(ok && !QFile::exists(oldPart) && QFile::exists(youngPart),
              QStringLiteral("an unfinished file of a backup that was cut off two hours ago is removed from the work folder; a young "
                             "one (another window may be packing) stays"),
              filesIn(work).join(QLatin1String(" | ")));
        QFile::remove(youngPart);
    }
}

// a backup folder that cannot be reached: the backup waits and is put in place later
static void waitingChecks(const QString &H)
{
    QString err;
    auto p = Project::create(H + QStringLiteral("/projects"), QStringLiteral("Waiting"), &err);
    textDoc(p.get(), p->draft(), QStringLiteral("Scene"), QStringLiteral("The text of the scene."));
    p->save(&err);
    const QString dir = H + QStringLiteral("/bk-waiting");
    const QString pf = dir; // 0.91.0: the backups are straight in the backup folder
    const QString work = backup::workFolderFor(pf);
    QStringList kept;
    {
        Block block(pf);
        QString out, why;
        bool waits = false;
        bool ok = backup::backupProject(p.get(), dir, 5, &out, &why, backup::Where::BackupFolder, &waits);
        kept << out;
        check(!ok && waits && QFileInfo(out).absolutePath() == work && ZipWriter::verify(out) > 0 && backup::isOwnBackupName(QFileInfo(out).fileName()),
              QStringLiteral("a backup folder that cannot be made: the backup is packed and checked all the same and waits, whole, in "
                             "the work folder"),
              why + QLatin1String(" | ") + out);
        check(why.contains(QLatin1String("was made")) && why.contains(QDir::toNativeSeparators(pf)) && why.contains(QDir::toNativeSeparators(work))
                  && why.contains(QLatin1String("next backup")),
              QStringLiteral("... and the message says that it was made, which folder did not take it, where it is kept, and that the "
                             "next backup puts it in place"),
              why);
        waitNextSecond();
        ok = backup::backupProject(p.get(), dir, 5, &out, &why, backup::Where::BackupFolder, &waits);
        kept << out;
        check(!ok && waits && filesIn(work, QStringLiteral("*.zip")).size() == 2,
              QStringLiteral("a second backup while the folder is still out of reach waits beside the first"),
              filesIn(work).join(QLatin1String(" | ")));
    }
    {
        waitNextSecond();
        QString out, why;
        bool waits = true;
        const bool ok = backup::backupProject(p.get(), dir, 5, &out, &why, backup::Where::BackupFolder, &waits);
        const QStringList there = backup::backupsFor(p.get(), dir);
        check(ok && !waits && why.isEmpty() && there.size() == 3 && QFileInfo(there.value(0)).fileName() == QFileInfo(kept.value(0)).fileName()
                  && QFileInfo(there.value(1)).fileName() == QFileInfo(kept.value(1)).fileName() && there.value(2) == out
                  && ZipWriter::verify(there.value(0)) > 0 && ZipWriter::verify(there.value(1)) > 0,
              QStringLiteral("once the folder can be made again, the next backup puts the two waiting backups in place, then itself; "
                             "all three are whole and in the order they were made"),
              why + QLatin1String(" | ") + there.join(QLatin1String(" | ")));
        check(filesIn(work) == QStringList{QStringLiteral("target.txt")}, QStringLiteral("... and the work folder is empty again"),
              filesIn(work).join(QLatin1String(" | ")));
    }
    // of those that wait, the newest ones up to the number to keep stay
    {
        Block block(pf);
        QStringList made;
        for (int i = 0; i < 3; ++i) {
            waitNextSecond();
            QString out, why;
            backup::backupProject(p.get(), dir, 2, &out, &why);
            made << QFileInfo(out).fileName();
        }
        check(filesIn(work, QStringLiteral("*.zip")) == QStringList{made.value(1), made.value(2)},
              QStringLiteral("three backups with two kept while the folder is out of reach: the two newest wait"),
              filesIn(work).join(QLatin1String(" | ")));
        for (const QString &f : filesIn(work, QStringLiteral("*.zip")))
            QFile::remove(work + QLatin1Char('/') + f);
    }
    // Back Up To... into a folder that cannot be made: told, nothing kept
    {
        const QString chosen = H + QStringLiteral("/bk-chosen");
        Block block(chosen);
        QString out, why;
        bool waits = true;
        const bool ok = backup::backupProject(p.get(), chosen, 0, &out, &why, backup::Where::AsChosen, &waits);
        check(!ok && !waits && why.contains(QLatin1String("could not be put into")) && why.contains(QDir::toNativeSeparators(chosen))
                  && filesIn(backup::workFolderFor(chosen), QStringLiteral("*.zip")).isEmpty(),
              QStringLiteral("Back Up To... into a folder that cannot be made says so with the reason and keeps nothing (the writer "
                             "chooses another folder)"),
              why);
    }
    // a waiting backup and a cut-off copy of it in the folder (the program ended while it copied): the copy is replaced
    {
        QString staged, why;
        {
            Block block(pf);
            waitNextSecond();
            backup::backupProject(p.get(), dir, 0, &staged, &why);
        }
        const QByteArray whole = readAll(staged);
        const QString inFolder = pf + QLatin1Char('/') + QFileInfo(staged).fileName();
        writeAll(inFolder, whole.left(200));
        waitNextSecond();
        QString out;
        const bool ok = backup::backupProject(p.get(), dir, 0, &out, &why);
        check(ok && why.isEmpty() && !whole.isEmpty() && readAll(inFolder) == whole && !QFile::exists(staged),
              QStringLiteral("a cut-off copy of a waiting backup in the backup folder is replaced by the whole backup (it never stays "
                             "there looking like a backup)"),
              why + QStringLiteral(" %1 of %2 bytes").arg(QFileInfo(inFolder).size()).arg(whole.size()));
    }
    // ... the same backup already in the folder, whole (the program ended before the waiting file was removed): no second one
    {
        QString staged, why;
        {
            Block block(pf);
            waitNextSecond();
            backup::backupProject(p.get(), dir, 0, &staged, &why);
        }
        const QString inFolder = pf + QLatin1Char('/') + QFileInfo(staged).fileName();
        QFile::copy(staged, inFolder);
        const int before = int(filesIn(pf, QStringLiteral("*.zip")).size());
        waitNextSecond();
        QString out;
        const bool ok = backup::backupProject(p.get(), dir, 0, &out, &why);
        check(ok && !QFile::exists(staged) && int(filesIn(pf, QStringLiteral("*.zip")).size()) == before + 1,
              QStringLiteral("a waiting backup that is already in the folder, whole, is not put there a second time"),
              filesIn(pf).join(QLatin1String(" | ")));
    }
    // ... another whole backup of the same name in the folder: it stays, the waiting one gets the next number
    {
        QString staged, why;
        {
            Block block(pf);
            waitNextSecond();
            backup::backupProject(p.get(), dir, 0, &staged, &why);
        }
        const QString name = QFileInfo(staged).fileName();
        const QString inFolder = pf + QLatin1Char('/') + name;
        writeOtherZip(inFolder);
        const QByteArray other = readAll(inFolder);
        const QByteArray ours = readAll(staged);
        waitNextSecond();
        QString out;
        const bool ok = backup::backupProject(p.get(), dir, 0, &out, &why);
        const QString second = pf + QLatin1Char('/') + QFileInfo(name).completeBaseName() + QStringLiteral(" (2).zip");
        check(ok && readAll(inFolder) == other && !ours.isEmpty() && readAll(second) == ours && backup::isOwnBackupName(QFileInfo(second).fileName()),
              QStringLiteral("another whole backup of the same name in the folder is never written over: the waiting one arrives as \"(2)\""),
              filesIn(pf).join(QLatin1String(" | ")));
    }
}

#ifdef QB_FAULT_SHIM
// a full drive, with the fault library: under the backup folder (the copy fails) and under the work folder (packing fails)
static void fullDriveChecks(const QString &H)
{
    QString err;
    QString folder;
    {
        auto p = Project::create(H + QStringLiteral("/projects"), QStringLiteral("Full"), &err);
        textDoc(p.get(), p->draft(), QStringLiteral("Scene"), QStringLiteral("The text of the scene."));
        p->save(&err);
        folder = p->folderPath();
    }
    auto child = [&](const QString &shimDir, bool copy, const QString &dir, QString *out) {
        QProcess pr;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        env.insert(QStringLiteral("LD_PRELOAD"), QStringLiteral(QB_FAULT_SHIM));
        env.insert(QStringLiteral("QB_SHIM_DIR"), shimDir);
        env.insert(QStringLiteral("QB_SHIM_MODE"), QStringLiteral("write"));
        if (copy)
            env.insert(QStringLiteral("QB_TEST_BACKUP_COPY"), QStringLiteral("1"));
        pr.setProcessEnvironment(env);
        pr.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child"), folder, dir});
        pr.waitForFinished(120000);
        *out = QString::fromUtf8(pr.readAllStandardOutput() + pr.readAllStandardError()).trimmed();
        return pr.exitStatus() == QProcess::NormalExit ? pr.exitCode() : -1;
    };
    // the drive of the backup folder is full: nothing of the backup stays there, the whole backup waits
    {
        const QString dir = H + QStringLiteral("/bk-full");
        QDir().mkpath(dir);
        QString out;
        const int rc = child(dir, true, dir, &out);
        QStringList zips, waitingOnes;
        for (const QString &f : allFilesBelow(dir))
            zips << f;
        QDirIterator it(backup::workDirectory(), {QStringLiteral("Full bak *.zip")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            waitingOnes << it.next();
        check(rc == 1 && out.contains(QLatin1String("result=0 waiting=1")) && zips.isEmpty() && waitingOnes.size() == 1
                  && ZipWriter::verify(waitingOnes.value(0)) > 0,
              QStringLiteral("the backup folder's drive is full while the backup is copied there: no file of it stays in the folder, "
                             "and the whole backup waits in the work folder"),
              QStringLiteral("rc %1: %2 | in the folder: %3 | waiting: %4").arg(rc).arg(out.right(500), zips.join(QLatin1Char(',')), waitingOnes.join(QLatin1Char(','))));
        // the drive has room again: the next backup puts the waiting one in place
        waitNextSecond();
        auto p = Project::open(folder, &err);
        QString o, w;
        const bool ok = p && backup::backupProject(p.get(), dir, 5, &o, &w);
        check(ok && w.isEmpty() && backup::backupsFor(p.get(), dir).size() == 2,
              QStringLiteral("... with room again, the next backup puts the waiting one in place too"), w);
    }
    // the drive of the work folder is full: no backup, nothing that looks like one anywhere
    {
        const QString dir = H + QStringLiteral("/bk-full2");
        QString out;
        const int rc = child(backup::workDirectory(), false, dir, &out);
        QStringList left;
        QDirIterator it(backup::workDirectory(), {QStringLiteral("Full bak *")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            left << it.next();
        check(rc == 1 && out.contains(QLatin1String("result=0 waiting=0")) && allFilesBelow(dir).isEmpty() && left.isEmpty(),
              QStringLiteral("the work folder's drive is full: the backup fails, says so, and leaves no file, finished or not"),
              QStringLiteral("rc %1: %2 | %3").arg(rc).arg(out.right(500), left.join(QLatin1Char(','))));
    }
}
#endif

// the window: a backup that waits is told with its own words
static void windowChecks(const QString &H)
{
    QString err;
    QString folder;
    {
        auto p = Project::create(H + QStringLiteral("/projects"), QStringLiteral("Window"), &err);
        textDoc(p.get(), p->draft(), QStringLiteral("Scene"), QStringLiteral("The text of the scene."));
        p->save(&err);
        folder = p->folderPath();
    }
    AppSettings &as = AppSettings::instance();
    const QString dir = H + QStringLiteral("/bk-window");
    as.backupDir = dir;
    as.backupKeep = 5;
    as.backupOnOpen = true;
    as.backupOnClose = false;
    as.backupOnManualSave = false;
    as.save();
    MainWindow w;
    w.show();
    settle(200);
    QString pf;
    {
        auto p = Project::open(folder, &err);
        pf = p ? dir : QString(); // 0.91.0: the backup folder itself
    }
    {
        Block block(pf);
        w.openProject(folder);
        settle(500);
        check(w.project() && w.lastBackupProblem().contains(QLatin1String("was made")) && w.lastBackupProblem().contains(QLatin1String("next backup")),
              QStringLiteral("the window: an automatic backup that could not be put into its folder is told as made and waiting, with "
                             "the place where it is kept"),
              w.lastBackupProblem());
        // a manual save with "back up with each manual save": the status bar says the same, not "could not be made"
        as.backupOnOpen = false;
        as.backupOnManualSave = true;
        as.save();
        waitNextSecond();
        w.manualSave();
        settle(300);
        const QString bar = w.statusBar()->currentMessage();
        check(bar.contains(QLatin1String("was made")) && !bar.contains(QLatin1String("could not be made")) && !bar.contains(QLatin1Char('\n')),
              QStringLiteral("... and after File > Save with a backup per save, the status bar says so in one line"), bar);
    }
    waitNextSecond();
    w.manualSave();
    settle(300);
    {
        auto *p = w.project();
        const QStringList there = p ? backup::backupsFor(p, dir) : QStringList();
        check(there.size() == 3, QStringLiteral("... the next save's backup puts the two waiting ones and itself into the folder"),
              there.join(QLatin1String(" | ")));
    }
    as.backupOnManualSave = false;
    as.save();
    w.closeProject();
    settle(200);
}

int main(int argc, char **argv)
{
    if (argc > 3 && qstrcmp(argv[1], "--child") == 0) {
        QApplication app(argc, argv);
        return childRun(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    }
    QTemporaryDir home;
    const QString H = home.path();
    qputenv("HOME", H.toUtf8());
    qputenv("XDG_CONFIG_HOME", (H + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (H + QLatin1String("/data")).toUtf8());
    qputenv("XDG_RUNTIME_DIR", (H + QLatin1String("/run")).toUtf8());
    QDir().mkpath(H + QLatin1String("/run"));
    QFile::setPermissions(H + QLatin1String("/run"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("QB_NO_DIALOGS", "1");
    qputenv("QB_SKIP_FIRST_START", "1");
    QCoreApplication::setOrganizationName(QStringLiteral("Quillbinder"));
    QCoreApplication::setApplicationName(QStringLiteral("Quillbinder"));
    QApplication app(argc, argv);
    const QString shots = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(shots);
    const QString src = QStringLiteral(QB_TEST_SOURCE_DIR) + QStringLiteral("/..");
    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.90.3")) >= 0,
          QStringLiteral("version 0.90.3 or newer"), QStringLiteral(QB_APP_VERSION_SHOWN));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    AppSettings::instance().load();
    QDir().mkpath(H + QStringLiteral("/projects"));

    nameChecks();
    foreignCopyChecks(H);
    placeChecks(H);
    waitingChecks(H);
#ifdef QB_FAULT_SHIM
    fullDriveChecks(H);
#endif
    windowChecks(H);

    // the documents
    const QString manual = QString::fromUtf8(readAll(src + QStringLiteral("/docs/manual.md")));
    check(manual.contains(QLatin1String("backup-work")) && manual.contains(QLatin1String("safeBackup")),
          QStringLiteral("the manual says where a backup is put together, what happens when the backup folder is out of reach, and that "
                         "copies made by sync programs are left alone"));
    check(QString::fromUtf8(readAll(src + QStringLiteral("/docs/CHANGES.md"))).contains(QLatin1String("## 0.90.3")),
          QStringLiteral("docs/CHANGES.md has the entry of 0.90.3"));

    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
