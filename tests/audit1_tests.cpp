// Scripted run for version 0.64.1, the fixes of the external audit of 2026-11-09 (the points that are
// changes of the program; the audit's numbers in brackets):
//  - ids from a project file that are not plain names never become parts of paths (S-1);
//  - ZIP archives: names outside the target folder, damaged entries, checksums (X-1, ST-1, ST-2);
//  - a compound file (Word 97) with a loop in its sector chain (ST-3); XML nested without end (S-4);
//  - settings a project may hold, and the question before programs and places from an options file
//    (X-2, D-2, D-4);
//  - the warning before a package with program code (X-3, the code part);
//  - links that lead out of the program (X-4);
//  - the socket folder in /tmp (X-9);
//  - Scrivener's lock looked at before every write into the .scriv (C-4), and a .scriv file changed
//    elsewhere whose version cannot be kept is not replaced;
//  - the name of this computer in shared folders (D-3);
//  - a full disk or a failing drive at the last moment of a save, and the save before a backup
//    (found while the system test's fault cases were written; tests/faultshim.cpp, LD_PRELOAD);
//  - a program built for release has no test switches (X-10): QB_RELEASE_BINARY=<program or library>
//    (several, separated by ':') are searched for the switches' names.
// Saves screenshots into the folder given as first argument. Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_audit1_tests /tmp/shots
#include "core/BinderItem.h"
#include "core/Backup.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/PathSafety.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "core/SettingsLayer.h"
#include "core/Snapshots.h"
#include "core/SyncSafety.h"
#include "core/Zip.h"
#include "compile/PluginManager.h"
#include "import/MiniXml.h"
#include "import/ScrivenerFormat.h"
#include "ui/AddonDialogs.h"
#include "ui/AppSettings.h"
#include "ui/EditorPane.h"
#include "ui/LinkOpen.h"
#include "ui/MainWindow.h"
#include "ui/MediaView.h"
#include "ui/OptionsDialog.h"
#include "ui/Theme.h"
#include "ui/ToolHost.h"

#include "Cfb.h"

#include "AppInfo.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QtEndian>

#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

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

static void info(const QString &what)
{
    std::printf("      %s\n", qPrintable(what));
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

static void shot(QWidget *w, const QString &name)
{
    if (w)
        w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}

// answers the next modal QMessageBox (button: the standard button to click); text/name: what it said
static void answerBox(int delayMs, QMessageBox::StandardButton button, QString *text = nullptr, QString *name = nullptr)
{
    QTimer::singleShot(delayMs, qApp, [=] {
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *d = qobject_cast<QMessageBox *>(tl))
                if (d->isVisible() && d->isModal()) {
                    if (text)
                        *text = d->text();
                    if (name)
                        *name = d->objectName();
                    if (QAbstractButton *b = d->button(button))
                        b->click();
                    else
                        d->reject();
                    return;
                }
    });
}

static QMessageBox *visibleBox(const QString &name)
{
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (auto *b = qobject_cast<QMessageBox *>(tl))
            if (b->isVisible() && b->objectName() == name)
                return b;
    return nullptr;
}

static int visibleBoxes(const QString &name)
{
    int n = 0;
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (auto *b = qobject_cast<QMessageBox *>(tl))
            if (b->isVisible() && b->objectName() == name)
                ++n;
    return n;
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

static BinderItem *byTitle(Project *p, const QString &title)
{
    for (BinderItem *it : p->allItems())
        if (it->title == title)
            return it;
    return nullptr;
}

// the UUID of the binder item with this title in a .scrivx
static QString scrivUuid(const QString &scriv, const QString &title)
{
    const QByteArray x = readAll(scrivener::scrivxIn(scriv));
    const int t = int(x.indexOf("<Title>" + title.toUtf8() + "</Title>"));
    const int u = int(x.lastIndexOf("UUID=\"", t));
    return (t > 0 && u > 0) ? QString::fromUtf8(x.mid(u + 6, 36)) : QString();
}

// a child run under the fault library: opens the project, adds to "Kept", saves or backs up
static int childRun(const QString &folder, const QString &what)
{
    QString err;
    auto p = Project::open(folder, &err);
    BinderItem *it = p ? byTitle(p.get(), QStringLiteral("Kept")) : nullptr;
    if (!it) {
        std::printf("result=open-failed err=%s\n", qPrintable(err));
        return 3;
    }
    QTextCursor c(what == QLatin1String("savenotes") ? p->docs()->notes(it->id) : p->docs()->text(it->id));
    c.movePosition(QTextCursor::End);
    c.insertText(QStringLiteral(" New words."));
    bool ok = false;
    if (what == QLatin1String("save") || what == QLatin1String("savenotes")) {
        ok = p->save(&err);
    } else {
        QString out;
        ok = backup::backupProject(p.get(), what.mid(7), 5, &out, &err);
    }
    std::printf("result=%d err=%s\n", ok ? 1 : 0, qPrintable(err));
    std::fflush(stdout);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc > 3 && qstrcmp(argv[1], "--child") == 0) {
        QApplication app(argc, argv);
        return childRun(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    }
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qputenv("XDG_CACHE_HOME", (home.path() + QLatin1String("/cache")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderAudit1Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderAudit1Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    const QString src = QStringLiteral(QB_TEST_SOURCE_DIR);

    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.64.1")) >= 0,
          QStringLiteral("version 0.64.1 or newer"), QStringLiteral(QB_APP_VERSION_SHOWN));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.backupOnClose = false;
    as.backupOnOpen = false;
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();
    templates::Info blank;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("blank"))
            blank = t;
    QString err;

    // ================================================================ S-1: ids from a project file
    {
        QTemporaryDir root;
        auto p = templates::createProject(blank, root.path(), QStringLiteral("Ids"), &err);
        check(p != nullptr, QStringLiteral("(a project)"), err);
        BinderItem *doomed = textDoc(p.get(), p->draft(), QStringLiteral("Doomed"), QStringLiteral("To the trash."));
        const QString oldId = doomed->id;
        p->markIndexDirty();
        p->save(&err);
        const QString folder = p->folderPath();
        const QString index = p->displayFile();
        p.reset();
        // the project file names the document "../../victim": a folder beside the project
        const QString victim = QDir::cleanPath(folder + QStringLiteral("/content/../../victim"));
        writeAll(victim + QStringLiteral("/keep.txt"), "must stay");
        QByteArray x = readAll(index);
        const int hits = int(x.count(oldId.toUtf8()));
        x.replace(oldId.toUtf8(), "../../victim");
        writeAll(index, x);
        auto q = Project::open(folder, &err);
        BinderItem *it = q ? byTitle(q.get(), QStringLiteral("Doomed")) : nullptr;
        check(hits > 0 && it && pathsafety::isSafeId(it->id) && it->id != QLatin1String("../../victim")
                  && pathsafety::isInside(q->contentDir(it->id), folder),
              QStringLiteral("S-1: an item id with \"../\" in the project file gets a new id; its folder lies inside the project"),
              it ? it->id + QStringLiteral(" -> ") + q->contentDir(it->id) : err);
        check(!pathsafety::isInside(QString(), folder), QStringLiteral("(isInside of nothing is false)"));
        check(pathsafety::isInside(QDir::cleanPath(folder + QStringLiteral("/content/") + QStringLiteral("x")), folder)
                  && !pathsafety::isInside(victim, folder) && !pathsafety::isSafeId(QStringLiteral("../x"))
                  && !pathsafety::isSafeId(QStringLiteral("a/b")) && !pathsafety::isSafeId(QString())
                  && pathsafety::isSafeId(QStringLiteral("7C3B2A10-1F2E-4D5C-9B8A-112233445566")) && pathsafety::isSafeId(QStringLiteral("123")),
              QStringLiteral("S-1: ids are plain names (letters, digits, '-', '_'); a path outside the project is not inside it"));
        if (q && it) {
            q->model()->moveToTrash({it});
            q->model()->emptyTrash();
            q->save(&err);
        }
        check(QFileInfo::exists(victim + QStringLiteral("/keep.txt")),
              QStringLiteral("S-1: Empty Trash with that document deletes nothing outside the project (before: the folder beside it)"));
        // a symbolic link in the trash's folder: the link goes, not what it points to
        QTemporaryDir other;
        writeAll(other.path() + QStringLiteral("/precious.txt"), "x");
        const QString link = root.path() + QStringLiteral("/linkdir");
        QFile::link(other.path(), link);
        check(pathsafety::removeTreeInside(link, root.path()) && !QFileInfo(link).isSymLink()
                  && QFileInfo::exists(other.path() + QStringLiteral("/precious.txt")),
              QStringLiteral("S-1: removing a folder that is a symbolic link removes the link only"));
        check(!pathsafety::removeTreeInside(other.path(), root.path()) && QFileInfo::exists(other.path() + QStringLiteral("/precious.txt")),
              QStringLiteral("S-1: a folder outside the project is never removed"));
        // the media file's ending comes from the project file too
        if (q) {
            BinderItem *pic = q->model()->createItem(q->research(), -1, ItemType::Image, QStringLiteral("Pic"));
            pic->fileExt = QStringLiteral("png/../../../evil");
            check(pathsafety::isInside(q->mediaFilePath(pic), folder) && q->mediaFilePath(pic).endsWith(QLatin1String("/file.bin")),
                  QStringLiteral("S-1: a file ending with '/' or '..' is not used (file.bin instead)"), q->mediaFilePath(pic));
        }
    }

    // ================================================================ X-1, ST-1, ST-2: ZIP archives
    {
        QTemporaryDir tmp;
        const QString zip = tmp.path() + QStringLiteral("/evil.zip");
        const QString abs = tmp.path() + QStringLiteral("/absolute.txt");
        {
            ZipWriter w(zip);
            w.addFile(QStringLiteral("ok/fine.txt"), "fine");
            w.addFile(QStringLiteral("../outside.txt"), "outside");
            w.addFile(abs, "absolute");
            w.addFile(QStringLiteral("a\\..\\..\\back.txt"), "back");
            w.addFile(QStringLiteral("C:/drive.txt"), "drive");
            w.close();
        }
        const QString target = tmp.path() + QStringLiteral("/target");
        QDir().mkpath(target);
        ZipReader r(zip);
        const bool ok = r.extractAll(target);
        check(!ok && readAll(target + QStringLiteral("/ok/fine.txt")) == "fine" && !QFileInfo::exists(tmp.path() + QStringLiteral("/outside.txt"))
                  && !QFileInfo::exists(abs) && !QFileInfo::exists(tmp.path() + QStringLiteral("/back.txt"))
                  && QDir(target).entryList(QDir::NoDotAndDotDot | QDir::AllEntries) == QStringList{QStringLiteral("ok")},
              QStringLiteral("X-1: an archive's absolute names, '..' parts, backslashes and drive letters are not written; the "
                             "rest is, inside the target folder"),
              QDir(target).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).join(QLatin1Char(',')));
        check(!pathsafety::isSafeRelative(QStringLiteral("/etc/x")) && !pathsafety::isSafeRelative(QStringLiteral("a/../b"))
                  && !pathsafety::isSafeRelative(QStringLiteral("a\\b")) && !pathsafety::isSafeRelative(QStringLiteral("c:x"))
                  && !pathsafety::isSafeRelative(QStringLiteral("a//b")) && pathsafety::isSafeRelative(QStringLiteral("a/b.txt"))
                  && pathsafety::isSafeRelative(QStringLiteral("folder/")),
              QStringLiteral("X-1: the rule for names inside archives and packages"));
        // a stored entry with one byte changed: its checksum no longer fits
        const QString damaged = tmp.path() + QStringLiteral("/damaged.zip");
        {
            ZipWriter w(damaged);
            w.addFile(QStringLiteral("text.txt"), QByteArray("The keeper counted the ships."), false);
            w.close();
        }
        QByteArray d = readAll(damaged);
        const int at = int(d.indexOf("keeper"));
        if (at > 0)
            d[at] = 'K';
        writeAll(damaged, d);
        bool read = true;
        const QByteArray got = ZipReader(d).fileData(QStringLiteral("text.txt"), &read);
        QString verr;
        const qint64 v = ZipWriter::verify(damaged, &verr);
        const QString t2 = tmp.path() + QStringLiteral("/t2");
        QDir().mkpath(t2);
        const bool ex = ZipReader(damaged).extractAll(t2);
        check(at > 0 && !read && got.isEmpty() && v < 0 && !ex && !QFileInfo::exists(t2 + QStringLiteral("/text.txt")),
              QStringLiteral("ST-1: an entry whose contents do not match its checksum is not read, the archive fails the check, "
                             "and nothing (not even an empty file) is written for it"),
              QStringLiteral("read %1, verify %2 %3, extract %4").arg(read).arg(v).arg(verr).arg(ex));
        // a deflated entry cut short: no partial text
        const QString cut = tmp.path() + QStringLiteral("/cut.zip");
        {
            QByteArray big;
            for (int i = 0; i < 2000; ++i)
                big += QByteArray::number(i * 7919) + ' ';
            ZipWriter w(cut);
            w.addFile(QStringLiteral("big.txt"), big, true);
            w.close();
        }
        QByteArray c = readAll(cut);
        // the local header's compressed size stays; the data in the middle is overwritten with zeros
        const int mid = int(c.indexOf("big.txt")) + 7 + 200;
        for (int i = mid; i < mid + 200 && i < c.size(); ++i)
            c[i] = 0;
        bool cutRead = true;
        const QByteArray cutGot = ZipReader(c).fileData(QStringLiteral("big.txt"), &cutRead);
        check(!cutRead && cutGot.isEmpty(), QStringLiteral("ST-2: a damaged deflated entry gives nothing, not a part of its text"));
        // an intact archive still reads
        bool fine = false;
        ZipWriter w3(tmp.path() + QStringLiteral("/fine.zip"));
        w3.addFile(QStringLiteral("a.txt"), "alpha", true);
        w3.addFile(QStringLiteral("empty.txt"), QByteArray(), true);
        w3.close();
        ZipReader r3(tmp.path() + QStringLiteral("/fine.zip"));
        bool emptyOk = false;
        check(r3.fileData(QStringLiteral("a.txt"), &fine) == "alpha" && fine && r3.fileData(QStringLiteral("empty.txt"), &emptyOk).isEmpty()
                  && emptyOk && ZipWriter::verify(tmp.path() + QStringLiteral("/fine.zip")) >= 0,
              QStringLiteral("(an intact archive reads, an empty entry counts as read)"));
    }

    // ================================================================ ST-3: compound file with a loop
    {
        // header: sector size 512, no FAT sectors in the header's list except sector 1, a DIFAT chain
        // that starts at sector 0 and points to itself, 50,000 DIFAT sectors claimed
        QByteArray f(512 * 4, '\0');
        f.replace(0, 8, QByteArray::fromHex("D0CF11E0A1B11AE1"));
        auto put16 = [&](int at, quint16 v) { qToLittleEndian<quint16>(v, f.data() + at); };
        auto put32 = [&](int at, quint32 v) { qToLittleEndian<quint32>(v, f.data() + at); };
        put16(0x1A, 3);
        put16(0x1C, 0xFFFE);
        put16(0x1E, 9);
        put16(0x20, 6);
        put32(0x2C, 0);           // number of FAT sectors: none given (no limit)
        put32(0x30, 0xFFFFFFFE);  // directory: none
        put32(0x38, 4096);
        put32(0x3C, 0xFFFFFFFE);
        put32(0x44, 0);           // first DIFAT sector: 0
        put32(0x48, 50000);       // DIFAT sectors: 50,000
        for (int i = 0; i < 109; ++i)
            put32(0x4C + i * 4, 0xFFFFFFFF);
        put32(0x4C, 1);
        // sector 0 (at 512): DIFAT, every entry names sector 1, the last one sector 0 again
        for (int i = 0; i < 127; ++i)
            put32(512 + i * 4, 1);
        put32(512 + 127 * 4, 0);
        // sector 1 (at 1024): the FAT
        for (int i = 0; i < 128; ++i)
            put32(1024 + i * 4, 0xFFFFFFFF);
        QElapsedTimer t;
        t.start();
        qbdoc::CompoundFile cf;
        cf.open(f);
        const qint64 ms = t.elapsed();
        check(ms < 2000, QStringLiteral("ST-3: a Word 97 file whose sector chain loops is read in a moment (before: GB of tables)"),
              QStringLiteral("%1 ms").arg(ms));
    }

    // ================================================================ S-4: XML nested without end
    {
        QByteArray deep;
        for (int i = 0; i < 20000; ++i)
            deep += "<a>";
        for (int i = 0; i < 20000; ++i)
            deep += "</a>";
        xml::Document doc;
        QString xerr;
        const bool parsed = doc.parse(deep, &xerr);
        xml::Document fine;
        const bool fineParsed = fine.parse("<w:document><w:body><w:p><w:r><w:t>x</w:t></w:r></w:p></w:body></w:document>");
        check(!parsed && !xerr.isEmpty() && fineParsed,
              QStringLiteral("S-4: XML nested 20,000 deep (a Word file) is refused with an error instead of crashing"), xerr);
    }

    // ================================================================ X-2, D-2, D-4: settings of a project
    {
        QTemporaryDir root;
        auto p = templates::createProject(blank, root.path(), QStringLiteral("Settings"), &err);
        p->save(&err);
        {
            QSettings prog;
            prog.setValue(QStringLiteral("app/citationsApp"), QStringLiteral("/usr/bin/zotero"));
            prog.setValue(QStringLiteral("backup/dir"), home.path() + QStringLiteral("/backups"));
            prog.setValue(QStringLiteral("app/street"), QStringLiteral("1 Harbour Lane"));
            prog.setValue(QStringLiteral("app/autosave"), 2);
            prog.sync();
        }
        const QString ini = settingslayer::fileFor(p->settingsDir());
        {
            QSettings proj(ini, QSettings::IniFormat);
            proj.setValue(QStringLiteral("app/citationsApp"), QStringLiteral("/tmp/evil-tool"));
            proj.setValue(QStringLiteral("backup/dir"), QStringLiteral("/tmp/elsewhere"));
            proj.setValue(QStringLiteral("app/street"), QStringLiteral("Somebody Else's Street"));
            proj.setValue(QStringLiteral("app/email"), QStringLiteral("other@example.org"));
            proj.setValue(QStringLiteral("app/autosave"), 9);
            proj.setValue(QStringLiteral("importers/docConverterPath"), QStringLiteral("/tmp/evil-converter"));
            proj.setValue(QStringLiteral("app/deviceName"), QStringLiteral("Their Laptop"));
            proj.sync();
        }
        settingslayer::setProjectFirst(true);
        settingslayer::activate(ini);
        QSettings now;
        check(settingslayer::active() && now.value(QStringLiteral("app/autosave")).toInt() == 9,
              QStringLiteral("(a project's own value of an ordinary option is used)"),
              now.value(QStringLiteral("app/autosave")).toString());
        check(now.value(QStringLiteral("app/citationsApp")).toString() == QLatin1String("/usr/bin/zotero")
                  && now.value(QStringLiteral("backup/dir")).toString() == home.path() + QStringLiteral("/backups")
                  && !now.value(QStringLiteral("importers/docConverterPath")).toString().contains(QLatin1String("evil")),
              QStringLiteral("X-2/D-2: programs to start, folders and converters are never taken from a project's settings"),
              now.value(QStringLiteral("app/citationsApp")).toString() + QLatin1Char(' ') + now.value(QStringLiteral("backup/dir")).toString());
        check(now.value(QStringLiteral("app/street")).toString() == QLatin1String("1 Harbour Lane")
                  && !now.value(QStringLiteral("app/email")).toString().contains(QLatin1String("other@")),
              QStringLiteral("D-4: contact data (street, e-mail ...) is the program's, never a project's"));
        check(!now.value(QStringLiteral("app/deviceName")).toString().contains(QLatin1String("Their")),
              QStringLiteral("D-3: the name of this computer is never taken from a project"));
        check(settingslayer::programOnly(QStringLiteral("app/street")) && settingslayer::programOnly(QStringLiteral("app/citationsApp"))
                  && settingslayer::programOnly(QStringLiteral("somethingNew/key")) && !settingslayer::programOnly(QStringLiteral("app/autosave"))
                  && !settingslayer::programOnly(QStringLiteral("editing/fontSize"))
                  && !settingslayer::programOnly(QStringLiteral("keyboard/shortcuts")) && !settingslayer::programOnly(QStringLiteral("backup/keep"))
                  && settingslayer::programOnly(QStringLiteral("backup/dir")) && !settingslayer::programOnly(QStringLiteral("app/autoQuitMinutes"))
                  && settingslayer::programOnly(QStringLiteral("app/zip")) && settingslayer::programOnly(QStringLiteral("app/phone")),
              QStringLiteral("X-2: a project may hold only the options of a list (a key that is not on it stays the program's)"));
        settingslayer::activate(QString());
        p.reset();

        // an options file from someone else: the places in it are shown first
        {
            QSettings prog;
            prog.setValue(QStringLiteral("app/citationsApp"), QStringLiteral("/usr/bin/zotero"));
            prog.sync();
        }
        AppSettings::instance().load();
        const QString optFile = root.path() + QStringLiteral("/options.qboptions");
        {
            QSettings f(optFile, QSettings::IniFormat);
            f.setValue(QStringLiteral("quillbinderOptions/version"), QStringLiteral(QB_APP_VERSION));
            f.setValue(QStringLiteral("app/citationsApp"), QStringLiteral("/tmp/evil-tool"));
            f.setValue(QStringLiteral("app/autosave"), 11);
            f.sync();
        }
        for (const QMessageBox::StandardButton answer : {QMessageBox::No, QMessageBox::Yes}) {
            QString askText, askName;
            answerBox(400, answer, &askText, &askName);
            QTimer::singleShot(1200, qApp, [] { // "The options were loaded."
                for (QWidget *tl : QApplication::topLevelWidgets())
                    if (auto *b = qobject_cast<QMessageBox *>(tl))
                        if (b->isVisible())
                            b->accept();
            });
            OptionsDialog dlg;
            dlg.loadOptionsFromFile(optFile, &err);
            settle(300);
            QSettings now2;
            const QString cit = now2.value(QStringLiteral("app/citationsApp")).toString();
            if (answer == QMessageBox::No)
                check(askName == QLatin1String("loadOptionsPlaces") && askText.contains(QLatin1String("/tmp/evil-tool"))
                          && cit == QLatin1String("/usr/bin/zotero") && now2.value(QStringLiteral("app/autosave")).toInt() == 11,
                      QStringLiteral("X-2: Load Options from File shows the programs and places in the file and takes them only "
                                     "after a yes (No: the other options are taken, the program to start is not)"),
                      askName + QStringLiteral(": ") + cit);
            else
                check(cit == QLatin1String("/tmp/evil-tool"), QStringLiteral("... Yes: they are taken too"), cit);
        }
        {
            QSettings prog;
            prog.remove(QStringLiteral("app/citationsApp"));
            prog.setValue(QStringLiteral("app/autosave"), 2);
            prog.sync();
        }
        AppSettings::instance().load();
    }

    // ================================================================ X-3: packages with program code
    {
        QTemporaryDir tmp;
        auto makeAddon = [&](const QString &name, bool withCode) {
            const QString dir = tmp.path() + QLatin1Char('/') + name;
            QDir().mkpath(dir + QStringLiteral("/payload/themes"));
            QFile::copy(src + QStringLiteral("/../addons/themes-extra/payload/themes/forest.json"), dir + QStringLiteral("/payload/themes/forest.json"));
            QByteArray m = readAll(src + QStringLiteral("/../addons/themes-extra/manifest.json"));
            m.replace("\"themes-extra\"", ("\"audit-" + name + "\"").toUtf8());
            writeAll(dir + QStringLiteral("/manifest.json"), m);
            if (withCode)
                writeAll(dir + QStringLiteral("/payload/plugins/libsomething.so.1"), QByteArray("\x7f" "ELF fake", 8));
            const QString out = tmp.path() + QLatin1Char('/') + name + QStringLiteral(".qbpkg");
            QProcess::execute(QStringLiteral("python3"), {QStringLiteral("-I"), src + QStringLiteral("/../tools/make-package.py"),
                                                          QStringLiteral("addon"), dir, out});
            return out;
        };
        for (const bool code : {false, true}) {
            const QString pkg = makeAddon(code ? QStringLiteral("code") : QStringLiteral("plain"), code);
            bool warned = false, shown = false;
            QTimer::singleShot(600, qApp, [&] {
                for (QWidget *tl : QApplication::topLevelWidgets())
                    if (auto *d = qobject_cast<QDialog *>(tl))
                        if (d->isVisible() && !qobject_cast<QMessageBox *>(d)) {
                            shown = true;
                            auto *lab = d->findChild<QLabel *>(QStringLiteral("addonCodeWarning"));
                            warned = lab != nullptr;
                            if (code)
                                shot(d, QStringLiteral("audit1-code-warning"));
                            d->reject();
                            return;
                        }
                answerBox(0, QMessageBox::Ok); // an error box (the package could not be read)
            });
            runInstallPackage(nullptr, pkg);
            settle(200);
            check(QFileInfo::exists(pkg) && shown && warned == code,
                  code ? QStringLiteral("X-3: a package with a library (\"libsomething.so.1\", also a versioned one) warns of program code")
                       : QStringLiteral("X-3: a package of themes only shows no such warning"),
                  QStringLiteral("shown %1, warned %2").arg(shown).arg(warned));
        }
    }

    // ================================================================ X-4: links that lead out
    {
        QTemporaryDir tmp;
        const QString script = tmp.path() + QStringLiteral("/run.sh");
        writeAll(script, "#!/bin/sh\necho hi\n");
        const QString elf = tmp.path() + QStringLiteral("/tool");
        writeAll(elf, QByteArray("\x7f" "ELF\x02\x01\x01", 7));
        QFile::setPermissions(elf, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        const QString pdf = tmp.path() + QStringLiteral("/letter.pdf");
        writeAll(pdf, "%PDF-1.4");
        const QString exe = tmp.path() + QStringLiteral("/letter.pdf.exe");
        writeAll(exe, "MZ");
        using links::Decision;
        auto j = [](const QString &h) { return links::judge(links::urlOf(h)); };
        check(j(QStringLiteral("https://example.org/a")) == Decision::Open && j(QStringLiteral("http://example.org")) == Decision::Open
                  && j(QStringLiteral("mailto:writer@example.org")) == Decision::Open && j(QStringLiteral("www.example.org")) == Decision::Open,
              QStringLiteral("X-4: web and mail links open at once"));
        check(j(QUrl::fromLocalFile(script).toString()) == Decision::Refuse && j(QUrl::fromLocalFile(elf).toString()) == Decision::Refuse
                  && j(QUrl::fromLocalFile(exe).toString()) == Decision::Refuse
                  && j(QUrl::fromLocalFile(tmp.path() + QStringLiteral("/Setup.desktop")).toString()) == Decision::Refuse
                  && j(QUrl::fromLocalFile(tmp.path() + QStringLiteral("/x.lnk")).toString()) == Decision::Refuse,
              QStringLiteral("X-4: links to programs, scripts, shortcuts are never opened (also \"letter.pdf.exe\" and an ELF without an ending)"));
        check(j(QUrl::fromLocalFile(pdf).toString()) == Decision::Ask && j(QStringLiteral("smb://server/share/doc.docx")) == Decision::Ask
                  && j(QStringLiteral("file://server/share/doc.docx")) == Decision::Ask && j(QStringLiteral("javascript:alert(1)")) == Decision::Ask
                  && j(QStringLiteral("vscode://file/etc/passwd")) == Decision::Ask && j(tmp.path()) == Decision::Ask
                  && j(QString()) == Decision::Nothing,
              QStringLiteral("X-4: files, folders, network shares and other programs' links are asked about"));
        QList<QUrl> opened;
        links::setOpenerForTests([&opened](const QUrl &u) { opened << u; });
        QString text, name;
        answerBox(300, QMessageBox::Cancel, &text, &name);
        const bool o1 = links::open(nullptr, QUrl::fromLocalFile(pdf).toString());
        check(!o1 && opened.isEmpty() && name == QLatin1String("linkQuestion") && text.contains(pdf),
              QStringLiteral("X-4: a link to a file shows the whole target and is not opened on Cancel"), name + QLatin1Char(' ') + text);
        answerBox(300, QMessageBox::Open, &text, &name);
        const bool o2 = links::open(nullptr, QStringLiteral("smb://server/share/doc.docx"));
        check(o2 && opened.size() == 1 && opened.first().scheme() == QLatin1String("smb") && text.contains(QLatin1String("smb://server/share")),
              QStringLiteral("... and opened on Open"), text);
        opened.clear();
        answerBox(300, QMessageBox::Ok, &text, &name);
        const bool o3 = links::open(nullptr, QUrl::fromLocalFile(script).toString());
        check(!o3 && opened.isEmpty() && name == QLatin1String("linkRefused"), QStringLiteral("X-4: a link to a script is refused with a notice"), name);
        check(links::open(nullptr, QStringLiteral("https://example.org/page")) && opened.size() == 1,
              QStringLiteral("(a web link goes to the opener at once)"));
        opened.clear();
        answerBox(300, QMessageBox::Ok, &text, &name);
        check(!links::openFile(nullptr, script) && opened.isEmpty() && name == QLatin1String("linkRefused")
                  && links::openFile(nullptr, pdf) && opened.size() == 1,
              QStringLiteral("X-4: Open in another program refuses a project's file that is a program; a PDF opens"));
        opened.clear();
        // through the main window: a text's link, the inspector's bookmarks go the same way
        {
            QTemporaryDir root;
            MainWindow w;
            w.show();
            settle(200);
            w.adoptProject(templates::createProject(blank, root.path(), QStringLiteral("Links"), &err), true);
            settle(300);
            EditorPane *pane = w.findChild<EditorPane *>();
            if (pane) {
                answerBox(300, QMessageBox::Ok, &text, &name);
                emit pane->linkActivated(pane, QUrl::fromLocalFile(script).toString());
                settle(100);
                const bool scriptRefused = opened.isEmpty() && name == QLatin1String("linkRefused");
                emit pane->linkActivated(pane, QStringLiteral("https://example.org/from-text"));
                check(scriptRefused && opened.size() == 1 && opened.first().host() == QLatin1String("example.org"),
                      QStringLiteral("X-4: Ctrl+click on a link in a text: a script is refused, a web page opens"));
            } else {
                check(false, QStringLiteral("(an editor pane)"));
            }
            w.closeProject();
            w.close();
            settle(200);
        }
        opened.clear();
        // an imported web page: its links do not open by themselves
        {
            QTemporaryDir root;
            auto p = templates::createProject(blank, root.path(), QStringLiteral("Web"), &err);
            BinderItem *page = p->model()->createItem(p->research(), -1, ItemType::WebPage, QStringLiteral("Page"));
            page->fileExt = QStringLiteral("html");
            writeAll(p->mediaFilePath(page), "<html><body><p><a href=\"file:///tmp/run.sh\">run</a> <a href=\"https://example.org/w\">web</a> "
                                             "<a href=\"#part\">part</a></p><h2 id=\"part\">Part</h2></body></html>");
            MediaView v;
            v.setProject(p.get());
            v.resize(500, 400);
            v.show();
            v.showItem(page);
            settle(200);
            QTextBrowser *tb = nullptr;
            for (QTextBrowser *b : v.findChildren<QTextBrowser *>())
                if (b->isVisible())
                    tb = b;
            check(tb && !tb->openLinks() && !tb->openExternalLinks(),
                  QStringLiteral("X-4: an imported web page does not open its links itself (before: straight to the desktop)"));
            if (tb) {
                answerBox(300, QMessageBox::Ok, &text, &name);
                emit tb->anchorClicked(QUrl(QStringLiteral("file:///tmp/run.sh")));
                settle(100);
                const bool refused = opened.isEmpty() && name == QLatin1String("linkRefused");
                emit tb->anchorClicked(QUrl(QStringLiteral("https://example.org/w")));
                emit tb->anchorClicked(QUrl(QStringLiteral("#part")));
                settle(100);
                check(refused && opened.size() == 1 && opened.first().path() == QLatin1String("/w"),
                      QStringLiteral("X-4: in a web page, a link to a script is refused, a web link opens, a link within the page stays there"));
            }
            v.close();
        }
        links::setOpenerForTests({});
    }

    // ================================================================ X-9: the socket folder in /tmp
    {
        QTemporaryDir tmp;
        const QString open = tmp.path() + QStringLiteral("/open");
        QDir().mkpath(open);
        ::chmod(QFile::encodeName(open).constData(), 0777);
        const bool closed = toolhost::Host::linkDirSafe(open);
        struct stat st;
        ::lstat(QFile::encodeName(open).constData(), &st);
        check(closed && (st.st_mode & 077) == 0, QStringLiteral("X-9: a socket folder of this user that others could enter is closed to them first"));
        const QString link = tmp.path() + QStringLiteral("/link");
        QFile::link(open, link);
        const QString file = tmp.path() + QStringLiteral("/file");
        writeAll(file, "x");
        check(!toolhost::Host::linkDirSafe(link) && !toolhost::Host::linkDirSafe(file)
                  && !toolhost::Host::linkDirSafe(tmp.path() + QStringLiteral("/none")),
              QStringLiteral("X-9: a symbolic link, a file or nothing is not used as the socket folder"));
        check(getuid() == 0 || !toolhost::Host::linkDirSafe(QStringLiteral("/")),
              QStringLiteral("X-9: a folder of another user is not used (\"/\" belongs to root)"));
        const QString dir = toolhost::Host::linkDir();
        check(!dir.isEmpty() && toolhost::Host::linkDirSafe(dir), QStringLiteral("(the folder the program uses is a safe one)"), dir);
    }

    // ================================================================ D-3: the name of this computer
    {
        QTemporaryDir tmp;
        const QString host = syncsafe::deviceName();
        syncsafe::setDeviceName(QStringLiteral("Laptop"));
        const QString lock = tmp.path() + QStringLiteral("/lock");
        syncsafe::writeLock(lock, QDateTime::currentDateTimeUtc());
        const QString text = QString::fromUtf8(readAll(lock));
        const syncsafe::LockInfo l = syncsafe::readLock(lock);
        check(syncsafe::deviceName() == QLatin1String("Laptop") && text.contains(QLatin1String("device=Laptop\n"))
                  && text.contains(QLatin1String("\nuser=\n")) && l.user.isEmpty() && l.key == syncsafe::machineKey()
                  && !syncsafe::machineKey().isEmpty() && !text.contains(host + QLatin1Char('\n')),
              QStringLiteral("D-3: with a name of the writer's own, the lock names \"Laptop\", leaves the user name out and carries "
                             "a key (a hash) for this computer"),
              text);
        // another computer the writer also called "Laptop": told apart by its key
        writeAll(lock, QStringLiteral("Laptop 4242\ndevice=Laptop\nuser=\ninstance=other\nkey=0123456789abcdef\nalive=%1\n")
                           .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate))
                           .toUtf8());
        const syncsafe::LockState other = syncsafe::judge(syncsafe::readLock(lock), 900, [](qint64) { return false; });
        writeAll(lock, QStringLiteral("Old name 4242\ndevice=Old%20name\nuser=\ninstance=other\nkey=%1\nalive=%2\n")
                           .arg(syncsafe::machineKey(), QDateTime::currentDateTimeUtc().toString(Qt::ISODate))
                           .toUtf8());
        const syncsafe::LockState same = syncsafe::judge(syncsafe::readLock(lock), 900, [](qint64) { return false; });
        check(other == syncsafe::LockState::OtherDevice && same == syncsafe::LockState::LeftOverHere,
              QStringLiteral("D-3: two computers with the same name are told apart by the key; this computer under an older "
                             "name is still this computer"));
        syncsafe::setDeviceName(QString());
        check(syncsafe::deviceName() == host && !syncsafe::userName().isEmpty() == !qEnvironmentVariable("USER").isEmpty(),
              QStringLiteral("D-3: without a name of its own, the computer's name and the user name as before"));
        // the option
        OptionsDialog dlg;
        auto *field = dlg.findChild<QLineEdit *>(QStringLiteral("optDeviceName"));
        if (field) {
            field->setText(QStringLiteral("  Study  computer "));
            if (QWidget *page = field->parentWidget()) {
                page->resize(760, 420);
                shot(page, QStringLiteral("audit1-device-name"));
            }
            dlg.apply();
        }
        QSettings s;
        check(field && AppSettings::instance().deviceName == QLatin1String("Study computer")
                  && s.value(QStringLiteral("app/deviceName")).toString() == QLatin1String("Study computer")
                  && syncsafe::deviceName() == QLatin1String("Study computer"),
              QStringLiteral("D-3: File > Options > General > Saving sets the name (kept in the program's settings, used at once)"),
              field ? AppSettings::instance().deviceName + QLatin1String(" / ") + syncsafe::deviceName() : QStringLiteral("no field"));
        if (field) {
            field->setText(QString());
            dlg.apply();
        }
    }

    // ================================================================ C-4: Scrivener's lock before every write
    {
        QTemporaryDir sw;
        auto sp = templates::createProject(blank, sw.path(), QStringLiteral("Shared"), &err);
        textDoc(sp.get(), sp->draft(), QStringLiteral("Alpha"), QStringLiteral("Alpha text."));
        sp->save(&err);
        const QString scriv = sw.path() + QStringLiteral("/Shared.scriv");
        auto wc = scrivener::convertToScrivener(sp.get(), scriv, &err);
        check(wc != nullptr, QStringLiteral("(saved in Scrivener format)"), err);
        wc.reset();
        sp.reset();
        const QString uuid = scrivUuid(scriv, QStringLiteral("Alpha"));
        const QString rtf = scriv + QStringLiteral("/Files/Data/") + uuid + QStringLiteral("/content.rtf");
        const QString userLock = scriv + QStringLiteral("/Files/user.lock");
        MainWindow w;
        w.show();
        settle(200);
        check(w.openScrivenerProject(scriv), QStringLiteral("(the .scriv opens)"));
        settle(500);
        Project *p = w.project();
        BinderItem *alpha = p ? byTitle(p, QStringLiteral("Alpha")) : nullptr;
        if (p && alpha) {
            // Scrivener opens the project meanwhile
            writeAll(userLock, "user=ann\nhost=mac-mini\n");
            QTextCursor c(p->docs()->text(alpha->id));
            c.movePosition(QTextCursor::End);
            c.insertText(QStringLiteral(" Written while Scrivener had it."));
            const QByteArray before = readAll(rtf);
            QString e2;
            const bool saved = p->save(&e2);
            check(!saved && e2.contains(QLatin1String("Scrivener has opened")) && e2.contains(QLatin1String("ann on mac-mini"))
                      && readAll(rtf) == before && !p->scrivenerLockRefused.isEmpty(),
                  QStringLiteral("C-4: Scrivener opened the .scriv after this program: nothing is written into it, with an error "
                                 "that says why (before: both programs wrote)"),
                  e2);
            const QString wcText = QString::fromUtf8(readAll(p->contentDir(alpha->id) + QStringLiteral("/text.rtf")))
                                   + QString::fromUtf8(readAll(p->contentDir(alpha->id) + QStringLiteral("/text.md")));
            check(wcText.contains(QLatin1String("Written while Scrivener")), QStringLiteral("... the working copy has the change"));
            // the autosave reports it: one notice, then the status bar
            c.insertText(QStringLiteral(" More."));
            p->autosave(&e2);
            emit p->saveFailed(e2);
            settle(300);
            emit p->saveFailed(e2);
            settle(300);
            check(visibleBoxes(QStringLiteral("scrivenerLockNotice")) == 1, QStringLiteral("C-4: the writer is told once (not on every autosave)"),
                  QString::number(visibleBoxes(QStringLiteral("scrivenerLockNotice"))));
            if (QMessageBox *n = visibleBox(QStringLiteral("scrivenerLockNotice"))) {
                shot(n, QStringLiteral("audit1-scrivener-lock"));
                n->accept();
            }
            // Scrivener closes it again
            QFile::remove(userLock);
            const bool saved2 = p->save(&e2);
            check(saved2 && QString::fromUtf8(readAll(rtf)).contains(QLatin1String("Written while Scrivener")) && p->scrivenerLockRefused.isEmpty(),
                  QStringLiteral("C-4: once Scrivener has closed the project, the next save writes everything into it"), e2);
        } else {
            check(false, QStringLiteral("(Alpha in the working copy)"));
        }
        w.closeProject();
        settle(300);
        // a lock that was there when the project was opened here (the writer chose Open Anyway) does not stop writing
        writeAll(userLock, "user=ann\nhost=mac-mini\n");
        answerBox(400, QMessageBox::Yes); // "Scrivener has this project open ... Open it anyway?"
        check(w.openScrivenerProject(scriv), QStringLiteral("(opened anyway)"));
        settle(500);
        p = w.project();
        alpha = p ? byTitle(p, QStringLiteral("Alpha")) : nullptr;
        if (p && alpha) {
            QTextCursor c(p->docs()->text(alpha->id));
            c.movePosition(QTextCursor::End);
            c.insertText(QStringLiteral(" Chosen."));
            QString e3;
            check(p->save(&e3) && QString::fromUtf8(readAll(rtf)).contains(QLatin1String("Chosen")),
                  QStringLiteral("C-4: a lock the writer saw when opening (Open Anyway) does not stop the saves"), e3);
        }
        QFile::remove(userLock);
        // the .scriv's text changed elsewhere, and the other version cannot be kept: not replaced
        if (p && alpha) {
            const QString conflicts = p->settingsDir() + QStringLiteral("/conflicts");
            const QString snaps = p->snapshotsDir(alpha->id);
            QDir(conflicts).removeRecursively();
            QDir(snaps).removeRecursively();
            writeAll(conflicts, "a file where the folder should be");
            writeAll(snaps, "a file where the folder should be");
            QTest::qWait(1100);
            const QByteArray theirs = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Times;}}\\f0 Alpha as the laptop wrote it.\\par}";
            writeAll(rtf, theirs);
            QTextCursor c(p->docs()->text(alpha->id));
            c.movePosition(QTextCursor::End);
            c.insertText(QStringLiteral(" Ours."));
            QString e4;
            const bool saved = p->save(&e4);
            check(!saved && readAll(rtf) == theirs && e4.contains(QLatin1String("could not be kept")),
                  QStringLiteral("item 10: a .scriv text changed elsewhere whose version cannot be kept is not replaced (the save "
                                 "fails and says why)"),
                  e4.left(400));
            QFile::remove(conflicts);
            QFile::remove(snaps);
            QString e5;
            const bool saved2 = p->save(&e5);
            const QStringList kept = QDir(conflicts).entryList(QDir::Files);
            check(saved2 && QString::fromUtf8(readAll(rtf)).contains(QLatin1String("Ours")) && !kept.isEmpty(),
                  QStringLiteral("... once it can be kept, it is kept and ours is written"), e5 + kept.join(QLatin1Char(',')));
            if (QMessageBox *n = visibleBox(QStringLiteral("conflictNotice")))
                n->accept();
        }
        w.closeProject();
        settle(300);
    }

    // ================================================================ a full disk or a failing drive while saving
    {
        QTemporaryDir root;
        auto p = templates::createProject(blank, root.path(), QStringLiteral("Disk"), &err);
        BinderItem *k = textDoc(p.get(), p->draft(), QStringLiteral("Kept"), QStringLiteral("Old text."));
        p->markIndexDirty();
        p->save(&err);
        const QString folder = p->folderPath();
        const QString dir = p->contentDir(k->id);
        p.reset();
        auto textOf = [&] {
            const QByteArray md = readAll(dir + QStringLiteral("/text.md"));
            return md.isEmpty() ? readAll(dir + QStringLiteral("/text.rtf")) : md;
        };
        const QByteArray before = textOf();
        auto child = [&](const QString &mode, const QString &what, QString *out) {
            QProcess pr;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
            if (!mode.isEmpty()) {
                env.insert(QStringLiteral("LD_PRELOAD"), QStringLiteral(QB_FAULT_SHIM));
                env.insert(QStringLiteral("QB_SHIM_DIR"), mode == QLatin1String("backup") ? root.path() + QStringLiteral("/backups") : dir);
                env.insert(QStringLiteral("QB_SHIM_MODE"), mode == QLatin1String("backup") ? QStringLiteral("write") : mode);
            }
            pr.setProcessEnvironment(env);
            pr.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child"), folder, what});
            pr.waitForFinished(120000);
            *out = QString::fromUtf8(pr.readAllStandardOutput() + pr.readAllStandardError()).trimmed();
            return pr.exitStatus() == QProcess::NormalExit ? pr.exitCode() : -1;
        };
        auto leftovers = [&] {
            QStringList l = QDir(dir).entryList(QDir::Files);
            l.removeAll(QStringLiteral("text.md"));
            l.removeAll(QStringLiteral("text.rtf"));
            return l;
        };
        QString out;
        int rc = child(QStringLiteral("write"), QStringLiteral("save"), &out);
        check(rc == 1 && out.contains(QLatin1String("result=0")) && textOf() == before && leftovers().isEmpty(),
              QStringLiteral("a full disk at the last write of a text: the save fails and the text file stays as it was (Qt's "
                             "commit() reported success and left a cut-off file)"),
              QStringLiteral("rc %1: %2; file now: %3; left: %4").arg(rc).arg(out.right(300), QString::fromUtf8(textOf().left(200)),
                                                                          leftovers().join(QLatin1Char(','))));
        rc = child(QStringLiteral("fsync"), QStringLiteral("save"), &out);
        check(rc == 1 && out.contains(QLatin1String("result=0")) && textOf() == before,
              QStringLiteral("a drive that fails to write the text to the disk (fsync, EIO): the save fails, the old text stays"),
              QStringLiteral("rc %1: %2").arg(rc).arg(out.right(300)));
        rc = child(QStringLiteral("close"), QStringLiteral("save"), &out);
        check(rc == 1 && out.contains(QLatin1String("result=0")),
              QStringLiteral("an I/O error when the text file is closed (some network file systems report write errors only "
                             "then): the save reports failure, so it is saved again"),
              QStringLiteral("rc %1: %2").arg(rc).arg(out.right(300)));
        rc = child(QStringLiteral("close"), QStringLiteral("savenotes"), &out);
        check(rc == 1 && out.contains(QLatin1String("result=0")),
              QStringLiteral("... the same for the notes of a document"), QStringLiteral("rc %1: %2").arg(rc).arg(out.right(300)));
        rc = child(QStringLiteral("backup"), QStringLiteral("backup:") + root.path() + QStringLiteral("/backups"), &out);
        const QStringList zips = QDir(root.path() + QStringLiteral("/backups")).entryList({QStringLiteral("*.zip")}, QDir::Files);
        check(rc == 1 && out.contains(QLatin1String("result=0")) && zips.isEmpty(),
              QStringLiteral("a backup on a full disk fails and leaves no backup that looks finished"),
              QStringLiteral("rc %1: %2; %3").arg(rc).arg(out.right(300), zips.join(QLatin1Char(','))));
        // the save before a backup fails: no backup (it would lack the latest changes)
        rc = child(QStringLiteral("write"), QStringLiteral("backup:") + root.path() + QStringLiteral("/backups2"), &out);
        const QStringList zips2 = QDir(root.path() + QStringLiteral("/backups2")).entryList({QStringLiteral("*.zip")}, QDir::Files);
        check(rc == 1 && out.contains(QLatin1String("no backup was made")) && zips2.isEmpty(),
              QStringLiteral("when the save before a backup fails, no backup is made and the error says so (before: a backup "
                             "without the latest changes, reported as made)"),
              QStringLiteral("rc %1: %2").arg(rc).arg(out.right(300)));
        rc = child(QString(), QStringLiteral("save"), &out);
        check(rc == 0 && QString::fromUtf8(textOf()).contains(QLatin1String("New words.")),
              QStringLiteral("(the same save without the fault library succeeds)"), out.right(300));
    }

    // ================================================================ X-10: no test switches in a release build
    {
        const QStringList files = qEnvironmentVariable("QB_RELEASE_BINARY").split(QLatin1Char(':'), Qt::SkipEmptyParts);
        if (files.isEmpty()) {
            info(QStringLiteral("(QB_RELEASE_BINARY not set: the release build was not searched for test switches)"));
        } else {
            for (const QString &f : files) {
                const QByteArray b = readAll(f);
                QStringList found;
                // the name as a string of the code ("\0NAME\0"), not inside a text such as the developer guide
                for (const char *sw : {"QB_PLUGIN_PATH", "QB_SPEECH_SOURCE", "QB_SPEECH_ACCEPT_CHANGED", "QB_SPEECH_SKIP_TEST"})
                    if (b.contains(QByteArray(1, '\0') + sw + QByteArray(1, '\0')))
                        found << QLatin1String(sw);
                check(!b.isEmpty() && found.isEmpty(),
                      QStringLiteral("X-10: %1 (built for release) has none of the test switches").arg(QFileInfo(f).fileName()),
                      b.isEmpty() ? QStringLiteral("not readable") : found.join(QLatin1String(", ")));
            }
        }
    }

    settle(200);
    std::printf("%d failure(s)\n", failures);
    return failures;
}
