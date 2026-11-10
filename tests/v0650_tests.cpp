// Scripted run for version 0.65.0, the fixes of the external audit of 2026-11-10:
//  - safe copies (a failed copy leaves the old file), the save journal (F19): a save lands as a whole, an
//    interrupted one is finished or dropped at the opening, a journal may name files inside the project only,
//    the list of documents by title;
//  - pictures that cannot be written fail the save (F04); a read cut short counts as unreadable (F23);
//  - Roll Back with a snapshot that cannot be read or is empty (F02), also from the inspector;
//  - backups with missing files: named incomplete, no complete one deleted for them, told at opening (F03);
//  - Duplicate refuses a copy that is not complete (F06); object pictures kept until their save (F09);
//  - Scrivener format: documents made elsewhere kept with their binder, files changed elsewhere kept (F10, F05);
//  - command exporters: exit status, the old export kept, the window drawn meanwhile (F11, F24);
//  - the lock that cannot be written is told (F13); a change of the same size and time is noticed (F14);
//  - changing the storage format all or nothing (F15); large archives read (F17); bundles all or nothing (F18);
//  - signatures of packages (F12): Ed25519, the release key, signed, changed, unsigned, other key, bundles;
//  - the scratch pad keeps a note it cannot save (F01); the end by a signal writes an emergency copy (F07);
//  - closing after a failed save: Keep Open is the default (F26); the manual's note on encryption (F21).
// Saves screenshots into the folder given as first argument. Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_v0650_tests /tmp/shots
#include "compile/ExporterApi.h"
#include "compile/PluginManager.h"
#include "compile/Progress.h"
#include "core/Addons.h"
#include "core/Backup.h"
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/CrashGuard.h"
#include "core/DocumentStore.h"
#include "core/Objects.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "core/SafeSave.h"
#include "core/SaveBatch.h"
#include "core/Signing.h"
#include "core/Snapshots.h"
#include "core/Zip.h"
#include "import/ScrivenerFormat.h"
#include "ui/AddonDialogs.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include "AppInfo.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QStatusBar>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QTimer>
#include <QToolButton>

#include <cstdio>
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

static void closeBoxesLater(int ms)
{
    QTimer::singleShot(ms, qApp, [] {
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *b = qobject_cast<QMessageBox *>(tl); b && b->isVisible())
                b->accept();
    });
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

static QByteArray shaHex(const QByteArray &d) { return QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex(); }

// a folder replaced by a plain file of the same name (the session's root ignores permissions); undo puts it back
struct Block {
    QString path, away;
    explicit Block(const QString &dir) : path(dir), away(dir + QStringLiteral(".away"))
    {
        QDir().rename(path, away);
        writeAll(path, "blocked");
    }
    ~Block() { undo(); }
    void undo()
    {
        if (away.isEmpty())
            return;
        QFile::remove(path);
        QDir().rename(away, path);
        away.clear();
    }
};

static int stagedFilesIn(const QString &dir)
{
    int n = 0;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext())
        n += savebatch::isStaged(it.next()) ? 1 : 0;
    return n;
}

static BinderItem *textDoc(Project *p, BinderItem *parent, const QString &title, const QString &text)
{
    BinderItem *it = p->model()->createItem(parent, -1, ItemType::Text, title);
    QTextCursor c(p->docs()->text(it->id));
    c.insertText(text);
    return it;
}

static void setText(Project *p, BinderItem *it, const QString &text)
{
    QTextDocument *d = p->docs()->text(it->id);
    QTextCursor c(d);
    c.select(QTextCursor::Document);
    c.insertText(text);
}

static QString run(const QString &program, const QStringList &args, const QProcessEnvironment &env = QProcessEnvironment::systemEnvironment(),
                   int *rc = nullptr)
{
    QProcess p;
    p.setProcessEnvironment(env);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    p.waitForFinished(120000);
    if (rc)
        *rc = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    return QString::fromLocal8Bit(p.readAll());
}

// a journal as a save writes it before it puts its files in place
static void writeJournal(const QString &folder, const QList<QJsonObject> &files)
{
    QJsonArray a;
    for (const QJsonObject &o : files)
        a.append(o);
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("files"), a);
    writeAll(folder + QStringLiteral("/settings/journal.json"), QJsonDocument(root).toJson());
}

static QJsonObject journalEntry(const QString &folder, const QString &rel, const QByteArray &newData, const QString &title)
{
    QJsonObject o;
    o.insert(QStringLiteral("file"), rel);
    o.insert(QStringLiteral("staged"), rel + QLatin1String(savebatch::kStageSuffix));
    o.insert(QStringLiteral("sha256"), QString::fromLatin1(shaHex(newData)));
    const QByteArray before = readAll(QDir(folder).filePath(rel));
    o.insert(QStringLiteral("replaces"), QFile::exists(QDir(folder).filePath(rel)) ? QString::fromLatin1(shaHex(before)) : QString());
    o.insert(QStringLiteral("title"), title);
    writeAll(QDir(folder).filePath(rel) + QLatin1String(savebatch::kStageSuffix), newData);
    return o;
}

static int picturesIn(const QTextDocument *d)
{
    int n = 0;
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it)
            if (it.fragment().charFormat().isImageFormat())
                n += int(it.fragment().text().count(QChar::ObjectReplacementCharacter));
    return n;
}

int main(int argc, char **argv)
{
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
    const QString buildDir = QStringLiteral(QB_TEST_BUILD_DIR);

    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.65.0")) >= 0,
          QStringLiteral("version 0.65.0 or newer"), QStringLiteral(QB_APP_VERSION_SHOWN));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    QString err;
    templates::Info blank;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("blank"))
            blank = t;
    QTemporaryDir work;
    const QString W = work.path();

    // ================================================================ safe copies
    {
        writeAll(W + QStringLiteral("/a.txt"), "new content");
        writeAll(W + QStringLiteral("/b.txt"), "old");
        const bool over = safesave::copyFile(W + QStringLiteral("/a.txt"), W + QStringLiteral("/b.txt"));
        const bool missing = !safesave::copyFile(W + QStringLiteral("/gone.txt"), W + QStringLiteral("/b.txt"));
        writeAll(W + QStringLiteral("/plain"), "a file, not a folder");
        const bool blocked = !safesave::copyFile(W + QStringLiteral("/a.txt"), W + QStringLiteral("/plain/b.txt"));
        // a file that gives other bytes than its size (as a file of a sync folder that is not all there)
        QFile::link(QStringLiteral("/proc/self/status"), W + QStringLiteral("/short.txt"));
        writeAll(W + QStringLiteral("/c.txt"), "kept");
        const bool shortRead = !safesave::copyFile(W + QStringLiteral("/short.txt"), W + QStringLiteral("/c.txt"));
        check(over && readAll(W + QStringLiteral("/b.txt")) == "new content" && missing && blocked && shortRead
                  && readAll(W + QStringLiteral("/c.txt")) == "kept",
              QStringLiteral("safe copy: replaces the target whole; a missing source, a folder that cannot be written and a "
                             "short read leave the target as it was (audit F05, F06, F11, F25)"));
    }

    // ================================================================ the save journal: batch, recovery
    {
        const QString f = W + QStringLiteral("/batch");
        writeAll(f + QStringLiteral("/one.txt"), "one old");
        writeAll(f + QStringLiteral("/two.txt"), "two old");
        {
            savebatch::Batch b(f, f + QStringLiteral("/settings/journal.json"));
            QString e;
            bool after = false;
            b.stage(f + QStringLiteral("/one.txt"), "one new", QStringLiteral("One"), QString(), &e);
            b.stage(f + QStringLiteral("/two.txt"), "two new", QStringLiteral("Two"), QString(), &e);
            b.after([&after] { after = true; });
            const bool mid = readAll(f + QStringLiteral("/one.txt")) == "one old" && stagedFilesIn(f) == 2;
            const bool done = b.commit(&e);
            check(mid && done && after && readAll(f + QStringLiteral("/one.txt")) == "one new" && readAll(f + QStringLiteral("/two.txt")) == "two new"
                      && stagedFilesIn(f) == 0 && !QFile::exists(f + QStringLiteral("/settings/journal.json")),
                  QStringLiteral("a save of two files: both written beside their places first, then put in place together; no "
                                 "journal or temporary file is left (audit F19)"),
                  e);
        }
        {
            savebatch::Batch b(f, f + QStringLiteral("/settings/journal.json"));
            QString e;
            b.stage(f + QStringLiteral("/one.txt"), "one dropped", QString(), QString(), &e);
        }
        check(readAll(f + QStringLiteral("/one.txt")) == "one new" && stagedFilesIn(f) == 0,
              QStringLiteral("a save that is dropped changes nothing and leaves no temporary file"));
        // an interrupted save: the journal written, the files not yet in place
        writeAll(f + QStringLiteral("/three.txt"), "three old");
        writeJournal(f, {journalEntry(f, QStringLiteral("one.txt"), "one journal", QStringLiteral("One")),
                         journalEntry(f, QStringLiteral("two.txt"), "two journal", QStringLiteral("Two")),
                         journalEntry(f, QStringLiteral("three.txt"), "three journal", QStringLiteral("Three"))});
        writeAll(f + QStringLiteral("/two.txt"), "two changed elsewhere");  // changed after the save began
        QFile::remove(f + QStringLiteral("/three.txt.qbstage"));             // not complete
        {
            // a journal from outside naming a file outside the project folder
            QJsonObject evil;
            evil.insert(QStringLiteral("file"), QStringLiteral("../outside.txt"));
            evil.insert(QStringLiteral("staged"), QStringLiteral("../outside.txt.qbstage"));
            evil.insert(QStringLiteral("sha256"), QString::fromLatin1(shaHex("evil")));
            QJsonObject root = QJsonDocument::fromJson(readAll(f + QStringLiteral("/settings/journal.json"))).object();
            QJsonArray a = root.value(QStringLiteral("files")).toArray();
            a.append(evil);
            root.insert(QStringLiteral("files"), a);
            writeAll(f + QStringLiteral("/settings/journal.json"), QJsonDocument(root).toJson());
            writeAll(W + QStringLiteral("/outside.txt"), "outside");
            writeAll(W + QStringLiteral("/outside.txt.qbstage"), "evil");
        }
        writeAll(f + QStringLiteral("/stray.txt.qbstage"), "a save without a journal");
        const savebatch::Recovery r = savebatch::recover(f, f + QStringLiteral("/settings/journal.json"), f + QStringLiteral("/settings"));
        const QStringList kept = QDir(f + QStringLiteral("/settings/conflicts")).entryList(QDir::Files);
        check(readAll(f + QStringLiteral("/one.txt")) == "one journal" && r.finished.size() == 1 && r.finished.value(0).contains(QLatin1String("One")),
              QStringLiteral("an interrupted save is finished at the next opening: a file still as before is put in place"),
              r.text());
        check(readAll(f + QStringLiteral("/two.txt")) == "two changed elsewhere" && r.kept.size() == 1 && kept.size() == 1
                  && readAll(f + QStringLiteral("/settings/conflicts/") + kept.value(0)) == "two journal",
              QStringLiteral("a file changed elsewhere meanwhile stays; the interrupted save's version is kept in settings/conflicts"),
              kept.join(QLatin1String(", ")));
        check(r.lost.size() == 1 && readAll(f + QStringLiteral("/three.txt")) == "three old",
              QStringLiteral("a file whose temporary copy is not there stays as before and is named in the notice"), r.text());
        check(readAll(W + QStringLiteral("/outside.txt")) == "outside" && r.dropped == 1 && stagedFilesIn(f) == 0
                  && !QFile::exists(f + QStringLiteral("/settings/journal.json")),
              QStringLiteral("a journal names files inside the project only; a temporary file without a journal is removed"),
              QString::number(r.dropped));
    }

    // ================================================================ the project's save as a whole (F19)
    QString projectFolder;
    {
        auto p = templates::createProject(blank, W, QStringLiteral("Journal"), &err);
        check(p != nullptr, QStringLiteral("(a project)"), err);
        if (!p)
            return 1;
        projectFolder = p->folderPath();
        BinderItem *a = textDoc(p.get(), p->draft(), QStringLiteral("Chapter Alpha"), QStringLiteral("Alpha first."));
        BinderItem *b = textDoc(p.get(), p->draft(), QStringLiteral("Chapter Beta"), QStringLiteral("Beta first."));
        check(p->save(&err), QStringLiteral("(saved)"), err);
        const QByteArray aBefore = readAll(p->docs()->textPath(a->id));
        setText(p.get(), a, QStringLiteral("Alpha second."));
        setText(p.get(), b, QStringLiteral("Beta second."));
        {
            Block blk(p->contentDir(b->id));
            QString e;
            const bool saved = p->save(&e);
            check(!saved && readAll(p->docs()->textPath(a->id)) == aBefore && p->hasUnsavedChanges() && stagedFilesIn(projectFolder) == 0,
                  QStringLiteral("a save whose one file cannot be written writes none of its files; the changes stay unsaved "
                                 "(before, the other texts were written alone)"),
                  e);
        }
        const bool saved = p->save(&err);
        check(saved && readAll(p->docs()->textPath(a->id)).contains("Alpha second") && readAll(p->docs()->textPath(b->id)).contains("Beta second")
                  && !QFile::exists(p->journalFile()),
              QStringLiteral("the next save writes both, and leaves no journal"), err);
        const QString list = QString::fromUtf8(readAll(p->titleListFile()));
        check(list.contains(QLatin1String("Chapter Alpha")) && list.contains(QStringLiteral("content/%1/text.md").arg(a->id))
                  && list.contains(QLatin1String("Draft")),
              QStringLiteral("contents-by-title.txt lists each document with its title, place and files"), list.left(400));
        // an interrupted save found at the opening
        const QString aRel = QDir(projectFolder).relativeFilePath(p->docs()->textPath(a->id));
        const QString idxRel = QDir(projectFolder).relativeFilePath(p->indexPath());
        const QByteArray idxNow = readAll(p->indexPath());
        p.reset();
        writeJournal(projectFolder, {journalEntry(projectFolder, aRel, "Alpha from the interrupted save.\n", QStringLiteral("Chapter Alpha")),
                                     journalEntry(projectFolder, idxRel, idxNow, QStringLiteral("project file"))});
        auto again = Project::open(projectFolder, &err);
        QString shown;
        for (BinderItem *x : again ? again->allItems() : QList<BinderItem *>())
            if (x->title == QLatin1String("Chapter Alpha"))
                shown = again->docs()->text(x->id)->toPlainText();
        check(again && shown.contains(QLatin1String("interrupted save")) && again->saveRecoveryText().contains(QLatin1String("Chapter Alpha")),
              QStringLiteral("opening a project finishes a save that was cut off and says which files"),
              again ? again->saveRecoveryText() : err);
    }

    // ================================================================ pictures (F04), partial reads (F23)
    {
        auto p = templates::createProject(blank, W, QStringLiteral("Pictures"), &err);
        BinderItem *d = textDoc(p.get(), p->draft(), QStringLiteral("With a picture"), QStringLiteral("Before the picture."));
        p->save(&err);
        const QString textFile = p->docs()->textPath(d->id);
        const QByteArray before = readAll(textFile);
        QImage img(40, 30, QImage::Format_RGB32);
        img.fill(QColor(20, 120, 200));
        QTextDocument *doc = p->docs()->text(d->id);
        QDir().mkpath(p->contentDir(d->id));
        QString e;
        bool saved;
        {
            // a picture pasted while its folder cannot be written (the paste as the editor does it)
            writeAll(p->contentDir(d->id) + QStringLiteral("/images"), "a file where the pictures' folder goes");
            const QString rel = p->docs()->importImage(d->id, img);
            QTextCursor c(doc);
            c.movePosition(QTextCursor::End);
            c.insertText(QStringLiteral(" After: "));
            QTextImageFormat f;
            f.setName(rel);
            c.insertImage(f);
            saved = p->save(&e);
            QFile::remove(p->contentDir(d->id) + QStringLiteral("/images"));
        }
        check(!saved && readAll(textFile) == before && e.contains(QLatin1String("picture")),
              QStringLiteral("a picture that cannot be written fails the save: the text is not written pointing at a file "
                             "that is not there (audit F04)"),
              QStringLiteral("saved=%1 same=%2 error=%3 text=%4").arg(saved).arg(readAll(textFile) == before).arg(e, QString::fromUtf8(readAll(textFile)).left(300)));
        saved = p->save(&e);
        const QStringList pngs = QDir(p->contentDir(d->id) + QStringLiteral("/images")).entryList({QStringLiteral("*.png")});
        const QString folder = p->folderPath();
        const QString id = d->id;
        p.reset();
        auto again = Project::open(folder, &e);
        check(saved && pngs.size() == 1 && again && picturesIn(again->docs()->text(id)) == 1,
              QStringLiteral("the next save writes the picture, and it is there when the project opens again"),
              QStringLiteral("saved=%1 pngs=%2 pictures=%3 %4 text=%5").arg(saved).arg(pngs.size()).arg(again ? picturesIn(again->docs()->text(id)) : -1)
                  .arg(e, QString::fromUtf8(readAll(QDir(folder).filePath(QStringLiteral("content/%1/text.md").arg(id)))).left(300)));
        // a read cut short: shown empty, never written over (F23)
        again.reset();
        const QString tf = QDir(folder).filePath(QStringLiteral("content/%1/text.md").arg(id));
        QFile::remove(tf);
        QFile::link(QStringLiteral("/proc/self/status"), tf); // its size says 0, its read gives bytes
        auto third = Project::open(folder, &e);
        bool unreadable = false;
        if (third) {
            third->docs()->text(id);
            unreadable = third->docs()->isUnreadable(id);
            setText(third.get(), third->item(id), QStringLiteral("typed over it"));
            third->save(&e);
        }
        check(unreadable && QFileInfo(tf).isSymLink(),
              QStringLiteral("a text that reads other bytes than its size counts as unreadable and is not saved over (audit F23)"));
    }

    // ================================================================ Roll Back (F02)
    {
        auto p = templates::createProject(blank, W, QStringLiteral("Snapshots"), &err);
        BinderItem *d = textDoc(p.get(), p->draft(), QStringLiteral("Rolled"), QStringLiteral("The old text."));
        p->save(&err);
        snapshots::take(p.get(), d->id, QStringLiteral("Old"));
        QTextDocument empty;
        QTest::qWait(1100);
        snapshots::take(p.get(), d->id, QStringLiteral("Empty"), nullptr, &empty);
        setText(p.get(), d, QStringLiteral("The current text."));
        const QList<Snapshot> snaps = snapshots::list(p.get(), d->id);
        Snapshot oldSnap, emptySnap;
        QStringList seen;
        for (const Snapshot &s : snaps) {
            seen << s.title + QLatin1Char('=') + QFileInfo(s.file).fileName();
            (s.title == QLatin1String("Old") ? oldSnap : emptySnap) = s;
        }
        QString e;
        const bool emptyRefused = !snapshots::rollBack(p.get(), emptySnap, &e);
        const QString afterEmpty = p->docs()->text(d->id)->toPlainText();
        QFile::rename(oldSnap.file, oldSnap.file + QStringLiteral(".gone"));
        const bool goneRefused = !snapshots::rollBack(p.get(), oldSnap, &e);
        check(emptyRefused && goneRefused && afterEmpty == QLatin1String("The current text.")
                  && p->docs()->text(d->id)->toPlainText() == QLatin1String("The current text."),
              QStringLiteral("Roll Back with a snapshot that is gone, or that holds no text, keeps the current text (audit F02)"), e);
        QFile::rename(oldSnap.file + QStringLiteral(".gone"), oldSnap.file);
        check(snapshots::rollBack(p.get(), oldSnap, &e) && p->docs()->text(d->id)->toPlainText() == QLatin1String("The old text."),
              QStringLiteral("Roll Back with a snapshot that can be read works as before"), e);
        e.clear();
        check(snapshots::rollBack(p.get(), emptySnap, &e, true) && p->docs()->text(d->id)->toPlainText().isEmpty(),
              QStringLiteral("an empty snapshot replaces the text only when that was confirmed"), e + QLatin1String(" / ") + seen.join(QLatin1String(", ")));
    }

    // ================================================================ backups (F03)
    {
        auto p = templates::createProject(blank, W, QStringLiteral("Backed"), &err);
        BinderItem *d = textDoc(p.get(), p->draft(), QStringLiteral("Noted"), QStringLiteral("Text."));
        QTextCursor(p->docs()->notes(d->id)).insertText(QStringLiteral("A note that becomes unreadable."));
        p->save(&err);
        const QString dir = W + QStringLiteral("/backups");
        QString out, e;
        int made = 0;
        for (int i = 0; i < 2; ++i) {
            made += backup::backupProject(p.get(), dir, 2, &out, &e) ? 1 : 0;
            QTest::qWait(1100);
        }
        qputenv("QB_TEST_UNREADABLE", "/notes.md");
        e.clear();
        const bool inc = backup::backupProject(p.get(), dir, 2, &out, &e);
        const QString incName = QFileInfo(out).fileName();
        QTest::qWait(1100);
        backup::backupProject(p.get(), dir, 2, &out, &e);
        auto complete = [&] {
            int n = 0;
            for (const QString &f : backup::backupsFor(p->title(), dir))
                n += QFileInfo(f).completeBaseName().contains(QLatin1String("(incomplete")) ? 0 : 1;
            return n;
        };
        check(made == 2 && inc && incName.contains(QLatin1String("(incomplete)")) && e.contains(QLatin1String("incomplete")) && complete() == 2,
              QStringLiteral("a backup with files missing is named incomplete and no complete backup is deleted for it (audit F03)"),
              incName + QLatin1String(" / ") + e);
        qunsetenv("QB_TEST_UNREADABLE");
        QTest::qWait(1100);
        backup::backupProject(p.get(), dir, 2, &out, &e);
        const QStringList all = backup::backupsFor(p->title(), dir);
        int incomplete = 0;
        for (const QString &f : all)
            incomplete += QFileInfo(f).completeBaseName().contains(QLatin1String("(incomplete")) ? 1 : 0;
        QTest::qWait(1100);
        backup::backupProject(p.get(), dir, 2, &out, &e);
        const QStringList all2 = backup::backupsFor(p->title(), dir);
        int incomplete2 = 0;
        for (const QString &f : all2)
            incomplete2 += QFileInfo(f).completeBaseName().contains(QLatin1String("(incomplete")) ? 1 : 0;
        check(complete() == 2 && incomplete == 2 && incomplete2 == 0,
              QStringLiteral("complete backups delete the oldest complete ones; an incomplete one goes once it is older than "
                             "every complete backup kept"),
              all.join(QLatin1String(", ")) + QLatin1String(" / ") + all2.join(QLatin1String(", ")));
    }

    // ================================================================ objects (F09)
    {
        auto p = templates::createProject(blank, W, QStringLiteral("Objects"), &err);
        objects::Store *st = p->objects();
        const QString oid = st->create(QStringLiteral("characters"), QString(), QStringLiteral("Anna"));
        QImage img(20, 20, QImage::Format_RGB32);
        img.fill(Qt::red);
        img.save(W + QStringLiteral("/red.png"));
        img.fill(Qt::green);
        img.save(W + QStringLiteral("/green.png"));
        const QString first = st->importPicture(oid, QStringLiteral("picture"), W + QStringLiteral("/red.png"));
        objects::Object o = *st->object(oid);
        o.values.insert(QStringLiteral("picture"), first);
        st->update(o);
        st->save(&err);
        const QString second = st->importPicture(oid, QStringLiteral("picture"), W + QStringLiteral("/green.png"));
        o = *st->object(oid);
        o.values.insert(QStringLiteral("picture"), second);
        st->update(o);
        bool saved;
        {
            Block blk(p->folderPath() + QStringLiteral("/objects/characters"));
            saved = st->save(&err);
        }
        const bool firstKept = QFile::exists(st->picturePath(first));
        const bool savedNow = st->save(&err);
        check(!first.isEmpty() && !saved && firstKept && savedNow && !QFile::exists(st->picturePath(first)) && QFile::exists(st->picturePath(second)),
              QStringLiteral("an object's old picture is deleted only after the object's file that names the new one is "
                             "written (audit F09)"),
              err);
    }

    // ================================================================ storage format (F15), same size and time (F14)
    {
        auto p = templates::createProject(blank, W, QStringLiteral("Convert"), &err);
        BinderItem *a = textDoc(p.get(), p->draft(), QStringLiteral("One"), QStringLiteral("One text."));
        BinderItem *b = textDoc(p.get(), p->draft(), QStringLiteral("Two"), QStringLiteral("Two text."));
        p->save(&err);
        bool converted;
        {
            Block blk(p->contentDir(b->id));
            converted = p->docs()->convertStorage(StorageFormat::Rtf, &err);
        }
        const bool unchanged = !converted && p->storage == StorageFormat::Markdown
                               && QFile::exists(p->contentDir(a->id) + QStringLiteral("/text.md"))
                               && !QFile::exists(p->contentDir(a->id) + QStringLiteral("/text.rtf")) && stagedFilesIn(p->folderPath()) == 0;
        const bool now = p->docs()->convertStorage(StorageFormat::Rtf, &err);
        check(unchanged && now && QFile::exists(p->contentDir(a->id) + QStringLiteral("/text.rtf"))
                  && !QFile::exists(p->contentDir(a->id) + QStringLiteral("/text.md")),
              QStringLiteral("changing the storage format is all or nothing: a text that cannot be written leaves every text "
                             "and the format as they were (audit F15)"),
              err);
        // F14: the file changed elsewhere with the same size and time
        const QString tf = p->docs()->textPath(a->id);
        QByteArray bytes = readAll(tf);
        const QDateTime mtime = QFileInfo(tf).lastModified();
        const int at = bytes.indexOf("One text");
        bytes.replace(at, 8, "Uno texT");
        {
            QFile f(tf);
            f.open(QIODevice::WriteOnly);
            f.write(bytes);
            f.close();
            f.open(QIODevice::ReadWrite);
            f.setFileTime(mtime, QFileDevice::FileModificationTime);
        }
        setText(p.get(), a, QStringLiteral("One text, changed here."));
        p->save(&err);
        bool keptOther = false;
        for (const QString &c : QDir(p->settingsDir() + QStringLiteral("/conflicts")).entryList(QDir::Files))
            keptOther = keptOther || readAll(p->settingsDir() + QStringLiteral("/conflicts/") + c).contains("Uno texT");
        check(QFileInfo(tf).size() > 0 && keptOther,
              QStringLiteral("a text changed elsewhere with the same size and time is noticed and kept before this one is "
                             "saved (audit F14)"));
    }

    // ================================================================ large archives (F17)
    {
        QByteArray big(70 * 1024 * 1024, Qt::Uninitialized);
        QRandomGenerator rng(65);
        for (qsizetype i = 0; i + 4 <= big.size(); i += 4) {
            const quint32 v = rng.generate();
            memcpy(big.data() + i, &v, 4);
        }
        const QString zp = W + QStringLiteral("/big.zip");
        {
            ZipWriter zw(zp);
            zw.addFile(QStringLiteral("big.bin"), big, false);
            zw.close();
        }
        ZipReader zr(zp);
        bool ok = false;
        const QByteArray back = zr.fileData(QStringLiteral("big.bin"), &ok);
        check(QFileInfo(zp).size() > 64 * 1024 * 1024 && ok && back == big,
              QStringLiteral("an archive above 64 MiB is read by mapping it, not by reading it whole (audit F17)"));
        QFile::remove(zp);
    }

    // ================================================================ command exporters (F11, F24)
    {
        const QString script = W + QStringLiteral("/fake-converter.sh");
        writeAll(script, "#!/bin/sh\n# $1: the input, $2: the output, $3: exit status\nsleep 1.5\necho partial > \"$2\"\nexit \"$3\"\n");
        QFile::setPermissions(script, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        auto exporter = [&](int status) {
            QJsonObject def;
            def.insert(QStringLiteral("id"), QStringLiteral("fake"));
            def.insert(QStringLiteral("extension"), QStringLiteral("txt"));
            def.insert(QStringLiteral("program"), script);
            def.insert(QStringLiteral("timeout"), 30);
            def.insert(QStringLiteral("arguments"), QJsonArray{QStringLiteral("{input}"), QStringLiteral("{output}"), QString::number(status)});
            return compile::CommandExporter(def, QString());
        };
        const QString target = W + QStringLiteral("/export.txt");
        writeAll(target, "the earlier export");
        compile::Manuscript m;
        int ticks = 0;
        QTimer t;
        QObject::connect(&t, &QTimer::timeout, [&ticks] { ++ticks; });
        t.start(100);
        QString e;
        bool failed;
        {
            compile::progress::Scope scope([](double) { QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents); });
            failed = !exporter(3).exportManuscript(m, target, &e);
        }
        t.stop();
        check(failed && readAll(target) == "the earlier export" && e.contains(QLatin1String("exit code 3")),
              QStringLiteral("an exporter program that ends with an error is not a success, and the earlier export stays "
                             "(audit F11)"),
              e);
        check(ticks >= 8, QStringLiteral("while the exporter program runs, the window's event loop keeps running (audit F24)"),
              QString::number(ticks));
        check(exporter(0).exportManuscript(m, target, &e) && readAll(target).startsWith("partial"),
              QStringLiteral("an exporter that ends well replaces the export"), e);
    }

    // ================================================================ bundles (F18), signatures (F12)
    const QString makePkg = src + QStringLiteral("/tools/make-package.py");
    {
        // RFC 8032, test 1 (the empty message)
        const QByteArray pub = QByteArray::fromHex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
        const QByteArray sig = QByteArray::fromHex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
        QByteArray bad = sig;
        bad[10] = char(bad[10] ^ 1);
        check(signing::verify(QByteArray(), sig, pub) && !signing::verify(QByteArray("x"), sig, pub) && !signing::verify(QByteArray(), bad, pub),
              QStringLiteral("Ed25519: the RFC 8032 test signature checks, a changed message or signature does not (audit F12)"));
        const QString pem = QString::fromLatin1(readAll(src + QStringLiteral("/packaging/quillbinder-signing-public.pem")));
        const QByteArray der = QByteArray::fromBase64(pem.section(QLatin1Char('\n'), 1, 1).toLatin1());
        check(der.size() == 44 && !signing::trustedKeys().isEmpty() && signing::trustedKeys().constFirst() == der.right(32),
              QStringLiteral("the program carries the release key of packaging/quillbinder-signing-public.pem"));
    }
    const QString py = QStringLiteral("python3");
    QString keyFile = W + QStringLiteral("/test-key.pem"), otherKey = W + QStringLiteral("/other-key.pem");
    run(QStringLiteral("openssl"), {QStringLiteral("genpkey"), QStringLiteral("-algorithm"), QStringLiteral("ed25519"), QStringLiteral("-out"), keyFile});
    run(QStringLiteral("openssl"), {QStringLiteral("genpkey"), QStringLiteral("-algorithm"), QStringLiteral("ed25519"), QStringLiteral("-out"), otherKey});
    {
        QProcess pk;
        pk.start(QStringLiteral("openssl"), {QStringLiteral("pkey"), QStringLiteral("-in"), keyFile, QStringLiteral("-pubout"), QStringLiteral("-outform"), QStringLiteral("DER")});
        pk.waitForFinished();
        qputenv("QB_TEST_SIGNING_KEY", pk.readAllStandardOutput().right(32).toHex());
    }
    // an add-on folder and a program update whose program does not start
    const QString addonDir = W + QStringLiteral("/addon");
    writeAll(addonDir + QStringLiteral("/manifest.json"),
             R"({"id": "test-theme-pack", "name": "Test Themes", "version": "1.0", "description": "A theme for the checks."})");
    writeAll(addonDir + QStringLiteral("/payload/themes/check.json"), R"({"name": "Check", "base": "light", "colors": {}})");
    const QString fakeBin = W + QStringLiteral("/bin/quillbinder");
    writeAll(fakeBin, "#!/bin/sh\nexit 1\n");
    QFile::setPermissions(fakeBin, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    auto makeAddon = [&](const QString &out, const QString &key) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        if (!key.isEmpty())
            env.insert(QStringLiteral("QB_SIGNING_KEY_FILE"), key);
        int rc = 0;
        const QString o = run(py, {makePkg, QStringLiteral("addon"), addonDir, out}, env, &rc);
        return rc == 0 ? QString() : o;
    };
    auto makeCore = [&](const QString &out, const QString &key) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        if (!key.isEmpty())
            env.insert(QStringLiteral("QB_SIGNING_KEY_FILE"), key);
        int rc = 0;
        const QString o = run(py, {makePkg, QStringLiteral("core"), QStringLiteral("--binary"), fakeBin, QStringLiteral("--version"),
                                   QStringLiteral("9.9.9"), QStringLiteral("--runtime"), QStringLiteral(QB_RUNTIME_ID), out},
                              env, &rc);
        return rc == 0 ? QString() : o;
    };
    {
        const QString signedAddon = W + QStringLiteral("/signed-addon.qbpkg"), plainAddon = W + QStringLiteral("/plain-addon.qbpkg"),
                      otherAddon = W + QStringLiteral("/other-addon.qbpkg"), signedCore = W + QStringLiteral("/signed-core.qbpkg"),
                      plainCore = W + QStringLiteral("/plain-core.qbpkg"), changed = W + QStringLiteral("/changed-addon.qbpkg");
        const QString problems = makeAddon(signedAddon, keyFile) + makeAddon(plainAddon, QString()) + makeAddon(otherAddon, otherKey)
                                 + makeCore(signedCore, keyFile) + makeCore(plainCore, QString());
        check(problems.isEmpty(), QStringLiteral("(packages made, signed and unsigned)"), problems);
        // the description changed after signing
        run(py, {QStringLiteral("-c"),
                 QStringLiteral("import zipfile,sys\nz=zipfile.ZipFile(sys.argv[1]);o=zipfile.ZipFile(sys.argv[2],'w')\n"
                                "for n in z.namelist():\n d=z.read(n)\n if n=='manifest.json': d=d.replace(b'A theme for the checks.',b'A theme, changed later.')\n"
                                " o.writestr(n,d)\no.close()"),
                 signedAddon, changed});
        const addons::PackageInfo s = addons::inspect(signedAddon), u = addons::inspect(plainAddon), x = addons::inspect(otherAddon),
                                  c = addons::inspect(changed), sc = addons::inspect(signedCore), uc = addons::inspect(plainCore);
        check(s.isValid() && s.signature == signing::State::Valid && s.trusted(),
              QStringLiteral("a package signed with a trusted key is recognized as signed (audit F12)"), s.error);
        check(!c.isValid() && c.signature == signing::State::Invalid && c.error.contains(QLatin1String("signature")),
              QStringLiteral("a package whose description was changed after signing is refused"), c.error);
        check(u.isValid() && u.signature == signing::State::Unsigned && !u.trusted() && x.signature == signing::State::UnknownKey && !x.trusted(),
              QStringLiteral("unsigned, and signed with an unknown key: both count as not signed"));
        qputenv("QB_REQUIRE_SIGNED", "1");
        check(addons::trustProblem(u, addons::Unsigned::AllowAddons).isEmpty() && !addons::trustProblem(u, addons::Unsigned::Refuse).isEmpty()
                  && addons::trustProblem(u, addons::Unsigned::AllowAll).isEmpty(),
              QStringLiteral("an unsigned add-on: after the window's warning yes, on the command line only with --allow-unsigned"));
        check(sc.isValid() && sc.trusted() && addons::trustProblem(sc, addons::Unsigned::Refuse).isEmpty() && uc.isValid()
                  && addons::trustProblem(uc, addons::Unsigned::AllowAddons).contains(QLatin1String("program update")),
              QStringLiteral("a program update is installed only with the project's signature"),
              addons::trustProblem(uc, addons::Unsigned::AllowAddons));
        QString e;
        QStringList log;
        bool restart = false;
        const bool refused = !addons::install(plainCore, &log, &e, &restart, addons::Unsigned::AllowAddons);
        qunsetenv("QB_REQUIRE_SIGNED");
        check(refused && e.contains(QLatin1String("not signed")), QStringLiteral("installing an unsigned program update is refused"), e);
        // bundles: as trusted as their parts; installed all or nothing (F18)
        const QString signedBundle = W + QStringLiteral("/signed-bundle.qbpkg"), mixed = W + QStringLiteral("/mixed-bundle.qbpkg"),
                      twoAddons = W + QStringLiteral("/two-addons.qbpkg");
        run(py, {makePkg, QStringLiteral("bundle"), QStringLiteral("--id"), QStringLiteral("check-bundle"), QStringLiteral("--name"),
                 QStringLiteral("Check Bundle"), QStringLiteral("--version"), QStringLiteral("1.0"), signedAddon, signedCore, signedBundle});
        run(py, {makePkg, QStringLiteral("bundle"), QStringLiteral("--id"), QStringLiteral("check-bundle"), QStringLiteral("--name"),
                 QStringLiteral("Check Bundle"), QStringLiteral("--version"), QStringLiteral("1.0"), plainAddon, plainCore, mixed});
        const addons::PackageInfo sb = addons::inspect(signedBundle), mb = addons::inspect(mixed);
        check(sb.isValid() && sb.trusted() && mb.isValid() && !mb.trusted(), QStringLiteral("a bundle is signed when every part is"));
        // a bundle of two add-ons whose second one cannot be written
        const QString addon2 = W + QStringLiteral("/addon2");
        writeAll(addon2 + QStringLiteral("/manifest.json"),
                 R"({"id": "test-other-pack", "name": "Other Themes", "version": "1.0", "description": "The second part."})");
        writeAll(addon2 + QStringLiteral("/payload/themes/other.json"), R"({"name": "Other", "base": "dark", "colors": {}})");
        const QString plain2 = W + QStringLiteral("/plain-addon2.qbpkg");
        run(py, {makePkg, QStringLiteral("addon"), addon2, plain2});
        run(py, {makePkg, QStringLiteral("bundle"), QStringLiteral("--id"), QStringLiteral("two-themes"), QStringLiteral("--name"),
                 QStringLiteral("Two Themes"), QStringLiteral("--version"), QStringLiteral("1.0"), plainAddon, plain2, twoAddons});
        QDir().mkpath(addons::addonsRoot());
        writeAll(addons::addonsRoot() + QStringLiteral("/.new-test-other-pack"), "in the way");
        e.clear();
        const bool bundleFailed = !addons::install(twoAddons, &log, &e, &restart);
        QFile::remove(addons::addonsRoot() + QStringLiteral("/.new-test-other-pack"));
        bool themeThere = false;
        for (const addons::Installed &in : addons::installedAddons())
            themeThere = themeThere || in.manifest.id == QLatin1String("test-theme-pack");
        check(bundleFailed && !themeThere,
              QStringLiteral("a bundle whose second part cannot be installed installs none of its parts (before, the first "
                             "part stayed installed; audit F18)"),
              e);
        // the install window: the signed note, or the warning with Cancel as the default
        auto lookAt = [&](const QString &pkg, QString *seen) {
            QTimer::singleShot(600, qApp, [seen] {
                for (QWidget *tl : QApplication::topLevelWidgets())
                    if (auto *d = qobject_cast<QDialog *>(tl); d && d->isVisible() && !qobject_cast<QMessageBox *>(d)) {
                        if (d->findChild<QLabel *>(QStringLiteral("addonSignedNote")))
                            *seen += QStringLiteral("signed ");
                        if (d->findChild<QLabel *>(QStringLiteral("addonUnsignedWarning")))
                            *seen += QStringLiteral("unsigned ");
                        for (QPushButton *b : d->findChildren<QPushButton *>())
                            if (b->isDefault())
                                *seen += QStringLiteral("default=") + b->text().remove(QLatin1Char('&'));
                        shot(d, QStringLiteral("v0650-install-%1").arg(seen->contains(QLatin1String("unsigned")) ? QStringLiteral("unsigned") : QStringLiteral("signed")));
                        d->reject();
                        return;
                    }
            });
            runInstallPackage(nullptr, pkg);
            settle(200);
        };
        QString seenSigned, seenUnsigned;
        lookAt(signedAddon, &seenSigned);
        lookAt(plainAddon, &seenUnsigned);
        check(seenSigned.startsWith(QLatin1String("signed default=Install")) && seenUnsigned.startsWith(QLatin1String("unsigned default=Cancel")),
              QStringLiteral("the install window says Signed by the Quillbinder project, or warns of a package without the "
                             "signature with Cancel as the default"),
              seenSigned + QLatin1String(" / ") + seenUnsigned);
        check(addons::install(plainAddon, &log, &e, &restart), QStringLiteral("(the add-on alone installs)"), e);
    }

    // ================================================================ the window: scratch pad, Scrivener, notices
    {
        QStringList problems;
        for (const QString &id : {QStringLiteral("tool-scratch-pad")}) {
            const QString pkg = buildDir + QStringLiteral("/addons-dist/") + id + QStringLiteral(".qbpkg");
            QStringList log;
            QString e;
            bool restart = false;
            if (!addons::install(pkg, &log, &e, &restart))
                problems << id + QLatin1String(": ") + e;
        }
        check(problems.isEmpty(), QStringLiteral("(the scratch pad's add-on installs)"), problems.join(QLatin1String("; ")));
    }
    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.backupOnOpen = false;
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();
    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.resize(1300, 900);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);

    // ---------------------------------------------------------------- scratch pad (F01)
    if (QAction *spAction = w.findChild<QAction *>(QStringLiteral("tool:scratch-pad.show"))) {
        spAction->trigger();
        settle(400);
        QWidget *sp = w.findChild<QWidget *>(QStringLiteral("scratchPad"));
        const QString notes = home.path() + QStringLiteral("/Documents/Quillbinder Scratch Pad");
        if (sp) {
            QMetaObject::invokeMethod(sp, "newNote", Q_ARG(bool, false));
            settle(200);
            QMetaObject::invokeMethod(sp, "setNoteText", Q_ARG(QString, QStringLiteral("Saved first.")));
            QString file;
            QMetaObject::invokeMethod(sp, "currentFile", Q_RETURN_ARG(QString, file));
            const bool firstSaved = readAll(file) == "Saved first.";
            QString status, text, after, emergency;
            {
                Block blk(QFileInfo(file).absolutePath());
                QMetaObject::invokeMethod(sp, "setNoteText", Q_ARG(QString, QStringLiteral("Not saved yet: the folder is gone.")));
                settle(600);
                QMetaObject::invokeMethod(sp, "newNote", Q_ARG(bool, false));
                settle(300);
                QMetaObject::invokeMethod(sp, "status", Q_RETURN_ARG(QString, status));
                QMetaObject::invokeMethod(sp, "noteText", Q_RETURN_ARG(QString, text));
                QMetaObject::invokeMethod(sp, "currentFile", Q_RETURN_ARG(QString, after));
                QMetaObject::invokeMethod(sp, "saveEmergencyCopy");
                QMetaObject::invokeMethod(sp, "emergencyFile", Q_RETURN_ARG(QString, emergency));
                shot(sp, QStringLiteral("v0650-scratch-unsaved"));
            }
            check(firstSaved && status.contains(QLatin1String("could not be saved")) && text.contains(QLatin1String("Not saved yet"))
                      && after == file,
                  QStringLiteral("a scratch pad note that cannot be saved stays open with its text; a new note waits until it "
                                 "is saved (audit F01)"),
                  status);
            check(!emergency.isEmpty() && readAll(emergency).contains("Not saved yet"),
                  QStringLiteral("its text goes into the emergency folder when the program ends meanwhile"), emergency);
            bool saved = false;
            QMetaObject::invokeMethod(sp, "save", Q_RETURN_ARG(bool, saved));
            check(saved && readAll(file).contains("Not saved yet"), QStringLiteral("once the folder is back, the note is saved"));
            Q_UNUSED(notes);
            sp->close();
        } else {
            check(false, QStringLiteral("(the scratch pad opens)"));
        }
    } else {
        check(false, QStringLiteral("(the scratch pad's command is there)"));
    }

    // ---------------------------------------------------------------- an interrupted save at the opening, in the window
    {
        auto p = Project::open(projectFolder, &err);
        BinderItem *any = nullptr;
        for (BinderItem *x : p ? p->allItems() : QList<BinderItem *>())
            if (x->title == QLatin1String("Chapter Beta"))
                any = x;
        const QString rel = any ? QDir(projectFolder).relativeFilePath(p->docs()->textPath(any->id)) : QString();
        const QByteArray idx = readAll(p->indexPath());
        const QString idxRel = QDir(projectFolder).relativeFilePath(p->indexPath());
        p.reset();
        writeJournal(projectFolder, {journalEntry(projectFolder, rel, "Beta from the interrupted save.\n", QStringLiteral("Chapter Beta")),
                                     journalEntry(projectFolder, idxRel, idx, QStringLiteral("project file"))});
        as.backupOnOpen = true;
        as.backupDir = W + QStringLiteral("/window-backups");
        as.save();
        qputenv("QB_TEST_UNREADABLE", "/text.md");
        w.openProject(projectFolder);
        settle(400);
        qunsetenv("QB_TEST_UNREADABLE");
        as.backupOnOpen = false;
        as.save();
        check(w.lastSaveRecoveryNotice().contains(QLatin1String("Chapter Beta")),
              QStringLiteral("the window tells that an interrupted save was finished at the opening (audit F19)"), w.lastSaveRecoveryNotice());
        check(w.lastBackupProblem().contains(QLatin1String("incomplete")),
              QStringLiteral("an incomplete backup at the opening is told (it was not even looked at; audit F03)"), w.lastBackupProblem());
    }

    // ---------------------------------------------------------------- Roll Back from the inspector (F02)
    if (Project *p = w.project()) {
        BinderItem *beta = nullptr;
        for (BinderItem *x : p->allItems())
            if (x->title == QLatin1String("Chapter Beta"))
                beta = x;
        snapshots::take(p, beta->id, QStringLiteral("Will be gone"));
        w.findChild<BinderPanel *>()->select({beta}, true);
        settle(300);
        auto *list = w.findChild<QListWidget *>(QStringLiteral("snapshotList"));
        auto *roll = w.findChild<QToolButton *>(QStringLiteral("snapshotRollBack"));
        if (list && roll) {
            list->setCurrentRow(0);
            for (const Snapshot &s : snapshots::list(p, beta->id))
                QFile::remove(s.file);
            const QString before = p->docs()->text(beta->id)->toPlainText();
            QTimer::singleShot(300, qApp, [] {
                if (auto *b = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                    b->button(QMessageBox::No)->click();
            });
            roll->click();
            settle(400);
            QWidget *insp = roll->parentWidget();
            while (insp && !insp->property("qbLastRollBackError").isValid() && insp->parentWidget())
                insp = insp->parentWidget();
            const QString msg = insp ? insp->property("qbLastRollBackError").toString() : QString();
            check(msg.contains(QLatin1String("cannot be read")) && p->docs()->text(beta->id)->toPlainText() == before,
                  QStringLiteral("Roll Back in the inspector with a snapshot that is gone says so and keeps the text"), msg);
        } else {
            check(false, QStringLiteral("(the snapshot list and Roll Back are found)"));
        }
    }

    // ---------------------------------------------------------------- Duplicate (F06)
    if (Project *p = w.project()) {
        BinderItem *alpha = nullptr;
        for (BinderItem *x : p->allItems())
            if (x->title == QLatin1String("Chapter Alpha"))
                alpha = x;
        QFile::link(QStringLiteral("/proc/self/status"), p->contentDir(alpha->id) + QStringLiteral("/attachment.bin"));
        const int count = int(p->allItems().size());
        auto *binder = w.findChild<BinderPanel *>();
        binder->select({alpha}, true);
        w.activateWindow();
        binder->tree()->setFocus();
        settle(200);
        if (QAction *dup = w.findChild<QAction *>(QStringLiteral("duplicate")))
            dup->trigger();
        settle(300);
        check(int(p->allItems().size()) == count && w.statusBar()->currentMessage().contains(QLatin1String("could not be made")),
              QStringLiteral("Duplicate takes a copy that is not complete out again and says so (audit F06)"),
              w.statusBar()->currentMessage());
        QFile::remove(p->contentDir(alpha->id) + QStringLiteral("/attachment.bin"));
        if (QAction *dup = w.findChild<QAction *>(QStringLiteral("duplicate")))
            dup->trigger();
        settle(300);
        check(int(p->allItems().size()) == count + 1, QStringLiteral("Duplicate works as before when every file can be copied"));
    }

    // ---------------------------------------------------------------- closing after a failed save (F26), the end (F07)
    if (Project *p = w.project()) {
        BinderItem *alpha = nullptr;
        for (BinderItem *x : p->allItems())
            if (x->title == QLatin1String("Chapter Alpha"))
                alpha = x;
        setText(p, alpha, QStringLiteral("Alpha, typed before the disk failed."));
        Block blk(p->contentDir(alpha->id));
        QString defaultText;
        QTimer::singleShot(400, qApp, [&defaultText] {
            for (QWidget *tl : QApplication::topLevelWidgets())
                if (auto *b = qobject_cast<QMessageBox *>(tl); b && b->isVisible() && b->objectName() == QLatin1String("closeUnsavedQuestion")) {
                    defaultText = b->defaultButton() ? b->defaultButton()->text() : QString();
                    shot(b, QStringLiteral("v0650-close-unsaved"));
                    b->defaultButton()->click();
                }
        });
        const bool closed = w.closeProject();
        check(!closed && w.project() && defaultText == QLatin1String("Keep Open"),
              QStringLiteral("closing a project that cannot be saved: Keep Open is the default, and it stays open (audit F26)"),
              defaultText);
        w.saveForTermination();
        const QString emergency = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Quillbinder/Emergency");
        bool rtf = false;
        QDirIterator it(emergency, {QStringLiteral("*.rtf")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            rtf = rtf || readAll(it.next()).contains("typed before the disk failed");
        const bool notice = QFile::exists(emergency + QStringLiteral("/notice.txt"));
        w.showEmergencyNotice();
        check(crash::terminationSaveFailed() && rtf && notice && w.lastEmergencyNotice().contains(QLatin1String("could not be saved"))
                  && !QFile::exists(emergency + QStringLiteral("/notice.txt")),
              QStringLiteral("the end by a signal with a save that fails: the texts go into the emergency folder, the next start "
                             "says so, and the program ends with status 1 (audit F07)"),
              w.lastEmergencyNotice());
        blk.undo();
        w.closeProject();
        settle(300);
    }

    // ---------------------------------------------------------------- the lock that cannot be written (F13)
    {
        auto p = templates::createProject(blank, W, QStringLiteral("Locked"), &err);
        const QString folder = p->folderPath();
        p.reset();
        QDir().mkpath(folder + QStringLiteral("/settings/lock"));
        w.openProject(folder);
        settle(300);
        check(w.lastLockProblem().endsWith(QLatin1String("settings/lock")),
              QStringLiteral("a project that cannot be marked as open here is told (audit F13)"), w.lastLockProblem());
        w.closeProject();
        settle(200);
    }

    // ---------------------------------------------------------------- Scrivener format: two computers (F10, F05)
    {
        QTemporaryDir sw;
        auto sp = templates::createProject(blank, sw.path(), QStringLiteral("Twin"), &err);
        textDoc(sp.get(), sp->draft(), QStringLiteral("Alpha"), QStringLiteral("Alpha text."));
        textDoc(sp.get(), sp->draft(), QStringLiteral("Beta"), QStringLiteral("Beta text."));
        BinderItem *pic = sp->model()->createItem(sp->research(), -1, ItemType::Image, QStringLiteral("Map"));
        pic->fileExt = QStringLiteral("png");
        {
            QImage img(16, 16, QImage::Format_RGB32);
            img.fill(Qt::blue);
            QDir().mkpath(QFileInfo(sp->mediaFilePath(pic)).absolutePath());
            img.save(sp->mediaFilePath(pic), "PNG");
        }
        sp->save(&err);
        const QString scriv = sw.path() + QStringLiteral("/Twin.scriv");
        auto wc = scrivener::convertToScrivener(sp.get(), scriv, &err);
        check(wc != nullptr, QStringLiteral("(saved in Scrivener format)"), err);
        wc.reset();
        sp.reset();
        closeBoxesLater(1500);
        w.openScrivenerProject(scriv);
        settle(600);
        Project *wp2 = w.project();
        const QString scrivx = scrivener::scrivxIn(scriv);
        QString x = QString::fromUtf8(readAll(scrivx));
        auto uuidOf = [&](const QString &title) {
            const int t = x.indexOf(QStringLiteral("<Title>%1</Title>").arg(title));
            const int u = x.lastIndexOf(QStringLiteral("UUID=\""), t);
            return t > 0 && u > 0 ? x.mid(u + 6, 36) : QString();
        };
        const QString betaUuid = uuidOf(QStringLiteral("Beta")), mapUuid = uuidOf(QStringLiteral("Map"));
        if (wp2 && !betaUuid.isEmpty() && !mapUuid.isEmpty()) {
            QTest::qWait(1100);
            // the other computer: a new document, and another picture
            const QString gamma = QStringLiteral("0F0E0D0C-0B0A-4909-8807-060504030201");
            const int betaStart = x.lastIndexOf(QStringLiteral("<BinderItem"), x.indexOf(QStringLiteral("UUID=\"%1\"").arg(betaUuid)));
            const int betaEnd = x.indexOf(QStringLiteral("</BinderItem>"), betaStart) + int(qstrlen("</BinderItem>"));
            x.insert(betaEnd, QStringLiteral("\n<BinderItem UUID=\"%1\" Type=\"Text\" Created=\"2026-11-10 10:00:00 +0100\" "
                                             "Modified=\"2026-11-10 10:00:00 +0100\"><Title>Gamma from elsewhere</Title></BinderItem>")
                                  .arg(gamma));
            writeAll(scrivx, x.toUtf8());
            writeAll(scriv + QStringLiteral("/Files/Data/") + gamma + QStringLiteral("/content.rtf"),
                     "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Times;}}\\f0 Gamma written on the other computer.\\par}");
            QImage other(16, 16, QImage::Format_RGB32);
            other.fill(Qt::yellow);
            other.save(scriv + QStringLiteral("/Files/Data/") + mapUuid + QStringLiteral("/content.png"), "PNG");
            const QByteArray theirPicture = readAll(scriv + QStringLiteral("/Files/Data/") + mapUuid + QStringLiteral("/content.png"));
            // here: a change of a text, a new document, a full save
            BinderItem *alphaHere = nullptr;
            for (BinderItem *it : wp2->allItems())
                if (it->title == QLatin1String("Alpha"))
                    alphaHere = it;
            QTextCursor c(wp2->docs()->text(alphaHere->id));
            c.movePosition(QTextCursor::End);
            c.insertText(QStringLiteral(" More alpha."));
            textDoc(wp2, wp2->draft(), QStringLiteral("Delta made here"), QStringLiteral("Delta text."));
            wp2->save(&err);
            const bool gammaKept = QFile::exists(scriv + QStringLiteral("/Files/Data/") + gamma + QStringLiteral("/content.rtf"))
                                   && QString::fromUtf8(readAll(scrivx)).contains(gamma);
            check(gammaKept, QStringLiteral("Scrivener format: a document added in the .scriv elsewhere while it is open here is kept "
                                            "with the binder that names it (before, its folder was deleted at the next full save; audit F10)"));
            check(readAll(scriv + QStringLiteral("/Files/Data/") + mapUuid + QStringLiteral("/content.png")) == theirPicture,
                  QStringLiteral("Scrivener format: a picture changed in the .scriv elsewhere is not overwritten with the old one (audit F05)"));
            w.closeProject();
            settle(300);
            closeBoxesLater(1500);
            w.openScrivenerProject(scriv);
            settle(600);
            QStringList titles;
            QString alphaText;
            if (Project *p3 = w.project())
                for (BinderItem *it : p3->allItems()) {
                    titles << it->title;
                    if (it->title == QLatin1String("Alpha"))
                        alphaText = p3->docs()->text(it->id)->toPlainText();
                }
            check(titles.contains(QStringLiteral("Gamma from elsewhere")) && titles.contains(QStringLiteral("Delta made here"))
                      && alphaText.contains(QLatin1String("More alpha")),
                  QStringLiteral("opened again: the document made elsewhere, the one made here and the change here are all there"),
                  titles.join(QLatin1String(", ")));
            w.closeProject();
            settle(300);
        } else {
            check(false, QStringLiteral("(the Scrivener project opens and its documents are found)"),
                  QStringLiteral("project=%1 beta=%2 map=%3").arg(wp2 != nullptr).arg(betaUuid, mapUuid));
        }
    }

    // ================================================================ the manual (F21)
    {
        const QString manual = QString::fromUtf8(readAll(src + QStringLiteral("/docs/manual.md")));
        check(manual.contains(QLatin1String("not encrypted by")) && manual.contains(QLatin1String("SHA256SUMS.txt.sig")),
              QStringLiteral("the manual says that files are not encrypted and how a download's signature is checked (audit F21, F12)"));
    }

    delete wp;
    std::printf("%d failure(s)\n", failures);
    return failures;
}
