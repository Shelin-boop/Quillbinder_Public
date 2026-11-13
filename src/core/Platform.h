#pragma once
// Small things that differ between Linux and Windows (port step W2, 2026-11-07).
// The Linux answers are the ones the program used before W2.
#include <QByteArray>
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

// 0.90.4 (Windows requirement W-03): Windows reports the AltGr key as Ctrl+Alt, so on a German keyboard AltGr+7
// ("{") ran the command on Ctrl+Alt+7 instead of typing. Qt's Windows platform tells the two apart when it is
// started with the switch "altgr" (then AltGr is Qt::GroupSwitchModifier). Given the present value of
// QT_QPA_PLATFORM this returns the value to set ("windows:altgr", or the value with ",altgr" added to its
// switches), or "" when nothing is to change: another platform is named, or the switch is there already.
QByteArray qpaWithAltGr(const QByteArray &current);

} // namespace platform
} // namespace qb
