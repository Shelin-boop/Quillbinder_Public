// Settings per project (0.62); see SettingsLayer.h for what and why.
#include "core/SettingsLayer.h"
#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

namespace qb {
namespace settingslayer {

namespace {

struct State {
    bool init = false;
    bool usable = false;     // the real path of the settings file is known
    QString realDir;         // where QSettings keeps the program's file ("~/.config")
    QString rel;             // the file below that folder ("/Quillbinder/Quillbinder.conf")
    QString layerDir;        // where the merged copy is kept
    QString projectFile;
    bool active = false;
    bool suspended = false;
    bool editing = false;    // File > Options in "This project" is open: no sync
    QVariantMap base;        // the merged copy at the last sync
    QVariantMap editBase;    // the merged copy when File > Options opened in "This project"
    std::function<void()> reload;
};

State &st()
{
    static State s;
    return s;
}

void setPaths(const QString &dir)
{
    // on Unix both formats are the same INI files; the Windows build reads INI files as well
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, dir);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir);
}

void ensureInit()
{
    State &s = st();
    if (s.init)
        return;
    s.init = true;
    QString real;
    {
        QSettings probe;
        real = QFileInfo(probe.fileName()).absoluteFilePath();
    }
    s.layerDir = dataDir() + QLatin1String("/settings-layer");
    QDir().mkpath(s.layerDir);
    QString there;
    setPaths(s.layerDir);
    {
        QSettings probe;
        there = QFileInfo(probe.fileName()).absoluteFilePath();
    }
    if (there.startsWith(s.layerDir) && real.endsWith(there.mid(s.layerDir.size()))) {
        s.rel = there.mid(s.layerDir.size());
        s.realDir = real.left(real.size() - s.rel.size());
        s.usable = true;
    }
    // the folder layout is not the expected one: the project layer stays off, the standard path
    // comes back
    setPaths(s.usable ? s.realDir : QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
}

QVariantMap snapshot(QSettings &s)
{
    QVariantMap m;
    for (const QString &k : s.allKeys())
        m.insert(k, s.value(k));
    return m;
}

QVariantMap read(const QString &file)
{
    if (file.isEmpty() || !QFile::exists(file))
        return {};
    QSettings s(file, QSettings::IniFormat);
    return snapshot(s);
}

QString mergedFile() { return st().layerDir + st().rel; }

void rebuild()
{
    State &s = st();
    const bool was = s.active;
    const bool want = s.usable && !s.projectFile.isEmpty() && !s.suspended && projectFirst();
    if (want) {
        const QString merged = mergedFile();
        QDir().mkpath(QFileInfo(merged).absolutePath());
        QFile::remove(merged);
        {
            QSettings m(merged, QSettings::IniFormat);
            const QVariantMap prog = read(programFile());
            for (auto it = prog.cbegin(); it != prog.cend(); ++it)
                m.setValue(it.key(), it.value());
            const QVariantMap own = read(s.projectFile);
            for (auto it = own.cbegin(); it != own.cend(); ++it)
                if (!programOnly(it.key()))
                    m.setValue(it.key(), it.value());
            m.sync();
        }
        setPaths(s.layerDir);
        QSettings now;
        now.sync();
        s.base = snapshot(now);
        s.active = true;
    } else {
        setPaths(s.realDir);
        s.base.clear();
        s.active = false;
    }
    if ((was || s.active) && s.reload)
        s.reload();
}

} // namespace

QString fileFor(const QString &projectSettingsDir)
{
    return projectSettingsDir.isEmpty() ? QString() : projectSettingsDir + QLatin1String("/options.ini");
}

bool programOnly(const QString &key)
{
    if (!key.contains(QLatin1Char('/')))
        return true; // markers of migrations ("zoomPartsMigrated")
    static const QStringList prefixes = {
        QStringLiteral("window/"),          // window places and panels shown
        QStringLiteral("layouts/"),         // saved layouts are the writer's, for every project
        QStringLiteral("appearance/colors/"), QStringLiteral("appearance/fonts/"), // the look: settings/look.json
        QStringLiteral("quillbinderOptions/")};
    for (const QString &p : prefixes)
        if (key.startsWith(p))
            return true;
    static const QSet<QString> keys = {
        QStringLiteral("app/recent"), QStringLiteral("app/lastDir"), QStringLiteral("app/integrated"),
        QStringLiteral("app/welcomeFolder"), QStringLiteral("app/welcomeShown"), QStringLiteral("app/openLast"),
        QStringLiteral("app/portal"), QStringLiteral("app/recentCount"), QStringLiteral("app/projectFirst"),
        QStringLiteral("app/firstStartDone"), QStringLiteral("app/standard0212"), QStringLiteral("app/freezeSeconds"),
        QStringLiteral("app/keepReports"),
        // the look of a project is its settings/look.json (0.40.2); these are the look for new projects
        QStringLiteral("appearance/theme"), QStringLiteral("appearance/editorWidth"), QStringLiteral("appearance/textBorder"),
        QStringLiteral("appearance/recentIcons"),
        // markers of one-time changes
        QStringLiteral("toolbars/paraButtons0604"), QStringLiteral("toolbars/toolButtonsPlaced")};
    return keys.contains(key);
}

bool projectFirst()
{
    ensureInit();
    if (!st().usable)
        return false;
    QSettings p(programFile(), QSettings::IniFormat);
    return p.value(QStringLiteral("app/projectFirst"), true).toBool();
}

void setProjectFirst(bool on)
{
    ensureInit();
    if (!st().usable)
        return;
    sync();
    {
        QSettings p(programFile(), QSettings::IniFormat);
        p.setValue(QStringLiteral("app/projectFirst"), on);
        p.sync();
    }
    const bool was = st().active;
    rebuild();
    // the look and the values of the program's own follow the switch even when no copy changed
    if (!was && !st().active && st().reload)
        st().reload();
}

void activate(const QString &projectFile)
{
    ensureInit();
    sync();
    st().projectFile = projectFile;
    st().suspended = false;
    st().editing = false;
    rebuild();
}

QString projectFile() { return st().projectFile; }
bool active() { return st().active; }

QVariantMap projectValues()
{
    QVariantMap m = read(st().projectFile);
    for (auto it = m.begin(); it != m.end();)
        it = programOnly(it.key()) ? m.erase(it) : std::next(it);
    return m;
}

QVariantMap programValues()
{
    ensureInit();
    return read(programFile());
}

void sync()
{
    State &s = st();
    if (!s.active || s.editing)
        return;
    QSettings m;
    m.sync();
    const QVariantMap cur = snapshot(m);
    if (cur == s.base)
        return;
    const QVariantMap own = read(s.projectFile);
    QSettings p(programFile(), QSettings::IniFormat);
    QSettings o(s.projectFile, QSettings::IniFormat);
    QSet<QString> all;
    for (auto it = cur.cbegin(); it != cur.cend(); ++it)
        all.insert(it.key());
    for (auto it = s.base.cbegin(); it != s.base.cend(); ++it)
        all.insert(it.key());
    for (const QString &k : std::as_const(all)) {
        const bool inCur = cur.contains(k), inBase = s.base.contains(k);
        if (inCur == inBase && (!inCur || cur.value(k) == s.base.value(k)))
            continue;
        QSettings &target = (own.contains(k) && !programOnly(k)) ? o : p;
        if (inCur)
            target.setValue(k, cur.value(k));
        else
            target.remove(k);
    }
    p.sync();
    o.sync();
    s.base = cur;
}

void suspend()
{
    ensureInit();
    if (st().suspended)
        return;
    sync();
    st().suspended = true;
    rebuild();
}

void resume()
{
    if (!st().suspended)
        return;
    st().suspended = false;
    rebuild();
}

bool suspended() { return st().suspended; }

void beginProjectEdit()
{
    sync();
    st().editing = st().active;
    if (st().active) {
        QSettings m;
        m.sync();
        st().editBase = snapshot(m);
    }
}

void endProjectEdit() { st().editing = false; }

int takeProjectValues()
{
    State &s = st();
    if (!s.active)
        return 0;
    QSettings m;
    m.sync();
    const QVariantMap cur = snapshot(m);
    const QVariantMap prog = read(programFile());
    const QVariantMap before = read(s.projectFile);
    int n = 0;
    {
        QFile::remove(s.projectFile);
        QDir().mkpath(QFileInfo(s.projectFile).absolutePath());
        QSettings o(s.projectFile, QSettings::IniFormat);
        QSettings p(programFile(), QSettings::IniFormat);
        for (auto it = cur.cbegin(); it != cur.cend(); ++it) {
            const QString &k = it.key();
            if (programOnly(k))
                continue;
            bool own;
            if (prog.contains(k)) {
                own = prog.value(k) != it.value();
            } else {
                // a key the program file does not have yet (an option written for the first time):
                // the project's when it was before or the window changed it, else the program's
                own = before.contains(k) || (s.editBase.contains(k) && s.editBase.value(k) != it.value());
                if (!own)
                    p.setValue(k, it.value());
            }
            if (!own)
                continue;
            o.setValue(k, it.value());
            ++n;
        }
        o.sync();
        p.sync();
    }
    // what the program file holds for the keys the dialog does not show stays the program's
    s.base = cur;
    return n;
}

void clearProjectValues(const QStringList &keys)
{
    State &s = st();
    if (s.projectFile.isEmpty())
        return;
    sync();
    if (keys.isEmpty()) {
        QFile::remove(s.projectFile);
    } else {
        QSettings o(s.projectFile, QSettings::IniFormat);
        for (const QString &k : keys)
            o.remove(k);
        o.sync();
    }
    rebuild();
}

void setReloader(std::function<void()> reload) { st().reload = std::move(reload); }

QString programFile()
{
    ensureInit();
    return st().realDir + st().rel;
}

} // namespace settingslayer
} // namespace qb
