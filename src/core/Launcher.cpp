#include "core/Launcher.h"
#include "core/Addons.h"
#include "core/Paths.h"
#include "core/Platform.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>

#include <cstdio>

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>

#include <cstdlib>
#include <string>
#include <vector>
#endif

#ifndef QB_RUNTIME_ID
#define QB_RUNTIME_ID "unknown"
#endif

namespace qb {
namespace launcher {

namespace {

bool s_init = false;
bool s_overlay = false;
QString s_base;
QString s_appDir;

#ifdef Q_OS_WIN
// Environment variables through Windows itself: the C library's copy is only read by this
// process, a child process gets Windows' block. Values are UTF-16 (paths with any letters).
std::wstring wideEnv(const wchar_t *name)
{
    const DWORD n = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0)
        return std::wstring();
    std::wstring v(n, L'\0');
    const DWORD got = ::GetEnvironmentVariableW(name, &v[0], n);
    v.resize(got);
    return v;
}
void setWideEnv(const wchar_t *name, const std::wstring &value, bool remove = false)
{
    // the C library's copy, which Qt reads (qEnvironmentVariable), then Windows' block last
    ::_wputenv_s(name, remove ? L"" : value.c_str());
    ::SetEnvironmentVariableW(name, remove ? nullptr : value.c_str());
}
QString exeFolder()
{
    std::vector<wchar_t> buf(4096);
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
    return QFileInfo(QDir::fromNativeSeparators(QString::fromWCharArray(buf.data(), int(n)))).absolutePath();
}
#endif

void init()
{
    if (s_init)
        return;
    s_init = true;
#ifdef Q_OS_WIN
    if (!wideEnv(L"QB_CORE_ACTIVE").empty()) {
        s_overlay = true;
        s_base = QString::fromStdWString(wideEnv(L"QB_CORE_BASE"));
        s_appDir = QDir::fromNativeSeparators(QString::fromStdWString(wideEnv(L"QB_CORE_APPDIR")));
        // only for starting this process: programs started later must not see them, and PATH
        // gets back the value it had (the program folder came first only to find Qt's DLLs)
        const std::wstring origPath = wideEnv(L"QB_ORIG_PATH");
        setWideEnv(L"PATH", origPath, origPath.empty());
        for (const wchar_t *n : {L"QB_CORE_ACTIVE", L"QB_CORE_BASE", L"QB_CORE_APPDIR", L"QB_ORIG_PATH"})
            setWideEnv(n, std::wstring(), true);
    }
    if (s_appDir.isEmpty())
        s_appDir = exeFolder();
    if (s_base.isEmpty())
        s_base = QStringLiteral(QB_APP_VERSION);
#else
    s_appDir = qEnvironmentVariable("APPDIR");
    if (qEnvironmentVariableIsSet("QB_CORE_ACTIVE")) {
        s_overlay = true;
        s_base = qEnvironmentVariable("QB_CORE_BASE");
        // These variables only served to start this process; programs we
        // start later (LibreOffice, the file manager ...) must not see them.
        qunsetenv("QB_CORE_ACTIVE");
        qunsetenv("QB_CORE_BASE");
        const QByteArray orig = qgetenv("QB_ORIG_LD_LIBRARY_PATH");
        if (orig.isEmpty())
            qunsetenv("LD_LIBRARY_PATH");
        else
            qputenv("LD_LIBRARY_PATH", orig);
        qunsetenv("QB_ORIG_LD_LIBRARY_PATH");
    }
    if (s_base.isEmpty())
        s_base = QStringLiteral(QB_APP_VERSION);
#endif
}

QString statePath() { return addons::coreRoot() + QLatin1String("/state.json"); }

// Qt looks for its plugins relative to a qt.conf next to the program. The
// AppImage is mounted at a different place on every start, so this file is
// rewritten before each start.
void writeQtConf(const QString &dir)
{
    QSaveFile conf(dir + QLatin1String("/qt.conf"));
    if (conf.open(QIODevice::WriteOnly)) {
#ifdef Q_OS_WIN
        // the program folder holds Qt's plugin folders (platforms, styles ...) beside the program
        conf.write("[Paths]\nPrefix = " + s_appDir.toUtf8() + "\nPlugins = .\n");
#else
        conf.write("[Paths]\nPrefix = " + s_appDir.toUtf8() + "/usr\nPlugins = plugins\n");
#endif
        conf.commit();
    }
}

QByteArray libraryPath(const QByteArray &orig)
{
    QByteArray ld = QFile::encodeName(s_appDir + QLatin1String("/usr/lib"));
    if (!orig.isEmpty())
        ld += ':' + orig;
    return ld;
}

} // namespace

QJsonObject readState()
{
    QFile f(statePath());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

void writeState(const QJsonObject &state)
{
    QDir().mkpath(addons::coreRoot());
    QSaveFile f(statePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(state).toJson());
        f.commit();
    }
}

bool isOverlay()
{
    init();
    return s_overlay;
}

QString builtInVersion()
{
    init();
    return s_base;
}

QString appDir()
{
    init();
    return s_appDir;
}

QString startProgram()
{
    init();
#ifdef Q_OS_WIN
    // an update is started through the built-in program, which picks the active version
    if (s_overlay && !s_appDir.isEmpty())
        return s_appDir + QStringLiteral("/" QB_APP_ID ".exe");
    return QCoreApplication::applicationFilePath();
#else
    const QString appimage = qEnvironmentVariable("APPIMAGE");
    return appimage.isEmpty() ? QCoreApplication::applicationFilePath() : appimage;
#endif
}

void confirmStart()
{
    init();
    if (!s_overlay)
        return;
    QJsonObject st = readState();
    if (st.value(QStringLiteral("attempts")).toInt() != 0) {
        st.insert(QStringLiteral("attempts"), 0);
        writeState(st);
    }
}

bool selfTest(const QString &dir, const QString &binaryName, QString *error)
{
    init();
    // QB_TEST_NO_SELFTEST: the test suites install program updates with a fake program file (on
    // Windows the program folder is always known, so the start would really happen; W3)
    if (s_appDir.isEmpty() || qEnvironmentVariableIsSet("QB_TEST_NO_SELFTEST"))
        return true;
    writeQtConf(dir);
    const QString binary = dir + QLatin1Char('/') + binaryName;
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_WIN
    const QString path = env.value(QStringLiteral("PATH"));
    env.insert(QStringLiteral("QB_ORIG_PATH"), path);
    env.insert(QStringLiteral("PATH"), QDir::toNativeSeparators(s_appDir) + QLatin1Char(';') + path);
    env.insert(QStringLiteral("QB_CORE_APPDIR"), QDir::toNativeSeparators(s_appDir));
#else
    env.insert(QStringLiteral("LD_LIBRARY_PATH"), QString::fromLocal8Bit(libraryPath(qgetenv("LD_LIBRARY_PATH"))));
#endif
    env.insert(QStringLiteral("QB_CORE_ACTIVE"), QStringLiteral("1"));
    env.insert(QStringLiteral("QB_CORE_BASE"), builtInVersion());
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    proc.setProcessEnvironment(env);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(binary, {QStringLiteral("--selftest")});
    const bool finished = proc.waitForStarted(5000) && proc.waitForFinished(30000);
    const QString output = QString::fromLocal8Bit(proc.readAll());
    if (!finished)
        proc.kill();
    if (finished && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0
        && output.contains(QLatin1String("selftest ok")))
        return true;
    if (error)
        *error = QObject::tr("The new program version does not start on this system, so it was not installed.\n\n%1")
                     .arg(output.trimmed().right(600));
    return false;
}

#ifdef Q_OS_WIN
// Starts the update as a child process with this process's command line and waits for it; ends
// this process with the child's exit code. Returns false, with a notice, when the child could
// not be started or could not load its libraries (it then never reached its window).
static bool startChild(const QString &binary, QString *notice, const QString &active)
{
    const std::wstring origPath = wideEnv(L"PATH");
    const std::wstring appDir = QDir::toNativeSeparators(s_appDir).toStdWString();
    setWideEnv(L"QB_ORIG_PATH", origPath, origPath.empty());
    setWideEnv(L"PATH", appDir + L";" + origPath);
    setWideEnv(L"QB_CORE_ACTIVE", L"1");
    setWideEnv(L"QB_CORE_BASE", QStringLiteral(QB_APP_VERSION).toStdWString());
    setWideEnv(L"QB_CORE_APPDIR", appDir);

    const std::wstring exe = QDir::toNativeSeparators(binary).toStdWString();
    std::wstring cmd = ::GetCommandLineW();
    STARTUPINFOW own{};
    own.cb = sizeof own;
    ::GetStartupInfoW(&own);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    if (own.dwFlags & STARTF_USESHOWWINDOW) { // started minimized or maximized by a shortcut
        si.dwFlags |= STARTF_USESHOWWINDOW;
        si.wShowWindow = own.wShowWindow;
    }
    // the console output reaches whoever started us (checks read --versions, --selftest)
    const HANDLE std3[3] = {::GetStdHandle(STD_INPUT_HANDLE), ::GetStdHandle(STD_OUTPUT_HANDLE),
                            ::GetStdHandle(STD_ERROR_HANDLE)};
    bool anyStd = false;
    for (HANDLE h : std3)
        if (h && h != INVALID_HANDLE_VALUE) {
            ::SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
            anyStd = true;
        }
    if (anyStd) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = std3[0];
        si.hStdOutput = std3[1];
        si.hStdError = std3[2];
    }
    PROCESS_INFORMATION pi{};
    ::AllowSetForegroundWindow(ASFW_ANY); // its window may come to the front, as ours would
    // no "DLL not found" box: a child that cannot load its libraries just ends (and we take over)
    const UINT oldMode = ::SetErrorMode(SEM_FAILCRITICALERRORS);
    const BOOL ok = ::CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, anyStd ? TRUE : FALSE, 0, nullptr,
                                     nullptr, &si, &pi);
    ::SetErrorMode(oldMode);
    for (const wchar_t *n : {L"QB_CORE_ACTIVE", L"QB_CORE_BASE", L"QB_CORE_APPDIR", L"QB_ORIG_PATH"})
        setWideEnv(n, std::wstring(), true);
    setWideEnv(L"PATH", origPath, origPath.empty());
    if (!ok) {
        *notice = QStringLiteral("Program update %1 could not be started and was switched off.").arg(active);
        return false;
    }
    ::CloseHandle(pi.hThread);
    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    ::GetExitCodeProcess(pi.hProcess, &code);
    ::CloseHandle(pi.hProcess);
    // the loader of Windows could not start it: a library or a function missing, a damaged file
    const bool loadFailed = code == 0xC0000135 || code == 0xC0000139 || code == 0xC0000142 || code == 0xC000007B;
    if (loadFailed && readState().value(QStringLiteral("attempts")).toInt() > 0) {
        *notice = QStringLiteral("Program update %1 could not be started on this system and was switched off.").arg(active);
        return false;
    }
    std::fflush(nullptr);
    ::ExitProcess(code);
    return true;
}
#endif

// Tries to start the active program version. Returns (to the caller's
// fallback) when it cannot or must not be started; "notice" then says why.
static void runCore(const QString &active, QJsonObject &st, char **argv, QString *notice)
{
    const QString dir = addons::coreRoot() + QLatin1Char('/') + active;
    QFile mf(dir + QLatin1String("/manifest.json"));
    if (!mf.open(QIODevice::ReadOnly)) {
        *notice = QStringLiteral("Program update %1 is missing and was switched off.").arg(active);
        return;
    }
    const addons::Manifest m = addons::Manifest::fromJson(QJsonDocument::fromJson(mf.readAll()).object());
    if (m.runtime != QLatin1String(QB_RUNTIME_ID)) {
        *notice = QStringLiteral("Program update %1 was made for a different AppImage and was switched off.").arg(active);
        return;
    }
    const QString binary = dir + QLatin1Char('/') + (m.binary.isEmpty() ? platform::programFileName() : m.binary);
    QByteArray expected;
    for (const addons::FileEntry &fe : m.files)
        if (fe.path == QFileInfo(binary).fileName())
            expected = fe.sha256;
    QFile bf(binary);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!bf.open(QIODevice::ReadOnly) || !hash.addData(&bf) || expected.isEmpty()
        || hash.result().toHex() != expected.toLower()) {
        *notice = QStringLiteral("Program update %1 is damaged and was switched off. Install it again.").arg(active);
        return;
    }
    bf.close();
    const int attempts = st.value(QStringLiteral("attempts")).toInt();
    if (attempts >= 2) {
        *notice = QStringLiteral("Program update %1 could not be started and was switched off.").arg(active);
        return;
    }
    st.insert(QStringLiteral("attempts"), attempts + 1);
    writeState(st);

    writeQtConf(dir);
#ifdef Q_OS_WIN
    Q_UNUSED(argv);
    startChild(binary, notice, active); // ends this process when the update ran
#else
    QFile::setPermissions(binary, QFile::permissions(binary) | QFileDevice::ExeOwner | QFileDevice::ExeUser);

    const QByteArray origLd = qgetenv("LD_LIBRARY_PATH");
    qputenv("QB_ORIG_LD_LIBRARY_PATH", origLd);
    qputenv("LD_LIBRARY_PATH", libraryPath(origLd));
    qputenv("QB_CORE_ACTIVE", "1");
    qputenv("QB_CORE_BASE", QB_APP_VERSION);

#ifdef Q_OS_LINUX
    const QByteArray path = QFile::encodeName(binary);
    ::execv(path.constData(), argv);
#endif

    // exec failed: undo and let the caller fall back
    qunsetenv("QB_CORE_ACTIVE");
    qunsetenv("QB_CORE_BASE");
    qunsetenv("QB_ORIG_LD_LIBRARY_PATH");
    if (origLd.isEmpty())
        qunsetenv("LD_LIBRARY_PATH");
    else
        qputenv("LD_LIBRARY_PATH", origLd);
    *notice = QStringLiteral("Program update %1 could not be started and was switched off.").arg(active);
#endif
}

void maybeRunInstalledCore(int argc, char **argv)
{
    Q_UNUSED(argc);
    init();
    if (s_overlay || s_appDir.isEmpty() || qEnvironmentVariableIsSet("QB_NO_UPDATES"))
        return;
    QJsonObject st = readState();
    QString firstNotice;
    // the active version, then the one used before it, then the built-in program
    for (int round = 0; round < 2; ++round) {
        const QString active = st.value(QStringLiteral("active")).toString();
        if (active.isEmpty())
            break;
        QString notice;
        // the built-in program is as new or newer: nothing to load
        if (addons::compareVersions(active, QStringLiteral(QB_APP_VERSION)) > 0)
            runCore(active, st, argv, &notice); // does not return on success
        if (firstNotice.isEmpty())
            firstNotice = notice;
        QString previous = st.value(QStringLiteral("previous")).toString();
        if (previous == active)
            previous.clear();
        st.insert(QStringLiteral("active"), previous);
        st.remove(QStringLiteral("previous"));
        st.insert(QStringLiteral("attempts"), 0);
        // stored now: the next round may replace this process
        if (!firstNotice.isEmpty())
            st.insert(QStringLiteral("notice"),
                      firstNotice + QStringLiteral(" You are using version %1.")
                                        .arg(previous.isEmpty() ? QStringLiteral(QB_APP_VERSION) : previous));
        writeState(st);
    }
}

} // namespace launcher
} // namespace qb
