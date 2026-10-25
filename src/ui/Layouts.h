#pragma once
#include <QDialog>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <functional>

class QLabel;
class QListWidget;
class QPushButton;

namespace qb {

// Window layouts (Window > Layouts): which parts of the main window are
// shown, how the editor is split and which view each editor uses. A layout
// says nothing about documents, so it works in every project.
//
// State keys (JSON object "state" in a layout file):
//   binder, collections, inspector, toolbar, formatBar, ruler   bool
//   inspectorTab      0..4, -1 = leave as it is
//   binderShare, inspectorShare   share of the window width (0.05 .. 0.6)
//   split             "none" | "horizontal" (one above the other) | "vertical" (side by side)
//   splitShare        share of the first editor (0.1 .. 0.9)
//   panes             [ { view: "text" | "corkboard" | "outliner" | "keep",
//                         cork: "grid" | "freeform" | "labels",
//                         affectsOther: bool } , ... ]
//   binderAffects     "current" | "first" | "second" | "other"
//   active            0 | 1
//   window            { width, height, maximized }   (optional)
namespace layouts {

enum class Kind { BuiltIn, Addon, User };

struct Layout {
    QString id;        // "default", "addon:<add-on>/<file>", "user:<file>"
    QString name;
    QString description;
    Kind kind = Kind::BuiltIn;
    QString file;      // add-on and own layouts
    QVariantMap state;
};

// Built-in layouts, the layouts of enabled add-ons, the writer's own.
QList<Layout> available();
bool load(const QString &id, Layout *out);

QString userDir();
// Writes a new layout of one's own; returns its id or an empty string.
QString saveUser(const QString &name, const QVariantMap &state, QString *error);
bool updateUser(const QString &id, const QVariantMap &state, QString *error);
bool removeUser(const QString &id, QString *error);
bool renameUser(const QString &id, const QString &newName, QString *error);
QString importLayout(const QString &file, QString *error);
bool exportLayout(const QString &id, const QString &file, QString *error);
bool nameInUse(const QString &name, const QString &exceptId = QString());
QString uniqueName(const QString &wanted);

class Notifier : public QObject
{
    Q_OBJECT
public:
    static Notifier &instance();
signals:
    void changed();   // the list of layouts changed
};

} // namespace layouts

// Small picture of a layout: binder, editors with their views, inspector.
class LayoutPreview : public QWidget
{
    Q_OBJECT
public:
    explicit LayoutPreview(QWidget *parent = nullptr);
    void setState(const QVariantMap &state);
    QSize sizeHint() const override { return QSize(320, 200); }

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    QVariantMap m_state;
};

// Window > Layouts > Manage Layouts
class LayoutsDialog : public QDialog
{
    Q_OBJECT
public:
    using Capture = std::function<QVariantMap(bool withWindowSize)>;
    using Apply = std::function<void(const QVariantMap &)>;
    LayoutsDialog(Capture capture, Apply apply, QWidget *parent = nullptr);

    // The buttons ask for names and files and then call these (also used by the tests).
    QString saveCurrentAs(const QString &name, bool withWindowSize, QString *error);
    bool updateSelected(QString *error);
    bool renameSelected(const QString &name, QString *error);
    bool removeSelected(QString *error);
    void useSelected();
    void select(const QString &id);
    QString selectedId() const;

private:
    void reload(const QString &selectId);
    void selectionChanged();

    Capture m_capture;
    Apply m_apply;
    QListWidget *m_list;
    LayoutPreview *m_preview;
    QLabel *m_info;
    QPushButton *m_use, *m_update, *m_rename, *m_delete, *m_export;
};

} // namespace qb
