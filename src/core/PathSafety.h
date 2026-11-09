#pragma once
// Checks for names that come from files the writer may get from someone else (project files, .scriv
// projects, ZIP archives, packages) before they become parts of paths (0.64.1, the external audit of
// 2026-11-09: an item id "../../x" made Empty Trash delete a folder outside the project, and a ZIP entry
// with an absolute name was written outside the target folder).
#include <QString>

namespace qb {
namespace pathsafety {

// An id that can be a folder name: letters (A-Z, a-z), digits, '-' and '_', 1 to 80 characters
// (the program's own ids are UUIDs, Scrivener's are UUIDs or numbers).
bool isSafeId(const QString &id);
// A file name extension: letters and digits, 1 to 12 characters.
bool isSafeExtension(const QString &ext);
// A relative path inside an archive or a package: not empty, no absolute path, no drive letter or
// other ':', no backslash, no empty, "." or ".." part.
bool isSafeRelative(const QString &rel);
// Whether path is folder itself or lies inside it, after making both absolute and clean (existing
// parts with symbolic links resolved).
bool isInside(const QString &path, const QString &folder);
// Deletes a folder with everything in it, but only when it lies inside root; a symbolic link is
// removed itself, never what it points to. False when nothing was deleted for these reasons.
bool removeTreeInside(const QString &dir, const QString &root);

} // namespace pathsafety
} // namespace qb
