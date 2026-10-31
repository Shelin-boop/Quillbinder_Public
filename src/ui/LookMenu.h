#pragma once
// The look of what is under the mouse, in its right-click menu (0.40.2).
//
// What it is for: the colors, fonts and sizes that File > Options > Appearance (and the
// View menu) set for a part of the window can also be set where the writer is: a
// right-click on the editor's paper offers the paper's color, the text color, the color
// beside the column, the column width and the border; a right-click on the binder its
// background and font, and so on. The changes are those of the open project's look
// (ui/ProjectLook.h); without a project, of the look for new projects. Options that belong
// to the whole program (label colors in the binder, card width ...) change the program's
// settings, as in File > Options.

#include <QPoint>
#include <QString>
#include <functional>

class QMenu;
class QWidget;

namespace qb {
namespace lookmenu {

enum class Part {
    Editor,        // the text and its paper (main editor, copyholder, Quick Reference panels)
    HeaderBar,     // the bar above an editor
    Binder,
    Corkboard,
    Outliner,
    Inspector,     // the inspector, its synopsis card and notes
    Composition,   // composition mode
};

// The menu entries: a submenu "Appearance" at the end of menu. Returns the submenu.
QMenu *add(QMenu *menu, Part part, QWidget *parent);
// Fills a menu with the entries of a part (without the submenu around them).
void fill(QMenu *menu, Part part, QWidget *parent);
// A menu of its own at globalPos (for places that have no right-click menu yet).
void popup(Part part, const QPoint &globalPos, QWidget *parent);

// The name of the part's page in File > Options > Appearance ("Main Editor" ...).
QString optionsPage(Part part);
// How "Appearance Options..." opens File > Options at a page (set by the main window).
void setOptionsOpener(const std::function<void(const QString &page)> &opener);

} // namespace lookmenu
} // namespace qb
