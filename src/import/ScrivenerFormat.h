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
// Files/Data/<UUID>/content.comments (as Scrivener 3.1.6 for Windows writes them).
// Keywords, custom metadata and collections (0.81.2), as Scrivener 3.1.6.0 for
// Windows writes them: the .scrivx's Keywords (Keyword ID,
// Title, Color, Children), CustomMetaDataSettings (MetaDataField Type, ID, Title,
// ListOptions with Option ID), each BinderItem's Keywords/KeywordID and
// MetaData/CustomMetaData/MetaDataItem (FieldID, Value), and arbitrary collections
// (Collection Type="Arbitrary", Title, BinderUUIDs as a comma-separated list).
// The ids of the file are kept: this program uses Scrivener's ids as its own for
// what it reads; what is made here gets an id in Scrivener's form when written.
// Not written (no verified example): styles, inline notes as Scrivener keeps them
// (they become linked notes), this program's search collections.

#include "core/ProjectTypes.h"

#include <QDomElement>

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>
#include <memory>

namespace qb {

class Project;

namespace scrivener {

// Custom metadata values (0.81.2). Scrivener writes a checkbox as "Yes" / "No", a list as the ID of
// the option ("-1" = none), a date as "2026-03-14 00:00:00 +0100" and text as typed. This program
// keeps "1" / "0", the option's text ("" = none) and a date as yyyy-MM-dd.
// optionText: the field's options, Scrivener's option ID -> text.
QString valueFromScrivener(CustomFieldType type, const QString &text, const QHash<QString, QString> &optionText);

// A saved search (0.90.0): Scrivener 3.1.6.0 for Windows keeps it as <Collection Type="SavedSearch"> with
// <SearchSettings Operator="Any" Type="Text" Scope="All" CompileSetting="All" CaseSensitive="No" IgnoreDiacritics="No">
// the words</SearchSettings>. Seen in a project: those values. Read: Operator "Any" = any of the words, "All" = all
// of them, "Exact" = the phrase, "RegEx" = a regular expression [the last three are a guess: not seen in a file];
// CaseSensitive; CompileSetting "Included" / "Excluded" [a guess]. What else the element says stays as it is.
SearchQuery queryFromScrivener(const QDomElement &settings);

// What the last write or read had in the .scriv (settings/scrivener-sync.json
// of the working copy).
struct SyncState {
    QString scrivPath;               // absolute path of the .scriv folder
    QString identifier;              // ScrivenerProject Identifier
    QString fingerprint;             // of the .scriv right after the last write or read
    QHash<QString, QString> ours;    // "<ID>/text", "<ID>/notes", "<ID>/synopsis", "<ID>/file" -> hash of this side
    QHash<QString, QString> theirs;  // "<ID>/content.rtf" ... -> SHA-1 of the file in the .scriv
    QStringList snapshots;           // "<ID>/<file>" written by this program
    bool indexStale = false;         // texts changed since Files/search.indexes was written (0.60.5)
    // 0.65.0 (audit F10): the documents (UUIDs) of the .scrivx as last read or written here; a document of the working
    // copy that is not among them never reached the .scriv's binder (kept at the next opening)
    QStringList binder;
    bool binderKnown = false;        // false: a state of a version before 0.65 (then `theirs` is used, as before)
    // 0.81.2: 1 = keywords, custom metadata and collections of the working copy are in the .scriv (written by
    // this version, or read from it with nothing of the working copy's own to add). 0 = a state of an older
    // version, which kept them in the working copy only: the next opening reads the .scriv again and joins
    // the two once (nothing made here is lost), and the next write puts them into the .scriv.
    int meta = 0;
    bool load(const QString &file);
    bool save(const QString &file) const;
};

struct WriteReport {
    int written = 0;     // files written
    int unchanged = 0;   // documents left as they were
    int removed = 0;     // documents removed from the .scriv
    int snapshots = 0;   // snapshots written
    int snapshotsRemoved = 0; // snapshots deleted in this program, taken out of the .scriv (0.61.1)
    int keptTheirs = 0;  // files changed in Scrivener meanwhile, not overwritten
    QStringList conflicts; // changed here and there: the other version was kept first (0.64)
    QStringList keepFailed; // changed here and there, the other version could not be kept: not written (0.64.1)
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
// Scrivener's lock as a stamp ("" = none): which file, its size, time and a hash of what it holds.
// open() records the one it found (the writer chose to open anyway); sync() writes nothing while
// another one is there (0.64.1, the external audit's C-4: before, the lock was looked at only when
// opening, so both programs could write the .scriv).
QString scrivenerLockStamp(const QString &scrivDir);

// Writes the project into scrivDir. With the state of an earlier write only
// what changed is written. state is updated.
// quick (0.60.5, the autosave): the texts, notes and synopses that changed, docs.checksum
// and the .scrivx; a file of this program whose size and time are as recorded is not read
// again, and Scrivener's file only when ours changed. Snapshots, icons, documents removed
// with the trash and the search index wait for the next full write (state.indexStale); the
// fingerprint is brought up to date from what was written instead of reading every file.
bool write(Project *p, const QString &scrivDir, SyncState &state, WriteReport *report, QString *error, bool quick = false);

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

// Called by Project::save for a project in Scrivener format (quick: by Project::autosave).
bool sync(Project *p, QString *error, bool quick = false);

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
