#pragma once
// A document in a window of its own ("Quick Reference panel"): text or
// media, with label and status in the footer, an optional lower pane for
// synopsis, notes, keywords or metadata, and a sidebar with the project
// bookmarks to move between documents inside the panel.
#include "core/Project.h"

#include <QPointer>
#include <QVariantMap>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QSplitter;
class QStackedWidget;
class QToolButton;

namespace qb {

class BookmarkListWidget;
class MediaView;
class TextColumnView;
class TextEditor;

class QuickRefWindow : public QWidget
{
    Q_OBJECT
public:
    // what the lower pane shows
    enum Panel { NoPanel = 0, Synopsis, Notes, Keywords, Metadata };

    QuickRefWindow(Project *p, BinderItem *item, QWidget *parent = nullptr);
    BinderItem *item() const { return m_item; }
    void showItem(BinderItem *item);
    void setFloating(bool on);
    bool isFloating() const;

    Panel panel() const { return m_panel; }
    void setPanel(Panel panel);
    bool bookmarksShown() const;
    void setBookmarksShown(bool on);
    BookmarkListWidget *bookmarkList() const { return m_bookmarks; }
    TextColumnView *textView() const { return m_view; }
    // "+" below the bookmarks: a new text document in that folder, added to
    // the project bookmarks and shown in this panel.
    BinderItem *createBookmarkedDocument(BinderItem *folder, const QString &title);
    // Folders a new bookmarked document can go to (the dialog of "+").
    static QList<BinderItem *> noteFolders(Project *p);

    // kept with the project: document, place on screen, lower pane, sidebar
    QVariantMap saveState() const;
    void restoreState(const QVariantMap &state);

signals:
    void revealRequested(qb::BinderItem *item);
    void openInEditorRequested(qb::BinderItem *item);
    void itemShown(qb::BinderItem *item);
    void linkActivated(const QString &href);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void buildPanels();
    void refreshHeader();
    void refreshFooter();
    // later: from a signal of the project (the form may be the sender; it is rebuilt after the signal)
    void refreshPanel(bool later = false);
    void refreshKeywords();
    void rebuildMetadata();
    void addBookmarkedDocument();
    void headerMenu();

    QPointer<Project> m_project;
    BinderItem *m_item = nullptr;
    QString m_itemId;        // to notice that the document is gone
    QString m_metaShownFor;  // document the metadata form was built for
    Panel m_panel = NoPanel;
    bool m_loading = false;
    bool m_metaRebuildPending = false;

    // header
    QWidget *m_header = nullptr;
    QToolButton *m_icon = nullptr;
    QLineEdit *m_title = nullptr;
    QToolButton *m_panelButton = nullptr;
    QToolButton *m_pin = nullptr;
    // body
    QSplitter *m_sideSplit = nullptr;
    QWidget *m_sidebar = nullptr;
    BookmarkListWidget *m_bookmarks = nullptr;
    QSplitter *m_panelSplit = nullptr;
    QStackedWidget *m_content = nullptr;
    TextColumnView *m_view = nullptr;
    MediaView *m_media = nullptr;
    QLabel *m_noContent = nullptr;
    QStackedWidget *m_panels = nullptr;
    QLabel *m_panelTitle = nullptr;
    QWidget *m_panelBox = nullptr;
    QPlainTextEdit *m_synopsis = nullptr;
    TextEditor *m_notes = nullptr;
    QListWidget *m_keywords = nullptr;
    QWidget *m_metaPage = nullptr;
    QFormLayout *m_metaForm = nullptr;
    // footer
    QToolButton *m_bookmarkToggle = nullptr;
    QLabel *m_count = nullptr;
    QComboBox *m_label = nullptr, *m_status = nullptr;
};

} // namespace qb
