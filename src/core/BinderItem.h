#pragma once
#include "core/ProjectTypes.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>

namespace qb {

class BinderItem
{
public:
    QString id;
    ItemType type = ItemType::Text;
    QString title;
    BinderItem *parent = nullptr;
    QList<BinderItem *> children;

    int label = -1;
    int status = -1;
    bool includeInCompile = true;
    QString sectionType;           // empty = structure based
    QStringList keywords;          // keyword ids
    QMap<QString, QString> custom; // custom field id -> value
    QDateTime created;
    QDateTime modified;
    QString icon;                  // custom icon name ("" = default)
    QString synopsis;
    QString synopsisImage;         // file name inside the content folder
    QString fileExt;               // media extension ("png", "pdf" ...)
    QString originalFileName;
    int target = 0;                // document target
    bool targetChars = false;
    QString defaultTemplate;       // template id for new subdocuments
    bool scriptMode = false;
    // the title is the first line of the text and follows it until the title is changed by
    // hand (0.40.3; given to documents without a name, "Untitled")
    bool autoTitle = false;
    QList<Bookmark> bookmarks;     // document bookmarks
    QString corkboardPos;          // "x,y" for freeform corkboard
    // cached statistics (kept in the project file so they are known without
    // loading every document)
    int wordCount = 0;
    int charCount = 0;
    bool hasSnapshots = false;

    ~BinderItem() { qDeleteAll(children); }

    bool isRootFolder() const { return type == ItemType::Draft || type == ItemType::Research || type == ItemType::Trash; }
    bool isFolder() const { return isFolderType(type); }
    bool hasText() const { return isTextType(type); }
    bool isContainer() const { return isFolder() || !children.isEmpty(); }
    int row() const { return parent ? int(parent->children.indexOf(const_cast<BinderItem *>(this))) : 0; }
    int level() const
    {
        int l = 0;
        for (BinderItem *p = parent; p && p->parent; p = p->parent)
            ++l;
        return l;
    }
    bool isDescendantOf(const BinderItem *other) const
    {
        for (const BinderItem *p = parent; p; p = p->parent)
            if (p == other)
                return true;
        return false;
    }
    template<typename F>
    void forEach(F f)
    {
        f(this);
        for (BinderItem *c : children)
            c->forEach(f);
    }
    QList<BinderItem *> descendants() const
    {
        QList<BinderItem *> out;
        for (BinderItem *c : children) {
            out << c;
            out << c->descendants();
        }
        return out;
    }
};

} // namespace qb
