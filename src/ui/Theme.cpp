#include "ui/Theme.h"

#include "core/Addons.h"
#include "core/ItemIcons.h"
#include "ui/AppSettings.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QEvent>
#include <QSaveFile>
#include <QTimer>
#include <QWidget>

#include <array>

namespace qb {
namespace theme {

// ------------------------------------------------------------------ tables

const QList<RoleInfo> &roles()
{
    static const QList<RoleInfo> l = {
        {Role::Window, "window", QObject::tr("Window and panels"), Area::General},
        {Role::WindowText, "windowText", QObject::tr("Text on panels"), Area::General},
        {Role::Base, "base", QObject::tr("Lists and text fields"), Area::General},
        {Role::AlternateBase, "alternateBase", QObject::tr("Alternate list rows"), Area::General},
        {Role::Text, "text", QObject::tr("Text in lists and fields"), Area::General},
        {Role::Button, "button", QObject::tr("Buttons"), Area::General},
        {Role::ButtonText, "buttonText", QObject::tr("Text on buttons"), Area::General},
        {Role::Highlight, "highlight", QObject::tr("Selection and accent"), Area::General},
        {Role::HighlightedText, "highlightedText", QObject::tr("Selected text"), Area::General},
        {Role::Link, "link", QObject::tr("Links in the interface"), Area::General},
        {Role::ToolTipBase, "toolTipBase", QObject::tr("Tool tips"), Area::General},
        {Role::ToolTipText, "toolTipText", QObject::tr("Text of tool tips"), Area::General},
        {Role::Mid, "mid", QObject::tr("Borders"), Area::General},
        {Role::Dark, "dark", QObject::tr("Dark edges"), Area::General},
        {Role::Light, "light", QObject::tr("Light edges"), Area::General},
        {Role::DisabledText, "disabledText", QObject::tr("Unavailable text"), Area::General},
        {Role::TargetSession, "targetSession", QObject::tr("Session target bar"), Area::General},
        {Role::TargetDraft, "targetDraft", QObject::tr("Draft target bar"), Area::General},
        {Role::TargetReached, "targetReached", QObject::tr("Target reached"), Area::General},

        {Role::BinderBackground, "binderBackground", QObject::tr("Background"), Area::Binder},
        {Role::BinderText, "binderText", QObject::tr("Titles"), Area::Binder},

        {Role::EditorPaper, "editorPaper", QObject::tr("Paper"), Area::Editor},
        {Role::EditorText, "editorText", QObject::tr("Text"), Area::Editor},
        {Role::EditorMargin, "editorMargin", QObject::tr("Beside the text column"), Area::Editor},
        {Role::EditorPageSurround, "editorPageSurround", QObject::tr("Around the pages (page view)"), Area::Editor},
        {Role::EditorLink, "editorLink", QObject::tr("Links in the text"), Area::Editor},
        {Role::SearchHit, "searchHit", QObject::tr("Search hits"), Area::Editor},
        {Role::SpellingError, "spellingError", QObject::tr("Spelling underline"), Area::Editor},
        {Role::HeaderBar, "headerBar", QObject::tr("Header bar"), Area::Editor},
        {Role::HeaderBarText, "headerBarText", QObject::tr("Header bar title"), Area::Editor},
        {Role::HeaderBarActive, "headerBarActive", QObject::tr("Header bar, active editor"), Area::Editor},
        {Role::HeaderBarLocked, "headerBarLocked", QObject::tr("Header bar, locked in place"), Area::Editor},

        {Role::CorkboardBackground, "corkboardBackground", QObject::tr("Corkboard"), Area::Corkboard},
        {Role::CardPaper, "cardPaper", QObject::tr("Index card"), Area::Corkboard},
        {Role::CardTitle, "cardTitle", QObject::tr("Card title"), Area::Corkboard},
        {Role::CardText, "cardText", QObject::tr("Card text"), Area::Corkboard},
        {Role::CardTitleLine, "cardTitleLine", QObject::tr("Line under the title"), Area::Corkboard},
        {Role::CardLines, "cardLines", QObject::tr("Ruled lines"), Area::Corkboard},
        {Role::StatusStamp, "statusStamp", QObject::tr("Status stamp"), Area::Corkboard},

        {Role::OutlinerBackground, "outlinerBackground", QObject::tr("Background"), Area::Outliner},
        {Role::OutlinerAlternate, "outlinerAlternate", QObject::tr("Alternate rows"), Area::Outliner},
        {Role::OutlinerText, "outlinerText", QObject::tr("Text"), Area::Outliner},

        {Role::InspectorBackground, "inspectorBackground", QObject::tr("Background"), Area::Inspector},
        {Role::InspectorText, "inspectorText", QObject::tr("Labels"), Area::Inspector},
        {Role::SynopsisPaper, "synopsisPaper", QObject::tr("Synopsis card"), Area::Inspector},
        {Role::SynopsisText, "synopsisText", QObject::tr("Synopsis text"), Area::Inspector},
        {Role::NotesPaper, "notesPaper", QObject::tr("Notes"), Area::Inspector},
        {Role::NotesText, "notesText", QObject::tr("Notes text"), Area::Inspector},

        {Role::CompositionBackground, "compositionBackground", QObject::tr("Background"), Area::Composition},
        {Role::CompositionPaper, "compositionPaper", QObject::tr("Paper"), Area::Composition},
        {Role::CompositionText, "compositionText", QObject::tr("Text"), Area::Composition},
    };
    return l;
}

const QList<FontInfo> &fontRoles()
{
    static const QList<FontInfo> l = {
        {FontRole::Interface, "interface", QObject::tr("Menus, buttons and panels"), Area::General},
        {FontRole::Binder, "binder", QObject::tr("Binder"), Area::Binder},
        {FontRole::HeaderTitle, "headerTitle", QObject::tr("Header bar title"), Area::Editor},
        {FontRole::CombinedTitle, "combinedTitle", QObject::tr("Titles in Combined Text"), Area::Editor},
        {FontRole::CardTitle, "cardTitle", QObject::tr("Card titles"), Area::Corkboard},
        {FontRole::CardText, "cardText", QObject::tr("Card text"), Area::Corkboard},
        {FontRole::Outliner, "outliner", QObject::tr("Outliner"), Area::Outliner},
        {FontRole::Synopsis, "synopsis", QObject::tr("Synopsis"), Area::Inspector},
    };
    return l;
}

QString areaName(Area a)
{
    switch (a) {
    case Area::General: return QObject::tr("General");
    case Area::Binder: return QObject::tr("Binder");
    case Area::Editor: return QObject::tr("Main Editor");
    case Area::Corkboard: return QObject::tr("Corkboard");
    case Area::Outliner: return QObject::tr("Outliner");
    case Area::Inspector: return QObject::tr("Inspector");
    case Area::Composition: return QObject::tr("Composition Mode");
    case Area::Count: break;
    }
    return QString();
}

const char *key(Role r)
{
    for (const RoleInfo &i : roles())
        if (i.role == r)
            return i.key;
    return "";
}

const char *key(FontRole r)
{
    for (const FontInfo &i : fontRoles())
        if (i.role == r)
            return i.key;
    return "";
}

bool roleFromKey(const QString &k, Role *out)
{
    for (const RoleInfo &i : roles())
        if (k == QLatin1String(i.key)) {
            *out = i.role;
            return true;
        }
    return false;
}

bool fontRoleFromKey(const QString &k, FontRole *out)
{
    // the name settings and theme files before 0.40 used for the titles in combined text
    if (k == QLatin1String("scriveningsTitle")) {
        *out = FontRole::CombinedTitle;
        return true;
    }
    for (const FontInfo &i : fontRoles())
        if (k == QLatin1String(i.key)) {
            *out = i.role;
            return true;
        }
    return false;
}

// ------------------------------------------------------------------ fonts

QFont FontSpec::applied(QFont base) const
{
    if (!family.isEmpty())
        base.setFamily(family);
    if (size > 0)
        base.setPointSizeF(size);
    if (bold)
        base.setBold(true);
    if (italic)
        base.setItalic(true);
    return base;
}

QString FontSpec::toString() const
{
    if (!isSet())
        return QString();
    return QStringLiteral("%1|%2|%3|%4")
        .arg(family, QString::number(size, 'g', 6), bold ? QStringLiteral("b") : QString(),
             italic ? QStringLiteral("i") : QString());
}

FontSpec FontSpec::fromString(const QString &s)
{
    FontSpec f;
    const QStringList parts = s.split(QLatin1Char('|'));
    if (parts.size() < 2)
        return f;
    f.family = parts.at(0);
    f.size = qBound(0.0, parts.at(1).toDouble(), 200.0);
    f.bold = parts.value(2) == QLatin1String("b");
    f.italic = parts.value(3) == QLatin1String("i");
    return f;
}

FontSpec FontSpec::fromFont(const QFont &f, const QFont &base)
{
    FontSpec s;
    if (f.family() != base.family())
        s.family = f.family();
    if (qAbs(f.pointSizeF() - base.pointSizeF()) > 0.05)
        s.size = f.pointSizeF();
    s.bold = f.bold() && !base.bold();
    s.italic = f.italic() && !base.italic();
    return s;
}

// ------------------------------------------------------------------ theme files

QJsonObject Theme::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("name"), name);
    if (followDesktop)
        o.insert(QStringLiteral("followDesktop"), true);
    QJsonObject c;
    for (const RoleInfo &i : roles()) // file order = order of the options
        if (colors.contains(i.role) && colors.value(i.role).isValid())
            c.insert(QLatin1String(i.key), colors.value(i.role).name(QColor::HexRgb));
    o.insert(QStringLiteral("colors"), c);
    QJsonObject fo;
    for (auto it = fonts.constBegin(); it != fonts.constEnd(); ++it) {
        if (!it.value().isSet())
            continue;
        QJsonObject f;
        if (!it.value().family.isEmpty())
            f.insert(QStringLiteral("family"), it.value().family);
        if (it.value().size > 0)
            f.insert(QStringLiteral("size"), it.value().size);
        if (it.value().bold)
            f.insert(QStringLiteral("bold"), true);
        if (it.value().italic)
            f.insert(QStringLiteral("italic"), true);
        fo.insert(QLatin1String(key(it.key())), f);
    }
    if (!fo.isEmpty())
        o.insert(QStringLiteral("fonts"), fo);
    return o;
}

bool Theme::fromJson(const QJsonObject &o, Theme *out)
{
    Theme t;
    t.name = o.value(QStringLiteral("name")).toString().trimmed();
    t.followDesktop = o.value(QStringLiteral("followDesktop")).toBool();
    const QJsonObject c = o.value(QStringLiteral("colors")).toObject();
    for (auto it = c.constBegin(); it != c.constEnd(); ++it) {
        Role r;
        const QColor col(it.value().toString());
        if (roleFromKey(it.key(), &r) && col.isValid())
            t.colors.insert(r, QColor(col.red(), col.green(), col.blue()));
    }
    const QJsonObject fo = o.value(QStringLiteral("fonts")).toObject();
    for (auto it = fo.constBegin(); it != fo.constEnd(); ++it) {
        FontRole r;
        if (!fontRoleFromKey(it.key(), &r))
            continue;
        const QJsonObject f = it.value().toObject();
        FontSpec s;
        s.family = f.value(QStringLiteral("family")).toString();
        s.size = qBound(0.0, f.value(QStringLiteral("size")).toDouble(), 200.0);
        s.bold = f.value(QStringLiteral("bold")).toBool();
        s.italic = f.value(QStringLiteral("italic")).toBool();
        if (s.isSet())
            t.fonts.insert(r, s);
    }
    if (!o.contains(QStringLiteral("colors")) && !o.contains(QStringLiteral("fonts")))
        return false;
    // a theme with colors of its own needs at least the window and its text
    if (!t.followDesktop && (!t.colors.contains(Role::Window) || !t.colors.contains(Role::Text))) {
        if (t.colors.contains(Role::Window) || t.colors.contains(Role::Text) || t.colors.contains(Role::Base))
            return false;
        t.followDesktop = true;
    }
    *out = t;
    return true;
}

// ------------------------------------------------------------------ built-in themes

static const char *kBuiltIn[][2] = {
    {"system", R"json({ "name": "Default (Follow the Desktop)", "followDesktop": true, "colors": {} })json"},
    {"light", R"json({ "name": "Light", "colors": {
        "window": "#eff0f1", "windowText": "#232629", "base": "#fcfcfc", "alternateBase": "#eff0f1",
        "text": "#232629", "button": "#eff0f1", "buttonText": "#232629", "highlight": "#3daee9",
        "highlightedText": "#ffffff", "toolTipBase": "#f7f7f7", "toolTipText": "#232629" } })json"},
    {"dark", R"json({ "name": "Dark Mode", "colors": {
        "window": "#2a2e32", "windowText": "#ebebeb", "base": "#1f2225", "alternateBase": "#262a2e",
        "text": "#e6e6e6", "button": "#31363b", "buttonText": "#ebebeb", "highlight": "#3daee9",
        "highlightedText": "#ffffff", "link": "#6eb4f0", "toolTipBase": "#31363b", "toolTipText": "#ebebeb",
        "mid": "#464b50", "dark": "#191c1e", "light": "#50565c", "disabledText": "#787878" } })json"},
    {"sepia", R"json({ "name": "Sepia", "colors": {
        "window": "#ece3d0", "windowText": "#3c2d1e", "base": "#faf3e4", "alternateBase": "#f0e8d6",
        "text": "#3c2d1e", "button": "#e6dcc6", "buttonText": "#3c2d1e", "highlight": "#b2783c",
        "highlightedText": "#ffffff", "link": "#8a4f1d",
        "corkboardBackground": "#c2a171", "cardPaper": "#fcf7ea", "synopsisPaper": "#fcf7ea" } })json"},
    {"slate", R"json({ "name": "Slate", "colors": {
        "window": "#d5d8dc", "windowText": "#1f2328", "base": "#f4f5f6", "alternateBase": "#e8eaec",
        "text": "#1f2328", "button": "#c9cdd2", "buttonText": "#1f2328", "highlight": "#5a7a99",
        "highlightedText": "#ffffff", "link": "#2f5f8f",
        "binderBackground": "#e2e5e8", "editorPaper": "#ffffff", "editorMargin": "#b9bec4",
        "headerBar": "#c9cdd2", "corkboardBackground": "#8e959c", "cardPaper": "#fdfdfc" } })json"},
    {"midnight", R"json({ "name": "Midnight", "colors": {
        "window": "#1b2233", "windowText": "#d8deea", "base": "#141a28", "alternateBase": "#19202f",
        "text": "#dfe5f0", "button": "#253049", "buttonText": "#d8deea", "highlight": "#d9a441",
        "highlightedText": "#1b1b1b", "link": "#8fb8f0", "toolTipBase": "#253049", "toolTipText": "#dfe5f0",
        "mid": "#34405c", "dark": "#0e1320", "light": "#46557a", "disabledText": "#6f7b94",
        "binderBackground": "#171d2b", "editorPaper": "#10151f", "editorText": "#d5dbe6",
        "corkboardBackground": "#232c40", "cardPaper": "#2c3650", "cardTitle": "#f0f3f8", "cardText": "#cfd6e3",
        "cardLines": "#5b6c93", "synopsisPaper": "#2c3650", "synopsisText": "#e3e8f1",
        "compositionBackground": "#0b0e15", "compositionPaper": "#141a28", "compositionText": "#dfe5f0" } })json"},
    {"ocean", R"json({ "name": "Ocean", "colors": {
        "window": "#dfeaf0", "windowText": "#173042", "base": "#f6fafc", "alternateBase": "#e9f1f5",
        "text": "#173042", "button": "#d0e0e8", "buttonText": "#173042", "highlight": "#1e88a8",
        "highlightedText": "#ffffff", "link": "#0f6f8f",
        "binderBackground": "#e7f0f4", "editorMargin": "#e3edf2", "headerBar": "#cfe0e8",
        "corkboardBackground": "#7fa9b8", "cardPaper": "#fbfdfe", "cardTitleLine": "#5aa7bf",
        "compositionBackground": "#12303c", "compositionPaper": "#1a3d4b", "compositionText": "#e6f1f5" } })json"},
    {"contrast", R"json({ "name": "High Contrast", "colors": {
        "window": "#000000", "windowText": "#ffffff", "base": "#000000", "alternateBase": "#1a1a1a",
        "text": "#ffffff", "button": "#1a1a1a", "buttonText": "#ffffff", "highlight": "#ffd400",
        "highlightedText": "#000000", "link": "#7fd4ff", "toolTipBase": "#000000", "toolTipText": "#ffffff",
        "mid": "#808080", "dark": "#000000", "light": "#b0b0b0", "disabledText": "#a0a0a0",
        "editorLink": "#7fd4ff", "headerBarActive": "#5a4b00", "headerBarLocked": "#7a0030",
        "corkboardBackground": "#202020", "cardPaper": "#000000", "cardTitle": "#ffffff", "cardText": "#ffffff",
        "cardTitleLine": "#ffd400", "cardLines": "#606060", "statusStamp": "#ff6060",
        "synopsisPaper": "#000000", "synopsisText": "#ffffff",
        "compositionBackground": "#000000", "compositionPaper": "#000000", "compositionText": "#ffffff" } })json"},
};

static bool readThemeFile(const QString &file, Theme *out)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 512 * 1024)
        return false;
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !d.isObject())
        return false;
    if (!Theme::fromJson(d.object(), out))
        return false;
    if (out->name.isEmpty())
        out->name = QFileInfo(file).completeBaseName();
    return true;
}

QString userThemeDir()
{
    return QFileInfo(addons::addonsRoot()).absolutePath() + QLatin1String("/themes");
}

QList<Entry> available()
{
    QList<Entry> l;
    for (const auto &b : kBuiltIn) {
        const QJsonObject o = QJsonDocument::fromJson(QByteArray(b[1])).object();
        l << Entry{QLatin1String(b[0]), o.value(QStringLiteral("name")).toString(), Kind::BuiltIn, QString()};
    }
    for (const addons::Installed &in : addons::installedAddons()) {
        if (!in.enabled)
            continue;
        const QDir dir(in.dir + QLatin1String("/themes"));
        for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
            Theme t;
            if (!readThemeFile(fi.absoluteFilePath(), &t))
                continue;
            l << Entry{QStringLiteral("addon:") + in.manifest.id + QLatin1Char('/') + fi.completeBaseName(), t.name,
                       Kind::Addon, fi.absoluteFilePath()};
        }
    }
    QList<Entry> own;
    const QDir dir(userThemeDir());
    for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        Theme t;
        if (!readThemeFile(fi.absoluteFilePath(), &t))
            continue;
        own << Entry{QStringLiteral("user:") + fi.completeBaseName(), t.name, Kind::User, fi.absoluteFilePath()};
    }
    std::sort(own.begin(), own.end(),
              [](const Entry &a, const Entry &b) { return a.name.localeAwareCompare(b.name) < 0; });
    return l + own;
}

bool load(const QString &id, Theme *out)
{
    for (const auto &b : kBuiltIn) {
        if (id != QLatin1String(b[0]))
            continue;
        const bool ok = Theme::fromJson(QJsonDocument::fromJson(QByteArray(b[1])).object(), out);
        out->id = id;
        return ok;
    }
    QString file;
    if (id.startsWith(QLatin1String("user:"))) {
        const QString base = id.mid(5);
        if (base.contains(QLatin1Char('/')) || base.contains(QLatin1String("..")))
            return false;
        file = userThemeDir() + QLatin1Char('/') + base + QLatin1String(".json");
    } else if (id.startsWith(QLatin1String("addon:"))) {
        const QString rest = id.mid(6);
        const int slash = rest.indexOf(QLatin1Char('/'));
        if (slash <= 0)
            return false;
        for (const addons::Installed &in : addons::installedAddons())
            if (in.enabled && in.manifest.id == rest.left(slash))
                file = in.dir + QLatin1String("/themes/") + rest.mid(slash + 1) + QLatin1String(".json");
    }
    if (file.isEmpty() || !readThemeFile(file, out))
        return false;
    out->id = id;
    return true;
}

// ------------------------------------------------------------------ resolving

static QPalette s_systemPalette;
static QFont s_systemFont;
static bool s_captured = false;
static bool s_ownPalette = false; // a palette of ours is set on the application
static bool s_applying = false;

namespace {
// The desktop can change its color scheme while the program runs. As long
// as no palette of ours is set, the areas follow it.
class DesktopWatcher : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == qApp && e->type() == QEvent::ApplicationPaletteChange && !s_applying && !s_ownPalette
            && QApplication::palette() != s_systemPalette)
            QTimer::singleShot(0, qApp, [] {
                if (!s_ownPalette)
                    apply();
            });
        return false;
    }
};
} // namespace

static void capture()
{
    if (s_captured)
        return;
    s_systemPalette = QApplication::palette();
    s_systemFont = QApplication::font();
    s_captured = true;
    qApp->installEventFilter(new DesktopWatcher(qApp));
}

static QColor mix(const QColor &a, const QColor &b, double share)
{
    return QColor(int(a.red() * (1 - share) + b.red() * share), int(a.green() * (1 - share) + b.green() * share),
                  int(a.blue() * (1 - share) + b.blue() * share));
}

static bool darkColor(const QColor &c) { return c.lightness() < 128; }

QMap<Role, QColor> resolvedColors(const Theme &t, const QMap<QString, QColor> &own)
{
    capture();
    QMap<Role, QColor> set = t.colors;
    for (auto it = own.constBegin(); it != own.constEnd(); ++it) {
        Role r;
        if (roleFromKey(it.key(), &r) && it.value().isValid())
            set.insert(r, it.value());
    }
    QMap<Role, QColor> c;
    auto put = [&](Role r, const QColor &fallback) { c.insert(r, set.contains(r) ? set.value(r) : fallback); };
    const QPalette &sys = s_systemPalette;
    const bool desk = t.followDesktop;

    put(Role::Window, sys.color(QPalette::Window));
    put(Role::Text, sys.color(QPalette::Text));
    const QColor window = c[Role::Window], text = c[Role::Text];
    const bool dark = darkColor(window);
    put(Role::WindowText, desk ? sys.color(QPalette::WindowText) : text);
    put(Role::Base, desk ? sys.color(QPalette::Base) : (dark ? window.darker(125) : window.lighter(108)));
    const QColor base = c[Role::Base];
    put(Role::AlternateBase, desk ? sys.color(QPalette::AlternateBase) : (dark ? base.lighter(118) : base.darker(104)));
    put(Role::Button, desk ? sys.color(QPalette::Button) : (dark ? window.lighter(118) : window));
    put(Role::ButtonText, desk ? sys.color(QPalette::ButtonText) : c[Role::WindowText]);
    put(Role::Highlight, desk ? sys.color(QPalette::Highlight) : QColor(61, 174, 233));
    put(Role::HighlightedText, desk ? sys.color(QPalette::HighlightedText) : QColor(Qt::white));
    put(Role::Link, desk ? sys.color(QPalette::Link) : (dark ? QColor(110, 180, 240) : QColor(41, 128, 185)));
    put(Role::ToolTipBase, desk ? sys.color(QPalette::ToolTipBase) : (dark ? window.lighter(130) : base));
    put(Role::ToolTipText, desk ? sys.color(QPalette::ToolTipText) : text);
    put(Role::Mid, desk ? sys.color(QPalette::Mid) : (dark ? window.lighter(150) : window.darker(125)));
    put(Role::Dark, desk ? sys.color(QPalette::Dark) : (dark ? window.darker(160) : window.darker(150)));
    put(Role::Light, desk ? sys.color(QPalette::Light) : (dark ? window.lighter(170) : window.lighter(120)));
    put(Role::DisabledText,
        desk ? sys.color(QPalette::Disabled, QPalette::Text) : mix(text, base, 0.55));
    put(Role::TargetSession, QColor(230, 150, 30));
    put(Role::TargetDraft, QColor(60, 130, 220));
    put(Role::TargetReached, QColor(60, 170, 70));

    put(Role::BinderBackground, base);
    put(Role::BinderText, darkColor(c[Role::BinderBackground]) == darkColor(base) ? text : (darkColor(c[Role::BinderBackground]) ? QColor(235, 235, 235) : QColor(35, 38, 41)));

    put(Role::EditorPaper, base);
    const QColor paper = c[Role::EditorPaper];
    const bool darkPaper = darkColor(paper);
    put(Role::EditorText, darkPaper == darkColor(base) ? text : (darkPaper ? QColor(230, 230, 230) : QColor(30, 30, 30)));
    put(Role::EditorMargin, paper);
    {
        // page view: the sheets must stand out from what lies around them
        const QColor margin = c[Role::EditorMargin];
        QColor around = margin;
        if (qAbs(margin.lightness() - paper.lightness()) < 18)
            around = !darkPaper ? paper.darker(116) : (paper.lightness() < 28 ? paper.lighter(190) : paper.darker(160));
        put(Role::EditorPageSurround, around);
    }
    put(Role::EditorLink, darkPaper ? QColor(120, 175, 250) : QColor(30, 90, 200));
    put(Role::SearchHit, QColor(255, 200, 60));
    put(Role::SpellingError, darkPaper ? QColor(255, 95, 95) : QColor(220, 40, 40));
    put(Role::HeaderBar, window);
    const QColor header = c[Role::HeaderBar];
    put(Role::HeaderBarText, darkColor(header) == dark ? c[Role::WindowText] : (darkColor(header) ? QColor(235, 235, 235) : QColor(35, 38, 41)));
    put(Role::HeaderBarActive, mix(header, c[Role::Highlight], 0.27));
    put(Role::HeaderBarLocked, darkColor(header) ? QColor(120, 62, 84) : QColor(236, 170, 190));

    put(Role::CorkboardBackground, dark ? QColor(84, 70, 52) : QColor(201, 168, 119));
    put(Role::CardPaper, dark ? QColor(62, 64, 68) : QColor(253, 252, 247));
    const bool darkCard = darkColor(c[Role::CardPaper]);
    put(Role::CardTitle, darkCard ? QColor(238, 238, 238) : QColor(30, 30, 30));
    put(Role::CardText, darkCard ? QColor(215, 215, 215) : QColor(40, 40, 40));
    put(Role::CardTitleLine, darkCard ? QColor(190, 100, 100) : QColor(220, 120, 120));
    put(Role::CardLines, darkCard ? QColor(105, 130, 165) : QColor(150, 190, 230));
    put(Role::StatusStamp, darkCard ? QColor(235, 95, 95) : QColor(200, 40, 40));

    put(Role::OutlinerBackground, base);
    put(Role::OutlinerAlternate, set.contains(Role::OutlinerBackground)
                                     ? (darkColor(c[Role::OutlinerBackground]) ? c[Role::OutlinerBackground].lighter(118)
                                                                               : c[Role::OutlinerBackground].darker(104))
                                     : c[Role::AlternateBase]);
    put(Role::OutlinerText, darkColor(c[Role::OutlinerBackground]) == darkColor(base) ? text : (darkColor(c[Role::OutlinerBackground]) ? QColor(230, 230, 230) : QColor(30, 30, 30)));

    put(Role::InspectorBackground, window);
    put(Role::InspectorText, darkColor(c[Role::InspectorBackground]) == dark ? c[Role::WindowText] : (darkColor(c[Role::InspectorBackground]) ? QColor(235, 235, 235) : QColor(35, 38, 41)));
    put(Role::SynopsisPaper, dark ? c[Role::CardPaper] : (set.contains(Role::CardPaper) ? c[Role::CardPaper] : QColor(253, 251, 240)));
    put(Role::SynopsisText, darkColor(c[Role::SynopsisPaper]) ? QColor(225, 225, 225) : QColor(30, 30, 30));
    put(Role::NotesPaper, base);
    put(Role::NotesText, darkColor(c[Role::NotesPaper]) == darkColor(base) ? text : (darkColor(c[Role::NotesPaper]) ? QColor(230, 230, 230) : QColor(30, 30, 30)));

    put(Role::CompositionBackground, QColor(28, 32, 40));
    put(Role::CompositionPaper, QColor(40, 45, 56));
    put(Role::CompositionText, darkColor(c[Role::CompositionPaper]) ? QColor(230, 230, 230) : QColor(30, 30, 30));
    return c;
}

QPalette paletteFor(const QMap<Role, QColor> &c)
{
    capture();
    QPalette p = s_systemPalette;
    const QColor window = c[Role::Window], text = c[Role::Text];
    const bool dark = darkColor(window);
    for (QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        p.setColor(g, QPalette::Window, window);
        p.setColor(g, QPalette::WindowText, c[Role::WindowText]);
        p.setColor(g, QPalette::Base, c[Role::Base]);
        p.setColor(g, QPalette::AlternateBase, c[Role::AlternateBase]);
        p.setColor(g, QPalette::Text, text);
        p.setColor(g, QPalette::Button, c[Role::Button]);
        p.setColor(g, QPalette::ButtonText, c[Role::ButtonText]);
        p.setColor(g, QPalette::BrightText, Qt::white);
        p.setColor(g, QPalette::Highlight, c[Role::Highlight]);
        p.setColor(g, QPalette::HighlightedText, c[Role::HighlightedText]);
        p.setColor(g, QPalette::Link, c[Role::Link]);
        p.setColor(g, QPalette::LinkVisited, dark ? c[Role::Link].lighter(115) : c[Role::Link].darker(125));
        p.setColor(g, QPalette::ToolTipBase, c[Role::ToolTipBase]);
        p.setColor(g, QPalette::ToolTipText, c[Role::ToolTipText]);
        p.setColor(g, QPalette::PlaceholderText, mix(text, c[Role::Base], 0.5));
        p.setColor(g, QPalette::Mid, c[Role::Mid]);
        p.setColor(g, QPalette::Dark, c[Role::Dark]);
        p.setColor(g, QPalette::Light, c[Role::Light]);
        p.setColor(g, QPalette::Midlight, mix(c[Role::Light], window, 0.5));
        p.setColor(g, QPalette::Shadow, dark ? QColor(0, 0, 0) : window.darker(250));
    }
    for (QPalette::ColorRole r : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, r, c[Role::DisabledText]);
    p.setColor(QPalette::Disabled, QPalette::Highlight, mix(c[Role::Highlight], window, 0.6));
    return p;
}

// ------------------------------------------------------------------ the look in use

static QMap<Role, QColor> s_colors;
static std::array<FontSpec, size_t(FontRole::Count)> s_fonts;
static bool s_dark = false;
static QString s_name;
Notifier &Notifier::instance()
{
    static Notifier n;
    return n;
}

static AreaLook s_area;

void setAreaLook(const AreaLook &look) { s_area = look; }
const AreaLook &areaLook() { return s_area; }

bool isAreaColor(const QString &key)
{
    for (const RoleInfo &i : roles())
        if (key == QLatin1String(i.key))
            return i.area != Area::General;
    return false;
}

bool isAreaFont(const QString &key)
{
    for (const FontInfo &i : fontRoles())
        if (key == QLatin1String(i.key))
            return i.role != FontRole::Interface;
    return false;
}

static QMap<FontRole, FontSpec> fontsOf(const QMap<QString, QString> &own)
{
    QMap<FontRole, FontSpec> m;
    for (auto it = own.constBegin(); it != own.constEnd(); ++it) {
        FontRole r;
        if (fontRoleFromKey(it.key(), &r))
            m.insert(r, FontSpec::fromString(it.value()));
    }
    return m;
}

// The own fonts in use: the interface font of the settings, the areas' of the project.
static QMap<FontRole, FontSpec> ownFonts()
{
    QMap<FontRole, FontSpec> m = fontsOf(AppSettings::instance().ownFonts);
    if (s_area.set) {
        for (auto it = m.begin(); it != m.end();)
            it = it.key() == FontRole::Interface ? std::next(it) : m.erase(it);
        const QMap<FontRole, FontSpec> area = fontsOf(s_area.fonts);
        for (auto it = area.constBegin(); it != area.constEnd(); ++it)
            if (it.key() != FontRole::Interface)
                m.insert(it.key(), it.value());
    }
    return m;
}

static Theme loadOrSystem(const QString &id)
{
    Theme t;
    if (!load(id, &t))
        load(QStringLiteral("system"), &t);
    return t;
}

static Theme chosenTheme() { return loadOrSystem(AppSettings::instance().theme); }

// The own colors the areas are worked out from: the general ones of the settings and the
// areas' own of the project (or of the settings without a project).
static QMap<QString, QColor> areaOwnColors()
{
    const AppSettings &s = AppSettings::instance();
    if (!s_area.set)
        return s.ownColors;
    QMap<QString, QColor> own;
    for (auto it = s.ownColors.constBegin(); it != s.ownColors.constEnd(); ++it)
        if (!isAreaColor(it.key()))
            own.insert(it.key(), it.value());
    for (auto it = s_area.colors.constBegin(); it != s_area.colors.constEnd(); ++it)
        if (isAreaColor(it.key()))
            own.insert(it.key(), it.value());
    return own;
}

// All colors in use: the general ones from the settings' theme and own colors, the areas'
// from the project's theme and own colors (0.40.2).
static QMap<Role, QColor> colorsInUse()
{
    const AppSettings &s = AppSettings::instance();
    QMap<Role, QColor> all = resolvedColors(chosenTheme(), s.ownColors);
    if (s_area.set) {
        const QMap<Role, QColor> area = resolvedColors(loadOrSystem(s_area.theme), areaOwnColors());
        for (const RoleInfo &i : roles())
            if (i.area != Area::General)
                all.insert(i.role, area.value(i.role));
    }
    return all;
}

Theme currentLook()
{
    const AppSettings &s = AppSettings::instance();
    Theme t = chosenTheme();
    if (s_area.set) {
        // the areas as the project shows them
        const Theme at = loadOrSystem(s_area.theme);
        for (const RoleInfo &i : roles())
            if (i.area != Area::General) {
                t.colors.remove(i.role);
                if (at.colors.contains(i.role))
                    t.colors.insert(i.role, at.colors.value(i.role));
            }
        for (const FontInfo &i : fontRoles())
            if (i.role != FontRole::Interface) {
                t.fonts.remove(i.role);
                if (at.fonts.contains(i.role))
                    t.fonts.insert(i.role, at.fonts.value(i.role));
            }
    }
    const QMap<QString, QColor> own = s_area.set ? areaOwnColors() : s.ownColors;
    for (auto it = own.constBegin(); it != own.constEnd(); ++it) {
        Role r;
        if (roleFromKey(it.key(), &r) && it.value().isValid())
            t.colors.insert(r, it.value());
    }
    const QMap<FontRole, FontSpec> fonts = ownFonts();
    for (auto it = fonts.constBegin(); it != fonts.constEnd(); ++it) {
        if (it.value().isSet())
            t.fonts.insert(it.key(), it.value());
    }
    // general colors set on top of the desktop's scheme no longer follow it
    // only when the window itself is given
    if (t.followDesktop && t.colors.contains(Role::Window) && t.colors.contains(Role::Text))
        t.followDesktop = false;
    return t;
}

// Icons of the desktop (Breeze) come in a light and a dark set; use the one
// that fits the window color. Without a desktop set the bundled icons are used.
static void applyIcons(bool dark, bool ownColors)
{
    icons::setDark(dark);
    const QString current = QIcon::themeName();
    static QString desktopTheme; // the icon set the desktop gave us
    static bool known = false;
    if (!known) {
        desktopTheme = current.startsWith(QLatin1String("qbicons")) ? QString() : current;
        known = true;
    }
    const QString bundled = dark ? QStringLiteral("qbicons-dark") : QStringLiteral("qbicons");
    QIcon::setFallbackThemeName(bundled);
    if (desktopTheme.isEmpty() || desktopTheme == QLatin1String("hicolor")) {
        QIcon::setThemeName(bundled);
        icons::setPreferBundled(false);
        return;
    }
    if (!ownColors) {
        QIcon::setThemeName(desktopTheme);
        icons::setPreferBundled(false);
        return;
    }
    // "breeze" <-> "breeze-dark" (and other sets named the same way)
    QString base = desktopTheme;
    for (const char *suffix : {"-dark", "-Dark", "-light", "-Light"})
        if (base.endsWith(QLatin1String(suffix)))
            base.chop(int(qstrlen(suffix)));
    const bool desktopIsDark = desktopTheme.endsWith(QLatin1String("-dark"), Qt::CaseInsensitive);
    QString wanted;
    if (dark == desktopIsDark) {
        wanted = desktopTheme;
    } else {
        const QStringList candidates = dark ? QStringList{base + QLatin1String("-dark"), base + QLatin1String("-Dark")}
                                            : QStringList{base, base + QLatin1String("-light"), base + QLatin1String("-Light")};
        for (const QString &cand : candidates) {
            for (const QString &path : QIcon::themeSearchPaths())
                if (QFile::exists(path + QLatin1Char('/') + cand + QLatin1String("/index.theme"))) {
                    wanted = cand;
                    break;
                }
            if (!wanted.isEmpty())
                break;
        }
    }
    if (wanted.isEmpty()) {
        // no fitting desktop set: the bundled icons in the right variant
        QIcon::setThemeName(desktopTheme);
        icons::setPreferBundled(true);
    } else {
        QIcon::setThemeName(wanted);
        icons::setPreferBundled(false);
    }
}

void apply()
{
    capture();
    if (!s_ownPalette)
        s_systemPalette = QApplication::palette(); // the desktop's colors as they are now
    s_applying = true;
    const AppSettings &s = AppSettings::instance();
    const Theme t = chosenTheme();
    s_name = t.name;
    s_colors = colorsInUse();
    // palette: untouched desktop colors stay exactly the desktop's palette
    bool generalChanged = !t.followDesktop;
    for (const RoleInfo &i : roles())
        if (i.area == Area::General && (s.ownColors.contains(QLatin1String(i.key)) || t.colors.contains(i.role))
            && i.role != Role::TargetSession && i.role != Role::TargetDraft && i.role != Role::TargetReached)
            generalChanged = true;
    const QPalette pal = generalChanged ? paletteFor(s_colors) : s_systemPalette;
    if ((generalChanged || s_ownPalette) && pal != QApplication::palette())
        QApplication::setPalette(pal);
    s_ownPalette = generalChanged;
    s_dark = darkColor(s_colors[Role::Window]);

    QMap<FontRole, FontSpec> fonts = t.fonts;
    if (s_area.set) {
        // the areas take the fonts of the project's theme
        const Theme at = loadOrSystem(s_area.theme);
        for (const FontInfo &i : fontRoles())
            if (i.role != FontRole::Interface) {
                fonts.remove(i.role);
                if (at.fonts.contains(i.role))
                    fonts.insert(i.role, at.fonts.value(i.role));
            }
    }
    const QMap<FontRole, FontSpec> own = ownFonts();
    for (auto it = own.constBegin(); it != own.constEnd(); ++it)
        fonts.insert(it.key(), it.value());
    for (size_t i = 0; i < s_fonts.size(); ++i)
        s_fonts[i] = fonts.value(FontRole(i));
    const QFont appFont = s_fonts[size_t(FontRole::Interface)].applied(s_systemFont);
    if (appFont != QApplication::font())
        QApplication::setFont(appFont);

    const QString iconsBefore = QIcon::themeName();
    applyIcons(s_dark, generalChanged);
    if (QIcon::themeName() != iconsBefore)
        icons::clearCache();
    s_applying = false;
    emit Notifier::instance().changed();
}

QColor color(Role r)
{
    if (s_colors.isEmpty()) {
        Theme t;
        load(QStringLiteral("system"), &t);
        s_colors = resolvedColors(t, {});
    }
    return s_colors.value(r);
}

QFont baseFont(FontRole r)
{
    capture();
    QFont base = r == FontRole::Interface ? s_systemFont : QApplication::font();
    switch (r) {
    case FontRole::HeaderTitle: base.setBold(true); break;
    case FontRole::CardTitle:
        base.setBold(true);
        base.setPointSizeF(base.pointSizeF() * 0.95);
        break;
    case FontRole::CombinedTitle:
        base.setBold(true);
        base.setPointSizeF(base.pointSizeF() * 1.25);
        break;
    case FontRole::CardText: base.setPointSizeF(base.pointSizeF() * 0.9); break;
    default: break;
    }
    return base;
}

QFont font(FontRole r)
{
    if (r == FontRole::Interface)
        return QApplication::font();
    return s_fonts[size_t(r)].applied(baseFont(r));
}

bool isDark() { return s_dark; }

QColor accent() { return color(Role::Highlight); }

QString currentName() { return s_name; }

void styleView(QWidget *view, Role background, Role text)
{
    QPalette p = QApplication::palette();
    const QColor bg = color(background), fg = color(text);
    p.setColor(QPalette::Base, bg);
    p.setColor(QPalette::Text, fg);
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, fg);
    p.setColor(QPalette::AlternateBase, darkColor(bg) ? bg.lighter(118) : bg.darker(104));
    p.setColor(QPalette::PlaceholderText, mix(fg, bg, 0.5));
    for (QPalette::ColorRole r : {QPalette::Text, QPalette::WindowText})
        p.setColor(QPalette::Disabled, r, mix(fg, bg, 0.55));
    view->setPalette(p);
}

void styleView(QWidget *view, Role background, Role text, FontRole f)
{
    styleView(view, background, text);
    view->setFont(font(f));
}

// ------------------------------------------------------------------ own themes

static QString fileBase(const QString &name)
{
    QString b = name.toLower();
    b.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    while (b.startsWith(QLatin1Char('-')))
        b.remove(0, 1);
    while (b.endsWith(QLatin1Char('-')))
        b.chop(1);
    return b.isEmpty() ? QStringLiteral("theme") : b.left(60);
}

bool nameInUse(const QString &name, const QString &exceptId)
{
    for (const Entry &e : available())
        if (e.id != exceptId && e.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

static bool writeTheme(const QString &file, const Theme &t, QString *error)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(t.toJson()).toJson(QJsonDocument::Indented)) < 0
        || !f.commit()) {
        if (error)
            *error = QObject::tr("The theme could not be written to %1: %2").arg(file, f.errorString());
        return false;
    }
    return true;
}

QString saveUserTheme(const Theme &t, QString *error)
{
    if (t.name.trimmed().isEmpty()) {
        if (error)
            *error = QObject::tr("The theme needs a name.");
        return QString();
    }
    QString base;
    if (t.id.startsWith(QLatin1String("user:"))) {
        base = t.id.mid(5); // save over the existing file
    } else {
        base = fileBase(t.name);
        const QString stem = base;
        for (int n = 2; QFile::exists(userThemeDir() + QLatin1Char('/') + base + QLatin1String(".json")); ++n)
            base = stem + QLatin1Char('-') + QString::number(n);
    }
    Theme out = t;
    out.name = t.name.trimmed();
    if (!writeTheme(userThemeDir() + QLatin1Char('/') + base + QLatin1String(".json"), out, error))
        return QString();
    return QStringLiteral("user:") + base;
}

bool removeUserTheme(const QString &id, QString *error)
{
    if (!id.startsWith(QLatin1String("user:")) || id.contains(QLatin1Char('/'))) {
        if (error)
            *error = QObject::tr("Only your own themes can be deleted.");
        return false;
    }
    QFile f(userThemeDir() + QLatin1Char('/') + id.mid(5) + QLatin1String(".json"));
    if (!f.remove()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

bool renameUserTheme(const QString &id, const QString &newName, QString *newId, QString *error)
{
    Theme t;
    if (!id.startsWith(QLatin1String("user:")) || !load(id, &t)) {
        if (error)
            *error = QObject::tr("Only your own themes can be renamed.");
        return false;
    }
    t.name = newName.trimmed();
    const QString saved = saveUserTheme(t, error); // same file, new name
    if (saved.isEmpty())
        return false;
    if (newId)
        *newId = saved;
    return true;
}

QString importTheme(const QString &file, QString *error)
{
    Theme t;
    if (!readThemeFile(file, &t)) {
        if (error)
            *error = QObject::tr("%1 is not a theme file.").arg(QFileInfo(file).fileName());
        return QString();
    }
    t.id.clear();
    const QString stem = t.name;
    for (int n = 2; nameInUse(t.name); ++n)
        t.name = QStringLiteral("%1 %2").arg(stem).arg(n);
    return saveUserTheme(t, error);
}

bool exportTheme(const QString &id, const QString &file, QString *error)
{
    Theme t;
    if (!load(id, &t)) {
        if (error)
            *error = QObject::tr("The theme could not be read.");
        return false;
    }
    return writeTheme(file, t, error);
}

} // namespace theme
} // namespace qb
