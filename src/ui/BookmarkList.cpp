#include "ui/BookmarkList.h"
#include "core/BinderModel.h"
#include "core/ItemIcons.h"
#include "ui/DragDrop.h"

#include <QDesktopServices>
#include <QDrag>
#include <QDragMoveEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QPainter>
#include <QUrl>

namespace qb {

static const QString kRowMime = QStringLiteral("application/x-quillbinder-bookmark-row");

BookmarkListWidget::BookmarkListWidget(QWidget *parent) : QListWidget(parent)
{
    setAcceptDrops(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDropIndicatorShown(false); // painted here: between rows only
    setSelectionMode(QAbstractItemView::SingleSelection);
    setIconSize(QSize(16, 16));
}

void BookmarkListWidget::setSource(Project *project, std::function<QList<Bookmark> *()> source)
{
    m_project = project;
    m_source = std::move(source);
    refresh();
}

void BookmarkListWidget::refresh()
{
    const int row = currentRow();
    QSignalBlocker block(this);
    clear();
    const QList<Bookmark> *bms = list();
    if (!bms || !m_project)
        return;
    for (const Bookmark &b : *bms) {
        QIcon ic;
        QString title = b.title;
        if (b.isInternal()) {
            if (BinderItem *it = m_project->item(b.internalId())) {
                ic = icons::forItem(m_project, it);
                title = it->title.isEmpty() ? tr("Untitled") : it->title;
            } else {
                ic = icons::action(QStringLiteral("dialog-warning"));
            }
        } else if (b.target.startsWith(QLatin1String("file:"))) {
            ic = icons::action(QStringLiteral("document-open"));
        } else {
            ic = icons::action(QStringLiteral("globe"));
        }
        auto *li = new QListWidgetItem(ic, title);
        li->setToolTip(b.isInternal() ? title : b.target);
        addItem(li);
    }
    if (row >= 0 && row < count())
        setCurrentRow(row);
}

Bookmark BookmarkListWidget::bookmarkAt(int row) const
{
    const QList<Bookmark> *bms = list();
    return bms && row >= 0 && row < bms->size() ? bms->at(row) : Bookmark();
}

BinderItem *BookmarkListWidget::documentAt(int row) const
{
    const Bookmark b = bookmarkAt(row);
    return m_project && b.isInternal() ? m_project->item(b.internalId()) : nullptr;
}

void BookmarkListWidget::commit(int selectRow)
{
    if (m_project)
        m_project->notifyBookmarksChanged(); // every list of this project refreshes, this one too
    refresh();
    if (selectRow >= 0 && selectRow < count()) {
        QSignalBlocker block(this);
        setCurrentRow(selectRow);
    }
    emit changed();
}

int BookmarkListWidget::addDocuments(const QList<BinderItem *> &items, int row)
{
    QList<Bookmark> *bms = list();
    if (!bms)
        return 0;
    int at = row < 0 ? int(bms->size()) : qMin(row, int(bms->size()));
    int added = 0;
    for (BinderItem *it : items) {
        const QString target = QStringLiteral("qb://") + it->id;
        bool exists = false;
        for (const Bookmark &b : *bms)
            exists = exists || b.target == target;
        if (exists)
            continue;
        bms->insert(at++, {it->title, target});
        ++added;
    }
    if (added)
        commit(at - 1);
    return added;
}

void BookmarkListWidget::addLink(const QString &title, const QString &target, int row)
{
    QList<Bookmark> *bms = list();
    if (!bms || target.isEmpty())
        return;
    const int at = row < 0 ? int(bms->size()) : qMin(row, int(bms->size()));
    bms->insert(at, {title.isEmpty() ? target : title, target});
    commit(at);
}

void BookmarkListWidget::removeBookmark(int row)
{
    QList<Bookmark> *bms = list();
    if (!bms || row < 0 || row >= bms->size())
        return;
    bms->removeAt(row);
    commit(qMin(row, int(bms->size()) - 1));
}

void BookmarkListWidget::moveBookmark(int from, int to)
{
    QList<Bookmark> *bms = list();
    if (!bms || from < 0 || from >= bms->size())
        return;
    to = qBound(0, to, int(bms->size()));
    if (to == from || to == from + 1)
        return; // dropped where it was
    const Bookmark b = bms->takeAt(from);
    if (to > from)
        --to;
    bms->insert(to, b);
    commit(to);
}

void BookmarkListWidget::openExternally(const Bookmark &b)
{
    if (!b.isInternal() && !b.target.isEmpty())
        QDesktopServices::openUrl(QUrl::fromUserInput(b.target));
}

// ------------------------------------------------------------------ drag and drop

void BookmarkListWidget::startDrag(Qt::DropActions)
{
    const int row = currentRow();
    const Bookmark b = bookmarkAt(row);
    if (b.target.isEmpty() || !m_project)
        return;
    QMimeData *md = nullptr;
    auto *drag = new QDrag(this);
    if (BinderItem *it = documentAt(row)) {
        md = BinderModel::mimeForItems({it}, m_project);
        drag->setPixmap(dnd::dragPixmap(m_project, {it}));
    } else {
        md = new QMimeData;
        md->setUrls({QUrl::fromUserInput(b.target)});
        md->setText(b.target);
    }
    md->setData(kRowMime, QByteArray::number(row));
    drag->setMimeData(md);
    drag->exec(Qt::CopyAction | Qt::MoveAction | Qt::LinkAction, Qt::CopyAction);
}

bool BookmarkListWidget::takes(const QMimeData *md) const
{
    if (!list())
        return false;
    if (md->hasFormat(kRowMime))
        return true;
    if (md->hasFormat(QString::fromLatin1(kItemsMime)))
        return m_project && !BinderModel::itemsFromMime(md, m_project).isEmpty();
    return md->hasUrls() || (md->hasText() && QUrl(md->text().trimmed()).scheme().startsWith(QLatin1String("http")));
}

int BookmarkListWidget::rowForDrop(const QPoint &pos) const
{
    const QModelIndex idx = indexAt(pos);
    if (!idx.isValid())
        return count();
    return pos.y() < visualRect(idx).center().y() ? idx.row() : idx.row() + 1;
}

void BookmarkListWidget::dragEnterEvent(QDragEnterEvent *e)
{
    if (takes(e->mimeData())) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    } else {
        e->ignore();
    }
}

void BookmarkListWidget::dragMoveEvent(QDragMoveEvent *e)
{
    const bool ok = takes(e->mimeData());
    const int row = ok ? rowForDrop(e->position().toPoint()) : -1;
    if (row != m_dropRow) {
        m_dropRow = row;
        viewport()->update();
    }
    if (ok) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    } else {
        e->ignore();
    }
}

void BookmarkListWidget::dragLeaveEvent(QDragLeaveEvent *e)
{
    m_dropRow = -1;
    viewport()->update();
    QListWidget::dragLeaveEvent(e);
}

void BookmarkListWidget::dropEvent(QDropEvent *e)
{
    const QMimeData *md = e->mimeData();
    const int row = rowForDrop(e->position().toPoint());
    m_dropRow = -1;
    viewport()->update();
    if (!takes(md))
        return;
    e->setDropAction(Qt::CopyAction);
    e->accept();
    if (e->source() == this && md->hasFormat(kRowMime)) {
        moveBookmark(md->data(kRowMime).toInt(), row);
    } else if (md->hasFormat(QString::fromLatin1(kItemsMime))) {
        addDocuments(BinderModel::itemsFromMime(md, m_project), row);
    } else if (md->hasUrls()) {
        const QList<QUrl> urls = md->urls();
        int at = row;
        for (const QUrl &u : urls) {
            // a link dragged from a browser brings the page title as text
            QString title = u.isLocalFile() ? QFileInfo(u.toLocalFile()).fileName() : u.toString();
            if (!u.isLocalFile() && urls.size() == 1 && md->hasText()) {
                const QString t = md->text().trimmed();
                if (!t.isEmpty() && !t.contains(QLatin1Char('\n')) && t != u.toString())
                    title = t;
            }
            addLink(title, u.toString(), at++);
        }
    } else if (md->hasText()) {
        addLink(md->text().trimmed(), md->text().trimmed(), row);
    }
}

void BookmarkListWidget::paintEvent(QPaintEvent *e)
{
    QListWidget::paintEvent(e);
    if (m_dropRow < 0)
        return;
    int y = 1;
    if (count() > 0) {
        const QRect r = visualRect(model()->index(qMin(m_dropRow, count() - 1), 0));
        y = m_dropRow < count() ? r.top() : r.bottom() + 1;
    }
    QPainter p(viewport());
    p.setPen(QPen(palette().color(QPalette::Highlight), 2));
    p.drawLine(2, y, viewport()->width() - 3, y);
}

} // namespace qb
