#include "core/Project.h"
#include "core/Objects.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "import/ScrivenerFormat.h"
#include "text/DisplayFormat.h"
#include "text/Script.h"
#include "text/Styles.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"

#include "AppInfo.h"

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextCursor>
#include <QTextDocument>
#include <QSaveFile>
#include <QTimer>
#include <QXmlStreamReader>
#include <cmath>

namespace qb {

// ---------------------------------------------------------------- types

QString itemTypeToString(ItemType t)
{
    switch (t) {
    case ItemType::Draft: return QStringLiteral("Draft");
    case ItemType::Research: return QStringLiteral("Research");
    case ItemType::Trash: return QStringLiteral("Trash");
    case ItemType::Folder: return QStringLiteral("Folder");
    case ItemType::Text: return QStringLiteral("Text");
    case ItemType::Image: return QStringLiteral("Image");
    case ItemType::Pdf: return QStringLiteral("PDF");
    case ItemType::Media: return QStringLiteral("Media");
    case ItemType::WebPage: return QStringLiteral("WebPage");
    case ItemType::File: return QStringLiteral("File");
    }
    return QStringLiteral("Text");
}

ItemType itemTypeFromString(const QString &s)
{
    static const QHash<QString, ItemType> map = {
        {QStringLiteral("Draft"), ItemType::Draft},     {QStringLiteral("Research"), ItemType::Research},
        {QStringLiteral("Trash"), ItemType::Trash},     {QStringLiteral("Folder"), ItemType::Folder},
        {QStringLiteral("Text"), ItemType::Text},       {QStringLiteral("Image"), ItemType::Image},
        {QStringLiteral("PDF"), ItemType::Pdf},         {QStringLiteral("Media"), ItemType::Media},
        {QStringLiteral("WebPage"), ItemType::WebPage}, {QStringLiteral("File"), ItemType::File}};
    return map.value(s, ItemType::Text);
}

bool isFolderType(ItemType t)
{
    return t == ItemType::Draft || t == ItemType::Research || t == ItemType::Trash || t == ItemType::Folder;
}

bool isTextType(ItemType t) { return isFolderType(t) || t == ItemType::Text; }

bool isMediaType(ItemType t)
{
    return t == ItemType::Image || t == ItemType::Pdf || t == ItemType::Media || t == ItemType::WebPage
           || t == ItemType::File;
}

const QStringList &mediaSuffixes()
{
    static const QStringList list = {
        QStringLiteral("mp3"),  QStringLiteral("wav"),  QStringLiteral("ogg"),  QStringLiteral("oga"),  QStringLiteral("opus"),
        QStringLiteral("flac"), QStringLiteral("m4a"),  QStringLiteral("aac"),  QStringLiteral("wma"),  QStringLiteral("aif"),
        QStringLiteral("aiff"), QStringLiteral("amr"),  QStringLiteral("mka"),  QStringLiteral("ac3"),  QStringLiteral("mp4"),
        QStringLiteral("m4v"),  QStringLiteral("mov"),  QStringLiteral("mkv"),  QStringLiteral("webm"), QStringLiteral("avi"),
        QStringLiteral("mpg"),  QStringLiteral("mpeg"), QStringLiteral("mts"),  QStringLiteral("m2ts"), QStringLiteral("wmv"),
        QStringLiteral("flv"),  QStringLiteral("3gp"),  QStringLiteral("ogv")};
    return list;
}

bool isAudioSuffix(const QString &suffix)
{
    static const QStringList audio = {QStringLiteral("mp3"),  QStringLiteral("wav"), QStringLiteral("ogg"),
                                      QStringLiteral("oga"),  QStringLiteral("opus"), QStringLiteral("flac"),
                                      QStringLiteral("m4a"),  QStringLiteral("aac"), QStringLiteral("wma"),
                                      QStringLiteral("aif"),  QStringLiteral("aiff"), QStringLiteral("amr"),
                                      QStringLiteral("mka"),  QStringLiteral("ac3")};
    return audio.contains(suffix.toLower());
}

// ---------------------------------------------------------------- project

Project::Project(QObject *parent) : QObject(parent)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(2000);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] {
        QString err;
        if (!save(&err))
            emit saveFailed(err);
    });
}

Project::~Project()
{
    delete m_docs;
    m_docs = nullptr;
    delete m_model;
    m_model = nullptr;
    delete m_root;
}

QString Project::resolveProjectFile(const QString &path)
{
    QFileInfo fi(path);
    if (fi.isDir()) {
        const QStringList files = QDir(path).entryList({QStringLiteral("*") + QLatin1String(QB_PROJECT_FILE_SUFFIX)},
                                                       QDir::Files);
        if (files.isEmpty())
            return QString();
        return QDir(path).filePath(files.first());
    }
    if (fi.suffix() == QLatin1String(QB_PROJECT_FILE_SUFFIX).mid(1))
        return fi.absoluteFilePath();
    return QString();
}

std::unique_ptr<Project> Project::create(const QString &parentDir, const QString &name, QString *error)
{
    const QString safe = text::safeFileName(name);
    QDir d(parentDir);
    const QString folder = d.filePath(safe + QLatin1String(QB_PROJECT_SUFFIX));
    if (QFileInfo::exists(folder)) {
        if (error)
            *error = QObject::tr("A project called \"%1\" already exists in this folder.").arg(name);
        return nullptr;
    }
    if (!QDir().mkpath(folder + QLatin1String("/content")) || !QDir().mkpath(folder + QLatin1String("/snapshots"))
        || !QDir().mkpath(folder + QLatin1String("/settings"))) {
        if (error)
            *error = QObject::tr("Could not create the project folder:\n%1").arg(folder);
        return nullptr;
    }
    std::unique_ptr<Project> p(new Project);
    p->m_folder = QFileInfo(folder).absoluteFilePath();
    p->m_indexFile = QDir(p->m_folder).filePath(safe + QLatin1String(QB_PROJECT_FILE_SUFFIX));
    p->m_title = name;
    p->m_root = new BinderItem;
    p->m_root->id = QStringLiteral("ROOT");
    p->m_root->type = ItemType::Folder;
    p->setupDefaults();
    p->ensureSpecialFolders();
    p->m_model = new BinderModel(p.get());
    p->m_docs = new DocumentStore(p.get());
    p->startSession(true);
    if (!p->saveIndex(error))
        return nullptr;
    return p;
}

std::unique_ptr<Project> Project::open(const QString &path, QString *error)
{
    const QString file = resolveProjectFile(path);
    if (file.isEmpty()) {
        if (error)
            *error = QObject::tr("No %1 project was found at:\n%2").arg(QLatin1String(QB_APP_NAME), path);
        return nullptr;
    }
    std::unique_ptr<Project> p(new Project);
    p->m_indexFile = file;
    p->m_folder = QFileInfo(file).absolutePath();
    if (!p->loadIndex(error))
        return nullptr;
    QDir().mkpath(p->m_folder + QLatin1String("/content"));
    QDir().mkpath(p->m_folder + QLatin1String("/snapshots"));
    QDir().mkpath(p->m_folder + QLatin1String("/settings"));
    // the project file that just opened: the spare copy if it is ever damaged (0.20)
    if (!p->m_loadedIndex.isEmpty()) {
        QSaveFile spare(spareCopyOf(file));
        if (spare.open(QIODevice::WriteOnly) && spare.write(p->m_loadedIndex) == p->m_loadedIndex.size())
            spare.commit();
        p->m_loadedIndex.clear();
    }
    p->ensureSpecialFolders();
    p->m_model = new BinderModel(p.get());
    // ui state (before the document store, which takes zoom and page view from it)
    QFile ui(QDir(p->settingsDir()).filePath(QStringLiteral("ui.json")));
    if (ui.open(QIODevice::ReadOnly))
        p->uiState = QJsonDocument::fromJson(ui.readAll()).toVariant().toMap();
    p->m_docs = new DocumentStore(p.get());
    // snapshots flags
    for (BinderItem *it : p->allItems()) {
        QDir sd(p->snapshotsDir(it->id));
        it->hasSnapshots = sd.exists() && !sd.entryList({QStringLiteral("*.md"), QStringLiteral("*.rtf")}, QDir::Files).isEmpty();
    }
    const QString reset = p->targets.sessionReset;
    bool newSession = reset == QLatin1String("open") || !p->targets.sessionStart.isValid();
    if (reset == QLatin1String("midnight") && p->targets.sessionStart.date() != QDate::currentDate())
        newSession = true;
    p->startSession(newSession);
    return p;
}

void Project::setupDefaults()
{
    labels = {{0, tr("Red"), QColor(214, 69, 65)},     {1, tr("Orange"), QColor(235, 140, 50)},
              {2, tr("Yellow"), QColor(230, 200, 40)}, {3, tr("Green"), QColor(80, 170, 80)},
              {4, tr("Blue"), QColor(70, 130, 210)},   {5, tr("Purple"), QColor(150, 90, 190)}};
    statuses = {{0, tr("To Do")},         {1, tr("In Progress")}, {2, tr("First Draft")},
                {3, tr("Revised Draft")}, {4, tr("Final Draft")}, {5, tr("Done")}};
    const QString heading = newId();
    const QString text = newId();
    sectionTypes = {{heading, tr("Heading")}, {text, tr("Text")}};
    structureDefaults.folders = {heading};
    structureDefaults.fileGroups = {text};
    structureDefaults.files = {text};
}

void Project::ensureSpecialFolders()
{
    auto findOrCreate = [this](ItemType type, const QString &title) -> BinderItem * {
        for (BinderItem *c : m_root->children)
            if (c->type == type)
                return c;
        auto *it = new BinderItem;
        it->id = newId();
        it->type = type;
        it->title = title;
        it->parent = m_root;
        it->created = it->modified = QDateTime::currentDateTime();
        int pos = m_root->children.size();
        if (type == ItemType::Draft)
            pos = 0;
        m_root->children.insert(pos, it);
        registerItem(it);
        return it;
    };
    m_draft = findOrCreate(ItemType::Draft, tr("Draft"));
    m_research = findOrCreate(ItemType::Research, tr("Research"));
    m_trash = findOrCreate(ItemType::Trash, tr("Trash"));
    // trash is always last
    m_root->children.removeAll(m_trash);
    m_root->children.append(m_trash);
    m_items.insert(m_root->id, m_root);
}

QList<BinderItem *> Project::allItems() const
{
    return m_root ? m_root->descendants() : QList<BinderItem *>();
}

void Project::registerItem(BinderItem *it)
{
    m_items.insert(it->id, it);
    for (BinderItem *c : it->children)
        registerItem(c);
}

void Project::unregisterItem(BinderItem *it)
{
    for (BinderItem *c : it->children)
        unregisterItem(c);
    m_items.remove(it->id);
}

void Project::setTitle(const QString &t)
{
    if (t == m_title)
        return;
    m_title = t;
    markIndexDirty();
    emit titleChanged();
}

QString Project::contentDir(const QString &id) const
{
    return m_folder + QLatin1String("/content/") + id;
}

QString Project::snapshotsDir(const QString &id) const
{
    return m_folder + QLatin1String("/snapshots/") + id;
}

QString Project::settingsDir() const { return m_folder + QLatin1String("/settings"); }

QString Project::mediaFilePath(const BinderItem *item) const
{
    if (!item)
        return QString();
    return contentDir(item->id) + QLatin1String("/file.") + (item->fileExt.isEmpty() ? QStringLiteral("bin") : item->fileExt);
}

const Label *Project::labelById(int id) const
{
    for (const Label &l : labels)
        if (l.id == id)
            return &l;
    return nullptr;
}

const Status *Project::statusById(int id) const
{
    for (const Status &s : statuses)
        if (s.id == id)
            return &s;
    return nullptr;
}

const Keyword *Project::keywordById(const QString &id) const
{
    for (const Keyword &k : keywords)
        if (k.id == id)
            return &k;
    return nullptr;
}

const CustomField *Project::customFieldById(const QString &id) const
{
    for (const CustomField &f : customFields)
        if (f.id == id)
            return &f;
    return nullptr;
}

const SectionType *Project::sectionTypeById(const QString &id) const
{
    for (const SectionType &s : sectionTypes)
        if (s.id == id)
            return &s;
    return nullptr;
}

int Project::nextLabelId() const
{
    int m = -1;
    for (const Label &l : labels)
        m = qMax(m, l.id);
    return m + 1;
}

int Project::nextStatusId() const
{
    int m = -1;
    for (const Status &s : statuses)
        m = qMax(m, s.id);
    return m + 1;
}

QString Project::effectiveSectionType(const BinderItem *item, const BinderItem *compileRoot) const
{
    if (!item)
        return QString();
    if (!item->sectionType.isEmpty() && sectionTypeById(item->sectionType))
        return item->sectionType;
    // level relative to the compile root (or the top-level folder)
    const BinderItem *rootItem = compileRoot;
    if (!rootItem || !(item == rootItem || item->isDescendantOf(rootItem))) {
        rootItem = item;
        while (rootItem->parent && rootItem->parent != m_root)
            rootItem = rootItem->parent;
    }
    int level = 0;
    for (const BinderItem *p = item; p && p != rootItem; p = p->parent)
        ++level;
    const QStringList *list = &structureDefaults.files;
    if (item->isFolder())
        list = &structureDefaults.folders;
    else if (!item->children.isEmpty())
        list = &structureDefaults.fileGroups;
    if (list->isEmpty())
        return sectionTypes.isEmpty() ? QString() : sectionTypes.last().id;
    return list->value(qMin(level, int(list->size()) - 1));
}

bool Project::isInDraft(const BinderItem *item) const
{
    return item && m_draft && (item == m_draft || item->isDescendantOf(m_draft));
}

bool Project::isInTrash(const BinderItem *item) const
{
    return item && m_trash && (item == m_trash || item->isDescendantOf(m_trash));
}

bool Project::isTemplate(const BinderItem *item) const
{
    if (!item || templatesFolderId.isEmpty())
        return false;
    const BinderItem *tf = this->item(templatesFolderId);
    return tf && item->isDescendantOf(tf);
}

int Project::draftWordCount() const
{
    int n = 0;
    if (m_draft)
        m_draft->forEach([&](BinderItem *i) { n += i->wordCount; });
    return n;
}

int Project::draftCharCount() const
{
    int n = 0;
    if (m_draft)
        m_draft->forEach([&](BinderItem *i) { n += i->charCount; });
    return n;
}

int Project::totalWordCount() const
{
    int n = 0;
    for (BinderItem *i : allItems())
        if (!isInTrash(i))
            n += i->wordCount;
    return n;
}

int Project::totalCharCount() const
{
    int n = 0;
    for (BinderItem *i : allItems())
        if (!isInTrash(i))
            n += i->charCount;
    return n;
}

void Project::startSession(bool force)
{
    if (!force)
        return;
    targets.sessionStart = QDateTime::currentDateTime();
    targets.sessionStartDraft = draftWordCount();
    targets.sessionStartTotal = totalWordCount();
    targets.sessionStartDraftChars = draftCharCount();
    targets.sessionStartTotalChars = totalCharCount();
    markIndexDirty();
}

int Project::sessionWords() const
{
    const int now = targets.countAnywhere ? totalWordCount() : draftWordCount();
    const int start = targets.countAnywhere ? targets.sessionStartTotal : targets.sessionStartDraft;
    return qMax(0, now - start);
}

int Project::sessionChars() const
{
    const int now = targets.countAnywhere ? totalCharCount() : draftCharCount();
    const int start = targets.countAnywhere ? targets.sessionStartTotalChars : targets.sessionStartDraftChars;
    return qMax(0, now - start);
}

int Project::sessionTargetToday() const
{
    if (!targets.autoFromDeadline || !targets.deadline.isValid() || targets.draftTarget <= 0)
        return targets.sessionTarget;
    const int written = targets.draftChars ? targets.sessionStartDraftChars : targets.sessionStartDraft;
    const int remaining = qMax(0, targets.draftTarget - written);
    int days = 0;
    for (QDate d = QDate::currentDate(); d <= targets.deadline; d = d.addDays(1)) {
        const int dow = d.dayOfWeek() - 1; // 0 = Monday
        if (targets.writingDays.size() > dow && targets.writingDays.at(dow) == QLatin1Char('1'))
            ++days;
    }
    if (days <= 0)
        return remaining;
    return int(std::ceil(double(remaining) / days));
}

void Project::recordHistory()
{
    const QDate today = QDate::currentDate();
    const int draftW = draftWordCount();
    const int totalW = totalWordCount();
    const int draftC = draftCharCount();
    const int totalC = totalCharCount();
    if (!uiState.contains(QStringLiteral("dayStartDate"))
        || uiState.value(QStringLiteral("dayStartDate")).toDate() != today) {
        uiState[QStringLiteral("dayStartDate")] = today;
        uiState[QStringLiteral("dayStartDraft")] = draftW;
        uiState[QStringLiteral("dayStartTotal")] = totalW;
        uiState[QStringLiteral("dayStartDraftChars")] = draftC;
        uiState[QStringLiteral("dayStartTotalChars")] = totalC;
    }
    WritingDay day;
    day.date = today;
    day.draftWords = draftW - uiState.value(QStringLiteral("dayStartDraft")).toInt();
    day.otherWords = (totalW - draftW) - (uiState.value(QStringLiteral("dayStartTotal")).toInt()
                                          - uiState.value(QStringLiteral("dayStartDraft")).toInt());
    day.draftChars = draftC - uiState.value(QStringLiteral("dayStartDraftChars")).toInt();
    day.otherChars = (totalC - draftC) - (uiState.value(QStringLiteral("dayStartTotalChars")).toInt()
                                          - uiState.value(QStringLiteral("dayStartDraftChars")).toInt());
    if (!history.isEmpty() && history.last().date == today)
        history.last() = day;
    else
        history.append(day);
}

void Project::notifyItemChanged(BinderItem *item, bool touchModified)
{
    if (touchModified)
        item->modified = QDateTime::currentDateTime();
    markIndexDirty();
    emit itemChanged(item);
}

bool Project::setStyleSheet(const StyleSheet &sheet, const QHash<QString, QString> &renamed,
                            const std::function<bool(int, int)> &progress, QString *error)
{
    StyleSheet s = sheet;
    // former names keep working (snapshots, backups, text pasted from them)
    for (auto it = s.aliases.begin(); it != s.aliases.end(); ++it)
        if (renamed.contains(it.value()))
            it.value() = renamed.value(it.value());
    for (auto it = renamed.constBegin(); it != renamed.constEnd(); ++it)
        s.aliases.insert(it.key(), it.value());
    for (auto it = s.aliases.begin(); it != s.aliases.end();) {
        if (s.indexOf(it.key()) >= 0 || s.indexOf(it.value()) < 0)
            it = s.aliases.erase(it);
        else
            ++it;
    }
    const Formatting oldF = formatting;
    const Formatting newF = styles::withSheet(oldF, s);
    styles::Changes changes;
    changes.renamed = renamed;
    const bool ok = m_docs->restyleAll(oldF, newF, changes, progress, error);
    formatting = newF;
    markIndexDirty();
    scheduleSave();
    emit stylesChanged();
    return ok;
}

// "0.10.0" against "0.9.1": numbers part by part (an empty text is the smallest)
int Project::compareVersionText(const QString &a, const QString &b)
{
    const QStringList pa = a.split(QLatin1Char('.'), Qt::SkipEmptyParts), pb = b.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    for (int i = 0; i < qMax(pa.size(), pb.size()); ++i) {
        const int x = i < pa.size() ? pa.at(i).toInt() : 0, y = i < pb.size() ? pb.at(i).toInt() : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

QString Project::neededVersion(const QString &projectFile)
{
    QFile f(projectFile);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    QXmlStreamReader xml(&f);
    while (!xml.atEnd())
        if (xml.readNext() == QXmlStreamReader::StartElement)
            return xml.attributes().value(QLatin1String("needs")).toString(); // the root element
    return QString();
}

bool Project::setScriptFormat(const script::Format &format, const QHash<QString, QString> &renamedElements,
                              const std::function<bool(int, int)> &progress, QString *error)
{
    const Formatting oldF = formatting;
    const Formatting newF = script::withFormat(oldF, format);
    styles::Changes changes;
    changes.renamed = script::renamedStyles(script::format(oldF), format, renamedElements);
    const bool ok = m_docs->restyleAll(oldF, newF, changes, progress, error);
    formatting = newF;
    markIndexDirty();
    scheduleSave();
    emit scriptFormatChanged();
    return ok;
}

void Project::setScriptMode(BinderItem *item, bool on)
{
    if (!item || !item->hasText() || item->scriptMode == on)
        return;
    item->scriptMode = on;
    if (on) {
        // an empty script starts with the first element of the format
        const script::Element *first = script::format(formatting).first();
        QTextDocument *doc = m_docs->text(item->id);
        if (first && doc && doc->isEmpty()) {
            const QString style = script::styleName(first->name);
            const bool undo = doc->isUndoRedoEnabled();
            doc->setUndoRedoEnabled(false);
            QTextCursor c(doc);
            const QTextCharFormat cf = display::charFormatFor(formatting, style);
            c.setBlockFormat(display::blockFormatFor(formatting, style));
            c.setBlockCharFormat(cf);
            c.setCharFormat(cf);
            doc->setUndoRedoEnabled(undo);
        }
    }
    notifyItemChanged(item, false);
}

void Project::notifyBookmarksChanged()
{
    markIndexDirty();
    emit bookmarksChanged();
}

void Project::notifySettingsChanged()
{
    markIndexDirty();
    emit settingsChanged();
}

void Project::markIndexDirty()
{
    m_indexDirty = true;
    scheduleSave();
}

void Project::scheduleSave()
{
    if (!m_dirtySince.isValid())
        m_dirtySince.start();
    if (m_dirtySince.elapsed() >= qMax<qint64>(m_maxSaveWait, m_saveTimer.interval())) {
        // the changes go on without a pause: save now (from the event loop) instead of waiting
        if (!m_forcedSave) {
            m_forcedSave = true;
            QTimer::singleShot(0, this, [this] {
                m_forcedSave = false;
                if (!hasUnsavedChanges())
                    return;
                QString err;
                if (!save(&err))
                    emit saveFailed(err);
            });
        }
        return;
    }
    m_saveTimer.start();
}

QString Project::spareCopyOf(const QString &projectFile)
{
    return QFileInfo(projectFile).absolutePath() + QLatin1String("/settings/spare-copy") + QLatin1String(QB_PROJECT_FILE_SUFFIX);
}

bool Project::restoreSpareCopy(const QString &projectFile, QString *keptAs, QString *error)
{
    const QString spare = spareCopyOf(projectFile);
    if (!QFile::exists(spare)) {
        if (error)
            *error = QObject::tr("There is no spare copy of the project file.");
        return false;
    }
    const QString damaged = projectFile + QStringLiteral(".damaged-%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    if (QFile::exists(projectFile) && !QFile::rename(projectFile, damaged)) {
        if (error)
            *error = QObject::tr("Cannot rename %1").arg(projectFile);
        return false;
    }
    if (!QFile::copy(spare, projectFile)) {
        QFile::rename(damaged, projectFile);
        if (error)
            *error = QObject::tr("Cannot write %1").arg(projectFile);
        return false;
    }
    if (keptAs)
        *keptAs = damaged;
    return true;
}

int Project::recoverUnlistedDocuments(QString *folderTitle)
{
    QDir content(m_folder + QLatin1String("/content"));
    BinderItem *folder = nullptr;
    int n = 0;
    for (const QString &id : content.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (item(id))
            continue;
        const QStringList texts = QDir(content.filePath(id)).entryList({QStringLiteral("text.*")}, QDir::Files);
        if (texts.isEmpty())
            continue; // pictures or media alone: left where they are
        if (!folder) {
            folder = m_model->createItem(m_research, -1, ItemType::Folder, QObject::tr("Recovered Documents"));
            if (folderTitle)
                *folderTitle = folder->title;
        }
        auto *it = new BinderItem;
        it->id = id;
        it->type = ItemType::Text;
        it->title = QObject::tr("Recovered document");
        it->created = it->modified = QFileInfo(content.filePath(id + QLatin1Char('/') + texts.first())).lastModified();
        m_model->insertExisting(folder, -1, it);
        // the title: the first line of its text
        const QString first = docs()->text(id)->toPlainText().section(QLatin1Char('\n'), 0, 0).simplified();
        if (!first.isEmpty())
            it->title = first.length() > 60 ? first.left(60).trimmed() + QLatin1String("...") : first;
        ++n;
    }
    if (n > 0)
        markIndexDirty();
    return n;
}

bool Project::hasUnsavedChanges() const
{
    return m_indexDirty || (m_docs && m_docs->hasDirty()) || (m_objects && m_objects->isDirty());
}

objects::Store *Project::objects()
{
    if (!m_objects) {
        m_objects = new objects::Store(m_folder, this);
        m_objects->load();
        connect(m_objects, &objects::Store::dirtied, this, &Project::scheduleSave);
    }
    return m_objects;
}

bool Project::save(QString *error)
{
    m_saveTimer.stop();
    m_dirtySince.invalidate();
    bool ok = true;
    if (m_docs && !m_docs->saveDirty(error))
        ok = false;
    if (m_objects && m_objects->isDirty() && !m_objects->save(error))
        ok = false;
    if (m_indexDirty || !QFileInfo::exists(m_indexFile)) {
        if (!saveIndex(error))
            ok = false;
    }
    // a project in Scrivener format: the .scriv gets what changed
    if (ok && !scrivenerPath.isEmpty() && !scrivener::sync(this, error))
        ok = false;
    if (ok)
        emit saved();
    return ok;
}

QString Project::displayFile() const
{
    return scrivenerPath.isEmpty() ? m_indexFile : scrivener::scrivxIn(scrivenerPath);
}

QString Project::displayFolder() const { return scrivenerPath.isEmpty() ? m_folder : scrivenerPath; }

// ---------------------------------------------------------------- XML

static QString colorStr(const QColor &c) { return c.isValid() ? c.name() : QString(); }

static void writeBookmarks(QDomDocument &dom, QDomElement &parent, const QList<Bookmark> &list, const QString &tag)
{
    if (list.isEmpty())
        return;
    QDomElement e = dom.createElement(tag);
    for (const Bookmark &b : list) {
        QDomElement be = dom.createElement(QStringLiteral("B"));
        be.setAttribute(QStringLiteral("title"), b.title);
        be.setAttribute(QStringLiteral("target"), b.target);
        e.appendChild(be);
    }
    parent.appendChild(e);
}

static QList<Bookmark> readBookmarks(const QDomElement &e)
{
    QList<Bookmark> list;
    for (QDomElement b = e.firstChildElement(QStringLiteral("B")); !b.isNull(); b = b.nextSiblingElement(QStringLiteral("B")))
        list.append({b.attribute(QStringLiteral("title")), b.attribute(QStringLiteral("target"))});
    return list;
}

static void writeQuery(QDomElement &e, const SearchQuery &q)
{
    e.setAttribute(QStringLiteral("text"), q.text);
    QString flags;
    auto f = [&](bool v, QChar c) { if (v) flags += c; };
    f(q.inTitle, QLatin1Char('t'));
    f(q.inText, QLatin1Char('x'));
    f(q.inSynopsis, QLatin1Char('s'));
    f(q.inNotes, QLatin1Char('n'));
    f(q.inKeywords, QLatin1Char('k'));
    f(q.inLabel, QLatin1Char('l'));
    f(q.inStatus, QLatin1Char('u'));
    f(q.inCustom, QLatin1Char('c'));
    f(q.wholeWord, QLatin1Char('w'));
    f(q.caseSensitive, QLatin1Char('C'));
    f(q.exactPhrase, QLatin1Char('p'));
    f(q.includeTrash, QLatin1Char('T'));
    f(q.anyWord, QLatin1Char('a'));
    f(q.regex, QLatin1Char('r'));
    f(q.draftOnly, QLatin1Char('D'));
    f(q.compile == 1, QLatin1Char('I'));
    f(q.compile == 2, QLatin1Char('E'));
    f(q.invert, QLatin1Char('V'));
    e.setAttribute(QStringLiteral("flags"), flags);
    if (!q.scopeIds.isEmpty())
        e.setAttribute(QStringLiteral("scope"), q.scopeIds.join(QLatin1Char(' ')));
}

static SearchQuery readQuery(const QDomElement &e)
{
    SearchQuery q;
    q.text = e.attribute(QStringLiteral("text"));
    const QString flags = e.attribute(QStringLiteral("flags"), QStringLiteral("txsnkc"));
    q.inTitle = flags.contains(QLatin1Char('t'));
    q.inText = flags.contains(QLatin1Char('x'));
    q.inSynopsis = flags.contains(QLatin1Char('s'));
    q.inNotes = flags.contains(QLatin1Char('n'));
    q.inKeywords = flags.contains(QLatin1Char('k'));
    q.inLabel = flags.contains(QLatin1Char('l'));
    q.inStatus = flags.contains(QLatin1Char('u'));
    q.inCustom = flags.contains(QLatin1Char('c'));
    q.wholeWord = flags.contains(QLatin1Char('w'));
    q.caseSensitive = flags.contains(QLatin1Char('C'));
    q.exactPhrase = flags.contains(QLatin1Char('p'));
    q.allWords = !q.exactPhrase;
    q.includeTrash = flags.contains(QLatin1Char('T'));
    q.anyWord = flags.contains(QLatin1Char('a')) && !q.exactPhrase;
    q.regex = flags.contains(QLatin1Char('r'));
    q.draftOnly = flags.contains(QLatin1Char('D'));
    q.compile = flags.contains(QLatin1Char('I')) ? 1 : flags.contains(QLatin1Char('E')) ? 2 : 0;
    q.invert = flags.contains(QLatin1Char('V'));
    q.scopeIds = e.attribute(QStringLiteral("scope")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return q;
}

static void writeItem(QDomDocument &dom, QDomElement &parent, const BinderItem *it)
{
    QDomElement e = dom.createElement(QStringLiteral("Item"));
    e.setAttribute(QStringLiteral("id"), it->id);
    e.setAttribute(QStringLiteral("type"), itemTypeToString(it->type));
    e.setAttribute(QStringLiteral("created"), it->created.toString(Qt::ISODate));
    e.setAttribute(QStringLiteral("modified"), it->modified.toString(Qt::ISODate));
    if (it->label >= 0)
        e.setAttribute(QStringLiteral("label"), it->label);
    if (it->status >= 0)
        e.setAttribute(QStringLiteral("status"), it->status);
    if (!it->includeInCompile)
        e.setAttribute(QStringLiteral("compile"), 0);
    if (!it->sectionType.isEmpty())
        e.setAttribute(QStringLiteral("section"), it->sectionType);
    if (!it->icon.isEmpty())
        e.setAttribute(QStringLiteral("icon"), it->icon);
    if (!it->fileExt.isEmpty())
        e.setAttribute(QStringLiteral("ext"), it->fileExt);
    if (!it->originalFileName.isEmpty())
        e.setAttribute(QStringLiteral("original"), it->originalFileName);
    if (!it->synopsisImage.isEmpty())
        e.setAttribute(QStringLiteral("synopsisImage"), it->synopsisImage);
    if (it->target > 0) {
        e.setAttribute(QStringLiteral("target"), it->target);
        if (it->targetChars)
            e.setAttribute(QStringLiteral("targetChars"), 1);
    }
    if (!it->defaultTemplate.isEmpty())
        e.setAttribute(QStringLiteral("template"), it->defaultTemplate);
    if (it->scriptMode)
        e.setAttribute(QStringLiteral("script"), 1);
    if (!it->corkboardPos.isEmpty())
        e.setAttribute(QStringLiteral("cork"), it->corkboardPos);
    e.setAttribute(QStringLiteral("words"), it->wordCount);
    e.setAttribute(QStringLiteral("chars"), it->charCount);

    QDomElement title = dom.createElement(QStringLiteral("Title"));
    title.appendChild(dom.createTextNode(it->title));
    e.appendChild(title);
    if (!it->synopsis.isEmpty()) {
        QDomElement syn = dom.createElement(QStringLiteral("Synopsis"));
        syn.appendChild(dom.createTextNode(it->synopsis));
        e.appendChild(syn);
    }
    if (!it->keywords.isEmpty()) {
        QDomElement kw = dom.createElement(QStringLiteral("Keywords"));
        for (const QString &k : it->keywords) {
            QDomElement ke = dom.createElement(QStringLiteral("K"));
            ke.appendChild(dom.createTextNode(k));
            kw.appendChild(ke);
        }
        e.appendChild(kw);
    }
    if (!it->custom.isEmpty()) {
        QDomElement cm = dom.createElement(QStringLiteral("Custom"));
        for (auto i = it->custom.cbegin(); i != it->custom.cend(); ++i) {
            QDomElement fe = dom.createElement(QStringLiteral("F"));
            fe.setAttribute(QStringLiteral("id"), i.key());
            fe.appendChild(dom.createTextNode(i.value()));
            cm.appendChild(fe);
        }
        e.appendChild(cm);
    }
    writeBookmarks(dom, e, it->bookmarks, QStringLiteral("Bookmarks"));
    if (!it->children.isEmpty()) {
        QDomElement ch = dom.createElement(QStringLiteral("Children"));
        for (const BinderItem *c : it->children)
            writeItem(dom, ch, c);
        e.appendChild(ch);
    }
    parent.appendChild(e);
}

static BinderItem *readItem(const QDomElement &e, BinderItem *parent)
{
    auto *it = new BinderItem;
    it->parent = parent;
    it->id = e.attribute(QStringLiteral("id"));
    if (it->id.isEmpty())
        it->id = newId();
    it->type = itemTypeFromString(e.attribute(QStringLiteral("type")));
    it->created = QDateTime::fromString(e.attribute(QStringLiteral("created")), Qt::ISODate);
    it->modified = QDateTime::fromString(e.attribute(QStringLiteral("modified")), Qt::ISODate);
    if (!it->created.isValid())
        it->created = QDateTime::currentDateTime();
    if (!it->modified.isValid())
        it->modified = it->created;
    it->label = e.attribute(QStringLiteral("label"), QStringLiteral("-1")).toInt();
    it->status = e.attribute(QStringLiteral("status"), QStringLiteral("-1")).toInt();
    it->includeInCompile = e.attribute(QStringLiteral("compile"), QStringLiteral("1")) != QLatin1String("0");
    it->sectionType = e.attribute(QStringLiteral("section"));
    it->icon = e.attribute(QStringLiteral("icon"));
    it->fileExt = e.attribute(QStringLiteral("ext"));
    it->originalFileName = e.attribute(QStringLiteral("original"));
    it->synopsisImage = e.attribute(QStringLiteral("synopsisImage"));
    it->target = e.attribute(QStringLiteral("target"), QStringLiteral("0")).toInt();
    it->targetChars = e.attribute(QStringLiteral("targetChars")) == QLatin1String("1");
    it->defaultTemplate = e.attribute(QStringLiteral("template"));
    it->scriptMode = e.attribute(QStringLiteral("script")) == QLatin1String("1");
    it->corkboardPos = e.attribute(QStringLiteral("cork"));
    it->wordCount = e.attribute(QStringLiteral("words"), QStringLiteral("0")).toInt();
    it->charCount = e.attribute(QStringLiteral("chars"), QStringLiteral("0")).toInt();
    it->title = e.firstChildElement(QStringLiteral("Title")).text();
    it->synopsis = e.firstChildElement(QStringLiteral("Synopsis")).text();
    const QDomElement kw = e.firstChildElement(QStringLiteral("Keywords"));
    for (QDomElement k = kw.firstChildElement(QStringLiteral("K")); !k.isNull(); k = k.nextSiblingElement(QStringLiteral("K")))
        it->keywords << k.text();
    const QDomElement cm = e.firstChildElement(QStringLiteral("Custom"));
    for (QDomElement f = cm.firstChildElement(QStringLiteral("F")); !f.isNull(); f = f.nextSiblingElement(QStringLiteral("F")))
        it->custom.insert(f.attribute(QStringLiteral("id")), f.text());
    it->bookmarks = readBookmarks(e.firstChildElement(QStringLiteral("Bookmarks")));
    const QDomElement ch = e.firstChildElement(QStringLiteral("Children"));
    for (QDomElement c = ch.firstChildElement(QStringLiteral("Item")); !c.isNull(); c = c.nextSiblingElement(QStringLiteral("Item")))
        it->children.append(readItem(c, it));
    return it;
}

bool Project::saveIndex(QString *error)
{
    QDomDocument dom;
    dom.appendChild(dom.createProcessingInstruction(QStringLiteral("xml"),
                                                    QStringLiteral("version=\"1.0\" encoding=\"UTF-8\"")));
    QDomElement rootEl = dom.createElement(QStringLiteral("QuillbinderProject"));
    rootEl.setAttribute(QStringLiteral("version"), 1);
    rootEl.setAttribute(QStringLiteral("app"), QStringLiteral(QB_APP_NAME " " QB_APP_VERSION));
    rootEl.setAttribute(QStringLiteral("title"), m_title);
    {
        // scripts need version 0.10: older versions take the elements away when they save a script
        bool scripts = formatting.script != nullptr;
        for (auto it = m_items.constBegin(); it != m_items.constEnd() && !scripts; ++it)
            scripts = it.value() && it.value()->scriptMode;
        QString needs = scripts ? QStringLiteral("0.10.0") : QString();
        if (!needsVersion.isEmpty() && compareVersionText(needsVersion, needs) > 0)
            needs = needsVersion; // set by a newer version for something this one does not know
        if (!needs.isEmpty())
            rootEl.setAttribute(QStringLiteral("needs"), needs);
    }
    rootEl.setAttribute(QStringLiteral("storage"),
                        storage == StorageFormat::Rtf ? QStringLiteral("rtf") : QStringLiteral("markdown"));
    if (!scrivenerPath.isEmpty())
        rootEl.setAttribute(QStringLiteral("scrivener"), scrivenerPath);
    dom.appendChild(rootEl);

    QDomElement binder = dom.createElement(QStringLiteral("Binder"));
    for (const BinderItem *c : m_root->children)
        writeItem(dom, binder, c);
    rootEl.appendChild(binder);

    QDomElement lab = dom.createElement(QStringLiteral("Labels"));
    lab.setAttribute(QStringLiteral("title"), labelTitle);
    for (const Label &l : labels) {
        QDomElement le = dom.createElement(QStringLiteral("Label"));
        le.setAttribute(QStringLiteral("id"), l.id);
        le.setAttribute(QStringLiteral("color"), colorStr(l.color));
        le.appendChild(dom.createTextNode(l.name));
        lab.appendChild(le);
    }
    rootEl.appendChild(lab);

    QDomElement st = dom.createElement(QStringLiteral("Statuses"));
    st.setAttribute(QStringLiteral("title"), statusTitle);
    for (const Status &s : statuses) {
        QDomElement se = dom.createElement(QStringLiteral("Status"));
        se.setAttribute(QStringLiteral("id"), s.id);
        se.appendChild(dom.createTextNode(s.name));
        st.appendChild(se);
    }
    rootEl.appendChild(st);

    QDomElement kws = dom.createElement(QStringLiteral("Keywords"));
    for (const Keyword &k : keywords) {
        QDomElement ke = dom.createElement(QStringLiteral("Keyword"));
        ke.setAttribute(QStringLiteral("id"), k.id);
        ke.setAttribute(QStringLiteral("color"), colorStr(k.color));
        if (!k.parentId.isEmpty())
            ke.setAttribute(QStringLiteral("parent"), k.parentId);
        ke.appendChild(dom.createTextNode(k.name));
        kws.appendChild(ke);
    }
    rootEl.appendChild(kws);

    QDomElement cfs = dom.createElement(QStringLiteral("CustomFields"));
    for (const CustomField &f : customFields) {
        QDomElement fe = dom.createElement(QStringLiteral("Field"));
        fe.setAttribute(QStringLiteral("id"), f.id);
        fe.setAttribute(QStringLiteral("title"), f.title);
        const char *t = "text";
        switch (f.type) {
        case CustomFieldType::Checkbox: t = "checkbox"; break;
        case CustomFieldType::List: t = "list"; break;
        case CustomFieldType::Date: t = "date"; break;
        default: break;
        }
        fe.setAttribute(QStringLiteral("type"), QString::fromLatin1(t));
        fe.setAttribute(QStringLiteral("default"), f.defaultValue);
        for (const QString &o : f.options) {
            QDomElement oe = dom.createElement(QStringLiteral("Option"));
            oe.appendChild(dom.createTextNode(o));
            fe.appendChild(oe);
        }
        cfs.appendChild(fe);
    }
    rootEl.appendChild(cfs);

    QDomElement sts = dom.createElement(QStringLiteral("SectionTypes"));
    for (const SectionType &s : sectionTypes) {
        QDomElement se = dom.createElement(QStringLiteral("Type"));
        se.setAttribute(QStringLiteral("id"), s.id);
        se.appendChild(dom.createTextNode(s.name));
        sts.appendChild(se);
    }
    auto writeList = [&](const QString &tag, const QStringList &l) {
        QDomElement e = dom.createElement(tag);
        for (const QString &id : l) {
            QDomElement le = dom.createElement(QStringLiteral("Level"));
            le.appendChild(dom.createTextNode(id));
            e.appendChild(le);
        }
        sts.appendChild(e);
    };
    writeList(QStringLiteral("Folders"), structureDefaults.folders);
    writeList(QStringLiteral("FileGroups"), structureDefaults.fileGroups);
    writeList(QStringLiteral("Files"), structureDefaults.files);
    rootEl.appendChild(sts);

    QDomElement fm = dom.createElement(QStringLiteral("Formatting"));
    fm.setAttribute(QStringLiteral("font"), formatting.fontFamily);
    fm.setAttribute(QStringLiteral("size"), formatting.fontSize);
    fm.setAttribute(QStringLiteral("lineSpacing"), formatting.lineSpacing);
    fm.setAttribute(QStringLiteral("indent"), formatting.firstLineIndent);
    fm.setAttribute(QStringLiteral("spacing"), formatting.paragraphSpacing);
    fm.setAttribute(QStringLiteral("justify"), formatting.justify ? 1 : 0);
    rootEl.appendChild(fm);
    // styles are stored once the project has some of its own
    if (formatting.styles)
        rootEl.appendChild(styles::toXml(dom, *formatting.styles));
    // the script format once the project has one of its own (older versions ignore it)
    if (formatting.script) {
        QDomElement sf = dom.createElement(QStringLiteral("ScriptFormat"));
        sf.appendChild(dom.createTextNode(
            QString::fromUtf8(QJsonDocument(script::toJson(formatting.script->format)).toJson(QJsonDocument::Compact))));
        rootEl.appendChild(sf);
    }

    QDomElement tg = dom.createElement(QStringLiteral("Targets"));
    tg.setAttribute(QStringLiteral("draft"), targets.draftTarget);
    tg.setAttribute(QStringLiteral("draftChars"), targets.draftChars ? 1 : 0);
    tg.setAttribute(QStringLiteral("deadline"), targets.deadline.toString(Qt::ISODate));
    tg.setAttribute(QStringLiteral("session"), targets.sessionTarget);
    tg.setAttribute(QStringLiteral("sessionChars"), targets.sessionChars ? 1 : 0);
    tg.setAttribute(QStringLiteral("sessionReset"), targets.sessionReset);
    tg.setAttribute(QStringLiteral("countAnywhere"), targets.countAnywhere ? 1 : 0);
    tg.setAttribute(QStringLiteral("autoFromDeadline"), targets.autoFromDeadline ? 1 : 0);
    tg.setAttribute(QStringLiteral("writingDays"), targets.writingDays);
    tg.setAttribute(QStringLiteral("sessionStart"), targets.sessionStart.toString(Qt::ISODate));
    tg.setAttribute(QStringLiteral("sessionStartDraft"), targets.sessionStartDraft);
    tg.setAttribute(QStringLiteral("sessionStartTotal"), targets.sessionStartTotal);
    tg.setAttribute(QStringLiteral("sessionStartDraftChars"), targets.sessionStartDraftChars);
    tg.setAttribute(QStringLiteral("sessionStartTotalChars"), targets.sessionStartTotalChars);
    rootEl.appendChild(tg);

    writeBookmarks(dom, rootEl, projectBookmarks, QStringLiteral("ProjectBookmarks"));

    QDomElement cols = dom.createElement(QStringLiteral("Collections"));
    for (const Collection &c : collections) {
        QDomElement ce = dom.createElement(QStringLiteral("Collection"));
        ce.setAttribute(QStringLiteral("id"), c.id);
        ce.setAttribute(QStringLiteral("name"), c.name);
        ce.setAttribute(QStringLiteral("color"), colorStr(c.color));
        ce.setAttribute(QStringLiteral("search"), c.isSearch ? 1 : 0);
        if (c.isSearch) {
            QDomElement q = dom.createElement(QStringLiteral("Query"));
            writeQuery(q, c.query);
            ce.appendChild(q);
        }
        for (const QString &id : c.items) {
            QDomElement de = dom.createElement(QStringLiteral("Doc"));
            de.appendChild(dom.createTextNode(id));
            ce.appendChild(de);
        }
        cols.appendChild(ce);
    }
    rootEl.appendChild(cols);

    QDomElement sp = dom.createElement(QStringLiteral("SpecialFolders"));
    sp.setAttribute(QStringLiteral("templates"), templatesFolderId);
    sp.setAttribute(QStringLiteral("notes"), notesFolderId);
    sp.setAttribute(QStringLiteral("compositionBackground"), compositionBackground);
    rootEl.appendChild(sp);

    QDomElement cp = dom.createElement(QStringLiteral("Compile"));
    cp.setAttribute(QStringLiteral("format"), compile.formatName);
    cp.setAttribute(QStringLiteral("exporter"), compile.exporterId);
    cp.setAttribute(QStringLiteral("group"), compile.compileGroupId);
    cp.setAttribute(QStringLiteral("complete"), compile.treatAsComplete ? 1 : 0);
    cp.setAttribute(QStringLiteral("groupText"), compile.includeGroupText ? 1 : 0);
    cp.setAttribute(QStringLiteral("front"), compile.frontMatterId);
    cp.setAttribute(QStringLiteral("back"), compile.backMatterId);
    cp.setAttribute(QStringLiteral("outputDir"), compile.lastOutputDir);
    cp.setAttribute(QStringLiteral("titleOverride"), compile.titleOverride);
    cp.setAttribute(QStringLiteral("authorOverride"), compile.authorOverride);
    {
        const QList<QPair<const char *, const QString *>> meta = {
            {"subtitle", &compile.subtitle},       {"subject", &compile.subject}, {"keywords", &compile.keywords},
            {"publisher", &compile.publisher},     {"description", &compile.description},
            {"language", &compile.language},       {"date", &compile.date},       {"cover", &compile.coverId},
            {"identifier", &compile.identifier}};
        for (const auto &m : meta)
            if (!m.second->isEmpty())
                cp.setAttribute(QLatin1String(m.first), *m.second);
    }
    for (auto i = compile.layoutAssignments.cbegin(); i != compile.layoutAssignments.cend(); ++i) {
        QDomElement a = dom.createElement(QStringLiteral("Assign"));
        a.setAttribute(QStringLiteral("key"), i.key());
        a.setAttribute(QStringLiteral("layout"), i.value());
        cp.appendChild(a);
    }
    for (auto i = compile.includeOverrides.cbegin(); i != compile.includeOverrides.cend(); ++i) {
        QDomElement a = dom.createElement(QStringLiteral("Include"));
        a.setAttribute(QStringLiteral("id"), i.key());
        a.setAttribute(QStringLiteral("value"), i.value() ? 1 : 0);
        cp.appendChild(a);
    }
    // 0.21: written only when used, so a project looks as before to older versions (which skip them)
    if (!compile.fontOverride.isEmpty())
        cp.setAttribute(QStringLiteral("fontOverride"), compile.fontOverride);
    if (compile.objectSheets)
        cp.setAttribute(QStringLiteral("objectSheets"), 1);
    if (!compile.objectCategories.isEmpty())
        cp.setAttribute(QStringLiteral("objectCategories"), compile.objectCategories.join(QLatin1Char(',')));
    for (const CompileReplacement &r : compile.replacements) {
        QDomElement a = dom.createElement(QStringLiteral("Replace"));
        a.setAttribute(QStringLiteral("find"), r.find);
        a.setAttribute(QStringLiteral("with"), r.replace);
        a.setAttribute(QStringLiteral("regex"), r.regex ? 1 : 0);
        a.setAttribute(QStringLiteral("case"), r.matchCase ? 1 : 0);
        a.setAttribute(QStringLiteral("whole"), r.wholeWords ? 1 : 0);
        a.setAttribute(QStringLiteral("on"), r.enabled ? 1 : 0);
        cp.appendChild(a);
    }
    rootEl.appendChild(cp);

    QDomElement hist = dom.createElement(QStringLiteral("History"));
    for (const WritingDay &d : history) {
        QDomElement de = dom.createElement(QStringLiteral("Day"));
        de.setAttribute(QStringLiteral("date"), d.date.toString(Qt::ISODate));
        de.setAttribute(QStringLiteral("draft"), d.draftWords);
        de.setAttribute(QStringLiteral("other"), d.otherWords);
        de.setAttribute(QStringLiteral("draftChars"), d.draftChars);
        de.setAttribute(QStringLiteral("otherChars"), d.otherChars);
        hist.appendChild(de);
    }
    rootEl.appendChild(hist);

    QSaveFile f(m_indexFile);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = tr("Cannot write %1: %2").arg(m_indexFile, f.errorString());
        return false;
    }
    // characters XML does not allow (pasted control characters) would make the file unreadable
    f.write(text::xmlSafeBytes(dom.toByteArray(1)));
    if (!f.commit()) {
        if (error)
            *error = tr("Cannot write %1: %2").arg(m_indexFile, f.errorString());
        return false;
    }
    // ui state
    QSaveFile ui(QDir(settingsDir()).filePath(QStringLiteral("ui.json")));
    if (ui.open(QIODevice::WriteOnly)) {
        ui.write(QJsonDocument::fromVariant(uiState).toJson());
        ui.commit();
    }
    m_indexDirty = false;
    return true;
}

bool Project::loadIndex(QString *error)
{
    QFile f(m_indexFile);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Cannot open %1: %2").arg(m_indexFile, f.errorString());
        return false;
    }
    QDomDocument dom;
    QString msg;
    int line = 0, col = 0;
    m_loadedIndex = f.readAll();
    if (!dom.setContent(m_loadedIndex, &msg, &line, &col)) {
        // characters XML does not allow (written by versions before 0.20): removed, then read again
        int removed = 0;
        const QByteArray clean = text::xmlSafeBytes(m_loadedIndex, &removed);
        QDomDocument again;
        if (removed > 0 && again.setContent(clean)) {
            dom = again;
            m_loadedIndex = clean;
            m_indexDirty = true; // saved without them
            openNotice = tr("The project file contained %n character(s) that are not allowed in it (control characters, "
                            "for example from pasted text), so it could not be read before. They were removed, and the "
                            "project file is saved without them.",
                            nullptr, removed);
        } else {
            m_loadedIndex.clear();
            if (error)
                *error = tr("The project file is damaged (%1, line %2).").arg(msg).arg(line);
            return false;
        }
    }
    const QDomElement rootEl = dom.documentElement();
    m_title = rootEl.attribute(QStringLiteral("title"), QFileInfo(m_indexFile).completeBaseName());
    needsVersion = rootEl.attribute(QStringLiteral("needs"));
    storage = rootEl.attribute(QStringLiteral("storage")) == QLatin1String("rtf") ? StorageFormat::Rtf
                                                                                  : StorageFormat::Markdown;
    scrivenerPath = rootEl.attribute(QStringLiteral("scrivener"));
    m_root = new BinderItem;
    m_root->id = QStringLiteral("ROOT");
    m_root->type = ItemType::Folder;
    const QDomElement binder = rootEl.firstChildElement(QStringLiteral("Binder"));
    for (QDomElement c = binder.firstChildElement(QStringLiteral("Item")); !c.isNull();
         c = c.nextSiblingElement(QStringLiteral("Item")))
        m_root->children.append(readItem(c, m_root));
    registerItem(m_root);
    for (BinderItem *c : m_root->children) {
        if (c->type == ItemType::Draft && !m_draft)
            m_draft = c;
        else if (c->type == ItemType::Research && !m_research)
            m_research = c;
        else if (c->type == ItemType::Trash && !m_trash)
            m_trash = c;
    }

    const QDomElement lab = rootEl.firstChildElement(QStringLiteral("Labels"));
    labelTitle = lab.attribute(QStringLiteral("title"), tr("Label"));
    labels.clear();
    for (QDomElement l = lab.firstChildElement(QStringLiteral("Label")); !l.isNull(); l = l.nextSiblingElement(QStringLiteral("Label")))
        labels.append({l.attribute(QStringLiteral("id")).toInt(), l.text(), QColor(l.attribute(QStringLiteral("color")))});

    const QDomElement st = rootEl.firstChildElement(QStringLiteral("Statuses"));
    statusTitle = st.attribute(QStringLiteral("title"), tr("Status"));
    statuses.clear();
    for (QDomElement s = st.firstChildElement(QStringLiteral("Status")); !s.isNull(); s = s.nextSiblingElement(QStringLiteral("Status")))
        statuses.append({s.attribute(QStringLiteral("id")).toInt(), s.text()});

    keywords.clear();
    const QDomElement kws = rootEl.firstChildElement(QStringLiteral("Keywords"));
    for (QDomElement k = kws.firstChildElement(QStringLiteral("Keyword")); !k.isNull(); k = k.nextSiblingElement(QStringLiteral("Keyword")))
        keywords.append({k.attribute(QStringLiteral("id")), k.text(), QColor(k.attribute(QStringLiteral("color"))),
                         k.attribute(QStringLiteral("parent"))});

    customFields.clear();
    const QDomElement cfs = rootEl.firstChildElement(QStringLiteral("CustomFields"));
    for (QDomElement fe = cfs.firstChildElement(QStringLiteral("Field")); !fe.isNull(); fe = fe.nextSiblingElement(QStringLiteral("Field"))) {
        CustomField cf;
        cf.id = fe.attribute(QStringLiteral("id"));
        cf.title = fe.attribute(QStringLiteral("title"));
        const QString t = fe.attribute(QStringLiteral("type"));
        cf.type = t == QLatin1String("checkbox") ? CustomFieldType::Checkbox
                  : t == QLatin1String("list")   ? CustomFieldType::List
                  : t == QLatin1String("date")   ? CustomFieldType::Date
                                                 : CustomFieldType::Text;
        cf.defaultValue = fe.attribute(QStringLiteral("default"));
        for (QDomElement o = fe.firstChildElement(QStringLiteral("Option")); !o.isNull(); o = o.nextSiblingElement(QStringLiteral("Option")))
            cf.options << o.text();
        customFields.append(cf);
    }

    sectionTypes.clear();
    const QDomElement sts = rootEl.firstChildElement(QStringLiteral("SectionTypes"));
    for (QDomElement s = sts.firstChildElement(QStringLiteral("Type")); !s.isNull(); s = s.nextSiblingElement(QStringLiteral("Type")))
        sectionTypes.append({s.attribute(QStringLiteral("id")), s.text()});
    auto readList = [&](const QString &tag) {
        QStringList l;
        const QDomElement e = sts.firstChildElement(tag);
        for (QDomElement x = e.firstChildElement(QStringLiteral("Level")); !x.isNull(); x = x.nextSiblingElement(QStringLiteral("Level")))
            l << x.text();
        return l;
    };
    structureDefaults.folders = readList(QStringLiteral("Folders"));
    structureDefaults.fileGroups = readList(QStringLiteral("FileGroups"));
    structureDefaults.files = readList(QStringLiteral("Files"));
    if (sectionTypes.isEmpty()) {
        setupDefaults();
    }

    const QDomElement fm = rootEl.firstChildElement(QStringLiteral("Formatting"));
    if (!fm.isNull()) {
        formatting.fontFamily = fm.attribute(QStringLiteral("font"));
        formatting.fontSize = fm.attribute(QStringLiteral("size"), QStringLiteral("13")).toDouble();
        formatting.lineSpacing = fm.attribute(QStringLiteral("lineSpacing"), QStringLiteral("1.2")).toDouble();
        formatting.firstLineIndent = fm.attribute(QStringLiteral("indent"), QStringLiteral("24")).toDouble();
        formatting.paragraphSpacing = fm.attribute(QStringLiteral("spacing"), QStringLiteral("4")).toDouble();
        formatting.justify = fm.attribute(QStringLiteral("justify")) == QLatin1String("1");
    }
    const QDomElement stylesEl = rootEl.firstChildElement(QStringLiteral("Styles"));
    if (!stylesEl.isNull())
        formatting.styles = QSharedPointer<const StyleSheet>(new StyleSheet(styles::fromXml(stylesEl)));
    const QDomElement scriptEl = rootEl.firstChildElement(QStringLiteral("ScriptFormat"));
    if (!scriptEl.isNull()) {
        bool ok = false;
        const script::Format sf = script::fromJson(QJsonDocument::fromJson(scriptEl.text().toUtf8()).object(), &ok);
        if (ok)
            formatting = script::withFormat(formatting, sf);
    }

    const QDomElement tg = rootEl.firstChildElement(QStringLiteral("Targets"));
    if (!tg.isNull()) {
        targets.draftTarget = tg.attribute(QStringLiteral("draft")).toInt();
        targets.draftChars = tg.attribute(QStringLiteral("draftChars")) == QLatin1String("1");
        targets.deadline = QDate::fromString(tg.attribute(QStringLiteral("deadline")), Qt::ISODate);
        targets.sessionTarget = tg.attribute(QStringLiteral("session")).toInt();
        targets.sessionChars = tg.attribute(QStringLiteral("sessionChars")) == QLatin1String("1");
        targets.sessionReset = tg.attribute(QStringLiteral("sessionReset"), QStringLiteral("midnight"));
        targets.countAnywhere = tg.attribute(QStringLiteral("countAnywhere")) == QLatin1String("1");
        targets.autoFromDeadline = tg.attribute(QStringLiteral("autoFromDeadline")) == QLatin1String("1");
        targets.writingDays = tg.attribute(QStringLiteral("writingDays"), QStringLiteral("1111111"));
        targets.sessionStart = QDateTime::fromString(tg.attribute(QStringLiteral("sessionStart")), Qt::ISODate);
        targets.sessionStartDraft = tg.attribute(QStringLiteral("sessionStartDraft")).toInt();
        targets.sessionStartTotal = tg.attribute(QStringLiteral("sessionStartTotal")).toInt();
        targets.sessionStartDraftChars = tg.attribute(QStringLiteral("sessionStartDraftChars")).toInt();
        targets.sessionStartTotalChars = tg.attribute(QStringLiteral("sessionStartTotalChars")).toInt();
    }

    projectBookmarks = readBookmarks(rootEl.firstChildElement(QStringLiteral("ProjectBookmarks")));

    collections.clear();
    const QDomElement cols = rootEl.firstChildElement(QStringLiteral("Collections"));
    for (QDomElement ce = cols.firstChildElement(QStringLiteral("Collection")); !ce.isNull();
         ce = ce.nextSiblingElement(QStringLiteral("Collection"))) {
        Collection c;
        c.id = ce.attribute(QStringLiteral("id"));
        c.name = ce.attribute(QStringLiteral("name"));
        c.color = QColor(ce.attribute(QStringLiteral("color")));
        c.isSearch = ce.attribute(QStringLiteral("search")) == QLatin1String("1");
        if (c.isSearch)
            c.query = readQuery(ce.firstChildElement(QStringLiteral("Query")));
        for (QDomElement d = ce.firstChildElement(QStringLiteral("Doc")); !d.isNull(); d = d.nextSiblingElement(QStringLiteral("Doc")))
            c.items << d.text();
        collections.append(c);
    }

    const QDomElement sp = rootEl.firstChildElement(QStringLiteral("SpecialFolders"));
    templatesFolderId = sp.attribute(QStringLiteral("templates"));
    notesFolderId = sp.attribute(QStringLiteral("notes"));
    compositionBackground = sp.attribute(QStringLiteral("compositionBackground"));

    const QDomElement cp = rootEl.firstChildElement(QStringLiteral("Compile"));
    if (!cp.isNull()) {
        compile.formatName = cp.attribute(QStringLiteral("format"), compile.formatName);
        compile.exporterId = cp.attribute(QStringLiteral("exporter"), compile.exporterId);
        compile.compileGroupId = cp.attribute(QStringLiteral("group"));
        compile.treatAsComplete = cp.attribute(QStringLiteral("complete")) == QLatin1String("1");
        compile.includeGroupText = cp.attribute(QStringLiteral("groupText"), QStringLiteral("1")) == QLatin1String("1");
        compile.frontMatterId = cp.attribute(QStringLiteral("front"));
        compile.backMatterId = cp.attribute(QStringLiteral("back"));
        compile.lastOutputDir = cp.attribute(QStringLiteral("outputDir"));
        compile.titleOverride = cp.attribute(QStringLiteral("titleOverride"));
        compile.authorOverride = cp.attribute(QStringLiteral("authorOverride"));
        compile.subtitle = cp.attribute(QStringLiteral("subtitle"));
        compile.subject = cp.attribute(QStringLiteral("subject"));
        compile.keywords = cp.attribute(QStringLiteral("keywords"));
        compile.publisher = cp.attribute(QStringLiteral("publisher"));
        compile.description = cp.attribute(QStringLiteral("description"));
        compile.language = cp.attribute(QStringLiteral("language"));
        compile.date = cp.attribute(QStringLiteral("date"));
        compile.coverId = cp.attribute(QStringLiteral("cover"));
        compile.identifier = cp.attribute(QStringLiteral("identifier"));
        for (QDomElement a = cp.firstChildElement(QStringLiteral("Assign")); !a.isNull(); a = a.nextSiblingElement(QStringLiteral("Assign")))
            compile.layoutAssignments.insert(a.attribute(QStringLiteral("key")), a.attribute(QStringLiteral("layout")));
        for (QDomElement a = cp.firstChildElement(QStringLiteral("Include")); !a.isNull(); a = a.nextSiblingElement(QStringLiteral("Include")))
            compile.includeOverrides.insert(a.attribute(QStringLiteral("id")), a.attribute(QStringLiteral("value")) == QLatin1String("1"));
        compile.fontOverride = cp.attribute(QStringLiteral("fontOverride"));
        compile.objectSheets = cp.attribute(QStringLiteral("objectSheets")) == QLatin1String("1");
        compile.objectCategories = cp.attribute(QStringLiteral("objectCategories")).split(QLatin1Char(','), Qt::SkipEmptyParts);
        compile.replacements.clear();
        for (QDomElement a = cp.firstChildElement(QStringLiteral("Replace")); !a.isNull(); a = a.nextSiblingElement(QStringLiteral("Replace"))) {
            CompileReplacement r;
            r.find = a.attribute(QStringLiteral("find"));
            r.replace = a.attribute(QStringLiteral("with"));
            r.regex = a.attribute(QStringLiteral("regex")) == QLatin1String("1");
            r.matchCase = a.attribute(QStringLiteral("case"), QStringLiteral("1")) == QLatin1String("1");
            r.wholeWords = a.attribute(QStringLiteral("whole")) == QLatin1String("1");
            r.enabled = a.attribute(QStringLiteral("on"), QStringLiteral("1")) == QLatin1String("1");
            if (!r.find.isEmpty())
                compile.replacements << r;
        }
    }

    history.clear();
    const QDomElement hist = rootEl.firstChildElement(QStringLiteral("History"));
    for (QDomElement d = hist.firstChildElement(QStringLiteral("Day")); !d.isNull(); d = d.nextSiblingElement(QStringLiteral("Day"))) {
        WritingDay w;
        w.date = QDate::fromString(d.attribute(QStringLiteral("date")), Qt::ISODate);
        w.draftWords = d.attribute(QStringLiteral("draft")).toInt();
        w.otherWords = d.attribute(QStringLiteral("other")).toInt();
        w.draftChars = d.attribute(QStringLiteral("draftChars")).toInt();
        w.otherChars = d.attribute(QStringLiteral("otherChars")).toInt();
        if (w.date.isValid())
            history.append(w);
    }
    return true;
}

} // namespace qb
