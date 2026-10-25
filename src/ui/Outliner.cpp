#include "ui/Outliner.h"
#include "ui/AppSettings.h"
#include "ui/DragDrop.h"
#include <QDragMoveEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <functional>
#include "core/BinderModel.h"
#include "core/Project.h"
#include "core/Search.h"
#include "text/TextUtil.h"

#include "ui/Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace qb {

// ------------------------------------------------------------------ proxy

void OutlinerProxy::setRoots(const QList<BinderItem *> &roots)
{
    beginResetModel();
    m_roots = QSet<BinderItem *>(roots.begin(), roots.end());
    m_ancestors.clear();
    for (BinderItem *r : roots)
        for (BinderItem *p = r->parent; p; p = p->parent)
            m_ancestors.insert(p);
    endResetModel();
}

void OutlinerProxy::setFilterText(const QString &t)
{
    beginResetModel();
    m_filter = t;
    endResetModel();
}

void OutlinerProxy::setFilter(Project *p, const ItemFilter &f)
{
    m_filterProject = p;
    m_itemFilter = f;
    invalidateFilter(); // (not a reset: the view keeps its root)
}

bool OutlinerProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    auto *model = static_cast<BinderModel *>(sourceModel());
    const QModelIndex idx = model->index(sourceRow, 0, sourceParent);
    BinderItem *it = model->itemFor(idx);
    if (!it)
        return false;
    if (m_keep.contains(it))
        return true;
    if (!m_roots.isEmpty()) {
        bool inside = m_roots.contains(it) || m_ancestors.contains(it);
        for (BinderItem *p = it->parent; p && !inside; p = p->parent)
            if (m_roots.contains(p))
                inside = true;
        if (!inside)
            return false;
    }
    if (m_itemFilter.isActive() && m_filterProject) {
        if (m_ancestors.contains(it) || (m_roots.contains(it) && it->isContainer()))
            return true; // the groups whose contents are listed
        if (search::matches(m_filterProject, it, m_itemFilter))
            return true;
        // keep the groups of matches
        for (BinderItem *c : it->descendants())
            if (search::matches(m_filterProject, c, m_itemFilter))
                return true;
        return false;
    }
    if (!m_filter.isEmpty()) {
        if (it->title.contains(m_filter, Qt::CaseInsensitive) || it->synopsis.contains(m_filter, Qt::CaseInsensitive))
            return true;
        // keep parents of matches
        for (BinderItem *c : it->descendants())
            if (c->title.contains(m_filter, Qt::CaseInsensitive) || c->synopsis.contains(m_filter, Qt::CaseInsensitive))
                return true;
        return m_ancestors.contains(it) || m_roots.contains(it);
    }
    return true;
}

// ------------------------------------------------------------------ delegates

namespace {

class TitleSynopsisDelegate : public QStyledItemDelegate
{
public:
    TitleSynopsisDelegate(Outliner *o, Project *p) : QStyledItemDelegate(o), m_outliner(o), m_project(p) {}

    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &index) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(opt, index);
        if (!m_outliner->showSynopsis())
            return s;
        const QString syn = index.data(BinderModel::PreviewRole).toString();
        if (syn.isEmpty())
            return s;
        QFont f = opt.font;
        f.setPointSizeF(f.pointSizeF() * 0.92);
        const QFontMetrics fm(f);
        const int w = qMax(100, m_outliner->columnWidth(0) - 60);
        const QRect br = fm.boundingRect(QRect(0, 0, w, 1000), Qt::TextWordWrap, syn.left(400));
        s.setHeight(s.height() + qMin(br.height(), fm.lineSpacing() * 3) + 4);
        return s;
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QString title = opt.text;
        opt.text.clear();
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, p, opt.widget);
        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
        BinderItem *it = static_cast<BinderItem *>(index.data(BinderModel::ItemRole).value<void *>());
        const bool selected = opt.state & QStyle::State_Selected;
        p->save();
        QFont tf = opt.font;
        if (it && it->isContainer())
            tf.setBold(true);
        p->setFont(tf);
        p->setPen(opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
        const QFontMetrics tfm(tf);
        QRect tr = textRect;
        tr.setHeight(tfm.height() + 2);
        const QString shown = title.isEmpty() ? QObject::tr("Untitled") : title;
        p->drawText(tr, Qt::AlignVCenter | Qt::AlignLeft, tfm.elidedText(shown, Qt::ElideRight, tr.width()));
        if (m_outliner->showSynopsis()) {
            const QString syn = index.data(BinderModel::PreviewRole).toString();
            if (!syn.isEmpty()) {
                QFont sf = opt.font;
                sf.setPointSizeF(sf.pointSizeF() * 0.92);
                p->setFont(sf);
                QColor c = opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
                c.setAlpha(it && it->synopsis.isEmpty() ? 120 : 190);
                p->setPen(c);
                const QFontMetrics sfm(sf);
                QRect sr(textRect.left(), tr.bottom() + 1, textRect.width(), sfm.lineSpacing() * 3);
                p->drawText(sr, Qt::TextWordWrap | Qt::AlignTop, syn.left(400));
            }
        }
        p->restore();
    }

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        auto *w = new QWidget(parent);
        w->setAutoFillBackground(true);
        auto *l = new QVBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(1);
        auto *title = new QLineEdit(w);
        title->setObjectName(QStringLiteral("title"));
        title->setPlaceholderText(QObject::tr("Title"));
        auto *syn = new QPlainTextEdit(w);
        syn->setObjectName(QStringLiteral("synopsis"));
        syn->setPlaceholderText(QObject::tr("Synopsis"));
        syn->setTabChangesFocus(true);
        l->addWidget(title);
        l->addWidget(syn, 1);
        QObject::connect(title, &QLineEdit::returnPressed, syn, [syn] { syn->setFocus(); });
        return w;
    }

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        QRect r = option.rect;
        r.setHeight(qMax(r.height(), 90));
        editor->setGeometry(r);
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        editor->findChild<QLineEdit *>(QStringLiteral("title"))->setText(index.data(Qt::EditRole).toString());
        editor->findChild<QPlainTextEdit *>(QStringLiteral("synopsis"))->setPlainText(index.data(BinderModel::SynopsisRole).toString());
        editor->findChild<QLineEdit *>(QStringLiteral("title"))->setFocus();
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        model->setData(index, editor->findChild<QLineEdit *>(QStringLiteral("title"))->text(), Qt::EditRole);
        BinderItem *it = static_cast<BinderItem *>(index.data(BinderModel::ItemRole).value<void *>());
        const QString syn = editor->findChild<QPlainTextEdit *>(QStringLiteral("synopsis"))->toPlainText();
        if (it && syn != it->synopsis) {
            it->synopsis = syn;
            m_project->notifyItemChanged(it, false);
        }
    }

private:
    Outliner *m_outliner;
    Project *m_project;
};

class ProgressDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(p, option, index);
        const double v = index.data(BinderModel::ProgressRole).toDouble();
        if (v < 0)
            return;
        const QRect r = option.rect.adjusted(4, option.rect.height() / 2 - 4, -4, -(option.rect.height() / 2 - 4));
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0, 0, 0, 40));
        p->drawRoundedRect(r, 3, 3);
        QRect f = r;
        f.setWidth(int(r.width() * v));
        p->setBrush(theme::color(v >= 1.0 ? theme::Role::TargetReached : theme::Role::TargetDraft));
        p->drawRoundedRect(f, 3, 3);
        p->restore();
    }
};

class ChoiceDelegate : public QStyledItemDelegate
{
public:
    ChoiceDelegate(QObject *parent, Project *p, int kind) : QStyledItemDelegate(parent), m_project(p), m_kind(kind) {}
    // kind: 0 label, 1 status
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        auto *cb = new QComboBox(parent);
        cb->addItem(m_kind == 0 ? QObject::tr("No %1").arg(m_project->labelTitle) : QObject::tr("No %1").arg(m_project->statusTitle), -1);
        if (m_kind == 0) {
            for (const Label &l : m_project->labels) {
                QPixmap pm(12, 12);
                pm.fill(l.color);
                cb->addItem(QIcon(pm), l.name, l.id);
            }
        } else {
            for (const Status &s : m_project->statuses)
                cb->addItem(s.name, s.id);
        }
        return cb;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *cb = static_cast<QComboBox *>(editor);
        cb->setCurrentIndex(qMax(0, cb->findData(index.data(Qt::EditRole).toInt())));
        cb->showPopup();
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        model->setData(index, static_cast<QComboBox *>(editor)->currentData(), Qt::EditRole);
    }

private:
    Project *m_project;
    int m_kind;
};

class CustomFieldDelegate : public QStyledItemDelegate
{
public:
    CustomFieldDelegate(QObject *parent, Project *p) : QStyledItemDelegate(parent), m_project(p) {}
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt, const QModelIndex &index) const override
    {
        const CustomField *cf = field(index);
        if (cf && cf->type == CustomFieldType::List) {
            auto *cb = new QComboBox(parent);
            cb->addItem(QString());
            cb->addItems(cf->options);
            return cb;
        }
        if (cf && cf->type == CustomFieldType::Date) {
            auto *de = new QDateEdit(parent);
            de->setCalendarPopup(true);
            de->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
            return de;
        }
        return QStyledItemDelegate::createEditor(parent, opt, index);
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (auto *cb = qobject_cast<QComboBox *>(editor)) {
            cb->setCurrentText(index.data(Qt::EditRole).toString());
            return;
        }
        if (auto *de = qobject_cast<QDateEdit *>(editor)) {
            const QDate d = QDate::fromString(index.data(Qt::EditRole).toString(), Qt::ISODate);
            de->setDate(d.isValid() ? d : QDate::currentDate());
            return;
        }
        QStyledItemDelegate::setEditorData(editor, index);
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        if (auto *cb = qobject_cast<QComboBox *>(editor)) {
            model->setData(index, cb->currentText(), Qt::EditRole);
            return;
        }
        if (auto *de = qobject_cast<QDateEdit *>(editor)) {
            model->setData(index, de->date().toString(Qt::ISODate), Qt::EditRole);
            return;
        }
        QStyledItemDelegate::setModelData(editor, model, index);
    }

private:
    const CustomField *field(const QModelIndex &index) const
    {
        auto *proxy = qobject_cast<const QSortFilterProxyModel *>(index.model());
        auto *bm = qobject_cast<BinderModel *>(proxy ? proxy->sourceModel() : nullptr);
        return bm ? m_project->customFieldById(bm->customFieldAt(index.column())) : nullptr;
    }
    Project *m_project;
};

} // namespace

// ------------------------------------------------------------------ outliner

Outliner::Outliner(QWidget *parent) : QTreeView(parent)
{
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    setUniformRowHeights(false);
    setAlternatingRowColors(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setIconSize(QSize(18, 18));
    setFrameShape(QFrame::NoFrame);
    header()->setSectionsMovable(true);
    header()->setContextMenuPolicy(Qt::CustomContextMenu);
    header()->setSectionsClickable(true);
    header()->setSortIndicatorShown(false);
    connect(header(), &QHeaderView::customContextMenuRequested, this,
            [this](const QPoint &p) { showColumnMenu(header()->mapToGlobal(p)); });
    connect(header(), &QHeaderView::sectionClicked, this, &Outliner::onHeaderClicked);
    connect(this, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &p) { emit contextMenuRequested(viewport()->mapToGlobal(p)); });
    connect(header(), &QHeaderView::sectionResized, this, [this](int logical, int, int) {
        if (logical == 0)
            doItemsLayout();
    });
    auto themed = [this] {
        theme::styleView(this, theme::Role::OutlinerBackground, theme::Role::OutlinerText, theme::FontRole::Outliner);
        QPalette p = palette();
        p.setColor(QPalette::AlternateBase, theme::color(theme::Role::OutlinerAlternate));
        setPalette(p);
        header()->setPalette(QApplication::palette());
        header()->setFont(QApplication::font());
        doItemsLayout(); // row heights follow the font
    };
    themed();
    connect(&theme::Notifier::instance(), &theme::Notifier::changed, this, themed);
}

void Outliner::setProject(Project *p)
{
    m_project = p;
    delete m_proxy;
    m_proxy = nullptr;
    if (!p) {
        setModel(nullptr);
        return;
    }
    m_proxy = new OutlinerProxy(this);
    m_proxy->setSourceModel(p->model());
    m_proxy->setSortRole(BinderModel::SortRole);
    m_proxy->setDynamicSortFilter(false);
    setModel(m_proxy);
    setItemDelegateForColumn(BinderModel::ColTitle, new TitleSynopsisDelegate(this, p));
    setItemDelegateForColumn(BinderModel::ColProgress, new ProgressDelegate(this));
    setItemDelegateForColumn(BinderModel::ColLabel, new ChoiceDelegate(this, p, 0));
    setItemDelegateForColumn(BinderModel::ColStatus, new ChoiceDelegate(this, p, 1));
    auto *custom = new CustomFieldDelegate(this, p);
    for (int c = BinderModel::ColFirstCustom; c < p->model()->columnCount(); ++c)
        setItemDelegateForColumn(c, custom);
    connect(p->model(), &QAbstractItemModel::modelReset, this, [this] {
        auto *custom = new CustomFieldDelegate(this, m_project);
        for (int c = BinderModel::ColFirstCustom; c < m_project->model()->columnCount(); ++c)
            setItemDelegateForColumn(c, custom);
        restoreColumns();
    });
    restoreColumns();
}

void Outliner::restoreColumns()
{
    if (!m_project)
        return;
    const QStringList visible = m_project->uiState.value(QStringLiteral("outlinerColumns"),
                                                         QStringList{QStringLiteral("0"), QStringLiteral("1"),
                                                                     QStringLiteral("2"), QStringLiteral("4"),
                                                                     QStringLiteral("6"), QStringLiteral("7")})
                                    .toStringList();
    for (int c = 0; c < m_project->model()->columnCount(); ++c)
        setColumnHidden(c, c != 0 && !visible.contains(QString::number(c)));
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < m_project->model()->columnCount(); ++c) {
        header()->setSectionResizeMode(c, QHeaderView::Interactive);
        setColumnWidth(c, c == BinderModel::ColProgress ? 90 : 100);
    }
    m_showSynopsis = m_project->uiState.value(QStringLiteral("outlinerSynopsis"), true).toBool();
    fitTitleColumn();
}

void Outliner::fitTitleColumn()
{
    if (!m_project || !model())
        return;
    // The title column takes what the other columns leave. In a narrow editor
    // (split window) that would be next to nothing: then it gets a fixed
    // width and the outliner scrolls sideways.
    const int minTitle = 260;
    int others = 0;
    for (int c = 1; c < model()->columnCount(); ++c)
        if (!isColumnHidden(c))
            others += columnWidth(c);
    const bool room = viewport()->width() - others >= minTitle;
    const QHeaderView::ResizeMode wanted = room ? QHeaderView::Stretch : QHeaderView::Interactive;
    if (header()->sectionResizeMode(0) != wanted)
        header()->setSectionResizeMode(0, wanted);
    if (!room && columnWidth(0) < minTitle)
        setColumnWidth(0, minTitle);
}

void Outliner::resizeEvent(QResizeEvent *e)
{
    QTreeView::resizeEvent(e);
    fitTitleColumn();
}

void Outliner::saveColumns()
{
    if (!m_project)
        return;
    QStringList visible;
    for (int c = 0; c < m_project->model()->columnCount(); ++c)
        if (!isColumnHidden(c))
            visible << QString::number(c);
    m_project->uiState[QStringLiteral("outlinerColumns")] = visible;
    m_project->uiState[QStringLiteral("outlinerSynopsis")] = m_showSynopsis;
    m_project->markIndexDirty();
}

void Outliner::showColumnMenu(const QPoint &globalPos)
{
    if (!m_project)
        return;
    QMenu menu;
    fillColumnMenu(&menu);
    menu.exec(globalPos);
}

void Outliner::fillColumnMenu(QMenu *menu, bool withSynopsis)
{
    if (!m_project)
        return;
    for (int c = 1; c < m_project->model()->columnCount(); ++c) {
        QAction *a = menu->addAction(m_project->model()->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString());
        a->setCheckable(true);
        a->setChecked(!isColumnHidden(c));
        connect(a, &QAction::toggled, this, [this, c](bool on) {
            setColumnHidden(c, !on);
            saveColumns();
            fitTitleColumn();
        });
    }
    if (!withSynopsis)
        return;
    menu->addSeparator();
    QAction *syn = menu->addAction(tr("Show Synopses"));
    syn->setCheckable(true);
    syn->setChecked(m_showSynopsis);
    connect(syn, &QAction::toggled, this, &Outliner::setShowSynopsis);
}

void Outliner::onHeaderClicked(int section)
{
    if (!m_proxy)
        return;
    if (section != m_sortColumn) {
        m_sortColumn = section;
        m_sortStep = 0;
    }
    m_sortStep = (m_sortStep + 1) % 3;
    if (m_sortStep == 0) {
        m_proxy->sort(-1);
        header()->setSortIndicatorShown(false);
        setDragDropMode(QAbstractItemView::DragDrop);
        m_sortColumn = -1;
    } else {
        const Qt::SortOrder o = m_sortStep == 1 ? Qt::AscendingOrder : Qt::DescendingOrder;
        m_proxy->sort(section, o);
        header()->setSortIndicatorShown(true);
        header()->setSortIndicator(section, o);
        setDragDropMode(QAbstractItemView::DragOnly); // no reordering while sorted
    }
}

void Outliner::setContent(const QList<BinderItem *> &containers, const QList<BinderItem *> &loose)
{
    if (!m_project || !m_proxy)
        return;
    m_contentContainers = containers;
    m_contentLoose = loose;
    QSet<BinderItem *> keep;
    if (containers.size() == 1 && loose.isEmpty())
        for (BinderItem *k = containers.first(); k; k = k->parent)
            keep.insert(k);
    m_proxy->setKeep(keep);
    if (containers.size() == 1 && loose.isEmpty()) {
        m_proxy->setRoots({});
        setRootIndex(m_proxy->mapFromSource(m_project->model()->indexFor(containers.first())));
    } else {
        QList<BinderItem *> roots = containers + loose;
        m_proxy->setRoots(roots);
        // common ancestor as root
        BinderItem *common = roots.isEmpty() ? m_project->root() : roots.first()->parent;
        auto isAncestor = [](BinderItem *a, BinderItem *b) { return b == a || b->isDescendantOf(a); };
        for (BinderItem *r : roots)
            while (common && !isAncestor(common, r))
                common = common->parent;
        setRootIndex(common && common != m_project->root() ? m_proxy->mapFromSource(m_project->model()->indexFor(common))
                                                            : QModelIndex());
        for (BinderItem *r : roots)
            for (BinderItem *p = r; p && p != common; p = p->parent)
                expand(m_proxy->mapFromSource(m_project->model()->indexFor(p)));
    }
    expandToDepth(0);
    if (m_proxy->filter().isActive())
        expandAll();
}

QList<QPair<BinderItem *, int>> Outliner::listedRows(bool all) const
{
    QList<QPair<BinderItem *, int>> out;
    if (!m_proxy || !m_project)
        return out;
    std::function<void(const QModelIndex &, int)> walk = [&](const QModelIndex &parent, int depth) {
        for (int r = 0; r < m_proxy->rowCount(parent); ++r) {
            const QModelIndex idx = m_proxy->index(r, 0, parent);
            BinderItem *it = m_project->model()->itemFor(m_proxy->mapToSource(idx));
            if (!it)
                continue;
            out.append({it, depth});
            if (all || isExpanded(idx))
                walk(idx, depth + 1);
        }
    };
    walk(rootIndex(), 0);
    return out;
}

QList<QPair<int, int>> Outliner::shownColumns() const
{
    QList<QPair<int, int>> out;
    if (!model())
        return out;
    for (int v = 0; v < header()->count(); ++v) {
        const int c = header()->logicalIndex(v);
        if (c < 0 || isColumnHidden(c))
            continue;
        out.append({c, columnWidth(c)});
    }
    return out;
}

void Outliner::setShowSynopsis(bool on)
{
    m_showSynopsis = on;
    saveColumns();
    doItemsLayout();
}

void Outliner::setFilterText(const QString &t)
{
    if (m_proxy)
        m_proxy->setFilterText(t);
    expandAll();
}

void Outliner::setFilter(const ItemFilter &f)
{
    if (!m_proxy || (!f.isActive() && !m_proxy->filter().isActive()))
        return;
    m_proxy->setFilter(m_project, f);
    if (f.isActive())
        expandAll();
}

ItemFilter Outliner::filter() const { return m_proxy ? m_proxy->filter() : ItemFilter(); }

void Outliner::filterCounts(int *shown, int *total) const
{
    *shown = *total = 0;
    if (!m_project || !m_proxy)
        return;
    const ItemFilter f = m_proxy->filter();
    QList<BinderItem *> all = m_contentLoose;
    for (BinderItem *c : m_contentContainers)
        all << c->descendants();
    const QList<BinderItem *> alive = m_project->allItems();
    for (BinderItem *it : all) {
        if (!alive.contains(it))
            continue;
        ++*total;
        if (search::matches(m_project, it, f))
            ++*shown;
    }
}

QList<BinderItem *> Outliner::selectedItems() const
{
    QList<BinderItem *> out;
    if (!m_proxy || !selectionModel())
        return out;
    for (const QModelIndex &i : selectionModel()->selectedRows())
        if (BinderItem *it = m_project->model()->itemFor(m_proxy->mapToSource(i)))
            out << it;
    return out;
}

void Outliner::selectItems(const QList<BinderItem *> &items)
{
    if (!m_proxy || !selectionModel())
        return;
    QSignalBlocker b(this);
    selectionModel()->clearSelection();
    for (BinderItem *it : items) {
        const QModelIndex idx = m_proxy->mapFromSource(m_project->model()->indexFor(it));
        if (idx.isValid())
            selectionModel()->select(idx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
}

void Outliner::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    QTreeView::selectionChanged(selected, deselected);
    emit selectionChangedByUser();
}

void Outliner::keyPressEvent(QKeyEvent *e)
{
    if (state() != QAbstractItemView::EditingState && e->key() == Qt::Key_Delete) {
        emit deleteRequested();
        return;
    }
    if (state() != QAbstractItemView::EditingState && e->key() == Qt::Key_Space && e->modifiers() == Qt::NoModifier) {
        emit spacePressed(); // Quick Reference panels for the selected documents
        return;
    }
    if (state() != QAbstractItemView::EditingState && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
        && e->modifiers() == Qt::NoModifier && AppSettings::instance().returnCreatesItem && currentIndex().isValid()) {
        emit newItemRequested();
        return;
    }
    QTreeView::keyPressEvent(e);
}

BinderItem *Outliner::itemAt(const QPoint &pos) const
{
    const QModelIndex idx = indexAt(pos);
    if (!idx.isValid() || !m_proxy || !m_project)
        return nullptr;
    return m_project->model()->itemFor(m_proxy->mapToSource(idx.siblingAtColumn(0)));
}

void Outliner::mousePressEvent(QMouseEvent *e)
{
    // Alt+click on the arrow of a group opens (or closes) everything below it
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::AltModifier)) {
        const QModelIndex idx = indexAt(e->pos()).siblingAtColumn(0);
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
    QTreeView::mousePressEvent(e);
}

void Outliner::mouseDoubleClickEvent(QMouseEvent *e)
{
    // a double click on the icon opens the document in this editor (on the text: edit title or synopsis)
    const QModelIndex idx = indexAt(e->pos()).siblingAtColumn(0);
    if (e->button() == Qt::LeftButton && idx.isValid()) {
        const QRect r = visualRect(idx);
        if (e->pos().x() >= r.left() && e->pos().x() <= r.left() + iconSize().width() + 6) {
            if (BinderItem *it = itemAt(e->pos())) {
                emit openRequested(it);
                return;
            }
        }
    }
    QTreeView::mouseDoubleClickEvent(e);
}

// keywords dragged from the Project Keywords window
void Outliner::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasFormat(QString::fromLatin1(kKeywordsMime))) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
        return;
    }
    QTreeView::dragEnterEvent(e);
}

void Outliner::dragMoveEvent(QDragMoveEvent *e)
{
    if (e->mimeData()->hasFormat(QString::fromLatin1(kKeywordsMime))) {
        if (itemAt(e->position().toPoint())) {
            e->setDropAction(Qt::CopyAction);
            e->accept();
        } else {
            e->ignore();
        }
        return;
    }
    QTreeView::dragMoveEvent(e);
}

void Outliner::dropEvent(QDropEvent *e)
{
    if (e->mimeData()->hasFormat(QString::fromLatin1(kKeywordsMime))) {
        dnd::dropKeywords(m_project, itemAt(e->position().toPoint()), selectedItems(), e->mimeData(), e->modifiers());
        e->setDropAction(Qt::CopyAction);
        e->accept();
        return;
    }
    QTreeView::dropEvent(e);
}

} // namespace qb
