#pragma once
#include <QIcon>
#include <QImage>
#include <QString>
#include <QStringList>

namespace qb {

class Project;
class BinderItem;

namespace icons {

// Icon from the bundled set (falls back to the desktop theme).
QIcon get(const QString &name);
// Toolbar / menu icon: desktop theme first (KDE Breeze), bundled fallback.
QIcon action(const QString &name);

QIcon forItem(const Project *project, const BinderItem *item);
QString defaultIconName(const Project *project, const BinderItem *item);

struct Choice {
    QString name;
    QString label;
};
QList<Choice> customChoices();

void clearCache();
// The label color shines through the light parts of item icons (View > Use
// Label Color In > Icons, 0.17).
void setLabelTint(bool on);
bool labelTint();
// The light parts of a picture take a color (white becomes the color, black
// and strong colors stay as they are).
QImage tinted(const QImage &image, const QColor &color);
// Use the light-on-dark variant of the bundled icons. Icons handed out
// earlier follow: they look up their picture when they are painted.
void setDark(bool dark);
bool isDark();
// Toolbar and menu icons from the bundled set even when the desktop has
// icons of its own (used when the desktop's set does not fit the theme).
void setPreferBundled(bool on);
bool preferBundled();

} // namespace icons
} // namespace qb
