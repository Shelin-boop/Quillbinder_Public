#include "ui/OptionStorage.h"

#include "core/SettingsLayer.h"

#include <QCoreApplication>
#include <QVariant>
#include <QWidget>

namespace qb {
namespace optstore {

namespace {

const char *kKey = "qbStorageKey";
const char *kBase = "qbBaseToolTip";
const char *kWhy = "qbStorageWhy";

QString joined(const QString &base, const QString &why)
{
    if (why.isEmpty())
        return base;
    return base.isEmpty() ? why : base + QStringLiteral("\n\n") + why;
}

QString tr(const char *s) { return QCoreApplication::translate("qb::optstore", s); }

} // namespace

void mark(QWidget *control, const QString &key)
{
    if (!control || control->property(kKey).isValid())
        return;
    control->setProperty(kKey, key);
    control->setProperty(kBase, control->toolTip());
}

QString keyOf(const QWidget *control)
{
    return control ? control->property(kKey).toString() : QString();
}

bool fixedToProgram(const QString &key)
{
    if (key == QLatin1String("look"))
        return false;
    if (key.endsWith(QLatin1String("/*")))
        return settingslayer::programOnly(key.chopped(1) + QStringLiteral("x"));
    return settingslayer::programOnly(key) || settingslayer::namesProgramOrPlace(key);
}

bool projectKeeps(const QString &key)
{
    if (key == QLatin1String("look"))
        return false;
    const QVariantMap own = settingslayer::projectValues();
    if (key.endsWith(QLatin1String("/*"))) {
        const QString prefix = key.chopped(1);
        for (auto it = own.cbegin(); it != own.cend(); ++it)
            if (it.key().startsWith(prefix))
                return true;
        return false;
    }
    return own.contains(key);
}

QString placeOf(const QString &key, const State &s)
{
    if (key == QLatin1String("look"))
        return s.projectOpen && s.lookThisProject ? QStringLiteral("P") : QStringLiteral("A");
    if (fixedToProgram(key) || !s.projectOpen || !s.projectFirst || !s.thisProject)
        return QStringLiteral("A");
    return projectKeeps(key) ? QStringLiteral("P") : QStringLiteral("A");
}

QString explanation(const QString &key, const State &s)
{
    if (key == QLatin1String("look")) {
        if (!s.projectOpen)
            return tr("Part of the look: kept by the program as the look for new projects. An open project keeps a "
                      "look of its own.");
        return s.lookThisProject
                   ? tr("Part of the look: kept with this project, as chosen in \"Show and change the look of\" above.")
                   : tr("Part of the look: kept by the program as the look for new projects, as chosen in \"Show and "
                        "change the look of\" above.");
    }
    if (fixedToProgram(key))
        return tr("Fixed: kept by the program for all projects. A project cannot keep a value of its own here.");
    if (!s.projectOpen)
        return tr("Depends on the setting: kept by the program for all projects. An open project can keep a value "
                  "of its own: \"Settings for: This project\".");
    if (!s.projectFirst)
        return tr("Depends on the setting: kept by the program for all projects. Projects' own values are not used "
                  "now (General > Which settings come first).");
    const bool own = projectKeeps(key);
    if (s.thisProject)
        return own ? tr("Depends on the setting: this project keeps a value of its own, used while it is open.")
                   : tr("Depends on the setting: this project uses the value for all projects. Change it here and "
                        "press OK to keep a value of its own.");
    return own ? tr("Depends on the setting: here you change the value for all projects. This project keeps a value "
                    "of its own, which it uses while it is open.")
               : tr("Depends on the setting: kept by the program for all projects. A project can keep a value of its "
                    "own: \"Settings for: This project\".");
}

void refresh(QWidget *root, const State &s)
{
    if (!root)
        return;
    QList<QWidget *> all = root->findChildren<QWidget *>();
    for (QWidget *w : all) {
        const QString key = keyOf(w);
        if (key.isEmpty())
            continue;
        const QString why = explanation(key, s);
        w->setProperty(kWhy, why);
        w->setProperty("qbPlace", placeOf(key, s));
        w->setToolTip(joined(w->property(kBase).toString(), why));
    }
}

void setToolTip(QWidget *control, const QString &base)
{
    if (!control)
        return;
    if (keyOf(control).isEmpty()) {
        control->setToolTip(base);
        return;
    }
    control->setProperty(kBase, base);
    control->setToolTip(joined(base, control->property(kWhy).toString()));
}

} // namespace optstore
} // namespace qb
