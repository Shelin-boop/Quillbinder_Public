#pragma once
// Start-up loader for installed program updates.
//
// Windows (port, W2): the program folder (installed or unzipped) takes the AppImage's place.
// Windows has no exec: the built-in program starts the update as a child process, with the
// program folder on PATH (Qt's DLLs) and a qt.conf beside the update pointing at the folder's
// Qt plugins, waits for it and ends with its exit code. A child that could not even load its
// DLLs switches the update off at once, and the built-in version runs.
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
#include <QByteArray>
#include <QString>

namespace qb {
namespace launcher {

// Call first in main(). Returns only if this binary should keep running.
void maybeRunInstalledCore(int argc, char **argv);
// 0.70.0 (audit A U-07): whether the installed program update in `dir` may start: its package manifest and
// signature (package-manifest.json / .sig, kept by the installer) are the project's and name the program file with
// expectedSha; *why: the rest of the notice ("is not signed ...").
bool coreOrigin(const QString &dir, const QString &binaryFile, const QByteArray &expectedSha, QString *why);

// True when this process is an installed update started by the loader.
bool isOverlay();
// Version of the AppImage (or plain build) that started us.
QString builtInVersion();
// Folder with the bundled libraries and plugins (AppDir), empty outside an AppImage.
// Windows: the folder of the built-in program (quillbinder.exe and Qt's DLLs), never empty.
QString appDir();
// The program file that starts this program anew (a restart, a writing tool's command): the
// AppImage, else the built-in program (Windows: also when an update runs), else this process's file.
QString startProgram();

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
