#include "ui/DragDrop.h"

#include "core/Snapshots.h"

#include "text/ScriptIO.h"
#include "AppInfo.h"
#include "compile/Compiler.h"
#include "compile/DocxWriter.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/ItemIcons.h"
#include "text/MarkdownIO.h"
#include "text/RtfIO.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"
#include "core/Project.h"
#include "ui/AppSettings.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QImage>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <memory>
#include <vector>

namespace qb {
namespace dnd {

// ------------------------------------------------------------------ documents

QPixmap dragPixmap(Project *project, const QList<BinderItem *> &items)
{
    if (items.isEmpty())
        return QPixmap();
    const QFont font = QApplication::font();
    const QFontMetrics fm(font);
    QString text = items.first()->title.isEmpty() ? QObject::tr("Untitled") : items.first()->title;
    if (items.size() > 1)
        text += QObject::tr("  +%1").arg(items.size() - 1);
    text = fm.elidedText(text, Qt::ElideRight, 240);
    const int h = qMax(22, fm.height() + 8);
    const int w = 8 + 16 + 6 + fm.horizontalAdvance(text) + 10;
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pm(QSize(w, h) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = QApplication::palette();
    QColor back = pal.color(QPalette::Highlight);
    back.setAlpha(215);
    p.setPen(Qt::NoPen);
    p.setBrush(back);
    p.drawRoundedRect(QRectF(0.5, 0.5, w - 1, h - 1), 5, 5);
    icons::forItem(project, items.first()).paint(&p, QRect(8, (h - 16) / 2, 16, 16));
    p.setPen(pal.color(QPalette::HighlightedText));
    p.setFont(font);
    p.drawText(QRect(30, 0, w - 36, h), Qt::AlignVCenter | Qt::AlignLeft, text);
    return pm;
}

Qt::DropAction startItemDrag(QWidget *source, Project *project, const QList<BinderItem *> &items)
{
    const QList<BinderItem *> top = BinderModel::topLevelOnly(items);
    if (!project || top.isEmpty())
        return Qt::IgnoreAction;
    auto *drag = new QDrag(source);
    drag->setMimeData(BinderModel::mimeForItems(top, project));
    drag->setPixmap(dragPixmap(project, top));
    return drag->exec(Qt::MoveAction | Qt::CopyAction | Qt::LinkAction, Qt::MoveAction);
}

namespace {

class DragHandle : public QObject
{
public:
    DragHandle(QWidget *w, std::function<Project *()> project, std::function<QList<BinderItem *>()> items)
        : QObject(w), m_widget(w), m_project(std::move(project)), m_items(std::move(items))
    {
        w->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(e);
            m_pressed = me->button() == Qt::LeftButton;
            m_pressPos = me->pos();
        } else if (e->type() == QEvent::MouseButtonRelease) {
            m_pressed = false;
        } else if (e->type() == QEvent::MouseMove && m_pressed) {
            auto *me = static_cast<QMouseEvent *>(e);
            if ((me->buttons() & Qt::LeftButton)
                && (me->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
                m_pressed = false;
                Project *p = m_project ? m_project() : nullptr;
                const QList<BinderItem *> items = m_items ? m_items() : QList<BinderItem *>();
                if (p && !items.isEmpty()) {
                    // the press must not become a click when the drag is over
                    if (auto *b = qobject_cast<QAbstractButton *>(m_widget))
                        b->setDown(false);
                    startItemDrag(m_widget, p, items);
                    return true;
                }
            }
        }
        return QObject::eventFilter(o, e);
    }

private:
    QWidget *m_widget;
    std::function<Project *()> m_project;
    std::function<QList<BinderItem *>()> m_items;
    bool m_pressed = false;
    QPoint m_pressPos;
};

} // namespace

void makeDragHandle(QAbstractButton *button, std::function<Project *()> project,
                    std::function<QList<BinderItem *>()> items)
{
    new DragHandle(button, std::move(project), std::move(items));
}

void makeDragHandle(QWidget *widget, std::function<Project *()> project, std::function<QList<BinderItem *>()> items)
{
    new DragHandle(widget, std::move(project), std::move(items));
}

// ------------------------------------------------------------------ drop target

DropTarget::DropTarget(QWidget *widget, std::function<bool(const QMimeData *)> accepts,
                       std::function<void(const QMimeData *, const QPoint &, Qt::KeyboardModifiers)> dropped)
    : QObject(widget), m_widget(widget), m_accepts(std::move(accepts)), m_dropped(std::move(dropped))
{
    widget->setAcceptDrops(true);
    widget->installEventFilter(this);
    widget->setProperty("qbDropTarget", true);
}

bool DropTarget::installedOn(QWidget *widget) { return widget && widget->property("qbDropTarget").toBool(); }

bool DropTarget::eventFilter(QObject *o, QEvent *e)
{
    auto *button = qobject_cast<QAbstractButton *>(m_widget);
    switch (e->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto *de = static_cast<QDragMoveEvent *>(e);
        if (m_accepts && m_accepts(de->mimeData())) {
            de->setDropAction(Qt::CopyAction); // the documents stay where they are
            de->accept();
            if (button && !button->isCheckable())
                button->setDown(true);
        } else {
            de->ignore();
        }
        return true;
    }
    case QEvent::DragLeave:
        if (button && !button->isCheckable())
            button->setDown(false);
        return true;
    case QEvent::Drop: {
        auto *de = static_cast<QDropEvent *>(e);
        if (button && !button->isCheckable())
            button->setDown(false);
        if (m_accepts && m_accepts(de->mimeData())) {
            de->setDropAction(Qt::CopyAction);
            de->accept();
            if (m_dropped)
                m_dropped(de->mimeData(), de->position().toPoint(), de->modifiers());
        }
        return true;
    }
    default: break;
    }
    return QObject::eventFilter(o, e);
}

// ------------------------------------------------------------------ files for other programs

static QString freeName(const QString &dir, const QString &base, const QString &suffix)
{
    QString name = base.isEmpty() ? QObject::tr("Untitled") : base;
    const QDir d(dir);
    for (int n = 2; d.exists(name + suffix) || d.exists(name); ++n)
        name = QStringLiteral("%1 %2").arg(base.isEmpty() ? QObject::tr("Untitled") : base).arg(n);
    return name;
}

static bool writeFile(const QString &path, const QByteArray &data)
{
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(data);
    return out.commit();
}

static void copyTreeTo(const QString &from, const QString &to)
{
    QDir().mkpath(to);
    for (const QFileInfo &fi : QDir(from).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (fi.isDir())
            copyTreeTo(fi.absoluteFilePath(), to + QLatin1Char('/') + fi.fileName());
        else
            QFile::copy(fi.absoluteFilePath(), to + QLatin1Char('/') + fi.fileName());
    }
}

static ExportOptions dragOptions(const AppSettings &s)
{
    ExportOptions o;
    o.format = s.dragExportFormat;
    o.children = s.dragExportChildren;
    return o;
}

// The text of a document in an export format (md | rtf | docx | txt | fountain | fdx).
static bool writeText(Project *p, const QString &id, QTextDocument *doc, const QString &title, const QString &path,
                      const QString &ext)
{
    if (ext == QLatin1String("md"))
        return writeFile(path, md::toStandardMarkdown(QString::fromUtf8(p->docs()->serialize(id, doc))).toUtf8());
    if (ext == QLatin1String("rtf"))
        return writeFile(path, rtf::write(doc, p->formatting));
    if (ext == QLatin1String("txt"))
        return writeFile(path, text::plainText(doc).toUtf8());
    if (ext == QLatin1String("fountain") || ext == QLatin1String("fdx")) {
        script::Screenplay sp = script::fromDocument(doc, p->formatting);
        return writeFile(path, ext == QLatin1String("fdx") ? script::toFdx(sp) : script::toFountain(sp).toUtf8());
    }
    compile::Format fmt = compile::formatByName(QStringLiteral("Default (Editor Formatting)"));
    compile::Compiler c(p, fmt, p->compile, {AppSettings::instance().forename, AppSettings::instance().surname});
    compile::Manuscript m;
    m.title = title;
    m.fontFamily = QStringLiteral("Times New Roman");
    m.lineSpacing = p->formatting.lineSpacing;
    c.appendDocument(m, doc, id, false, false);
    compile::DocxExporter docx;
    QString err;
    return docx.exportManuscript(m, path, &err);
}

// A copy of a text without comments and inline annotations (the commented text stays).
static void stripComments(QTextDocument *doc)
{
    QTextCursor c(doc);
    c.beginEditBlock();
    // collect first: changing formats while walking the fragments would change them under the walk
    QList<QPair<int, int>> annotations;
    QList<QPair<QPair<int, int>, QTextCharFormat>> comments;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            const auto kind = NoteKind(fr.charFormat().intProperty(props::NoteKind));
            if (kind == NoteKind::InlineAnnotation) {
                annotations.append({fr.position(), fr.position() + fr.length()});
            } else if (kind == NoteKind::Comment) {
                QTextCharFormat cf = fr.charFormat();
                for (int prop : {int(props::NoteKind), int(props::NoteId), int(props::NoteText), int(props::NoteColor)})
                    cf.clearProperty(prop);
                cf.clearBackground();
                comments.append({{fr.position(), fr.position() + fr.length()}, cf});
            }
        }
    for (const auto &cm : comments) {
        QTextCursor fc(doc);
        fc.setPosition(cm.first.first);
        fc.setPosition(cm.first.second, QTextCursor::KeepAnchor);
        fc.setCharFormat(cm.second);
    }
    for (int i = annotations.size() - 1; i >= 0; --i) {
        c.setPosition(annotations[i].first);
        c.setPosition(annotations[i].second, QTextCursor::KeepAnchor);
        c.removeSelectedText();
    }
    c.endEditBlock();
}

static QString typeName(ItemType t)
{
    switch (t) {
    case ItemType::Folder:
    case ItemType::Draft:
    case ItemType::Research:
    case ItemType::Trash: return QObject::tr("Folder");
    case ItemType::Image: return QObject::tr("Picture");
    case ItemType::Pdf: return QObject::tr("PDF");
    case ItemType::Media: return QObject::tr("Audio or video");
    case ItemType::WebPage: return QObject::tr("Web page");
    case ItemType::File: return QObject::tr("File");
    default: return QObject::tr("Text");
    }
}

QString metadataText(Project *p, BinderItem *it, bool withSynopsis)
{
    QStringList l;
    auto add = [&l](const QString &key, const QString &value) {
        if (!value.trimmed().isEmpty())
            l << key + QStringLiteral(": ") + value;
    };
    add(QObject::tr("Title"), it->title);
    add(QObject::tr("Type"), typeName(it->type) + (it->scriptMode ? QObject::tr(" (script)") : QString()));
    if (const Label *lb = p->labelById(it->label))
        add(QObject::tr("Label"), lb->name);
    if (const Status *st = p->statusById(it->status))
        add(QObject::tr("Status"), st->name);
    QStringList kw;
    for (const QString &k : it->keywords)
        if (const Keyword *key = p->keywordById(k))
            kw << key->name;
    add(QObject::tr("Keywords"), kw.join(QStringLiteral(", ")));
    if (it->hasText())
        add(QObject::tr("Include in Compile"), it->includeInCompile ? QObject::tr("Yes") : QObject::tr("No"));
    if (!it->sectionType.isEmpty())
        if (const SectionType *stp = p->sectionTypeById(it->sectionType))
            add(QObject::tr("Section Type"), stp->name);
    if (it->hasText() && it->wordCount > 0)
        add(QObject::tr("Words"), QString::number(it->wordCount));
    if (it->target > 0)
        add(QObject::tr("Target"), it->targetChars ? QObject::tr("%1 characters").arg(it->target)
                                                    : QObject::tr("%1 words").arg(it->target));
    if (it->created.isValid())
        add(QObject::tr("Created"), it->created.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    if (it->modified.isValid())
        add(QObject::tr("Modified"), it->modified.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    for (const CustomField &cf : p->customFields) {
        QString v = it->custom.value(cf.id);
        if (cf.type == CustomFieldType::Checkbox)
            v = (v == QLatin1String("1") || v == QLatin1String("true")) ? QObject::tr("Yes") : QString();
        v.replace(QLatin1Char('\n'), QStringLiteral(" / "));
        add(cf.title, v);
    }
    for (const Bookmark &b : it->bookmarks) {
        QString target = b.target;
        if (b.isInternal())
            if (BinderItem *o = p->item(b.internalId()))
                target = QObject::tr("document “%1”").arg(o->title);
        add(QObject::tr("Bookmark"), b.title == b.target || b.title.isEmpty() ? target : b.title + QStringLiteral(" <") + target + QLatin1Char('>'));
    }
    if (!it->originalFileName.isEmpty())
        add(QObject::tr("Original File"), it->originalFileName);
    QString out = l.join(QLatin1Char('\n')) + QLatin1Char('\n');
    if (withSynopsis && !it->synopsis.trimmed().isEmpty())
        out += QLatin1Char('\n') + QObject::tr("Synopsis:") + QLatin1Char('\n') + it->synopsis.trimmed() + QLatin1Char('\n');
    return out;
}

static QStringList exportInto(Project *p, BinderItem *it, const QString &dir, const ExportOptions &o)
{
    QStringList made;
    const QString &ext = o.format;
    const bool group = o.children && !it->children.isEmpty();
    QString suffix;
    if (it->hasText())
        suffix = QLatin1Char('.') + ext;
    else if (!it->fileExt.isEmpty())
        suffix = QLatin1Char('.') + it->fileExt;
    const QString name = freeName(dir, text::safeFileName(it->title), suffix);
    const QString base = QDir(dir).filePath(name);
    // notes and the like go in the chosen text format (scripts: plain text)
    const QString noteExt = (ext == QLatin1String("fountain") || ext == QLatin1String("fdx")) ? QStringLiteral("txt") : ext;
    if (it->hasText()) {
        // a folder that only holds other documents becomes a folder, not a folder and an empty file
        const bool emptyFolder = group && it->isFolder() && p->docs()->plainText(it->id).trimmed().isEmpty();
        if (!emptyFolder) {
            QTextDocument *doc = p->docs()->text(it->id);
            std::unique_ptr<QTextDocument> copy;
            if (!o.comments) {
                copy.reset(doc->clone());
                stripComments(copy.get());
                doc = copy.get();
            }
            const QString path = base + suffix;
            if (writeText(p, it->id, doc, it->title, path, ext))
                made << path;
        }
    } else if (!it->isFolder() || !it->fileExt.isEmpty()) {
        const QString path = base + suffix;
        if (QFile::copy(p->mediaFilePath(it), path))
            made << path;
        // a web page's pictures
        if (it->type == ItemType::WebPage)
            for (const QFileInfo &fi : QDir(p->contentDir(it->id)).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
                if (fi.fileName().endsWith(QLatin1String("-files")))
                    copyTreeTo(fi.absoluteFilePath(), QDir(dir).filePath(fi.fileName()));
    }
    if (o.notes) {
        QTextDocument *notes = p->docs()->notes(it->id);
        if (notes && !notes->toPlainText().trimmed().isEmpty()) {
            const QString path = base + QObject::tr(" (notes)") + QLatin1Char('.') + noteExt;
            if (writeText(p, it->id, notes, it->title, path, noteExt))
                made << path;
        }
    }
    if (o.metadata) {
        const QString path = base + QObject::tr(" (metadata)") + QStringLiteral(".txt");
        if (writeFile(path, metadataText(p, it, o.synopsis).toUtf8()))
            made << path;
    } else if (o.synopsis && !it->synopsis.trimmed().isEmpty()) {
        const QString path = base + QObject::tr(" (synopsis)") + QStringLiteral(".txt");
        if (writeFile(path, (it->synopsis.trimmed() + QLatin1Char('\n')).toUtf8()))
            made << path;
    }
    if (o.snapshots && it->hasText()) {
        const QList<Snapshot> shots = snapshots::list(p, it->id);
        if (!shots.isEmpty()) {
            const QString sdir = base + QObject::tr(" (snapshots)");
            QDir().mkpath(sdir);
            made << sdir;
            for (const Snapshot &s : shots) {
                QTextDocument sd;
                snapshots::load(p, s, &sd);
                QString n = s.date.toString(QStringLiteral("yyyy-MM-dd HH-mm"));
                if (!s.title.trimmed().isEmpty())
                    n += QLatin1Char(' ') + text::safeFileName(s.title.trimmed());
                const QString sp = QDir(sdir).filePath(freeName(sdir, n, QLatin1Char('.') + noteExt) + QLatin1Char('.') + noteExt);
                writeText(p, it->id, &sd, s.title, sp, noteExt);
            }
        }
    }
    if (group) {
        QDir().mkpath(base);
        made << base;
        for (BinderItem *c : it->children)
            exportInto(p, c, base, o);
    }
    return made;
}

QString exportItem(Project *project, BinderItem *item, const QString &dir, const ExportOptions &options)
{
    if (!project || !item)
        return QString();
    return exportInto(project, item, dir, options).value(0);
}

QString exportItem(Project *project, BinderItem *item, const QString &dir, const QString &format, bool notes, bool children)
{
    ExportOptions o;
    o.format = format;
    o.notes = notes;
    o.synopsis = notes;
    o.children = children;
    return exportItem(project, item, dir, o);
}

QList<QUrl> exportForDrag(Project *project, const QStringList &ids)
{
    // kept until the program ends: the receiving program may copy the files later
    static std::vector<std::unique_ptr<QTemporaryDir>> folders;
    QList<QUrl> urls;
    if (!project)
        return urls;
    auto dir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QLatin1String("/" QB_APP_NAME "-XXXXXX"));
    if (!dir->isValid())
        return urls;
    const AppSettings &s = AppSettings::instance();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const QString &id : ids)
        if (BinderItem *it = project->item(id))
            for (const QString &path : exportInto(project, it, dir->path(), dragOptions(s)))
                urls << QUrl::fromLocalFile(path);
    QApplication::restoreOverrideCursor();
    folders.push_back(std::move(dir));
    return urls;
}

void installFileProvider() { ItemsMimeData::fileProvider = &exportForDrag; }

// ------------------------------------------------------------------ keywords

QMimeData *mimeForKeywords(Project *project, const QStringList &keywordIds)
{
    auto *md = new QMimeData;
    md->setData(QString::fromLatin1(kKeywordsMime),
                project->folderPath().toUtf8() + '\n' + keywordIds.join(QLatin1Char('\n')).toUtf8());
    QStringList names;
    for (const QString &id : keywordIds)
        if (const Keyword *k = project->keywordById(id))
            names << k->name;
    md->setText(names.join(QLatin1String(", ")));
    return md;
}

QStringList keywordsFromMime(const QMimeData *data, Project *project)
{
    QStringList out;
    if (!data || !project || !data->hasFormat(QString::fromLatin1(kKeywordsMime)))
        return out;
    const QList<QByteArray> lines = data->data(QString::fromLatin1(kKeywordsMime)).split('\n');
    if (lines.isEmpty() || QString::fromUtf8(lines.first()) != project->folderPath())
        return out;
    for (int i = 1; i < lines.size(); ++i) {
        const QString id = QString::fromUtf8(lines[i]);
        if (project->keywordById(id))
            out << id;
    }
    return out;
}

int assignKeywords(Project *project, const QList<BinderItem *> &items, const QStringList &keywordIds, bool withGroups)
{
    if (!project)
        return 0;
    QStringList ids;
    for (const QString &id : keywordIds) {
        if (withGroups) {
            // the groups first, outermost first
            QStringList chain;
            const Keyword *k = project->keywordById(id);
            for (int guard = 0; k && !k->parentId.isEmpty() && guard < 20; ++guard) {
                chain.prepend(k->parentId);
                k = project->keywordById(k->parentId);
            }
            for (const QString &g : chain)
                if (!ids.contains(g) && project->keywordById(g))
                    ids << g;
        }
        if (!ids.contains(id))
            ids << id;
    }
    int added = 0;
    for (BinderItem *it : items) {
        bool changed = false;
        for (const QString &id : ids)
            if (!it->keywords.contains(id)) {
                it->keywords << id;
                changed = true;
                ++added;
            }
        if (changed)
            project->notifyItemChanged(it, false);
    }
    return added;
}

int dropKeywords(Project *project, BinderItem *target, const QList<BinderItem *> &selected, const QMimeData *data,
                 Qt::KeyboardModifiers modifiers)
{
    const QStringList ids = keywordsFromMime(data, project);
    if (!target || ids.isEmpty())
        return 0;
    const QList<BinderItem *> items = selected.contains(target) ? selected : QList<BinderItem *>{target};
    return assignKeywords(project, items, ids, modifiers & Qt::AltModifier);
}

// ------------------------------------------------------------------ images

bool isImageFile(const QString &path)
{
    static const QStringList exts{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("gif"),
                                  QStringLiteral("bmp"), QStringLiteral("webp"), QStringLiteral("svg")};
    return exts.contains(QFileInfo(path).suffix().toLower());
}

QString imageFileFromMime(const QMimeData *data, Project *project)
{
    if (!data)
        return QString();
    if (data->hasFormat(QString::fromLatin1(kItemsMime))) {
        if (project)
            for (BinderItem *it : BinderModel::itemsFromMime(data, project))
                if (it->type == ItemType::Image)
                    return project->mediaFilePath(it);
        return QString(); // other documents of the project are no pictures
    }
    if (data->hasUrls())
        for (const QUrl &u : data->urls())
            if (u.isLocalFile() && isImageFile(u.toLocalFile()))
                return u.toLocalFile();
    if (data->hasImage()) {
        const QImage img = qvariant_cast<QImage>(data->imageData());
        if (!img.isNull()) {
            static QTemporaryDir tmp;
            static int n = 0;
            const QString path = tmp.filePath(QStringLiteral("dropped-%1.png").arg(++n));
            if (img.save(path))
                return path;
        }
    }
    return QString();
}

} // namespace dnd
} // namespace qb
