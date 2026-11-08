#pragma once
// Small things that differ between Linux and Windows (port step W2, 2026-11-07).
// The Linux answers are the ones the program used before W2.
#include <QString>
#include <QStringList>

namespace qb {
namespace platform {

// Is a process with this id running on this computer? Linux: /proc/<pid> exists.
// Windows: the process exists and has not ended (also when it belongs to another user).
bool processAlive(qint64 pid);

// File patterns of plugin libraries: "*.so" (Linux), "*.dll" (Windows).
QStringList libraryPatterns();
// Is this file a plugin library of this system?
bool isLibraryFile(const QString &path);

// The name of the program file: "quillbinder" (Linux), "quillbinder.exe" (Windows).
QString programFileName();

// The system's name for add-on packages that carry libraries: "linux-x86_64" or "windows-x64".
// Add-ons without libraries have no platform and fit every system.
QString packagePlatform();

} // namespace platform
} // namespace qb
