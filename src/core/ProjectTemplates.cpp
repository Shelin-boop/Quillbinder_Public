#include "core/ProjectTemplates.h"
#include "core/Paths.h"
#include "core/Addons.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/Zip.h"
#include "text/MarkdownIO.h"
#include "text/TextUtil.h"

#include "AppInfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextDocument>

namespace qb {
namespace templates {

QString userTemplatesDir()
{
    return dataDir() + QLatin1String("/Templates");
}

static QString &sharedDir()
{
    static QString d;
    return d;
}

void setSharedTemplatesDir(const QString &dir) { sharedDir() = dir; }
QString sharedTemplatesDir() { return sharedDir(); }

QStringList categories()
{
    return {QObject::tr("Blank"), QObject::tr("Fiction"), QObject::tr("Nonfiction"), QObject::tr("My Templates")};
}

QList<Info> all()
{
    QList<Info> l = {
        {QStringLiteral("blank"), QObject::tr("Blank"), QObject::tr("Blank"),
         QObject::tr("An empty project with a Draft, a Research folder and one empty document."),
         QStringLiteral("text-x-generic"), QString()},
        {QStringLiteral("novel"), QObject::tr("Fiction"), QObject::tr("Novel"),
         QObject::tr("Chapters as folders, scenes as documents. Includes character and setting sheets, a title "
                     "page and notes folders."),
         QStringLiteral("view-pim-journal"), QString()},
        {QStringLiteral("novel-parts"), QObject::tr("Fiction"), QObject::tr("Novel (with Parts)"),
         QObject::tr("Like the Novel template, with an additional Part level above the chapters."),
         QStringLiteral("view-pim-journal"), QString()},
        {QStringLiteral("short-story"), QObject::tr("Fiction"), QObject::tr("Short Story"),
         QObject::tr("Scenes directly in the Draft, separated by scene breaks when compiled."),
         QStringLiteral("document-edit"), QString()},
        {QStringLiteral("non-fiction"), QObject::tr("Nonfiction"), QObject::tr("Nonfiction Book"),
         QObject::tr("Chapters with sections and sub-sections, plus folders for notes and references."),
         QStringLiteral("x-office-document"), QString()},
        {QStringLiteral("essay"), QObject::tr("Nonfiction"), QObject::tr("Essay / Paper"),
         QObject::tr("Introduction, body sections, conclusion and bibliography."),
         QStringLiteral("x-office-document"), QString()},
    };
    // templates from add-ons, then the user's own, then the shared folder (0.21)
    QStringList dirs = addons::contentDirs(QStringLiteral("templates"));
    const int addonDirs = dirs.size();
    dirs << userTemplatesDir();
    const bool shared = !sharedDir().isEmpty() && QDir(sharedDir()) != QDir(userTemplatesDir());
    if (shared)
        dirs << sharedDir();
    for (int d = 0; d < dirs.size(); ++d) {
        const QDir dir(dirs.at(d));
        for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.zip")}, QDir::Files, QDir::Name)) {
            ZipReader zr(fi.absoluteFilePath());
            const QJsonObject meta = QJsonDocument::fromJson(zr.fileData(QStringLiteral("template.json"))).object();
            Info i;
            i.id = (d < addonDirs ? QStringLiteral("addon:") : shared && d == dirs.size() - 1 ? QStringLiteral("shared:") : QStringLiteral("user:"))
                   + fi.completeBaseName();
            i.category = meta.value(QStringLiteral("category")).toString(QObject::tr("My Templates"));
            i.name = meta.value(QStringLiteral("name")).toString(fi.completeBaseName());
            i.description = meta.value(QStringLiteral("description")).toString();
            i.icon = meta.value(QStringLiteral("icon")).toString(QStringLiteral("folder-templates"));
            i.zipPath = fi.absoluteFilePath();
            l << i;
        }
    }
    return l;
}

void setMarkdown(Project *p, BinderItem *item, const QString &markdown)
{
    QTextDocument tmp;
    md::read(markdown, &tmp, p->formatting);
    p->docs()->setContent(item->id, &tmp);
}

namespace {

struct Builder {
    Project *p;
    BinderItem *add(BinderItem *parent, ItemType t, const QString &title, const QString &md = QString(),
                    const QString &icon = QString())
    {
        BinderItem *it = p->model()->createItem(parent, -1, t, title);
        if (!icon.isEmpty())
            it->icon = icon;
        if (!md.isEmpty())
            setMarkdown(p, it, md);
        return it;
    }
    QString type(const QString &name)
    {
        const QString id = newId();
        p->sectionTypes.append({id, name});
        return id;
    }
};

QString characterSheet()
{
    // 0.60: fields of this program's own choice and order (written new for the public release)
    return QObject::tr(
        "# Character Name\n"
        "**In one sentence:** \n"
        "**Part in the plot:** \n"
        "**Age and appearance:** \n"
        "**Home and work:** \n"
        "**Voice (how they talk):** \n"
        "**Goal:** \n"
        "**Fear:** \n"
        "**Secret:** \n"
        "**Ties to other characters:** \n"
        "**From the first scene to the last:** \n"
        "**First appearance:** \n"
        "**Open questions:** \n");
}

QString settingSheet()
{
    return QObject::tr(
        "# Place Name\n"
        "**In one sentence:** \n"
        "**Where it is:** \n"
        "**By day and by night:** \n"
        "**What one hears and smells there:** \n"
        "**Who comes here:** \n"
        "**What happened here before:** \n"
        "**Scenes set here:** \n"
        "**Open questions:** \n");
}

QString titlePage()
{
    return QStringLiteral("->%1<-\n->%2<-\n\n\n\n\n::Title:: <$projecttitle>\n->%3<-\n->%4<-\n")
        .arg(QObject::tr("<$author>"), QObject::tr("About <$wc100> words"), QObject::tr("by"), QObject::tr("<$author>"));
}

void addFictionExtras(Builder &b, bool withTemplates)
{
    Project *p = b.p;
    BinderItem *templatesFolder = nullptr;
    BinderItem *charSheet = nullptr, *placeSheet = nullptr;
    if (withTemplates) {
        templatesFolder = b.add(p->root(), ItemType::Folder, QObject::tr("Template Sheets"));
        charSheet = b.add(templatesFolder, ItemType::Text, QObject::tr("Character Sheet"), characterSheet(),
                          QStringLiteral("user-identity"));
        placeSheet = b.add(templatesFolder, ItemType::Text, QObject::tr("Setting Sheet"), settingSheet(),
                           QStringLiteral("globe"));
        p->templatesFolderId = templatesFolder->id;
    }
    BinderItem *chars = b.add(p->root(), ItemType::Folder, QObject::tr("Characters"), QString(),
                              QStringLiteral("x-office-address-book"));
    BinderItem *places = b.add(p->root(), ItemType::Folder, QObject::tr("Places"), QString(), QStringLiteral("globe"));
    if (charSheet) {
        chars->defaultTemplate = charSheet->id;
        places->defaultTemplate = placeSheet->id;
    }
    b.add(p->root(), ItemType::Folder, QObject::tr("Notes"), QString(), QStringLiteral("view-pim-notes"));
    BinderItem *front = b.add(p->root(), ItemType::Folder, QObject::tr("Front Matter"), QString(),
                              QStringLiteral("folder-grey"));
    BinderItem *tp = b.add(front, ItemType::Text, QObject::tr("Title Page"), titlePage());
    p->compile.frontMatterId = front->id;
    // title page uses its own section type
    const QString tpType = b.type(QObject::tr("Title Page"));
    tp->sectionType = tpType;
    // move helper folders after Research
    QList<BinderItem *> order;
    for (BinderItem *c : p->root()->children)
        order << c;
    Q_UNUSED(order);
}

void buildTemplate(Project *p, const QString &id)
{
    Builder b{p};
    p->sectionTypes.clear();
    p->structureDefaults = StructureDefaults();
    if (id == QLatin1String("novel") || id == QLatin1String("novel-parts")) {
        const QString part = b.type(QObject::tr("Part"));
        const QString chapter = b.type(QObject::tr("Chapter"));
        const QString scene = b.type(QObject::tr("Scene"));
        b.type(QObject::tr("Section"));
        if (id == QLatin1String("novel-parts")) {
            p->structureDefaults.folders = {part, part, chapter};
            BinderItem *pt = b.add(p->draft(), ItemType::Folder, QObject::tr("Part One"), QString(),
                                   QStringLiteral("folder-yellow"));
            BinderItem *ch = b.add(pt, ItemType::Folder, QObject::tr("Chapter"));
            b.add(ch, ItemType::Text, QObject::tr("Scene"));
        } else {
            p->structureDefaults.folders = {chapter};
            BinderItem *ch = b.add(p->draft(), ItemType::Folder, QObject::tr("Chapter"));
            b.add(ch, ItemType::Text, QObject::tr("Scene"));
        }
        p->structureDefaults.fileGroups = {scene};
        p->structureDefaults.files = {scene};
        p->compile.formatName = QStringLiteral("Manuscript (Times)");
        addFictionExtras(b, true);
        p->labelTitle = QObject::tr("POV");
    } else if (id == QLatin1String("short-story")) {
        const QString scene = b.type(QObject::tr("Scene"));
        const QString heading = b.type(QObject::tr("Heading"));
        p->structureDefaults.folders = {heading};
        p->structureDefaults.fileGroups = {scene};
        p->structureDefaults.files = {scene};
        b.add(p->draft(), ItemType::Text, QObject::tr("Scene"));
        p->compile.formatName = QStringLiteral("Manuscript (Times)");
        addFictionExtras(b, true);
    } else if (id == QLatin1String("non-fiction")) {
        const QString part = b.type(QObject::tr("Part"));
        const QString chapter = b.type(QObject::tr("Chapter"));
        const QString section = b.type(QObject::tr("Section"));
        const QString sub = b.type(QObject::tr("Subsection"));
        Q_UNUSED(part);
        p->structureDefaults.folders = {chapter};
        p->structureDefaults.fileGroups = {section, section, sub};
        p->structureDefaults.files = {section, section, sub};
        BinderItem *ch = b.add(p->draft(), ItemType::Folder, QObject::tr("Chapter"));
        b.add(ch, ItemType::Text, QObject::tr("Section"));
        b.add(p->root(), ItemType::Folder, QObject::tr("Notes"), QString(), QStringLiteral("view-pim-notes"));
        b.add(p->root(), ItemType::Folder, QObject::tr("References"), QString(), QStringLiteral("bookmarks"));
        BinderItem *front = b.add(p->root(), ItemType::Folder, QObject::tr("Front Matter"), QString(),
                                  QStringLiteral("folder-grey"));
        BinderItem *tp = b.add(front, ItemType::Text, QObject::tr("Title Page"), titlePage());
        tp->sectionType = b.type(QObject::tr("Title Page"));
        p->compile.frontMatterId = front->id;
        p->compile.formatName = QStringLiteral("Modern");
    } else if (id == QLatin1String("essay")) {
        const QString heading = b.type(QObject::tr("Heading"));
        const QString section = b.type(QObject::tr("Section"));
        p->structureDefaults.folders = {heading};
        p->structureDefaults.fileGroups = {section};
        p->structureDefaults.files = {section};
        for (const QString &t : {QObject::tr("Introduction"), QObject::tr("Main Body"), QObject::tr("Conclusion"),
                                 QObject::tr("Bibliography")})
            b.add(p->draft(), ItemType::Text, t);
        b.add(p->root(), ItemType::Folder, QObject::tr("Notes"), QString(), QStringLiteral("view-pim-notes"));
        p->compile.formatName = QStringLiteral("Modern");
    } else { // blank
        const QString heading = b.type(QObject::tr("Heading"));
        const QString text = b.type(QObject::tr("Text"));
        p->structureDefaults.folders = {heading};
        p->structureDefaults.fileGroups = {text};
        p->structureDefaults.files = {text};
        b.add(p->draft(), ItemType::Text, QObject::tr("Untitled"));
        p->compile.formatName = QStringLiteral("Manuscript (Times)");
    }
    // Keep Research and Trash at the end, helper folders between.
    BinderItem *root = p->root();
    root->children.removeAll(p->research());
    root->children.insert(1, p->research());
}

} // namespace

std::unique_ptr<Project> createProject(const Info &info, const QString &parentDir, const QString &name, QString *error)
{
    if (!info.zipPath.isEmpty()) {
        const QString safe = text::safeFileName(name);
        const QString folder = QDir(parentDir).filePath(safe + QLatin1String(QB_PROJECT_SUFFIX));
        if (QFileInfo::exists(folder)) {
            if (error)
                *error = QObject::tr("A project called \"%1\" already exists in this folder.").arg(name);
            return nullptr;
        }
        QTemporaryDir tmp;
        ZipReader zr(info.zipPath);
        if (!zr.isValid() || !zr.extractAll(tmp.path())) {
            if (error)
                *error = QObject::tr("The template file is damaged.");
            return nullptr;
        }
        // find the project folder inside
        QString src;
        for (const QFileInfo &fi : QDir(tmp.path()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (fi.fileName().endsWith(QLatin1String(QB_PROJECT_SUFFIX)))
                src = fi.absoluteFilePath();
        if (src.isEmpty() || !QDir().rename(src, folder)) {
            // rename across devices fails: copy recursively
            if (src.isEmpty()) {
                if (error)
                    *error = QObject::tr("The template file is damaged.");
                return nullptr;
            }
            QDir().mkpath(folder);
            ZipReader again(info.zipPath);
            QTemporaryDir t2;
            again.extractAll(t2.path());
            // simple recursive copy
            std::function<void(const QString &, const QString &)> copyRec = [&](const QString &a, const QString &b) {
                QDir().mkpath(b);
                for (const QFileInfo &fi : QDir(a).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                    if (fi.isDir())
                        copyRec(fi.absoluteFilePath(), b + QLatin1Char('/') + fi.fileName());
                    else
                        QFile::copy(fi.absoluteFilePath(), b + QLatin1Char('/') + fi.fileName());
                }
            };
            copyRec(src, folder);
        }
        // rename the index file
        const QString oldIndex = Project::resolveProjectFile(folder);
        const QString newIndex = QDir(folder).filePath(safe + QLatin1String(QB_PROJECT_FILE_SUFFIX));
        if (!oldIndex.isEmpty() && oldIndex != newIndex)
            QFile::rename(oldIndex, newIndex);
        QFile::remove(QDir(folder).filePath(QStringLiteral("template.json")));
        auto p = Project::open(folder, error);
        if (!p)
            return nullptr;
        p->setTitle(name);
        p->history.clear();
        p->startSession(true);
        p->save(error);
        return p;
    }
    auto p = Project::create(parentDir, name, error);
    if (!p)
        return nullptr;
    buildTemplate(p.get(), info.id);
    p->model()->refreshAll();
    p->save(error);
    return p;
}

bool saveAsTemplate(Project *p, const QString &name, const QString &category, const QString &description,
                    QString *error)
{
    p->save(error);
    QDir().mkpath(userTemplatesDir());
    const QString path = QDir(userTemplatesDir()).filePath(text::safeFileName(name) + QLatin1String(".zip"));
    ZipWriter zw(path);
    if (!zw.isOpen()) {
        if (error)
            *error = zw.errorString();
        return false;
    }
    QJsonObject meta;
    meta.insert(QStringLiteral("name"), name);
    meta.insert(QStringLiteral("category"), category);
    meta.insert(QStringLiteral("description"), description);
    zw.addFile(QStringLiteral("template.json"), QJsonDocument(meta).toJson());
    zw.addDirectory(p->folderPath(), QFileInfo(p->folderPath()).fileName());
    return zw.close();
}

} // namespace templates
} // namespace qb
