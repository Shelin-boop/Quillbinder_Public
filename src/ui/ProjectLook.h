#pragma once
// The look of a project (0.40.2): every project keeps its own look, so that a change of
// the program's standard (in an update, or in File > Options for new projects) never
// changes how an existing project looks.
//
// A look is the theme of the areas (binder, main editor, corkboard, outliner, inspector,
// composition mode), the writer's own colors and fonts of these areas on top of it, the
// width of the text column and the border in the paper's color left and right of the
// text. The colors of menus and panels (Area::General) and the interface font belong to
// the whole program (AppSettings), as before.
//
// Stored in the project folder as settings/look.json (older versions ignore the file):
//
//   { "version": 1, "theme": "light", "colors": { "editorPaper": "#d1d495" },
//     "fonts": { "binder": "Noto Sans|10.5||" }, "editorWidth": 500, "border": 10 }
//
// A project without the file gets one when it is first taken into the window
// (look::settle): a project just made gets the look for new projects (AppSettings:
// theme, the areas' own colors and fonts, editorWidth, textBorder), an existing one the
// look it had until then (the same, but without the border, which did not exist).

#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>

namespace qb {
class Project;

namespace look {

struct Values {
    QString theme = QStringLiteral("system");
    QMap<QString, QColor> colors;   // own colors of the areas (theme::isAreaColor keys)
    QMap<QString, QString> fonts;   // own fonts of the areas (theme::isAreaFont keys)
    int editorWidth = 500;          // px at 100 %
    int border = 10;                // px at 100 %, left and right of the text

    QJsonObject toJson() const;
    // Keys that are missing keep the values of fallback.
    static Values fromJson(const QJsonObject &o, const Values &fallback);
    static Values fromJson(const QJsonObject &o);
    bool operator==(const Values &o) const;
    bool operator!=(const Values &o) const { return !(*this == o); }
};

// The look for new projects (from the settings) and setting it (into the settings, not saved).
Values forNewProjects();
void setForNewProjects(const Values &v);

QString fileOf(const Project *p);
bool isStored(const Project *p);
Values read(const Project *p, bool *ok = nullptr);
bool write(const Project *p, const Values &v, QString *error = nullptr);

// A project taken into the window: its look, made and written first when it has none
// (isNew: the look for new projects; else the look it had so far, without a border).
Values settle(Project *p, bool isNew);

// ---- the look in use
// The open project and its look (settle()); null: none, the areas then show the look for
// new projects.
void activate(Project *p, const Values &v = Values());
Project *project();
// The look shown now: the open project's, or the one for new projects.
Values active();
// Change the look shown now: the project's file (or, without a project, the settings),
// then the window follows (theme::apply). Returns false when the file could not be written.
bool change(const Values &v, QString *error = nullptr);
// The width of the text column and the border at 100 %.
int editorWidth();
int border();
// For the test runs written before 0.40.2 (one look for the whole program): the open
// project shows and changes the look for new projects, as before. Not used by the program.
void setFollowSettings(bool on);

} // namespace look
} // namespace qb
