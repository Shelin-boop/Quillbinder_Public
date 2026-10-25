#pragma once
#include "core/BinderItem.h"

#include <QAbstractItemModel>
#include <QMimeData>
#include <QPointer>
#include <QUrl>
#include <functional>

namespace qb {

class Project;

inline constexpr const char *kItemsMime = "application/x-quillbinder-items";

// What a drag of documents carries: the documents themselves (for every place
// inside the program), their titles as text and, for other programs (file
// manager, desktop), files. The files are only written when a program asks
// for them: fileProvider (set by the user interface) exports the documents
// into a temporary folder.
class ItemsMimeData : public QMimeData
{
public:
    ItemsMimeData(Project *project, const QStringList &ids);
    QStringList formats() const override;
    bool hasFormat(const QString &mimeType) const override;
    QStringList ids() const { return m_ids; }
    static std::function<QList<QUrl>(Project *, const QStringList &)> fileProvider;

protected:
    QVariant retrieveData(const QString &mimeType, QMetaType type) const override;

private:
    QPointer<Project> m_project;
    QStringList m_ids;
    mutable bool m_exported = false;
    mutable QByteArray m_uriList;
};

class BinderModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Column {
        ColTitle = 0,
        ColLabel,
        ColStatus,
        ColKeywords,
        ColWords,
        ColChars,
        ColTarget,
        ColProgress,
        ColCreated,
        ColModified,
        ColCompile,
        ColSection,
        ColFirstCustom
    };
    enum Roles {
        ItemRole = Qt::UserRole + 1,
        IdRole,
        SynopsisRole,
        LabelColorRole,
        TypeRole,
        PreviewRole,   // synopsis, or the first lines of the text
        ProgressRole,  // 0..1 (-1 = no target)
        SortRole
    };

    explicit BinderModel(Project *project);

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction | Qt::CopyAction; }
    Qt::DropActions supportedDragActions() const override { return Qt::MoveAction | Qt::CopyAction; }
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override;

    QModelIndex indexFor(const BinderItem *item, int column = 0) const;
    BinderItem *itemFor(const QModelIndex &index) const;
    Project *project() const { return m_project; }

    // ---- operations (all undo-less; the project autosaves) --------------
    BinderItem *createItem(BinderItem *parent, int row, ItemType type, const QString &title);
    void insertExisting(BinderItem *parent, int row, BinderItem *item);
    bool moveItems(const QList<BinderItem *> &items, BinderItem *newParent, int row);
    void moveToTrash(const QList<BinderItem *> &items);
    void deletePermanently(BinderItem *item);
    void emptyTrash();
    void setItemType(BinderItem *item, ItemType type);
    void refresh(BinderItem *item);
    void refreshAll();
    void customFieldsChanged();
    QString customFieldAt(int column) const;

    bool canMoveInto(const BinderItem *item, const BinderItem *target) const;
    static QList<BinderItem *> topLevelOnly(const QList<BinderItem *> &items);
    static QList<BinderItem *> itemsFromMime(const QMimeData *data, Project *project);
    static QMimeData *mimeForItems(const QList<BinderItem *> &items, Project *project);
    // File > Options > Behaviors: documents may be dropped onto text documents
    // (which makes them subdocuments); folders always take drops.
    static bool dropOnTextDocuments;

signals:
    void filesDropped(qb::BinderItem *parent, int row, const QList<QUrl> &urls);
    // text dragged in from an editor or another program (rtf: empty if it came as plain text only)
    void textDropped(qb::BinderItem *parent, int row, const QString &text, const QByteArray &rtf);
    void itemsMoved(const QList<qb::BinderItem *> &items);
    void itemCreated(qb::BinderItem *item);

private:
    Project *m_project;
    void onItemChanged(BinderItem *item);
};

} // namespace qb
