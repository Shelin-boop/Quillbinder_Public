#include "core/Launcher.h"
#include "core/Addons.h"
#include "core/Paths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>

#include <unistd.h>

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

void init()
{
    if (s_init)
        return;
    s_init = true;
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
}

QString statePath() { return addons::coreRoot() + QLatin1String("/state.json"); }

// Qt looks for its plugins relative to a qt.conf next to the program. The
// AppImage is mounted at a different place on every start, so this file is
// rewritten before each start.
void writeQtConf(const QString &dir)
{
    QSaveFile conf(dir + QLatin1String("/qt.conf"));
    if (conf.open(QIODevice::WriteOnly)) {
        conf.write("[Paths]\nPrefix = " + s_appDir.toUtf8() + "/usr\nPlugins = plugins\n");
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
    if (s_appDir.isEmpty())
        return true;
    writeQtConf(dir);
    const QString binary = dir + QLatin1Char('/') + binaryName;
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LD_LIBRARY_PATH"), QString::fromLocal8Bit(libraryPath(qgetenv("LD_LIBRARY_PATH"))));
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
    const QString binary = dir + QLatin1Char('/') + (m.binary.isEmpty() ? QStringLiteral(QB_APP_ID) : m.binary);
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
    QFile::setPermissions(binary, QFile::permissions(binary) | QFileDevice::ExeOwner | QFileDevice::ExeUser);

    const QByteArray origLd = qgetenv("LD_LIBRARY_PATH");
    qputenv("QB_ORIG_LD_LIBRARY_PATH", origLd);
    qputenv("LD_LIBRARY_PATH", libraryPath(origLd));
    qputenv("QB_CORE_ACTIVE", "1");
    qputenv("QB_CORE_BASE", QB_APP_VERSION);

    const QByteArray path = QFile::encodeName(binary);
    ::execv(path.constData(), argv);

    // exec failed: undo and let the caller fall back
    qunsetenv("QB_CORE_ACTIVE");
    qunsetenv("QB_CORE_BASE");
    qunsetenv("QB_ORIG_LD_LIBRARY_PATH");
    if (origLd.isEmpty())
        qunsetenv("LD_LIBRARY_PATH");
    else
        qputenv("LD_LIBRARY_PATH", origLd);
    *notice = QStringLiteral("Program update %1 could not be started and was switched off.").arg(active);
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
