#pragma once
// The shared color palette (0.21).
//
// What it is for: wherever the writer picks a color (the mind map's fill,
// text, border and line colors, the color buttons of the format bar, the
// colors of labels and of object categories), the same few colors are
// offered first, so that a "red" in one place is the same red everywhere.
//
// What it holds: the colors of the project's labels (with their names, so
// a mind map note can be "Chapter" colored), then the six fixed colors of the
// format bar (red, blue, yellow, green, white, black, since 0.17), and at the
// end "Other..." for the system's color window.
//
// Why a "current project": the format bar and some dialogs do not know which
// project they belong to; the main window tells this file when a project is
// opened or closed (setProject), and the label colors follow.

#include <QColor>
#include <QIcon>
#include <QList>
#include <QString>
#include <functional>

class QMenu;
class QWidget;

namespace qb {

class Project;

namespace palette {

struct Entry {
    QString name;        // shown as the menu text or tool tip
    QColor color;
    bool fromLabel = false;
};

// The six fixed colors (red, blue, yellow, green, white, black).
QList<Entry> standard();
// The colors of the project's labels ("No Label" and colorless labels left out).
QList<Entry> labels(const Project *project);
// Labels first, then the six fixed colors (a fixed color equal to a label color is left out).
QList<Entry> all(const Project *project);

// The project whose label colors are offered when a caller passes no project.
void setProject(const Project *project);
const Project *currentProject();

// A small square of the color (with a red slash for "no color").
QIcon swatch(const QColor &color, int size = 16);

// Adds the palette to a menu: one entry per color (ticked when it is the
// current color), then "noneText" (when not empty: invalid color = none /
// automatic), then "Other..." which opens the color window. "chosen" is called
// with the color picked (invalid for none).
void fillMenu(QMenu *menu, const Project *project, const QColor &current, const std::function<void(const QColor &)> &chosen,
              QWidget *dialogParent, const QString &noneText = QString(), const QString &dialogTitle = QString());

} // namespace palette
} // namespace qb
