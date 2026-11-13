#pragma once
#include <QDialog>
#include <QIcon>
#include <QList>
#include <QString>
#include <QStringList>

class QComboBox;
class QLineEdit;
class QListWidget;

namespace qb {

// What the toolbars hold. A bar is a list of item ids: keys of commands (see Commands.h), ids of the special controls
// below, "|" for a separator and "<>" for a space that takes up what is left over.
//
// 0.91.0 (the writer's wish of 2026-11-13): the two toolbars became seven, each a toolbar of its own that can be
// switched off and dragged to another place by the grip at its front (top or bottom edge of the window, any row, any
// place along the row). Main and Format keep their numbers, their object names ("mainToolbar", "formatBar") and
// their meaning as the first bar of their row; the others took over parts of them:
//   first row (the main toolbars):    Main, Themes, Search, Tools
//   second row (the format toolbars): Format (style and font), Character, Paragraph; then the icon bar (MainWindow)
// The commands Show Toolbar and Show Format Bar switch a whole row's bars as before (layouts use them); each bar has
// a switch of its own besides (AppSettings::toolbarHidden).
namespace toolbars {

enum class Bar { Main = 0, Format = 1, Themes = 2, Search = 3, Tools = 4, Character = 5, Paragraph = 6 };
constexpr int BarCount = 7;
// in the order they stand in the window (and in lists)
QList<Bar> allBars();
// Format, Character, Paragraph: the bars of Show Format Bar; they hold the controls of the text's format
bool isFormatBar(Bar bar);
QString barKey(Bar bar);          // "main", "themes", "search", "tools", "style", "character", "paragraph"
QString barName(Bar bar);         // for menus and lists: "Main", "Themes", ...
QString barObjectName(Bar bar);   // "mainToolbar", "themesBar", "searchBar", "toolsBar", "formatBar", ...
QString barCommand(Bar bar);      // the key of the bar's own switch: "showBarMain", ...

extern const QString Separator;   // "|"
extern const QString Spacer;      // "<>"

struct Special {
    QString id;
    QString label;
    QString icon;   // icon name
};
// Controls that are more than one command: the View button, the search
// field, the style list ...
QList<Special> specials(Bar bar);
bool isSpecial(const QString &id);

QStringList standard(Bar bar);
// The list in use (settings, or the standard set).
QStringList configured(Bar bar);
// 0.91.0: the lists of the two toolbars of earlier versions ("" = the standard set then) split over the seven bars,
// each item into the bar that holds it in the standard sets; an item no standard set has goes with the item before
// it. Spaces are dropped (a bar is as wide as its buttons), separators stay inside a bar. A list that comes out as
// the bar's standard set is left empty; a bar that gets nothing from a list the writer arranged is named in `hidden`
// (by its key): what was taken off the old toolbar does not come back.
struct Split {
    QStringList lists[BarCount];
    QStringList hidden;
};
Split splitFormer(const QStringList &formerMain, const QStringList &formerFormat);
// Buttons that writing tools want on the toolbar (tool interface 1.1, 0.61; on the Tools bar since 0.91.0):
// "tool:<command>" before the item `before` of the standard set (empty: at the end).
// They become part of the standard set while the tool is there.
struct ToolButton {
    QString id;
    QString before;
};
void setToolButtons(const QList<ToolButton> &buttons);
QList<ToolButton> toolButtons();
// A Tools bar the writer arranged gets each tool button once (0.61; the main toolbar until 0.90): changes the list
// and returns true when a button was added; `done` holds the buttons placed before.
bool placeToolButtonsOnce(QStringList &bar, QStringList &done);
// Items a bar can hold: commands of writing tools ("tool:...") yes, styles and layouts
// (they come and go with projects and files) no.
bool usableOnBar(const QString &id);
QString label(const QString &id);   // for lists; empty for unknown ids
QIcon icon(const QString &id);

} // namespace toolbars

// View > Customize Toolbars
class ToolbarDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ToolbarDialog(QWidget *parent = nullptr, toolbars::Bar bar = toolbars::Bar::Main);
    void accept() override;
    void apply();

    // what the buttons do (also used by the tests)
    void setBar(toolbars::Bar bar);
    QStringList items() const;
    bool addItem(const QString &id, int row = -1);   // false if it is in the bar already
    bool removeItem(const QString &id);
    bool moveItem(int row, int delta);
    void resetToStandard();

private:
    void storeCurrent();
    void fillCurrent(const QStringList &ids);
    void fillAvailable();

    toolbars::Bar m_bar = toolbars::Bar::Main;
    QStringList m_lists[toolbars::BarCount];
    QComboBox *m_barCombo, *m_iconSize, *m_style;
    QLineEdit *m_filter;
    QListWidget *m_available, *m_current;
};

} // namespace qb
