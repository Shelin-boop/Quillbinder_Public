#pragma once
// Project settings saved to a file of their own and read back (0.40.2).
//
// What it is for: the settings of a project (labels, status, section types, custom
// metadata, keywords, formatting, styles, script format, targets, compile settings, page
// and print setup, the look, the object templates) can be saved to a ".qbsettings" file
// and later read into the same project or another one. When the project's settings differ
// from the file's, the window asks per section what to do: take the file's, keep the
// project's, or use what a new project gets (ui/SettingsTransfer.*).
//
// The file is JSON. Settings that point at other things of a project (labels, section
// types, the compile group ...) are written with names, not ids, so that they can be taken
// over by another project:
//
//   { "format": "quillbinder-project-settings", "version": 1, "app": "Quillbinder 0.40.2",
//     "project": "Harbor Light", "saved": "2026-10-26T15:02:11",
//     "sections": { "labels": { "title": "Label", "items": [ { "name": "Concept", "color": "#..." } ] },
//                   "status": ..., "sectionTypes": ..., "customMetadata": ..., "keywords": ...,
//                   "formatting": ..., "styles": ..., "scriptFormat": ..., "targets": ...,
//                   "compile": ..., "pageSetup": ..., "printing": ..., "look": ..., "objectTemplates": ... } }
//
// Taking a section over keeps what the documents still use: a label, status, section type,
// keyword or custom metadata field that documents have but the file does not is kept (and
// named in the notes); a style the text uses is kept as well (applyStyles in the window).

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace qb {

class Project;
struct StyleSheet;
struct Formatting;

namespace settingsfile {

enum Section {
    Labels,
    Status,
    SectionTypes,
    CustomMetadata,
    Keywords,
    Formatting,
    Styles,
    ScriptFormat,
    Targets,
    Compile,
    PageSetup,
    Printing,
    Look,
    ObjectTemplates,
    SectionCount
};

QString key(Section s);     // "labels" ... (in the file)
QString title(Section s);   // "Labels" ... (in the window)

// The settings of the project, section by section (key(s) -> value).
QJsonObject capture(Project *p);
// The whole file: header and sections.
QJsonObject document(Project *p);
bool save(const QString &file, Project *p, QString *error = nullptr);
// Reads a settings file; false (with error) when it is none.
bool load(const QString &file, QJsonObject *doc, QString *error = nullptr);

// Whether the section is the same in both, and a short description of what differs.
bool same(const QJsonValue &a, const QJsonValue &b);
QString differences(Section s, const QJsonValue &project, const QJsonValue &file);

// Takes a section over into the project: Labels, Status, SectionTypes, CustomMetadata,
// Keywords, Targets, Compile, PageSetup, Printing, ObjectTemplates. (Formatting, Styles,
// ScriptFormat and Look go with work in the window: the text follows them.) Notes say
// what was kept because documents use it, or could not be found.
void applyData(Project *p, Section s, const QJsonValue &value, QStringList *notes);

// For the window: the formatting, styles and script format a section holds.
// formattingFrom: the values of base with those of the section.
void formattingFrom(const QJsonValue &v, ::qb::Formatting *f);
// The style sheet of the section (builtIn: the project should use the built-in set).
bool stylesFrom(const QJsonValue &v, StyleSheet *sheet, bool *builtIn);
// The style names the text of the project uses (paragraph and character styles).
QStringList stylesInUse(Project *p);

} // namespace settingsfile
} // namespace qb
