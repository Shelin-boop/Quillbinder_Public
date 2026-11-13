// Scripted run for version 0.81.0 beta, the fixes of the audit of 0.80.0 (2026-11-11, part 1: usability,
// performance, trustworthiness, stability, data protection, data security, robustness):
//  - backups: the clean-up after the title changed (F-01), a restore onto a full disk (F-02, a child run with
//    tests/faultshim.cpp), entries above 1 GiB and archives of 2 GiB and more (F-03, U-01: a sparse 2.3 GB file, about
//    a minute; QB_SKIP_BIG_BACKUP=1 leaves it out), 70,000 entries (the ZIP64 count), a restore that does not fit,
//    folders that are links (F-05), incomplete backups kept to the number, the backup missed by a session ended by a
//    signal (S-03);
//  - snapshots: a damaged list of titles (F-04);
//  - saving: saves nobody looked at (F-06), the kept copy of a conflict (F-07), Empty Trash (F-08), three spare copies
//    (U-02), the storage conversion one document at a time and the comparison's limit (U-04), the ui file named by the
//    computer's key (S-06), opening on a full disk (S-12);
//  - the window: readable explanations (S-01), window titles (S-07), the failed-save notice (S-08), First Start (S-09);
//  - release: the release waits for the tests and the code check (S-02, S-11; tools/code-check.sh runs when cppcheck
//    is installed).
// Saves screenshots into the folder given as first argument. Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_v0810_tests /tmp/shots
#include "core/Backup.h"
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/Snapshots.h"
#include "core/SyncSafety.h"
#include "core/Zip.h"
#include "ui/AppSettings.h"
#include "ui/EditorPane.h"
#include "ui/FirstStartDialog.h"
#include "ui/HintLabel.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/StartDialog.h"

#include "AppInfo.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QRadioButton>
#include <QSignalSpy>
#include <QStatusBar>
#include <QStyleFactory>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cstdio>
#include <cstdlib>
#include <functional>

using namespace qb;

static int failures = 0;
static QString outDir;
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

static bool waitFor(const std::function<bool()> &f, int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        if (f())
            return true;
        settle(50);
    }
    return f();
}

static void shot(QWidget *w, const QString &name)
{
    if (w)
        w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
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

static void answerLater(int ms, std::function<void(QMessageBox *)> fn)
{
    QTimer::singleShot(ms, qApp, [fn] {
        if (auto *b = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            fn(b);
        else if (QWidget *m = QApplication::activeModalWidget())
            m->close();
    });
}

// a folder replaced by a plain file of the same name (the session's root ignores permissions)
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
        QFile::remove(path);
        if (!away.isEmpty())
            QDir().rename(away, path);
        away.clear();
    }
};

static QStringList zipNames(const QString &zip) { return ZipReader(zip).fileNames(); }

static void waitNextSecond()
{
    const qint64 s = QDateTime::currentSecsSinceEpoch();
    while (QDateTime::currentSecsSinceEpoch() == s)
        QTest::qWait(20);
}

// a ZIP of n entries "proj/a/<i>.txt" and n "proj/b/<i>.txt", 1,500 bytes each
static void writeTwoFolderZip(const QString &zip, int n)
{
    ZipWriter zw(zip);
    for (const char *sub : {"a", "b"})
        for (int i = 0; i < n; ++i)
            zw.addFile(QStringLiteral("proj/%1/%2.txt").arg(QLatin1String(sub)).arg(i), QByteArray(1500, char('a' + i % 26)), false);
    zw.close();
}

// ------------------------------------------------------------------ backups and restore

static void backupChecks(const QString &W)
{
    QString err;
    // ================================================================ F-01: the clean-up after the title changed
    {
        auto p = Project::create(W, QStringLiteral("Zebra"), &err);
        textDoc(p.get(), p->draft(), QStringLiteral("Scene"), QStringLiteral("The text of the scene."));
        p->save(&err);
        const QString dir = W + QStringLiteral("/backups-f01");
        QStringList zebra, apple;
        bool ok = true;
        for (int i = 0; i < 3; ++i) {
            QString out;
            ok = backup::backupProject(p.get(), dir, 3, &out, &err) && ok;
            zebra << out;
            waitNextSecond();
        }
        p->setTitle(QStringLiteral("Apple"));
        p->save(&err);
        for (int i = 0; i < 2; ++i) {
            QString out, why;
            const bool made = backup::backupProject(p.get(), dir, 3, &out, &why);
            ok = made && why.isEmpty() && ok;
            apple << out;
            waitNextSecond();
        }
        // 0.91.0: the backups are told apart by the title, so those made as "Zebra" stay as they are and the new ones
        // are counted on their own (0.70.0 to 0.90.4, with a folder per project, counted all five and removed two)
        const QStringList kept = backup::backupsFor(p.get(), dir);
        check(ok && QFile::exists(apple.value(0)) && QFile::exists(apple.value(1)) && kept.size() == 2 && QFile::exists(zebra.value(0))
                  && QFile::exists(zebra.value(1)) && QFile::exists(zebra.value(2)),
              QStringLiteral("F-01: after the title changed from \"Zebra\" to \"Apple\" (keep 3), the new backups stay; those "
                             "made under the old title stay too and are not counted (0.91.0)"),
              kept.join(QLatin1String(" | ")));
        check(kept.value(0) == apple.value(0) && kept.value(1) == apple.value(1),
              QStringLiteral("F-01: the backups are in the order of their date"), kept.join(QLatin1String(" | ")));
        // one more "Apple" than the number to keep: the oldest "Apple" goes, no "Zebra"
        {
            QStringList more;
            for (int i = 0; i < 2; ++i) {
                QString out;
                ok = backup::backupProject(p.get(), dir, 3, &out, &err) && ok;
                more << out;
                waitNextSecond();
            }
            const QStringList then = backup::backupsFor(p.get(), dir);
            check(ok && then.size() == 3 && !QFile::exists(apple.value(0)) && then.last() == more.value(1) && QFile::exists(zebra.value(0)),
                  QStringLiteral("0.91.0: the clean-up removes only backups with the project's title of today"),
                  then.join(QLatin1String(" | ")));
        }
        // two backups within one second under two titles: each is listed with its title
        QString out1, out2;
        p->setTitle(QStringLiteral("Mango"));
        backup::backupProject(p.get(), dir, 10, &out1, &err);
        p->setTitle(QStringLiteral("Banana"));
        backup::backupProject(p.get(), dir, 10, &out2, &err);
        const QStringList now = backup::backupsFor(p.get(), dir);
        p->setTitle(QStringLiteral("Mango"));
        check(now == QStringList{out2} && backup::backupsFor(p.get(), dir) == QStringList{out1} && out1 != out2,
              QStringLiteral("two backups within one second under two titles are both there, each listed with its title"),
              now.join(QLatin1String(" | ")));
        p->setTitle(QStringLiteral("Banana"));
    }

    // ================================================================ F-05: a folder that is a link; incomplete ones kept to the number
    {
        auto p = Project::create(W, QStringLiteral("Linked"), &err);
        textDoc(p.get(), p->draft(), QStringLiteral("Scene"), QStringLiteral("Linked research."));
        p->save(&err);
        const QString outside = W + QStringLiteral("/research-elsewhere");
        writeAll(outside + QStringLiteral("/notes/a.txt"), "research a");
        writeAll(outside + QStringLiteral("/b.txt"), "research b");
        QFile::link(outside, p->folderPath() + QStringLiteral("/research"));
        const QString dir = W + QStringLiteral("/backups-f05");
        QString out, why;
        bool ok = backup::backupProject(p.get(), dir, 2, &out, &why);
        const QStringList names = zipNames(out);
        const QString top = QFileInfo(p->folderPath()).fileName();
        check(ok && why.isEmpty() && !out.contains(QLatin1String("incomplete")) && names.contains(top + QStringLiteral("/research/notes/a.txt"))
                  && names.contains(top + QStringLiteral("/research/b.txt")),
              QStringLiteral("F-05: a folder of the project that is a link to a folder elsewhere is backed up with its files "
                             "(before, it was left out without a word)"),
              why + QLatin1String(" | ") + names.filter(QStringLiteral("research")).join(QLatin1String(", ")));
        // a link back to the project (a loop) and a link that points nowhere: the backup is incomplete and says so
        QFile::link(p->folderPath(), p->folderPath() + QStringLiteral("/loop"));
        QFile::link(W + QStringLiteral("/not-there"), p->folderPath() + QStringLiteral("/gone"));
        QStringList incomplete;
        for (int i = 0; i < 3; ++i) {
            waitNextSecond();
            QString o, w;
            ok = backup::backupProject(p.get(), dir, 2, &o, &w);
            incomplete << o;
            why = w;
        }
        int nIncomplete = 0, nComplete = 0;
        for (const QString &f : backup::backupsFor(p.get(), dir))
            (f.contains(QLatin1String("(incomplete")) ? nIncomplete : nComplete)++;
        check(ok && why.contains(QLatin1String("loop")) && why.contains(QLatin1String("gone")) && incomplete.last().contains(QLatin1String("(incomplete")),
              QStringLiteral("F-05: a link back into the project and a link that points nowhere make the backup incomplete, and "
                             "the message names them"),
              why.left(400));
        check(nIncomplete == 2 && nComplete == 1 && !QFile::exists(incomplete.first()) && QFile::exists(out),
              QStringLiteral("incomplete backups: the newest ones up to the number to keep stay, the complete one stays (before, "
                             "they were never deleted)"),
              QStringLiteral("%1 incomplete, %2 complete").arg(nIncomplete).arg(nComplete));
        QFile::remove(p->folderPath() + QStringLiteral("/loop"));
        QFile::remove(p->folderPath() + QStringLiteral("/gone"));
        QFile::remove(p->folderPath() + QStringLiteral("/research"));
    }

    // ================================================================ U-01: 70,000 entries (the ZIP64 count)
    {
        const QString zip = W + QStringLiteral("/many.zip");
        {
            ZipWriter zw(zip);
            for (int i = 0; i < 70000; ++i)
                zw.addFile(QStringLiteral("f/%1.txt").arg(i), QByteArray::number(i), false);
            zw.close();
        }
        ZipReader zr(zip);
        bool read = false;
        const QByteArray last = zr.fileData(QStringLiteral("f/69999.txt"), &read);
        QString why;
        const bool one = zr.extractFile(QStringLiteral("f/65600.txt"), W + QStringLiteral("/one.txt"), &why);
        check(zr.isValid() && zr.fileNames().size() == 70000 && read && last == "69999" && one && readAll(W + QStringLiteral("/one.txt")) == "65600",
              QStringLiteral("U-01: an archive of 70,000 entries is read (before, only the 16-bit count: 4,464 of them)"),
              QStringLiteral("valid %1, %2 names, %3").arg(zr.isValid()).arg(zr.fileNames().size()).arg(zr.errorString() + why));
        QFile::remove(zip);
    }

    // ================================================================ a restore that does not fit is not started
    {
        // one entry that says it holds 2^50 bytes (ZIP64 field in the table of contents)
        const QByteArray name = "Big/text.md";
        QByteArray z;
        auto p16 = [&z](quint16 v) { z.append(char(v & 0xff)).append(char(v >> 8)); };
        auto p32 = [&z](quint32 v) {
            for (int i = 0; i < 4; ++i)
                z.append(char((v >> (8 * i)) & 0xff));
        };
        auto p64 = [&z](quint64 v) {
            for (int i = 0; i < 8; ++i)
                z.append(char((v >> (8 * i)) & 0xff));
        };
        p32(0x04034b50); p16(45); p16(0x0800); p16(0); p16(0); p16(0x21); p32(0); p32(0); p32(0); p16(quint16(name.size())); p16(0);
        z += name;
        const quint32 cdStart = quint32(z.size());
        p32(0x02014b50); p16(45); p16(45); p16(0x0800); p16(0); p16(0); p16(0x21); p32(0); p32(0); p32(0xFFFFFFFFu);
        p16(quint16(name.size())); p16(12); p16(0); p16(0); p16(0); p32(0); p32(0);
        z += name;
        p16(0x0001); p16(8); p64(1ull << 50);
        const quint32 cdSize = quint32(z.size()) - cdStart;
        p32(0x06054b50); p16(0); p16(0); p16(1); p16(1); p32(cdSize); p32(cdStart); p16(0);
        const QString zip = W + QStringLiteral("/huge-claim.zip");
        writeAll(zip, z);
        const QString parent = W + QStringLiteral("/restore-huge");
        QDir().mkpath(parent);
        QString pf, why;
        const QString dest = backup::restore(zip, parent, &pf, &why);
        check(dest.isEmpty() && why.contains(QLatin1String("Nothing was restored")) && QDir(parent).isEmpty(),
              QStringLiteral("F-02: a backup that needs more room than the drive has is not restored at all, and the message "
                             "says how much it needs"),
              why);
    }

    // ================================================================ F-02: a full disk while restoring (child runs)
    {
        const QString zip = W + QStringLiteral("/two-folders.zip");
        writeTwoFolderZip(zip, 200);
        const QString target = W + QStringLiteral("/extract-full");
        QDir().mkpath(target + QStringLiteral("/proj/b"));
        QProcess pr;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("LD_PRELOAD"), QStringLiteral(QB_FAULT_SHIM));
        env.insert(QStringLiteral("QB_SHIM_DIR"), target + QStringLiteral("/proj/b"));
        env.insert(QStringLiteral("QB_SHIM_MODE"), QStringLiteral("write"));
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        pr.setProcessEnvironment(env);
        pr.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child-extract"), zip, target});
        pr.waitForFinished(120000);
        const QString out = QString::fromUtf8(pr.readAllStandardOutput());
        int whole = 0, shortOrEmpty = 0;
        for (const QFileInfo &fi : QDir(target + QStringLiteral("/proj/a")).entryInfoList(QDir::Files))
            (fi.size() == 1500 ? whole : shortOrEmpty)++;
        const int inB = int(QDir(target + QStringLiteral("/proj/b")).entryList(QDir::Files | QDir::Hidden).size());
        check(out.contains(QLatin1String("result=0")) && out.contains(QLatin1String("failed=200")) && whole == 200 && shortOrEmpty == 0 && inB == 0
                  && out.contains(QLatin1String("proj/b/0.txt: No space left on device")),
              QStringLiteral("F-02: unpacking onto a full disk reports failure and names each file with the reason; no file is "
                             "left cut short or empty (before: success, 3,232 of 4,000 files cut short or empty)"),
              QStringLiteral("%1 whole, %2 short in a; %3 files in b; %4").arg(whole).arg(shortOrEmpty).arg(inB).arg(out.left(300)));
        // the message of Restore From Backup: the files named, at most 20, then how many more
        const QString parent = W + QStringLiteral("/restore-full");
        QDir().mkpath(parent);
        QProcess pr2;
        env.insert(QStringLiteral("QB_SHIM_DIR"), parent);
        pr2.setProcessEnvironment(env);
        pr2.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child-restore"), zip, parent});
        pr2.waitForFinished(120000);
        const QString out2 = QString::fromUtf8(pr2.readAllStandardOutput());
        check(out2.contains(QLatin1String("Not restored (400)")) && out2.contains(QLatin1String("proj/a/0.txt: No space left on device"))
                  && out2.contains(QLatin1String("and 380 more")),
              QStringLiteral("F-02, F-03: Restore From Backup names the files that could not be restored, with the reason"),
              out2.left(600));
        QFile::remove(zip);
    }

    // ================================================================ U-01, F-03: a 2.3 GB backup with a 2.3 GB file
    if (qEnvironmentVariableIsSet("QB_SKIP_BIG_BACKUP")) {
        info(QStringLiteral("(QB_SKIP_BIG_BACKUP: the 2.3 GB backup and restore were left out)"));
    } else {
        auto p = Project::create(W, QStringLiteral("Film"), &err);
        BinderItem *d = textDoc(p.get(), p->draft(), QStringLiteral("Script"), QStringLiteral("The script of the film."));
        p->save(&err);
        const qint64 size = 2300000000LL;
        const QString film = p->contentDir(d->id) + QStringLiteral("/film.mp4");
        {
            QFile f(film);
            f.open(QIODevice::WriteOnly);
            f.write("START");
            f.resize(size);
            f.seek(size - 3);
            f.write("END");
        }
        QElapsedTimer t;
        t.start();
        const QString dir = W + QStringLiteral("/backups-big");
        QString out, why;
        const bool made = backup::backupProject(p.get(), dir, 3, &out, &why);
        const qint64 zipSize = QFileInfo(out).size();
        const qint64 backupMs = t.restart();
        QFile::remove(film); // room for the restored copy
        const QString parent = W + QStringLiteral("/restore-big");
        QDir().mkpath(parent);
        QString pf, rwhy;
        const QString dest = backup::restore(out, parent, &pf, &rwhy);
        const qint64 restoreMs = t.elapsed();
        QString back;
        QDirIterator it(dest, {QStringLiteral("film.mp4")}, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext())
            back = it.next();
        QByteArray head, tail;
        {
            QFile f(back);
            if (f.open(QIODevice::ReadOnly)) {
                head = f.read(5);
                f.seek(size - 3);
                tail = f.read(3);
            }
        }
        info(QStringLiteral("backup %1 s, restore %2 s").arg(backupMs / 1000.0, 0, 'f', 1).arg(restoreMs / 1000.0, 0, 'f', 1));
        check(made && zipSize > (qint64(1) << 31),
              QStringLiteral("U-01: a backup of 2.3 GB is made and checked"), QStringLiteral("%1 bytes; %2").arg(zipSize).arg(why));
        check(!dest.isEmpty() && rwhy.isEmpty() && QFileInfo(back).size() == size && head == "START" && tail == "END" && !pf.isEmpty()
                  && QFile::exists(QFileInfo(pf).absolutePath() + QStringLiteral("/content/%1/text.md").arg(d->id)),
              QStringLiteral("U-01, F-03: it is restored whole, the 2.3 GB file too (before: \"not a complete ZIP file\", and "
                             "files above 1 GiB were never restored)"),
              rwhy.left(400));
        QDir(dest).removeRecursively();
        QDir(dir).removeRecursively();
    }

    // ================================================================ S-03: the backup a signal kept from being made
    {
        const QString f = W + QStringLiteral("/some project/x.qbx");
        writeAll(f, "x");
        backup::markDue(f);
        backup::markDue(f);
        const bool first = backup::takeDue(f), again = backup::takeDue(f);
        check(first && !again, QStringLiteral("S-03: a backup that was due is noted once and taken once"));
    }
}

// ------------------------------------------------------------------ snapshots and saving

static void savingChecks(const QString &W)
{
    QString err;
    // ================================================================ F-04: a damaged list of snapshot titles
    {
        auto p = Project::create(W, QStringLiteral("Snaps"), &err);
        BinderItem *d = textDoc(p.get(), p->draft(), QStringLiteral("Scene"), QStringLiteral("First words."));
        p->save(&err);
        snapshots::take(p.get(), d->id, QStringLiteral("Before the big rewrite"), &err);
        snapshots::take(p.get(), d->id, QStringLiteral("Second"), &err);
        const QString dir = p->snapshotsDir(d->id);
        const QByteArray idx = readAll(dir + QStringLiteral("/index.json"));
        // cut off inside the second entry
        writeAll(dir + QStringLiteral("/index.json"), idx.left(idx.indexOf("Second") - 3));
        QString why;
        const bool taken = snapshots::take(p.get(), d->id, QStringLiteral("Third"), &why);
        QStringList titles;
        for (const qb::Snapshot &s : snapshots::list(p.get(), d->id))
            titles << s.title;
        const QStringList kept = QDir(dir).entryList({QStringLiteral("index.damaged-*.json")}, QDir::Files);
        check(taken && why.isEmpty() && titles.contains(QStringLiteral("Before the big rewrite")) && titles.contains(QStringLiteral("Third"))
                  && kept.size() == 1 && readAll(dir + QLatin1Char('/') + kept.value(0)) == idx.left(idx.indexOf("Second") - 3),
              QStringLiteral("F-04: a list of titles cut off: the titles that can still be read stay, the damaged file is kept "
                             "beside it (before, the next snapshot left only its own title)"),
              titles.join(QLatin1String(" | ")) + QLatin1String(" ; ") + kept.join(QLatin1Char(',')) + why);
        // a list that cannot be written: told
        QFile::remove(dir + QStringLiteral("/index.json"));
        for (const QString &k : QDir(dir).entryList({QStringLiteral("index.damaged-*.json")}, QDir::Files))
            QFile::remove(dir + QLatin1Char('/') + k);
        QDir().mkpath(dir + QStringLiteral("/index.json"));
        QString why2, why3;
        const bool taken2 = snapshots::take(p.get(), d->id, QStringLiteral("Fourth"), &why2);
        const QList<qb::Snapshot> all = snapshots::list(p.get(), d->id);
        const bool titled = !all.isEmpty() && snapshots::setTitle(p.get(), all.first(), QStringLiteral("New title"), &why3);
        check(taken2 && !why2.isEmpty() && !titled && !why3.isEmpty(),
              QStringLiteral("F-04: a list of titles that cannot be written is reported (the snapshot is there; before, the "
                             "failure was ignored and the title change said it worked)"),
              why2 + QLatin1String(" | ") + why3);
        QDir(dir + QStringLiteral("/index.json")).removeRecursively();
    }

    // ================================================================ F-06: a save nobody looks at
    {
        auto p = Project::create(W, QStringLiteral("Unchecked"), &err);
        BinderItem *d = textDoc(p.get(), p->draft(), QStringLiteral("Scene"), QStringLiteral("Words."));
        p->save(&err);
        QSignalSpy spy(p.get(), &Project::saveFailed);
        const QString folder = p->folderPath();
        QDir().rename(folder, folder + QStringLiteral(".away"));
        QTextCursor c(p->docs()->text(d->id));
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral(" More."));
        const bool saved = p->save();
        const int told = int(spy.count());
        QString why;
        p->save(&why);
        check(!saved && told == 1 && spy.count() == 1 && !why.isEmpty(),
              QStringLiteral("F-06: a failed save whose result the caller does not look at (Compile, Replace, Empty Trash ...) "
                             "is told the way of the autosave's; a caller that asks for the reason gets it instead"),
              QStringLiteral("told %1, then %2").arg(told).arg(spy.count()));
        QDir().rename(folder + QStringLiteral(".away"), folder);
        p->save(&err);
    }

    // ================================================================ F-07: the kept copy of another version
    {
        const QString file = W + QStringLiteral("/f07/text.md");
        writeAll(file, "their version");
        const QString kept = syncsafe::keepCopy(file, W + QStringLiteral("/f07/settings"), QStringLiteral("Scene"));
        const QStringList left = QDir(W + QStringLiteral("/f07/settings/conflicts")).entryList(QDir::Files | QDir::Hidden);
        check(!kept.isEmpty() && readAll(kept) == "their version" && left.size() == 1,
              QStringLiteral("F-07: the other version is kept whole through a synced copy (no temporary file left)"),
              left.join(QLatin1String(", ")));
    }

    // ================================================================ F-08: Empty Trash saves first
    {
        auto p = Project::create(W, QStringLiteral("Trash"), &err);
        BinderItem *a = textDoc(p.get(), p->draft(), QStringLiteral("Old scene"), QStringLiteral("To go."));
        textDoc(p.get(), p->draft(), QStringLiteral("Stays"), QStringLiteral("Stays."));
        p->save(&err);
        snapshots::take(p.get(), a->id, QStringLiteral("Snap"), &err);
        const QString id = a->id, content = p->contentDir(a->id), snaps = p->snapshotsDir(a->id);
        p->model()->moveToTrash({a});
        p->save(&err);
        // the project file cannot be staged: a folder in place of its staged name
        const QString staged = p->indexPath() + QStringLiteral(".qbstage");
        QDir().mkpath(staged + QStringLiteral("/x"));
        QString why;
        const bool emptied = p->model()->emptyTrash(&why);
        const bool stayed = QFileInfo::exists(content) && QFileInfo::exists(snaps) && readAll(p->indexPath()).contains(id.toUtf8());
        QDir(staged).removeRecursively();
        const bool savedNow = p->save(&err);
        check(!emptied && !why.isEmpty() && stayed,
              QStringLiteral("F-08: Empty Trash when the project file cannot be saved: the files and snapshots stay, and the "
                             "project file on the disk still lists them (before, the files went first)"),
              why);
        check(savedNow && !QFileInfo::exists(content) && !QFileInfo::exists(snaps) && !readAll(p->indexPath()).contains(id.toUtf8()),
              QStringLiteral("F-08: ... and they go with the next save that works"), err);
    }

    // ================================================================ U-02: three spare copies
    {
        auto p = Project::create(W, QStringLiteral("Spare"), &err);
        p->save(&err);
        const QString file = p->indexPath();
        p.reset();
        for (int i = 1; i <= 3; ++i) {
            auto o = Project::open(file, &err);
            if (!o)
                break;
            textDoc(o.get(), o->draft(), QStringLiteral("Generation %1").arg(i), QStringLiteral("x"));
            o->save(&err);
        }
        { auto o = Project::open(file, &err); }
        const QString s = QFileInfo(file).absolutePath() + QStringLiteral("/settings/spare-copy");
        const QByteArray s1 = readAll(s + QStringLiteral(".qbx")), s2 = readAll(s + QStringLiteral("-2.qbx")), s3 = readAll(s + QStringLiteral("-3.qbx"));
        check(s1.contains("Generation 3") && s2.contains("Generation 2") && !s2.contains("Generation 3") && s3.contains("Generation 1")
                  && !s3.contains("Generation 2"),
              QStringLiteral("U-02: the project file of the last three openings is kept (spare-copy, -2, -3; before, one copy, "
                             "replaced at every opening)"));
        check(Project::resolveProjectFile(s + QStringLiteral("-2.qbx")) == file,
              QStringLiteral("U-02: an older spare copy opened by mistake names the project itself"),
              Project::resolveProjectFile(s + QStringLiteral("-2.qbx")));
    }

    // ================================================================ U-04: storage conversion one document at a time
    {
        auto p = Project::create(W, QStringLiteral("Convert"), &err);
        QStringList ids;
        for (int i = 0; i < 30; ++i)
            ids << textDoc(p.get(), p->draft(), QStringLiteral("Doc %1").arg(i), QStringLiteral("Text of document %1.").arg(i))->id;
        p->save(&err);
        const QString file = p->indexPath();
        p.reset();
        auto o = Project::open(file, &err);
        QTextDocument *open5 = o->docs()->text(ids.value(5)); // one is open (it stays loaded)
        Q_UNUSED(open5);
        const bool ok = o->docs()->convertStorage(StorageFormat::Rtf, &err);
        int loaded = 0;
        for (const QString &id : ids)
            if (o->docs()->loadedText(id))
                ++loaded;
        bool files = true;
        for (const QString &id : ids)
            files = files && QFile::exists(o->contentDir(id) + QStringLiteral("/text.rtf")) && !QFile::exists(o->contentDir(id) + QStringLiteral("/text.md"));
        const QString back = o->docs()->text(ids.value(17))->toPlainText();
        check(ok && files && loaded == 1 && o->docs()->loadedText(ids.value(5)) && back == QStringLiteral("Text of document 17."),
              QStringLiteral("U-04: converting the storage keeps only the documents that were open in memory (before, all of "
                             "them), and every text arrives in the new format"),
              QStringLiteral("%1 loaded; %2; \"%3\"").arg(loaded).arg(err, back));
        // the comparison gives up with a coarse result long before its table grows large
        QString a, b;
        for (int i = 0; i < 6000; ++i) {
            a += QChar(u'a' + (i * 7) % 26);
            b += QChar(u'a' + (i * 11 + 3) % 26);
        }
        QElapsedTimer t;
        t.start();
        const QList<DiffOp> ops = diffTexts(a, b, DiffGranularity::Character);
        check(!ops.isEmpty() && t.elapsed() < 20000,
              QStringLiteral("U-04: the comparison of two long, very different paragraphs ends (at most 2,500 steps)"),
              QStringLiteral("%1 ms, %2 parts").arg(t.elapsed()).arg(ops.size()));
    }

    // ================================================================ S-06: the ui file named by the computer's key
    {
        auto p = Project::create(W, QStringLiteral("UiName"), &err);
        p->save(&err);
        const QString name = QFileInfo(p->uiStateFile()).fileName();
        const QString host = QSysInfo::machineHostName();
        check(name == QStringLiteral("ui-%1.json").arg(syncsafe::machineKey()) && (host.isEmpty() || !name.contains(host)),
              QStringLiteral("S-06: the window state file is named by the computer's anonymous key, not its name"), name);
        const QString file = p->indexPath();
        const QString old = p->oldUiStateFile(), now = p->uiStateFile();
        p.reset();
        // as left by 0.80.0: only the file named by the computer's name
        QFile::remove(now);
        writeAll(old, R"({"qbTestKey": "old value"})");
        auto o = Project::open(file, &err);
        const bool read = o && o->uiState.value(QStringLiteral("qbTestKey")).toString() == QStringLiteral("old value");
        if (o) {
            o->uiState.insert(QStringLiteral("qbTestKey"), QStringLiteral("new value"));
            o->markUiStateDirty();
            o->save(&err);
        }
        check(read && o && QFile::exists(o->uiStateFile()) && !QFile::exists(old),
              QStringLiteral("S-06: the file of earlier versions (with the computer's name) is read once, then replaced"));
    }
}

// ------------------------------------------------------------------ the window

static int hintsBelow45(QWidget *root, int *count)
{
    int bad = 0;
    for (QLabel *l : root->findChildren<QLabel *>()) {
        if (!l->property("qbHint").toBool())
            continue;
        ++*count;
        const QColor fg = l->palette().color(QPalette::Active, QPalette::WindowText);
        const QColor bg = l->palette().color(QPalette::Active, QPalette::Window);
        if (!l->isEnabled() || hintlabel::contrast(fg, bg) < 4.5)
            ++bad;
    }
    return bad;
}

static void windowChecks(const QString &W)
{
    QString err;
    AppSettings &as = AppSettings::instance();
    // ================================================================ S-01: explanations that can be read
    {
        OptionsDialog dlg(nullptr);
        dlg.show();
        settle(400);
        int n = 0;
        const int bad = hintsBelow45(&dlg, &n);
        shot(&dlg, QStringLiteral("v0810-options"));
        check(n > 10 && bad == 0,
              QStringLiteral("S-01: every line of explanation in File > Options has a contrast of 4.5:1 or more (before, 1.62:1)"),
              QStringLiteral("%1 lines, %2 below").arg(n).arg(bad));
        dlg.close();
        FirstStartDialog f;
        f.show();
        settle(300);
        int m = 0;
        const int badF = hintsBelow45(&f, &m);
        shot(&f, QStringLiteral("v0810-first-start"));
        check(m >= 1 && badF == 0 && !f.findChild<QRadioButton *>(),
              QStringLiteral("S-01, S-09: First Start: its explanation is readable, and it asks only about backups"),
              QStringLiteral("%1 lines, %2 below").arg(m).arg(badF));
        check(f.windowTitle() == QStringLiteral("First Start"),
              QStringLiteral("S-07: the First Start window's title does not repeat the program's name (Qt adds it)"), f.windowTitle());
        f.close();
        StartDialog sd;
        check(sd.windowTitle() == QStringLiteral("Projects"),
              QStringLiteral("S-07: the Projects window's title does not repeat the program's name"), sd.windowTitle());
        // a dark look: the color is worked out again
        QWidget host;
        QPalette dark;
        dark.setColor(QPalette::Window, QColor(0x2b, 0x2b, 0x2b));
        dark.setColor(QPalette::WindowText, QColor(0xe8, 0xe8, 0xe8));
        auto *l = new QLabel(QStringLiteral("An explanation."), &host);
        hintlabel::style(l);
        host.setPalette(dark);
        settle(50);
        const double c = hintlabel::contrast(l->palette().color(QPalette::Active, QPalette::WindowText), QColor(0x2b, 0x2b, 0x2b));
        check(c >= 4.5 && c < 14.0, QStringLiteral("S-01: in a dark look the explanation stays readable and still looks secondary"),
              QString::number(c, 'f', 2));
    }

    // a project in a window
    const QString bdir = W + QStringLiteral("/backups-window");
    as.backupDir = bdir;
    as.backupOnClose = true;
    as.backupOnOpen = false;
    as.backupKeep = 5;
    as.save();
    auto proj = Project::create(W, QStringLiteral("Window"), &err);
    BinderItem *chapter = textDoc(proj.get(), proj->draft(), QStringLiteral("Chapter"), QStringLiteral("The first words of the chapter."));
    BinderItem *old = textDoc(proj.get(), proj->draft(), QStringLiteral("Old"), QStringLiteral("Old words."));
    proj->save(&err);
    snapshots::take(proj.get(), old->id, QStringLiteral("Snap"), &err);
    proj->model()->moveToTrash({old});
    proj->save(&err);
    const QString chapterId = chapter->id;
    const QString projectFile = proj->indexPath();
    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1300, 900);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(proj)), QStringLiteral("(project open)"));
    settle(600);
    Project *p = w.project();

    // ================================================================ F-08: the question names the snapshots
    {
        QString question;
        answerLater(400, [&question](QMessageBox *b) {
            question = b->text();
            b->reject();
        });
        if (QAction *a = w.findChild<QAction *>(QStringLiteral("emptyTrash")))
            a->trigger();
        settle(600);
        check(question.contains(QLatin1String("1 snapshot")) && !p->trash()->children.isEmpty(),
              QStringLiteral("F-08: Empty Trash names the snapshots that go with the documents (answered Cancel: nothing deleted)"),
              question);
    }

    // ================================================================ S-08: the failed-save notice
    {
        check(MainWindow::plainSaveReason(QStringLiteral("Could not save /a/b/text.md: Cannot write /a/b/text.md.qbstage: No space left on device"))
                      == QStringLiteral("The disk is full.")
                  && MainWindow::plainSaveReason(QStringLiteral("x: Read-only file system")).contains(QLatin1String("only be read")),
              QStringLiteral("S-08: the reason of a failed save in plain words"));
        Block block(p->contentDir(chapterId));
        QTextCursor c(p->docs()->text(chapterId));
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral(" More text."));
        waitFor([&] { return w.saveFailedSince().isValid(); }, 15000);
        settle(400);
        QPointer<QMessageBox> notice = w.findChild<QMessageBox *>(QStringLiteral("saveFailedNotice"));
        const QRect ng = notice ? notice->frameGeometry() : QRect();
        shot(&w, QStringLiteral("v0810-save-failed"));
        if (notice)
            shot(notice, QStringLiteral("v0810-save-failed-notice"));
        check(notice && notice->isVisible() && notice->text().startsWith(QLatin1String("Your latest changes could not be saved. "))
                  && notice->text().contains(QLatin1String("Chapter")) && !notice->text().contains(p->folderPath())
                  && notice->detailedText().contains(p->folderPath()),
              QStringLiteral("S-08: the notice leads with the reason and the document; the paths are in its details"),
              notice ? notice->text() + QLatin1String(" || ") + notice->detailedText().left(300) : QStringLiteral("no notice"));
        check(notice && ng.center().x() > w.frameGeometry().center().x() && ng.center().y() > w.frameGeometry().center().y(),
              QStringLiteral("S-08: the notice is in the window's lower right corner, not over the middle of the text"),
              QStringLiteral("notice %1,%2 %3x%4; window %5,%6 %7x%8")
                  .arg(ng.x()).arg(ng.y()).arg(ng.width()).arg(ng.height())
                  .arg(w.frameGeometry().x()).arg(w.frameGeometry().y()).arg(w.frameGeometry().width()).arg(w.frameGeometry().height()));
        block.undo();
        waitFor([&] { return !w.saveFailedSince().isValid(); }, 30000);
        if (notice)
            notice->close();
    }

    // ================================================================ S-03: the backup a signal kept from being made
    {
        w.saveForTermination(); // what a SIGTERM runs (then the program ends: here the window closes its project)
        w.closeProject();
        settle(300);
        auto again = Project::open(projectFile, &err);
        w.adoptProject(std::move(again));
        settle(600);
        const QString due = w.lastDueBackup();
        check(!due.isEmpty() && QFile::exists(due) && w.statusBar()->currentMessage().contains(QLatin1String("backup was made now")),
              QStringLiteral("S-03: after the program was ended by a signal, the next opening makes the backup the closing "
                             "would have made, and says so"),
              due + QLatin1String(" | ") + w.statusBar()->currentMessage());
        check(!backup::takeDue(projectFile), QStringLiteral("S-03: ... once"));
    }

    // ================================================================ S-12: opening when the disk is full
    {
        w.closeProject();
        settle(300);
        // a project left by a session that ended without closing: its text was saved, the counts in the project file
        // were not yet (they follow a minute later); opened while nothing can be written into its folder
        {
            // the chapter's counts in the project file as they were before its last words
            QByteArray x = readAll(projectFile);
            const qsizetype at = x.lastIndexOf('<', x.indexOf(chapterId.toUtf8()));
            const qsizetype end = x.indexOf('>', at);
            QString el = QString::fromUtf8(x.mid(at, end - at));
            const QString before = el;
            el.replace(QRegularExpression(QStringLiteral(" words=\"\\d+\"")), QStringLiteral(" words=\"2\""));
            el.replace(QRegularExpression(QStringLiteral(" chars=\"\\d+\"")), QStringLiteral(" chars=\"9\""));
            if (at < 0 || el == before)
                info(QStringLiteral("(the counts were not found in the project file: %1)").arg(before));
            x.replace(at, end - at, el.toUtf8());
            writeAll(projectFile, x);
        }
        QProcess pr;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("LD_PRELOAD"), QStringLiteral(QB_FAULT_SHIM));
        env.insert(QStringLiteral("QB_SHIM_DIR"), QFileInfo(projectFile).absolutePath());
        env.insert(QStringLiteral("QB_SHIM_MODE"), QStringLiteral("write"));
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        pr.setProcessEnvironment(env);
        pr.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child-open"), projectFile});
        pr.waitForFinished(120000);
        const QString out = QString::fromUtf8(pr.readAllStandardOutput());
        check(out.contains(QLatin1String("opened=1")) && out.contains(QLatin1String("failed=0")) && out.contains(QLatin1String("unsaved=0")),
              QStringLiteral("S-12: a project whose counts are older than its texts, opened on a full disk, shows no failed save "
                             "and no unsaved change before any edit (before, the new counts were written at once)"),
              out.left(800));
        check(out.contains(QLatin1String("notice=The project file could not be updated with the new word counts. The disk is full. Nothing you wrote is lost")),
              QStringLiteral("S-12: when only the new counts cannot be written, the notice says that, not \"Your latest changes "
                             "could not be saved\""),
              out.left(800));
    }
    delete wp;
}

// ------------------------------------------------------------------ release

static void releaseChecks(const QString &src)
{
    const QString yml = QString::fromUtf8(readAll(src + QStringLiteral("/.github/workflows/release.yml")));
    auto job = [&yml](const QString &name) {
        const qsizetype i = yml.indexOf(QStringLiteral("\n  %1:\n").arg(name));
        if (i < 0)
            return QString();
        const qsizetype j = yml.indexOf(QRegularExpression(QStringLiteral("\n  [a-z-]+:\n")), i + 3);
        return yml.mid(i, j < 0 ? -1 : j - i);
    };
    const QString publish = job(QStringLiteral("publish"));
    const QString needs = publish.mid(publish.indexOf(QLatin1String("needs:")), publish.indexOf(QLatin1Char('\n'), publish.indexOf(QLatin1String("needs:"))) - publish.indexOf(QLatin1String("needs:")));
    check(needs.contains(QLatin1String("tests")) && needs.contains(QLatin1String("code-check")) && needs.contains(QLatin1String("sign-checksums")),
          QStringLiteral("S-02, S-11: the release is published only after the full test suite and the code check have passed"), needs);
    check(job(QStringLiteral("code-check")).contains(QLatin1String("tools/code-check.sh")) && QFileInfo(src + QStringLiteral("/tools/code-check.sh")).isExecutable(),
          QStringLiteral("S-11: the code check (cppcheck) is a job of the release"));
    if (QStandardPaths::findExecutable(QStringLiteral("cppcheck")).isEmpty()) {
        info(QStringLiteral("(cppcheck is not installed: tools/code-check.sh was not run here)"));
    } else {
        QProcess pr;
        pr.setProcessChannelMode(QProcess::MergedChannels);
        pr.start(src + QStringLiteral("/tools/code-check.sh"), {});
        pr.waitForFinished(600000);
        const QString out = QString::fromUtf8(pr.readAll());
        check(pr.exitStatus() == QProcess::NormalExit && pr.exitCode() == 0,
              QStringLiteral("S-11: tools/code-check.sh finds nothing in the saving, backup and update code"), out.right(1500));
    }
}

// ------------------------------------------------------------------ child runs (with the fault shim)

static int childRun(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString what = QString::fromLocal8Bit(argv[1]);
    if (what == QLatin1String("--child-extract") && argc > 3) {
        ZipReader zr(QString::fromLocal8Bit(argv[2]));
        QStringList failed;
        const bool ok = zr.extractAll(QString::fromLocal8Bit(argv[3]), &failed);
        std::printf("result=%d failed=%d\n%s\n", ok ? 1 : 0, int(failed.size()), qPrintable(failed.join(QLatin1Char('\n'))));
        return 0;
    }
    if (what == QLatin1String("--child-restore") && argc > 3) {
        QString pf, err;
        const QString dest = backup::restore(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]), &pf, &err);
        std::printf("dest=%s\n%s\n", qPrintable(dest), qPrintable(err));
        return 0;
    }
    return 2;
}

static int childOpen(int argc, char **argv)
{
    qputenv("QB_NO_DIALOGS", "1");
    QApplication app(argc, argv);
    AppSettings::instance().load();
    AppSettings::instance().backupOnOpen = false;
    QString err;
    auto p = Project::open(QString::fromLocal8Bit(argv[2]), &err);
    if (!p) {
        std::printf("opened=0 %s\n", qPrintable(err));
        return 0;
    }
    MainWindow w;
    w.show();
    const bool opened = w.adoptProject(std::move(p));
    QSignalSpy spy(w.project(), &Project::saveFailed);
    QStringList why;
    QObject::connect(w.project(), &Project::saveFailed, [&why](const QString &e) { why << e; });
    // the counts of every text are worked out again (as when the window shows them)
    for (BinderItem *it : w.project()->allItems())
        if (it->hasText())
            w.project()->docs()->recount(it->id);
    // the time of an autosave, of the minute's write of counts, and a switch to another window and back
    settle(3000);
    QWidget other;
    other.show();
    other.activateWindow();
    settle(1500);
    w.activateWindow();
    settle(3000);
    std::printf("opened=%d failed=%d unsaved=%d modified=%d state=%s\n%s\n", opened ? 1 : 0, int(spy.count()) + (w.saveFailedSince().isValid() ? 1 : 0),
                w.project()->hasUnsavedChanges() ? 1 : 0, w.isWindowModified() ? 1 : 0, qPrintable(w.saveStateText()),
                qPrintable(why.join(QLatin1String(" || "))));
    // the full save writes the new counts: it fails here, and the notice tells what it was about
    const bool fullSave = w.project()->save();
    settle(500);
    auto *notice = w.findChild<QMessageBox *>(QStringLiteral("saveFailedNotice"));
    std::printf("full save=%d signals=%d\nnotice=%s\n", fullSave ? 1 : 0, int(spy.count()), notice ? qPrintable(notice->text()) : "none");
    std::fflush(stdout);
    std::_Exit(0); // no closing (it would try to write)
}

int main(int argc, char **argv)
{
    if (argc > 3 && (qstrcmp(argv[1], "--child-extract") == 0 || qstrcmp(argv[1], "--child-restore") == 0))
        return childRun(argc, argv);
    if (argc > 2 && qstrcmp(argv[1], "--child-open") == 0)
        return childOpen(argc, argv);
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qputenv("XDG_RUNTIME_DIR", (home.path() + QLatin1String("/run")).toUtf8());
    QDir().mkpath(home.path() + QLatin1String("/run"));
    QFile::setPermissions(home.path() + QLatin1String("/run"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("QB_NO_DIALOGS", "1");
    QCoreApplication::setOrganizationName(QStringLiteral("Quillbinder"));
    QCoreApplication::setApplicationName(QStringLiteral("Quillbinder"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    const QString src = QStringLiteral(QB_TEST_SOURCE_DIR) + QStringLiteral("/..");
    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.81.0")) >= 0,
          QStringLiteral("version 0.81.0 or newer"), QStringLiteral(QB_APP_VERSION_SHOWN));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    AppSettings::instance().load();
    QTemporaryDir work;
    const QString W = work.path();
    // a window left open by mistake would stop the run: told and closed after 10 s
    QTimer watcher;
    QElapsedTimer modalSince;
    QObject::connect(&watcher, &QTimer::timeout, [&modalSince] {
        QWidget *m = QApplication::activeModalWidget();
        if (!m) {
            modalSince.invalidate();
            return;
        }
        if (!modalSince.isValid())
            modalSince.start();
        if (modalSince.elapsed() > 10000) {
            auto *b = qobject_cast<QMessageBox *>(m);
            info(QStringLiteral("(a window stayed open and was closed: %1 %2 \"%3\")")
                     .arg(QString::fromLatin1(m->metaObject()->className()), m->objectName(), b ? b->text().left(200) : m->windowTitle()));
            m->close();
            modalSince.invalidate();
        }
    });
    watcher.start(1000);
    // QB_V0810_ONLY=backups,saving,window,release runs parts
    const QStringList only = qEnvironmentVariable("QB_V0810_ONLY").split(QLatin1Char(','), Qt::SkipEmptyParts);
    auto want = [&only](const char *n) { return only.isEmpty() || only.contains(QLatin1String(n)); };
    if (want("backups"))
        backupChecks(W);
    if (want("saving"))
        savingChecks(W);
    if (want("window"))
        windowChecks(W);
    if (want("release"))
        releaseChecks(src);
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
