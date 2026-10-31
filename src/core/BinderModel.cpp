#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "text/TextUtil.h"

#include <QDir>
#include <QLocale>
#include <QMimeData>

namespace qb {

BinderModel::BinderModel(Project *project) : QAbstractItemModel(project), m_project(project)
{
    connect(project, &Project::itemChanged, this, &BinderModel::onItemChanged);
}

QModelIndex BinderModel::index(int row, int column, const QModelIndex &parent) const
{
    BinderItem *p = parent.isValid() ? itemFor(parent) : m_project->root();
    if (!p || row < 0 || row >= p->children.size() || column < 0 || column >= columnCount())
        return QModelIndex();
    return createIndex(row, column, p->children.at(row));
}

QModelIndex BinderModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return QModelIndex();
    BinderItem *it = itemFor(child);
    if (!it || !it->parent || it->parent == m_project->root())
        return QModelIndex();
    return createIndex(it->parent->row(), 0, it->parent);
}

int BinderModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() && parent.column() != 0)
        return 0;
    BinderItem *p = parent.isValid() ? itemFor(parent) : m_project->root();
    return p ? int(p->children.size()) : 0;
}

int BinderModel::columnCount(const QModelIndex &) const
{
    return ColFirstCustom + int(m_project->customFields.size());
}

BinderItem *BinderModel::itemFor(const QModelIndex &index) const
{
    if (!index.isValid())
        return nullptr;
    return static_cast<BinderItem *>(index.internalPointer());
}

QModelIndex BinderModel::indexFor(const BinderItem *item, int column) const
{
    if (!item || item == m_project->root() || !item->parent)
        return QModelIndex();
    return createIndex(item->row(), column, const_cast<BinderItem *>(item));
}

QString BinderModel::customFieldAt(int column) const
{
    const int i = column - ColFirstCustom;
    if (i < 0 || i >= m_project->customFields.size())
        return QString();
    return m_project->customFields.at(i).id;
}

QVariant BinderModel::data(const QModelIndex &index, int role) const
{
    BinderItem *it = itemFor(index);
    if (!it)
        return QVariant();
    const int col = index.column();
    switch (role) {
    case ItemRole: return QVariant::fromValue(static_cast<void *>(it));
    case IdRole: return it->id;
    case SynopsisRole: return it->synopsis;
    case TypeRole: return int(it->type);
    case LabelColorRole: {
        const Label *l = m_project->labelById(it->label);
        return l ? l->color : QColor();
    }
    case PreviewRole: {
        if (!it->synopsis.isEmpty())
            return it->synopsis;
        if (!it->hasText())
            return QString();
        return text::previewText(m_project->docs()->plainText(it->id), 400);
    }
    case ProgressRole: {
        if (it->target <= 0)
            return -1.0;
        const int cur = it->targetChars ? it->charCount : it->wordCount;
        return qMin(1.0, double(cur) / it->target);
    }
    default: break;
    }

    if (col == ColTitle) {
        if (role == Qt::DisplayRole || role == Qt::EditRole || role == SortRole)
            return it->title;
        if (role == Qt::DecorationRole)
            return icons::forItem(m_project, it);
        if (role == Qt::ToolTipRole) {
            QString tip = it->title;
            if (it->hasText())
                tip += QStringLiteral("\n") + text::plural(it->wordCount, tr("word"), tr("words"));
            return tip;
        }
        return QVariant();
    }
    if (role == Qt::TextAlignmentRole) {
        if (col == ColWords || col == ColChars || col == ColTarget)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        if (col == ColCompile)
            return int(Qt::AlignCenter);
        return QVariant();
    }
    switch (col) {
    case ColLabel: {
        const Label *l = m_project->labelById(it->label);
        if (role == Qt::DisplayRole || role == SortRole)
            return l ? l->name : QString();
        if (role == Qt::DecorationRole && l) {
            QPixmap pm(12, 12);
            pm.fill(l->color);
            return pm;
        }
        if (role == Qt::EditRole)
            return it->label;
        break;
    }
    case ColStatus: {
        const Status *s = m_project->statusById(it->status);
        if (role == Qt::DisplayRole || role == SortRole)
            return s ? s->name : QString();
        if (role == Qt::EditRole)
            return it->status;
        break;
    }
    case ColKeywords: {
        if (role == Qt::DisplayRole || role == SortRole) {
            QStringList names;
            for (const QString &k : it->keywords)
                if (const Keyword *kw = m_project->keywordById(k))
                    names << kw->name;
            return names.join(QStringLiteral(", "));
        }
        break;
    }
    case ColWords:
        if (role == Qt::DisplayRole)
            return it->hasText() ? QLocale().toString(it->wordCount) : QString();
        if (role == SortRole)
            return it->wordCount;
        break;
    case ColChars:
        if (role == Qt::DisplayRole)
            return it->hasText() ? QLocale().toString(it->charCount) : QString();
        if (role == SortRole)
            return it->charCount;
        break;
    case ColTarget:
        if (role == Qt::DisplayRole || role == Qt::EditRole)
            return it->target > 0 ? QVariant(it->target) : QVariant(QString());
        if (role == SortRole)
            return it->target;
        break;
    case ColProgress:
        if (role == SortRole) {
            if (it->target <= 0)
                return -1.0;
            return double(it->targetChars ? it->charCount : it->wordCount) / it->target;
        }
        break;
    case ColCreated:
        if (role == Qt::DisplayRole)
            return QLocale().toString(it->created, QStringLiteral("yyyy-MM-dd HH:mm"));
        if (role == SortRole)
            return it->created;
        break;
    case ColModified:
        if (role == Qt::DisplayRole)
            return QLocale().toString(it->modified, QStringLiteral("yyyy-MM-dd HH:mm"));
        if (role == SortRole)
            return it->modified;
        break;
    case ColCompile:
        if (role == Qt::CheckStateRole && m_project->isInDraft(it) && it != m_project->draft())
            return it->includeInCompile ? Qt::Checked : Qt::Unchecked;
        if (role == SortRole)
            return it->includeInCompile;
        break;
    case ColSection: {
        const bool explicitType = !it->sectionType.isEmpty();
        const SectionType *st = m_project->sectionTypeById(m_project->effectiveSectionType(it));
        if (role == Qt::DisplayRole || role == SortRole)
            return st ? st->name : QString();
        if (role == Qt::FontRole && !explicitType) {
            QFont f;
            f.setItalic(true);
            return f;
        }
        if (role == Qt::ForegroundRole && !explicitType)
            return QColor(130, 130, 130);
        break;
    }
    default: {
        const QString fid = customFieldAt(col);
        const CustomField *cf = m_project->customFieldById(fid);
        if (!cf)
            break;
        const QString v = it->custom.value(fid, cf->defaultValue);
        if (cf->type == CustomFieldType::Checkbox) {
            if (role == Qt::CheckStateRole)
                return v == QLatin1String("1") || v == QLatin1String("true") ? Qt::Checked : Qt::Unchecked;
            if (role == SortRole)
                return v;
            break;
        }
        if (role == Qt::DisplayRole || role == Qt::EditRole || role == SortRole) {
            if (cf->type == CustomFieldType::Date && role == Qt::DisplayRole) {
                const QDate d = QDate::fromString(v, Qt::ISODate);
                return d.isValid() ? d.toString(QStringLiteral("yyyy-MM-dd")) : v;
            }
            return v;
        }
        break;
    }
    }
    return QVariant();
}

bool BinderModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    BinderItem *it = itemFor(index);
    if (!it)
        return false;
    const int col = index.column();
    if (col == ColTitle && role == Qt::EditRole) {
        const QString t = value.toString().trimmed();
        if (t == it->title)
            return false;
        it->title = t;
        it->autoTitle = false; // named by hand: the title no longer follows the text (0.40.3)
        m_project->notifyItemChanged(it, false);
        return true;
    }
    if (col == ColCompile && role == Qt::CheckStateRole) {
        it->includeInCompile = value.toInt() == Qt::Checked;
        m_project->notifyItemChanged(it, false);
        return true;
    }
    if (col == ColLabel && role == Qt::EditRole) {
        it->label = value.toInt();
        m_project->notifyItemChanged(it, false);
        return true;
    }
    if (col == ColStatus && role == Qt::EditRole) {
        it->status = value.toInt();
        m_project->notifyItemChanged(it, false);
        return true;
    }
    if (col == ColTarget && role == Qt::EditRole) {
        it->target = qMax(0, value.toInt());
        m_project->notifyItemChanged(it, false);
        return true;
    }
    if (col >= ColFirstCustom) {
        const QString fid = customFieldAt(col);
        const CustomField *cf = m_project->customFieldById(fid);
        if (!cf)
            return false;
        if (cf->type == CustomFieldType::Checkbox && role == Qt::CheckStateRole)
            it->custom[fid] = value.toInt() == Qt::Checked ? QStringLiteral("1") : QStringLiteral("0");
        else if (role == Qt::EditRole)
            it->custom[fid] = value.toString();
        else
            return false;
        m_project->notifyItemChanged(it, false);
        return true;
    }
    return false;
}

Qt::ItemFlags BinderModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::ItemIsDropEnabled;
    BinderItem *it = itemFor(index);
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    const int col = index.column();
    if (col == ColTitle) {
        f |= Qt::ItemIsEditable;
        if (!it->isRootFolder())
            f |= Qt::ItemIsDragEnabled;
        f |= Qt::ItemIsDropEnabled;
    } else if (col == ColCompile) {
        if (m_project->isInDraft(it) && it != m_project->draft())
            f |= Qt::ItemIsUserCheckable;
    } else if (col == ColLabel || col == ColStatus || col == ColTarget) {
        f |= Qt::ItemIsEditable;
    } else if (col >= ColFirstCustom) {
        const CustomField *cf = m_project->customFieldById(customFieldAt(col));
        if (cf && cf->type == CustomFieldType::Checkbox)
            f |= Qt::ItemIsUserCheckable;
        else
            f |= Qt::ItemIsEditable;
    }
    if (!it->isRootFolder())
        f |= Qt::ItemIsDragEnabled;
    return f;
}

QVariant BinderModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();
    switch (section) {
    case ColTitle: return tr("Title and Synopsis");
    case ColLabel: return m_project->labelTitle;
    case ColStatus: return m_project->statusTitle;
    case ColKeywords: return tr("Keywords");
    case ColWords: return tr("Words");
    case ColChars: return tr("Characters");
    case ColTarget: return tr("Target");
    case ColProgress: return tr("Progress");
    case ColCreated: return tr("Created");
    case ColModified: return tr("Modified");
    case ColCompile: return tr("Compile");
    case ColSection: return tr("Section Type");
    default: {
        const CustomField *cf = m_project->customFieldById(customFieldAt(section));
        return cf ? cf->title : QVariant();
    }
    }
}

QStringList BinderModel::mimeTypes() const
{
    return {QString::fromLatin1(kItemsMime), QStringLiteral("text/uri-list"), QStringLiteral("text/plain")};
}

// ------------------------------------------------------------------ dragged documents

std::function<QList<QUrl>(Project *, const QStringList &)> ItemsMimeData::fileProvider;
bool BinderModel::dropOnTextDocuments = true;

ItemsMimeData::ItemsMimeData(Project *project, const QStringList &ids) : m_project(project), m_ids(ids) {}

static const QString kUriList = QStringLiteral("text/uri-list");

QStringList ItemsMimeData::formats() const
{
    QStringList f = QMimeData::formats();
    if (fileProvider && !f.contains(kUriList))
        f << kUriList;
    return f;
}

bool ItemsMimeData::hasFormat(const QString &mimeType) const
{
    if (mimeType == kUriList)
        return bool(fileProvider);
    return QMimeData::hasFormat(mimeType);
}

QVariant ItemsMimeData::retrieveData(const QString &mimeType, QMetaType type) const
{
    if (mimeType != kUriList)
        return QMimeData::retrieveData(mimeType, type);
    if (!m_exported) {
        m_exported = true; // also after a failure: do not try again on every request
        if (fileProvider && m_project) {
            QByteArray list;
            for (const QUrl &u : fileProvider(m_project, m_ids))
                list += u.toEncoded() + "\r\n";
            m_uriList = list;
        }
    }
    return m_uriList;
}

QMimeData *BinderModel::mimeForItems(const QList<BinderItem *> &items, Project *project)
{
    QStringList ids, titles;
    for (BinderItem *it : items)
        ids << it->id;
    auto *md = new ItemsMimeData(project, ids);
    ids.clear();
    for (BinderItem *it : items) {
        ids << it->id;
        titles << it->title;
    }
    QByteArray payload = project->folderPath().toUtf8() + '\n' + ids.join(QLatin1Char('\n')).toUtf8();
    md->setData(QString::fromLatin1(kItemsMime), payload);
    md->setText(titles.join(QLatin1Char('\n')));
    return md;
}

QList<BinderItem *> BinderModel::itemsFromMime(const QMimeData *data, Project *project)
{
    QList<BinderItem *> out;
    if (!data || !data->hasFormat(QString::fromLatin1(kItemsMime)))
        return out;
    const QList<QByteArray> lines = data->data(QString::fromLatin1(kItemsMime)).split('\n');
    if (lines.isEmpty() || QString::fromUtf8(lines.first()) != project->folderPath())
        return out;
    for (int i = 1; i < lines.size(); ++i)
        if (BinderItem *it = project->item(QString::fromUtf8(lines[i])))
            out << it;
    return out;
}

QMimeData *BinderModel::mimeData(const QModelIndexList &indexes) const
{
    QList<BinderItem *> items;
    for (const QModelIndex &i : indexes) {
        if (i.column() != 0)
            continue;
        BinderItem *it = itemFor(i);
        if (it && !items.contains(it))
            items << it;
    }
    return mimeForItems(topLevelOnly(items), m_project);
}

bool BinderModel::canMoveInto(const BinderItem *item, const BinderItem *target) const
{
    if (!item || !target || item->isRootFolder())
        return false;
    if (item == target || target->isDescendantOf(item))
        return false;
    // the Draft only holds text documents and folders
    if (m_project->isInDraft(target)) {
        bool ok = true;
        const_cast<BinderItem *>(item)->forEach([&](BinderItem *b) {
            if (!b->hasText())
                ok = false;
        });
        if (!ok)
            return false;
    }
    return true;
}

bool BinderModel::canDropMimeData(const QMimeData *data, Qt::DropAction, int row, int, const QModelIndex &parent) const
{
    BinderItem *target = parent.isValid() ? itemFor(parent) : m_project->root();
    if (!target)
        return false;
    if (data->hasFormat(QString::fromLatin1(kItemsMime))) {
        const QList<BinderItem *> items = itemsFromMime(data, m_project);
        if (items.isEmpty())
            return false;
        if (target == m_project->root()) {
            // only allow items at root level (next to Draft/Research)
        }
        for (BinderItem *it : items)
            if (!canMoveInto(it, target))
                return false;
        // dropped onto a document (not between two): only folders take that if the option is off
        if (row < 0 && parent.isValid() && !dropOnTextDocuments && !target->isFolder())
            return false;
        return true;
    }
    if (target == m_project->root())
        return false;
    return data->hasUrls() || data->hasText();
}

bool BinderModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int, const QModelIndex &parent)
{
    if (action == Qt::IgnoreAction)
        return true;
    BinderItem *target = parent.isValid() ? itemFor(parent) : m_project->root();
    if (!target)
        return false;
    if (row < 0)
        row = target->children.size();
    if (data->hasFormat(QString::fromLatin1(kItemsMime))) {
        const QList<BinderItem *> items = topLevelOnly(itemsFromMime(data, m_project));
        if (items.isEmpty())
            return false;
        if (target == m_project->root() && row >= target->children.size())
            row = target->children.indexOf(m_project->trash()); // keep trash last
        moveItems(items, target, row);
        emit itemsMoved(items);
        return false; // we moved ourselves; stop the view from removing rows
    }
    if (target == m_project->root())
        return false;
    if (data->hasUrls()) {
        QList<QUrl> urls = data->urls();
        emit filesDropped(target, row, urls);
        return false;
    }
    if (data->hasText()) {
        emit textDropped(target, row, data->text(), data->data(QStringLiteral("text/rtf")));
        return false;
    }
    return false;
}

QList<BinderItem *> BinderModel::topLevelOnly(const QList<BinderItem *> &items)
{
    QList<BinderItem *> out;
    for (BinderItem *it : items) {
        bool nested = false;
        for (BinderItem *o : items)
            if (o != it && it->isDescendantOf(o))
                nested = true;
        if (!nested && !out.contains(it))
            out << it;
    }
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

BinderItem *BinderModel::createItem(BinderItem *parent, int row, ItemType type, const QString &title)
{
    if (!parent)
        parent = m_project->draft();
    if (row < 0 || row > parent->children.size())
        row = parent->children.size();
    if (parent == m_project->root() && row >= parent->children.indexOf(m_project->trash()))
        row = parent->children.indexOf(m_project->trash());
    auto *it = new BinderItem;
    it->id = newId();
    it->type = type;
    it->title = title;
    it->parent = parent;
    it->created = it->modified = QDateTime::currentDateTime();
    const QModelIndex pidx = indexFor(parent);
    beginInsertRows(pidx, row, row);
    parent->children.insert(row, it);
    m_project->registerItem(it);
    endInsertRows();
    if (parent->children.size() == 1)
        refresh(parent);
    m_project->markIndexDirty();
    emit itemCreated(it);
    return it;
}

void BinderModel::insertExisting(BinderItem *parent, int row, BinderItem *item)
{
    if (row < 0 || row > parent->children.size())
        row = parent->children.size();
    if (parent == m_project->root() && row > parent->children.indexOf(m_project->trash()))
        row = parent->children.indexOf(m_project->trash());
    item->parent = parent;
    beginInsertRows(indexFor(parent), row, row);
    parent->children.insert(row, item);
    m_project->registerItem(item);
    endInsertRows();
    m_project->markIndexDirty();
}

bool BinderModel::moveItems(const QList<BinderItem *> &itemsIn, BinderItem *newParent, int row)
{
    const QList<BinderItem *> items = topLevelOnly(itemsIn);
    for (BinderItem *it : items) {
        if (!canMoveInto(it, newParent))
            continue;
        BinderItem *oldParent = it->parent;
        const int oldRow = it->row();
        int destRow = qBound(0, row, int(newParent->children.size()));
        if (oldParent == newParent && (destRow == oldRow || destRow == oldRow + 1)) {
            row = oldRow + 1;
            continue;
        }
        if (!beginMoveRows(indexFor(oldParent), oldRow, oldRow, indexFor(newParent), destRow)) {
            continue;
        }
        oldParent->children.removeAt(oldRow);
        if (oldParent == newParent && destRow > oldRow)
            --destRow;
        newParent->children.insert(destRow, it);
        it->parent = newParent;
        endMoveRows();
        row = destRow + 1;
        refresh(oldParent);
        refresh(newParent);
    }
    m_project->markIndexDirty();
    return true;
}

void BinderModel::moveToTrash(const QList<BinderItem *> &items)
{
    QList<BinderItem *> list;
    for (BinderItem *it : topLevelOnly(items))
        if (!it->isRootFolder() && !m_project->isInTrash(it))
            list << it;
    if (list.isEmpty())
        return;
    moveItems(list, m_project->trash(), m_project->trash()->children.size());
    refresh(m_project->trash());
}

void BinderModel::deletePermanently(BinderItem *item)
{
    if (!item || item->isRootFolder())
        return;
    BinderItem *parent = item->parent;
    const int row = item->row();
    // remove content of the whole subtree
    item->forEach([this](BinderItem *b) {
        m_project->docs()->removeDocument(b->id);
        QDir(m_project->snapshotsDir(b->id)).removeRecursively();
    });
    beginRemoveRows(indexFor(parent), row, row);
    parent->children.removeAt(row);
    m_project->unregisterItem(item);
    endRemoveRows();
    delete item;
    refresh(parent);
    m_project->markIndexDirty();
}

void BinderModel::emptyTrash()
{
    BinderItem *t = m_project->trash();
    while (!t->children.isEmpty())
        deletePermanently(t->children.last());
    refresh(t);
}

void BinderModel::setItemType(BinderItem *item, ItemType type)
{
    item->type = type;
    m_project->notifyItemChanged(item, false);
}

void BinderModel::refresh(BinderItem *item)
{
    if (!item || item == m_project->root())
        return;
    const QModelIndex a = indexFor(item, 0);
    const QModelIndex b = indexFor(item, columnCount() - 1);
    if (a.isValid())
        emit dataChanged(a, b);
}

void BinderModel::refreshAll()
{
    icons::clearCache();
    for (BinderItem *it : m_project->allItems())
        refresh(it);
}

void BinderModel::customFieldsChanged()
{
    beginResetModel();
    endResetModel();
}

void BinderModel::onItemChanged(BinderItem *item) { refresh(item); }

} // namespace qb
