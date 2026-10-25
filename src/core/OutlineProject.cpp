#include "core/OutlineProject.h"

#include "compile/CompileFormat.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/MindMap.h"
#include "core/Objects.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "core/Snapshots.h"
#include "text/DisplayFormat.h"
#include "text/MarkdownIO.h"
#include "text/Script.h"
#include "text/TextProps.h"

#include "AppInfo.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPageSize>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QUrl>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>

namespace qb {
namespace outline {

namespace {

// Everything the outline is built with; one per build (no statics: the program
// may build the welcome project more than once in a session).
struct Builder
{
    Builder(Project *p, const QString &baseDir, Result *r) : m_p(p), m_base(baseDir), m_r(r) {}

    QString readText(const QString &rel)
    {
        QFile f(QDir(m_base).filePath(rel));
        if (!f.open(QIODevice::ReadOnly)) {
            m_r->errors << QStringLiteral("cannot read %1").arg(f.fileName());
            return QString();
        }
        return fill(QString::fromUtf8(f.readAll())).trimmed();
    }

    static QString fill(QString s) { return s.replace(QLatin1String("{{APP}}"), QLatin1String(QB_APP_NAME)); }

    static ItemType typeOf(const QString &t)
    {
        if (t == QLatin1String("folder"))
            return ItemType::Folder;
        if (t == QLatin1String("image"))
            return ItemType::Image;
        if (t == QLatin1String("pdf"))
            return ItemType::Pdf;
        if (t == QLatin1String("media"))
            return ItemType::Media;
        if (t == QLatin1String("webpage"))
            return ItemType::WebPage;
        if (t == QLatin1String("file"))
            return ItemType::File;
        return ItemType::Text;
    }

    void addItems(BinderItem *parent, const QJsonArray &items)
    {
        for (const QJsonValue &v : items) {
            const QJsonObject o = v.toObject();
            const ItemType type = typeOf(o.value(QStringLiteral("type")).toString());
            BinderItem *it = m_p->model()->createItem(parent, -1, type, fill(o.value(QStringLiteral("title")).toString()));
            it->icon = o.value(QStringLiteral("icon")).toString();
            it->synopsis = fill(o.value(QStringLiteral("synopsis")).toString());
            if (o.contains(QStringLiteral("file"))) {
                // a research file: copied into the project
                const QString src = QDir(m_base).filePath(o.value(QStringLiteral("file")).toString());
                it->fileExt = QFileInfo(src).suffix().toLower();
                it->originalFileName = QFileInfo(src).fileName();
                QDir().mkpath(m_p->contentDir(it->id));
                if (!QFile::copy(src, m_p->mediaFilePath(it)))
                    m_r->errors << QStringLiteral("cannot copy %1").arg(src);
                else
                    QFile(m_p->mediaFilePath(it)).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup
                                                               | QFile::ReadOther); // resources are read-only
            }
            if (o.contains(QStringLiteral("makeFrom")) && type == ItemType::Pdf) {
                // a PDF made here from an HTML page (pages split by "page-break-before: always")
                it->fileExt = QStringLiteral("pdf");
                QDir().mkpath(m_p->contentDir(it->id));
                QPdfWriter w(m_p->mediaFilePath(it));
                w.setPageSize(QPageSize(QPageSize::A5));
                w.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
                w.setTitle(it->title);
                w.setCreator(QStringLiteral(QB_APP_NAME));
                QTextDocument d;
                QFont f(QStringLiteral("DejaVu Sans"));
                f.setPointSizeF(10);
                d.setDefaultFont(f);
                d.setHtml(readText(o.value(QStringLiteral("makeFrom")).toString()));
                d.print(&w);
            }
            QString text = fill(o.value(QStringLiteral("text")).toString());
            if (o.contains(QStringLiteral("textFile")))
                text = readText(o.value(QStringLiteral("textFile")).toString());
            if (!text.isEmpty())
                templates::setMarkdown(m_p, it, text);
            QString notes = fill(o.value(QStringLiteral("notes")).toString());
            if (o.contains(QStringLiteral("notesFile")))
                notes = readText(o.value(QStringLiteral("notesFile")).toString());
            if (!notes.isEmpty()) {
                QTextDocument tmp;
                md::read(notes, &tmp, m_p->formatting);
                m_p->docs()->setNotesContent(it->id, &tmp);
            }
            if (o.contains(QStringLiteral("includeInCompile")))
                it->includeInCompile = o.value(QStringLiteral("includeInCompile")).toBool();
            if (o.contains(QStringLiteral("keywords")) || o.contains(QStringLiteral("bookmarks"))
                || o.contains(QStringLiteral("snapshots")))
                m_pendingItems.append({it, o});
            if (o.contains(QStringLiteral("mindmap")))
                m_boards.append({it, o.value(QStringLiteral("mindmap")).toObject()});
            if (o.value(QStringLiteral("script")).toBool())
                addScript(it, o.value(QStringLiteral("elements")).toArray());
            const QString label = o.value(QStringLiteral("label")).toString();
            for (const Label &l : m_p->labels)
                if (l.name == label)
                    it->label = l.id;
            const QString status = o.value(QStringLiteral("status")).toString();
            for (const Status &s : m_p->statuses)
                if (s.name == status)
                    it->status = s.id;
            it->sectionType = m_types.value(o.value(QStringLiteral("sectionType")).toString());
            if (o.contains(QStringLiteral("defaultTemplate")))
                m_pendingTemplates.append({it, o.value(QStringLiteral("defaultTemplate")).toString()});
            if (m_byTitle.contains(it->title))
                m_r->warnings << QStringLiteral("the title \"%1\" is used twice").arg(it->title);
            m_byTitle.insert(it->title, it);
            addItems(it, o.value(QStringLiteral("children")).toArray());
        }
    }

    void addScript(BinderItem *it, const QJsonArray &els)
    {
        if (els.isEmpty()) {
            m_p->setScriptMode(it, true);
            return;
        }
        QTextDocument tmp;
        QTextCursor c(&tmp);
        bool first = true;
        for (const QJsonValue &ev : els) {
            const QJsonArray pair = ev.toArray();
            const script::Element *el = script::format(m_p->formatting).find(pair.at(0).toString());
            if (!el && !pair.at(0).toString().isEmpty())
                m_r->warnings << QStringLiteral("no element \"%1\" in the script format (%2)").arg(pair.at(0).toString(), it->title);
            const QString style = el ? script::styleName(el->name) : QString();
            const QTextCharFormat cf = display::charFormatFor(m_p->formatting, style);
            if (first)
                c.setBlockFormat(display::blockFormatFor(m_p->formatting, style));
            else
                c.insertBlock(display::blockFormatFor(m_p->formatting, style));
            c.setBlockCharFormat(cf);
            first = false;
            c.insertText(pair.at(1).toString(), cf);
        }
        m_p->docs()->setContent(it->id, &tmp);
        it->scriptMode = true;
    }

    void addKeywords(const QJsonArray &list, const QString &parentId)
    {
        for (const QJsonValue &v : list) {
            const QJsonObject o = v.toObject();
            Keyword k;
            k.id = newId();
            k.name = o.value(QStringLiteral("name")).toString();
            k.color = QColor(o.value(QStringLiteral("color")).toString(QStringLiteral("#8a8a8a")));
            k.parentId = parentId;
            m_p->keywords.append(k);
            addKeywords(o.value(QStringLiteral("children")).toArray(), k.id);
        }
    }

    BinderItem *byTitle(const QString &title, const QString &where)
    {
        BinderItem *it = m_byTitle.value(fill(title));
        if (!it)
            m_r->errors << QStringLiteral("no document \"%1\" (%2)").arg(title, where);
        return it;
    }

    // keywords, bookmarks, snapshots, and links to documents by title
    void finishItems()
    {
        for (const auto &pi : std::as_const(m_pendingItems)) {
            BinderItem *it = pi.first;
            const QJsonObject &o = pi.second;
            for (const QJsonValue &k : o.value(QStringLiteral("keywords")).toArray()) {
                bool found = false;
                for (const Keyword &kw : m_p->keywords)
                    if (kw.name == k.toString()) {
                        it->keywords << kw.id;
                        found = true;
                    }
                if (!found)
                    m_r->errors << QStringLiteral("no keyword \"%1\"").arg(k.toString());
            }
            for (const QJsonValue &b : o.value(QStringLiteral("bookmarks")).toArray())
                if (BinderItem *t = byTitle(b.toString(), it->title))
                    it->bookmarks.append({t->title, QStringLiteral("qb://") + t->id});
            for (const QJsonValue &sv : o.value(QStringLiteral("snapshots")).toArray()) {
                const QJsonObject so = sv.toObject();
                const QString md = so.contains(QStringLiteral("textFile")) ? readText(so.value(QStringLiteral("textFile")).toString())
                                                                             : fill(so.value(QStringLiteral("text")).toString());
                QTextDocument tmp;
                md::read(md, &tmp, m_p->formatting);
                QString err;
                if (!snapshots::take(m_p, it->id, so.value(QStringLiteral("title")).toString(), &err, &tmp))
                    m_r->errors << QStringLiteral("snapshot of %1: %2").arg(it->title, err);
                it->hasSnapshots = true;
            }
        }
    }

    // [words](qb-title:Title) -> a link to that document; [words](qbobj-name:Name) -> to that object
    void resolveLinks()
    {
        for (BinderItem *it : m_p->allItems()) {
            if (!it->hasText())
                continue;
            QTextDocument *doc = m_p->docs()->text(it->id);
            QList<QPair<QPair<int, int>, QString>> links;
            for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
                for (auto f = b.begin(); !f.atEnd(); ++f) {
                    const QTextCharFormat cf = f.fragment().charFormat();
                    if (cf.isAnchor()
                        && (cf.anchorHref().startsWith(QLatin1String("qb-title:"))
                            || cf.anchorHref().startsWith(QLatin1String("qbobj-name:"))))
                        links.append({{f.fragment().position(), f.fragment().length()}, cf.anchorHref()});
                }
            for (const auto &l : std::as_const(links)) {
                QString href;
                if (l.second.startsWith(QLatin1String("qb-title:"))) {
                    if (BinderItem *t = byTitle(QUrl::fromPercentEncoding(l.second.mid(9).toUtf8()), it->title + QStringLiteral(" (link)")))
                        href = QStringLiteral("qb://") + t->id;
                } else {
                    const QString name = QUrl::fromPercentEncoding(l.second.mid(11).toUtf8());
                    if (m_objects.contains(name))
                        href = objects::linkTo(m_objects.value(name));
                    else
                        m_r->errors << QStringLiteral("no object \"%1\" (%2)").arg(name, it->title);
                }
                if (href.isEmpty())
                    continue;
                QTextCursor c(doc);
                c.setPosition(l.first.first);
                c.setPosition(l.first.first + l.first.second, QTextCursor::KeepAnchor);
                QTextCharFormat cf;
                cf.setAnchorHref(href);
                c.mergeCharFormat(cf); // contentsChanged marks the document as changed
            }
        }
    }

    static QColor colorOf(const QJsonObject &o, const char *key)
    {
        const QString s = o.value(QLatin1String(key)).toString();
        return s.isEmpty() ? QColor() : QColor(s);
    }

    static int iconPos(const QString &k)
    {
        static const QStringList names = {QStringLiteral("topLeft"), QStringLiteral("top"),    QStringLiteral("topRight"),
                                          QStringLiteral("right"),   QStringLiteral("bottomRight"), QStringLiteral("bottom"),
                                          QStringLiteral("bottomLeft"), QStringLiteral("left")};
        return int(names.indexOf(k));
    }

    void readLook(const QJsonObject &o, mindmap::Appearance &look)
    {
        look.fill = colorOf(o, "fill");
        look.textColor = colorOf(o, "textColor");
        look.borderColor = colorOf(o, "borderColor");
        look.borderWeight = o.value(QStringLiteral("border")).toInt(0);
        const QString shape = o.value(QStringLiteral("shape")).toString();
        if (shape == QLatin1String("square"))
            look.borderStyle = mindmap::BorderStyle::Square;
        else if (shape == QLatin1String("jagged"))
            look.borderStyle = mindmap::BorderStyle::Jagged;
        else if (shape == QLatin1String("cloud"))
            look.borderStyle = mindmap::BorderStyle::Cloud;
        if (look.borderStyle != mindmap::BorderStyle::Rounded && look.borderWeight == 0)
            look.borderWeight = 1; // a shape shows only with a border
        look.shadow = o.value(QStringLiteral("shadow")).toBool();
        look.faded = o.value(QStringLiteral("faded")).toBool();
    }

    void readIcons(const QJsonObject &o, mindmap::IconMap &icons, const QString &where)
    {
        const QJsonObject ic = o.value(QStringLiteral("icons")).toObject();
        for (auto i = ic.begin(); i != ic.end(); ++i) {
            const int pos = iconPos(i.key());
            if (pos < 0) {
                m_r->errors << QStringLiteral("no icon place \"%1\" (%2)").arg(i.key(), where);
                continue;
            }
            icons.insert(pos, i.value().toString());
        }
    }

    // the mind maps of the items, as files in their content folders
    void writeBoards()
    {
        for (const auto &b : std::as_const(m_boards)) {
            BinderItem *it = b.first;
            const QJsonObject &o = b.second;
            mindmap::Board board;
            if (o.contains(QStringLiteral("background")))
                board.settings.ownLight = colorOf(o, "background");
            for (const QJsonValue &nv : o.value(QStringLiteral("notes")).toArray()) {
                const QJsonObject no = nv.toObject();
                mindmap::Note n;
                n.id = no.value(QStringLiteral("id")).toInt();
                n.pos = QPointF(no.value(QStringLiteral("x")).toDouble(), no.value(QStringLiteral("y")).toDouble());
                n.width = no.value(QStringLiteral("width")).toDouble(160);
                n.fontSize = no.value(QStringLiteral("fontSize")).toDouble(12);
                n.text = fill(no.value(QStringLiteral("text")).toString());
                n.bold = no.value(QStringLiteral("bold")).toBool();
                n.italic = no.value(QStringLiteral("italic")).toBool();
                if (no.value(QStringLiteral("align")).toString() == QLatin1String("center"))
                    n.align = mindmap::Align::Center;
                readLook(no, n.look);
                readIcons(no, n.icons, it->title);
                if (n.id <= 0 || board.exists(n.id))
                    m_r->errors << QStringLiteral("mind map of %1: note id %2 missing or used twice").arg(it->title).arg(n.id);
                board.notes << n;
            }
            for (const QJsonValue &sv : o.value(QStringLiteral("shapes")).toArray()) {
                const QJsonObject so = sv.toObject();
                mindmap::Shape s;
                s.id = so.value(QStringLiteral("id")).toInt();
                s.rect = QRectF(so.value(QStringLiteral("x")).toDouble(), so.value(QStringLiteral("y")).toDouble(),
                                so.value(QStringLiteral("w")).toDouble(200), so.value(QStringLiteral("h")).toDouble(120));
                s.magnetic = so.value(QStringLiteral("magnetic")).toBool(true);
                readLook(so, s.look);
                readIcons(so, s.icons, it->title);
                if (s.id <= 0 || board.exists(s.id))
                    m_r->errors << QStringLiteral("mind map of %1: shape id %2 missing or used twice").arg(it->title).arg(s.id);
                board.shapes << s;
            }
            for (const QJsonValue &lv : o.value(QStringLiteral("lines")).toArray()) {
                const QJsonObject lo = lv.toObject();
                const int a = lo.value(QStringLiteral("from")).toInt(), z = lo.value(QStringLiteral("to")).toInt();
                if (!board.exists(a) || !board.exists(z)) {
                    m_r->errors << QStringLiteral("mind map of %1: a line between %2 and %3, one of them missing").arg(it->title).arg(a).arg(z);
                    continue;
                }
                const QString arrow = lo.value(QStringLiteral("arrow")).toString();
                board.connect(a, z, arrow == QLatin1String("to") || arrow == QLatin1String("both"),
                              arrow == QLatin1String("from") || arrow == QLatin1String("both"));
                if (lo.contains(QStringLiteral("label")))
                    board.setLineLabel(a, z, fill(lo.value(QStringLiteral("label")).toString()));
                mindmap::LineLook look;
                look.color = colorOf(lo, "color");
                look.icon = lo.value(QStringLiteral("icon")).toString();
                if (!look.isEmpty())
                    board.setLineLook(a, z, look);
            }
            QString err;
            QDir().mkpath(m_p->contentDir(it->id));
            if (!board.save(mindmap::pathFor(m_p->contentDir(it->id)), &err))
                m_r->errors << QStringLiteral("mind map of %1: %2").arg(it->title, err);
        }
    }

    // objects: made first, then their links (to other objects by name, to documents by title)
    void addObjects(const QJsonArray &list)
    {
        if (list.isEmpty())
            return;
        objects::Store *st = m_p->objects();
        if (m_standardObjects)
            st->setSchema(objects::builtInSchema()); // not the writer's own templates for new projects
        QList<QPair<QString, QJsonObject>> made;
        for (const QJsonValue &v : list) {
            const QJsonObject o = v.toObject();
            const QString cat = o.value(QStringLiteral("category")).toString();
            if (!st->schema().category(cat)) {
                m_r->errors << QStringLiteral("no object category \"%1\"").arg(cat);
                continue;
            }
            QString folder;
            const QString folderName = o.value(QStringLiteral("folder")).toString();
            if (!folderName.isEmpty()) {
                const QString key = cat + QLatin1Char('/') + folderName;
                if (!m_folders.contains(key))
                    m_folders.insert(key, st->addFolder(cat, QString(), folderName));
                folder = m_folders.value(key);
            }
            const QString name = fill(o.value(QStringLiteral("name")).toString());
            const QString id = st->create(cat, folder, name);
            if (m_objects.contains(name))
                m_r->warnings << QStringLiteral("the object name \"%1\" is used twice").arg(name);
            m_objects.insert(name, id);
            made.append({id, o});
        }
        for (const auto &m : std::as_const(made)) {
            const objects::Object *found = st->object(m.first);
            if (!found)
                continue;
            objects::Object obj = *found;
            const objects::Category *c = st->schema().category(obj.category);
            const QJsonObject vals = m.second.value(QStringLiteral("values")).toObject();
            const QString col = m.second.value(QStringLiteral("color")).toString();
            if (!col.isEmpty())
                obj.color = QColor(col);
            for (auto i = vals.begin(); i != vals.end(); ++i) {
                const objects::FieldDef *f = c ? c->field(i.key()) : nullptr;
                if (!f) {
                    m_r->errors << QStringLiteral("object %1: no field \"%2\"").arg(obj.name, i.key());
                    continue;
                }
                switch (f->type) {
                case objects::FieldType::YesNo:
                    obj.values.insert(f->id, i.value().toBool());
                    break;
                case objects::FieldType::Objects: {
                    QStringList ids;
                    for (const QJsonValue &n : i.value().toArray()) {
                        if (m_objects.contains(n.toString()))
                            ids << m_objects.value(n.toString());
                        else
                            m_r->errors << QStringLiteral("object %1: no object \"%2\"").arg(obj.name, n.toString());
                    }
                    obj.values.insert(f->id, ids);
                    break;
                }
                case objects::FieldType::Documents: {
                    QStringList ids;
                    for (const QJsonValue &t : i.value().toArray())
                        if (BinderItem *it = byTitle(t.toString(), obj.name))
                            ids << it->id;
                    obj.values.insert(f->id, ids);
                    break;
                }
                case objects::FieldType::Picture:
                    m_r->errors << QStringLiteral("object %1: pictures are not supported in outlines").arg(obj.name);
                    break;
                default: {
                    QString t = fill(i.value().toVariant().toString());
                    // [words](qbobj-name:Name) in long texts
                    static const QRegularExpression rx(QStringLiteral("\\(qbobj-name:([^)]+)\\)"));
                    QRegularExpressionMatchIterator mi = rx.globalMatch(t);
                    QList<QRegularExpressionMatch> found;
                    while (mi.hasNext())
                        found.prepend(mi.next());
                    for (const QRegularExpressionMatch &rm : std::as_const(found)) {
                        const QString name = rm.captured(1);
                        if (!m_objects.contains(name)) {
                            m_r->errors << QStringLiteral("object %1: no object \"%2\"").arg(obj.name, name);
                            continue;
                        }
                        t.replace(rm.capturedStart(), rm.capturedLength(),
                                  QLatin1Char('(') + objects::linkTo(m_objects.value(name)) + QLatin1Char(')'));
                    }
                    obj.values.insert(f->id, t);
                    break;
                }
                }
            }
            st->update(obj);
        }
    }

    Project *m_p;
    QString m_base;
    Result *m_r;
    QHash<QString, QString> m_types;          // section type name -> id
    QHash<QString, BinderItem *> m_byTitle;   // for links, bookmarks, templates
    QHash<QString, QString> m_objects;        // object name -> id
    QHash<QString, QString> m_folders;        // "category/folder name" -> folder id
    bool m_standardObjects = false;           // "objectTemplates": "standard"
    QList<QPair<BinderItem *, QString>> m_pendingTemplates;
    QList<QPair<BinderItem *, QJsonObject>> m_pendingItems; // keywords, bookmarks, snapshots: after all items exist
    QList<QPair<BinderItem *, QJsonObject>> m_boards;

};

} // namespace

QJsonObject read(const QString &path, QString *error)
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot read %1").arg(path);
        return {};
    }
    QJsonParseError pe;
    const QJsonObject o = QJsonDocument::fromJson(in.readAll(), &pe).object();
    if (pe.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, pe.errorString());
        return {};
    }
    return o;
}

Result build(const QJsonObject &o, const QString &baseDir, const QString &parentDir, const QString &name)
{
    Result r;
    QString err;
    auto p = Project::create(parentDir, name, &err);
    if (!p) {
        r.errors << err;
        return r;
    }
    Builder b(p.get(), baseDir, &r);
    if (o.contains(QStringLiteral("labelTitle")))
        p->labelTitle = o.value(QStringLiteral("labelTitle")).toString();
    if (o.contains(QStringLiteral("labels"))) {
        p->labels.clear();
        int id = 1;
        for (const QJsonValue &v : o.value(QStringLiteral("labels")).toArray())
            p->labels.append({id++, v.toObject().value(QStringLiteral("name")).toString(),
                              QColor(v.toObject().value(QStringLiteral("color")).toString())});
    }
    if (o.contains(QStringLiteral("statuses"))) {
        p->statuses.clear();
        int id = 1;
        for (const QJsonValue &v : o.value(QStringLiteral("statuses")).toArray())
            p->statuses.append({id++, v.toString()});
    }
    if (o.contains(QStringLiteral("sectionTypes"))) {
        p->sectionTypes.clear();
        for (const QJsonValue &v : o.value(QStringLiteral("sectionTypes")).toArray()) {
            const QString id = newId();
            p->sectionTypes.append({id, v.toString()});
            b.m_types.insert(v.toString(), id);
        }
        p->structureDefaults = StructureDefaults();
        for (const QJsonValue &v : o.value(QStringLiteral("folderTypes")).toArray())
            p->structureDefaults.folders << b.m_types.value(v.toString());
        for (const QJsonValue &v : o.value(QStringLiteral("fileTypes")).toArray()) {
            p->structureDefaults.files << b.m_types.value(v.toString());
            p->structureDefaults.fileGroups << b.m_types.value(v.toString());
        }
    }
    if (o.contains(QStringLiteral("scriptFormat"))) {
        const QString wanted = o.value(QStringLiteral("scriptFormat")).toString();
        script::Format sf;
        bool found = false;
        for (const script::Entry &e : script::available())
            if (e.format.name == wanted) {
                sf = e.format;
                found = true;
            }
        QString ferr;
        if (!found)
            found = script::readFile(QDir(baseDir).filePath(wanted), &sf, &ferr);
        if (!found) {
            r.errors << QStringLiteral("script format \"%1\" not found %2").arg(wanted, ferr);
        } else if (sf != script::standard()) {
            p->formatting = script::withFormat(p->formatting, sf);
        }
    }
    if (o.contains(QStringLiteral("compileFormat")))
        p->compile.formatName = o.value(QStringLiteral("compileFormat")).toString();
    if (o.contains(QStringLiteral("draftTitle")))
        p->draft()->title = o.value(QStringLiteral("draftTitle")).toString();
    b.addKeywords(o.value(QStringLiteral("keywords")).toArray(), QString());
    // the template's own empty document goes, the Draft gets what the outline says
    if (o.contains(QStringLiteral("rootFirst")) || o.value(QStringLiteral("emptyDraft")).toBool())
        for (BinderItem *c : QList<BinderItem *>(p->draft()->children))
            p->model()->deletePermanently(c);

    b.addItems(p->draft(), o.value(QStringLiteral("draft")).toArray());
    b.addItems(p->research(), o.value(QStringLiteral("research")).toArray());
    b.addItems(p->root(), o.value(QStringLiteral("root")).toArray());
    {
        // new items at the top level go before the Trash; these are moved above the Draft
        const QList<BinderItem *> before = p->root()->children;
        b.addItems(p->root(), o.value(QStringLiteral("rootFirst")).toArray());
        QList<BinderItem *> first;
        for (BinderItem *it : p->root()->children)
            if (!before.contains(it))
                first << it;
        if (!first.isEmpty())
            p->model()->moveItems(first, p->root(), 0);
    }
    b.m_standardObjects = o.value(QStringLiteral("objectTemplates")).toString() == QLatin1String("standard");
    b.addObjects(o.value(QStringLiteral("objects")).toArray());
    b.finishItems();
    b.resolveLinks();
    b.writeBoards();
    for (const QJsonValue &bm : o.value(QStringLiteral("projectBookmarks")).toArray())
        if (BinderItem *t = b.byTitle(bm.toString(), QStringLiteral("project bookmarks")))
            p->projectBookmarks.append({t->title, QStringLiteral("qb://") + t->id});
    for (const QJsonValue &cv : o.value(QStringLiteral("collections")).toArray()) {
        const QJsonObject co = cv.toObject();
        Collection c;
        c.id = newId();
        c.name = Builder::fill(co.value(QStringLiteral("name")).toString());
        c.color = QColor(co.value(QStringLiteral("color")).toString(QStringLiteral("#5a8fd8")));
        for (const QJsonValue &t : co.value(QStringLiteral("items")).toArray())
            if (BinderItem *it = b.byTitle(t.toString(), c.name))
                c.items << it->id;
        p->collections.append(c);
    }
    if (BinderItem *tf = b.m_byTitle.value(o.value(QStringLiteral("templatesFolder")).toString()))
        p->templatesFolderId = tf->id;
    for (const auto &pt : std::as_const(b.m_pendingTemplates))
        if (BinderItem *t = b.m_byTitle.value(pt.second))
            pt.first->defaultTemplate = t->id;
    if (!r.errors.isEmpty()) {
        const QString folder = p->folderPath();
        p.reset();
        QDir(folder).removeRecursively();
        return r;
    }
    if (!p->save(&err)) {
        r.errors << err;
        return r;
    }
    r.project = std::move(p);
    return r;
}

} // namespace outline
} // namespace qb
