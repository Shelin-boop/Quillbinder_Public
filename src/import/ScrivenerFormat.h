#pragma once
// Projects in Scrivener 3 format (".scriv" folders).
//
// A project in Scrivener format is worked on through a working copy: an
// ordinary project of this program in the data folder
// (~/.local/share/<app>/ScrivenerProjects/<key>/), whose "scrivenerPath" names
// the .scriv folder. Every save of the working copy also writes the .scriv
// (scrivener::sync, called by Project::save). Only what changed in this
// program is written; documents nobody touched keep the RTF that Scrivener
// wrote byte for byte, and files that Scrivener changed in the meantime are
// left alone. When the .scriv was changed elsewhere (Scrivener, a sync
// service), opening it reads it again and keeps what only this program knows
// (styles, script formats, compile settings, the window layout ...) from the
// old working copy.
//
// What is written follows a real Scrivener 3 project (Mac 3.4, format
// "2.0", Files/version.txt 23): .scrivx with Binder, SectionTypes,
// LabelSettings, StatusSettings, ProjectBookmarks, ProjectTargets;
// Files/Data/<UUID>/content.rtf, notes.rtf, synopsis.txt, content.<ext>;
// Files/Data/docs.checksum; Files/search.indexes; Snapshots/<UUID>.snapshots/.
// Linked comments and footnotes: HYPERLINK "scrivcmt://<ID>" fields plus
// Files/Data/<UUID>/content.comments (documented by others, not seen in the
// sample). Not written (no verified example): keywords, custom metadata,
// arbitrary collections, styles; they are kept in the working copy, and
// whatever a .scriv already has of them is left in its .scrivx untouched.

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>
#include <memory>

namespace qb {

class Project;

namespace scrivener {

// What the last write or read had in the .scriv (settings/scrivener-sync.json
// of the working copy).
struct SyncState {
    QString scrivPath;               // absolute path of the .scriv folder
    QString identifier;              // ScrivenerProject Identifier
    QString fingerprint;             // of the .scriv right after the last write or read
    QHash<QString, QString> ours;    // "<ID>/text", "<ID>/notes", "<ID>/synopsis", "<ID>/file" -> hash of this side
    QHash<QString, QString> theirs;  // "<ID>/content.rtf" ... -> SHA-1 of the file in the .scriv
    QStringList snapshots;           // "<ID>/<file>" written by this program
    bool load(const QString &file);
    bool save(const QString &file) const;
};

struct WriteReport {
    int written = 0;     // files written
    int unchanged = 0;   // documents left as they were
    int removed = 0;     // documents removed from the .scriv
    int snapshots = 0;   // snapshots written
    int keptTheirs = 0;  // files changed in Scrivener meanwhile, not overwritten
    QStringList warnings;
};

QString stateFile(const Project *p);
// The .scrivx file of a .scriv folder (the existing one, or the name it should get).
QString scrivxIn(const QString &scrivDir);
// The .scriv folder for a path that may be the folder or the .scrivx file.
QString scrivFolder(const QString &path);
bool isScrivenerPath(const QString &path);
// A hash over the .scrivx and the names, sizes and times of the files in
// Files/Data and Snapshots.
QString fingerprint(const QString &scrivDir);
// Scrivener has the project open (Files/user.lock); who: "user on host".
bool lockedByScrivener(const QString &scrivDir, QString *who = nullptr);

// Writes the project into scrivDir. With the state of an earlier write only
// what changed is written. state is updated.
bool write(Project *p, const QString &scrivDir, SyncState &state, WriteReport *report, QString *error);

// Working copies
QString workingCopiesDir();
// The working copy of a .scriv (its project folder; "" if there is none yet).
QString workingCopyFor(const QString &scrivPath);

struct OpenResult {
    std::unique_ptr<Project> project;  // the working copy (scrivenerPath set)
    bool reread = false;               // the .scriv was read (first time or changed elsewhere)
    int changedElsewhere = 0;          // documents whose text came from the .scriv
    QStringList warnings;
};
OpenResult open(const QString &scrivPath, QString *error);

// Called by Project::save for a project in Scrivener format.
bool sync(Project *p, QString *error);

// Switch the open project to Scrivener format: a working copy of it is made and
// written to scrivDir (which must not exist yet). The original project folder
// stays as it is. Returns the working copy.
std::unique_ptr<Project> convertToScrivener(Project *p, const QString &scrivDir, QString *error);
// A copy in Scrivener format; the open project stays as it is.
bool saveCopyAsScrivener(Project *p, const QString &scrivDir, WriteReport *report, QString *error);
// A copy as an ordinary project "<parentDir>/<name>.quillbinder" (not linked to
// any .scriv). Returns the copy, opened.
std::unique_ptr<Project> copyAsQuillbinder(Project *p, const QString &parentDir, const QString &name, QString *error);

// Scrivener's date text: "2025-09-02 03:48:53 -0700"
QString dateText(const QDateTime &dt);

} // namespace scrivener
} // namespace qb
