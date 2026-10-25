// Developer tool: builds a project template (.zip) from an outline file, for
// template add-ons.
//
//   qb_template outline.json out.zip
//
// Outline (JSON):
//   { "name": "Memoir", "category": "Nonfiction", "description": "...", "icon": "view-pim-journal",
//     "labelTitle": "Period", "labels": [{"name": "Childhood", "color": "#e9a23b"}],
//     "statuses": ["To Do", "Draft", "Done"],
//     "sectionTypes": ["Part", "Chapter", "Section"],
//     "folderTypes": ["Part", "Chapter"], "fileTypes": ["Section"],
//     "compileFormat": "Modern", "draftTitle": "Manuscript", "templatesFolder": "Sheets",
//     "draft": [ITEM...], "research": [ITEM...], "root": [ITEM...] }
//   ITEM = { "title": "...", "type": "folder" | "text", "icon": "...", "synopsis": "...",
//            "text": "Markdown, one paragraph per line", "label": "Childhood", "status": "To Do",
//            "sectionType": "Chapter", "defaultTemplate": "title of a sheet", "children": [ITEM...] }
// Script projects: "scriptFormat": "Stage Play" (a built-in format, or a format file beside the outline),
//   and in an ITEM "script": true (the document is in script mode) with
//   "elements": [["Scene Heading", "INT. HOUSE - DAY"], ["Action", "..."]] instead of "text".
// Larger projects (the welcome project, resources/welcome/outline.json):
//   "rootFirst": [ITEM...]            items at the top of the binder, above the Draft
//   "keywords": [{"name": "Characters", "color": "#3b7dd8", "children": [{"name": "John"}]}]
//   "projectBookmarks": ["START HERE"]  (titles), "collections": [{"name": "...", "color": "#..", "items": ["title"...]}]
//   ITEM "textFile" / "notesFile": a Markdown file beside the outline instead of "text" / "notes"
//        "type": "image" | "pdf" | "media" | "webpage" | "file" with "file": "files/Sunset.jpg"
//        "keywords": ["John"], "bookmarks": ["Key Concepts"] (titles), "includeInCompile": false,
//        "snapshots": [{"title": "...", "text": "Markdown" or "textFile": "..."}]
//   ITEM "mindmap": a board for the item; top level "objects": objects of the Objects sheet
//        (both described in src/core/OutlineProject.h, which does the work)
//   In texts, {{APP}} is the program's name and [words](qb-title:Some Title) a link to the document of that title.
//
//   qb_template --dump-script "Screenplay"     prints a script format as JSON (starting point for format add-ons)
#include "compile/CompileFormat.h"
#include "core/OutlineProject.h"
#include "core/Project.h"
#include "core/Zip.h"
#include "text/Script.h"

#include "AppInfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>

using namespace qb;

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    if (argc == 3 && QByteArray(argv[1]) == "--dump-format") {
        // prints a built-in compile format as JSON (starting point for format add-ons)
        const compile::Format f = compile::formatByName(QString::fromLocal8Bit(argv[2]));
        std::printf("%s", QJsonDocument(f.toJson()).toJson().constData());
        return 0;
    }
    if (argc == 3 && QByteArray(argv[1]) == "--dump-script") {
        for (const script::Entry &e : script::available())
            if (e.format.name == QString::fromLocal8Bit(argv[2])) {
                std::printf("%s", QJsonDocument(script::toJson(e.format)).toJson().constData());
                return 0;
            }
        std::fprintf(stderr, "no script format \"%s\"\n", argv[2]);
        return 1;
    }
    if (argc < 3) {
        std::fprintf(stderr, "usage: qb_template outline.json out.zip\n       qb_template --dump-format \"Format name\"\n");
        return 2;
    }
    const QString file = QString::fromLocal8Bit(argv[1]);
    QString err;
    const QJsonObject o = outline::read(file, &err);
    if (o.isEmpty()) {
        std::fprintf(stderr, "%s\n", qPrintable(err));
        return 1;
    }
    const QString name = o.value(QStringLiteral("name")).toString().replace(QLatin1String("{{APP}}"), QLatin1String(QB_APP_NAME));
    QTemporaryDir tmp;
    outline::Result r = outline::build(o, QFileInfo(file).absolutePath(), tmp.path(), name);
    for (const QString &w : std::as_const(r.warnings))
        std::fprintf(stderr, "warning: %s\n", qPrintable(w));
    for (const QString &e : std::as_const(r.errors))
        std::fprintf(stderr, "error: %s\n", qPrintable(e));
    if (!r.project) {
        std::fprintf(stderr, "%d error(s): template not written\n", int(r.errors.size()));
        return 1;
    }

    const QString out = QString::fromLocal8Bit(argv[2]);
    QFile::remove(out);
    ZipWriter zw(out);
    QJsonObject meta;
    meta.insert(QStringLiteral("name"), name);
    meta.insert(QStringLiteral("category"), o.value(QStringLiteral("category")).toString(QStringLiteral("My Templates")));
    meta.insert(QStringLiteral("description"), o.value(QStringLiteral("description")).toString());
    if (o.contains(QStringLiteral("icon")))
        meta.insert(QStringLiteral("icon"), o.value(QStringLiteral("icon")).toString());
    zw.addFile(QStringLiteral("template.json"), QJsonDocument(meta).toJson());
    zw.addDirectory(r.project->folderPath(), QFileInfo(r.project->folderPath()).fileName());
    if (!zw.close()) {
        std::fprintf(stderr, "%s\n", qPrintable(zw.errorString()));
        return 1;
    }
    std::printf("%s\n", qPrintable(out));
    return 0;
}
