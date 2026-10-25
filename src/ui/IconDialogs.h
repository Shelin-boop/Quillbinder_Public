#pragma once
// Documents > Change Icon (0.17): Icon from Text, Manage Icons, and the
// choice of the icons of the icon bar.

#include <QDialog>
#include <QStringList>
#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;

namespace qb {

class Project;
class ColorWell;

// The icons of Documents > Change Icon as a menu, for other places that take
// an icon (0.21: the icons on mind map notes and lines): the eight groups as
// submenus, the single icons, and the project's own icons. "chosen" gets the
// icon id (as stored for binder items, see core/IconCatalog.h).
void fillIconChoiceMenu(QMenu *menu, const Project *project, const std::function<void(const QString &)> &chosen);

// A picture of up to three characters on a box; becomes an icon of the project.
class IconFromTextDialog : public QDialog
{
    Q_OBJECT
public:
    IconFromTextDialog(Project *project, QWidget *parent = nullptr);
    QString createdIcon() const { return m_created; } // "custom:<name>" after OK
    void accept() override;
    QLineEdit *textField() const { return m_text; }
    QLineEdit *nameField() const { return m_name; }

private:
    void refresh();
    Project *m_project;
    QLineEdit *m_text, *m_name;
    ColorWell *m_box, *m_ink, *m_border;
    QLabel *m_preview;
    QString m_created;
};

// The project's own icons: add pictures, make icons from text, rename, delete.
class ManageIconsDialog : public QDialog
{
    Q_OBJECT
public:
    ManageIconsDialog(Project *project, QWidget *parent = nullptr);
    QListWidget *list() const { return m_list; }
    // the icon to give the selected documents (Apply), empty when none
    QString chosen() const { return m_chosen; }
    bool addFile(const QString &path);

private:
    void reload(const QString &select = QString());
    Project *m_project;
    QListWidget *m_list;
    QString m_chosen;
};

// Up to ten icons for the icon bar.
class IconBarDialog : public QDialog
{
    Q_OBJECT
public:
    IconBarDialog(Project *project, const QStringList &current, QWidget *parent = nullptr);
    QStringList icons() const;
    QListWidget *available() const { return m_all; }
    QListWidget *chosenList() const { return m_chosen; }
    bool addIcon(const QString &id);   // false when the bar is full (10)
    static constexpr int kMax = 10;

private:
    void updateCount();
    Project *m_project;
    QListWidget *m_all, *m_chosen;
    QLabel *m_count;
};

// The standard icons of the icon bar.
QStringList standardIconBar();

} // namespace qb
