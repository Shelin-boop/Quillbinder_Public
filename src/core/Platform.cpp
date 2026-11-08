#include "core/Platform.h"
#include "AppInfo.h"

#include <QFile>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_UNIX) && !defined(Q_OS_LINUX)
#include <signal.h>
#endif

namespace qb {
namespace platform {

bool processAlive(qint64 pid)
{
    if (pid <= 0)
        return false;
#if defined(Q_OS_LINUX)
    return QFile::exists(QStringLiteral("/proc/") + QString::number(pid));
#elif defined(Q_OS_WIN)
    HANDLE h = ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!h)
        return ::GetLastError() == ERROR_ACCESS_DENIED; // there, but not ours to look at
    const bool running = ::WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    ::CloseHandle(h);
    return running;
#else
    return ::kill(pid_t(pid), 0) == 0;
#endif
}

QStringList libraryPatterns()
{
#ifdef Q_OS_WIN
    return {QStringLiteral("*.dll")};
#else
    return {QStringLiteral("*.so")};
#endif
}

bool isLibraryFile(const QString &path)
{
#ifdef Q_OS_WIN
    return path.endsWith(QLatin1String(".dll"), Qt::CaseInsensitive);
#else
    return path.endsWith(QLatin1String(".so"));
#endif
}

QString programFileName()
{
#ifdef Q_OS_WIN
    return QStringLiteral(QB_APP_ID ".exe");
#else
    return QStringLiteral(QB_APP_ID);
#endif
}

QString packagePlatform()
{
#ifdef Q_OS_WIN
    return QStringLiteral("windows-x64");
#else
    return QStringLiteral("linux-x86_64");
#endif
}

} // namespace platform
} // namespace qb
