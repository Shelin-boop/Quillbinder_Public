#include "ui/ProjectLook.h"

#include "core/Project.h"
#include "ui/AppSettings.h"
#include "ui/Theme.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>

namespace qb {
namespace look {

static const char *const kFile = "look.json";

QJsonObject Values::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("version"), 1);
    o.insert(QStringLiteral("theme"), theme);
    QJsonObject c;
    for (auto it = colors.constBegin(); it != colors.constEnd(); ++it)
        if (it.value().isValid())
            c.insert(it.key(), it.value().name(QColor::HexRgb));
    o.insert(QStringLiteral("colors"), c);
    QJsonObject f;
    for (auto it = fonts.constBegin(); it != fonts.constEnd(); ++it)
        f.insert(it.key(), it.value());
    o.insert(QStringLiteral("fonts"), f);
    o.insert(QStringLiteral("editorWidth"), editorWidth);
    o.insert(QStringLiteral("border"), border);
    return o;
}

Values Values::fromJson(const QJsonObject &o, const Values &fallback)
{
    Values v = fallback;
    if (o.contains(QStringLiteral("theme")))
        v.theme = o.value(QStringLiteral("theme")).toString(fallback.theme);
    if (o.contains(QStringLiteral("colors"))) {
        v.colors.clear();
        const QJsonObject c = o.value(QStringLiteral("colors")).toObject();
        for (auto it = c.constBegin(); it != c.constEnd(); ++it) {
            const QColor col(it.value().toString());
            if (col.isValid() && theme::isAreaColor(it.key()))
                v.colors.insert(it.key(), col);
        }
    }
    if (o.contains(QStringLiteral("fonts"))) {
        v.fonts.clear();
        const QJsonObject f = o.value(QStringLiteral("fonts")).toObject();
        for (auto it = f.constBegin(); it != f.constEnd(); ++it)
            if (theme::isAreaFont(it.key()) && !it.value().toString().isEmpty())
                v.fonts.insert(it.key(), it.value().toString());
    }
    if (o.contains(QStringLiteral("editorWidth")))
        v.editorWidth = qBound(200, o.value(QStringLiteral("editorWidth")).toInt(fallback.editorWidth), 4000);
    if (o.contains(QStringLiteral("border")))
        v.border = qBound(0, o.value(QStringLiteral("border")).toInt(fallback.border), 200);
    return v;
}

Values Values::fromJson(const QJsonObject &o) { return fromJson(o, Values()); }

bool Values::operator==(const Values &o) const
{
    if (theme != o.theme || fonts != o.fonts || editorWidth != o.editorWidth || border != o.border
        || colors.size() != o.colors.size())
        return false;
    for (auto it = colors.constBegin(); it != colors.constEnd(); ++it)
        if (!o.colors.contains(it.key()) || o.colors.value(it.key()).rgb() != it.value().rgb())
            return false;
    return true;
}

Values forNewProjects()
{
    const AppSettings &s = AppSettings::instance();
    Values v;
    v.theme = s.theme;
    for (auto it = s.ownColors.constBegin(); it != s.ownColors.constEnd(); ++it)
        if (theme::isAreaColor(it.key()) && it.value().isValid())
            v.colors.insert(it.key(), it.value());
    for (auto it = s.ownFonts.constBegin(); it != s.ownFonts.constEnd(); ++it)
        if (theme::isAreaFont(it.key()))
            v.fonts.insert(it.key(), it.value());
    v.editorWidth = s.editorWidth;
    v.border = s.textBorder;
    return v;
}

void setForNewProjects(const Values &v)
{
    AppSettings &s = AppSettings::instance();
    s.theme = v.theme;
    for (auto it = s.ownColors.begin(); it != s.ownColors.end();)
        it = theme::isAreaColor(it.key()) ? s.ownColors.erase(it) : std::next(it);
    for (auto it = v.colors.constBegin(); it != v.colors.constEnd(); ++it)
        s.ownColors.insert(it.key(), it.value());
    for (auto it = s.ownFonts.begin(); it != s.ownFonts.end();)
        it = theme::isAreaFont(it.key()) ? s.ownFonts.erase(it) : std::next(it);
    for (auto it = v.fonts.constBegin(); it != v.fonts.constEnd(); ++it)
        s.ownFonts.insert(it.key(), it.value());
    s.editorWidth = v.editorWidth;
    s.textBorder = v.border;
}

QString fileOf(const Project *p) { return p ? QDir(p->settingsDir()).filePath(QLatin1String(kFile)) : QString(); }

bool isStored(const Project *p) { return p && QFile::exists(fileOf(p)); }

Values read(const Project *p, bool *ok)
{
    if (ok)
        *ok = false;
    QFile f(fileOf(p));
    if (!p || !f.open(QIODevice::ReadOnly))
        return forNewProjects();
    QJsonParseError err;
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !d.isObject())
        return forNewProjects();
    if (ok)
        *ok = true;
    // what the file does not say stays as the program shows it so far
    Values fallback = forNewProjects();
    fallback.border = 0;
    return Values::fromJson(d.object(), fallback);
}

bool write(const Project *p, const Values &v, QString *error)
{
    if (!p) {
        if (error)
            *error = QObject::tr("No project is open.");
        return false;
    }
    QDir().mkpath(p->settingsDir());
    QSaveFile f(fileOf(p));
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(v.toJson()).toJson()) < 0 || !f.commit()) {
        if (error)
            *error = QObject::tr("Cannot write %1: %2").arg(fileOf(p), f.errorString());
        return false;
    }
    return true;
}

Values settle(Project *p, bool isNew)
{
    if (!p)
        return forNewProjects();
    bool ok = false;
    if (isStored(p)) {
        const Values v = read(p, &ok);
        if (ok)
            return v;
    }
    Values v = forNewProjects();
    if (!isNew)
        v.border = 0; // the look it had: there was no border before 0.40.2
    write(p, v);      // a project folder that cannot be written keeps the look in memory
    return v;
}

// ------------------------------------------------------------------ in use

static QPointer<Project> s_project;
static Values s_values;
static bool s_follow = false;

static void pushToTheme()
{
    theme::AreaLook a;
    a.set = s_project != nullptr && !s_follow;
    if (a.set) {
        a.theme = s_values.theme;
        a.colors = s_values.colors;
        a.fonts = s_values.fonts;
    }
    theme::setAreaLook(a);
}

void activate(Project *p, const Values &v)
{
    s_project = p;
    s_values = p ? v : Values();
    pushToTheme();
    theme::apply();
}

Project *project() { return s_project; }

Values active() { return s_project && !s_follow ? s_values : forNewProjects(); }

void setFollowSettings(bool on)
{
    s_follow = on;
    pushToTheme();
}

bool change(const Values &v, QString *error)
{
    if (s_project && !s_follow) {
        if (!write(s_project, v, error))
            return false;
        s_values = v;
        pushToTheme();
        theme::apply();
        return true;
    }
    setForNewProjects(v);
    AppSettings::instance().notifyChanged(); // saves; the window applies the settings
    return true;
}

int editorWidth() { return active().editorWidth; }
int border() { return active().border; }

} // namespace look
} // namespace qb
