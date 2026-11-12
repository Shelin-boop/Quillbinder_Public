#pragma once
// Windows (port, W4b, 2026-11-12): what the portable program puts outside its folder, and only when the writer asks
// for it (the first start's guide, File > Options > General > This Computer), each with a way back:
//  - a Start menu entry and a desktop shortcut (Quillbinder.lnk in the user's Start menu and Desktop folders);
//  - the project file types: .qbx files open with Quillbinder, and Quillbinder is offered in "Open with" for
//    Scrivener's .scrivx files (Scrivener keeps them; keys under HKEY_CURRENT_USER\Software\Classes, no admin).
// Other systems: available() is false and nothing is done. Test builds: QB_TEST_SHELL_DIR (a folder for both
// shortcuts) and QB_TEST_SHELL_CLASSES (a registry key instead of HKEY_CURRENT_USER\Software\Classes).
#include <QString>

namespace qb {
namespace winshell {

enum class Item { StartMenu, Desktop, FileTypes };
enum class State { Absent, Here, Elsewhere }; // Elsewhere: there, but for another copy of the program

bool available();
State state(Item item);
bool add(Item item, QString *error);
bool remove(Item item, QString *error);
// The file of a shortcut (Start menu, desktop), for the checks and the guide's details.
QString shortcutFile(Item item);
// The registry key of the file types (HKEY_CURRENT_USER\Software\Classes).
QString classesKey();

} // namespace winshell
} // namespace qb
