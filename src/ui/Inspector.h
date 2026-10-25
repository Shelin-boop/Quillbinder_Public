#pragma once
#include "core/Project.h"
#include "core/Snapshots.h"

#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QWidget>

class QTextDocument;

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QStackedWidget;
class QTextBrowser;
class QToolButton;
class QVBoxLayout;
class QScrollArea;
class QSplitter;

namespace qb {

class BookmarkListWidget;
class MindMapDoc;
class MindMapPanel;
class ObjectsPanel;

class TextEditor;

class Inspector : public QWidget
{
    Q_OBJECT
public:
    // (the numbers are stored in layouts; the mind map came last in 0.17 and the
    // objects in 0.20, but their buttons sit between Bookmarks and Metadata)
    enum Tab { Notes = 0, Bookmarks, Metadata, Snapshots, Comments, MindMap, Objects };
    explicit Inspector(QWidget *parent = nullptr);
    void setProject(Project *p);
    void setItem(BinderItem *item);
    void setEditor(TextEditor *editor);
    BinderItem *item() const { return m_item; }
    void showTab(Tab t);
    int currentTab() const;
    void focusNote(const QString &noteId);
    void addLinkedNote(bool footnote);
    void takeSnapshot(bool askTitle);
    void refreshAll();
    // The picture shown on the index card instead of the synopsis (a file is copied into the project; empty: none).
    void setSynopsisImage(const QString &file);
    QListWidget *keywordList() const { return m_keywords; }
    BookmarkListWidget *bookmarkList() const { return m_bookmarks; }            // the document's
    BookmarkListWidget *projectBookmarkList() const { return m_projectBookmarks; }
    QLabel *synopsisImageArea() const { return m_synopsisImage; }

    class MindMapPanel *mindMapPanel() const { return m_mindMap; }
    class ObjectsPanel *objectsPanel() const { return m_objects; }
    // Comments and footnotes of several documents (a folder, combined text): the
    // inspector lists them by document (0.17). Empty: those of the editor.
    void setCommentScope(const QList<BinderItem *> &items);

    // ---- 0.21.1
    // The bookmark preview: an editable copy of the bookmarked document, kept in
    // step with it both ways (DocMirror), at a zoom of its own.
    TextEditor *bookmarkPreview() const { return m_bookmarkPreview; }
    class DocMirror *previewMirror() const { return m_previewMirror; }
    QTextBrowser *snapshotView() const { return m_snapView; }
    // The zoom of the part the zoom list at the bottom shows and sets: the part with
    // the keyboard, else the one zoomed last in this tab, else the tab's first part.
    // (The Mind Map tab: the board's own zoom, key "board".)
    QString currentZoomKey() const;
    // The tab, the dividers, the bookmark and the object shown: kept with the project.
    QVariantMap saveState();
    void restoreState(const QVariantMap &state);

signals:
    void openItemRequested(qb::BinderItem *item);
    void openNoteRequested(qb::BinderItem *item, const QString &noteId);
    void openMindMapWindow(qb::MindMapDoc *doc);
    void openObjectsWindow(const QString &objectId);
    // a link in a long text of an object that is no object link (documents, web pages)
    void objectLinkActivated(const QString &href);
    void keywordsDialogRequested();
    void projectSettingsRequested(int page);
    void selectNoteRequested(const QString &noteId);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    QWidget *buildNotesTab();
    QWidget *buildBookmarksTab();
    QWidget *buildMetadataTab();
    QWidget *buildSnapshotsTab();
    QWidget *buildCommentsTab();
    void refreshMindMap();
    void refreshNotesTab();
    void refreshBookmarks();
    void refreshMetadata();
    void refreshCustomFields();
    void refreshKeywords();
    void refreshSnapshots();
    void refreshComments();
    void refreshScopeComments();
    void refreshLabelStatus();
    void updateTabDots();
    void applyTheme();
    void showSnapshot();
    QList<Bookmark> &bookmarksOf(bool project);
    void showBookmarkPreview(BinderItem *it);
    QColor labelTint(const QColor &paper) const;
    void applyLabelTint();

    QPointer<Project> m_project;
    BinderItem *m_item = nullptr;
    QPointer<TextEditor> m_editor;
    QStackedWidget *m_stack = nullptr;
    QList<QToolButton *> m_tabButtons;

    // notes
    QLineEdit *m_cardTitle = nullptr;
    QPlainTextEdit *m_synopsis = nullptr;
    QLabel *m_synopsisImage = nullptr;
    QToolButton *m_imageToggle = nullptr;
    TextEditor *m_notes = nullptr;
    QComboBox *m_notesScope = nullptr;
    // bookmarks
    BookmarkListWidget *m_bookmarks = nullptr;
    BookmarkListWidget *m_projectBookmarks = nullptr;
    TextEditor *m_bookmarkPreview = nullptr;
    QPointer<QTextDocument> m_previewCopy; // the preview's copy of a document an editor shows
    // metadata
    QLabel *m_created = nullptr, *m_modified = nullptr;
    QCheckBox *m_include = nullptr;
    QComboBox *m_sectionType = nullptr;
    QWidget *m_customBox = nullptr;
    QFormLayout *m_customForm = nullptr;
    QListWidget *m_keywords = nullptr;
    // snapshots
    QListWidget *m_snapList = nullptr;
    QTextBrowser *m_snapView = nullptr;
    QToolButton *m_compare = nullptr;
    QComboBox *m_granularity = nullptr;
    QList<Snapshot> m_snaps;
    // comments
    QScrollArea *m_commentScroll = nullptr;
    QWidget *m_commentList = nullptr;
    QVBoxLayout *m_commentLayout = nullptr;
    QHash<QString, QWidget *> m_noteCards;
    QTimer m_commentTimer;
    QString m_focusNote;
    QList<BinderItem *> m_commentScope;  // several documents (0.17)
    QString m_scopeKey;
    // the bookmark preview (0.21.1)
    class DocMirror *m_previewMirror = nullptr;
    QString m_previewItemId;
    void applyPreviewZoom();
    void stopPreview();
    void refreshMindMapZoom();
    // the place in the notes, per document (and the project's notes), 0.21.1
    QString m_notesKey;
    void rememberNotesPlace();
    void restoreNotesPlace();
    // dividers inside the tabs (kept with the project, 0.21.1)
    QSplitter *m_notesSplit = nullptr, *m_bookmarkSplit = nullptr, *m_snapSplit = nullptr;
    // the part of each tab zoomed last (0.21.1)
    QHash<int, QString> m_lastZoomPart;
    QWidget *m_tabsRow = nullptr, *m_bottomRow = nullptr;
    QLabel *m_zoomTitle = nullptr;
    // mind map
    class MindMapPanel *m_mindMap = nullptr;
    // zoom (0.21): the field at the bottom; the key of a tab's zoom (current tab when -1)
    QComboBox *m_zoomBox = nullptr;
    QString zoomKey(int tab = -1) const;
    void applyZoomMode();
    void applyNotesZoom();
    void refreshZoomBox();
    class ObjectsPanel *m_objects = nullptr;
    // bottom
    QComboBox *m_label = nullptr, *m_status = nullptr;
    QLabel *m_labelTitle = nullptr, *m_statusTitle = nullptr;
    bool m_updating = false;
};

} // namespace qb
