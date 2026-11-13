// Writing tools (add-ons with a plugin that implements tools/ToolApi.h):
// their commands in the menus and what the tools ask of the main window.
#include "ui/MainWindow.h"

#include "compile/PluginManager.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "import/FileImporter.h"
#include "text/DisplayFormat.h"
#include "text/Styles.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/Commands.h"
#include "ui/EditorPane.h"
#include "core/ItemIcons.h"
#include "ui/TextEditor.h"
#include "ui/ToolHost.h"
#include "ui/Inspector.h"
#include "ui/OptionsDialog.h"
#include "ui/ToolbarConfig.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextBlock>
#include <QUrl>
#include <QImage>
#include <QJsonArray>
#include <QMenu>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextImageFormat>
#include <QSettings>
#include <QToolBar>
#include <QToolButton>

namespace qb {

void MainWindow::createToolActions()
{
    toolhost::Host &host = toolhost::host();
    host.attachTools();
    for (const tools::Command &c : host.allCommands()) {
        auto *ac = new QAction(c.text, this);
        ac->setObjectName(QStringLiteral("tool:") + c.key);
        QString plain = c.text;
        plain.remove(QLatin1Char('&'));
        if (plain.endsWith(QChar(0x2026)))
            plain.chop(1);
        ac->setIconText(plain);
        if (!c.icon.isEmpty())
            ac->setIcon(icons::action(c.icon));
        if (!c.shortcut.isEmpty())
            ac->setShortcut(QKeySequence::fromString(c.shortcut, QKeySequence::PortableText));
        if (!c.toolTip.isEmpty())
            ac->setToolTip(c.toolTip);
        ac->setProperty("qbToolPlace", c.place);
        ac->setProperty("qbToolNeedsProject", c.needsProject);
        const QString key = c.key;
        connect(ac, &QAction::triggered, this, [key] { toolhost::host().runCommand(key); });
        addAction(ac); // shortcuts work while the menu is closed
        a[ac->objectName()] = ac;
        m_toolActions << ac;
    }

    // tool interface 1.1 (0.61): tabs in the inspector, buttons of the main toolbar with a menu
    QList<toolbars::ToolButton> buttons;
    for (tools::ToolInterface *t : compile::PluginManager::instance().tools()) {
        tools::ToolExtension *ext = toolhost::Host::extension(t);
        if (!ext)
            continue;
        for (const tools::InspectorTab &tab : ext->inspectorTabs()) {
            if (!m_inspector)
                break;
            if (QWidget *page = ext->createInspectorTab(tab.key, m_inspector))
                m_inspector->addExtraTab(tab.key, page, toolIcon(tab.icon), tab.toolTip);
        }
        for (const tools::ToolbarButton &b : ext->toolbarButtons()) {
            const QString id = QStringLiteral("tool:") + b.commandKey;
            if (!a.contains(id))
                continue;
            buttons << toolbars::ToolButton{id, b.before};
            if (QMenu *menu = ext->commandMenu(b.commandKey))
                m_toolMenus.insert(b.commandKey, menu);
        }
    }
    toolbars::setToolButtons(buttons);
    // a main toolbar the writer arranged gets each tool button once (as the paragraph buttons in 0.60.4)
    AppSettings &s = AppSettings::instance();
    QSettings st;
    QStringList done = st.value(QStringLiteral("toolbars/toolButtonsPlaced")).toStringList();
    const QStringList before = done;
    if (toolbars::placeToolButtonsOnce(s.toolbarMain, done))
        st.setValue(QStringLiteral("toolbars/main"), s.toolbarMain);
    if (done != before)
        st.setValue(QStringLiteral("toolbars/toolButtonsPlaced"), done);
}

QIcon MainWindow::toolIcon(const QString &icon)
{
    if (icon.isEmpty())
        return QIcon();
    if (icon.contains(QLatin1Char('/')) && QFileInfo::exists(icon))
        return QIcon(icon);
    return icons::action(icon);
}

void MainWindow::placeToolButtons()
{
    // 0.90.1 (UR-12): the arrow beside Undo and Redo lists the steps
    for (const auto &pair : {qMakePair(QStringLiteral("undo"), m_undoMenu), qMakePair(QStringLiteral("redo"), m_redoMenu)}) {
        QAction *ac = a.value(pair.first);
        if (!ac || !pair.second)
            continue;
        for (QToolBar *bar : {m_toolbar, m_formatBar})
            if (bar)
                if (auto *b = qobject_cast<QToolButton *>(bar->widgetForAction(ac))) {
                    b->setMenu(pair.second);
                    b->setPopupMode(QToolButton::MenuButtonPopup);
                    b->setObjectName(pair.first + QStringLiteral("Button"));
                }
    }
    // a tool command with a menu: the arrow beside its buttons opens it (the button itself runs the command)
    for (auto it = m_toolMenus.cbegin(); it != m_toolMenus.cend(); ++it) {
        QAction *ac = a.value(QStringLiteral("tool:") + it.key());
        if (!ac || !it.value())
            continue;
        for (QToolBar *bar : {m_toolbar, m_formatBar})
            if (bar)
                if (auto *b = qobject_cast<QToolButton *>(bar->widgetForAction(ac))) {
                    b->setMenu(it.value());
                    b->setPopupMode(QToolButton::MenuButtonPopup);
                    b->setObjectName(QStringLiteral("toolButton:") + it.key());
                }
    }
}

void MainWindow::toolShowInspectorTab(const QString &key)
{
    if (!m_inspector)
        return;
    if (QAction *si = a.value(QStringLiteral("showInspector")); si && !si->isChecked())
        si->trigger();
    m_inspector->showExtraTab(key);
}

void MainWindow::toolSetCommandState(const QString &commandKey, bool checked, const QString &icon, const QString &toolTip)
{
    QAction *ac = a.value(QStringLiteral("tool:") + commandKey);
    if (!ac)
        return;
    if (!ac->property("qbToolIcon").isValid()) { // the command's own look, to go back to
        ac->setProperty("qbToolIcon", QVariant::fromValue(ac->icon()));
        ac->setProperty("qbToolTip", ac->toolTip());
    }
    ac->setCheckable(true);
    ac->setChecked(checked);
    ac->setIcon(icon.isEmpty() ? ac->property("qbToolIcon").value<QIcon>() : toolIcon(icon));
    ac->setToolTip(toolTip.isEmpty() ? ac->property("qbToolTip").toString() : toolTip);
}

void MainWindow::toolShowOptions(const QString &toolId)
{
    OptionsDialog::run(this, [toolId](OptionsDialog &dlg) { dlg.showToolSection(toolId); });
}

QList<QAction *> MainWindow::toolActionsAt(const QString &place) const
{
    QList<QAction *> out;
    for (QAction *ac : m_toolActions)
        if (ac->property("qbToolPlace").toString() == place)
            out << ac;
    return out;
}

void MainWindow::fillWritingToolsMenu(QMenu *menu)
{
    const QList<QAction *> list = toolActionsAt(QStringLiteral("edit/writing-tools"));
    for (QAction *ac : list)
        menu->addAction(ac);
    if (list.isEmpty()) {
        QAction *none = menu->addAction(tr("No writing tools installed"));
        none->setEnabled(false);
    }
    menu->addSeparator();
    menuAlias(menu, QStringLiteral("addonManager"), tr("Add-on &Manager…"));
}

void MainWindow::updateToolActions()
{
    const bool has = m_project != nullptr;
    for (QAction *ac : m_toolActions)
        if (ac->property("qbToolNeedsProject").toBool())
            ac->setEnabled(has);
}

// ------------------------------------------------------------------ what the tools ask for

QString MainWindow::toolProjectTitle() const
{
    return m_project ? m_project->title() : QString();
}

bool MainWindow::toolCanInsert() const
{
    TextEditor *e = editor();
    return m_project && e && !e->isReadOnly();
}

bool MainWindow::toolInsertText(const QString &text)
{
    TextEditor *e = editor();
    if (!m_project || !e || e->isReadOnly())
        return false;
    QTextCursor c = e->textCursor();
    c.beginEditBlock();
    c.insertText(text); // takes the formatting at the cursor
    c.endEditBlock();
    e->setTextCursor(c);
    return true;
}

QString MainWindow::toolSelectedText() const
{
    TextEditor *e = editor();
    if (!e)
        return QString();
    QString s = e->textCursor().selectedText();
    s.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    s.replace(QChar::LineSeparator, QLatin1Char('\n'));
    return s;
}

static QString folderPath(const BinderItem *it)
{
    QStringList parts;
    for (const BinderItem *p = it->parent; p && p->parent; p = p->parent)
        parts.prepend(p->title);
    return parts.join(QStringLiteral(" / "));
}

QList<tools::TextDocument> MainWindow::toolDocuments(tools::Scope scope) const
{
    QList<tools::TextDocument> out;
    if (!m_project)
        return out;
    if (scope == tools::Scope::Selection) {
        const QString sel = toolSelectedText();
        if (!sel.isEmpty()) {
            tools::TextDocument d;
            d.title = tr("Selected text");
            d.text = sel;
            out << d;
        }
        return out;
    }
    QList<BinderItem *> items;
    auto addTree = [&items](BinderItem *top) {
        top->forEach([&items](BinderItem *it) {
            if (!items.contains(it))
                items << it;
        });
    };
    switch (scope) {
    case tools::Scope::Document:
        // what the editor shows; a folder on the corkboard or in the outliner
        // counts with the documents inside it, as in combined text
        if (m_active)
            for (BinderItem *it : m_active->items())
                addTree(it);
        break;
    case tools::Scope::BinderSelection:
        for (BinderItem *it : m_binder->selectedItems())
            addTree(it);
        break;
    case tools::Scope::Draft:
        m_project->draft()->forEach([&items](BinderItem *it) {
            if (it->includeInCompile)
                items << it;
        });
        break;
    case tools::Scope::Project:
        for (BinderItem *top : m_project->root()->children)
            if (top != m_project->trash())
                addTree(top);
        break;
    case tools::Scope::Selection:
        break;
    }
    for (BinderItem *it : items) {
        if (!it->hasText() || m_project->isInTrash(it))
            continue;
        tools::TextDocument d;
        d.text = m_project->docs()->plainText(it->id);
        if (it->isContainer() && d.text.trimmed().isEmpty())
            continue; // folders without text of their own (the Draft folder itself)
        d.id = it->id;
        d.title = it->title;
        d.path = folderPath(it);
        d.inDraft = m_project->isInDraft(it);
        out << d;
    }
    return out;
}

void MainWindow::toolShowDocument(const QString &id)
{
    BinderItem *it = m_project ? m_project->item(id) : nullptr;
    if (!it)
        return;
    m_binder->select({it}, false);
    loadInPane(m_active, {it});
    raise();
    activateWindow();
}

void MainWindow::toolSearch(const QString &text)
{
    if (!m_project || text.isEmpty())
        return;
    SearchQuery q;
    q.text = text;
    q.exactPhrase = true;
    q.wholeWord = true;
    m_binder->show();
    m_binder->showSearch(false);
    m_binder->runSearch(q);
    raise();
    activateWindow();
}

bool MainWindow::showIfOpen(const QString &path)
{
    if (!m_project || path.isEmpty())
        return false;
    auto canon = [](const QString &p) {
        const QFileInfo fi(p);
        const QString c = fi.canonicalFilePath();
        return c.isEmpty() ? fi.absoluteFilePath() : c;
    };
    const QString want = canon(path);
    QStringList mine = {canon(m_project->indexPath()), canon(m_project->folderPath())};
    if (!m_project->scrivenerPath.isEmpty()) {
        mine << canon(m_project->scrivenerPath);
        // the .scrivx inside the .scriv folder names the same project
        if (want.endsWith(QLatin1String(".scrivx"), Qt::CaseInsensitive))
            mine << canon(QFileInfo(path).absolutePath());
    }
    if (!mine.contains(want) && !(want.endsWith(QLatin1String(".scrivx"), Qt::CaseInsensitive) && mine.contains(canon(QFileInfo(path).absolutePath()))))
        return false;
    if (isMinimized())
        showNormal();
    show();
    raise();
    activateWindow();
    return true;
}

QJsonObject MainWindow::toolProjectInfo() const
{
    QJsonObject o;
    if (!m_project)
        return o;
    std::function<QJsonArray(const BinderItem *)> tree = [&](const BinderItem *parent) {
        QJsonArray arr;
        for (const BinderItem *it : parent->children) {
            if (it == m_project->trash())
                continue;
            QJsonObject n{{QStringLiteral("id"), it->id},
                          {QStringLiteral("title"), it->title},
                          {QStringLiteral("folder"), it->isFolder()},
                          {QStringLiteral("text"), it->hasText()}};
            if (!it->children.isEmpty())
                n.insert(QStringLiteral("children"), tree(it));
            arr.append(n);
        }
        return arr;
    };
    o.insert(QStringLiteral("title"), m_project->title());
    o.insert(QStringLiteral("file"), m_project->displayFile());
    o.insert(QStringLiteral("binder"), tree(m_project->root()));
    return o;
}

// An HTML note (scratch pad): paragraphs with their headings and semantic
// formatting (as the importer keeps them) and the pictures, which are copied
// into the document's folder. Inserted at the cursor.
static void insertHtmlNote(Project *p, BinderItem *target, QTextDocument *doc, QTextCursor &c, const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return;
    QTextDocument tmp;
    const QString dir = QFileInfo(file).absolutePath() + QLatin1Char('/');
    tmp.setBaseUrl(QUrl::fromLocalFile(dir));
    tmp.setHtml(QString::fromUtf8(f.readAll()));
    const Formatting &fm = p->formatting;
    bool first = true;
    for (QTextBlock b = tmp.begin(); b.isValid(); b = b.next()) {
        QString style;
        if (b.blockFormat().headingLevel() > 0)
            style = styles::forHeadingLevel(b.blockFormat().headingLevel());
        if (!first)
            c.insertBlock(display::blockFormatFor(fm, style), display::charFormatFor(fm, style));
        else
            c.setBlockFormat(display::blockFormatFor(fm, style));
        first = false;
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            if (fr.charFormat().isImageFormat()) {
                const QTextImageFormat src = fr.charFormat().toImageFormat();
                QString name = src.name();
                if (name.startsWith(QLatin1String("file:")))
                    name = QUrl(name).toLocalFile();
                else if (QFileInfo(name).isRelative())
                    name = dir + name;
                const QImage img(name);
                if (img.isNull())
                    continue;
                const QString rel = p->docs()->importImage(target->id, img);
                doc->addResource(QTextDocument::ImageResource, QUrl(rel), img);
                QTextImageFormat imf;
                imf.setName(rel);
                const int w = src.width() > 0 ? int(src.width()) : img.width();
                if (w > 600 || src.width() > 0) {
                    const int shown = qMin(w, 600);
                    imf.setWidth(shown);
                    imf.setHeight(img.height() * double(shown) / img.width());
                }
                c.insertImage(imf);
            } else {
                c.insertText(fr.text(), display::semanticCharFormat(fr.charFormat()));
            }
        }
    }
}

bool MainWindow::receiveFromTool(const tools::SendRequest &r, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (!m_project)
        return fail(tr("No project is open."));
    if (!QFileInfo::exists(r.file))
        return fail(tr("The file to send does not exist: %1").arg(r.file));
    const importer::FileKind kind = importer::classify(r.file);
    const QString suffix = QFileInfo(r.file).suffix().toLower();
    const bool isHtml = suffix == QLatin1String("html") || suffix == QLatin1String("htm");

    if (r.mode == QLatin1String("append")) {
        BinderItem *target = m_project->item(r.targetId);
        if (!target || !target->hasText() || m_project->isInTrash(target))
            return fail(tr("The document to add to is not a text document of the project (any more)."));
        QTextDocument *doc = m_project->docs()->text(target->id);
        if (!doc)
            return fail(tr("The document could not be opened."));
        QTextCursor c(doc);
        c.movePosition(QTextCursor::End);
        c.beginEditBlock();
        // File > Options > General > Separators (0.21); an empty text gets no separator
        if (!doc->toPlainText().trimmed().isEmpty() || doc->blockCount() > 1)
            display::insertSeparator(c, AppSettings::instance().sepScratch, AppSettings::instance().sepScratchCustom,
                                     m_project->formatting);
        if (isHtml) {
            insertHtmlNote(m_project.get(), target, doc, c, r.file);
        } else if (kind == importer::FileKind::Image) {
            QImage img(r.file);
            if (img.isNull()) {
                c.endEditBlock();
                return fail(tr("The picture could not be read."));
            }
            const QString rel = m_project->docs()->importImage(target->id, img);
            doc->addResource(QTextDocument::ImageResource, QUrl(rel), img);
            QTextImageFormat imf;
            imf.setName(rel);
            if (img.width() > 600) {
                imf.setWidth(600);
                imf.setHeight(img.height() * 600.0 / img.width());
            }
            c.insertImage(imf);
        } else {
            QTextDocument incoming;
            QString err;
            if (!importer::readTextFile(r.file, &incoming, m_project->formatting,
                                        m_project->storage == StorageFormat::Rtf, &err)) {
                c.endEditBlock();
                return fail(err.isEmpty() ? tr("The text could not be read.") : err);
            }
            c.insertFragment(QTextDocumentFragment(&incoming));
        }
        c.endEditBlock();
        m_project->docs()->recount(target->id);
        m_project->notifyItemChanged(target);
        return true;
    }

    BinderItem *parent = r.targetId.isEmpty() ? m_project->research() : m_project->item(r.targetId);
    if (!parent || m_project->isInTrash(parent))
        parent = m_project->research();
    if (isHtml) {
        // a note with formatting and pictures: a new text document filled like "append"
        BinderItem *item = m_project->model()->createItem(parent, -1, ItemType::Text,
                                                          r.title.trimmed().isEmpty() ? QFileInfo(r.file).completeBaseName() : r.title.trimmed());
        if (!item)
            return fail(tr("The document could not be created."));
        QTextDocument *doc = m_project->docs()->text(item->id);
        QTextCursor c(doc);
        c.beginEditBlock();
        insertHtmlNote(m_project.get(), item, doc, c, r.file);
        c.endEditBlock();
        m_project->docs()->recount(item->id);
        m_project->notifyItemChanged(item);
        m_project->markIndexDirty();
        return true;
    }
    if (m_project->isInDraft(parent) && kind != importer::FileKind::Text)
        return fail(tr("The Draft folder takes text only; choose a folder outside the Draft for pictures and files."));
    QStringList warnings;
    const auto items = importer::importFiles(m_project.get(), {r.file}, parent, -1, &warnings);
    if (items.isEmpty())
        return fail(warnings.isEmpty() ? tr("The note could not be imported.") : warnings.join(QLatin1Char('\n')));
    if (!r.title.trimmed().isEmpty()) {
        items.first()->title = r.title.trimmed();
        m_project->notifyItemChanged(items.first());
    }
    m_project->markIndexDirty();
    return true;
}

} // namespace qb
