#pragma once
// The style list of the format bar and of the right-click menus (0.40.3): the entries the
// writer chose, in the chosen order (Project::styleListOrder / styleListHidden); more than ten
// scroll in a list of ten rows, with the style commands fixed below it.

#include <QComboBox>
#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class QListWidget;
class QMenu;

namespace qb {

class Project;
struct Formatting;

namespace stylelist {

constexpr int kVisibleRows = 10;

struct Entry {
    QString name;          // empty = No Style
    QString text;          // with the marker ("¶a Heading 1")
    bool character = false;
    bool shown = true;
};

// Every entry in the chosen order (shown or not); p may be null (the built-in styles).
QList<Entry> all(const Project *p);
// The entries the list shows.
QList<Entry> shown(const Project *p);

// Adds the entries to the menu: up to ten as plain entries, more in a list that shows ten
// rows and scrolls. current: the names to mark ("" = No Style); apply gets the chosen name.
// Returns the list widget, or null when plain entries were added.
QListWidget *addTo(QMenu *menu, const QList<Entry> &entries, const Formatting &f, const QStringList &current,
                   const std::function<void(const QString &)> &apply);

} // namespace stylelist

// Format > Style > Customize Style List: which entries the list shows, and in what order.
class StyleListDialog : public QDialog
{
    Q_OBJECT
public:
    explicit StyleListDialog(const Project *p, QWidget *parent = nullptr);
    QStringList order() const;   // all names ("-" = No Style)
    QStringList hidden() const;

private:
    void fill(const QList<stylelist::Entry> &entries);
    void move(int delta);
    QListWidget *m_list;
    const Project *m_project;
};

// The format bar's style list: a combo box whose popup is the style list (with its commands)
// instead of Qt's list. The items stay in the combo box, so it still shows the style of the
// selection, also one that the list leaves out.
class StyleCombo : public QComboBox
{
    Q_OBJECT
public:
    using QComboBox::QComboBox;
    void showPopup() override;
    void hidePopup() override;
    // fills the popup menu (the window does: it knows the styles and the commands)
    std::function<void(QMenu *)> fillPopup;

private:
    QMenu *m_popup = nullptr;
};

} // namespace qb
