#pragma once
#include "core/Project.h"
#include <QPointer>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QTreeView>

class QMenu;

namespace qb {


class OutlinerProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setRoots(const QList<BinderItem *> &roots);
    void setFilterText(const QString &t);
    void setFilter(Project *p, const ItemFilter &f);
    ItemFilter filter() const { return m_itemFilter; }
    // rows that are listed whatever the filter says (the group shown and the groups it is in)
    void setKeep(const QSet<BinderItem *> &keep) { m_keep = keep; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QSet<BinderItem *> m_roots;
    QSet<BinderItem *> m_ancestors;
    QString m_filter;
    ItemFilter m_itemFilter;
    QPointer<Project> m_filterProject;
    QSet<BinderItem *> m_keep;
};

class Outliner : public QTreeView
{
    Q_OBJECT
public:
    explicit Outliner(QWidget *parent = nullptr);
    void setProject(Project *p);
    void setContent(const QList<BinderItem *> &containers, const QList<BinderItem *> &loose);
    QList<BinderItem *> selectedItems() const;
    void selectItems(const QList<BinderItem *> &items);
    void setShowSynopsis(bool on);
    bool showSynopsis() const { return m_showSynopsis; }
    void setFilterText(const QString &t);
    // Filter bar: only matching documents (and the groups they are in) are listed.
    void setFilter(const ItemFilter &f);
    ItemFilter filter() const;
    // documents listed / documents there are (without the filter)
    void filterCounts(int *shown, int *total) const;
    void showColumnMenu(const QPoint &globalPos);
    void fillColumnMenu(QMenu *menu, bool withSynopsis = true);   // columns and "Show Synopses" as checkable entries
    void saveColumns();
    // File > Print Current Document: the rows in the order shown, with their depth
    // below the outliner's top (the rows inside closed groups only if `all`),
    // and the columns shown, in their order: (model column, width in pixels).
    QList<QPair<BinderItem *, int>> listedRows(bool all) const;
    QList<QPair<int, int>> shownColumns() const;

signals:
    void selectionChangedByUser();
    void contextMenuRequested(const QPoint &globalPos);
    void openRequested(qb::BinderItem *item);
    void deleteRequested();
    void spacePressed();
    void newItemRequested();   // Return on a row (File > Options > Behaviors > Return Key, 0.21)

protected:
    void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) override;
    void keyPressEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    BinderItem *itemAt(const QPoint &viewportPos) const;
    void restoreColumns();
    void fitTitleColumn();   // titles keep a readable width in a narrow editor
    void onHeaderClicked(int section);

    QPointer<Project> m_project;
    QList<BinderItem *> m_contentContainers, m_contentLoose;
    OutlinerProxy *m_proxy = nullptr;
    bool m_showSynopsis = true;
    int m_sortColumn = -1;
    int m_sortStep = 0;
};

} // namespace qb
