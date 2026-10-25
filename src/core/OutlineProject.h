#pragma once
// A project built from an outline (0.40): the welcome project of the program
// and the project templates of add-ons (tools/qb-template.cpp) are written as
// an outline file (JSON) with Markdown files beside it, and made into a real
// project here. The format is described in tools/qb-template.cpp; in short:
//
//   { "name": "...", "labels": [...], "statuses": [...], "sectionTypes": [...],
//     "draft": [ITEM...], "research": [ITEM...], "root": [ITEM...], "rootFirst": [ITEM...],
//     "keywords": [...], "projectBookmarks": ["title"], "collections": [...],
//     "objects": [OBJECT...], "objectTemplates": "standard" (the built-in object categories,
//     not the writer's own templates for new projects) }
//   ITEM   = { "title", "type", "icon", "synopsis", "text" | "textFile", "notes" | "notesFile",
//              "label", "status", "keywords", "bookmarks", "snapshots", "children": [ITEM...],
//              "mindmap": BOARD }
//   BOARD  = { "notes": [{"id": 1, "text": "...", "x": 0, "y": 0, "width": 160, "fontSize": 12,
//                         "bold": true, "fill": "#hex", "textColor": "#hex", "border": 1,
//                         "shape": "rounded|square|jagged|cloud", "shadow": true, "faded": true,
//                         "icons": {"topLeft": "icon id", ...}}],
//              "lines": [{"from": 1, "to": 2, "arrow": "none|to|from|both", "label": "...",
//                         "color": "#hex", "icon": "icon id"}],
//              "shapes": [{"id": 20, "x": 0, "y": 0, "w": 100, "h": 80, "fill": "#hex", "magnetic": true}] }
//   OBJECT = { "category": "characters", "name": "...", "folder": "...", "color": "#hex",
//              "values": { "<field id>": "text" | true | ["names of objects" or "titles of documents"] } }
//
// In texts, {{APP}} is the program's name, [words](qb-title:Title) a link to the
// document of that title and [words](qbobj-name:Name) a link to the object of that name.
// The folder of the files may be a resource folder (":/welcome").
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace qb {

class Project;

namespace outline {

struct Result {
    std::unique_ptr<Project> project; // null when it failed
    QStringList errors;               // something the outline names is missing (the project is not made)
    QStringList warnings;             // made all the same
};

// Makes the project "name" in parentDir from the outline and saves it.
// baseDir: where the files named by the outline are (a folder or ":/resource/folder").
Result build(const QJsonObject &outline, const QString &baseDir, const QString &parentDir, const QString &name);

// Reads an outline file (JSON); an empty object and *error when it cannot.
QJsonObject read(const QString &path, QString *error = nullptr);

} // namespace outline
} // namespace qb
