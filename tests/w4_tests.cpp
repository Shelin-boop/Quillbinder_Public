// Windows port, step W4b (2026-11-12): the portable program. On Linux with QB_TEST_PORTABLE=1 (set here) the Windows
// parts of the windows are shown and checked: the place of notes and transcriptions, the guide of the first start,
// File > Options > General > This Computer, Help > This Computer, no standard backup folder (the question, the status
// bar); Linux itself stays as it was (the data folder, the first start without the guide, the font folder). On the
// Windows runner also: the data folder inside the program folder, the settings file there, fonts in data\fonts and
// not in the registry, the Start menu entry, the desktop shortcut and the file types (in a test folder and a test key).
// Saves screenshots into the folder given as first argument. Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_w4_tests /tmp/shots
#include "AppInfo.h"
#include "core/Backup.h"
#include "core/Launcher.h"
#include "core/Paths.h"
#include "core/Portable.h"
#include "core/Project.h"
#include "ui/AppSettings.h"
#include "ui/FirstStartDialog.h"
#include "ui/FontInstall.h"
#include "ui/MainWindow.h"
#include "ui/OptionStorage.h"
#include "ui/OptionsDialog.h"
#include "ui/PortableSetup.h"
#include "ui/ProjectLook.h"
#include "ui/Theme.h"
#include "ui/WinShell.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
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

static void shot(QWidget *w, const QString &name)
{
    if (w)
        w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}

// answers the next modal window with `act` (called with it) as soon as it is there
static void whenModal(const std::function<void(QWidget *)> &act)
{
    auto *t = new QTimer;
    t->setInterval(50);
    auto tries = std::make_shared<int>(0);
    QObject::connect(t, &QTimer::timeout, [t, act, tries] {
        if (QWidget *m = QApplication::activeModalWidget()) {
            t->stop();
            t->deleteLater();
            act(m);
        } else if (++*tries > 200) {
            t->stop();
            t->deleteLater();
        }
    });
    t->start();
}

// a text as shown, without the places where a long path may break (zero-width spaces)
static QString plain(QString s) { return s.remove(QChar(0x200B)); }

static QString toolFolder(const char *tool)
{
    QSettings s;
    return s.value(QStringLiteral("tools/%1/folder").arg(QLatin1String(tool))).toString();
}

// ---------------------------------------------------------------- the place of notes and transcriptions, the folder

static void coreChecks(const QString &W)
{
    check(portable::active(), QStringLiteral("(the portable parts are on: Windows, or QB_TEST_PORTABLE)"));
    const QString prog = portable::programFolder();
    {
        QSettings s;
        s.remove(QStringLiteral("app/notesPlace"));
        s.remove(QStringLiteral("tools"));
    }
    portable::Notes n = portable::notes();
    check(n.place == portable::NotesPlace::Documents && portable::notesFolder(QStringLiteral("scratch-pad"), n).isEmpty(),
          QStringLiteral("notes and transcriptions: Documents unless chosen otherwise (the tools' own standard folders)"));
    n.place = portable::NotesPlace::ProgramFolder;
    portable::setNotes(n);
    check(toolFolder("scratch-pad") == prog + QStringLiteral("/Scratch Pad")
              && toolFolder("speech-transcription") == prog + QStringLiteral("/Transcriptions")
              && portable::notes().place == portable::NotesPlace::ProgramFolder,
          QStringLiteral("in the program's folder: both tools get a folder there"),
          toolFolder("scratch-pad") + QStringLiteral(" | ") + toolFolder("speech-transcription"));
    portable::Notes own;
    own.place = portable::NotesPlace::Own;
    own.own = W + QStringLiteral("/Writing");
    portable::setNotes(own);
    check(toolFolder("scratch-pad") == W + QStringLiteral("/Writing/Quillbinder Scratch Pad")
              && toolFolder("speech-transcription") == W + QStringLiteral("/Writing/Quillbinder Transcriptions"),
          QStringLiteral("in a folder of one's own: a folder for each tool inside it"));
#ifndef Q_OS_WIN
    // the program folder moved (a USB stick with another drive letter): folders given by the program follow, a folder
    // the writer chose in a tool stays
    n.place = portable::NotesPlace::ProgramFolder;
    qputenv("QB_TEST_PORTABLE_FOLDER", (W + QStringLiteral("/stick-E/Quillbinder")).toUtf8());
    portable::setNotes(n);
    qputenv("QB_TEST_PORTABLE_FOLDER", (W + QStringLiteral("/stick-F/Quillbinder")).toUtf8());
    {
        QSettings s;
        s.setValue(QStringLiteral("tools/speech-transcription/folder"), W + QStringLiteral("/my transcripts"));
    }
    portable::followProgramFolder();
    check(toolFolder("scratch-pad") == W + QStringLiteral("/stick-F/Quillbinder/Scratch Pad")
              && toolFolder("speech-transcription") == W + QStringLiteral("/my transcripts"),
          QStringLiteral("a moved program folder: the folders it gave follow it, a tool's own choice stays"),
          toolFolder("scratch-pad") + QStringLiteral(" | ") + toolFolder("speech-transcription"));
    qputenv("QB_TEST_PORTABLE_FOLDER", prog.toUtf8());
#endif
    {
        QSettings s;
        s.remove(QStringLiteral("app/notesPlace"));
        s.remove(QStringLiteral("app/notesOwn"));
        s.remove(QStringLiteral("app/notesGiven"));
        s.remove(QStringLiteral("tools"));
    }

    // sync services and long paths (the guide's warnings)
    check(portable::syncService(QStringLiteral("C:/Users/anna/OneDrive/Programs/Quillbinder")) == QLatin1String("OneDrive")
              && portable::syncService(QStringLiteral("C:/Users/anna/OneDrive - Firma/Quillbinder")) == QLatin1String("OneDrive")
              && portable::syncService(QStringLiteral("D:/Dropbox/Quillbinder")) == QLatin1String("Dropbox")
              && portable::syncService(QStringLiteral("G:/My Drive/Quillbinder")) == QLatin1String("Google Drive")
              && portable::syncService(QStringLiteral("D:/Dropboxes/Quillbinder")).isEmpty()
              && portable::syncService(QStringLiteral("C:/Users/anna/Quillbinder")).isEmpty(),
          QStringLiteral("a program folder inside OneDrive, Dropbox or Google Drive is recognized, another one is not"));
    check(portable::longPath(QStringLiteral("C:/") + QString(120, QLatin1Char('x')))
              && !portable::longPath(QStringLiteral("C:/Users/anna/Quillbinder")),
          QStringLiteral("a long path of the program folder is recognized"));

    // no standard backup folder
    {
        QSettings s;
        s.remove(QStringLiteral("backup/dir"));
    }
    AppSettings::instance().load();
    check(AppSettings::instance().backupDir.isEmpty(), QStringLiteral("no standard backup folder (backups wait for the writer's choice)"),
          AppSettings::instance().backupDir);

    // the shell items
#ifdef Q_OS_WIN
    const QString shellDir = W + QStringLiteral("/shell");
    qputenv("QB_TEST_SHELL_DIR", shellDir.toUtf8());
    qputenv("QB_TEST_SHELL_CLASSES", "HKEY_CURRENT_USER\\Software\\QuillbinderTest\\Classes");
    const QString realStart = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) + QStringLiteral("/Quillbinder.lnk");
    const bool realBefore = QFileInfo::exists(realStart);
    QString err;
    bool all = true;
    QString why;
    for (winshell::Item it : {winshell::Item::StartMenu, winshell::Item::Desktop, winshell::Item::FileTypes}) {
        const bool added = winshell::add(it, &err) && winshell::state(it) == winshell::State::Here;
        const bool removed = winshell::remove(it, &err) && winshell::state(it) == winshell::State::Absent;
        if (!added || !removed) {
            all = false;
            why += QStringLiteral("item %1: added %2 removed %3 (%4); ").arg(int(it)).arg(added).arg(removed).arg(err);
        }
    }
    check(all, QStringLiteral("Start menu entry, desktop shortcut and file types are added and removed again"), why);
    winshell::add(winshell::Item::StartMenu, &err);
    winshell::add(winshell::Item::FileTypes, &err);
    {
        QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\QuillbinderTest\\Classes"), QSettings::NativeFormat);
        const QString cmd = reg.value(QStringLiteral("Quillbinder.Project/shell/open/command/Default")).toString();
        check(QFileInfo::exists(winshell::shortcutFile(winshell::Item::StartMenu))
                  && reg.value(QStringLiteral(".qbx/Default")).toString() == QLatin1String("Quillbinder.Project")
                  && cmd.contains(QLatin1String("\"%1\"")) && reg.childKeys().isEmpty()
                  && reg.childGroups().contains(QStringLiteral(".scrivx")),
              QStringLiteral("the shortcut file and the registry entries (.qbx opens with the program, .scrivx in Open with)"), cmd);
    }
    winshell::remove(winshell::Item::StartMenu, &err);
    winshell::remove(winshell::Item::FileTypes, &err);
    {
        QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\QuillbinderTest\\Classes"), QSettings::NativeFormat);
        check(reg.childGroups().isEmpty(), QStringLiteral("removed, the file types leave no key behind"),
              reg.childGroups().join(QLatin1Char(' ')));
    }
    QSettings(QStringLiteral("HKEY_CURRENT_USER\\Software"), QSettings::NativeFormat).remove(QStringLiteral("QuillbinderTest"));
    check(QFileInfo::exists(realStart) == realBefore, QStringLiteral("the real Start menu was not touched by the checks"));
    qunsetenv("QB_TEST_SHELL_DIR");
    qunsetenv("QB_TEST_SHELL_CLASSES");

    // the data folder, the settings file and the fonts inside the program folder (the runner gives each suite a data
    // folder of its own with QB_DATA_DIR: without it, the program's own place)
    const QByteArray runnerData = qgetenv("QB_DATA_DIR");
    qunsetenv("QB_DATA_DIR");
    check(dataDir() == launcher::appDir() + QStringLiteral("/data"), QStringLiteral("the data folder is \"data\" in the program folder"),
          dataDir());
    check(fontinstall::userFontDir() == dataDir() + QStringLiteral("/fonts"), QStringLiteral("installed fonts go into data\\fonts"),
          fontinstall::userFontDir());
    const fontinstall::Result fr = fontinstall::install({QStringLiteral(QB_TEST_SOURCE_DIR "/fonts/source-serif-4/SourceSerif4Variable-Roman.ttf")});
    QSettings fontsReg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts"), QSettings::NativeFormat);
    bool inRegistry = false;
    for (const QString &k : fontsReg.childKeys())
        if (fontsReg.value(k).toString().contains(QLatin1String("SourceSerif4"), Qt::CaseInsensitive))
            inRegistry = true;
    check(!fr.families.isEmpty() && QFileInfo::exists(dataDir() + QStringLiteral("/fonts/SourceSerif4Variable-Roman.ttf")) && !inRegistry,
          QStringLiteral("a font installed by the program is a file in data\\fonts, usable at once, not in the registry"),
          fr.skipped.join(QLatin1Char(';')));
    QFile::remove(dataDir() + QStringLiteral("/fonts/SourceSerif4Variable-Roman.ttf"));
    if (!runnerData.isEmpty())
        qputenv("QB_DATA_DIR", runnerData);
#else
    QString err;
    check(!winshell::available() && winshell::state(winshell::Item::StartMenu) == winshell::State::Absent
              && !winshell::add(winshell::Item::Desktop, &err) && err.contains(QLatin1String("Windows")),
          QStringLiteral("Linux: the Windows shell items are not offered"), err);
    check(dataDir() == QString::fromLocal8Bit(qgetenv("XDG_DATA_HOME")) + QStringLiteral("/" QB_APP_NAME),
          QStringLiteral("Linux: the data folder is as before (~/.local/share/Quillbinder)"), dataDir());
    check(fontinstall::userFontDir() == QString::fromLocal8Bit(qgetenv("XDG_DATA_HOME")) + QStringLiteral("/fonts"),
          QStringLiteral("Linux: installed fonts go into ~/.local/share/fonts as before"), fontinstall::userFontDir());
#endif
}

// ---------------------------------------------------------------- the guide of the first start

static void guideChecks(const QString &W)
{
#ifndef Q_OS_WIN
    // a program folder inside a Dropbox folder, with a long path
    const QString prog = W + QStringLiteral("/Dropbox/") + QString(70, QLatin1Char('q')) + QStringLiteral("/Quillbinder");
    QDir().mkpath(prog);
    qputenv("QB_TEST_PORTABLE_FOLDER", prog.toUtf8());
#else
    Q_UNUSED(W)
#endif
    {
        FirstStartDialog dlg;
        dlg.show();
        settle(300);
        shot(&dlg, QStringLiteral("w4-first-start-guide"));
        auto *where = dlg.findChild<QLabel *>(QStringLiteral("firstStartWhere"));
        const QList<QLabel *> warnings = dlg.findChildren<QLabel *>(QStringLiteral("firstStartWarning"));
        QStringList texts;
        for (QLabel *l : warnings)
            texts << l->text();
        auto *folder = dlg.findChild<QLineEdit *>(QStringLiteral("firstBackupFolder"));
        check(where && plain(where->text()).contains(QDir::toNativeSeparators(portable::programFolder()))
                  && where->text().contains(QLatin1String("\"data\"")) && dlg.findChild<PortableSetup *>()
                  && dlg.findChild<QScrollArea *>(QStringLiteral("firstStartScroll")),
              QStringLiteral("the guide says where everything is kept and offers the notes' place and the Windows items"));
#ifndef Q_OS_WIN
        check(texts.join(QLatin1Char('\n')).contains(QLatin1String("Dropbox")) && texts.join(QLatin1Char('\n')).contains(QLatin1String("260")),
              QStringLiteral("the guide warns of a program folder in a sync service and of a long path"), texts.join(QStringLiteral(" | ")));
#endif
        check(folder && folder->text().isEmpty() && !folder->placeholderText().isEmpty(),
              QStringLiteral("the guide's backup folder is empty until chosen, and says so"),
              folder ? folder->text() : QString());
        // choose the program's folder for notes, OK
        if (auto *r = dlg.findChild<QRadioButton *>(QStringLiteral("portNotesProgram")))
            r->setChecked(true);
        dlg.take();
        check(toolFolder("scratch-pad") == portable::programFolder() + QStringLiteral("/Scratch Pad")
                  && AppSettings::instance().backupDir.isEmpty() && !FirstStartDialog::due(),
              QStringLiteral("OK takes the place of notes over, leaves backups without a folder and marks the first start done"),
              toolFolder("scratch-pad"));
        auto *shellState = dlg.findChild<QLabel *>(QStringLiteral("portShellState_startMenu"));
        auto *shellButton = dlg.findChild<QPushButton *>(QStringLiteral("portShell_startMenu"));
#ifdef Q_OS_WIN
        check(shellButton && shellButton->isEnabled(), QStringLiteral("the Start menu entry can be added from the guide"));
#else
        check(shellState && shellState->text().contains(QLatin1String("Windows")) && shellButton && !shellButton->isEnabled(),
              QStringLiteral("Linux (shown for the checks): the Windows items are not offered"),
              shellState ? shellState->text() : QString());
#endif
    }
    {
        QSettings s;
        s.remove(QStringLiteral("app/notesPlace"));
        s.remove(QStringLiteral("app/notesGiven"));
        s.remove(QStringLiteral("tools"));
    }
#ifndef Q_OS_WIN
    qputenv("QB_TEST_PORTABLE_FOLDER", (W + QStringLiteral("/Quillbinder")).toUtf8());
    // Linux itself: the first start as before (no guide, the standard backup folder)
    qunsetenv("QB_TEST_PORTABLE");
    {
        FirstStartDialog dlg;
        auto *folder = dlg.findChild<QLineEdit *>(QStringLiteral("firstBackupFolder"));
        check(!portable::active() && !dlg.findChild<PortableSetup *>() && !dlg.findChild<QScrollArea *>()
                  && !dlg.findChild<QLabel *>(QStringLiteral("firstStartWhere")) && folder
                  && folder->text() == backup::defaultDirectory(),
              QStringLiteral("Linux: the first start is as before (no guide, the standard backup folder)"),
              folder ? folder->text() : QString());
    }
    qputenv("QB_TEST_PORTABLE", "1");
#endif
}

// ---------------------------------------------------------------- the window: backups, Help, Options

static void windowChecks(const QString &W)
{
    AppSettings &s = AppSettings::instance();
    s.backupDir.clear();
    s.backupOnClose = true;
    s.backupOnOpen = false;
    s.save();
    QString err;
    auto p = Project::create(W, QStringLiteral("Portable"), &err);
    check(bool(p), QStringLiteral("(a project is made)"), err);
    if (!p)
        return;
    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1300, 900);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(p)), QStringLiteral("(project open in the window)"));
    settle(400);
    // a theme chosen as Window > Themes does it: the program's theme and the open project's look (run 5 changed only
    // the program's theme and showed a light binder and inspector beside a dark editor); "" puts the first one back
    const QString themeBefore = s.theme;
    const look::Values lookBefore = look::active();
    auto toTheme = [&](const QString &id) {
        look::Values v = lookBefore;
        if (!id.isEmpty())
            v.theme = id;
        s.theme = id.isEmpty() ? themeBefore : id;
        s.notifyChanged();
        look::change(v);
        settle(300);
    };
    auto *state = w.findChild<QLabel *>(QStringLiteral("backupState"));
#ifdef Q_OS_WIN
    // what the program looks like with Windows 11's own controls (main.cpp sets the style "windows11"), light and dark
    if (QStyleFactory::keys().contains(QStringLiteral("windows11"), Qt::CaseInsensitive)) {
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("windows11")));
        settle(300);
        shot(&w, QStringLiteral("w4-windows11-light"));
        toTheme(QStringLiteral("dark"));
        shot(&w, QStringLiteral("w4-windows11-dark"));
        toTheme(QString());
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        settle(200);
    }
#endif
    check(state && state->isVisibleTo(&w) && state->text().contains(QLatin1String("No backups")),
          QStringLiteral("the status bar says that no backups are made while no folder is chosen"));
    shot(&w, QStringLiteral("w4-window-no-backups"));
    {
        // run 6's dark picture: the note stayed black on the dark status bar after the change of theme
        toTheme(QStringLiteral("dark"));
        const QColor text = QApplication::palette().color(QPalette::WindowText);
        const QString shown = state ? state->text() : QString();
        check(text.lightness() > 128 && shown.contains(QLatin1String("color:") + text.name(), Qt::CaseInsensitive),
              QStringLiteral("after a change to a dark theme, the note \"No backups\" takes the dark theme's text color"),
              shown + QLatin1String(" / ") + text.name());
        shot(&w, QStringLiteral("w4-window-no-backups-dark"));
        toTheme(QString());
        const QColor back = QApplication::palette().color(QPalette::WindowText);
        check(state && state->text().contains(QLatin1String("color:") + back.name(), Qt::CaseInsensitive),
              QStringLiteral("and the light theme's again when it comes back"),
              (state ? state->text() : QString()) + QLatin1String(" / ") + back.name());
    }

    // Back Up Now asks for a folder: "Not Now" makes no backup and says so
    qputenv("QB_TEST_ASK_BACKUP_FOLDER", "1");
    const QString backups = W + QStringLiteral("/Backups chosen");
    QDir().mkpath(backups);
    qputenv("QB_TEST_BACKUP_FOLDER", backups.toUtf8());
    QAction *now = w.findChild<QAction *>(QStringLiteral("backupNow"));
    QString asked;
    whenModal([&asked](QWidget *m) {
        asked = m->objectName();
        if (auto *box = qobject_cast<QMessageBox *>(m))
            box->escapeButton()->click();
    });
    if (now)
        now->trigger();
    settle(200);
    check(asked == QLatin1String("backupFolderQuestion") && w.statusBar()->currentMessage().contains(QLatin1String("No backup was made"))
              && QDir(backups).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty(),
          QStringLiteral("Back Up Now without a folder asks; \"Not Now\" makes no backup and says so"),
          asked + QStringLiteral(" | ") + w.statusBar()->currentMessage());
    // "Choose Folder" takes the folder and makes the backup
    whenModal([](QWidget *m) {
        if (auto *box = qobject_cast<QMessageBox *>(m)) {
            box->defaultButton()->click();
            // then the message "Backup saved"
            whenModal([](QWidget *m2) {
                if (auto *b2 = qobject_cast<QMessageBox *>(m2))
                    b2->accept();
            });
        }
    });
    if (now)
        now->trigger();
    settle(400);
    check(QDir::cleanPath(s.backupDir) == QDir::cleanPath(backups) && !backup::backupsFor(w.project(), s.backupDir).isEmpty()
              && state && !state->isVisibleTo(&w),
          QStringLiteral("\"Choose Folder\" keeps the folder, the backup is made, the status bar's note goes"), s.backupDir);
    qunsetenv("QB_TEST_ASK_BACKUP_FOLDER");
    qunsetenv("QB_TEST_BACKUP_FOLDER");

    // Help > This Computer
    QAction *again = w.findChild<QAction *>(QStringLiteral("thisComputer"));
    QString title;
    bool hasSetup = false;
    whenModal([&title, &hasSetup](QWidget *m) {
        title = m->windowTitle();
        hasSetup = m->findChild<PortableSetup *>() != nullptr;
        shot(m, QStringLiteral("w4-help-this-computer"));
        if (auto *d = qobject_cast<QDialog *>(m))
            d->reject();
    });
    if (again && again->isVisible() && again->isEnabled())
        again->trigger();
    settle(200);
    check(again && again->isVisible() && title.contains(QLatin1String("This Computer")) && hasSetup,
          QStringLiteral("Help > This Computer shows the guide again"), title);

    // File > Options > General > This Computer, and Backup's empty folder
    QString whereText, placeholder;
    bool optSetup = false;
    // 0.90.4: the place of notes and transcriptions tells where it is kept, as every option does since 0.90.1
    // (v0901 found these four controls without it in its first run on Windows, where this page is always there)
    QStringList notesUnmarked;
    int notesControls = 0;
    whenModal([&](QWidget *m) {
        for (const char *name : {"portNotesDocuments", "portNotesProgram", "portNotesOwn", "portNotesOwnFolder"})
            if (auto *c = m->findChild<QWidget *>(QLatin1String(name))) {
                ++notesControls;
                const QString tip = c->toolTip();
                if (optstore::keyOf(c).isEmpty() || !(tip.contains(QLatin1String("Fixed:")) || tip.contains(QLatin1String("Depends on the setting:"))))
                    notesUnmarked << QLatin1String(name) + QLatin1String(" [") + optstore::keyOf(c) + QLatin1String("] ") + tip.right(120);
            }
        if (auto *l = m->findChild<QLabel *>(QStringLiteral("optThisComputerWhere")))
            whereText = plain(l->text());
        optSetup = m->findChild<PortableSetup *>() != nullptr;
        if (auto *e = m->findChild<QLineEdit *>(QStringLiteral("optBackupDir")))
            placeholder = e->placeholderText();
        shot(m, QStringLiteral("w4-options-this-computer"));
        if (auto *d = qobject_cast<QDialog *>(m))
            d->reject();
    });
    OptionsDialog::run(&w, [](OptionsDialog &dlg) { dlg.showSection(OptionsDialog::SGeneral, QStringLiteral("This Computer")); });
    settle(200);
    check(notesControls == 4 && notesUnmarked.isEmpty(),
          QStringLiteral("Options > This Computer: the place of notes and transcriptions tells where it is kept (the program's own setting)"),
          QStringLiteral("%1 controls; %2").arg(notesControls).arg(notesUnmarked.join(QLatin1String(" | "))));
    check(optSetup && whereText.contains(QDir::toNativeSeparators(dataDir())) && !placeholder.isEmpty(),
          QStringLiteral("File > Options > General > This Computer, and the backup folder's note on the page Backup"), whereText);

    s.backupOnClose = false;
    s.save();
    w.closeProject();
    w.close();
    delete wp;
    settle(100);
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
#ifndef Q_OS_WIN
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qputenv("XDG_RUNTIME_DIR", (home.path() + QLatin1String("/run")).toUtf8());
    QDir().mkpath(home.path() + QLatin1String("/run"));
    QFile::setPermissions(home.path() + QLatin1String("/run"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("QB_TEST_PORTABLE", "1");
    qputenv("QB_TEST_PORTABLE_FOLDER", (home.path() + QLatin1String("/Quillbinder")).toUtf8());
#endif
    qputenv("QB_NO_DIALOGS", "1");
    QCoreApplication::setOrganizationName(QStringLiteral("Quillbinder"));
    QCoreApplication::setApplicationName(QStringLiteral("Quillbinder"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    AppSettings::instance().load();
    QTemporaryDir work;
    const QString W = QDir::cleanPath(work.path());
    // a window left open by mistake would stop the run: closed after 15 s
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
        if (modalSince.elapsed() > 15000) {
            std::printf("      (a window stayed open and was closed: %s)\n", qPrintable(m->objectName()));
            m->close();
            modalSince.invalidate();
        }
    });
    watcher.start(1000);
    coreChecks(W);
    guideChecks(W);
    windowChecks(W);
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
