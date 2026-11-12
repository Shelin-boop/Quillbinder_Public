#pragma once
// Windows (port, W4b, 2026-11-12): the portable program. Everything lasting stays inside the folder the writer
// unpacked the program into: settings, add-ons, program updates, crash reports, fonts and the rest in its folder
// "data" (qb::dataDir(), core/Paths.h). Windows' own folders and the registry are touched only when the writer asks
// for a Start menu entry, a desktop shortcut or the project file types (ui/WinShell.h). Other systems: active() is
// false and the functions do nothing (the Linux paths are unchanged).
#include <QString>

namespace qb {
namespace portable {

// True on Windows. Test builds: QB_TEST_PORTABLE=1 shows the Windows parts of the windows elsewhere too.
bool active();
// The folder the program was unpacked into (Windows: the built-in program's folder, also while an update runs).
QString programFolder();
// Windows: the settings files go into data\settings (call first thing in main(), before any QSettings).
void useProgramFolder();
// "" when the program can keep its data in its folder; else why not (a sentence's end: "... because <why>").
QString folderProblem();
// The sync service a folder seems to belong to ("OneDrive", "Dropbox", "Google Drive", "iCloud Drive"), or "".
QString syncService(const QString &path);
// Windows limits most paths to 260 characters: true when the program folder's own path leaves little room.
bool longPath(const QString &path);

// Where Scratch Pad notes and speech transcriptions are kept when their tools have no folder of their own
// (the writer's choice at the first start and in File > Options > General > This Computer).
enum class NotesPlace { Documents, ProgramFolder, Own };
struct Notes
{
    NotesPlace place = NotesPlace::Documents;
    QString own; // the folder chosen (Own)
};
Notes notes();
// Stores the choice and gives the tools their folders (tools/scratch-pad/folder, tools/speech-transcription/folder).
void setNotes(const Notes &n);
// The folder a tool gets for a choice ("" = the tool's own standard, a folder in Documents).
QString notesFolder(const QString &toolId, const Notes &n);
// At each start: a folder inside the program folder follows the program folder when it was moved (a USB stick with
// another drive letter); a tool's folder the writer changed in the tool itself is left alone.
void followProgramFolder();

} // namespace portable
} // namespace qb
