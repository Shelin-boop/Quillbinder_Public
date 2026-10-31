#pragma once
#include "core/BinderItem.h"
#include "core/ProjectTypes.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <functional>
#include <memory>

namespace qb {

class BinderModel;
class DocumentStore;
namespace objects {
class Store;
}
namespace script {
struct Format;
}

class Project : public QObject
{
    Q_OBJECT
public:
    ~Project() override;

    // Creates "<parentDir>/<name>.quillbinder/<name>.qbx" with the three
    // special folders. Templates add their own structure afterwards.
    static std::unique_ptr<Project> create(const QString &parentDir, const QString &name, QString *error);
    // path may be the project folder or the .qbx file inside it
    static std::unique_ptr<Project> open(const QString &path, QString *error);
    static QString resolveProjectFile(const QString &path);

    bool save(QString *error = nullptr);
    bool saveIndex(QString *error = nullptr);
    // Saves when no change came for the autosave delay, but at the latest
    // max(maxSaveWait, delay) after the first unsaved change (0.20: before, changes
    // that kept coming were never saved).
    void scheduleSave();
    void setAutosaveDelay(int ms) { m_saveTimer.setInterval(ms); }
    void setMaxSaveWait(int ms) { m_maxSaveWait = ms; }
    // A message for the writer after opening (the project file was repaired ...); "" = none.
    QString openNotice;
    // The copy of the project file kept when the project last opened (settings/spare-copy.qbx).
    static QString spareCopyOf(const QString &projectFile);
    // Puts the spare copy in place of a damaged project file, which is kept as
    // "<name>.qbx.damaged-<time>" (keptAs).
    static bool restoreSpareCopy(const QString &projectFile, QString *keptAs, QString *error);
    // Documents whose text is in content/ but that the binder does not list (written after
    // the spare copy was made): put into a folder "Recovered Documents" of Research.
    // Returns how many; the folder's title goes into folderTitle.
    int recoverUnlistedDocuments(QString *folderTitle = nullptr);
    bool hasUnsavedChanges() const;

    QString folderPath() const { return m_folder; }
    QString indexPath() const { return m_indexFile; }
    QString title() const { return m_title; }
    void setTitle(const QString &t);
    QString contentDir(const QString &id) const;
    QString snapshotsDir(const QString &id) const;
    QString settingsDir() const;
    QString mediaFilePath(const BinderItem *item) const;

    BinderItem *root() const { return m_root; }
    BinderItem *draft() const { return m_draft; }
    BinderItem *research() const { return m_research; }
    BinderItem *trash() const { return m_trash; }
    BinderItem *item(const QString &id) const { return m_items.value(id, nullptr); }
    QList<BinderItem *> allItems() const;
    void registerItem(BinderItem *it);
    void unregisterItem(BinderItem *it);

    BinderModel *model() const { return m_model; }
    DocumentStore *docs() const { return m_docs; }
    // The objects of the project (0.20; core/Objects.h): read from objects/ on first use.
    objects::Store *objects();
    bool objectsLoaded() const { return m_objects != nullptr; }

    // ---- settings --------------------------------------------------------
    StorageFormat storage = StorageFormat::Markdown;
    // The oldest program version that understands everything in this project
    // ("needs" in the project file; empty = any). Read when the project is
    // opened; a program older than this would lose content when it saves.
    QString needsVersion;
    QString labelTitle = QStringLiteral("Label");
    QString statusTitle = QStringLiteral("Status");
    QList<Label> labels;
    QList<Status> statuses;
    QList<Keyword> keywords;
    QList<CustomField> customFields;
    QList<SectionType> sectionTypes;
    StructureDefaults structureDefaults;
    Formatting formatting;
    // the style list of the format bar and of the right-click menus (0.40.3): the order chosen
    // ("-" = No Style) and the entries left out; styles not named follow in the order of the
    // style sheet. Empty: all styles in the order of the sheet.
    QStringList styleListOrder;
    QStringList styleListHidden;
    Targets targets;
    QList<Bookmark> projectBookmarks;
    QList<Collection> collections;
    QString templatesFolderId;
    QString notesFolderId;            // where "new bookmark note" documents go
    CompileProjectSettings compile;
    QList<WritingDay> history;
    QVariantMap uiState;              // editor layout etc. (settings/ui.xml)
    QString compositionBackground;    // image path (optional)
    // Scrivener format: the .scriv folder this project is the working copy of
    // ("" = an ordinary project). Every save writes it (import/ScrivenerFormat.h).
    QString scrivenerPath;
    // The file the writer knows the project by: the .scrivx of a project in
    // Scrivener format, the project file otherwise.
    QString displayFile() const;
    QString displayFolder() const;

    const Label *labelById(int id) const;
    const Status *statusById(int id) const;
    const Keyword *keywordById(const QString &id) const;
    const CustomField *customFieldById(const QString &id) const;
    const SectionType *sectionTypeById(const QString &id) const;
    int nextLabelId() const;
    int nextStatusId() const;

    // Section type used when compiling (resolves "structure based").
    QString effectiveSectionType(const BinderItem *item, const BinderItem *compileRoot = nullptr) const;
    bool isInDraft(const BinderItem *item) const;
    bool isInTrash(const BinderItem *item) const;
    bool isTemplate(const BinderItem *item) const;

    // statistics
    int draftWordCount() const;
    int draftCharCount() const;
    int totalWordCount() const;
    int totalCharCount() const;
    void startSession(bool force);
    int sessionWords() const;
    int sessionChars() const;
    int sessionTargetToday() const; // may be derived from the deadline
    void recordHistory();

    // Replace the project's styles and bring all text up to date (the whole
    // project: open documents at once, the others in their files). renamed:
    // old name -> new name. progress(done, total) may return false to ask for
    // nothing; the update always runs to the end. Returns false if a file
    // could not be written (error says which).
    bool setStyleSheet(const StyleSheet &sheet, const QHash<QString, QString> &renamed = {},
                       const std::function<bool(int, int)> &progress = {}, QString *error = nullptr);

    // The script format of the project changes (Format > Scriptwriting). Script
    // text follows: elements keep their names (renamedElements: old -> new
    // name), elements the new format does not have go to the element with the
    // same role, or keep their look as formatting set by hand.
    bool setScriptFormat(const script::Format &format, const QHash<QString, QString> &renamedElements = {},
                         const std::function<bool(int, int)> &progress = {}, QString *error = nullptr);
    // Switch script mode on or off for a document. Switching it on in an
    // empty document gives the first paragraph the first element.
    void setScriptMode(BinderItem *item, bool on);
    static int compareVersionText(const QString &a, const QString &b);
    // "needs" of a project file, read without opening the project ("" = any version)
    static QString neededVersion(const QString &projectFile);

    void notifyItemChanged(BinderItem *item, bool touchModified = true);
    void notifySettingsChanged();
    // project bookmarks or the bookmarks of a document were changed
    void notifyBookmarksChanged();
    void markIndexDirty();

signals:
    void itemChanged(qb::BinderItem *item);
    void settingsChanged();
    void bookmarksChanged();
    void stylesChanged();
    void scriptFormatChanged();
    void titleChanged();
    void countsChanged();
    void saved();
    void saveFailed(const QString &error);

private:
    explicit Project(QObject *parent = nullptr);
    bool loadIndex(QString *error);
    void setupDefaults();
    void ensureSpecialFolders();

    QString m_folder;
    QString m_indexFile;
    QString m_title;
    BinderItem *m_root = nullptr;
    BinderItem *m_draft = nullptr;
    BinderItem *m_research = nullptr;
    BinderItem *m_trash = nullptr;
    QHash<QString, BinderItem *> m_items;
    BinderModel *m_model = nullptr;
    DocumentStore *m_docs = nullptr;
    objects::Store *m_objects = nullptr;
    QTimer m_saveTimer;
    QElapsedTimer m_dirtySince; // the first change not saved yet
    bool m_forcedSave = false;
    int m_maxSaveWait = 30000;
    QByteArray m_loadedIndex;   // the project file as it was read (for the spare copy)
    bool m_indexDirty = false;

    friend class ProjectIO;
};

} // namespace qb
