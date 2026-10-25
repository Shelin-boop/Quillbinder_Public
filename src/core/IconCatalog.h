#pragma once
// The document icons of Documents > Change Icon (0.17), arranged as in the
// icon menu of Scrivener 3 for Windows: submenus (Book, Characters, Flag,
// Locations, Notes, To Do, TV, Way-Station), then single icons (Back Matter
// ... Warning). Each icon has the name Scrivener stores in a project
// (<IconFileName> in the .scrivx: "Notes (Yellow Notepad)" for an icon of a
// submenu, "Front Matter" for a single one), so icons survive the way into
// Scrivener and back.
//
// Icon names used in BinderItem::icon:
//   qb-...           an icon of this catalog (drawn by tools/make-item-icons.py)
//   custom:<name>    an icon of the project (Icon from Text, Manage Icons), a
//                    picture in <project>/icons/<name>.png
//   scriv:<name>     a Scrivener icon this program has no picture for (kept as
//                    it was, so that it goes back into the .scriv unchanged)
//   anything else    a name of the desktop's icon theme (icons before 0.17)

#include <QIcon>
#include <QList>
#include <QString>
#include <QStringList>

namespace qb {

class Project;

namespace icons {

struct CatalogEntry {
    QString id;        // qb-book-green
    QString label;     // Green Book
    QString group;     // Book (empty: a single icon below the submenus)
    QString scrivener; // Book (Green Book); see scrivenerName()
    bool known = true; // the name is one seen in a real Scrivener project or menu
};

const QList<CatalogEntry> &catalog();
QStringList groups();                          // submenus in menu order
QString groupIcon(const QString &group);       // icon shown for a submenu
QList<CatalogEntry> inGroup(const QString &group);
const CatalogEntry *findEntry(const QString &id);

// The name of the icon as the user sees it (catalog, older choices, own icons).
QString label(const QString &id);

// Scrivener: the <IconFileName> for an icon (empty when it has none, e.g. the
// icons of the desktop theme) and the icon for an <IconFileName>.
QString scrivenerName(const QString &id);
QString fromScrivener(const QString &name);
// The picture shown for a Scrivener icon this program does not have.
QString lookalike(const QString &scrivenerName);

// Own icons of a project (<project>/icons/<name>.png).
QString customDir(const Project *project);
QStringList customNames(const Project *project);
QString customPath(const Project *project, const QString &name);
bool isCustom(const QString &id);
QString customName(const QString &id);
// Adds a picture as an own icon (scaled to 64 px); returns the icon id or an empty string.
QString addCustom(Project *project, const QString &name, const QImage &picture, QString *error = nullptr);
bool removeCustom(Project *project, const QString &name);
bool renameCustom(Project *project, const QString &from, const QString &to);
// A picture of up to three characters on a rounded box (Icon from Text).
QImage textIcon(const QString &text, const QColor &box, const QColor &ink, const QColor &border, int size = 64);

// The icon of a name of any kind (for menus and buttons).
QIcon iconFor(const Project *project, const QString &id);

} // namespace icons
} // namespace qb
