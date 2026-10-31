#include "ui/LookMenu.h"

#include "core/Project.h"
#include "ui/AppSettings.h"
#include "ui/ColorPalette.h"
#include "ui/ProjectLook.h"
#include "ui/Theme.h"

#include <QAction>
#include <QActionGroup>
#include <QFontDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>

namespace qb {
namespace lookmenu {

namespace {

struct PartInfo {
    QStringList colors;
    QStringList fonts;
};

PartInfo infoOf(Part part)
{
    auto l = [](std::initializer_list<const char *> keys) {
        QStringList out;
        for (const char *k : keys)
            out << QLatin1String(k);
        return out;
    };
    switch (part) {
    case Part::Editor:
        return {l({"editorPaper", "editorText", "editorMargin", "editorPageSurround", "editorLink", "searchHit", "spellingError"}),
                l({"combinedTitle"})};
    case Part::HeaderBar:
        return {l({"headerBar", "headerBarText", "headerBarActive", "headerBarLocked"}), l({"headerTitle"})};
    case Part::Binder: return {l({"binderBackground", "binderText"}), l({"binder"})};
    case Part::Corkboard:
        return {l({"corkboardBackground", "cardPaper", "cardTitle", "cardText", "cardTitleLine", "cardLines", "statusStamp"}),
                l({"cardTitle", "cardText"})};
    case Part::Outliner: return {l({"outlinerBackground", "outlinerAlternate", "outlinerText"}), l({"outliner"})};
    case Part::Inspector:
        return {l({"inspectorBackground", "inspectorText", "synopsisPaper", "synopsisText", "notesPaper", "notesText"}),
                l({"synopsis"})};
    case Part::Composition: return {l({"compositionBackground", "compositionPaper", "compositionText"}), {}};
    }
    return {};
}

std::function<void(const QString &)> &opener()
{
    static std::function<void(const QString &)> f;
    return f;
}

QString colorLabel(const QString &key)
{
    for (const theme::RoleInfo &ri : theme::roles())
        if (key == QLatin1String(ri.key))
            return ri.label;
    return key;
}

const theme::FontInfo *fontInfo(const QString &key)
{
    for (const theme::FontInfo &fi : theme::fontRoles())
        if (key == QLatin1String(fi.key))
            return &fi;
    return nullptr;
}

// Change the look in use (the open project's, or the one for new projects) and tell the
// writer when the project's file could not be written.
void changeLook(const std::function<void(look::Values &)> &edit, QWidget *parent)
{
    look::Values v = look::active();
    edit(v);
    if (v == look::active())
        return;
    QString err;
    if (!look::change(v, &err))
        QMessageBox::warning(parent, QObject::tr("Appearance"), err);
}

// A submenu of sizes in px with "Other..."
void sizeMenu(QMenu *menu, const QString &title, const QString &objectName, int current, const QList<int> &choices, int lo,
              int hi, const std::function<void(int)> &set, QWidget *parent)
{
    QMenu *m = menu->addMenu(QStringLiteral("%1 (%2 px)").arg(title).arg(current));
    m->setObjectName(objectName);
    auto *group = new QActionGroup(m);
    bool known = false;
    for (int px : choices) {
        QAction *a = m->addAction(QObject::tr("%1 px").arg(px));
        a->setCheckable(true);
        a->setChecked(px == current);
        known = known || px == current;
        group->addAction(a);
        QObject::connect(a, &QAction::triggered, m, [set, px] { set(px); });
    }
    m->addSeparator();
    QAction *other = m->addAction(known ? QObject::tr("Other...") : QObject::tr("Other (%1 px)...").arg(current));
    other->setCheckable(!known);
    other->setChecked(!known);
    QPointer<QWidget> p(parent);
    QObject::connect(other, &QAction::triggered, m, [set, title, current, lo, hi, p] {
        bool ok = false;
        const int v = QInputDialog::getInt(p, title, QObject::tr("%1 in px (at 100 % zoom):").arg(title), current, lo, hi, 1, &ok);
        if (ok)
            set(v);
    });
}

} // namespace

QString optionsPage(Part part)
{
    switch (part) {
    case Part::Editor:
    case Part::HeaderBar: return QStringLiteral("Main Editor");
    case Part::Binder: return QStringLiteral("Binder");
    case Part::Corkboard: return QStringLiteral("Corkboard");
    case Part::Outliner: return QStringLiteral("Outliner");
    case Part::Inspector: return QStringLiteral("Inspector & Notes");
    case Part::Composition: return QStringLiteral("Composition Mode");
    }
    return QString();
}

void setOptionsOpener(const std::function<void(const QString &)> &f) { opener() = f; }

void fill(QMenu *menu, Part part, QWidget *parent)
{
    const PartInfo info = infoOf(part);
    const look::Values v = look::active();
    const bool project = look::project() != nullptr;
    QPointer<QWidget> p(parent);

    // who the changes are for
    QAction *whose = menu->addAction(project ? QObject::tr("Look of this project") : QObject::tr("Look of new projects"));
    whose->setEnabled(false);
    whose->setObjectName(QStringLiteral("lookWhose"));

    // colors: the shared palette, the theme's color, "Other..."
    for (const QString &key : info.colors) {
        if (key == QLatin1String("editorPageSurround") && project && !look::project()->uiState.value(QStringLiteral("pageView")).toBool())
            continue; // the color around the pages only matters in page view
        const QColor shown = theme::color([&] {
            theme::Role r = theme::Role::EditorPaper;
            theme::roleFromKey(key, &r);
            return r;
        }());
        const bool own = v.colors.contains(key);
        QMenu *m = menu->addMenu(palette::swatch(shown), colorLabel(key) + (own ? QObject::tr(" (own color)") : QString()));
        m->setObjectName(QStringLiteral("lookColor_") + key);
        palette::fillMenu(
            m, look::project(), shown,
            [key, p](const QColor &c) {
                if (!c.isValid())
                    return;
                changeLook([&](look::Values &lv) { lv.colors.insert(key, QColor(c.red(), c.green(), c.blue())); }, p);
            },
            parent, QString(), colorLabel(key));
        m->addSeparator();
        QAction *reset = m->addAction(QObject::tr("The Theme's Color"));
        reset->setObjectName(QStringLiteral("lookColorReset_") + key);
        reset->setEnabled(own);
        QObject::connect(reset, &QAction::triggered, m, [key, p] { changeLook([&](look::Values &lv) { lv.colors.remove(key); }, p); });
    }

    // fonts
    for (const QString &key : info.fonts) {
        const theme::FontInfo *fi = fontInfo(key);
        if (!fi)
            continue;
        const bool own = v.fonts.contains(key);
        QMenu *m = menu->addMenu(QObject::tr("Font: %1").arg(fi->label) + (own ? QObject::tr(" (own font)") : QString()));
        m->setObjectName(QStringLiteral("lookFont_") + key);
        const theme::FontRole role = fi->role;
        QObject::connect(m->addAction(QObject::tr("Choose Font...")), &QAction::triggered, m, [key, role, p] {
            bool ok = false;
            const QFont chosen = QFontDialog::getFont(&ok, theme::font(role), p, QObject::tr("Font"));
            if (!ok)
                return;
            const theme::FontSpec spec = theme::FontSpec::fromFont(chosen, theme::baseFont(role));
            changeLook(
                [&](look::Values &lv) {
                    if (spec.isSet())
                        lv.fonts.insert(key, spec.toString());
                    else
                        lv.fonts.remove(key);
                },
                p);
        });
        QAction *reset = m->addAction(QObject::tr("The Theme's Font"));
        reset->setObjectName(QStringLiteral("lookFontReset_") + key);
        reset->setEnabled(own);
        QObject::connect(reset, &QAction::triggered, m, [key, p] { changeLook([&](look::Values &lv) { lv.fonts.remove(key); }, p); });
    }

    // sizes and switches of the part
    AppSettings &s = AppSettings::instance();
    auto programToggle = [&](const QString &text, const QString &name, bool on, const std::function<void(bool)> &set) {
        QAction *a = menu->addAction(text);
        a->setObjectName(name);
        a->setCheckable(true);
        a->setChecked(on);
        QObject::connect(a, &QAction::toggled, a, [set](bool b) {
            set(b);
            AppSettings::instance().notifyChanged();
        });
    };
    switch (part) {
    case Part::Editor:
        menu->addSeparator();
        sizeMenu(menu, QObject::tr("Text Column Width"), QStringLiteral("lookEditorWidth"), v.editorWidth,
                 {400, 450, 500, 550, 600, 650, 700, 800}, 200, 4000,
                 [p](int px) { changeLook([&](look::Values &lv) { lv.editorWidth = px; }, p); }, parent);
        sizeMenu(menu, QObject::tr("Border Beside the Text"), QStringLiteral("lookBorder"), v.border, {0, 5, 10, 15, 20, 30, 40},
                 0, 200, [p](int px) { changeLook([&](look::Values &lv) { lv.border = px; }, p); }, parent);
        programToggle(QObject::tr("Fixed Width (Text as a Centered Column)"), QStringLiteral("lookFixedWidth"), s.fixedWidth,
                      [](bool b) { AppSettings::instance().fixedWidth = b; });
        break;
    case Part::Binder:
        menu->addSeparator();
        programToggle(QObject::tr("Show Label Colors in the Binder"), QStringLiteral("lookLabelBinder"), s.labelColorInBinder,
                      [](bool b) { AppSettings::instance().labelColorInBinder = b; });
        programToggle(QObject::tr("Let the Label Color Shine Through the Icons"), QStringLiteral("lookLabelIcons"),
                      s.labelColorInIcons, [](bool b) { AppSettings::instance().labelColorInIcons = b; });
        sizeMenu(menu, QObject::tr("Indent per Level"), QStringLiteral("lookBinderIndent"), s.binderIndent, {8, 10, 12, 16, 20}, 4,
                 40,
                 [](int px) {
                     AppSettings::instance().binderIndent = px;
                     AppSettings::instance().notifyChanged();
                 },
                 parent);
        break;
    case Part::Corkboard:
        menu->addSeparator();
        programToggle(QObject::tr("Show Status Stamps"), QStringLiteral("lookStamps"), s.showStatusStamps,
                      [](bool b) { AppSettings::instance().showStatusStamps = b; });
        programToggle(QObject::tr("Tint Cards with the Label Color"), QStringLiteral("lookTint"), s.tintCardsWithLabel,
                      [](bool b) { AppSettings::instance().tintCardsWithLabel = b; });
        sizeMenu(menu, QObject::tr("Card Width"), QStringLiteral("lookCardWidth"), s.cardWidth, {160, 180, 210, 240, 280, 320}, 120,
                 480,
                 [](int px) {
                     AppSettings::instance().cardWidth = px;
                     AppSettings::instance().notifyChanged();
                 },
                 parent);
        break;
    default: break;
    }

    menu->addSeparator();
    QAction *more = menu->addAction(QObject::tr("Appearance Options..."));
    more->setObjectName(QStringLiteral("lookOptions"));
    const QString page = optionsPage(part);
    QObject::connect(more, &QAction::triggered, menu, [page] {
        if (opener())
            opener()(page);
    });
}

QMenu *add(QMenu *menu, Part part, QWidget *parent)
{
    menu->addSeparator();
    QMenu *m = menu->addMenu(QIcon::fromTheme(QStringLiteral("preferences-desktop-color")), QObject::tr("Appearance"));
    m->setObjectName(QStringLiteral("lookMenu"));
    fill(m, part, parent);
    return m;
}

void popup(Part part, const QPoint &globalPos, QWidget *parent)
{
    auto *menu = new QMenu(parent);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setObjectName(QStringLiteral("lookPopup"));
    fill(menu, part, parent);
    menu->popup(globalPos);
}

} // namespace lookmenu
} // namespace qb
