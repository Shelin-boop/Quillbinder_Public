#pragma once
#include "AppInfo.h"

#include <QStandardPaths>
#include <QString>

namespace qb {

// Per-user data folder: ~/.local/share/<AppName> (plugins, backups,
// templates, compile formats, dictionaries).
inline QString dataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1String("/" QB_APP_NAME);
}

} // namespace qb
