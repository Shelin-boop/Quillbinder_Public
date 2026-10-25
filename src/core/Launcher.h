#pragma once
// Start-up loader for installed program updates.
//
// The AppImage is read-only. A program update ("core" package) is a newer
// program binary stored in <data>/core/<version>/. At start the built-in
// program checks whether such a version is active and compatible and, if
// so, replaces itself with it (exec). The update runs on the libraries
// inside the AppImage, which is why it must match the runtime id.
//
// Safety net: every start of an update is counted; the update resets the
// counter once its window is up. After two starts in a row that did not
// get that far, the loader switches the update off and the built-in
// version runs again.

#include <QJsonObject>
#include <QString>

namespace qb {
namespace launcher {

// Call first in main(). Returns only if this binary should keep running.
void maybeRunInstalledCore(int argc, char **argv);

// True when this process is an installed update started by the loader.
bool isOverlay();
// Version of the AppImage (or plain build) that started us.
QString builtInVersion();
// Folder with the bundled libraries and plugins (AppDir), empty outside an AppImage.
QString appDir();

// Called by the running program once it has started successfully.
void confirmStart();

// Starts the program version in dir once, invisibly, to see whether it runs
// on this system (used before a program update is installed). Always true
// outside an AppImage, where updates are not started anyway.
bool selfTest(const QString &dir, const QString &binaryName, QString *error);

// Shared state file <data>/core/state.json
QJsonObject readState();
void writeState(const QJsonObject &state);

} // namespace launcher
} // namespace qb
