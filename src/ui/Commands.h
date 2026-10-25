#pragma once
#include <QIcon>
#include <QKeySequence>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>

class QAction;
class QMenuBar;

namespace qb {

// Every command of the program (the actions of the main window) with its
// place in the menus and its keyboard shortcut. The one place that knows
// which shortcut a command has out of the box and which one the writer gave
// it (File > Options > Keyboard). Also the catalog the toolbar editor
// offers its buttons from.
namespace commands {

struct Command {
    QString key;        // object name of the action; "style:<name>", "layout:<id>" for the lists
    QString category;   // menu the command lives in ("File", "Styles" ...)
    QString path;       // submenu inside it ("Find"), may be empty
    QKeySequence standard;
    QPointer<QAction> action;
    bool dynamic = false;
    QString label() const;      // text without "&" and "…"
    QString fullLabel() const;  // "Find ▸ Project Search"
};

// ---- registry
// Remembers the action's shortcut as the default. Later calls for the same
// key replace the entry (dynamic commands are rebuilt).
void add(QAction *action, const QString &category, const QString &path = QString(), bool dynamic = false);
// Registers every action with an object name reachable through the menu bar.
void addFromMenuBar(QMenuBar *bar);
void removeDynamic(const QString &prefix);
void clear();
QList<Command> all();               // in registration order
const Command *find(const QString &key);
QStringList categories();
// The command's icon, or an empty one of the same size so that lists line up.
QIcon listIcon(const QString &key);

// ---- shortcuts
// The writer's changes: key -> shortcut as portable text; an empty string
// means "no shortcut". Commands without an entry use their default.
QMap<QString, QString> custom();
void setCustom(const QMap<QString, QString> &map);   // stores and applies
void apply();                                        // puts the shortcuts on the actions
QKeySequence shortcutOf(const QString &key);         // with the changes
// The shortcut a command would have with the given changes.
QKeySequence effective(const Command &c, const QMap<QString, QString> &changes);
// Key of another command that has this shortcut with the given changes.
QString conflict(const QString &key, const QKeySequence &seq, const QMap<QString, QString> &changes);
// Letters and digits without Ctrl, Alt or Meta would be swallowed while writing.
bool usable(const QKeySequence &seq, QString *why = nullptr);

// Shortcut sets as files (.qbkeys, JSON).
bool exportTo(const QString &file, const QMap<QString, QString> &changes, QString *error);
bool importFrom(const QString &file, QMap<QString, QString> *changes, QString *error);

// Emits changed() after setCustom() and after the registry changed.
class Notifier : public QObject
{
    Q_OBJECT
public:
    static Notifier &instance();
signals:
    void changed();
};

} // namespace commands
} // namespace qb
