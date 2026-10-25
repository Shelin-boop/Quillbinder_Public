// Version 0.19: the Page Down / Page Up crash, crash reports with a snapshot of the
// program and the system, sessions that ended unexpectedly, saving on SIGTERM, and
// Help > System Information / Crash Reports. Saves screenshots into the folder
// given as first argument. Exit code = number of failed checks.
//
//   QT_QPA_PLATFORM=offscreen ./qb_v019_tests /tmp/shots
//
// The Page Down checks run in a child process of this program (a crash there is a
// failed check, not the end of the run); the crash report checks start the real
// program (<build>/quillbinder) and end it with signals.
#include "AppInfo.h"
#include "compile/PluginManager.h"
#include "core/BinderModel.h"
#include "core/CrashGuard.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/Copyholder.h"
#include "ui/CrashDialogs.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/QuickRef.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"
#include "ui/ToolHost.h"
#include "ui/Windows.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QRegularExpression>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>

#include <csignal>
#include <cstdio>
#include <functional>
#include <sys/resource.h>
#include <unistd.h>

using namespace qb;

static int failures = 0;
static QString outDir;
static QString prefix; // "[Page Down] " in the child
static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s%s  %s\n", qPrintable(prefix), ok ? "PASS" : "FAIL", qPrintable(what));
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
static void shot(QWidget *w, const QString &name)
{
    settle(250);
    w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}
template<typename T>
static void withModal(int delayMs, std::function<void(T *)> fn)
{
    QTimer::singleShot(delayMs, qApp, [fn] {
        if (auto *m = qobject_cast<T *>(QApplication::activeModalWidget())) {
            fn(m);
            return;
        }
        info(QStringLiteral("(the expected window (%1) did not open)").arg(QString::fromLatin1(T::staticMetaObject.className())));
        if (QWidget *m = QApplication::activeModalWidget())
            m->close();
    });
}

static void setupApp(const QString &name)
{
    QCoreApplication::setOrganizationName(name);
    QCoreApplication::setApplicationName(name);
}
static void styleApp()
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();
}

static std::unique_ptr<Project> novelProject(const QString &dir, const QString &title, QString *err)
{
    templates::Info novel;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("novel"))
            novel = t;
    return templates::createProject(novel, dir, title, err);
}

// a text of `paragraphs` paragraphs, each about 90 words, numbered so a place can be told
static void fillText(Project *p, BinderItem *it, int paragraphs, const QString &tag)
{
    QTextCursor c(p->docs()->text(it->id));
    for (int i = 1; i <= paragraphs; ++i) {
        if (i > 1)
            c.insertBlock();
        c.insertText(QStringLiteral("%1 paragraph %2. ").arg(tag).arg(i)
                     + QStringLiteral("The keeper climbed the stairs again and looked out over the water, where the "
                                      "boats lay still in the grey light and nothing moved but the gulls. ")
                           .repeated(3));
    }
}

static TextEditor *editorOf(QWidget *view)
{
    for (TextEditor *e : view->findChildren<TextEditor *>())
        if (e->isVisible())
            return e;
    return nullptr;
}
static TextEditor *focusedEditor() { return qobject_cast<TextEditor *>(QApplication::focusWidget()); }

// =====================================================================================
// child: Page Down / Page Up in every view built on the text column
// =====================================================================================
static int pageDownChild()
{
    prefix = QStringLiteral("[Page Down] ");
    QTemporaryDir work;
    QString err;
    auto proj = novelProject(work.path(), QStringLiteral("Paging"), &err);
    if (!proj) {
        check(false, QStringLiteral("(project made)"), err);
        return 1;
    }
    Project *p = proj.get();
    BinderItem *longDoc = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Long Night"));
    fillText(p, longDoc, 40, QStringLiteral("Long"));
    BinderItem *ch = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Three Scenes"));
    BinderItem *s1 = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("Scene A"));
    BinderItem *s2 = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("Scene B"));
    BinderItem *s3 = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("Scene C"));
    fillText(p, s1, 12, QStringLiteral("SceneA"));
    fillText(p, s2, 12, QStringLiteral("SceneB"));
    fillText(p, s3, 12, QStringLiteral("SceneC"));
    p->save(&err);

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1300, 900);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    w.adoptProject(std::move(proj));
    settle(400);
    auto *binder = w.findChild<BinderPanel *>();
    auto *editorSplit = qobject_cast<QSplitter *>(w.findChild<EditorPane *>()->parentWidget());
    auto *pane = editorSplit ? qobject_cast<EditorPane *>(editorSplit->widget(0)) : nullptr;
    if (!binder || !pane) {
        check(false, QStringLiteral("(window parts found)"));
        return 1;
    }

    // ---- one long document in the main editor
    binder->select({longDoc}, true);
    settle(400);
    TextColumnView *view = pane->textView();
    TextEditor *ed = editorOf(view);
    if (!ed) {
        check(false, QStringLiteral("(editor of the long document)"));
        return 1;
    }
    QApplication::setActiveWindow(&w);
    ed->setFocus();
    QTextCursor c = ed->textCursor();
    c.movePosition(QTextCursor::Start);
    ed->setTextCursor(c);
    settle(200);
    view->verticalScrollBar()->setValue(0);
    settle(100);
    std::printf("%s(pressing Page Down in the main editor; before 0.19 the program ended here)\n", qPrintable(prefix));
    std::fflush(stdout);
    QTest::keyClick(ed, Qt::Key_PageDown);
    settle(300);
    const int pos1 = ed->textCursor().position();
    const int scroll1 = view->verticalScrollBar()->value();
    const int page = view->viewport()->height();
    check(pos1 > 0 && scroll1 > page / 2,
          QStringLiteral("Page Down in the main editor: the text scrolls about one screen and the cursor moves with it"),
          QStringLiteral("cursor %1, scrolled %2 px, screen %3 px").arg(pos1).arg(scroll1).arg(page));
    // the cursor stays on the screen
    const QPoint cur = ed->viewport()->mapTo(view->viewport(), ed->cursorRect().center());
    check(cur.y() >= 0 && cur.y() <= page, QStringLiteral("... the cursor is on the screen after the jump"),
          QStringLiteral("cursor at y = %1 of %2").arg(cur.y()).arg(page));
    shot(&w, QStringLiteral("v019-01-page-down"));
    QTest::keyClick(ed, Qt::Key_PageDown);
    settle(300);
    const int pos2 = ed->textCursor().position();
    const int scroll2 = view->verticalScrollBar()->value();
    check(pos2 > pos1 && scroll2 > scroll1, QStringLiteral("... a second Page Down goes on"),
          QStringLiteral("cursor %1 -> %2, scroll %3 -> %4").arg(pos1).arg(pos2).arg(scroll1).arg(scroll2));
    QTest::keyClick(ed, Qt::Key_PageUp);
    QTest::keyClick(ed, Qt::Key_PageUp);
    settle(300);
    check(ed->textCursor().position() < pos1 && view->verticalScrollBar()->value() < scroll1,
          QStringLiteral("Page Up twice comes back to the first screen"),
          QStringLiteral("cursor %1, scroll %2").arg(ed->textCursor().position()).arg(view->verticalScrollBar()->value()));
    QTest::keyClick(ed, Qt::Key_PageDown, Qt::ShiftModifier);
    settle(200);
    check(true, QStringLiteral("Shift+Page Down (selecting) does not end the program either"));
    // many presses: to the end and back without trouble
    for (int i = 0; i < 40; ++i)
        QTest::keyClick(ed, Qt::Key_PageDown);
    settle(200);
    const bool atEnd = view->verticalScrollBar()->value() == view->verticalScrollBar()->maximum();
    for (int i = 0; i < 40; ++i)
        QTest::keyClick(ed, Qt::Key_PageUp);
    settle(200);
    check(atEnd && view->verticalScrollBar()->value() == 0,
          QStringLiteral("40 times Page Down reaches the end, 40 times Page Up the start"),
          QStringLiteral("at end %1, scroll back %2").arg(atEnd).arg(view->verticalScrollBar()->value()));

    // ---- Combined text: Page Down goes on into the next document
    binder->select({ch}, true);
    settle(300);
    pane->setViewMode(ViewMode::Combined);
    settle(500);
    TextEditor *first = nullptr;
    for (TextEditor *e : view->findChildren<TextEditor *>())
        if (e->isVisible() && e->item() == s1)
            first = e;
    if (first) {
        QApplication::setActiveWindow(&w);
        first->setFocus();
        QTextCursor fc = first->textCursor();
        fc.movePosition(QTextCursor::Start);
        first->setTextCursor(fc);
        settle(200);
        QSet<QString> visited;
        for (int i = 0; i < 30; ++i) {
            if (TextEditor *f = focusedEditor())
                if (f->item())
                    visited.insert(f->item()->title);
            if (!QApplication::focusWidget())
                break;
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_PageDown);
            settle(60);
        }
        if (TextEditor *f = focusedEditor())
            if (f->item())
                visited.insert(f->item()->title);
        shot(&w, QStringLiteral("v019-02-page-down-combined-text"));
        check(visited.contains(QStringLiteral("Scene A")) && visited.contains(QStringLiteral("Scene B"))
                  && visited.contains(QStringLiteral("Scene C")),
              QStringLiteral("Combined text: Page Down goes from document to document"),
              QStringList(visited.values()).join(QLatin1String(", ")));
        for (int i = 0; i < 30; ++i) {
            if (!QApplication::focusWidget())
                break;
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_PageUp);
            settle(40);
        }
        TextEditor *f = focusedEditor();
        check(f && f->item() == s1, QStringLiteral("... and Page Up back to the first one"),
              f && f->item() ? f->item()->title : QStringLiteral("(no editor has the keyboard)"));
    } else {
        check(false, QStringLiteral("Combined text: Page Down goes from document to document"), QStringLiteral("(no editor for Scene A)"));
    }

    // ---- copyholder, Quick Reference panel, composition mode
    pane->setViewMode(ViewMode::Single);
    binder->select({s1}, true);
    settle(300);
    pane->openCopyholder(longDoc);
    settle(400);
    if (Copyholder *h = pane->copyholder()) {
        TextEditor *he = editorOf(h->textView());
        if (he) {
            QApplication::setActiveWindow(&w);
            he->setFocus();
            QTest::keyClick(he, Qt::Key_PageDown);
            settle(250);
            check(h->textView()->verticalScrollBar()->value() > 0, QStringLiteral("Page Down in a copyholder scrolls it"));
            shot(&w, QStringLiteral("v019-03-page-down-copyholder"));
        } else {
            check(false, QStringLiteral("Page Down in a copyholder scrolls it"), QStringLiteral("(no editor)"));
        }
    }
    binder->select({longDoc}, true);
    settle(300);
    w.findChild<QAction *>(QStringLiteral("quickRef"))->trigger();
    settle(500);
    QuickRefWindow *qr = nullptr;
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (auto *q = qobject_cast<QuickRefWindow *>(tl); q && q->isVisible())
            qr = q;
    if (qr) {
        qr->resize(600, 500);
        settle(400);
        TextEditor *qe = editorOf(qr->textView());
        if (qe) {
            qr->activateWindow();
            QApplication::setActiveWindow(qr);
            qe->setFocus();
            QTest::keyClick(qe, Qt::Key_PageDown);
            settle(250);
            check(qr->textView()->verticalScrollBar()->value() > 0, QStringLiteral("Page Down in a Quick Reference panel scrolls it"));
            qr->grab().save(outDir + QLatin1String("/v019-04-page-down-quickref.png"));
        } else {
            check(false, QStringLiteral("Page Down in a Quick Reference panel scrolls it"), QStringLiteral("(no editor)"));
        }
        qr->close();
        settle(200);
    }
    binder->select({longDoc}, true);
    settle(300);
    w.findChild<QAction *>(QStringLiteral("composition"))->trigger();
    settle(800);
    CompositionWindow *comp = nullptr;
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (auto *cw = qobject_cast<CompositionWindow *>(tl); cw && cw->isVisible())
            comp = cw;
    if (comp) {
        TextEditor *ce = editorOf(comp->view());
        if (ce) {
            QApplication::setActiveWindow(comp);
            ce->setFocus();
            QTextCursor cc = ce->textCursor();
            cc.movePosition(QTextCursor::Start);
            ce->setTextCursor(cc);
            settle(150);
            QTest::keyClick(ce, Qt::Key_PageDown);
            settle(250);
            check(ce->textCursor().position() > 0, QStringLiteral("Page Down in composition mode moves on"));
            comp->grab().save(outDir + QLatin1String("/v019-05-page-down-composition.png"));
        }
        comp->close();
        settle(300);
    } else {
        check(false, QStringLiteral("Page Down in composition mode moves on"), QStringLiteral("(composition mode did not open)"));
    }
    check(w.isVisible(), QStringLiteral("the window is still there after all of it"));
    std::printf("%s%d failure(s)\n", qPrintable(prefix), failures);
    return failures;
}

// =====================================================================================
// the real program, ended with signals
// =====================================================================================
struct Env {
    QString home, data, config, runtime, work;
};

static QProcess *startProgram(const Env &env, const QStringList &args)
{
    auto *proc = new QProcess;
    QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
    pe.insert(QStringLiteral("HOME"), env.home);
    pe.insert(QStringLiteral("XDG_DATA_HOME"), env.data);
    pe.insert(QStringLiteral("XDG_CONFIG_HOME"), env.config);
    pe.insert(QStringLiteral("XDG_RUNTIME_DIR"), env.runtime);
    pe.insert(QStringLiteral("QB_NO_DIALOGS"), QStringLiteral("1"));
    pe.remove(QStringLiteral("APPIMAGE"));
    proc->setProcessEnvironment(pe);
    proc->setWorkingDirectory(env.work);
    // no core file of several hundred MB for each crash of the test
    proc->setChildProcessModifier([] {
        struct rlimit rl = {0, 0};
        ::setrlimit(RLIMIT_CORE, &rl);
    });
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    proc->start(QStringLiteral(QB_TEST_BUILD_DIR "/quillbinder"), args);
    proc->waitForStarted(10000);
    return proc;
}

// waits until the program has its session marker and (with a project) the project's lock file
static bool waitUntilUp(QProcess *proc, const Env &env, const QString &lockFile, int ms = 30000)
{
    QElapsedTimer t;
    t.start();
    const QString marker = env.data + QStringLiteral("/Quillbinder/sessions/%1.json").arg(proc->processId());
    while (t.elapsed() < ms) {
        if (QFile::exists(marker) && (lockFile.isEmpty() || QFile::exists(lockFile))) {
            settle(1500); // the window is up and the event loop runs
            return true;
        }
        if (proc->state() == QProcess::NotRunning)
            return false;
        settle(100);
    }
    return false;
}

// keeps the main thread busy without answering events (a freeze for the watchdog)
[[gnu::noinline]] static void busyMainThread(int ms)
{
    QElapsedTimer t;
    t.start();
    volatile double x = 0;
    while (t.elapsed() < ms)
        x = x + 1;
}

static QString readAll(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

int main(int argc, char **argv)
{
    const bool child = argc > 1 && QByteArray(argv[1]) == "--child-pagedown";
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    setupApp(QStringLiteral("QuillbinderV019Test"));
    QApplication app(argc, argv);
    outDir = argc > (child ? 2 : 1) ? QString::fromLocal8Bit(argv[child ? 2 : 1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    styleApp();
    if (child)
        return pageDownChild();

    // ============================================================ Page Down (child process)
    {
        QProcess pd;
        pd.setProcessChannelMode(QProcess::MergedChannels);
        pd.setChildProcessModifier([] {
            struct rlimit rl = {0, 0};
            ::setrlimit(RLIMIT_CORE, &rl);
        });
        pd.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child-pagedown"), outDir});
        pd.waitForFinished(300000);
        const QString out = QString::fromLocal8Bit(pd.readAll());
        for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            if (line.startsWith(QLatin1String("[Page Down]")) || line.startsWith(QLatin1String("      ")))
                std::printf("%s\n", qPrintable(line));
        std::fflush(stdout);
        check(pd.exitStatus() == QProcess::NormalExit,
              QStringLiteral("Page Down / Page Up: the program keeps running (before 0.19 it ended at the first press)"),
              pd.exitStatus() == QProcess::CrashExit ? QStringLiteral("the child process ended with a crash") : QString());
        if (pd.exitStatus() == QProcess::NormalExit && pd.exitCode() != 0) {
            failures += pd.exitCode();
            info(QStringLiteral("(%1 check(s) of the child failed, counted here)").arg(pd.exitCode()));
        }
    }

    // ============================================================ the real program
    QTemporaryDir progHome;
    Env env;
    env.home = progHome.path();
    env.data = env.home + QLatin1String("/.local/share");
    env.config = env.home + QLatin1String("/.config");
    env.runtime = env.home + QLatin1String("/run");
    env.work = env.home + QLatin1String("/work");
    for (const QString &d : {env.data, env.config, env.runtime, env.work})
        QDir().mkpath(d);
    QFile::setPermissions(env.runtime, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    {
        // settings of the program: no backup on close, autosave only after 300 s (so that
        // only the termination signal can have saved what the test changes)
        QSettings ps(env.config + QLatin1String("/Quillbinder/Quillbinder.conf"), QSettings::IniFormat);
        ps.setValue(QStringLiteral("app/autosave"), 300);
        ps.setValue(QStringLiteral("backup/onClose"), false);
        ps.setValue(QStringLiteral("app/portal"), false);
        ps.sync();
    }
    QString err;
    QString projectFile;
    {
        auto pr = novelProject(env.work, QStringLiteral("Signals"), &err);
        check(pr != nullptr, QStringLiteral("(project for the program made)"), err);
        if (!pr)
            return failures;
        BinderItem *it = pr->model()->createItem(pr->draft(), -1, ItemType::Text, QStringLiteral("The Watch"));
        fillText(pr.get(), it, 3, QStringLiteral("Watch"));
        pr->save(&err);
        projectFile = pr->indexPath();
    }
    const QString lockFile = QFileInfo(projectFile).absolutePath() + QLatin1String("/settings/lock");
    const QString reports = env.data + QLatin1String("/Quillbinder/CrashReports");
    const QString sessions = env.data + QLatin1String("/Quillbinder/sessions");

    // ---- SIGSEGV
    QString segvReport;
    qint64 segvPid = 0;
    {
        QProcess *proc = startProgram(env, {projectFile});
        const bool up = waitUntilUp(proc, env, lockFile);
        check(up, QStringLiteral("(the program is up with the project)"), QString::fromLocal8Bit(proc->readAllStandardError()));
        segvPid = proc->processId();
        ::kill(pid_t(segvPid), SIGSEGV);
        proc->waitForFinished(20000);
        const QString err2 = QString::fromLocal8Bit(proc->readAllStandardError());
        segvReport = QDir(reports).filePath(QDir(reports).entryList({QStringLiteral("crash-*-%1.txt").arg(segvPid)}).value(0));
        const QString r = readAll(segvReport);
        check(proc->exitStatus() == QProcess::CrashExit && QFileInfo(segvReport).isFile(),
              QStringLiteral("a crash (SIGSEGV) writes a report into ~/.local/share/Quillbinder/CrashReports, then the program ends as before"),
              QStringLiteral("exit status %1, files: %2").arg(int(proc->exitStatus())).arg(QDir(reports).entryList(QDir::Files).join(QLatin1String(", "))));
        check(err2.contains(QLatin1String("A crash report was written to")), QStringLiteral("... and says so on the terminal"), err2.right(400));
        check(r.contains(QLatin1String("Signal: 11 SIGSEGV")) && r.contains(QLatin1String("Version: " QB_APP_VERSION))
                  && r.contains(QStringLiteral("Process: %1").arg(segvPid)) && r.contains(QLatin1String("Unix time: ")),
              QStringLiteral("... the report names the signal, the version, the process and the time"), r.left(600));
        check(r.contains(QLatin1String("== Program and system")) && r.contains(QLatin1String("Operating system:"))
                  && r.contains(QLatin1String("Qt: ")) && r.contains(QLatin1String("Memory: ")) && r.contains(QLatin1String("Signals"))
                  && r.contains(QLatin1String("Save automatically after 300 s")) && r.contains(QLatin1String("Program data:")),
              QStringLiteral("... the snapshot of program and system: operating system, Qt, memory, folders, settings, the open project"),
              r.section(QLatin1String("== Program and system"), 1, 1).left(1500));
        check(r.contains(QLatin1String("== Process at the crash")) && r.contains(QLatin1String("VmRSS")),
              QStringLiteral("... the memory of the process at the crash"));
        check(r.contains(QLatin1String("project opened: Signals")), QStringLiteral("... the recent actions (the project that was opened)"),
              r.section(QLatin1String("== Recent actions"), 1, 1).left(800));
        const QString bt = r.section(QLatin1String("== Backtrace"), 1, 1).section(QLatin1String("== "), 0, 0);
        check(bt.contains(QLatin1String("quillbinder(")) && bt.contains(QLatin1String("_ZN")),
              QStringLiteral("... a backtrace with the program's function names"), bt.left(1500));
        check(r.contains(QLatin1String("== Program files in memory")) && r.contains(QLatin1String(" r-xp ")) && r.contains(QLatin1String("== End")),
              QStringLiteral("... the program files in memory (to translate addresses later), and the report is complete"));
        check(!r.contains(QLatin1String("The keeper climbed")), QStringLiteral("... no text of the documents in the report"));
        const QString readable = crash::readable(r);
        check(readable.contains(QLatin1String("QEventLoop::exec(")) && !readable.contains(QLatin1String("_ZN10QEventLoop4exec")),
              QStringLiteral("crash::readable turns the names into readable ones (QEventLoop::exec(...) instead of _ZN10QEventLoop4exec...)"),
              readable.section(QLatin1String("== Backtrace"), 1, 1).left(1200));
        check(QFile::exists(lockFile) && QFile::exists(QDir(sessions).filePath(QStringLiteral("%1.json").arg(segvPid))),
              QStringLiteral("... the lock and the session marker stay (they tell the next start)"));
        delete proc;
    }

    // ---- what the next start sees after the crash
    {
        const QByteArray keep = qgetenv("XDG_DATA_HOME");
        qputenv("XDG_DATA_HOME", env.data.toUtf8());
        const QList<crash::PreviousEnd> ends = crash::beginSession();
        crash::endSession();
        qputenv("XDG_DATA_HOME", keep);
        const crash::PreviousEnd seg = ends.value(0);
        check(ends.size() == 1 && seg.pid == segvPid && seg.report == segvReport && seg.project.endsWith(QLatin1String(".qbx"))
                  && seg.started.isValid() && seg.crashed.isValid(),
              QStringLiteral("next start: the crashed session is known, with its report and its project"),
              QStringLiteral("%1 earlier session(s); pid %2, report %3, project %4").arg(ends.size()).arg(seg.pid).arg(seg.report, seg.project));
    }

    // ---- SIGABRT
    {
        QFile::remove(lockFile);
        QProcess *proc = startProgram(env, {projectFile});
        waitUntilUp(proc, env, lockFile);
        const qint64 pid = proc->processId();
        ::kill(pid_t(pid), SIGABRT);
        proc->waitForFinished(20000);
        const QString r = readAll(QDir(reports).filePath(QDir(reports).entryList({QStringLiteral("crash-*-%1.txt").arg(pid)}).value(0)));
        check(r.contains(QLatin1String("Signal: 6 SIGABRT")) && r.contains(QLatin1String("== End")),
              QStringLiteral("an abort (SIGABRT) writes a report as well"), r.left(300));
        delete proc;
    }

    // ---- SIGKILL: no report possible, but the next start knows
    qint64 killedPid = 0;
    {
        QFile::remove(lockFile);
        QProcess *proc = startProgram(env, {projectFile});
        waitUntilUp(proc, env, lockFile);
        killedPid = proc->processId();
        ::kill(pid_t(killedPid), SIGKILL);
        proc->waitForFinished(20000);
        delete proc;
    }

    // ---- what the next start sees after the kill
    {
        const QByteArray keep = qgetenv("XDG_DATA_HOME");
        qputenv("XDG_DATA_HOME", env.data.toUtf8());
        const QList<crash::PreviousEnd> ends = crash::beginSession();
        crash::endSession();
        qputenv("XDG_DATA_HOME", keep);
        check(ends.size() == 1 && ends.value(0).pid == killedPid && ends.value(0).report.isEmpty(),
              QStringLiteral("... a killed session (SIGKILL, as a power cut) is known as well, without a report"),
              QStringLiteral("%1 earlier session(s), pid %2 (killed %3)").arg(ends.size()).arg(ends.value(0).pid).arg(killedPid));
        check(QDir(env.data + QLatin1String("/Quillbinder/sessions")).entryList({QStringLiteral("*.json")}).isEmpty(),
              QStringLiteral("... and the markers are gone, so the notice comes once"));
    }

    // ---- SIGTERM: what was not saved yet is saved, then a normal end
    {
        QFile::remove(lockFile);
        QProcess *proc = startProgram(env, {projectFile});
        const bool up = waitUntilUp(proc, env, lockFile);
        const qint64 pid = proc->processId();
        // a new document through the instance link (the scratch pad's way); autosave waits 300 s
        QFile note(env.work + QLatin1String("/note.txt"));
        note.open(QIODevice::WriteOnly);
        note.write("A note sent to the project just before the computer shuts down.");
        note.close();
        const QString sock = env.runtime + QStringLiteral("/quillbinder/%1.sock").arg(pid);
        const QJsonObject reply = toolhost::Host::query(
            sock, QJsonObject{{QStringLiteral("cmd"), QStringLiteral("send")},
                              {QStringLiteral("request"), QJsonObject{{QStringLiteral("file"), note.fileName()},
                                                                      {QStringLiteral("title"), QStringLiteral("Shutdown Note")},
                                                                      {QStringLiteral("mode"), QStringLiteral("import")}}}},
            10000);
        settle(500);
        const bool notSavedYet = !readAll(projectFile).contains(QLatin1String("Shutdown Note"));
        check(up && reply.value(QStringLiteral("ok")).toBool() && notSavedYet,
              QStringLiteral("(a document added to the running program, not saved yet)"),
              QStringLiteral("reply %1, saved already %2").arg(QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact))).arg(!notSavedYet));
        ::kill(pid_t(pid), SIGTERM);
        const bool ended = proc->waitForFinished(20000);
        check(ended && proc->exitStatus() == QProcess::NormalExit && proc->exitCode() == 0,
              QStringLiteral("SIGTERM (log out, shutdown, kill): the program ends normally"),
              QStringLiteral("exit status %1, code %2").arg(int(proc->exitStatus())).arg(proc->exitCode()));
        check(readAll(projectFile).contains(QLatin1String("Shutdown Note")),
              QStringLiteral("... after saving what was not saved yet (the new document is in the project file)"));
        check(!QFile::exists(lockFile) && !QFile::exists(QDir(sessions).filePath(QStringLiteral("%1.json").arg(pid)))
                  && QDir(reports).entryList({QStringLiteral("crash-*-%1.txt").arg(pid)}).isEmpty()
                  && !QFile::exists(sock),
              QStringLiteral("... without a crash report, and the lock, the session marker and the link are removed"));
        delete proc;
    }

    // ---- the self-test of a program update is not affected
    {
        QProcess st;
        QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
        pe.insert(QStringLiteral("HOME"), env.home);
        pe.insert(QStringLiteral("XDG_DATA_HOME"), env.data);
        pe.insert(QStringLiteral("XDG_CONFIG_HOME"), env.config);
        pe.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        st.setProcessEnvironment(pe);
        st.start(QStringLiteral(QB_TEST_BUILD_DIR "/quillbinder"), {QStringLiteral("--selftest")});
        st.waitForFinished(60000);
        check(st.exitCode() == 0 && QString::fromLocal8Bit(st.readAllStandardOutput()).contains(QLatin1String("selftest ok")),
              QStringLiteral("--selftest still reports ok (and leaves no session marker)"));
    }

    // ============================================================ the window: Help menu and dialogs
    // (this process shows the reports of the program run above)
    qputenv("XDG_DATA_HOME", env.data.toUtf8());
    QTemporaryDir work;
    auto proj = novelProject(work.path(), QStringLiteral("Lighthouse"), &err);
    BinderItem *doc = proj->model()->createItem(proj->draft(), -1, ItemType::Text, QStringLiteral("Fog"));
    fillText(proj.get(), doc, 3, QStringLiteral("Fog"));
    sysinfo::installActivityRecorder();
    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1300, 900);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    w.adoptProject(std::move(proj));
    settle(400);
    QMenu *help = nullptr;
    for (QAction *a : w.menuBar()->actions())
        if (a->menu() && a->text().remove(QLatin1Char('&')) == QLatin1String("Help"))
            help = a->menu();
    QStringList entries;
    if (help)
        for (QAction *a : help->actions())
            if (!a->isSeparator())
                entries << a->text().remove(QLatin1Char('&'));
    check(entries.contains(QStringLiteral("System Information…")) && entries.contains(QStringLiteral("Crash Reports…"))
              && entries.indexOf(QStringLiteral("Crash Reports…")) < entries.indexOf(QStringLiteral("About Quillbinder")),
          QStringLiteral("Help menu: System Information… and Crash Reports… (before About)"), entries.join(QLatin1String(" | ")));

    // ---- System Information
    {
        QString text;
        bool copied = false;
        withModal<SystemInfoDialog>(400, [&](SystemInfoDialog *d) {
            text = d->text();
            if (auto *b = d->findChild<QPushButton *>(QStringLiteral("copySystemInfo")))
                b->click();
            copied = QApplication::clipboard()->text() == text;
            d->grab().save(outDir + QLatin1String("/v019-10-system-information.png"));
            d->reject();
        });
        w.findChild<QAction *>(QStringLiteral("systemInfo"))->trigger();
        settle(200);
        check(text.contains(QLatin1String("Quillbinder " QB_APP_VERSION)) && text.contains(QLatin1String("Runtime: " QB_RUNTIME_ID))
                  && text.contains(QLatin1String("Operating system: ")) && text.contains(QLatin1String("Screen "))
                  && text.contains(QLatin1String("Processor: ")) && text.contains(QLatin1String("Memory: "))
                  && text.contains(QLatin1String("Lighthouse")) && text.contains(QLatin1String("Save automatically after"))
                  && text.contains(QLatin1String("Environment variables")),
              QStringLiteral("Help > System Information: program, runtime, system, screens, processor, memory, settings, project"),
              text.left(2500));
        check(!text.contains(QDir::homePath() + QLatin1Char('/')) || QDir::homePath() == QLatin1String("/"),
              QStringLiteral("... the home folder is written as ~"));
        check(copied, QStringLiteral("... Copy to Clipboard copies all of it"));
    }

    // ---- Things to look at (what may cause trouble soon)
    {
        AppSettings &as = AppSettings::instance();
        const int autosaveWas = as.autosaveSeconds;
        as.autosaveSeconds = 120;
        as.backupOnClose = false;
        as.backupOnOpen = false;
        // a project folder of 4.5 GB (a sparse file: no disk space is used)
        const QString big = w.project()->folderPath() + QLatin1String("/content/interview.mp4");
        {
            QFile f(big);
            f.open(QIODevice::WriteOnly);
            f.resize(4500000000LL);
        }
        const QStringList c = sysinfo::concerns(w.project());
        const QString all = c.join(QLatin1Char('\n'));
        const QString text = sysinfo::text(w.project());
        check(text.contains(QLatin1String("Things to look at\n  - ")) && text.indexOf(QLatin1String("Things to look at")) < text.indexOf(QLatin1String("Program\n")),
              QStringLiteral("System Information starts with \"Things to look at\""), text.left(600));
        check(all.contains(QLatin1String("waits for 120 s")) && all.contains(QLatin1String("Automatic backups are off")),
              QStringLiteral("... it names a long autosave time and backups that are off"), all);
        // (0.20: backups of more than 4.29 GB are correct, Zip64; the warning of 0.19 is gone)
        check(!all.contains(QLatin1String("4.29 GB")),
              QStringLiteral("... a large project folder is no longer called too large for a backup (0.20)"), all);
        QFile::remove(big);
        as.autosaveSeconds = autosaveWas;
        as.backupOnClose = true;
        const QString all2 = sysinfo::concerns(w.project()).join(QLatin1Char('\n'));
        check(!all2.contains(QLatin1String("4.29 GB")) && !all2.contains(QLatin1String("waits for")) && !all2.contains(QLatin1String("backups are off")),
              QStringLiteral("... the lines go when the cause is gone"), all2);
        as.backupOnClose = false;
    }

    // ---- recent actions: commands and keys, never typed text
    {
        TextEditor *ed = nullptr;
        for (TextEditor *e : w.findChildren<TextEditor *>())
            if (e->isVisible() && e->item() == doc)
                ed = e;
        if (!ed) {
            w.findChild<BinderPanel *>()->select({doc}, true);
            settle(300);
            for (TextEditor *e : w.findChildren<TextEditor *>())
                if (e->isVisible() && e->item() == doc)
                    ed = e;
        }
        if (ed) {
            QApplication::setActiveWindow(&w);
            ed->setFocus();
            settle(100);
            QTest::keyClicks(ed, QStringLiteral("secret"));
            QTest::keyClick(ed, Qt::Key_Left);
            QTest::keyClick(ed, Qt::Key_Left);
            QTest::keyClick(ed, Qt::Key_End);
            settle(100);
        }
        const QString acts = crash::recentActions();
        check(ed && acts.contains(QLatin1String("typing in TextEditor: 6 keys (not recorded)"))
                  && acts.contains(QLatin1String("arrow keys in TextEditor: 2 presses")) && acts.contains(QLatin1String("key End in TextEditor"))
                  && !acts.contains(QLatin1String("secret")) && acts.contains(QLatin1String("project opened: Lighthouse")),
              QStringLiteral("recent actions: typing is only counted, other keys and the project are named"), acts.right(900));
    }

    // ---- Crash Reports
    {
        int count = 0;
        QString shown;
        bool deleted = false;
        withModal<CrashReportsDialog>(400, [&](CrashReportsDialog *d) {
            count = d->count();
            d->select(segvReport);
            settle(100);
            shown = d->shownText();
            d->grab().save(outDir + QLatin1String("/v019-11-crash-reports.png"));
            if (auto *del = d->findChild<QPushButton *>(QStringLiteral("deleteCrashReport"))) {
                del->click();
                settle(100);
                deleted = d->count() == count - 1 && !QFile::exists(segvReport);
            }
            d->reject();
        });
        w.findChild<QAction *>(QStringLiteral("crashReports"))->trigger();
        settle(200);
        check(count == 2 && shown.contains(QLatin1String("SIGSEGV")) && shown.contains(QLatin1String("QEventLoop::exec(")),
              QStringLiteral("Help > Crash Reports lists the two reports and shows one with readable names"),
              QStringLiteral("%1 report(s); %2").arg(count).arg(shown.section(QLatin1String("== Backtrace"), 1, 1).left(800)));
        check(deleted, QStringLiteral("... Delete removes the report shown"));
        // Delete All asks first
        bool asked = false;
        int left = -1;
        withModal<CrashReportsDialog>(400, [&](CrashReportsDialog *d) {
            QTimer::singleShot(300, qApp, [&asked] {
                if (auto *q = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    asked = q->text().contains(QLatin1String("1 crash report"));
                    q->button(QMessageBox::Yes)->click();
                }
            });
            d->findChild<QPushButton *>(QStringLiteral("deleteAllCrashReports"))->click();
            settle(200);
            left = d->count();
            d->grab().save(outDir + QLatin1String("/v019-12-no-crash-reports.png"));
            d->reject();
        });
        w.findChild<QAction *>(QStringLiteral("crashReports"))->trigger();
        settle(200);
        check(asked && left == 0 && crash::reports().isEmpty(), QStringLiteral("... Delete All… asks, then the folder is empty"));
    }

    // ---- the notice after an unexpected end
    {
        // a report of a crash 20 s after the start
        QDir().mkpath(crash::reportDir());
        const qint64 t0 = QDateTime::currentSecsSinceEpoch() - 600;
        const QString fake = crash::reportDir() + QStringLiteral("/crash-%1-4242.txt").arg(t0 + 20);
        QFile f(fake);
        f.open(QIODevice::WriteOnly);
        f.write(QStringLiteral("Quillbinder crash report\nFormat: 1\nVersion: 0.19.0\nSignal: 11 SIGSEGV (segmentation fault: "
                               "the program used memory it must not use)\nUnix time: %1\nProcess: 4242\n\n== End\n")
                    .arg(t0 + 20)
                    .toUtf8());
        f.close();
        crash::PreviousEnd e;
        e.pid = 4242;
        e.started = QDateTime::fromSecsSinceEpoch(t0);
        e.crashed = QDateTime::fromSecsSinceEpoch(t0 + 20);
        e.version = QStringLiteral("0.19.0");
        e.report = fake;
        QString text;
        bool hasSkip = false;
        withModal<QMessageBox>(400, [&](QMessageBox *m) {
            text = m->text() + QLatin1Char('\n') + m->informativeText();
            auto *skip = m->findChild<QPushButton *>(QStringLiteral("startWithoutProject"));
            hasSkip = skip != nullptr;
            m->grab().save(outDir + QLatin1String("/v019-13-notice-crash.png"));
            if (skip)
                skip->click();
            else
                m->reject();
        });
        const bool skipped = showPreviousEnds(&w, {e}, true);
        check(text.contains(QLatin1String("ended because of an error on ")) && text.contains(QLatin1String("Help > Crash Reports"))
                  && text.contains(QLatin1String("snapshots")) && text.contains(QLatin1String("backup")),
              QStringLiteral("notice after a crash: what happened, where the report is, where to look for lost work"), text);
        check(hasSkip && skipped,
              QStringLiteral("... it ended within a minute of starting: \"Start Without the Last Project\" is offered and works"));
        // without a report (a power cut), two times
        crash::PreviousEnd k = e;
        k.report.clear();
        k.crashed = QDateTime();
        bool hasSkip2 = true;
        QString text2;
        withModal<QMessageBox>(400, [&](QMessageBox *m) {
            text2 = m->text();
            hasSkip2 = m->findChild<QPushButton *>(QStringLiteral("startWithoutProject")) != nullptr;
            m->grab().save(outDir + QLatin1String("/v019-14-notice-killed.png"));
            m->accept();
        });
        const bool skipped2 = showPreviousEnds(&w, {k, k}, true);
        check(text2.contains(QLatin1String("did not end normally")) && text2.contains(QLatin1String("No crash report"))
                  && text2.contains(QLatin1String("2 times")) && !hasSkip2 && !skipped2,
              QStringLiteral("notice after a power cut or a kill: no report, how often, no extra button"), text2);
        QFile::remove(fake);
    }

    // ---- freeze reports: the main thread stops answering for a while
    {
        crash::install(); // in this test process (the program does it in main)
        crash::note(QStringLiteral("before the freeze"));
        crash::startWatchdog(2);
        settle(600);
        busyMainThread(5000); // 5 s without answering
        settle(2500);         // it answers again; the watchdog adds how long the freeze was
        crash::startWatchdog(3600); // no more reports in the rest of this run
        crash::Report fr;
        for (const crash::Report &r : crash::reports())
            if (r.kind == QLatin1String("freeze") && r.pid == QCoreApplication::applicationPid())
                fr = r;
        const QString t = readAll(fr.file);
        const QString trace = t.section(QLatin1String("== Where the main thread is (innermost first)\n"), 1).section(QLatin1String("\n== "), 0, 0);
        check(!fr.file.isEmpty() && QFileInfo(fr.file).fileName().startsWith(QLatin1String("freeze-"))
                  && t.startsWith(QLatin1String("Quillbinder freeze report\n")) && t.contains(QLatin1String("Not responding for: "))
                  && t.contains(QLatin1String("== Program and system")) && t.contains(QLatin1String("== Recent actions"))
                  && t.contains(QLatin1String("before the freeze")),
              QStringLiteral("a freeze of the main thread writes a freeze report with snapshot and recent actions"), t.left(800));
        check(trace.count(QLatin1String("qb_v019_tests")) >= 2 && trace.contains(QLatin1String("[0x")),
              QStringLiteral("... and where the main thread was stuck (its backtrace, taken while it was frozen)"), trace.left(1500));
        static const QRegularExpression after(QStringLiteral("== Responded again after (\\d+) s"));
        const auto m = after.match(t);
        check(m.hasMatch() && m.captured(1).toInt() >= 4 && m.captured(1).toInt() <= 8 && fr.recovered,
              QStringLiteral("... when it answers again, the report says after how long (about 5 s)"), t.right(300));
        // listed in Help > Crash Reports
        QStringList items;
        withModal<CrashReportsDialog>(400, [&](CrashReportsDialog *d) {
            for (QListWidget *l : d->findChildren<QListWidget *>())
                for (int i = 0; i < l->count(); ++i)
                    items << l->item(i)->text();
            d->select(fr.file);
            settle(100);
            d->grab().save(outDir + QLatin1String("/v019-15-freeze-report.png"));
            d->reject();
        });
        w.findChild<QAction *>(QStringLiteral("crashReports"))->trigger();
        settle(200);
        check(items.join(QLatin1Char('|')).contains(QLatin1String("freeze (recovered)")),
              QStringLiteral("... Help > Crash Reports lists it as \"freeze (recovered)\""), items.join(QLatin1String(" | ")));
        // a freeze the program did not come out of (it was ended from outside): the next start says so
        const QString stuck = crash::reportDir() + QStringLiteral("/freeze-%1-4343.txt").arg(QDateTime::currentSecsSinceEpoch() - 100);
        {
            QFile f(stuck);
            f.open(QIODevice::WriteOnly);
            f.write(t.section(QLatin1String("\n== Responded again"), 0, 0).toUtf8());
        }
        crash::PreviousEnd e;
        e.pid = 4343;
        e.started = QDateTime::currentDateTime().addSecs(-400);
        e.crashed = QDateTime::currentDateTime().addSecs(-100);
        e.version = QStringLiteral(QB_APP_VERSION);
        e.report = stuck;
        QString text;
        QStringList buttons;
        withModal<QMessageBox>(400, [&](QMessageBox *mb) {
            text = mb->text();
            for (QAbstractButton *b : mb->buttons())
                buttons << b->text();
            mb->grab().save(outDir + QLatin1String("/v019-16-notice-freeze.png"));
            mb->accept();
        });
        showPreviousEnds(&w, {e}, true);
        check(text.contains(QLatin1String("stopped responding")) && text.contains(QLatin1String("freeze report"))
                  && buttons.contains(QStringLiteral("Show Freeze Report")) && !crash::readHeader(stuck).recovered,
              QStringLiteral("notice after a freeze the program did not come out of: \"stopped responding\", Show Freeze Report"),
              text + QLatin1String(" | ") + buttons.join(QLatin1Char(',')));
        QFile::remove(stuck);
        QFile::remove(fr.file);
    }

    check(w.isVisible() && w.project(), QStringLiteral("the window and the project are still there"));
    w.closeProject();
    std::printf("%d failure(s)\n", failures);
    return failures;
}
