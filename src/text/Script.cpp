#include "text/Script.h"

#include "core/Addons.h"
#include "core/Paths.h"
#include "text/DirectFormat.h"
#include "text/TextProps.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTextBlock>
#include <QTextDocument>

namespace qb {
namespace script {

namespace {

const QString &stylePrefix()
{
    static const QString p = QStringLiteral("Script > ");
    return p;
}

struct RoleInfo {
    Role role;
    const char *key;
    const char *label;
};
const RoleInfo kRoles[] = {
    {Role::General, "general", QT_TRANSLATE_NOOP("QObject", "General text")},
    {Role::SceneHeading, "scene", QT_TRANSLATE_NOOP("QObject", "Scene heading")},
    {Role::Action, "action", QT_TRANSLATE_NOOP("QObject", "Action or description")},
    {Role::Character, "character", QT_TRANSLATE_NOOP("QObject", "Character name")},
    {Role::Parenthetical, "parenthetical", QT_TRANSLATE_NOOP("QObject", "Parenthetical")},
    {Role::Dialogue, "dialogue", QT_TRANSLATE_NOOP("QObject", "Dialogue")},
    {Role::Transition, "transition", QT_TRANSLATE_NOOP("QObject", "Transition")},
    {Role::Shot, "shot", QT_TRANSLATE_NOOP("QObject", "Shot")},
    {Role::Centered, "centered", QT_TRANSLATE_NOOP("QObject", "Centered text")},
    {Role::Lyrics, "lyrics", QT_TRANSLATE_NOOP("QObject", "Lyrics")},
    {Role::Section, "section", QT_TRANSLATE_NOOP("QObject", "Act or part heading")},
};

double in(double inches) { return inches * 25.4; }

Element el(const QString &name, Role role, const QString &key)
{
    Element e;
    e.name = name;
    e.role = role;
    e.key = key;
    return e;
}

Format screenplay()
{
    Format f;
    f.name = QStringLiteral("Screenplay");
    f.description = QObject::tr("Film and television scripts in the usual layout: scene headings, action, character, "
                                "parenthetical, dialogue, transition, shot.");
    f.firstElement = QStringLiteral("Scene Heading");

    Element scene = el(QStringLiteral("Scene Heading"), Role::SceneHeading, QStringLiteral("S"));
    scene.allCaps = true;
    scene.linesBefore = 2;
    scene.keepWithNext = true;
    scene.onEnter = QStringLiteral("Action");
    scene.onTabEmpty = QStringLiteral("Action");
    scene.tabInserts = QStringLiteral(" - ");
    scene.completions = {QStringLiteral("INT. "), QStringLiteral("EXT. "), QStringLiteral("INT./EXT. "),
                         QStringLiteral("EST. ")};
    scene.tabCompletions = {QStringLiteral("DAY"),     QStringLiteral("NIGHT"),      QStringLiteral("MORNING"),
                            QStringLiteral("EVENING"), QStringLiteral("AFTERNOON"),  QStringLiteral("DAWN"),
                            QStringLiteral("DUSK"),    QStringLiteral("LATER"),      QStringLiteral("MOMENTS LATER"),
                            QStringLiteral("CONTINUOUS"), QStringLiteral("SAME TIME")};
    scene.completeFromScript = true;

    Element action = el(QStringLiteral("Action"), Role::Action, QStringLiteral("A"));
    action.onEnter = QStringLiteral("Action");
    action.onTabEmpty = QStringLiteral("Character");
    action.onTabEnd = QStringLiteral("Character");

    Element character = el(QStringLiteral("Character"), Role::Character, QStringLiteral("C"));
    character.allCaps = true;
    character.leftMm = in(2.2);
    character.keepWithNext = true;
    character.onEnter = QStringLiteral("Dialogue");
    character.onTabEmpty = QStringLiteral("Transition");
    character.onTabEnd = QStringLiteral("Parenthetical");
    character.completeFromScript = true;

    Element paren = el(QStringLiteral("Parenthetical"), Role::Parenthetical, QStringLiteral("P"));
    paren.leftMm = in(1.6);
    paren.rightMm = in(2.0);
    paren.linesBefore = 0;
    paren.keepWithNext = true;
    paren.onEnter = QStringLiteral("Dialogue");
    paren.onTabEmpty = QStringLiteral("Dialogue");
    paren.onTabEnd = QStringLiteral("Dialogue");
    paren.prefix = QStringLiteral("(");
    paren.suffix = QStringLiteral(")");

    Element dialogue = el(QStringLiteral("Dialogue"), Role::Dialogue, QStringLiteral("D"));
    dialogue.leftMm = in(1.0);
    dialogue.rightMm = in(1.5);
    dialogue.linesBefore = 0;
    dialogue.onEnter = QStringLiteral("Action");
    dialogue.onTabEmpty = QStringLiteral("Parenthetical");
    dialogue.onTabEnd = QStringLiteral("Parenthetical");

    Element transition = el(QStringLiteral("Transition"), Role::Transition, QStringLiteral("T"));
    transition.allCaps = true;
    transition.align = 2;
    transition.onEnter = QStringLiteral("Scene Heading");
    transition.onTabEmpty = QStringLiteral("Scene Heading");
    transition.completions = {QStringLiteral("CUT TO:"),       QStringLiteral("DISSOLVE TO:"),
                              QStringLiteral("FADE IN:"),      QStringLiteral("FADE OUT."),
                              QStringLiteral("FADE TO BLACK."), QStringLiteral("SMASH CUT TO:"),
                              QStringLiteral("MATCH CUT TO:"), QStringLiteral("INTERCUT WITH:")};

    Element shot = el(QStringLiteral("Shot"), Role::Shot, QStringLiteral("H"));
    shot.allCaps = true;
    shot.keepWithNext = true;
    shot.onEnter = QStringLiteral("Action");
    shot.onTabEmpty = QStringLiteral("Action");

    f.elements = {scene, action, character, paren, dialogue, transition, shot};
    return f;
}

Format stagePlay()
{
    Format f;
    f.name = QStringLiteral("Stage Play");
    f.description = QObject::tr("Plays in the manuscript layout common in the US: centered act and scene headings, "
                                "character names above the dialogue, stage directions indented and in parentheses.");
    f.firstElement = QStringLiteral("Act Heading");

    Element act = el(QStringLiteral("Act Heading"), Role::Section, QStringLiteral("H"));
    act.allCaps = true;
    act.underline = true;
    act.align = 1;
    act.linesBefore = 2;
    act.keepWithNext = true;
    act.onEnter = QStringLiteral("Scene Heading");
    act.onTabEmpty = QStringLiteral("Scene Heading");
    act.completions = {QStringLiteral("ACT ONE"), QStringLiteral("ACT TWO"), QStringLiteral("ACT THREE"),
                       QStringLiteral("ACT FOUR"), QStringLiteral("ACT FIVE"), QStringLiteral("PROLOGUE"),
                       QStringLiteral("EPILOGUE")};

    Element scene = el(QStringLiteral("Scene Heading"), Role::SceneHeading, QStringLiteral("S"));
    scene.allCaps = true;
    scene.align = 1;
    scene.keepWithNext = true;
    scene.onEnter = QStringLiteral("Stage Direction");
    scene.onTabEmpty = QStringLiteral("Stage Direction");
    scene.completions = {QStringLiteral("SCENE 1"), QStringLiteral("SCENE 2"), QStringLiteral("SCENE 3"),
                         QStringLiteral("SCENE 4"), QStringLiteral("SCENE 5")};

    Element direction = el(QStringLiteral("Stage Direction"), Role::Action, QStringLiteral("R"));
    direction.leftMm = in(2.0);
    direction.onEnter = QStringLiteral("Character");
    direction.onTabEmpty = QStringLiteral("Character");
    direction.onTabEnd = QStringLiteral("Character");
    direction.prefix = QStringLiteral("(");
    direction.suffix = QStringLiteral(")");

    Element character = el(QStringLiteral("Character"), Role::Character, QStringLiteral("C"));
    character.allCaps = true;
    character.leftMm = in(2.5);
    character.keepWithNext = true;
    character.onEnter = QStringLiteral("Dialogue");
    character.onTabEmpty = QStringLiteral("Dialogue");
    character.onTabEnd = QStringLiteral("Parenthetical");
    character.completeFromScript = true;

    Element paren = el(QStringLiteral("Parenthetical"), Role::Parenthetical, QStringLiteral("P"));
    paren.leftMm = in(1.5);
    paren.linesBefore = 0;
    paren.keepWithNext = true;
    paren.onEnter = QStringLiteral("Dialogue");
    paren.onTabEmpty = QStringLiteral("Dialogue");
    paren.onTabEnd = QStringLiteral("Dialogue");
    paren.prefix = QStringLiteral("(");
    paren.suffix = QStringLiteral(")");

    Element dialogue = el(QStringLiteral("Dialogue"), Role::Dialogue, QStringLiteral("D"));
    dialogue.linesBefore = 0;
    dialogue.onEnter = QStringLiteral("Character");
    dialogue.onTabEmpty = QStringLiteral("Stage Direction");
    dialogue.onTabEnd = QStringLiteral("Parenthetical");

    Element transition = el(QStringLiteral("Transition"), Role::Transition, QStringLiteral("T"));
    transition.allCaps = true;
    transition.align = 2;
    transition.onEnter = QStringLiteral("Scene Heading");
    transition.onTabEmpty = QStringLiteral("Act Heading");
    transition.completions = {QStringLiteral("BLACKOUT."), QStringLiteral("LIGHTS FADE."), QStringLiteral("CURTAIN."),
                              QStringLiteral("END OF SCENE"), QStringLiteral("END OF ACT"), QStringLiteral("END OF PLAY")};

    f.elements = {act, scene, direction, character, paren, dialogue, transition};
    return f;
}

Format comicScript()
{
    Format f;
    f.name = QStringLiteral("Comic Script");
    f.description = QObject::tr("Comics and graphic novels: pages, panels, panel descriptions, and the lettering "
                                "(character, dialogue, captions, sound effects).");
    f.firstElement = QStringLiteral("Page");

    Element page = el(QStringLiteral("Page"), Role::SceneHeading, QStringLiteral("G"));
    page.allCaps = true;
    page.bold = true;
    page.underline = true;
    page.linesBefore = 2;
    page.keepWithNext = true;
    page.onEnter = QStringLiteral("Panel");
    page.onTabEmpty = QStringLiteral("Panel");
    page.completions = {QStringLiteral("PAGE ONE"),   QStringLiteral("PAGE TWO"),  QStringLiteral("PAGE THREE"),
                        QStringLiteral("PAGE FOUR"),  QStringLiteral("PAGE FIVE"), QStringLiteral("PAGE SIX"),
                        QStringLiteral("PAGE SEVEN"), QStringLiteral("PAGE EIGHT")};
    page.tabInserts = QStringLiteral(" (");
    page.tabCompletions = {QStringLiteral("four panels)"), QStringLiteral("five panels)"), QStringLiteral("six panels)"),
                           QStringLiteral("splash page)")};

    Element panel = el(QStringLiteral("Panel"), Role::Shot, QStringLiteral("N"));
    panel.bold = true;
    panel.keepWithNext = true;
    panel.onEnter = QStringLiteral("Description");
    panel.onTabEmpty = QStringLiteral("Description");
    panel.completions = {QStringLiteral("Panel 1"), QStringLiteral("Panel 2"), QStringLiteral("Panel 3"),
                         QStringLiteral("Panel 4"), QStringLiteral("Panel 5"), QStringLiteral("Panel 6"),
                         QStringLiteral("Panel 7"), QStringLiteral("Panel 8"), QStringLiteral("Panel 9")};

    Element description = el(QStringLiteral("Description"), Role::Action, QStringLiteral("A"));
    description.linesBefore = 0;
    description.onEnter = QStringLiteral("Character");
    description.onTabEmpty = QStringLiteral("Character");
    description.onTabEnd = QStringLiteral("Character");

    Element character = el(QStringLiteral("Character"), Role::Character, QStringLiteral("C"));
    character.allCaps = true;
    character.leftMm = in(1.5);
    character.keepWithNext = true;
    character.onEnter = QStringLiteral("Dialogue");
    character.onTabEmpty = QStringLiteral("Caption");
    character.onTabEnd = QStringLiteral("Parenthetical");
    character.completeFromScript = true;

    Element paren = el(QStringLiteral("Parenthetical"), Role::Parenthetical, QStringLiteral("P"));
    paren.leftMm = in(1.25);
    paren.rightMm = in(1.25);
    paren.linesBefore = 0;
    paren.keepWithNext = true;
    paren.onEnter = QStringLiteral("Dialogue");
    paren.onTabEmpty = QStringLiteral("Dialogue");
    paren.onTabEnd = QStringLiteral("Dialogue");
    paren.prefix = QStringLiteral("(");
    paren.suffix = QStringLiteral(")");

    Element dialogue = el(QStringLiteral("Dialogue"), Role::Dialogue, QStringLiteral("D"));
    dialogue.leftMm = in(1.0);
    dialogue.rightMm = in(1.0);
    dialogue.linesBefore = 0;
    dialogue.onEnter = QStringLiteral("Character");
    dialogue.onTabEmpty = QStringLiteral("Description");
    dialogue.onTabEnd = QStringLiteral("Parenthetical");

    Element caption = el(QStringLiteral("Caption"), Role::Dialogue, QStringLiteral("O"));
    caption.italic = true;
    caption.leftMm = in(1.0);
    caption.rightMm = in(1.0);
    caption.onEnter = QStringLiteral("Character");
    caption.onTabEmpty = QStringLiteral("Sound Effect");
    caption.prefix = QStringLiteral("CAPTION: ");

    Element sfx = el(QStringLiteral("Sound Effect"), Role::Action, QStringLiteral("X"));
    sfx.allCaps = true;
    sfx.bold = true;
    sfx.leftMm = in(1.0);
    sfx.onEnter = QStringLiteral("Character");
    sfx.onTabEmpty = QStringLiteral("Page");
    sfx.prefix = QStringLiteral("SFX: ");

    f.elements = {page, panel, description, character, paren, dialogue, caption, sfx};
    return f;
}

} // namespace

// ------------------------------------------------------------------ roles

QString roleKey(Role r)
{
    for (const RoleInfo &i : kRoles)
        if (i.role == r)
            return QLatin1String(i.key);
    return QStringLiteral("general");
}

Role roleFromKey(const QString &key)
{
    for (const RoleInfo &i : kRoles)
        if (key.compare(QLatin1String(i.key), Qt::CaseInsensitive) == 0)
            return i.role;
    return Role::General;
}

QString roleLabel(Role r)
{
    for (const RoleInfo &i : kRoles)
        if (i.role == r)
            return QObject::tr(i.label);
    return QString();
}

QList<Role> elementRoles()
{
    return {Role::SceneHeading, Role::Action, Role::Character, Role::Parenthetical, Role::Dialogue, Role::Transition,
            Role::Shot,         Role::Lyrics, Role::Centered,  Role::Section};
}

// ------------------------------------------------------------------ format

bool Element::operator==(const Element &o) const
{
    return name == o.name && role == o.role && bold == o.bold && italic == o.italic && underline == o.underline
           && allCaps == o.allCaps && color == o.color && align == o.align && qAbs(leftMm - o.leftMm) < 0.01
           && qAbs(rightMm - o.rightMm) < 0.01 && qAbs(firstMm - o.firstMm) < 0.01
           && qAbs(linesBefore - o.linesBefore) < 0.001 && qAbs(lineSpacing - o.lineSpacing) < 0.001
           && keepWithNext == o.keepWithNext && onEnter == o.onEnter && onTabEmpty == o.onTabEmpty
           && onTabEnd == o.onTabEnd && tabInserts == o.tabInserts && prefix == o.prefix && suffix == o.suffix
           && completions == o.completions && tabCompletions == o.tabCompletions
           && completeFromScript == o.completeFromScript && key == o.key;
}

const Element *Format::find(const QString &n) const
{
    const int i = indexOf(n);
    return i >= 0 ? &elements[i] : nullptr;
}

int Format::indexOf(const QString &n) const
{
    if (n.isEmpty())
        return -1;
    for (int i = 0; i < elements.size(); ++i)
        if (elements[i].name.compare(n, Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

const Element *Format::byRole(Role r) const
{
    for (const Element &e : elements)
        if (e.role == r)
            return &e;
    return nullptr;
}

const Element *Format::first() const
{
    if (const Element *e = find(firstElement))
        return e;
    return elements.isEmpty() ? nullptr : &elements.first();
}

bool Format::operator==(const Format &o) const
{
    return name == o.name && description == o.description && fontFamily == o.fontFamily
           && qAbs(fontSize - o.fontSize) < 0.01 && unit == o.unit && firstElement == o.firstElement
           && elements == o.elements;
}

QList<Format> builtIn() { return {screenplay(), stagePlay(), comicScript()}; }

const Format &standard()
{
    static const Format f = screenplay();
    return f;
}

bool validElementName(const QString &name)
{
    return styles::validName(name) && !name.contains(QLatin1Char('/'));
}

// ------------------------------------------------------------------ files

QJsonObject toJson(const Format &f)
{
    QJsonObject o;
    o.insert(QStringLiteral("name"), f.name);
    o.insert(QStringLiteral("description"), f.description);
    o.insert(QStringLiteral("font"), f.fontFamily);
    o.insert(QStringLiteral("size"), f.fontSize);
    o.insert(QStringLiteral("unit"), f.unit);
    o.insert(QStringLiteral("first"), f.firstElement);
    QJsonArray arr;
    for (const Element &e : f.elements) {
        QJsonObject j;
        j.insert(QStringLiteral("name"), e.name);
        j.insert(QStringLiteral("role"), roleKey(e.role));
        if (e.bold)
            j.insert(QStringLiteral("bold"), true);
        if (e.italic)
            j.insert(QStringLiteral("italic"), true);
        if (e.underline)
            j.insert(QStringLiteral("underline"), true);
        if (e.allCaps)
            j.insert(QStringLiteral("caps"), true);
        if (e.color.isValid())
            j.insert(QStringLiteral("color"), e.color.name());
        j.insert(QStringLiteral("align"), e.align == 1 ? QStringLiteral("center")
                                                        : e.align == 2 ? QStringLiteral("right") : QStringLiteral("left"));
        j.insert(QStringLiteral("left"), e.leftMm);
        j.insert(QStringLiteral("right"), e.rightMm);
        j.insert(QStringLiteral("firstLine"), e.firstMm);
        j.insert(QStringLiteral("linesBefore"), e.linesBefore);
        j.insert(QStringLiteral("lineSpacing"), e.lineSpacing);
        if (e.keepWithNext)
            j.insert(QStringLiteral("keepWithNext"), true);
        j.insert(QStringLiteral("enter"), e.onEnter);
        j.insert(QStringLiteral("tabEmpty"), e.onTabEmpty);
        j.insert(QStringLiteral("tabEnd"), e.onTabEnd);
        if (!e.tabInserts.isEmpty())
            j.insert(QStringLiteral("tabInserts"), e.tabInserts);
        if (!e.prefix.isEmpty())
            j.insert(QStringLiteral("prefix"), e.prefix);
        if (!e.suffix.isEmpty())
            j.insert(QStringLiteral("suffix"), e.suffix);
        if (!e.completions.isEmpty())
            j.insert(QStringLiteral("completions"), QJsonArray::fromStringList(e.completions));
        if (!e.tabCompletions.isEmpty())
            j.insert(QStringLiteral("tabCompletions"), QJsonArray::fromStringList(e.tabCompletions));
        if (e.completeFromScript)
            j.insert(QStringLiteral("completeFromScript"), true);
        if (!e.key.isEmpty())
            j.insert(QStringLiteral("key"), e.key);
        arr.append(j);
    }
    o.insert(QStringLiteral("elements"), arr);
    return o;
}

Format fromJson(const QJsonObject &o, bool *ok)
{
    Format f;
    f.name = o.value(QStringLiteral("name")).toString().trimmed();
    f.description = o.value(QStringLiteral("description")).toString();
    f.fontFamily = o.value(QStringLiteral("font")).toString();
    f.fontSize = qBound(6.0, o.value(QStringLiteral("size")).toDouble(12), 48.0);
    f.unit = o.value(QStringLiteral("unit")).toString() == QLatin1String("cm") ? QStringLiteral("cm")
                                                                                : QStringLiteral("in");
    f.firstElement = o.value(QStringLiteral("first")).toString();
    auto strings = [](const QJsonValue &v) {
        QStringList l;
        for (const QJsonValue &s : v.toArray())
            if (!s.toString().isEmpty())
                l << s.toString();
        return l;
    };
    for (const QJsonValue &v : o.value(QStringLiteral("elements")).toArray()) {
        const QJsonObject j = v.toObject();
        Element e;
        e.name = j.value(QStringLiteral("name")).toString().trimmed();
        if (!validElementName(e.name) || f.indexOf(e.name) >= 0)
            continue;
        e.role = roleFromKey(j.value(QStringLiteral("role")).toString());
        if (e.role == Role::General)
            e.role = Role::Action;
        e.bold = j.value(QStringLiteral("bold")).toBool();
        e.italic = j.value(QStringLiteral("italic")).toBool();
        e.underline = j.value(QStringLiteral("underline")).toBool();
        e.allCaps = j.value(QStringLiteral("caps")).toBool();
        const QString color = j.value(QStringLiteral("color")).toString();
        if (!color.isEmpty())
            e.color = QColor(color);
        const QString align = j.value(QStringLiteral("align")).toString();
        e.align = align == QLatin1String("center") ? 1 : align == QLatin1String("right") ? 2 : 0;
        e.leftMm = qBound(0.0, j.value(QStringLiteral("left")).toDouble(), 200.0);
        e.rightMm = qBound(0.0, j.value(QStringLiteral("right")).toDouble(), 200.0);
        e.firstMm = qBound(-100.0, j.value(QStringLiteral("firstLine")).toDouble(), 100.0);
        e.linesBefore = qBound(0.0, j.value(QStringLiteral("linesBefore")).toDouble(1), 10.0);
        e.lineSpacing = qBound(0.5, j.value(QStringLiteral("lineSpacing")).toDouble(1), 4.0);
        e.keepWithNext = j.value(QStringLiteral("keepWithNext")).toBool();
        e.onEnter = j.value(QStringLiteral("enter")).toString();
        e.onTabEmpty = j.value(QStringLiteral("tabEmpty")).toString();
        e.onTabEnd = j.value(QStringLiteral("tabEnd")).toString();
        e.tabInserts = j.value(QStringLiteral("tabInserts")).toString();
        e.prefix = j.value(QStringLiteral("prefix")).toString();
        e.suffix = j.value(QStringLiteral("suffix")).toString();
        e.completions = strings(j.value(QStringLiteral("completions")));
        e.tabCompletions = strings(j.value(QStringLiteral("tabCompletions")));
        e.completeFromScript = j.value(QStringLiteral("completeFromScript")).toBool();
        e.key = j.value(QStringLiteral("key")).toString().left(1).toUpper();
        f.elements.append(e);
    }
    // references to elements that do not exist are dropped
    for (Element &e : f.elements) {
        for (QString *ref : {&e.onEnter, &e.onTabEmpty, &e.onTabEnd})
            if (!ref->isEmpty() && f.indexOf(*ref) < 0)
                ref->clear();
    }
    if (f.indexOf(f.firstElement) < 0)
        f.firstElement = f.elements.isEmpty() ? QString() : f.elements.first().name;
    if (ok)
        *ok = !f.name.isEmpty() && !f.elements.isEmpty();
    return f;
}

bool readFile(const QString &path, Format *f, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("The file could not be read.");
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &pe);
    bool ok = false;
    const Format got = doc.isObject() ? fromJson(doc.object(), &ok) : Format();
    if (!ok) {
        if (error)
            *error = QObject::tr("This is not a script format file.");
        return false;
    }
    *f = got;
    return true;
}

bool writeFile(const QString &path, const Format &f, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(QJsonDocument(toJson(f)).toJson(QJsonDocument::Indented)) < 0) {
        if (error)
            *error = QObject::tr("The file could not be written.");
        return false;
    }
    return true;
}

QString ownDir() { return dataDir() + QLatin1String("/scripts"); }

static QString ownFileName(const QString &name)
{
    QString base;
    for (const QChar c : name)
        base += c.isLetterOrNumber() ? c.toLower() : QLatin1Char('-');
    while (base.contains(QLatin1String("--")))
        base.replace(QLatin1String("--"), QLatin1String("-"));
    if (base.isEmpty())
        base = QStringLiteral("format");
    return ownDir() + QLatin1Char('/') + base + QLatin1String(".json");
}

QList<Entry> available()
{
    QList<Entry> out;
    auto put = [&out](const Entry &e) {
        for (Entry &o : out)
            if (o.format.name.compare(e.format.name, Qt::CaseInsensitive) == 0) {
                o = e;
                return;
            }
        out.append(e);
    };
    for (const Format &f : builtIn())
        put({f, Kind::BuiltIn, QString()});
    QStringList dirs = addons::contentDirs(QStringLiteral("scripts"));
    const int addonDirs = dirs.size();
    dirs << ownDir();
    for (int d = 0; d < dirs.size(); ++d) {
        const QDir dir(dirs.at(d));
        for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
            Format f;
            if (readFile(fi.absoluteFilePath(), &f))
                put({f, d < addonDirs ? Kind::Addon : Kind::Own, fi.absoluteFilePath()});
        }
    }
    return out;
}

bool saveOwn(const Format &f, QString *error)
{
    QDir().mkpath(ownDir());
    // a format of this name keeps its file
    for (const Entry &e : available())
        if (e.kind == Kind::Own && e.format.name.compare(f.name, Qt::CaseInsensitive) == 0)
            return writeFile(e.path, f, error);
    QString path = ownFileName(f.name);
    for (int n = 2; QFile::exists(path); ++n)
        path = ownFileName(f.name + QLatin1Char(' ') + QString::number(n));
    return writeFile(path, f, error);
}

bool removeOwn(const QString &name)
{
    bool removed = false;
    const QDir dir(ownDir());
    for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        Format f;
        if (readFile(fi.absoluteFilePath(), &f) && f.name.compare(name, Qt::CaseInsensitive) == 0)
            removed = QFile::remove(fi.absoluteFilePath()) || removed;
    }
    return removed;
}

// ------------------------------------------------------------------ styles

QString styleName(const QString &element) { return element.isEmpty() ? QString() : stylePrefix() + element; }

bool isScriptStyle(const QString &name) { return name.startsWith(stylePrefix(), Qt::CaseInsensitive); }

QString elementName(const QString &name) { return isScriptStyle(name) ? name.mid(stylePrefix().size()) : QString(); }

QString resolvedFontFamily(const Format &f)
{
    static QHash<QString, QString> cache;
    const auto it = cache.constFind(f.fontFamily);
    if (it != cache.constEnd())
        return it.value();
    QString found;
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        const QStringList families = QFontDatabase::families();
        QStringList wanted;
        if (!f.fontFamily.isEmpty())
            wanted << f.fontFamily;
        wanted << QStringLiteral("Courier Prime") << QStringLiteral("Courier New") << QStringLiteral("Courier Final Draft")
               << QStringLiteral("Liberation Mono") << QStringLiteral("Nimbus Mono PS") << QStringLiteral("Courier 10 Pitch")
               << QStringLiteral("FreeMono") << QStringLiteral("DejaVu Sans Mono") << QStringLiteral("Noto Sans Mono");
        for (const QString &w : wanted)
            if (families.contains(w, Qt::CaseInsensitive)) {
                found = w;
                break;
            }
    }
    if (found.isEmpty())
        found = f.fontFamily.isEmpty() ? QStringLiteral("monospace") : f.fontFamily;
    cache.insert(f.fontFamily, found);
    return found;
}

StyleSheet toStyleSheet(const Format &f)
{
    StyleSheet sheet;
    const QString family = resolvedFontFamily(f);
    const double linePx = f.fontSize * direct::kPxPerPt;
    for (const Element &e : f.elements) {
        Style s;
        s.name = styleName(e.name);
        s.bold = e.bold;
        s.italic = e.italic;
        s.underline = e.underline;
        s.keepWithNext = e.keepWithNext;
        s.chars.family = family;
        s.chars.size = f.fontSize;
        s.chars.color = e.color;
        s.chars.caps = e.allCaps ? 2 : 0;
        s.para.bits = direct::AllPara & ~direct::Tabs;
        s.para.align = e.align == 1 ? Qt::AlignHCenter : e.align == 2 ? Qt::AlignRight : Qt::AlignLeft;
        s.para.left = direct::mmToPx(e.leftMm);
        s.para.right = direct::mmToPx(e.rightMm);
        s.para.first = direct::mmToPx(e.firstMm);
        s.para.before = e.linesBefore * linePx;
        s.para.after = 0;
        s.para.lineType = QTextBlockFormat::ProportionalHeight;
        s.para.line = e.lineSpacing * 100.0;
        if (!e.onEnter.isEmpty() && f.indexOf(e.onEnter) >= 0)
            s.next = styleName(f.find(e.onEnter)->name);
        sheet.styles.append(s);
    }
    return sheet;
}

const Data &data(const Formatting &f)
{
    if (f.script)
        return *f.script;
    static const Data standardData = [] {
        Data d;
        d.format = standard();
        d.sheet = toStyleSheet(d.format);
        return d;
    }();
    return standardData;
}

Formatting withFormat(Formatting f, const Format &format)
{
    auto *d = new Data;
    d->format = format;
    d->sheet = toStyleSheet(format);
    f.script = QSharedPointer<const Data>(d);
    return f;
}

const Element *element(const Formatting &f, const QString &style)
{
    return isScriptStyle(style) ? format(f).find(elementName(style)) : nullptr;
}

QHash<QString, QString> renamedStyles(const Format &from, const Format &to,
                                      const QHash<QString, QString> &renamedElements)
{
    QHash<QString, QString> out;
    for (const Element &e : from.elements) {
        const QString wanted = renamedElements.value(e.name);
        if (!wanted.isEmpty() && to.indexOf(wanted) >= 0) {
            if (wanted != e.name)
                out.insert(styleName(e.name), styleName(to.find(wanted)->name));
            continue;
        }
        if (const Element *same = to.find(e.name)) {
            if (same->name != e.name)
                out.insert(styleName(e.name), styleName(same->name));
            continue;
        }
        if (const Element *alike = to.byRole(e.role))
            out.insert(styleName(e.name), styleName(alike->name));
    }
    return out;
}

// ------------------------------------------------------------------ documents

QString elementAt(const QTextDocument *doc, int position)
{
    if (!doc)
        return QString();
    const QTextBlock b = doc->findBlock(position);
    return b.isValid() ? elementName(b.blockFormat().stringProperty(props::Style)) : QString();
}

QStringList usedTexts(const QTextDocument *doc, const Formatting &f, const Element &el, int part, int exceptBlock)
{
    Q_UNUSED(f);
    QStringList out;
    if (!doc)
        return out;
    QSet<QString> seen;
    const QString style = styleName(el.name);
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        if (b.blockNumber() == exceptBlock
            || b.blockFormat().stringProperty(props::Style).compare(style, Qt::CaseInsensitive) != 0)
            continue;
        QString t = b.text();
        if (!el.prefix.isEmpty() && t.startsWith(el.prefix))
            t = t.mid(el.prefix.size());
        if (!el.suffix.isEmpty() && t.endsWith(el.suffix))
            t.chop(el.suffix.size());
        if (!el.tabInserts.isEmpty()) {
            const int at = t.indexOf(el.tabInserts);
            if (part == 0)
                t = at >= 0 ? t.left(at) : t;
            else
                t = at >= 0 ? t.mid(at + el.tabInserts.size()) : QString();
        } else if (part != 0) {
            t.clear();
        }
        if (el.role == Role::Character) {
            // "ANNA (V.O.)", "ANNA (CONT'D)": the name only
            const int paren = t.indexOf(QLatin1String(" ("));
            if (paren > 0)
                t = t.left(paren);
        }
        t = t.trimmed();
        if (el.allCaps)
            t = t.toUpper();
        if (t.isEmpty() || seen.contains(t))
            continue;
        seen.insert(t);
        out << t;
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

bool hasElements(const QTextDocument *doc)
{
    if (!doc)
        return false;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        if (isScriptStyle(b.blockFormat().stringProperty(props::Style)))
            return true;
    return false;
}

} // namespace script
} // namespace qb
