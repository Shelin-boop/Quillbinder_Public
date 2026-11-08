#pragma once
#include "core/BinderItem.h"
#include "core/ProjectTypes.h"
#include "core/SyncSafety.h"

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
    // The save of the autosave timer (0.60.5): the same as save() for the texts and the
    // binder, but a project in Scrivener format gets only what changed (scrivener::sync,
    // quick), and a change of counts alone rewrites the project file at most once a minute.
    // Every other save (switching to another program, closing, File > Save) is a full save.
    bool autosave(QString *error = nullptr);
    // the binder or the project's settings changed since the .scrivx of a project in Scrivener
    // format was last written (0.60.5): set by markIndexDirty (not by a change of counts),
    // cleared by scrivener::write; a quick write leaves the .scrivx alone while it is false
    bool binderChangedSinceSync = true;
    bool saveIndex(QString *error = nullptr);
    // Saves when no change came for the autosave delay, but at the latest
    // max(maxSaveWait, delay) after the first unsaved change (0.20: before, changes
    // that kept coming were never saved). 0.64: after max(gapAfter, delay) of changes that
    // keep coming, the first short gap (gapPause without a change, a breath between words)
    // saves too, so typing that goes on is saved about every 5 s without waiting for a pause.
    void scheduleSave();
    void setAutosaveDelay(int ms) { m_saveTimer.setInterval(ms); }
    void setMaxSaveWait(int ms) { m_maxSaveWait = ms; }
    void setGapSave(int afterMs, int pauseMs) { m_gapSaveAfter = afterMs; m_gapTimer.setInterval(pauseMs); }
    int autosaveCount() const { return m_autosaves; }   // tests
    // The editor layout, open documents, places ... (uiState) changed, nothing else (0.64): written
    // with the next save into this computer's own file, without touching the project file, so
    // opening and closing a project on two computers does not make two versions of it.
    void markUiStateDirty() { m_uiDirty = true; }
    // settings/ui-<computer>.json (before 0.64: settings/ui.json, still read when the computer
    // has no file of its own yet)
    QString uiStateFile() const;
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
    // the counts of an item's text changed while it was edited (0.60.5): like
    // notifyItemChanged, but the project file waits for the next full save or a minute
    void notifyItemCounted(BinderItem *item);
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
    // A file of the project changed on disk after this program read it (another computer through
    // a sync program, or another program), and this program was about to write its own version
    // over it (0.64): the other version was kept first. itemId: the document ("" = the project
    // file); what: readable ("text", "notes", "project file" ...); copy: the kept copy in
    // settings/conflicts; snapshot: the text also became a snapshot of the document.
    void conflictKept(const QString &itemId, const QString &what, const QString &copy, bool snapshot);

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
    QTimer m_gapTimer;          // a short gap in changes that keep coming (0.64)
    int m_gapSaveAfter = 5000;
    int m_autosaves = 0;
    bool m_uiDirty = false;
    syncsafe::Stamp m_indexMark; // the project file as read or last written here (0.64)
    bool writeUiState();
    QByteArray m_loadedIndex;   // the project file as it was read (for the spare copy)
    bool m_indexDirty = false;
    bool m_countsDirty = false;     // counts and modification times only (0.60.5)
    QElapsedTimer m_lastIndexWrite; // the last write of the project file
    bool saveImpl(QString *error, bool quick);

    friend class ProjectIO;
};

} // namespace qb
