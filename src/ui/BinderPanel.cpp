#include "ui/BinderPanel.h"
#include "ui/Zoom.h"
#include "core/BinderModel.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "core/Search.h"
#include <QActionGroup>
#include <QApplication>
#include "ui/AppSettings.h"
#include "ui/DragDrop.h"

#include <QColorDialog>
#include <QDrag>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace qb {

// ------------------------------------------------------------------ delegate

namespace {
class BinderDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        // the document in the editor, when it is not the selected one
        const auto *tree = qobject_cast<const BinderTree *>(option.widget);
        if (tree && !tree->editorItemId().isEmpty() && !(option.state & QStyle::State_Selected)
            && index.data(BinderModel::IdRole).toString() == tree->editorItemId()) {
            QColor bar = option.palette.color(QPalette::Text);
            bar.setAlpha(34);
            p->save();
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(Qt::NoPen);
            p->setBrush(bar);
            p->drawRoundedRect(QRect(2, option.rect.top() + 1, tree->viewport()->width() - 4, option.rect.height() - 2), 3, 3);
            p->restore();
        }
        QStyledItemDelegate::paint(p, option, index);
        if (!AppSettings::instance().labelColorInBinder)
            return;
        const QColor c = index.data(BinderModel::LabelColorRole).value<QColor>();
        if (!c.isValid())
            return;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const int d = qMin(10, option.rect.height() - 8);
        // at the right edge of what can be seen (the column can be wider than
        // the view since 0.17; scrolled fully to the right the title ends before it)
        int right = option.rect.right();
        if (tree)
            right = qMin(right, tree->viewport()->width() - 1);
        const QRect r(right - d - 6, option.rect.center().y() - d / 2, d, d);
        p->setPen(c.darker(130));
        p->setBrush(c);
        p->drawEllipse(r);
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize sz = QStyledItemDelegate::sizeHint(option, index);
        sz.rwidth() += 24; // room for the label dot
        return sz;
    }
};
} // namespace

// ------------------------------------------------------------------ tree

BinderTree::BinderTree(QWidget *parent) : QTreeView(parent)
{
    setHeaderHidden(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    setAnimated(true);
    setUniformRowHeights(true);
    setIconSize(QSize(18, 18));
    setContextMenuPolicy(Qt::CustomContextMenu);
    setItemDelegate(new BinderDelegate(this));
    setFrameShape(QFrame::NoFrame);
    setExpandsOnDoubleClick(false);
    // narrower steps than Qt's 20 px (0.17: more of the titles to be seen)
    setIndentation(AppSettings::instance().binderIndent);
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this] {
        if (indentation() != AppSettings::instance().binderIndent) {
            setIndentation(AppSettings::instance().binderIndent);
            scheduleWidthUpdate();
        }
    });
    // long titles: a horizontal scroll bar instead of cut-off titles (0.17)
    header()->setStretchLastSection(false);
    header()->setMinimumSectionSize(20);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_widthTimer = new QTimer(this);
    m_widthTimer->setSingleShot(true);
    m_widthTimer->setInterval(0);
    connect(m_widthTimer, &QTimer::timeout, this, &BinderTree::updateColumnWidth);
    connect(this, &QTreeView::expanded, this, &BinderTree::scheduleWidthUpdate);
    connect(this, &QTreeView::collapsed, this, &BinderTree::scheduleWidthUpdate);
}

void BinderTree::setModel(QAbstractItemModel *m)
{
    if (model())
        disconnect(model(), nullptr, this, nullptr);
    QTreeView::setModel(m);
    if (m) {
        connect(m, &QAbstractItemModel::modelReset, this, &BinderTree::scheduleWidthUpdate);
        connect(m, &QAbstractItemModel::layoutChanged, this, &BinderTree::scheduleWidthUpdate);
        connect(m, &QAbstractItemModel::rowsInserted, this, &BinderTree::scheduleWidthUpdate);
        connect(m, &QAbstractItemModel::rowsRemoved, this, &BinderTree::scheduleWidthUpdate);
        connect(m, &QAbstractItemModel::rowsMoved, this, &BinderTree::scheduleWidthUpdate);
        connect(m, &QAbstractItemModel::dataChanged, this, &BinderTree::scheduleWidthUpdate);
    }
    scheduleWidthUpdate();
}

void BinderTree::scheduleWidthUpdate()
{
    if (m_widthTimer)
        m_widthTimer->start();
}

int BinderTree::contentWidth() const
{
    if (!model())
        return 0;
    return sizeHintForColumn(0);
}

void BinderTree::updateColumnWidth()
{
    if (!model() || model()->columnCount() == 0)
        return;
    const int w = qMax(viewport()->width(), contentWidth());
    if (header()->sectionSize(0) != w)
        header()->resizeSection(0, w);
}

void BinderTree::resizeEvent(QResizeEvent *e)
{
    QTreeView::resizeEvent(e);
    updateColumnWidth();
}

void BinderTree::keyPressEvent(QKeyEvent *e)
{
    if (state() != QAbstractItemView::EditingState) {
        if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && e->modifiers() == Qt::NoModifier) {
            emit enterPressed();
            return;
        }
        if (e->key() == Qt::Key_Delete) {
            emit deletePressed();
            return;
        }
        if (e->key() == Qt::Key_Space && e->modifiers() == Qt::NoModifier) {
            emit spacePressed();
            return;
        }
    }
    QTreeView::keyPressEvent(e);
}

void BinderTree::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    QTreeView::selectionChanged(selected, deselected);
    if (m_mouseDown && (QGuiApplication::mouseButtons() & Qt::LeftButton)) {
        m_selectionPending = true; // told when the button is released (see the header)
        return;
    }
    m_mouseDown = false; // (a release that never arrived must not hold later changes back)
    emit selectionChangedByUser();
}

void BinderTree::setEditorItemId(const QString &id)
{
    if (id == m_editorItemId)
        return;
    m_editorItemId = id;
    viewport()->update();
}

void BinderTree::mousePressEvent(QMouseEvent *e)
{
    // Alt+click on the arrow of a group opens (or closes) everything below it
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::AltModifier)) {
        const QModelIndex idx = indexAt(e->pos());
        if (idx.isValid() && model()->hasChildren(idx) && e->pos().x() < visualRect(idx).left()) {
            if (isExpanded(idx)) {
                std::function<void(const QModelIndex &)> closeAll = [&](const QModelIndex &i) {
                    for (int r = 0; r < model()->rowCount(i); ++r)
                        closeAll(model()->index(r, 0, i));
                    collapse(i);
                };
                closeAll(idx);
            } else {
                expandRecursively(idx);
            }
            return;
        }
    }
    if (e->button() == Qt::LeftButton)
        m_mouseDown = true;
    QTreeView::mousePressEvent(e);
}

void BinderTree::mouseReleaseEvent(QMouseEvent *e)
{
    QTreeView::mouseReleaseEvent(e);
    if (e->button() != Qt::LeftButton)
        return;
    m_mouseDown = false;
    if (m_selectionPending) {
        m_selectionPending = false;
        emit selectionChangedByUser();
        return;
    }
    // A click on the document that is already selected while the editor shows
    // another one (after Back / Forward, a search result, a link): load it.
    if (e->modifiers() != Qt::NoModifier || m_editorItemId.isEmpty() || !selectionModel())
        return;
    auto *bm = qobject_cast<BinderModel *>(model());
    const QModelIndex idx = indexAt(e->pos());
    const QModelIndexList rows = selectionModel()->selectedRows();
    if (!bm || !idx.isValid() || rows.size() != 1 || rows.first() != idx.siblingAtColumn(0) || e->pos().x() < visualRect(idx).left())
        return;
    const BinderItem *it = bm->itemFor(rows.first());
    if (it && it->id != m_editorItemId)
        emit selectionChangedByUser();
}

static BinderItem *treeItemAt(const QTreeView *view, const QPoint &pos)
{
    auto *model = qobject_cast<BinderModel *>(view->model());
    const QModelIndex idx = view->indexAt(pos);
    return model && idx.isValid() ? model->itemFor(idx.siblingAtColumn(0)) : nullptr;
}

static QList<BinderItem *> treeSelection(const QTreeView *view)
{
    QList<BinderItem *> out;
    auto *model = qobject_cast<BinderModel *>(view->model());
    if (model && view->selectionModel())
        for (const QModelIndex &i : view->selectionModel()->selectedRows())
            if (BinderItem *it = model->itemFor(i))
                out << it;
    return out;
}

void BinderTree::dropEvent(QDropEvent *e)
{
    const QMimeData *md = e->mimeData();
    if (md->hasFormat(QString::fromLatin1(kKeywordsMime))) {
        // keywords from the Project Keywords window: for the document under the mouse,
        // or for all selected ones if it is one of them
        if (auto *model = qobject_cast<BinderModel *>(this->model()))
            dnd::dropKeywords(model->project(), treeItemAt(this, e->position().toPoint()), treeSelection(this), md,
                              e->modifiers());
        e->setDropAction(Qt::CopyAction);
        e->accept();
        return;
    }
    m_droppedHere = true;
    if (!md->hasFormat(QString::fromLatin1(kItemsMime)))
        e->setDropAction(Qt::CopyAction); // text and files are copied in, never taken away from where they came
    QTreeView::dropEvent(e);
}

void BinderTree::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasFormat(QString::fromLatin1(kKeywordsMime))) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
        return;
    }
    QTreeView::dragEnterEvent(e);
}

void BinderTree::dragMoveEvent(QDragMoveEvent *e)
{
    const QMimeData *md = e->mimeData();
    if (md->hasFormat(QString::fromLatin1(kKeywordsMime))) {
        if (treeItemAt(this, e->position().toPoint())) {
            e->setDropAction(Qt::CopyAction);
            e->accept();
        } else {
            e->ignore();
        }
        return;
    }
    QTreeView::dragMoveEvent(e);
    if (md->hasFormat(QString::fromLatin1(kItemsMime)))
        return; // documents of the project: the model has decided
    // files from the file manager, text from an editor or another program
    if (md->hasUrls() && !e->isAccepted())
        e->accept();
    if (e->isAccepted())
        e->setDropAction(Qt::CopyAction);
}

void BinderTree::startDrag(Qt::DropActions supportedActions)
{
    m_droppedHere = false;
    QTreeView::startDrag(supportedActions | Qt::CopyAction); // returns when the drag is over
    m_mouseDown = false;
    if (m_selectionPending) {
        m_selectionPending = false;
        // moved within the binder: the document is shown as after a click;
        // dragged somewhere else: that place decides what happens with it
        if (m_droppedHere)
            emit selectionChangedByUser();
    }
}

void BinderTree::mouseDoubleClickEvent(QMouseEvent *e)
{
    // double click renames
    const QModelIndex idx = indexAt(e->pos());
    if (idx.isValid()) {
        edit(idx);
        return;
    }
    QTreeView::mouseDoubleClickEvent(e);
}

// ------------------------------------------------------------------ list of documents

ItemListWidget::ItemListWidget(QWidget *parent) : QListWidget(parent)
{
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDropIndicatorShown(false); // painted here: only between rows
}

QList<BinderItem *> ItemListWidget::selectedBinderItems() const
{
    QList<BinderItem *> out;
    if (!m_project)
        return out;
    for (int r = 0; r < count(); ++r) // list order, not the order of selecting
        if (item(r)->isSelected())
            if (BinderItem *it = m_project->item(item(r)->data(Qt::UserRole).toString()))
                out << it;
    return out;
}

BinderItem *ItemListWidget::binderItemAt(const QPoint &pos) const
{
    QListWidgetItem *li = itemAt(pos);
    return li && m_project ? m_project->item(li->data(Qt::UserRole).toString()) : nullptr;
}

QStringList ItemListWidget::mimeTypes() const { return {QString::fromLatin1(kItemsMime)}; }

QMimeData *ItemListWidget::mimeData(const QList<QListWidgetItem *> &items) const
{
    QList<BinderItem *> docs;
    if (m_project)
        for (QListWidgetItem *li : items)
            if (BinderItem *it = m_project->item(li->data(Qt::UserRole).toString()))
                docs << it;
    return m_project && !docs.isEmpty() ? BinderModel::mimeForItems(docs, m_project) : nullptr;
}

void ItemListWidget::startDrag(Qt::DropActions)
{
    // a drag of our own: the view's would take the rows out of the list after a "move"
    const QList<BinderItem *> docs = selectedBinderItems();
    if (!m_project || docs.isEmpty())
        return;
    auto *drag = new QDrag(this);
    drag->setMimeData(BinderModel::mimeForItems(docs, m_project));
    drag->setPixmap(dnd::dragPixmap(m_project, docs));
    drag->exec(Qt::MoveAction | Qt::CopyAction | Qt::LinkAction, Qt::MoveAction);
}

int ItemListWidget::rowForDrop(const QPoint &pos) const
{
    const QModelIndex idx = indexAt(pos);
    if (!idx.isValid())
        return count();
    return pos.y() < visualRect(idx).center().y() ? idx.row() : idx.row() + 1;
}

void ItemListWidget::dragEnterEvent(QDragEnterEvent *e)
{
    const QMimeData *md = e->mimeData();
    if (md->hasFormat(QString::fromLatin1(kKeywordsMime))
        || (m_reorderable && md->hasFormat(QString::fromLatin1(kItemsMime)))) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    } else {
        e->ignore();
    }
}

void ItemListWidget::dragMoveEvent(QDragMoveEvent *e)
{
    const QMimeData *md = e->mimeData();
    const QPoint pos = e->position().toPoint();
    int row = -1;
    bool ok = false;
    if (md->hasFormat(QString::fromLatin1(kKeywordsMime))) {
        ok = binderItemAt(pos) != nullptr;
    } else if (m_reorderable && md->hasFormat(QString::fromLatin1(kItemsMime))) {
        ok = true;
        row = rowForDrop(pos);
    }
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

void ItemListWidget::dragLeaveEvent(QDragLeaveEvent *e)
{
    m_dropRow = -1;
    viewport()->update();
    QListWidget::dragLeaveEvent(e);
}

void ItemListWidget::dropEvent(QDropEvent *e)
{
    const QMimeData *md = e->mimeData();
    const QPoint pos = e->position().toPoint();
    m_dropRow = -1;
    viewport()->update();
    if (!m_project)
        return;
    if (md->hasFormat(QString::fromLatin1(kKeywordsMime))) {
        dnd::dropKeywords(m_project, binderItemAt(pos), selectedBinderItems(), md, e->modifiers());
    } else if (m_reorderable && md->hasFormat(QString::fromLatin1(kItemsMime))) {
        const QList<BinderItem *> docs = BinderModel::itemsFromMime(md, m_project);
        if (!docs.isEmpty())
            emit itemsDropped(docs, rowForDrop(pos), e->source() == this);
    } else {
        return;
    }
    e->setDropAction(Qt::CopyAction);
    e->accept();
}

void ItemListWidget::keyPressEvent(QKeyEvent *e)
{
    if (e->modifiers() == Qt::NoModifier) {
        if (e->key() == Qt::Key_Space) {
            emit spacePressed();
            return;
        }
        if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
            emit deletePressed();
            return;
        }
    }
    QListWidget::keyPressEvent(e);
}

void ItemListWidget::paintEvent(QPaintEvent *e)
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

// ------------------------------------------------------------------ collection tabs

CollectionTabs::CollectionTabs(QWidget *parent) : QTabBar(parent)
{
    setAcceptDrops(true);
    setMovable(true);
}

void CollectionTabs::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasFormat(QString::fromLatin1(kItemsMime))) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    } else {
        e->ignore();
    }
}

void CollectionTabs::dragMoveEvent(QDragMoveEvent *e)
{
    const int tab = tabAt(e->position().toPoint());
    if (tab >= 0 && e->mimeData()->hasFormat(QString::fromLatin1(kItemsMime)) && takesDocuments && takesDocuments(tab)) {
        e->setDropAction(Qt::CopyAction); // the documents stay in the binder
        e->accept(tabRect(tab));
    } else {
        e->ignore();
    }
}

void CollectionTabs::dropEvent(QDropEvent *e)
{
    const int tab = tabAt(e->position().toPoint());
    if (tab < 0 || !takesDocuments || !takesDocuments(tab))
        return;
    emit documentsDropped(tab, e->mimeData());
    e->setDropAction(Qt::CopyAction);
    e->accept();
}

// ------------------------------------------------------------------ panel

BinderPanel::BinderPanel(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // collections tab bar
    m_collectionsBar = new QWidget(this);
    auto *cl = new QHBoxLayout(m_collectionsBar);
    cl->setContentsMargins(2, 2, 2, 2);
    m_tabs = new CollectionTabs(m_collectionsBar);
    m_tabs->setObjectName(QStringLiteral("collectionTabs"));
    m_tabs->setShape(QTabBar::RoundedNorth);
    m_tabs->setExpanding(false);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setDrawBase(false);
    m_tabs->setContextMenuPolicy(Qt::CustomContextMenu);
    cl->addWidget(m_tabs, 1);
    auto *addCol = new QToolButton(m_collectionsBar);
    addCol->setIcon(icons::action(QStringLiteral("list-add")));
    addCol->setToolTip(tr("New collection from the selected documents"));
    addCol->setAutoRaise(true);
    cl->addWidget(addCol);
    auto *delCol = new QToolButton(m_collectionsBar);
    delCol->setIcon(icons::action(QStringLiteral("list-remove")));
    delCol->setToolTip(tr("Delete the current collection"));
    delCol->setAutoRaise(true);
    cl->addWidget(delCol);
    lay->addWidget(m_collectionsBar);
    m_collectionsBar->hide();

    // search bar
    m_searchBar = new QWidget(this);
    auto *sl = new QHBoxLayout(m_searchBar);
    sl->setContentsMargins(4, 4, 4, 4);
    m_searchOptions = new QToolButton(m_searchBar);
    m_searchOptions->setIcon(icons::action(QStringLiteral("search")));
    m_searchOptions->setPopupMode(QToolButton::InstantPopup);
    m_searchOptions->setAutoRaise(true);
    m_searchOptions->setToolTip(tr("Search options"));
    sl->addWidget(m_searchOptions);
    m_searchEdit = new QLineEdit(m_searchBar);
    m_searchEdit->setPlaceholderText(tr("Search project…"));
    m_searchEdit->setClearButtonEnabled(true);
    sl->addWidget(m_searchEdit, 1);
    lay->addWidget(m_searchBar);
    m_searchBar->hide();

    m_stack = new QStackedWidget(this);
    m_tree = new BinderTree(m_stack);
    m_stack->addWidget(m_tree);

    m_resultsPage = new QWidget(m_stack);
    auto *rl = new QVBoxLayout(m_resultsPage);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);
    auto *rh = new QWidget(m_resultsPage);
    rh->setAutoFillBackground(true);
    auto *rhl = new QHBoxLayout(rh);
    rhl->setContentsMargins(4, 2, 4, 2);
    auto *closeRes = new QToolButton(rh);
    closeRes->setIcon(icons::action(QStringLiteral("window-close")));
    closeRes->setAutoRaise(true);
    closeRes->setToolTip(tr("Back to the binder"));
    rhl->addWidget(closeRes);
    m_resultsTitle = new QLabel(rh);
    QFont bf = m_resultsTitle->font();
    bf.setBold(true);
    m_resultsTitle->setFont(bf);
    rhl->addWidget(m_resultsTitle, 1);
    rl->addWidget(rh);
    m_results = new ItemListWidget(m_resultsPage);
    m_results->setObjectName(QStringLiteral("binderResults"));
    m_results->setFrameShape(QFrame::NoFrame);
    m_results->setIconSize(QSize(18, 18));
    m_results->setContextMenuPolicy(Qt::CustomContextMenu);
    rl->addWidget(m_results, 1);
    m_stack->addWidget(m_resultsPage);
    auto themed = [this] {
        theme::styleView(m_tree, theme::Role::BinderBackground, theme::Role::BinderText, theme::FontRole::Binder);
        theme::styleView(m_results, theme::Role::BinderBackground, theme::Role::BinderText, theme::FontRole::Binder);
        if (m_project)
            refreshCollections(); // tab text colors depend on the window color
    };
    themed();
    connect(&theme::Notifier::instance(), &theme::Notifier::changed, this, themed);
    lay->addWidget(m_stack, 1);
    // Ctrl + mouse wheel (0.21): the binder's text and icons grow and shrink together
    zoom::mark(m_stack, QLatin1String(zoom::kBinder));
    zoom::attachFonts(m_stack, QLatin1String(zoom::kBinder));
    auto icons = [this] {
        const int side = qRound(18 * zoom::factor(QLatin1String(zoom::kBinder)));
        m_tree->setIconSize(QSize(side, side));
        m_results->setIconSize(QSize(side, side));
    };
    icons();
    connect(&zoom::notifier(), &zoom::Notifier::changed, this, [icons](const QString &key, double) {
        if (key == QLatin1String(zoom::kBinder))
            icons();
    });

    // footer
    auto *footer = new QWidget(this);
    auto *fl = new QHBoxLayout(footer);
    fl->setContentsMargins(2, 2, 2, 2);
    m_addButton = new QToolButton(footer);
    m_addButton->setIcon(icons::action(QStringLiteral("document-new")));
    m_addButton->setToolTip(tr("New document (Ctrl+N)"));
    m_addButton->setAutoRaise(true);
    m_addButton->setPopupMode(QToolButton::MenuButtonPopup);
    fl->addWidget(m_addButton);
    m_folderButton = new QToolButton(footer);
    m_folderButton->setIcon(icons::action(QStringLiteral("folder-new")));
    m_folderButton->setToolTip(tr("New folder (Alt+Shift+N)"));
    m_folderButton->setAutoRaise(true);
    fl->addWidget(m_folderButton);
    fl->addStretch(1);
    m_gearButton = new QToolButton(footer);
    m_gearButton->setIcon(icons::action(QStringLiteral("configure")));
    m_gearButton->setToolTip(tr("More actions"));
    m_gearButton->setAutoRaise(true);
    m_gearButton->setPopupMode(QToolButton::InstantPopup);
    fl->addWidget(m_gearButton);
    lay->addWidget(footer);

    connect(m_addButton, &QToolButton::clicked, this, &BinderPanel::newDocumentRequested);
    connect(m_folderButton, &QToolButton::clicked, this, &BinderPanel::newFolderRequested);
    connect(m_tree, &BinderTree::enterPressed, this, &BinderPanel::newDocumentRequested);
    connect(m_tree, &BinderTree::deletePressed, this, &BinderPanel::trashRequested);
    connect(m_tree, &BinderTree::spacePressed, this, &BinderPanel::quickReferenceRequested);
    connect(m_tree, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &p) { emit contextMenuRequested(m_tree->viewport()->mapToGlobal(p)); });
    connect(m_results, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &p) { emit contextMenuRequested(m_results->viewport()->mapToGlobal(p)); });
    connect(m_tree, &BinderTree::selectionChangedByUser, this, [this] {
        if (m_stack->currentWidget() == m_tree)
            emit selectionChanged(selectedItems());
    });
    connect(m_results, &QListWidget::itemSelectionChanged, this, &BinderPanel::onResultSelection);
    connect(m_results, &ItemListWidget::spacePressed, this, &BinderPanel::quickReferenceRequested);
    connect(m_results, &ItemListWidget::deletePressed, this, [this] {
        if (showsArbitraryCollection())
            removeFromCollection(selectedItems());
    });
    // documents dropped into the list of an arbitrary collection: added at that
    // place, or moved there if they are in the collection already
    connect(m_results, &ItemListWidget::itemsDropped, this, [this](const QList<BinderItem *> &items, int row, bool) {
        if (showsArbitraryCollection())
            addToCollection(m_shownCollection, items, row);
    });
    m_tabs->takesDocuments = [this](int tab) {
        if (!m_project)
            return false;
        const QString id = m_tabs->tabData(tab).toString();
        for (const Collection &c : m_project->collections)
            if (c.id == id)
                return !c.isSearch;
        return false;
    };
    connect(m_tabs, &CollectionTabs::documentsDropped, this, [this](int tab, const QMimeData *data) {
        if (m_project)
            addToCollection(m_tabs->tabData(tab).toString(), BinderModel::itemsFromMime(data, m_project));
    });
    connect(m_tabs, &QTabBar::tabMoved, this, [this](int from, int to) {
        if (m_updatingTabs || !m_project)
            return;
        const int n = int(m_project->collections.size());
        if (from >= 1 && to >= 1 && from <= n && to <= n) {
            m_project->collections.move(from - 1, to - 1);
            m_project->notifySettingsChanged();
        } else {
            // "Binder" stays the first tab
            QTimer::singleShot(0, this, [this] { refreshCollections(); });
        }
    });
    connect(closeRes, &QToolButton::clicked, this, [this] {
        if (!m_shownCollection.isEmpty() && m_shownCollection != QLatin1String("__search")) {
            m_tabs->setCurrentIndex(0);
        } else {
            // back to the binder; the search field stays until the search button hides it
            m_shownCollection.clear();
            m_stack->setCurrentWidget(m_tree);
            selectSearchTab(false);
            emit searchChanged(); // (the editor keeps the document it shows)
        }
    });
    closeRes->setObjectName(QStringLiteral("closeResults"));
    auto *searchTimer = new QTimer(this);
    searchTimer->setSingleShot(true);
    searchTimer->setInterval(300);
    connect(m_searchEdit, &QLineEdit::textChanged, searchTimer, qOverload<>(&QTimer::start));
    connect(searchTimer, &QTimer::timeout, this, &BinderPanel::onSearchTextChanged);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &BinderPanel::onSearchTextChanged);
    connect(m_tabs, &QTabBar::currentChanged, this, &BinderPanel::onTabChanged);
    connect(addCol, &QToolButton::clicked, this, [this] {
        if (!m_project)
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("New Collection"), tr("Name:"), QLineEdit::Normal,
                                                   tr("New Collection"), &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
        Collection c;
        c.id = newId();
        c.name = name.trimmed();
        c.color = QColor::fromHsv(int(m_project->collections.size() * 57) % 360, 90, 230);
        for (BinderItem *it : selectedItems())
            c.items << it->id;
        m_project->collections << c;
        m_project->notifySettingsChanged();
        refreshCollections();
        showCollection(c.id);
    });
    connect(delCol, &QToolButton::clicked, this, [this] {
        const QString id = currentCollectionId();
        if (!m_project || id.isEmpty() || id == QLatin1String("__search"))
            return;
        if (QMessageBox::question(this, tr("Delete Collection"),
                                  tr("Delete this collection? The documents themselves stay in the binder."))
            != QMessageBox::Yes)
            return;
        for (int i = 0; i < m_project->collections.size(); ++i)
            if (m_project->collections[i].id == id)
                m_project->collections.removeAt(i--);
        m_project->notifySettingsChanged();
        refreshCollections();
        m_tabs->setCurrentIndex(0);
    });
    connect(m_tabs, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const int idx = m_tabs->tabAt(pos);
        const QString id = m_tabs->tabData(idx).toString();
        if (!m_project || id.isEmpty() || id == QLatin1String("__search"))
            return;
        QMenu menu;
        menu.addAction(tr("Rename…"), this, [this, id] {
            for (Collection &c : m_project->collections)
                if (c.id == id) {
                    bool ok = false;
                    const QString n = QInputDialog::getText(this, tr("Rename Collection"), tr("Name:"),
                                                            QLineEdit::Normal, c.name, &ok);
                    if (ok && !n.trimmed().isEmpty()) {
                        c.name = n.trimmed();
                        m_project->notifySettingsChanged();
                        refreshCollections();
                    }
                }
        });
        menu.addAction(tr("Color…"), this, [this, id] {
            for (Collection &c : m_project->collections)
                if (c.id == id) {
                    const QColor col = QColorDialog::getColor(c.color, this);
                    if (col.isValid()) {
                        c.color = col;
                        m_project->notifySettingsChanged();
                        refreshCollections();
                    }
                }
        });
        menu.exec(m_tabs->mapToGlobal(pos));
    });
    buildSearchMenu();
}

void BinderPanel::buildSearchMenu()
{
    auto *menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("searchOptionsMenu"));
    // filled when it opens: it shows the options of the current search
    connect(menu, &QMenu::aboutToShow, this, [this, menu] { fillSearchMenu(menu); });
    m_searchOptions->setMenu(menu);
}

void BinderPanel::chooseSearchIn(bool SearchQuery::*member, bool multi)
{
    if (!member) {
        m_query.searchEverywhere();
    } else if (multi) {
        m_query.*member = !(m_query.*member);
        if (!(m_query.inTitle || m_query.inText || m_query.inSynopsis || m_query.inNotes || m_query.inKeywords
              || m_query.inLabel || m_query.inStatus || m_query.inCustom))
            m_query.*member = true; // at least one place
    } else {
        m_query.searchOnlyIn(member);
    }
    onSearchTextChanged();
}

void BinderPanel::fillSearchMenu(QMenu *menu)
{
    menu->clear();
    const bool multi = QApplication::keyboardModifiers() & Qt::AltModifier;
    struct Opt {
        QString label;
        bool SearchQuery::*member;
    };
    const QString labelTitle = m_project ? m_project->labelTitle : tr("Label");
    const QString statusTitle = m_project ? m_project->statusTitle : tr("Status");
    const QList<Opt> where = {{tr("Title"), &SearchQuery::inTitle},       {tr("Text"), &SearchQuery::inText},
                              {tr("Synopsis"), &SearchQuery::inSynopsis}, {tr("Notes"), &SearchQuery::inNotes},
                              {tr("Keywords"), &SearchQuery::inKeywords}, {labelTitle, &SearchQuery::inLabel},
                              {statusTitle, &SearchQuery::inStatus},      {tr("Custom Metadata"), &SearchQuery::inCustom}};
    menu->addSection(multi ? tr("Search In (several)") : tr("Search In"));
    const bool everywhere = m_query.searchesEverywhere();
    QAction *all = menu->addAction(tr("All"));
    all->setCheckable(true);
    all->setChecked(everywhere);
    connect(all, &QAction::triggered, this, [this] { chooseSearchIn(nullptr, false); });
    for (const Opt &o : where) {
        QAction *a = menu->addAction(o.label);
        a->setCheckable(true);
        a->setChecked(!everywhere && m_query.*(o.member));
        auto member = o.member;
        connect(a, &QAction::triggered, this, [this, member, multi] {
            chooseSearchIn(member, multi || (QApplication::keyboardModifiers() & Qt::AltModifier));
        });
    }
    menu->addSection(tr("Operator"));
    auto *ops = new QActionGroup(menu);
    auto op = [&](const QString &text, bool checked, std::function<void(SearchQuery &)> set) {
        QAction *a = menu->addAction(text);
        a->setCheckable(true);
        a->setChecked(checked);
        ops->addAction(a);
        connect(a, &QAction::triggered, this, [this, set] {
            m_query.exactPhrase = m_query.anyWord = m_query.regex = false;
            m_query.allWords = true;
            set(m_query);
            onSearchTextChanged();
        });
    };
    op(tr("Exact Phrase"), m_query.exactPhrase && !m_query.regex, [](SearchQuery &q) {
        q.exactPhrase = true;
        q.allWords = false;
    });
    op(tr("All Words"), !m_query.exactPhrase && !m_query.anyWord && !m_query.regex, [](SearchQuery &) {});
    op(tr("Any Word"), m_query.anyWord && !m_query.exactPhrase && !m_query.regex, [](SearchQuery &q) { q.anyWord = true; });
    op(tr("Regular Expression"), m_query.regex, [](SearchQuery &q) { q.regex = true; });

    menu->addSection(tr("Options"));
    auto flag = [&](const QString &text, bool checked, std::function<void(SearchQuery &, bool)> set) {
        QAction *a = menu->addAction(text);
        a->setCheckable(true);
        a->setChecked(checked);
        connect(a, &QAction::triggered, this, [this, set](bool on) {
            set(m_query, on);
            onSearchTextChanged();
        });
        return a;
    };
    flag(tr("Whole Words"), m_query.wholeWord, [](SearchQuery &q, bool on) { q.wholeWord = on; });
    flag(tr("Case Sensitive"), m_query.caseSensitive, [](SearchQuery &q, bool on) { q.caseSensitive = on; });
    flag(tr("Invert Results"), m_query.invert, [](SearchQuery &q, bool on) { q.invert = on; });
    menu->addSeparator();
    flag(tr("Search Draft Only"), m_query.draftOnly, [](SearchQuery &q, bool on) { q.draftOnly = on; });
    QStringList selected;
    if (m_project && m_tree->selectionModel())
        for (const QModelIndex &i : m_tree->selectionModel()->selectedRows())
            if (BinderItem *it = m_project->model()->itemFor(i))
                selected << it->id;
    QAction *sel = flag(tr("Search Binder Selection Only"), !m_query.scopeIds.isEmpty(),
                        [selected](SearchQuery &q, bool on) { q.scopeIds = on ? selected : QStringList(); });
    sel->setEnabled(!selected.isEmpty() || !m_query.scopeIds.isEmpty());
    flag(tr("Search \"Included\" Documents"), m_query.compile == 1, [](SearchQuery &q, bool on) { q.compile = on ? 1 : 0; });
    flag(tr("Search \"Excluded\" Documents"), m_query.compile == 2, [](SearchQuery &q, bool on) { q.compile = on ? 2 : 0; });
    flag(tr("Include Trash"), m_query.includeTrash, [](SearchQuery &q, bool on) { q.includeTrash = on; });
    menu->addSeparator();
    QAction *save = menu->addAction(tr("Save Search as Collection…"), this, [this] {
        if (!m_project || m_query.text.trimmed().isEmpty())
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save Search"), tr("Collection name:"), QLineEdit::Normal,
                                                   m_query.text, &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
        Collection c;
        c.id = newId();
        c.name = name.trimmed();
        c.color = QColor(170, 140, 220);
        c.isSearch = true;
        c.query = m_query;
        m_project->collections << c;
        m_project->notifySettingsChanged();
        setCollectionsVisible(true);
        refreshCollections();
        showCollection(c.id);
    });
    save->setEnabled(m_project && !m_query.text.trimmed().isEmpty());
}

void BinderPanel::setProject(Project *p)
{
    m_project = p;
    m_results->setProject(p);
    m_results->setReorderable(false);
    m_tree->setEditorItemId(QString());
    m_tree->setModel(p ? p->model() : nullptr);
    if (p) {
        for (int c = 1; c < p->model()->columnCount(); ++c)
            m_tree->setColumnHidden(c, true);
        connect(p->model(), &QAbstractItemModel::modelReset, this, [this] {
            if (!m_project)
                return;
            for (int c = 1; c < m_project->model()->columnCount(); ++c)
                m_tree->setColumnHidden(c, true);
        });
        m_tree->expand(p->model()->indexFor(p->draft()));
    }
    m_stack->setCurrentWidget(m_tree);
    m_shownCollection.clear();
    // the last search of the project ("Search Results" runs it again)
    m_query = SearchQuery();
    m_query.searchEverywhere();
    if (p && p->uiState.contains(QStringLiteral("lastSearch")))
        m_query = search::fromMap(p->uiState.value(QStringLiteral("lastSearch")).toMap());
    {
        QSignalBlocker b(m_searchEdit);
        m_searchEdit->setText(m_query.text);
    }
    m_searchBar->hide();
    refreshCollections();
}

QList<BinderItem *> BinderPanel::selectedItems() const
{
    QList<BinderItem *> out;
    if (!m_project)
        return out;
    if (m_stack->currentWidget() == m_resultsPage)
        return m_results->selectedBinderItems();
    const QModelIndexList rows = m_tree->selectionModel() ? m_tree->selectionModel()->selectedRows() : QModelIndexList();
    for (const QModelIndex &i : rows)
        if (BinderItem *it = m_project->model()->itemFor(i))
            out << it;
    // keep binder order
    std::sort(out.begin(), out.end(), [](BinderItem *a, BinderItem *b) {
        QList<int> pa, pb;
        for (BinderItem *x = a; x->parent; x = x->parent)
            pa.prepend(x->row());
        for (BinderItem *x = b; x->parent; x = x->parent)
            pb.prepend(x->row());
        return pa < pb;
    });
    return out;
}

BinderItem *BinderPanel::currentItem() const
{
    if (!m_project)
        return nullptr;
    if (m_stack->currentWidget() == m_resultsPage) {
        QListWidgetItem *li = m_results->currentItem();
        return li ? m_project->item(li->data(Qt::UserRole).toString()) : nullptr;
    }
    return m_project->model()->itemFor(m_tree->currentIndex());
}

void BinderPanel::select(const QList<BinderItem *> &items, bool notify)
{
    if (!m_project || !m_tree->selectionModel())
        return;
    QSignalBlocker b(m_tree);
    m_tree->selectionModel()->clearSelection();
    for (BinderItem *it : items) {
        const QModelIndex idx = m_project->model()->indexFor(it);
        for (QModelIndex p = idx.parent(); p.isValid(); p = p.parent())
            m_tree->expand(p);
        m_tree->selectionModel()->select(idx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    if (!items.isEmpty()) {
        const QModelIndex idx = m_project->model()->indexFor(items.first());
        m_tree->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
        m_tree->scrollTo(idx);
    }
    if (notify)
        emit selectionChanged(items);
}

void BinderPanel::reveal(BinderItem *item)
{
    if (m_stack->currentWidget() != m_tree) {
        m_tabs->setCurrentIndex(0);
        m_stack->setCurrentWidget(m_tree);
    }
    select({item}, false);
}

void BinderPanel::editTitle(BinderItem *item)
{
    if (!m_project)
        return;
    reveal(item);
    const QModelIndex idx = m_project->model()->indexFor(item);
    m_tree->setCurrentIndex(idx);
    m_tree->edit(idx);
}

void BinderPanel::setFooterMenus(QMenu *addMenu, QMenu *gearMenu)
{
    m_addButton->setMenu(addMenu);
    m_gearButton->setMenu(gearMenu);
}

// ------------------------------------------------------------------ search

void BinderPanel::showSearch(bool focus)
{
    m_searchBar->show();
    if (focus) {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    }
}

void BinderPanel::hideSearch()
{
    m_searchBar->hide();
    m_shownCollection.clear();
    m_stack->setCurrentWidget(m_tree);
    selectSearchTab(false);
    emit searchChanged(); // (the editor keeps the document it shows)
}

bool BinderPanel::isSearching() const { return m_searchBar->isVisible(); }

bool BinderPanel::showsSearchResults() const
{
    return m_stack->currentWidget() == m_resultsPage && m_shownCollection == QLatin1String("__search");
}

void BinderPanel::setSearchQuery(const SearchQuery &q)
{
    m_query = q;
    QSignalBlocker b(m_searchEdit);
    m_searchEdit->setText(q.text);
}

// The tab that goes with what the list shows: "Search Results" or "Binder".
void BinderPanel::selectSearchTab(bool search)
{
    const bool was = m_updatingTabs;
    m_updatingTabs = true;
    for (int i = 0; i < m_tabs->count(); ++i) {
        const QString id = m_tabs->tabData(i).toString();
        if (search ? id == QLatin1String("__search") : id.isEmpty())
            m_tabs->setCurrentIndex(i);
    }
    m_updatingTabs = was;
}

void BinderPanel::onSearchTextChanged()
{
    m_query.text = m_searchEdit->text();
    if (m_query.text.trimmed().isEmpty()) {
        if (m_shownCollection == QLatin1String("__search")) {
            m_shownCollection.clear();
            m_stack->setCurrentWidget(m_tree);
            selectSearchTab(false);
            emit searchChanged();
        }
        return;
    }
    runSearch(m_query);
}

void BinderPanel::runSearch(const SearchQuery &q)
{
    if (!m_project)
        return;
    m_query = q;
    if (m_searchEdit->text() != q.text) {
        QSignalBlocker b(m_searchEdit);
        m_searchEdit->setText(q.text);
    }
    const QString mistake = search::patternError(q);
    m_searchEdit->setToolTip(mistake.isEmpty() ? QString() : tr("Regular expression: %1").arg(mistake));
    const QList<BinderItem *> hits = searchProject(m_project, q);
    m_shownCollection = QStringLiteral("__search");
    if (!q.text.trimmed().isEmpty())
        m_project->uiState[QStringLiteral("lastSearch")] = search::toMap(q);
    showResults(hits, mistake.isEmpty() ? tr("Search Results (%1)").arg(hits.size()) : tr("Search Results (mistake in the expression)"),
                QColor(180, 150, 220));
    selectSearchTab(true);
    emit searchChanged();
}

void BinderPanel::showResults(const QList<BinderItem *> &items, const QString &title, const QColor &color)
{
    m_results->clear();
    for (BinderItem *it : items) {
        auto *li = new QListWidgetItem(icons::forItem(m_project, it), it->title.isEmpty() ? tr("Untitled") : it->title);
        li->setData(Qt::UserRole, it->id);
        QString path;
        for (BinderItem *p = it->parent; p && p->parent; p = p->parent)
            path.prepend(p->title + QStringLiteral(" › "));
        li->setToolTip(path + it->title);
        m_results->addItem(li);
    }
    m_resultsTitle->setText(title);
    QWidget *header = m_resultsTitle->parentWidget();
    QPalette pal = header->palette();
    QColor bg = color;
    bg.setAlpha(110);
    pal.setColor(QPalette::Window, bg);
    header->setPalette(pal);
    m_stack->setCurrentWidget(m_resultsPage);
    m_results->setReorderable(showsArbitraryCollection());
}

void BinderPanel::onResultSelection()
{
    if (m_stack->currentWidget() == m_resultsPage)
        emit selectionChanged(selectedItems());
}

// ------------------------------------------------------------------ collections

void BinderPanel::setCollectionsVisible(bool on)
{
    m_collectionsBar->setVisible(on);
    if (!on && m_shownCollection != QLatin1String("__search")) {
        m_shownCollection.clear();
        m_stack->setCurrentWidget(m_tree);
    }
}

bool BinderPanel::collectionsVisible() const { return m_collectionsBar->isVisible(); }

void BinderPanel::refreshCollections()
{
    m_updatingTabs = true;
    const QString current = currentCollectionId();
    while (m_tabs->count())
        m_tabs->removeTab(0);
    m_tabs->addTab(icons::action(QStringLiteral("view-list-tree")), tr("Binder"));
    m_tabs->setTabData(0, QString());
    if (m_project) {
        for (const Collection &c : m_project->collections) {
            const int i = m_tabs->addTab(c.isSearch ? icons::action(QStringLiteral("search")) : QIcon(), c.name);
            m_tabs->setTabData(i, c.id);
            m_tabs->setTabTextColor(i, !c.color.isValid() ? palette().color(QPalette::WindowText)
                                          : theme::isDark() ? c.color.lighter(135) : c.color.darker(160));
        }
        // the last project search, run again when chosen (always the last tab)
        const int i = m_tabs->addTab(icons::action(QStringLiteral("search")), tr("Search Results"));
        m_tabs->setTabData(i, QStringLiteral("__search"));
        m_tabs->setTabToolTip(i, tr("Runs the last project search again"));
    }
    int idx = 0;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->tabData(i).toString() == current)
            idx = i;
    m_tabs->setCurrentIndex(idx);
    m_updatingTabs = false;
}

QString BinderPanel::currentCollectionId() const
{
    return m_tabs->currentIndex() >= 0 ? m_tabs->tabData(m_tabs->currentIndex()).toString() : QString();
}

void BinderPanel::showCollection(const QString &id)
{
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->tabData(i).toString() == id) {
            m_tabs->setCurrentIndex(i);
            onTabChanged(i);
            return;
        }
}

void BinderPanel::onTabChanged(int index)
{
    if (m_updatingTabs || !m_project)
        return;
    const QString id = m_tabs->tabData(index).toString();
    const bool wasSearch = showsSearchResults();
    if (id.isEmpty()) {
        m_shownCollection.clear();
        m_stack->setCurrentWidget(m_tree);
        if (wasSearch)
            emit searchChanged();
        emit selectionChanged(selectedItems());
        return;
    }
    if (id == QLatin1String("__search")) {
        if (m_query.text.trimmed().isEmpty()) {
            m_shownCollection = id;
            showResults({}, tr("Search Results (no search yet)"), QColor(180, 150, 220));
        } else {
            runSearch(m_query);
        }
        return;
    }
    if (wasSearch) {
        m_shownCollection = id; // (a collection takes the list)
        emit searchChanged();
    }
    for (const Collection &c : m_project->collections) {
        if (c.id != id)
            continue;
        m_shownCollection = id;
        QList<BinderItem *> items;
        if (c.isSearch) {
            items = searchProject(m_project, c.query);
        } else {
            for (const QString &iid : c.items)
                if (BinderItem *it = m_project->item(iid))
                    items << it;
        }
        showResults(items, c.name, c.color.isValid() ? c.color : QColor(150, 150, 150));
        return;
    }
}

void BinderPanel::addToCollection(const QString &collectionId, const QList<BinderItem *> &items, int row)
{
    if (!m_project)
        return;
    for (Collection &c : m_project->collections) {
        if (c.id != collectionId || c.isSearch)
            continue;
        if (row < 0) {
            for (BinderItem *it : items)
                if (!c.items.contains(it->id))
                    c.items << it->id;
        } else {
            // put them at that place, in the order given; those already in the collection move there
            QStringList ids;
            for (BinderItem *it : items)
                if (!ids.contains(it->id))
                    ids << it->id;
            int at = qBound(0, row, int(c.items.size()));
            for (const QString &id : ids) {
                const int old = int(c.items.indexOf(id));
                if (old >= 0) {
                    c.items.removeAt(old);
                    if (old < at)
                        --at;
                }
            }
            for (const QString &id : ids)
                c.items.insert(at++, id);
        }
    }
    m_project->notifySettingsChanged();
    if (m_shownCollection == collectionId) {
        QSignalBlocker b(m_results);
        showCollection(collectionId);
        for (int r = 0; r < m_results->count(); ++r)
            for (BinderItem *it : items)
                if (m_results->item(r)->data(Qt::UserRole).toString() == it->id)
                    m_results->item(r)->setSelected(true);
    }
}

bool BinderPanel::showsArbitraryCollection() const
{
    if (!m_project || m_stack->currentWidget() != m_resultsPage)
        return false;
    for (const Collection &c : m_project->collections)
        if (c.id == m_shownCollection)
            return !c.isSearch;
    return false;
}

void BinderPanel::removeFromCollection(const QList<BinderItem *> &items)
{
    if (!showsArbitraryCollection() || items.isEmpty())
        return;
    for (Collection &c : m_project->collections)
        if (c.id == m_shownCollection)
            for (BinderItem *it : items)
                c.items.removeAll(it->id);
    m_project->notifySettingsChanged();
    QSignalBlocker b(m_results);
    showCollection(m_shownCollection);
}

void BinderPanel::setEditorItem(BinderItem *item) { m_tree->setEditorItemId(item ? item->id : QString()); }

QStringList BinderPanel::expandedIds() const
{
    QStringList out;
    if (!m_project)
        return out;
    for (BinderItem *it : m_project->allItems())
        if (!it->children.isEmpty() && m_tree->isExpanded(m_project->model()->indexFor(it)))
            out << it->id;
    return out;
}

void BinderPanel::restoreExpanded(const QStringList &ids)
{
    if (!m_project)
        return;
    for (const QString &id : ids)
        if (BinderItem *it = m_project->item(id))
            m_tree->expand(m_project->model()->indexFor(it));
}

} // namespace qb
