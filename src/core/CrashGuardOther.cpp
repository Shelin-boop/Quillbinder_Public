#include "core/CrashGuard.h"
// Systems other than Linux and Windows (W1 of the port, 2026-11-07; Windows has its own file
// since W2, CrashGuardWin.cpp): the parts that need no system calls. No crash or freeze reports,
// no watchdog, no check for sessions that ended unexpectedly.
#if !defined(Q_OS_LINUX) && !defined(Q_OS_WIN)
#include "AppInfo.h"
#include "core/Paths.h"

#include <QDir>
#include <QElapsedTimer>
#include <QMutex>

#include <utility>

namespace qb {
namespace crash {

namespace {

bool g_installed = false;
QElapsedTimer g_clock;
QtMessageHandler g_prevHandler = nullptr;
QStringList g_log;           // all messages, for the log console (as on Linux)
quint64 g_logCount = 0;
QMutex g_logMutex;
QStringList g_notes;         // the last actions
QString g_lastGroup;
std::function<void()> g_onTerm;

void onQtMessage(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    {
        static const char *const kinds[] = {"debug", "warning", "critical", "fatal", "info"};
        const QString line = QStringLiteral("+%1 s  %2: %3").arg(secondsSinceStart()).arg(QLatin1String(kinds[qBound(0, int(type), 4)]), msg);
        QMutexLocker lock(&g_logMutex);
        g_log.append(line);
        if (g_log.size() > 1000)
            g_log.removeFirst();
        ++g_logCount;
    }
    if (g_prevHandler)
        g_prevHandler(type, ctx, msg);
}

void writeNote(const QString &what, bool replaceLast)
{
    const QString line = QStringLiteral("+%1 s  %2\n").arg(secondsSinceStart()).arg(what.simplified().left(190));
    if (replaceLast && !g_notes.isEmpty())
        g_notes.last() = line;
    else
        g_notes.append(line);
    if (g_notes.size() > 48)
        g_notes.removeFirst();
}

} // namespace

QString reportDir() { return dataDir() + QLatin1String("/CrashReports"); }
QString sessionDir() { return dataDir() + QLatin1String("/sessions"); }

void install()
{
    if (g_installed)
        return;
    g_installed = true;
    g_clock.start();
    QDir().mkpath(reportDir());
    g_prevHandler = qInstallMessageHandler(onQtMessage);
}

bool isInstalled() { return g_installed; }

void setSnapshot(const QByteArray &) {}

void note(const QString &what)
{
    g_lastGroup.clear();
    writeNote(what, false);
}

void noteInGroup(const QString &group, const QString &what)
{
    const bool same = !group.isEmpty() && group == g_lastGroup;
    g_lastGroup = group;
    writeNote(what, same);
}

QString recentActions() { return g_notes.join(QString()); }

QString buildId() { return QString(); }

qint64 secondsSinceStart() { return g_clock.isValid() ? g_clock.elapsed() / 1000 : 0; }

QStringList logLines()
{
    QMutexLocker lock(&g_logMutex);
    return g_log;
}

quint64 logCount()
{
    QMutexLocker lock(&g_logMutex);
    return g_logCount;
}

QList<PreviousEnd> beginSession() { return {}; }
void setSessionProject(const QString &) {}
void endSession() {}

void onTermination(std::function<void()> save) { g_onTerm = std::move(save); }

void setFreezeSeconds(int) {}
void startWatchdog(int) {}

QString readable(const QString &text) { return text; }


} // namespace crash
} // namespace qb
#endif // neither Linux nor Windows
