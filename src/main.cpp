#include "AppInfo.h"
#include "ui/Zoom.h"
#include "compile/PluginManager.h"
#include "core/Addons.h"
#include "core/CrashGuard.h"
#include "core/Launcher.h"
#include "text/FontFaces.h"
#include "ui/AppSettings.h"
#include "ui/CrashDialogs.h"
#include "ui/MainWindow.h"
#include "ui/ToolHost.h"

#include <QAction>
#include "text/PageLayout.h"
#include <QApplication>
#include <QTextDocument>
#include <QHash>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QJsonObject>
#include <QLocalServer>
#include <QMessageBox>
#include <QSettings>
#include <QStyleFactory>
#include <QTimer>

#include <cstdio>

static bool isKde()
{
    const QByteArray d = qgetenv("XDG_CURRENT_DESKTOP").toUpper();
    return d.contains("KDE") || !qgetenv("KDE_FULL_SESSION").isEmpty();
}

int main(int argc, char **argv)
{
    // An installed program update takes over here (see core/Launcher.h).
    qb::launcher::maybeRunInstalledCore(argc, argv);

    // Names first: QSettings (~/.config/<name>/<name>.conf) works before QApplication.
    QCoreApplication::setOrganizationName(QStringLiteral(QB_APP_NAME));
    QCoreApplication::setApplicationName(QStringLiteral(QB_APP_NAME));
    QCoreApplication::setApplicationVersion(QStringLiteral(QB_APP_VERSION));

    // --version / --help work without a display
    for (int i = 1; i < argc; ++i) {
        const QByteArray a(argv[i]);
        // these exit before a window opens: they count as a successful start
        if (a == "--version" || a == "-v" || a == "--help" || a == "-h" || a == "--install" || a == "--versions")
            qb::launcher::confirmStart();
        if (a == "--version" || a == "-v") {
            std::printf("%s %s\n", QB_APP_NAME, QB_APP_VERSION);
            return 0;
        }
        if (a == "--help" || a == "-h") {
            std::printf("Usage: %s [--no-restore] [project | package.qbpkg]\n\n"
                        "  project            project folder or %s file to open\n"
                        "  package.qbpkg      update or add-on to install\n"
                        "  --no-restore       do not reopen the last project\n"
                        "  --install <file>   install a package without opening a window\n"
                        "  --versions         list the installed program versions\n"
                        "  --tool <key>       run a command of a writing tool without the main window\n"
                        "  --scratch-pad      the scratch pad (same as --tool scratch-pad.show)\n",
                        QB_APP_ID, QB_PROJECT_FILE_SUFFIX);
            return 0;
        }
        if (a == "--install" && i + 1 < argc) {
            QCoreApplication core(argc, argv); // the self-test of a program update starts a process
            QStringList log;
            QString error;
            bool restart = false;
            const bool ok = qb::addons::install(QString::fromLocal8Bit(argv[i + 1]), &log, &error, &restart);
            for (const QString &l : log)
                std::printf("%s\n", qPrintable(l));
            if (!ok)
                std::fprintf(stderr, "%s\n", qPrintable(error));
            return ok ? 0 : 1;
        }
        if (a == "--versions") {
            if (qEnvironmentVariableIsSet("QB_DEBUG_ENV")) // developer check: nothing of the loader leaks to child processes
                std::printf("LD_LIBRARY_PATH=[%s] QB_CORE_ACTIVE=[%s]\n", qgetenv("LD_LIBRARY_PATH").constData(),
                            qgetenv("QB_CORE_ACTIVE").constData());
            for (const qb::addons::CoreVersion &cv : qb::addons::coreVersions())
                std::printf("%s%s%s%s\n", qPrintable(cv.version), cv.builtIn ? " (built in)" : "",
                            cv.running ? " (running)" : "", cv.active ? " (active)" : "");
            return 0;
        }
    }

    // KDE: native (Plasma) file dialogs through the desktop portal. Must be
    // decided before the QApplication exists.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME") && isKde()) {
        QSettings s;
        if (s.value(QStringLiteral("app/portal"), true).toBool())
            qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
    }

    QApplication app(argc, argv);
    // fonts with several designs in one family (Sitka: Small ... Banner) get a family per
    // design, so that Bold and Italic stay within the design (0.40.1, text/FontFaces.h)
    qb::fontfaces::install();
    QGuiApplication::setApplicationDisplayName(QStringLiteral(QB_APP_NAME));
    QGuiApplication::setDesktopFileName(QStringLiteral(QB_APP_ID));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app/" QB_APP_ID ".svg")));

    // Icons: the desktop theme (Breeze on KDE) first, the bundled Breeze
    // subset when an icon is missing or no theme is installed.
    QStringList themePaths = QIcon::themeSearchPaths();
    themePaths << QStringLiteral(":/icons");
    QIcon::setThemeSearchPaths(themePaths);
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    if (QIcon::themeName().isEmpty() || QIcon::themeName() == QLatin1String("hicolor"))
        QIcon::setThemeName(QStringLiteral("qbicons"));

    // Fusion is the Qt style that follows the KDE colour scheme best and
    // looks identical everywhere (the AppImage cannot load system styles).
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(fusion);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("%1 - a writing studio for long texts").arg(QStringLiteral(QB_APP_NAME)));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("project"), QStringLiteral("Project to open (folder or %1 file).")
                                                                .arg(QStringLiteral(QB_PROJECT_FILE_SUFFIX)));
    QCommandLineOption noRestore(QStringLiteral("no-restore"), QStringLiteral("Do not reopen the last project."));
    parser.addOption(noRestore);
    parser.addOption(QCommandLineOption(QStringLiteral("selftest"), QStringLiteral("Start invisibly and exit (used by the update routine).")));
    QCommandLineOption toolOpt(QStringLiteral("tool"), QStringLiteral("Run a command of a writing tool without the main window."),
                               QStringLiteral("key"));
    parser.addOption(toolOpt);
    QCommandLineOption scratchOpt(QStringLiteral("scratch-pad"), QStringLiteral("Show the scratch pad (--tool scratch-pad.show)."));
    parser.addOption(scratchOpt);
    parser.process(app);

    qb::AppSettings::instance().load();
    qb::theme::apply();
    qb::zoom::install(); // Ctrl + mouse wheel zooms the area under the mouse (0.21)
    // crash reports with a snapshot of the program and the system (0.19); not for the
    // self-test of a program update, which runs invisibly and only once
    const bool selftest = app.arguments().contains(QStringLiteral("--selftest"));
    if (!selftest) {
        qb::crash::install();
        qb::crash::prune(qBound(1, qb::AppSettings::instance().keepReports, 200)); // File > Options > Extras (0.21)
        qb::sysinfo::installActivityRecorder();
    }
    qb::compile::PluginManager::instance().load();

    // ---- a writing tool on its own (a desktop-wide shortcut starts this)
    QString toolKey = parser.value(toolOpt);
    if (parser.isSet(scratchOpt))
        toolKey = QStringLiteral("scratch-pad.show");
    if (!toolKey.isEmpty()) {
        qb::launcher::confirmStart(); // a tool window counts as a successful start
        qb::toolhost::Host &host = qb::toolhost::host();
        host.setStandaloneCommand(toolKey);
        // one standalone window per command: an open one comes to the front
        for (const QString &peer : host.peers()) {
            const QJsonObject info = qb::toolhost::Host::query(peer, QJsonObject{{QStringLiteral("cmd"), QStringLiteral("info")}}, 1500);
            if (info.value(QStringLiteral("standalone")).toString() == toolKey) {
                qb::toolhost::Host::query(peer, QJsonObject{{QStringLiteral("cmd"), QStringLiteral("run")}, {QStringLiteral("key"), toolKey}});
                return 0;
            }
        }
        bool standalone = false;
        for (const qb::tools::Command &c : host.allCommands())
            if (c.key == toolKey)
                standalone = c.standalone;
        if (!standalone) {
            const QString msg = host.toolFor(toolKey)
                ? QStringLiteral("The command %1 needs the main window.").arg(toolKey)
                : QStringLiteral("No writing tool with the command %1 is installed and switched on (Help > Add-on Manager).").arg(toolKey);
            std::fprintf(stderr, "%s\n", qPrintable(msg));
            if (!qEnvironmentVariableIsSet("QB_NO_DIALOGS"))
                QMessageBox::warning(nullptr, QStringLiteral(QB_APP_NAME), msg);
            return 2;
        }
        host.startLink();
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [] { qb::toolhost::host().shutdownTools(); });
        qb::sysinfo::refresh(nullptr);
        qb::crash::note(QStringLiteral("started as a writing tool: %1").arg(toolKey));
        qb::crash::onTermination([] {
            qb::toolhost::host().shutdownTools();
            QLocalServer::removeServer(qb::toolhost::host().linkPath());
        });
        host.runCommand(toolKey);
        if (host.openToolWindows() == 0)
            return 0;
        return app.exec();
    }

    qb::MainWindow w;
    if (selftest) {
        // Used before a program update is installed: everything loaded, window built.
        // The page view relies on the Qt this program runs on (text/PageLayout.cpp): try it.
        QTextDocument probe;
        probe.setPlainText(QString(QStringLiteral("Page view self-test. ")).repeated(400));
        const qb::PageMetrics pm = qb::PageMetrics::from(qb::PageSetup(), 1.0);
        if (!qb::pagelayout::apply(&probe, pm) || qb::pagelayout::pageCount(&probe) < 2 || probe.isUndoAvailable()
            || !qb::pagelayout::clear(&probe, 600) || probe.pageCount() != 1) {
            std::printf("selftest failed: page layout\n");
            return 3;
        }
        std::printf("selftest ok %s\n", QB_APP_VERSION);
        return 0;
    }
    if (qEnvironmentVariableIsSet("QB_DEBUG_SHORTCUTS")) {
        // Developer check: report shortcuts used by more than one action.
        QHash<QString, QStringList> seen;
        for (QAction *ac : w.findChildren<QAction *>())
            for (const QKeySequence &ks : ac->shortcuts())
                seen[ks.toString()] << ac->text();
        for (auto it = seen.cbegin(); it != seen.cend(); ++it)
            if (it.value().size() > 1)
                std::fprintf(stderr, "shortcut %s: %s\n", qPrintable(it.key()), qPrintable(it.value().join(QStringLiteral(" | "))));
        return 0;
    }
    // how the sessions before this one ended (a crash, a power cut ...); this one starts
    qb::sysinfo::refresh(nullptr);
    const QList<qb::crash::PreviousEnd> previous = qb::crash::beginSession();
    // log out, shutdown or a kill command: save first, then end
    qb::crash::onTermination([&w] { w.saveForTermination(); });
    w.show();
    // other instances (and standalone tools) reach this window through a local socket
    qb::toolhost::host().startLink();

    QString toOpen, toInstall;
    if (!parser.positionalArguments().isEmpty()) {
        const QString arg = QFileInfo(parser.positionalArguments().constFirst()).absoluteFilePath();
        if (arg.endsWith(QLatin1String(".qbpkg"), Qt::CaseInsensitive))
            toInstall = arg;
        else
            toOpen = arg;
    }
    bool reopening = false;
    if (!toOpen.isEmpty()) {
    } else if (!parser.isSet(noRestore) && qb::AppSettings::instance().openLastProject
             && !qb::AppSettings::instance().recentProjects.isEmpty()) {
        const QString last = qb::AppSettings::instance().recentProjects.constFirst();
        if (QFileInfo::exists(last)) {
            toOpen = last;
            reopening = true;
        }
    }

    QTimer::singleShot(0, &w, [&w, toOpen, toInstall, previous, reopening]() mutable {
        w.showStartNotice();
        if (!previous.isEmpty() && !qEnvironmentVariableIsSet("QB_NO_DIALOGS")
            && qb::showPreviousEnds(&w, previous, reopening))
            toOpen.clear(); // the last session ended right after the start: not the last project this time
        w.offerDesktopIntegration();
        if (!toInstall.isEmpty()) {
            w.installPackage(toInstall);
            if (w.isClosing())
                return; // restarting to finish the installation
        }
        // The first start (also the first start of 0.40 after an older version) opens the
        // welcome project, unless a project file was given; the last project comes back next time.
        if (!(toOpen.isEmpty() || reopening) || !w.openWelcomeOnFirstStart()) {
            if (toOpen.isEmpty() || !w.openProject(toOpen))
                w.showStartDialog(false);
        }
        if (!w.project())
            w.close(); // start window cancelled: nothing to show
    });
    // The window is up: tell the start-up loader that this version works.
    QTimer::singleShot(1000, &app, [] { qb::launcher::confirmStart(); });
    // a freeze report when the window does not respond for 30 s (an endless loop ...);
    // the time is set in File > Options > Extras > Crash Reports (0.21)
    qb::crash::startWatchdog(qBound(10, qb::AppSettings::instance().freezeSeconds, 600));
    const int rc = app.exec();
    qb::crash::endSession(); // a normal end
    return rc;
}
