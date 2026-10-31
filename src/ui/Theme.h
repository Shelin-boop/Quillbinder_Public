#pragma once
// Themes: the colors and fonts of every area of the window.
//
// A theme is a JSON file (built in, part of an add-on, or one of the
// writer's own in <data>/themes/):
//
//   { "name": "Northern Night",
//     "colors": { "window": "#2e3440", "text": "#eceff4", "cardPaper": "#3b4252", ... },
//     "fonts":  { "binder": { "family": "Noto Sans", "size": 10.5, "bold": false, "italic": false } } }
//
// Every key is optional. A color that is not given is derived from the
// others (the binder uses the list background, the cards a paper color
// that fits a light or dark window ...), so a theme of five colors is a
// complete theme. Files written for versions before 0.5 (palette colors
// only) are read unchanged.
//
// On top of the chosen theme sit the writer's own changes (File > Options >
// Appearance), stored in the settings: AppSettings::ownColors / ownFonts.
// "Save as Theme" turns theme + own changes into a new theme file.

#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPalette>
#include <QString>

class QWidget;

namespace qb {
namespace theme {

enum class Area { General, Binder, Editor, Corkboard, Outliner, Inspector, Composition, Count };

enum class Role {
    // general (the window palette)
    Window, WindowText, Base, AlternateBase, Text, Button, ButtonText, Highlight, HighlightedText, Link,
    ToolTipBase, ToolTipText, Mid, Dark, Light, DisabledText,
    TargetSession, TargetDraft, TargetReached,
    // binder
    BinderBackground, BinderText,
    // main editor
    EditorPaper, EditorText, EditorMargin, EditorPageSurround, EditorLink, SearchHit, SpellingError,
    HeaderBar, HeaderBarText, HeaderBarActive, HeaderBarLocked,
    // corkboard and index cards
    CorkboardBackground, CardPaper, CardTitle, CardText, CardTitleLine, CardLines, StatusStamp,
    // outliner
    OutlinerBackground, OutlinerAlternate, OutlinerText,
    // inspector
    InspectorBackground, InspectorText, SynopsisPaper, SynopsisText, NotesPaper, NotesText,
    // composition mode
    CompositionBackground, CompositionPaper, CompositionText,
    Count
};

enum class FontRole {
    Interface, Binder, HeaderTitle, CombinedTitle, CardTitle, CardText, Outliner, Synopsis, Count
};

struct RoleInfo {
    Role role;
    const char *key;   // name in theme files and settings
    QString label;
    Area area;
};
struct FontInfo {
    FontRole role;
    const char *key;
    QString label;
    Area area;
};
const QList<RoleInfo> &roles();
const QList<FontInfo> &fontRoles();
QString areaName(Area a);
const char *key(Role r);
const char *key(FontRole r);
bool roleFromKey(const QString &key, Role *out);
bool fontRoleFromKey(const QString &key, FontRole *out);

// A font change: only the parts that are set differ from the area's normal font.
struct FontSpec {
    QString family;     // empty = keep
    double size = 0;    // points; 0 = keep
    bool bold = false;
    bool italic = false;

    bool isSet() const { return !family.isEmpty() || size > 0 || bold || italic; }
    bool operator==(const FontSpec &o) const
    {
        return family == o.family && qFuzzyCompare(size + 1, o.size + 1) && bold == o.bold && italic == o.italic;
    }
    bool operator!=(const FontSpec &o) const { return !(*this == o); }
    QFont applied(QFont base) const;
    QString toString() const;                 // for the settings: "Noto Sans|10.5|b|i"
    static FontSpec fromString(const QString &s);
    static FontSpec fromFont(const QFont &f, const QFont &base);
};

struct Theme {
    QString id;      // system | light | dark | ... | addon:<add-on>/<file> | user:<file>
    QString name;
    bool followDesktop = false;   // general colors come from the desktop's color scheme
    QMap<Role, QColor> colors;    // only the colors the theme sets
    QMap<FontRole, FontSpec> fonts;

    QJsonObject toJson() const;
    // false when the object holds neither colors nor fonts
    static bool fromJson(const QJsonObject &o, Theme *out);
};

enum class Kind { BuiltIn, Addon, User };
struct Entry {
    QString id;
    QString name;
    Kind kind = Kind::BuiltIn;
    QString file;   // add-on and own themes
};
// Built-in themes, the themes of enabled add-ons, the writer's own themes.
QList<Entry> available();
bool load(const QString &id, Theme *out);

// ---- the look of the open project's areas (0.40.2, ui/ProjectLook.h)
// Every project keeps the look of its areas (binder, editor, corkboard, outliner,
// inspector, composition mode): a theme and own colors and fonts on top of it. The general
// colors (menus, panels: Area::General) and the interface font stay those of the
// settings. Without a project (set = false) the areas use the settings as well, which are
// also the look new projects get.
struct AreaLook {
    bool set = false;
    QString theme;
    QMap<QString, QColor> colors;   // keys of colors outside Area::General
    QMap<QString, QString> fonts;   // keys of fonts other than "interface" (FontSpec::toString)
};
void setAreaLook(const AreaLook &look);   // call apply() afterwards
const AreaLook &areaLook();
bool isAreaColor(const QString &key);     // a color of an area (not Area::General)
bool isAreaFont(const QString &key);      // a font of an area (not the interface font)

// ---- the look in use (chosen theme + own changes)
void apply();                       // reads AppSettings and the area look; call after every change
QColor color(Role r);
QFont font(FontRole r);             // the area's font with the changes applied
QFont baseFont(FontRole r);         // the area's font without them
bool isDark();
QColor accent();
QString currentName();

// All colors of a theme with the derived ones filled in. own: changes on top
// (keys as in theme files). Used by the options dialog to show what
// "automatic" means before anything is applied.
QMap<Role, QColor> resolvedColors(const Theme &t, const QMap<QString, QColor> &own);
QPalette paletteFor(const QMap<Role, QColor> &colors);

// Theme + own changes of the settings as one theme (for "Save as Theme").
Theme currentLook();

// ---- the writer's own themes (<data>/themes/*.json)
QString userThemeDir();
// Writes the theme; returns its id ("user:<file>") or an empty string.
QString saveUserTheme(const Theme &t, QString *error);
bool removeUserTheme(const QString &id, QString *error);
bool renameUserTheme(const QString &id, const QString &newName, QString *newId, QString *error);
// Copies a theme file into the own themes; returns the new id.
QString importTheme(const QString &file, QString *error);
bool exportTheme(const QString &id, const QString &file, QString *error);
bool nameInUse(const QString &name, const QString &exceptId = QString());

// Background, text color and font of a list, tree or text field.
void styleView(QWidget *view, Role background, Role text, FontRole font);
void styleView(QWidget *view, Role background, Role text);

// Emits changed() at the end of apply(); widgets that paint with theme
// colors connect to it.
class Notifier : public QObject
{
    Q_OBJECT
public:
    static Notifier &instance();
signals:
    void changed();
};

} // namespace theme
} // namespace qb
