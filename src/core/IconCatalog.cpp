#include "core/IconCatalog.h"
#include "core/ItemIcons.h"
#include "core/Project.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>

namespace qb {
namespace icons {

namespace {

struct Def {
    const char *id;
    const char *label;
    const char *group;
    bool known;
};

// known = the label was seen in Scrivener's menu (the user's screenshots) or the
// Scrivener name in a real project; the others are this program's own choice
// for submenus whose contents were not seen (marked in the manual as well).
const Def kDefs[] = {
    {"qb-book-blue", "Blue Book", "Book", true},
    {"qb-notebook-blue", "Blue Notebook", "Book", true},
    {"qb-book-brown", "Brown Book", "Book", true},
    {"qb-composition", "Composition", "Book", true},
    {"qb-book-green", "Green Book", "Book", true},
    {"qb-notebook-green", "Green Notebook", "Book", true},
    {"qb-notebook-orange", "Orange Notebook", "Book", true},
    {"qb-book-red", "Red Book", "Book", true},
    {"qb-book-yellow", "Yellow Book", "Book", true},

    {"qb-character-sheet", "Character Sheet", "Characters", true},
    {"qb-character-photo", "Photo", "Characters", true},
    {"qb-character-female", "Woman", "Characters", false},
    {"qb-character-male", "Man", "Characters", false},
    {"qb-character-group", "Group", "Characters", false},

    {"qb-flag-blue", "Blue Flag", "Flag", true},
    {"qb-flag-green", "Green Flag", "Flag", true},
    {"qb-flag-orange", "Orange Flag", "Flag", true},
    {"qb-flag-purple", "Purple Flag", "Flag", true},
    {"qb-flag-red", "Red Flag", "Flag", true},
    {"qb-flag-white", "White Flag", "Flag", true},
    {"qb-flag-yellow", "Yellow Flag", "Flag", true},

    {"qb-location-sheet", "Location Sheet", "Locations", true},
    {"qb-location-map", "Map", "Locations", true},
    {"qb-location-globe", "Globe", "Locations", false},
    {"qb-location-house", "House", "Locations", false},

    {"qb-notepad-blue", "Blue Notepad", "Notes", true},
    {"qb-notepad-green", "Green Notepad", "Notes", true},
    {"qb-notepad-red", "Red Notepad", "Notes", true},
    {"qb-notepad-white", "White Notepad", "Notes", true},
    {"qb-notepad-yellow", "Yellow Notepad", "Notes", true},

    {"qb-checked", "Ticked", "To Do", true},
    {"qb-unchecked", "Unticked", "To Do", true},
    {"qb-pass", "Pass (Green Tick)", "To Do", false},
    {"qb-fail", "Fail (Red Cross)", "To Do", false},
    {"qb-unclear", "Unclear (Orange Circle)", "To Do", false},

    {"qb-tv-blue", "Blue TV", "TV", false},
    {"qb-tv-green", "Green TV", "TV", false},
    {"qb-tv-grey", "Grey TV", "TV", false},
    {"qb-tv-red", "Red TV", "TV", false},
    {"qb-tv-yellow", "Yellow TV", "TV", false},

    {"qb-waystation-blue", "Blue Way-Station", "Way-Station", false},
    {"qb-waystation-green", "Green Way-Station", "Way-Station", false},
    {"qb-waystation-orange", "Orange Way-Station", "Way-Station", false},
    {"qb-waystation-purple", "Purple Way-Station", "Way-Station", false},
    {"qb-waystation-red", "Red Way-Station", "Way-Station", false},
    {"qb-waystation-yellow", "Yellow Way-Station", "Way-Station", false},

    {"qb-back-matter", "Back Matter", "", true},
    {"qb-blackboard", "Blackboard", "", true},
    {"qb-calendar", "Calendar", "", true},
    {"qb-clapper", "Clapper Board", "", true},
    {"qb-clock", "Clock", "", true},
    {"qb-cog", "Cog", "", true},
    {"qb-eye", "Eye", "", true},
    {"qb-figures", "Figures and Graphs", "", true},
    {"qb-filing-cabinet", "Filing Cabinet", "", true},
    {"qb-front-matter", "Front Matter", "", true},
    {"qb-information", "Information", "", true},
    {"qb-lectern", "Lectern", "", true},
    {"qb-light-bulb", "Light Bulb", "", true},
    {"qb-magnifier", "Magnifying Glass", "", true},
    {"qb-music", "Music", "", true},
    {"qb-musical-notes", "Musical Notes", "", true},
    {"qb-outline", "Outline", "", true},
    {"qb-pencil", "Pencil", "", true},
    {"qb-proofread", "Proofread", "", true},
    {"qb-question", "Question Mark", "", true},
    {"qb-reviewer", "Reviewer Comments", "", true},
    {"qb-scissors", "Scissors", "", true},
    {"qb-speech-bubble", "Speech Bubble", "", true},
    {"qb-speech-marks", "Speech Marks", "", true},
    {"qb-statistics", "Statistics", "", true},
    {"qb-sync-folder", "Sync Folder", "", true},
    {"qb-tables", "Tables", "", true},
    {"qb-test-tube", "Test Tube", "", true},
    {"qb-thought-bubble", "Thought Bubble", "", true},
    {"qb-warning", "Warning", "", true},
};

// icons of the desktop theme offered before 0.17, with their nearest Scrivener icon
struct Legacy {
    const char *id;
    const char *label;
    const char *scrivener;
};
const Legacy kLegacy[] = {
    {"folder", "Folder (Blue)", ""},
    {"folder-red", "Folder (Red)", ""},
    {"folder-orange", "Folder (Orange)", ""},
    {"folder-yellow", "Folder (Yellow)", ""},
    {"folder-green", "Folder (Green)", ""},
    {"folder-cyan", "Folder (Cyan)", ""},
    {"folder-violet", "Folder (Violet)", ""},
    {"folder-brown", "Folder (Brown)", ""},
    {"folder-grey", "Folder (Grey)", ""},
    {"view-pim-journal", "Book", "Book (Blue Book)"},
    {"text-x-generic", "Page", ""},
    {"user-identity", "Character", "Characters (Character Sheet)"},
    {"x-office-address-book", "Characters", "Characters (Character Sheet)"},
    {"globe", "Place", "Locations (Location Sheet)"},
    {"map-flat", "Map", "Locations (Map)"},
    {"view-calendar", "Timeline", "Calendar"},
    {"help-hint", "Idea", "Light Bulb"},
    {"view-pim-notes", "Notes", "Notes (Yellow Notepad)"},
    {"bookmarks", "Bookmark", ""},
    {"flag", "Flag", "Flag (Red Flag)"},
    {"star-shape", "Star", "Reviewer Comments"},
    {"tag", "Tag", ""},
    {"camera-photo", "Photo", "Characters (Photo)"},
    {"mail-attachment", "Attachment", ""},
    {"document-edit", "Draft", "Pencil"},
    {"chronometer", "Clock", "Clock"},
    {"applications-education-language", "Language", ""},
    {"office-chart-bar", "Chart", "Statistics"},
};

QString scrivName(const CatalogEntry &e)
{
    return e.group.isEmpty() ? e.label : QStringLiteral("%1 (%2)").arg(e.group, e.label);
}

QString norm(QString s)
{
    s = s.toLower();
    s.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
    return s;
}

} // namespace

const QList<CatalogEntry> &catalog()
{
    static const QList<CatalogEntry> list = [] {
        QList<CatalogEntry> l;
        for (const Def &d : kDefs) {
            CatalogEntry e;
            e.id = QLatin1String(d.id);
            e.label = QObject::tr(d.label);
            e.group = QLatin1String(d.group);
            e.known = d.known;
            // the Scrivener name uses the English words whatever the language of the menus
            CatalogEntry en = e;
            en.label = QLatin1String(d.label);
            e.scrivener = scrivName(en);
            l << e;
        }
        return l;
    }();
    return list;
}

QStringList groups()
{
    QStringList g;
    for (const CatalogEntry &e : catalog())
        if (!e.group.isEmpty() && !g.contains(e.group))
            g << e.group;
    return g;
}

QString groupIcon(const QString &group)
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("Book"), QStringLiteral("qb-book-blue")},
        {QStringLiteral("Characters"), QStringLiteral("qb-character-sheet")},
        {QStringLiteral("Flag"), QStringLiteral("qb-flag-blue")},
        {QStringLiteral("Locations"), QStringLiteral("qb-location-sheet")},
        {QStringLiteral("Notes"), QStringLiteral("qb-notepad-blue")},
        {QStringLiteral("To Do"), QStringLiteral("qb-checked")},
        {QStringLiteral("TV"), QStringLiteral("qb-tv-grey")},
        {QStringLiteral("Way-Station"), QStringLiteral("qb-waystation-blue")},
    };
    return map.value(group);
}

QList<CatalogEntry> inGroup(const QString &group)
{
    QList<CatalogEntry> l;
    for (const CatalogEntry &e : catalog())
        if (e.group == group)
            l << e;
    return l;
}

const CatalogEntry *findEntry(const QString &id)
{
    for (const CatalogEntry &e : catalog())
        if (e.id == id)
            return &e;
    return nullptr;
}

QString label(const QString &id)
{
    if (id.isEmpty())
        return QObject::tr("Default");
    if (const CatalogEntry *e = findEntry(id))
        return e->label;
    if (isCustom(id))
        return customName(id);
    if (id.startsWith(QLatin1String("scriv:")))
        return id.mid(6);
    for (const Legacy &l : kLegacy)
        if (id == QLatin1String(l.id))
            return QObject::tr(l.label);
    return id;
}

QString scrivenerName(const QString &id)
{
    if (id.isEmpty())
        return QString();
    if (const CatalogEntry *e = findEntry(id))
        return e->scrivener;
    if (id.startsWith(QLatin1String("scriv:")))
        return id.mid(6);
    if (isCustom(id))
        return customName(id);
    for (const Legacy &l : kLegacy)
        if (id == QLatin1String(l.id))
            return QLatin1String(l.scrivener);
    return QString();
}

QString fromScrivener(const QString &name)
{
    const QString n = name.trimmed();
    if (n.isEmpty())
        return QString();
    const QString key = norm(n);
    for (const CatalogEntry &e : catalog())
        if (norm(e.scrivener) == key)
            return e.id;
    // "Green Book" without its submenu
    const int count = int(sizeof(kDefs) / sizeof(kDefs[0]));
    for (int i = 0; i < count && i < catalog().size(); ++i)
        if (norm(QLatin1String(kDefs[i].label)) == key)
            return catalog().at(i).id;
    return QString();
}

QString lookalike(const QString &scrivenerName)
{
    // a Scrivener icon without a picture here: the nearest one by its words
    const QString s = scrivenerName.toLower();
    struct Word {
        const char *word;
        const char *id;
    };
    static const Word words[] = {
        {"character", "qb-character-sheet"}, {"person", "qb-character-male"}, {"people", "qb-character-group"},
        {"photo", "qb-character-photo"}, {"location", "qb-location-sheet"}, {"place", "qb-location-sheet"},
        {"map", "qb-location-map"}, {"globe", "qb-location-globe"}, {"notebook", "qb-notebook-blue"},
        {"book", "qb-book-blue"}, {"note", "qb-notepad-yellow"}, {"flag", "qb-flag-red"},
        {"way", "qb-waystation-blue"}, {"tv", "qb-tv-grey"}, {"tick", "qb-checked"}, {"star", "qb-reviewer"},
        {"idea", "qb-light-bulb"}, {"bulb", "qb-light-bulb"}, {"calendar", "qb-calendar"}, {"time", "qb-clock"},
        {"clock", "qb-clock"}, {"music", "qb-music"}, {"question", "qb-question"}, {"warning", "qb-warning"},
    };
    for (const Word &w : words)
        if (s.contains(QLatin1String(w.word)))
            return QLatin1String(w.id);
    return QStringLiteral("text-x-generic");
}

// ------------------------------------------------------------------ own icons

QString customDir(const Project *project)
{
    return project ? project->folderPath() + QStringLiteral("/icons") : QString();
}

QStringList customNames(const Project *project)
{
    QStringList names;
    if (!project)
        return names;
    const QDir d(customDir(project));
    for (const QFileInfo &fi : d.entryInfoList({QStringLiteral("*.png")}, QDir::Files, QDir::Name | QDir::IgnoreCase))
        names << fi.completeBaseName();
    return names;
}

QString customPath(const Project *project, const QString &name)
{
    return customDir(project) + QLatin1Char('/') + name + QStringLiteral(".png");
}

bool isCustom(const QString &id) { return id.startsWith(QLatin1String("custom:")); }

QString customName(const QString &id) { return isCustom(id) ? id.mid(7) : QString(); }

static QString safeName(QString n)
{
    n = n.trimmed();
    n.replace(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|\\x00-\\x1f]")), QStringLiteral("-"));
    return n.left(60);
}

QString addCustom(Project *project, const QString &wanted, const QImage &picture, QString *error)
{
    if (!project || picture.isNull()) {
        if (error)
            *error = QObject::tr("The picture could not be read.");
        return QString();
    }
    QString name = safeName(wanted);
    if (name.isEmpty())
        name = QObject::tr("Icon");
    QDir().mkpath(customDir(project));
    const QStringList taken = customNames(project);
    QString unique = name;
    for (int n = 2; taken.contains(unique, Qt::CaseInsensitive); ++n)
        unique = QStringLiteral("%1 %2").arg(name).arg(n);
    QImage img = picture.convertToFormat(QImage::Format_ARGB32);
    if (img.width() > 64 || img.height() > 64)
        img = img.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!img.save(customPath(project, unique), "PNG")) {
        if (error)
            *error = QObject::tr("The icon could not be saved in the project.");
        return QString();
    }
    clearCache();
    return QStringLiteral("custom:") + unique;
}

bool removeCustom(Project *project, const QString &name)
{
    const bool ok = QFile::remove(customPath(project, name));
    clearCache();
    return ok;
}

bool renameCustom(Project *project, const QString &from, const QString &to)
{
    const QString t = safeName(to);
    if (t.isEmpty() || customNames(project).contains(t, Qt::CaseInsensitive))
        return false;
    const bool ok = QFile::rename(customPath(project, from), customPath(project, t));
    clearCache();
    return ok;
}

QImage textIcon(const QString &text, const QColor &box, const QColor &ink, const QColor &border, int size)
{
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const qreal s = size / 32.0;
    const QRectF r(2.5 * s, 4.5 * s, 27 * s, 23 * s);
    QPen pen(border.isValid() ? border : QColor(58, 58, 58), 1.8 * s);
    p.setPen(border.alpha() == 0 ? Qt::NoPen : pen);
    p.setBrush(box.isValid() ? box : Qt::white);
    p.drawRoundedRect(r, 3 * s, 3 * s);
    const QString t = text.trimmed().left(3);
    QFont f(QStringLiteral("DejaVu Sans"));
    f.setBold(true);
    f.setItalic(true);
    f.setPixelSize(int((t.size() <= 1 ? 17 : t.size() == 2 ? 14.5 : 10.5) * s));
    p.setFont(f);
    p.setPen(ink.isValid() ? ink : QColor(26, 26, 26));
    p.drawText(r.adjusted(0, -0.5 * s, 0, 0), Qt::AlignCenter, t);
    p.end();
    return img;
}

QIcon iconFor(const Project *project, const QString &id)
{
    if (id.isEmpty())
        return QIcon();
    if (isCustom(id)) {
        const QString path = customPath(project, customName(id));
        if (QFile::exists(path))
            return QIcon(path);
        return get(QStringLiteral("qb-question"));
    }
    if (id.startsWith(QLatin1String("scriv:")))
        return get(lookalike(id.mid(6)));
    return get(id);
}

} // namespace icons
} // namespace qb
