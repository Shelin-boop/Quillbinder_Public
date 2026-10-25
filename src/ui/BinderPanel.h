#pragma once
#include "core/Project.h"
#include "core/ProjectTypes.h"

#include <QListWidget>
#include <QPointer>
#include <QTabBar>
#include <QTimer>
#include <QTreeView>
#include <QWidget>
#include <functional>

class QLineEdit;
class QStackedWidget;
class QToolButton;
class QLabel;
class QMenu;

namespace qb {


class BinderTree : public QTreeView
{
    Q_OBJECT
public:
    explicit BinderTree(QWidget *parent = nullptr);
    // The document the active editor shows; marked with a grey bar while it
    // is not the selected one (after Back / Forward, a link, a locked editor).
    void setEditorItemId(const QString &id);
    QString editorItemId() const { return m_editorItemId; }
    void setModel(QAbstractItemModel *model) override;
    // The title column is as wide as the widest visible title (at least the
    // width of the view), so long titles are reached with the horizontal
    // scroll bar instead of being cut off (0.17).
    void updateColumnWidth();
    int contentWidth() const;
signals:
    void enterPressed();
    void deletePressed();
    void spacePressed();
    void selectionChangedByUser();

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void startDrag(Qt::DropActions supportedActions) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    void scheduleWidthUpdate();
    QTimer *m_widthTimer = nullptr;
    // A click loads the document when the button is released, so that a
    // document can be dragged elsewhere (an editor's header bar, a copyholder)
    // without being loaded into the editor on the way.
    bool m_mouseDown = false;
    bool m_selectionPending = false;
    bool m_droppedHere = false;
    QString m_editorItemId;
};

// The flat list of documents that takes the binder's place for search
// results and collections. Its rows can be dragged like binder documents; an
// arbitrary collection can be reordered and takes documents dropped into it.
class ItemListWidget : public QListWidget
{
    Q_OBJECT
public:
    explicit ItemListWidget(QWidget *parent = nullptr);
    void setProject(Project *p) { m_project = p; }
    void setReorderable(bool on) { m_reorderable = on; }
    bool isReorderable() const { return m_reorderable; }
    QList<BinderItem *> selectedBinderItems() const;
    BinderItem *binderItemAt(const QPoint &viewportPos) const;
signals:
    void spacePressed();
    void deletePressed();
    // row: where in the list; internal: the rows were dragged inside this list
    void itemsDropped(const QList<qb::BinderItem *> &items, int row, bool internal);

protected:
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override;
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction | Qt::CopyAction; }
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void paintEvent(QPaintEvent *e) override;

private:
    int rowForDrop(const QPoint &pos) const;
    QPointer<Project> m_project;
    bool m_reorderable = false;
    int m_dropRow = -1;
};

// The tabs above the binder: "Binder" and one per collection. Documents can
// be dropped onto the tab of an arbitrary collection; the tabs can be dragged
// into another order.
class CollectionTabs : public QTabBar
{
    Q_OBJECT
public:
    explicit CollectionTabs(QWidget *parent = nullptr);
    std::function<bool(int tab)> takesDocuments; // set by the panel
signals:
    void documentsDropped(int tab, const QMimeData *data);

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
};

class BinderPanel : public QWidget
{
    Q_OBJECT
public:
    explicit BinderPanel(QWidget *parent = nullptr);
    void setProject(Project *p);
    BinderTree *tree() const { return m_tree; }

    QList<BinderItem *> selectedItems() const;
    BinderItem *currentItem() const;
    void select(const QList<BinderItem *> &items, bool notify);
    void reveal(BinderItem *item);
    void editTitle(BinderItem *item);
    void setFooterMenus(QMenu *addMenu, QMenu *gearMenu);

    // project search
    void showSearch(bool focus);
    void hideSearch();
    bool isSearching() const;
    void runSearch(const SearchQuery &q);
    SearchQuery searchQuery() const { return m_query; }
    // the list shows the results of a project search (not the binder, not a collection)
    bool showsSearchResults() const;
    // Options menu of the search field. "Search In": one place (or all with a
    // null member); multi (Alt held) switches that place on or off beside the others.
    void chooseSearchIn(bool SearchQuery::*member, bool multi);
    void fillSearchMenu(QMenu *menu);
    void setSearchQuery(const SearchQuery &q);   // options without running a search (empty text)
    QLineEdit *searchField() const { return m_searchEdit; }

    // collections
    void setCollectionsVisible(bool on);
    bool collectionsVisible() const;
    void refreshCollections();
    void showCollection(const QString &id);
    QString currentCollectionId() const;
    void addToCollection(const QString &collectionId, const QList<BinderItem *> &items, int row = -1);
    // true while the list shows an arbitrary collection (not the binder, not a search)
    bool showsArbitraryCollection() const;
    void removeFromCollection(const QList<BinderItem *> &items);
    ItemListWidget *resultsList() const { return m_results; }
    CollectionTabs *collectionTabs() const { return m_tabs; }
    void setEditorItem(BinderItem *item);

    QStringList expandedIds() const;
    void restoreExpanded(const QStringList &ids);

signals:
    void selectionChanged(const QList<qb::BinderItem *> &items);
    void contextMenuRequested(const QPoint &globalPos);
    void newDocumentRequested();
    void newFolderRequested();
    void trashRequested();
    void quickReferenceRequested();
    // a project search was run, changed or closed (the editors show its matches)
    void searchChanged();

private:
    void onSearchTextChanged();
    void showResults(const QList<BinderItem *> &items, const QString &title, const QColor &color);
    void onResultSelection();
    void onTabChanged(int index);
    void buildSearchMenu();
    void selectSearchTab(bool search);

    QPointer<Project> m_project;
    BinderTree *m_tree = nullptr;
    QWidget *m_searchBar = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QToolButton *m_searchOptions = nullptr;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_resultsPage = nullptr;
    QLabel *m_resultsTitle = nullptr;
    ItemListWidget *m_results = nullptr;
    CollectionTabs *m_tabs = nullptr;
    QWidget *m_collectionsBar = nullptr;
    QToolButton *m_addButton = nullptr;
    QToolButton *m_folderButton = nullptr;
    QToolButton *m_gearButton = nullptr;
    SearchQuery m_query;
    QString m_shownCollection; // id or "__search"
    bool m_updatingTabs = false;
};

} // namespace qb
