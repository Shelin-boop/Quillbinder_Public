#pragma once
#include "AppInfo.h"
#ifdef Q_OS_WIN
#include "core/Launcher.h"
#include "core/TestSwitches.h"
#endif

#include <QDir>
#include <QStandardPaths>
#include <QString>

namespace qb {

// Per-user data folder: ~/.local/share/<AppName> (plugins, backups,
// templates, compile formats, dictionaries).
// Windows (port, W4b, 2026-11-12): portable, the folder "data" inside the program folder (the folder the writer unpacked
// the program into; an installed update runs from data\core\<version> and still names the built-in program's folder),
// so nothing lasting is written outside it. Test builds: QB_DATA_DIR points elsewhere, and Qt's test mode keeps the
// folder of the tests written before W4b (w3).
inline QString dataDir()
{
#ifdef Q_OS_WIN
    const QString forced = QB_TEST_ENV("QB_DATA_DIR");
    if (!forced.isEmpty())
        return QDir::cleanPath(QDir(QDir::fromNativeSeparators(forced)).absolutePath()); // compared with "/" and absolute
    if (!QStandardPaths::isTestModeEnabled())
        return launcher::appDir() + QLatin1String("/data");
#endif
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1String("/" QB_APP_NAME);
}

} // namespace qb
