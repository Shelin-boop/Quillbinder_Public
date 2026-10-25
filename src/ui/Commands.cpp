#include "ui/Commands.h"
#include "AppInfo.h"

#include <QAction>
#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QSettings>

namespace qb {
namespace commands {

namespace {

struct Registry {
    QList<Command> list;
    QHash<QString, int> index;
    QMap<QString, QString> custom;
    bool loaded = false;

    void reindex()
    {
        index.clear();
        for (int i = 0; i < list.size(); ++i)
            index.insert(list[i].key, i);
    }
    void load()
    {
        if (loaded)
            return;
        loaded = true;
        QSettings s;
        const QJsonObject o = QJsonDocument::fromJson(s.value(QStringLiteral("keyboard/shortcuts")).toByteArray()).object();
        for (auto it = o.begin(); it != o.end(); ++it)
            custom.insert(it.key(), it.value().toString());
    }
};

Registry &reg()
{
    static Registry r;
    r.load();
    return r;
}

QString plain(QString t)
{
    // "&Find…\tCtrl+F" -> "Find"
    const int tab = int(t.indexOf(QLatin1Char('\t')));
    if (tab >= 0)
        t.truncate(tab);
    t.replace(QLatin1String("&&"), QString(QChar(1)));
    t.remove(QLatin1Char('&'));
    t.replace(QChar(1), QLatin1Char('&'));
    if (t.endsWith(QChar(0x2026)))
        t.chop(1);
    return t.trimmed();
}

void walk(QMenu *menu, const QString &category, const QString &path)
{
    for (QAction *a : menu->actions()) {
        if (a->isSeparator())
            continue;
        if (QMenu *sub = a->menu()) {
            const QString title = plain(sub->title());
            walk(sub, category, path.isEmpty() ? title : path + QStringLiteral(" ▸ ") + title);
            continue;
        }
        // an entry that stands for a command under a shorter text (see MainWindow::menuAlias)
        if (auto *original = qobject_cast<QAction *>(a->property("qbAliasOf").value<QObject *>()))
            a = original;
        // lists that change (styles, layouts) are registered by their owner
        if (a->objectName().isEmpty() || a->objectName().contains(QLatin1Char(':')) || reg().index.contains(a->objectName()))
            continue;
        add(a, category, path);
    }
}

} // namespace

QString Command::label() const { return action ? plain(action->text()) : key; }

QString Command::fullLabel() const
{
    return path.isEmpty() ? label() : path + QStringLiteral(" ▸ ") + label();
}

void add(QAction *action, const QString &category, const QString &path, bool dynamic)
{
    if (!action || action->objectName().isEmpty())
        return;
    Registry &r = reg();
    Command c;
    c.key = action->objectName();
    c.category = category;
    c.path = path;
    c.standard = action->shortcut();
    c.action = action;
    c.dynamic = dynamic;
    const auto it = r.index.constFind(c.key);
    if (it != r.index.constEnd()) {
        r.list[*it] = c;
    } else {
        r.index.insert(c.key, int(r.list.size()));
        r.list.append(c);
    }
}

void addFromMenuBar(QMenuBar *bar)
{
    for (QAction *a : bar->actions())
        if (QMenu *m = a->menu())
            walk(m, plain(m->title()), QString());
}

void removeDynamic(const QString &prefix)
{
    Registry &r = reg();
    for (int i = int(r.list.size()) - 1; i >= 0; --i)
        if (r.list[i].dynamic && r.list[i].key.startsWith(prefix))
            r.list.removeAt(i);
    r.reindex();
}

void clear()
{
    reg().list.clear();
    reg().index.clear();
}

QList<Command> all() { return reg().list; }

const Command *find(const QString &key)
{
    Registry &r = reg();
    const auto it = r.index.constFind(key);
    return it == r.index.constEnd() ? nullptr : &r.list[*it];
}

QStringList categories()
{
    QStringList out;
    for (const Command &c : reg().list)
        if (!out.contains(c.category))
            out << c.category;
    return out;
}

QIcon listIcon(const QString &key)
{
    if (const Command *c = find(key))
        if (c->action && !c->action->icon().isNull())
            return c->action->icon();
    static QIcon blank;
    if (blank.isNull()) {
        QPixmap pm(22, 22);
        pm.fill(Qt::transparent);
        blank = QIcon(pm);
    }
    return blank;
}

QMap<QString, QString> custom() { return reg().custom; }

QKeySequence effective(const Command &c, const QMap<QString, QString> &changes)
{
    const auto it = changes.constFind(c.key);
    if (it == changes.constEnd())
        return c.standard;
    return QKeySequence(*it, QKeySequence::PortableText);
}

void apply()
{
    Registry &r = reg();
    for (const Command &c : r.list)
        if (c.action)
            c.action->setShortcut(effective(c, r.custom));
}

void setCustom(const QMap<QString, QString> &map)
{
    Registry &r = reg();
    r.custom.clear();
    // keep only real changes; entries of commands that are not registered
    // right now (styles of another project) stay as they are
    for (auto it = map.begin(); it != map.end(); ++it) {
        const Command *c = find(it.key());
        if (c && c->standard.toString(QKeySequence::PortableText) == it.value())
            continue;
        r.custom.insert(it.key(), it.value());
    }
    QJsonObject o;
    for (auto it = r.custom.begin(); it != r.custom.end(); ++it)
        o.insert(it.key(), it.value());
    QSettings s;
    s.setValue(QStringLiteral("keyboard/shortcuts"), QJsonDocument(o).toJson(QJsonDocument::Compact));
    apply();
    emit Notifier::instance().changed();
}

QKeySequence shortcutOf(const QString &key)
{
    if (const Command *c = find(key))
        return effective(*c, reg().custom);
    const auto it = reg().custom.constFind(key);
    return it == reg().custom.constEnd() ? QKeySequence() : QKeySequence(*it, QKeySequence::PortableText);
}

QString conflict(const QString &key, const QKeySequence &seq, const QMap<QString, QString> &changes)
{
    if (seq.isEmpty())
        return QString();
    for (const Command &c : reg().list)
        if (c.key != key && effective(c, changes) == seq)
            return c.key;
    return QString();
}

bool usable(const QKeySequence &seq, QString *why)
{
    if (seq.isEmpty())
        return true;
    const QKeyCombination kc = seq[0];
    const Qt::KeyboardModifiers mods = kc.keyboardModifiers() & ~Qt::KeypadModifier;
    const int key = kc.key();
    const bool strong = mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const bool functionKey = key >= Qt::Key_F1 && key <= Qt::Key_F35;
    if (strong || functionKey)
        return true;
    if (why)
        *why = QCoreApplication::translate("qb::commands",
                                           "A shortcut needs Ctrl, Alt or Meta (or a function key): “%1” alone is needed for writing.")
                   .arg(seq.toString(QKeySequence::NativeText));
    return false;
}

bool exportTo(const QString &file, const QMap<QString, QString> &changes, QString *error)
{
    QJsonObject keys;
    for (auto it = changes.begin(); it != changes.end(); ++it)
        keys.insert(it.key(), it.value());
    QJsonObject o;
    o.insert(QStringLiteral("format"), QStringLiteral("qbkeys"));
    o.insert(QStringLiteral("version"), 1);
    o.insert(QStringLiteral("program"), QStringLiteral(QB_APP_VERSION));
    o.insert(QStringLiteral("shortcuts"), keys);
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(QJsonDocument(o).toJson()) < 0) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

bool importFrom(const QString &file, QMap<QString, QString> *changes, QString *error)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.value(QStringLiteral("format")).toString() != QLatin1String("qbkeys")
        || !o.value(QStringLiteral("shortcuts")).isObject()) {
        if (error)
            *error = QCoreApplication::translate("qb::commands", "This is not a file with keyboard shortcuts.");
        return false;
    }
    changes->clear();
    const QJsonObject keys = o.value(QStringLiteral("shortcuts")).toObject();
    for (auto it = keys.begin(); it != keys.end(); ++it) {
        const QString text = it.value().toString();
        // unreadable entries are skipped rather than turned into "no shortcut"
        if (!text.isEmpty() && QKeySequence(text, QKeySequence::PortableText).isEmpty())
            continue;
        changes->insert(it.key(), text);
    }
    return true;
}

Notifier &Notifier::instance()
{
    static Notifier n;
    return n;
}

} // namespace commands
} // namespace qb
