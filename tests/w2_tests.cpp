// Checks of the Windows port, step W2 (2026-11-07), on Linux: what W2 changed in the code that
// Linux runs as well. The Windows parts themselves are checked on the Windows runner
// (tools/win-check.sh).
//  - Packages name the system their libraries were built for ("platform"): a Windows add-on
//    with libraries is refused here, a Linux one (with or without the field) and one without
//    libraries are accepted; the field goes through the manifest and tools/make-package.py.
//  - platform::processAlive (the project lock check, the writing-tool link): this process
//    is alive, an ended one is not.
//  - The Linux answers of the platform helpers are the ones the program used before.
//  - prune() takes a Windows memory dump (.dmp) with its report; other files stay.
// Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_w2_tests /tmp/shots     (no window is opened; the folder is not used)
#include "core/Addons.h"
#include "core/CrashGuard.h"
#include "core/Platform.h"
#include "core/Zip.h"

#include "AppInfo.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

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

// an add-on package made with tools/make-package.py from files given as name -> content
static QString makeAddon(const QString &dir, const QString &id, const QMap<QString, QByteArray> &files,
                         const QString &platform, QString *log)
{
    const QString folder = dir + QLatin1Char('/') + id;
    QDir().mkpath(folder + QLatin1String("/payload"));
    QJsonObject m{{QStringLiteral("id"), id},
                  {QStringLiteral("name"), QStringLiteral("Check %1").arg(id)},
                  {QStringLiteral("version"), QStringLiteral("1.0")},
                  {QStringLiteral("minApp"), QStringLiteral("0.1")}};
    QFile mf(folder + QLatin1String("/manifest.json"));
    mf.open(QIODevice::WriteOnly);
    mf.write(QJsonDocument(m).toJson());
    mf.close();
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        const QString p = folder + QLatin1String("/payload/") + it.key();
        QDir().mkpath(QFileInfo(p).absolutePath());
        QFile f(p);
        f.open(QIODevice::WriteOnly);
        f.write(it.value());
    }
    const QString out = dir + QLatin1Char('/') + id + QLatin1String(".qbpkg");
    QStringList args{QStringLiteral(QB_TEST_SOURCE_DIR "/../tools/make-package.py"), QStringLiteral("addon")};
    if (!platform.isEmpty())
        args << QStringLiteral("--platform") << platform;
    args << folder << out;
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    QString py = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (py.isEmpty())
        py = QStandardPaths::findExecutable(QStringLiteral("python")); // the Windows runner
    p.start(py.isEmpty() ? QStringLiteral("python3") : py, args);
    p.waitForFinished(60000);
    if (log)
        *log = QString::fromUtf8(p.readAll());
    return out;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral(QB_APP_NAME));
    QCoreApplication::setApplicationName(QStringLiteral(QB_APP_NAME));
    QTemporaryDir tmp;
    const QString dir = tmp.path();

    // ---- the Linux answers of the platform helpers
#ifdef Q_OS_WIN
    // the same suite on the Windows runner (W3): the Windows answers
    check(platform::libraryPatterns() == QStringList{QStringLiteral("*.dll")}, QStringLiteral("plugin libraries: *.dll"));
    check(platform::isLibraryFile(QStringLiteral("C:/x/qb-scratch-pad.DLL")) && !platform::isLibraryFile(QStringLiteral("/x/libqb.so")),
          QStringLiteral("a .dll is a plugin library here, a .so is not"));
    check(platform::programFileName() == QLatin1String(QB_APP_ID ".exe"), QStringLiteral("program file name with .exe"));
    check(platform::packagePlatform() == QLatin1String("windows-x64"), QStringLiteral("this system's package platform: windows-x64"));
#else
    check(platform::libraryPatterns() == QStringList{QStringLiteral("*.so")}, QStringLiteral("plugin libraries: *.so"));
    check(platform::isLibraryFile(QStringLiteral("/x/libqb-scratch-pad.so")) && !platform::isLibraryFile(QStringLiteral("/x/qb.dll")),
          QStringLiteral("a .so is a plugin library here, a .dll is not"));
    check(platform::programFileName() == QLatin1String(QB_APP_ID), QStringLiteral("program file name without .exe"));
    check(platform::packagePlatform() == QLatin1String("linux-x86_64"), QStringLiteral("this system's package platform: linux-x86_64"));
#endif

    // ---- processAlive: this process, an ended one, impossible ids
    check(platform::processAlive(QCoreApplication::applicationPid()), QStringLiteral("this process is alive"));
    // the id of a child of a shell that has ended before the shell prints it
#ifdef Q_OS_WIN
    QProcess cmd; // its id while it runs, then it ends
    cmd.start(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), QStringLiteral("ping -n 2 127.0.0.1 > nul")});
    cmd.waitForStarted(10000);
    const qint64 gone = cmd.processId();
    cmd.waitForFinished(20000);
#else
    QProcess sh;
    sh.start(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("sleep 0 & echo $!; wait")});
    sh.waitForFinished(10000);
    const qint64 gone = QString::fromLatin1(sh.readAllStandardOutput()).trimmed().toLongLong();
#endif
    check(gone > 0 && !platform::processAlive(gone), QStringLiteral("a process that ended is not alive"),
          QStringLiteral("pid %1").arg(gone));
    check(!platform::processAlive(0) && !platform::processAlive(-5), QStringLiteral("ids 0 and below are never alive"));

    // ---- packages and their platform
    QString log;
    const QString linuxOld = makeAddon(dir, QStringLiteral("check-linux-old"),
                                       {{QStringLiteral("plugins/libqb-check.so"), QByteArray("x")}}, QString(), &log);
    const addons::PackageInfo a = addons::inspect(linuxOld);
#ifdef Q_OS_WIN
    check(!a.isValid() && a.error.contains(QLatin1String("made for Linux")), QStringLiteral("a Linux add-on with a .so and no platform field is refused here"),
          a.error + QLatin1Char(' ') + log);
#else
    check(a.isValid() && a.manifest.platform.isEmpty(), QStringLiteral("a Linux add-on with a .so and no platform field is accepted"),
          a.error + QLatin1Char(' ') + log);
#endif

    const QString linuxNew = makeAddon(dir, QStringLiteral("check-linux-new"),
                                       {{QStringLiteral("plugins/libqb-check.so"), QByteArray("x")}}, QStringLiteral("linux-x86_64"), &log);
    const addons::PackageInfo b = addons::inspect(linuxNew);
#ifdef Q_OS_WIN
    check(!b.isValid() && b.manifest.platform == QLatin1String("linux-x86_64"),
          QStringLiteral("make-package.py --platform writes the field; a linux-x86_64 add-on is refused here"), b.error + QLatin1Char(' ') + log);
#else
    check(b.isValid() && b.manifest.platform == QLatin1String("linux-x86_64"),
          QStringLiteral("make-package.py --platform writes the field; a linux-x86_64 add-on is accepted"), b.error + QLatin1Char(' ') + log);
#endif

    const QString win = makeAddon(dir, QStringLiteral("check-windows"),
                                  {{QStringLiteral("plugins/qb-check.dll"), QByteArray("x")}}, QStringLiteral("windows-x64"), &log);
    const addons::PackageInfo c = addons::inspect(win);
#ifdef Q_OS_WIN
    check(c.isValid(), QStringLiteral("a Windows add-on is accepted here"), c.error + QLatin1Char(' ') + log);
    if (false) {
#else
    check(!c.isValid() && c.error.contains(QLatin1String("made for Windows")), QStringLiteral("a Windows add-on is refused here"),
          c.error + QLatin1Char(' ') + log);
    {
#endif
        QStringList ilog;
        QString err;
        bool restart = false;
        const bool installed = addons::install(win, &ilog, &err, &restart);
        check(!installed && !QFileInfo::exists(addons::addonsRoot() + QLatin1String("/check-windows")),
              QStringLiteral("... and not installed"), err);
    }

    const QString content = makeAddon(dir, QStringLiteral("check-content"),
                                      {{QStringLiteral("themes/check.json"), QByteArray("{}")}}, QString(), &log);
    const addons::PackageInfo d = addons::inspect(content);
    check(d.isValid(), QStringLiteral("an add-on without libraries fits every system"), d.error + QLatin1Char(' ') + log);

    // the field through the manifest
    addons::Manifest m;
    m.id = QStringLiteral("x");
    m.platform = QStringLiteral("windows-x64");
    check(addons::Manifest::fromJson(m.toJson()).platform == QLatin1String("windows-x64")
              && !addons::Manifest().toJson().contains(QLatin1String("platform")),
          QStringLiteral("the platform field is written when set and read back"));

    // ---- prune() takes the memory dump with its report
    QDir().mkpath(crash::reportDir());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QStringList made;
    for (int i = 0; i < 3; ++i) {
        const QString base = crash::reportDir() + QStringLiteral("/crash-%1-%2").arg(now - 100 * (i + 1)).arg(4000 + i);
        QFile t(base + QLatin1String(".txt"));
        t.open(QIODevice::WriteOnly);
        t.write(QStringLiteral("Quillbinder crash report\nFormat: 1\nVersion: 0.60.5\nSignal: 11 SIGSEGV (segmentation fault)\n"
                               "Unix time: %1\nProcess: %2\n").arg(now - 100 * (i + 1)).arg(4000 + i).toUtf8());
        t.close();
        QFile dmp(base + QLatin1String(".dmp"));
        dmp.open(QIODevice::WriteOnly);
        dmp.write("MDMP");
        made << base;
    }
    QFile other(crash::reportDir() + QLatin1String("/notes.txt"));
    other.open(QIODevice::WriteOnly);
    other.write("kept");
    other.close();
    crash::prune(1);
    check(QFile::exists(made.at(0) + QLatin1String(".txt")) && QFile::exists(made.at(0) + QLatin1String(".dmp")),
          QStringLiteral("prune keeps the newest report and its memory dump"));
    check(!QFile::exists(made.at(1) + QLatin1String(".txt")) && !QFile::exists(made.at(1) + QLatin1String(".dmp"))
              && !QFile::exists(made.at(2) + QLatin1String(".dmp")),
          QStringLiteral("... and removes the older ones with their dumps"));
    check(QFile::exists(other.fileName()), QStringLiteral("... other files of the folder stay"));
    const QList<crash::Report> reps = crash::reports();
    check(reps.size() == 1 && reps.first().signal == QLatin1String("SIGSEGV") && reps.first().pid == 4000,
          QStringLiteral("the report's header is read as before (moved to CrashGuardCommon.cpp)"));

    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
