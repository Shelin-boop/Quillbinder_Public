#pragma once
// The list of bookmarks used by the inspector and by the bookmarks sidebar of
// Quick Reference panels. Documents, files and web links can be dropped into
// it, its rows can be dragged into another order, and a bookmarked document
// can be dragged out of it like the document itself.
#include "core/Project.h"

#include <QListWidget>
#include <QPointer>
#include <functional>

namespace qb {

class BookmarkListWidget : public QListWidget
{
    Q_OBJECT
public:
    explicit BookmarkListWidget(QWidget *parent = nullptr);
    // source returns the list shown (project bookmarks or those of a document), or null for none
    void setSource(Project *project, std::function<QList<Bookmark> *()> source);
    void refresh();

    Bookmark bookmarkAt(int row) const;
    BinderItem *documentAt(int row) const; // null for files and web links
    // The changes go into the source list, tell the project and refresh.
    int addDocuments(const QList<BinderItem *> &items, int row = -1); // returns how many were new
    void addLink(const QString &title, const QString &target, int row = -1);
    void removeBookmark(int row);
    void moveBookmark(int from, int to); // to: row before which it is put (as when dropping)
    // Opens a file or web link with the desktop; documents are for the owner of the list.
    static void openExternally(const Bookmark &b);

signals:
    void changed();

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void paintEvent(QPaintEvent *e) override;

private:
    QList<Bookmark> *list() const { return m_source ? m_source() : nullptr; }
    int rowForDrop(const QPoint &pos) const;
    bool takes(const QMimeData *md) const;
    void commit(int selectRow);

    QPointer<Project> m_project;
    std::function<QList<Bookmark> *()> m_source;
    int m_dropRow = -1;
};

} // namespace qb
