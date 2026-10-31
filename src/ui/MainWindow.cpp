#include "ui/LicensesDialog.h"
#include "ui/MainWindow.h"
#include "ui/LookMenu.h"
#include "ui/ProjectLook.h"
#include "import/ScrivenerFormat.h"
#include "import/ScrivenerImporter.h"
#include "ui/MediaPlayback.h"
#include "ui/MediaView.h"
#include "ui/DragDrop.h"
#include "ui/QuickRef.h"
#include "text/RtfIO.h"
#include "ui/Copyholder.h"
#include "ui/PageDialogs.h"
#include <QJsonDocument>
#include <QJsonObject>
#include "text/PageLayout.h"
#include "core/Paths.h"
#include "text/MarkdownIO.h"
#include "AppInfo.h"
#include "core/Backup.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/ItemIcons.h"
#include "core/ProjectTemplates.h"
#include "core/Search.h"
#include "ui/SearchTools.h"
#include "core/Snapshots.h"
#include "import/FileImporter.h"
#include "text/DirectFormat.h"
#include "text/DisplayFormat.h"
#include "ui/FormatDialogs.h"
#include "ui/FormattingChange.h"
#include "ui/Ruler.h"
#include "text/TextUtil.h"
#include "core/Addons.h"
#include "core/CrashGuard.h"
#include "core/Launcher.h"
#include "ui/AddonDialogs.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/BookmarkList.h"
#include "ui/CompileDialog.h"
#include "ui/Corkboard.h"
#include "ui/CrashDialogs.h"
#include "ui/Commands.h"
#include "ui/ToolHost.h"
#include "ui/Inspector.h"
#include "ui/MindMapController.h"
#include "ui/MindMapDoc.h"
#include "ui/MindMapUi.h"
#include "ui/ObjectsUi.h"
#include "ui/ColorPickButton.h"
#include "ui/ViewMemory.h"
#include "ui/MindMapView.h"
#include "core/Objects.h"
#include "ui/Layouts.h"
#include "ui/OptionsDialog.h"
#include "ui/ColorPalette.h"
#include "ui/Zoom.h"
#include "ui/PrintDialogs.h"
#include "ui/Printing.h"
#include "ui/ThemeDialogs.h"
#include "ui/Outliner.h"
#include "ui/ProjectSettingsDialog.h"
#include "ui/SpellChecker.h"
#include "ui/StartDialog.h"
#include "ui/StyleDialogs.h"
#include "ui/TargetLineEdit.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"
#include "ui/ToolDialogs.h"
#include "ui/Windows.h"

#include <QAction>
#include <cstdio>
#include <QLocalServer>
#include <QVBoxLayout>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QSysInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QStatusBar>
#include <QPrinter>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTextList>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>

namespace qb {

namespace {
// A double click on the divider between the two editors makes them equally large.
class SplitEqualizer : public QObject
{
public:
    explicit SplitEqualizer(QSplitter *split) : QObject(split), m_split(split)
    {
        if (QSplitterHandle *h = split->handle(1)) {
            h->installEventFilter(this);
            h->setToolTip(QObject::tr("Double-click to make both editors the same size"));
        }
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::MouseButtonDblClick) {
            const QList<int> sizes = m_split->sizes();
            const int total = sizes.value(0) + sizes.value(1);
            if (total > 0)
                m_split->setSizes({total / 2, total - total / 2});
            return true;
        }
        return QObject::eventFilter(o, e);
    }

private:
    QSplitter *m_split;
};
} // namespace

// ------------------------------------------------------------------ construction

namespace {
// Notes the time of every key press and mouse click for Automatic Quit (0.21).
class InputWatch : public QObject
{
public:
    explicit InputWatch(MainWindow *w) : QObject(w), m_window(w) {}
    bool eventFilter(QObject *o, QEvent *e) override
    {
        switch (e->type()) {
        case QEvent::KeyPress:
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
            if (m_window)
                m_window->noteInput(QDateTime::currentDateTime());
            break;
        default: break;
        }
        return QObject::eventFilter(o, e);
    }

private:
    QPointer<MainWindow> m_window;
};
} // namespace

MainWindow::MainWindow()
{
    setWindowTitle(QStringLiteral(QB_APP_NAME));
    setWindowIcon(QIcon(QStringLiteral(":/app/quillbinder.svg")));
    setAcceptDrops(false);

    m_mainSplit = new QSplitter(Qt::Horizontal, this);
    m_binder = new BinderPanel(m_mainSplit);
    // the editors with the bar for offers of the format bar above them
    auto *editorArea = new QWidget(m_mainSplit);
    editorArea->setObjectName(QStringLiteral("editorArea"));
    auto *editorLayout = new QVBoxLayout(editorArea);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(0);
    m_fontScope = new FontScopeBar(editorArea);
    connect(m_fontScope, &FontScopeBar::accepted, this, &MainWindow::acceptFontOffer);
    m_editorSplit = new QSplitter(Qt::Vertical, editorArea);
    editorLayout->addWidget(m_fontScope);
    editorLayout->addWidget(m_editorSplit, 1);
    m_panes[0] = new EditorPane(m_editorSplit);
    m_panes[1] = new EditorPane(m_editorSplit);
    m_panes[1]->hide();
    m_inspector = new Inspector(m_mainSplit);
    m_mainSplit->addWidget(m_binder);
    m_mainSplit->addWidget(editorArea);
    m_mainSplit->addWidget(m_inspector);
    m_mainSplit->setStretchFactor(0, 0);
    m_mainSplit->setStretchFactor(1, 1);
    m_mainSplit->setStretchFactor(2, 0);
    m_mainSplit->setSizes({260, 800, 300});
    m_mainSplit->setChildrenCollapsible(false);
    setCentralWidget(m_mainSplit);
    m_active = m_panes[0];
    connectPane(m_panes[0]);
    connectPane(m_panes[1]);
    dnd::installFileProvider(); // documents dragged to the file manager become files
    new SplitEqualizer(m_editorSplit); // a double click on the divider gives both editors the same room

    m_panes[1]->setStateSuffix(QStringLiteral("1"));
    commands::clear();
    toolhost::host().setWindow(this);
    createActions();
    createExtraActions();
    createToolActions();
    createMenus();
    commands::addFromMenuBar(menuBar());
    for (QAction *ac : actions()) // commands that sit in no menu
        if (!ac->objectName().isEmpty() && !commands::find(ac->objectName()))
            commands::add(ac, tr("Other"));
    createToolBars();
    createFormatBar();
    createIconBar();
    rebuildStyleActions();
    rebuildScriptActions();
    rebuildLayoutActions();
    commands::apply();
    rebuildToolBars();
    connect(&layouts::Notifier::instance(), &layouts::Notifier::changed, this, [this] {
        rebuildLayoutActions();
        commands::apply();
    });

    // binder
    connect(m_binder, &BinderPanel::selectionChanged, this, [this](const QList<BinderItem *> &items) {
        if (!m_project || m_updatingUi)
            return;
        EditorPane *target = binderTarget();
        if (m_quickSearchMark && !m_binder->showsSearchResults()) {
            // the matches of a quick search are marked until another document is chosen
            m_quickSearchMark = false;
            applySearchHighlights();
        }
        if (target && items != target->items() && !items.isEmpty()) {
            loadInPane(target, items);
            // a search result: its first match is selected and shown (0.14.1)
            if (m_binder->showsSearchResults())
                QTimer::singleShot(0, target, [target] { target->revealFirstMatch(); });
        }
        if (!items.isEmpty() && (!target || target == nullptr)) {
            m_inspector->setItem(items.size() == 1 ? items.first() : nullptr);
        }
        updateActions();
    });
    connect(m_binder, &BinderPanel::searchChanged, this, &MainWindow::applySearchHighlights);
    connect(m_binder, &BinderPanel::contextMenuRequested, this,
            [this](const QPoint &pos) { showItemContextMenu(pos, int(lookmenu::Part::Binder)); });
    connect(m_binder, &BinderPanel::newDocumentRequested, this, [this] { a[QStringLiteral("newText")]->trigger(); });
    connect(m_binder, &BinderPanel::newFolderRequested, this, [this] { a[QStringLiteral("newFolder")]->trigger(); });
    connect(m_binder, &BinderPanel::trashRequested, this, [this] { a[QStringLiteral("trash")]->trigger(); });
    connect(m_binder, &BinderPanel::quickReferenceRequested, this, [this] {
        for (BinderItem *it : m_binder->selectedItems())
            openQuickReference(it);
    });

    // inspector
    connect(m_inspector, &Inspector::openItemRequested, this, [this](BinderItem *it) { loadInPane(m_active, {it}); });
    // a comment of a folder's document (0.17): shown in the editor where it is
    connect(m_inspector, &Inspector::openNoteRequested, this, [this](BinderItem *it, const QString &noteId) {
        if (!m_project || !it)
            return;
        auto select = [this, it, noteId]() -> bool {
            for (TextEditor *e : m_active->textView()->editors())
                if (e->item() == it && e->selectNote(noteId)) {
                    e->setFocus();
                    return true;
                }
            return false;
        };
        if (select())
            return;
        loadInPane(m_active, {it});
        QTimer::singleShot(0, this, [select] { select(); });
    });
    connect(m_inspector, &Inspector::openMindMapWindow, this, [this](MindMapDoc *doc) { MindMapWindow::open(doc, this); });
    connect(m_inspector, &Inspector::openObjectsWindow, this, [this](const QString &id) {
        if (m_project)
            if (ObjectsWindow *ow = ObjectsWindow::open(m_project.get(), id, this))
                shareShortcuts(ow); // Ctrl+B and the like in the text of objects (0.21.1)
    });
    connect(m_inspector, &Inspector::objectLinkActivated, this, [this](const QString &href) { openLink(nullptr, href); });
    // document links in mind map notes open as document links in the text do (Options > Behaviors)
    MindMapController::setDocumentOpener([this](const QString &href) {
        if (!m_project)
            return;
        openLink(m_active, href);
        raise();
        activateWindow();
    });
    connect(m_inspector, &Inspector::keywordsDialogRequested, this, [this] { a[QStringLiteral("keywords")]->trigger(); });
    connect(m_inspector, &Inspector::projectSettingsRequested, this, [this](int page) {
        if (!m_project)
            return;
        ProjectSettingsDialog dlg(m_project.get(), page, this);
        dlg.exec();
    });

    // context menu entries in every text editor
    setTextEditorContextHook([this](TextEditor *ed, QMenu *menu, const QPoint &) {
        menu->addSeparator();
        QMenu *styles = menu->addMenu(tr("Style"));
        styles->setObjectName(QStringLiteral("contextStyleMenu"));
        fillStyleList(styles, ed);
        menu->addAction(a[QStringLiteral("comment")]);
        menu->addAction(a[QStringLiteral("footnote")]);
        menu->addAction(a[QStringLiteral("annotation")]);
        menu->addAction(a[QStringLiteral("inlineFootnote")]);
        if (!ed->noteIdAt(ed->textCursor().position()).isEmpty())
            menu->addAction(tr("Remove Note"), this, [ed] { ed->removeNoteAtCursor(); });
        menu->addAction(a[QStringLiteral("docLink")]);
        if (!ed->anchorAtCursor().isEmpty()) {
            const QString href = ed->anchorAtCursor();
            menu->addAction(tr("Open Link"), this, [this, href] { openLink(m_active, href); });
            menu->addAction(tr("Remove Link"), this, [ed] {
                QTextCursor c = ed->textCursor();
                if (!c.hasSelection())
                    c.select(QTextCursor::WordUnderCursor);
                QTextCharFormat f;
                f.setAnchor(false);
                f.setAnchorHref(QString());
                f.setFontUnderline(false);
                f.clearForeground();
                c.mergeCharFormat(f);
            });
        }
        if (!ed->isNotes()) {
            menu->addSeparator();
            menu->addAction(a[QStringLiteral("split")]);
            menu->addAction(a[QStringLiteral("splitTitle")]);
        }
        // the look of what was clicked: the notes are part of the inspector, composition mode has
        // its own colors, every other text is the editor's (0.40.2)
        const lookmenu::Part part = ed->isNotes()                                ? lookmenu::Part::Inspector
                                    : qobject_cast<CompositionWindow *>(ed->window()) ? lookmenu::Part::Composition
                                                                                        : lookmenu::Part::Editor;
        lookmenu::add(menu, part, this);
    });
    // "Appearance Options..." of those menus: File > Options > Appearance at the part's page,
    // showing the open project's look
    lookmenu::setOptionsOpener([this](const QString &page) {
        OptionsDialog dlg(this);
        dlg.showSection(OptionsDialog::SAppearance, page);
        dlg.exec();
    });

    connect(&AppSettings::instance(), &AppSettings::changed, this, &MainWindow::applySettings);
    // File > Options > General > Reset All Warnings: the project's "don't ask again" answers, too (0.21)
    connect(&AppSettings::instance(), &AppSettings::warningsReset, this, [this] {
        if (!m_project)
            return;
        for (const QString &k : m_project->uiState.keys())
            if (k.endsWith(QLatin1String("DontAsk")))
                m_project->uiState.remove(k);
    });
    // a new default text zoom (File > Options > Editing) reaches the open project's editors (0.21)
    connect(&zoom::notifier(), &zoom::Notifier::changed, this, [this](const QString &key, double f) {
        if (m_project && key == QLatin1String(zoom::kEditor))
            m_project->docs()->setZoom(f);
    });
    // File > Options > General > Automatic Quit (0.21): every key and mouse input counts
    m_lastInput = QDateTime::currentDateTime();
    qApp->installEventFilter(new InputWatch(this));
    m_autoQuitTimer = new QTimer(this);
    m_autoQuitTimer->setInterval(20000);
    connect(m_autoQuitTimer, &QTimer::timeout, this, [this] {
        if (!autoQuitDue())
            return;
        crash::note(QStringLiteral("automatic quit after %1 min without input").arg(AppSettings::instance().autoQuitMinutes));
        close();
    });
    m_autoQuitTimer->start();
    // "Use Current" buttons in the options and the project settings
    currentFormatting::setProvider([this](Formatting *f) {
        return formattingAtCursor(m_active ? m_active->currentEditor() : nullptr, f);
    });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (!now)
            return;
        // the text field the format bar acts on (0.21.1): the one the keyboard was in last,
        // also while the keyboard is on the format bar (the size field) or in a popup
        if (qobject_cast<TextEditor *>(now) || qobject_cast<ObjectTextEdit *>(now)) {
            m_lastTextField = qobject_cast<QTextEdit *>(now);
        } else {
            const bool bar = (m_formatBar && m_formatBar->isAncestorOf(now)) || (m_toolbar && m_toolbar->isAncestorOf(now));
            const bool popup = now->window() && (now->window()->windowType() == Qt::Popup || qobject_cast<QMenu *>(now->window()));
            if (!bar && !popup)
                m_lastTextField = nullptr;
        }
        for (EditorPane *p : m_panes)
            if (p->isAncestorOf(now) && p != m_active)
                setActivePane(p);
        updateFormatBar();
        // the inspector shows the copyholder's document while the keyboard is in it
        if (m_active && m_active->hasCopyholder() && m_active->isAncestorOf(now))
            refreshInspector();
    });

    QSettings s;
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(s.value(QStringLiteral("window/state")).toByteArray());
    const QVariantList sizes = s.value(QStringLiteral("window/mainSplit")).toList();
    if (sizes.size() == 3)
        m_mainSplit->setSizes({sizes[0].toInt(), sizes[1].toInt(), sizes[2].toInt()});
    if (!s.contains(QStringLiteral("window/geometry")))
        resize(1300, 820);
    a[QStringLiteral("showInspector")]->setChecked(s.value(QStringLiteral("window/inspector"), true).toBool());
    a[QStringLiteral("showBinder")]->setChecked(s.value(QStringLiteral("window/binder"), true).toBool());
    a[QStringLiteral("showToolbar")]->setChecked(s.value(QStringLiteral("window/toolbar"), true).toBool());
    a[QStringLiteral("showFormatBar")]->setChecked(s.value(QStringLiteral("window/formatBar"), true).toBool());
    // restoreState() above may have shown or hidden the bars on its own
    m_toolbar->setVisible(a[QStringLiteral("showToolbar")]->isChecked());
    m_formatBar->setVisible(a[QStringLiteral("showFormatBar")]->isChecked());
    applySettings();
    updateActions();
}

MainWindow::~MainWindow()
{
    // The project goes away with the members below: its documents leave the editors, which
    // report a moved cursor or a changed selection. The window, half gone by then, must not
    // answer them (seen when a window with an open project and the cursor inside a text is
    // deleted without being closed first, 0.40.1).
    for (EditorPane *p : std::as_const(m_panes))
        disconnect(p, nullptr, this, nullptr);
    if (toolhost::host().mainWindow() == this)
        toolhost::host().setWindow(nullptr);
    // players inside the window go away after this window's members
    disconnect(&playback::hub(), nullptr, this, nullptr);
}

void MainWindow::connectPane(EditorPane *p)
{
    connect(p->ruler(), &Ruler::paragraphDialogRequested, this, [this, p] {
        if (TextEditor *e = p->currentEditor()) {
            showParagraphDialog(e);
            updateFormatBar();
        }
    });
    connect(p, &EditorPane::activated, this, &MainWindow::setActivePane);
    connect(p, &EditorPane::currentItemChanged, this, [this](EditorPane *pane) {
        if (pane == m_active) {
            refreshInspector();
            updateFormatBar();
        }
    });
    connect(p, &EditorPane::viewChanged, this, [this](EditorPane *pane) {
        if (pane == m_active) {
            updateViewButtons();
            refreshInspector();
            updateActions();
        }
    });
    connect(p, &EditorPane::selectionChanged, this, [this](EditorPane *pane) {
        if (pane == m_active)
            updateActions();
    });
    connect(p, &EditorPane::cursorMoved, this, [this](TextEditor *) {
        updateFormatBar();
        refreshInspector();
    });
    connect(p, &EditorPane::splitButtonClicked, this, [this](EditorPane *, bool alt) {
        if (isSplit())
            setSplit(m_editorSplit->orientation(), false);
        else
            setSplit(alt ? Qt::Horizontal : Qt::Vertical, true);
    });
    connect(p, &EditorPane::linkActivated, this, &MainWindow::openLink);
    connect(p, &EditorPane::noteClicked, this, [this](const QString &id) {
        m_inspector->showTab(Inspector::Comments);
        m_inspector->focusNote(id);
        if (!m_inspector->isVisible()) {
            m_inspector->show();
            a[QStringLiteral("showInspector")]->setChecked(true);
        }
    });
    connect(p, &EditorPane::itemContextMenuRequested, this,
            [this](EditorPane *pane, const QPoint &pos) {
                setActivePane(pane);
                showItemContextMenu(pos, int(pane->mode() == ViewMode::Outliner ? lookmenu::Part::Outliner
                                                                                 : lookmenu::Part::Corkboard));
            });
    connect(p, &EditorPane::deleteRequested, this, [this](EditorPane *pane) {
        setActivePane(pane);
        a[QStringLiteral("trash")]->trigger();
    });
    connect(p, &EditorPane::newItemRequested, this, [this](EditorPane *pane) {
        setActivePane(pane);
        a[QStringLiteral("newText")]->trigger();
    });
    connect(p, &EditorPane::filesDropped, this, [this](BinderItem *container, int row, const QList<QUrl> &urls) {
        importDropped(container, row, urls, false); // the corkboard or outliner stays as it is
    });
    connect(p, &EditorPane::openInOtherRequested, this, [this](EditorPane *pane, const QList<BinderItem *> &items) {
        EditorPane *o = otherPane(pane);
        if (o && o->isVisible() && !o->isLocked())
            o->showItems(items);
    });
    connect(p, &EditorPane::headerMenuRequested, this, &MainWindow::showHeaderMenu);
    connect(p, &EditorPane::itemsDroppedOnHeader, this, [this](EditorPane *pane, const QList<BinderItem *> &items, bool alt) {
        setActivePane(pane);
        if (alt)
            pane->openCopyholder(items.first()); // Alt+drag: show it beside what the editor has
        else
            loadInPane(pane, items);
    });
    connect(p, &EditorPane::quickReferenceRequested, this, &MainWindow::openQuickReference);
    connect(p, &EditorPane::copyholderChanged, this, [this] { updateActions(); });
    connect(p, &EditorPane::navigateSibling, this, [this](EditorPane *pane, bool next) {
        setActivePane(pane);
        a[next ? QStringLiteral("nextDoc") : QStringLiteral("prevDoc")]->trigger();
    });
}

// ------------------------------------------------------------------ actions

void MainWindow::createActions()
{
    auto act = [this](const QString &key, const QString &text, const QString &icon = QString(),
                      const QKeySequence &ks = QKeySequence()) {
        auto *ac = new QAction(text, this);
        ac->setObjectName(key);
        {
            // text for toolbar buttons: without "&" and without the "…" of commands that ask first
            QString plain = text;
            plain.remove(QLatin1Char('&'));
            if (plain.endsWith(QChar(0x2026)))
                plain.chop(1);
            ac->setIconText(plain);
        }
        if (!icon.isEmpty())
            ac->setIcon(icons::action(icon));
        if (!ks.isEmpty())
            ac->setShortcut(ks);
        a.insert(key, ac);
        addAction(ac);
        return ac;
    };
    auto withProject = [this](auto fn) {
        return [this, fn]() {
            if (m_project)
                fn();
        };
    };

    // ---- file
    connect(act(QStringLiteral("newProject"), tr("&New Project…"), QStringLiteral("document-new"),
                QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N)),
            &QAction::triggered, this, [this] { showStartDialog(false); });
    connect(act(QStringLiteral("openProject"), tr("&Open Project…"), QStringLiteral("document-open"), QKeySequence::Open),
            &QAction::triggered, this, [this] {
                const QString f = QFileDialog::getOpenFileName(this, tr("Open Project"), AppSettings::instance().lastProjectDir,
                                                               projectFileFilter());
                if (!f.isEmpty())
                    openProject(f);
            });
    connect(act(QStringLiteral("importScrivener"), tr("Import &Scrivener Project…"), QStringLiteral("document-import")),
            &QAction::triggered, this, [this] {
                auto p = runScrivenerImport(this, a[QStringLiteral("importScrivener")]->data().toString());
                if (p)
                    adoptProject(std::move(p), true);
            });
    // File > Project Format
    {
        QAction *own = act(QStringLiteral("formatQuillbinder"), tr("&%1 Project").arg(QStringLiteral(QB_APP_NAME)));
        QAction *scriv = act(QStringLiteral("formatScrivener"), tr("&Scrivener 3 Project (.scriv)"));
        own->setCheckable(true);
        scriv->setCheckable(true);
        auto *group = new QActionGroup(this);
        group->addAction(own);
        group->addAction(scriv);
        own->setToolTip(tr("Save the project in %1's own format").arg(QStringLiteral(QB_APP_NAME)));
        scriv->setToolTip(tr("Save the project in the format of Scrivener 3, so that Scrivener can open it too"));
        connect(own, &QAction::triggered, this, [this] { switchToQuillbinderFormat(); });
        connect(scriv, &QAction::triggered, this, [this] { switchToScrivenerFormat(); });
        connect(act(QStringLiteral("saveCopyScrivener"), tr("Save a Copy as Scrivener Project…"), QStringLiteral("document-save-as")),
                &QAction::triggered, this, withProject([this] { saveCopyAsScrivener(); }));
        connect(act(QStringLiteral("saveCopyQuillbinder"), tr("Save a Copy as %1 Project…").arg(QStringLiteral(QB_APP_NAME)),
                    QStringLiteral("document-save-as")),
                &QAction::triggered, this, withProject([this] { saveCopyAsQuillbinder(); }));
    }
    connect(act(QStringLiteral("closeProject"), tr("&Close Project"), QStringLiteral("document-close"), QKeySequence::Close),
            &QAction::triggered, this, [this] {
                closeProject();
                showStartDialog(true);
            });
    connect(act(QStringLiteral("save"), tr("&Save"), QStringLiteral("document-save"), QKeySequence::Save),
            &QAction::triggered, this, withProject([this] { manualSave(); }));
    connect(act(QStringLiteral("saveTemplate"), tr("Save as &Template…")), &QAction::triggered, this, withProject([this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save as Template"), tr("Template name:"), QLineEdit::Normal,
                                                   m_project->title(), &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
        const QString cat = QInputDialog::getItem(this, tr("Save as Template"), tr("Category:"), templates::categories(),
                                                  3, true, &ok);
        if (!ok)
            return;
        const QString desc = QInputDialog::getText(this, tr("Save as Template"), tr("Description:"), QLineEdit::Normal,
                                                   QString(), &ok);
        QString err;
        if (templates::saveAsTemplate(m_project.get(), name.trimmed(), cat, desc, &err))
            QMessageBox::information(this, tr("Save as Template"), tr("The template \"%1\" is now available under File ▸ New Project.").arg(name));
        else
            QMessageBox::warning(this, tr("Save as Template"), err);
    }));
    connect(act(QStringLiteral("backupNow"), tr("Back Up &Now"), QStringLiteral("document-save-all")), &QAction::triggered,
            this, withProject([this] { backupNow(false); }));
    connect(act(QStringLiteral("backupTo"), tr("Back Up &To…")), &QAction::triggered, this,
            withProject([this] { backupNow(true); }));
    connect(act(QStringLiteral("showBackups"), tr("Show Backup Folder")), &QAction::triggered, this, [] {
        const QString d = AppSettings::instance().backupDir.isEmpty() ? backup::defaultDirectory() : AppSettings::instance().backupDir;
        QDir().mkpath(d);
        QDesktopServices::openUrl(QUrl::fromLocalFile(d));
    });
    connect(act(QStringLiteral("importFiles"), tr("Import &Files…"), QStringLiteral("document-import"),
                QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I)),
            &QAction::triggered, this, withProject([this] { importFiles(); }));
    connect(act(QStringLiteral("importWebPage"), tr("Import &Web Page…"), QStringLiteral("text-html")),
            &QAction::triggered, this, withProject([this] { importWebPage(); }));
    connect(act(QStringLiteral("importSplit"), tr("Import and &Split…")), &QAction::triggered, this, withProject([this] {
        ImportSplitDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted || dlg.file().isEmpty())
            return;
        int row = -1;
        BinderItem *parent = insertionPoint(&row);
        if (!parent->isFolder() && !m_project->isInDraft(parent))
            parent = m_project->draft();
        QString err;
        QStringList warnings;
        const auto items = importer::importAndSplit(m_project.get(), dlg.file(), dlg.separator(), dlg.firstLineIsTitle(),
                                                    parent, row, &err, &warnings);
        if (items.isEmpty()) {
            QMessageBox::warning(this, tr("Import and Split"), err.isEmpty() ? tr("Nothing was imported.") : err);
        } else {
            m_binder->select(items, true);
            if (!warnings.isEmpty())
                QMessageBox::information(this, tr("Import and Split"), warnings.join(QLatin1Char('\n')));
        }
    }));
    connect(act(QStringLiteral("exportFiles"), tr("&Export Files…"), QStringLiteral("document-export")),
            &QAction::triggered, this, withProject([this] { exportItems(m_project.get(), actionItems(), this); }));
    connect(act(QStringLiteral("compile"), tr("&Compile…"), QStringLiteral("document-export"),
                QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E)),
            &QAction::triggered, this, withProject([this] {
                m_project->save();
                CompileDialog dlg(m_project.get(), this);
                dlg.exec();
            }));
    connect(act(QStringLiteral("convertFile"), tr("Convert &Markdown ⇄ RTF File…"), QStringLiteral("view-refresh")),
            &QAction::triggered, this, [this] { convertFileTool(); });
    connect(act(QStringLiteral("printSettings"), tr("Print Se&ttings…"), QStringLiteral("printer")), &QAction::triggered,
            this, withProject([this] { showPrintSettings(); }));
    a[QStringLiteral("printSettings")]->setToolTip(tr("Paper, header and footer, and how texts, index cards and the outliner are printed"));
    connect(act(QStringLiteral("printPreview"), tr("Print Pre&view…"), QStringLiteral("document-print-preview")),
            &QAction::triggered, this, [this] { printPreview(); });
    connect(act(QStringLiteral("print"), tr("&Print Current Document…"), QStringLiteral("document-print"), QKeySequence::Print),
            &QAction::triggered, this, [this] { printDocument(); });
    a[QStringLiteral("print")]->setToolTip(tr("Prints what the editor shows: the text, the index cards of the corkboard, "
                                             "the outliner, a picture or a PDF file"));
    connect(act(QStringLiteral("options"), tr("&Options…"), QStringLiteral("configure"), QKeySequence(Qt::CTRL | Qt::Key_Comma)),
            &QAction::triggered, this, [this] {
                OptionsDialog dlg(this);
                dlg.exec();
            });
    connect(act(QStringLiteral("quit"), tr("E&xit"), QStringLiteral("application-exit"), QKeySequence::Quit),
            &QAction::triggered, this, &QWidget::close);

    // ---- themes (View > Theme)
    connect(act(QStringLiteral("themeSave"), tr("&Save Current Look as Theme…"), QStringLiteral("document-save-as")),
            &QAction::triggered, this, [this] { themeui::saveCurrentLook(this); });
    connect(act(QStringLiteral("themeImport"), tr("&Import Theme…"), QStringLiteral("document-import")), &QAction::triggered,
            this, [this] {
                const QString id = themeui::importFromFile(this);
                if (id.isEmpty())
                    return;
                theme::Theme t;
                theme::load(id, &t);
                if (QMessageBox::question(this, tr("Import Theme"), tr("The theme “%1” was added. Use it now?").arg(t.name))
                    == QMessageBox::Yes)
                    themeui::choose(this, id);
            });
    connect(act(QStringLiteral("themeManage"), tr("&Manage Themes…")), &QAction::triggered, this, [this] {
        ThemeManagerDialog dlg(this);
        dlg.exec();
    });
    connect(act(QStringLiteral("themeCustomize"), tr("&Colors and Fonts…"), QStringLiteral("preferences-desktop-color")),
            &QAction::triggered, this, [this] {
                OptionsDialog dlg(this, OptionsDialog::Appearance);
                dlg.exec();
            });

    // ---- edit (routed to the focused text widget)
    auto route = [](auto textEditFn, auto plainFn, auto lineFn) {
        return [=]() {
            QWidget *fw = QApplication::focusWidget();
            if (auto *te = qobject_cast<QTextEdit *>(fw))
                textEditFn(te);
            else if (auto *pe = qobject_cast<QPlainTextEdit *>(fw))
                plainFn(pe);
            else if (auto *le = qobject_cast<QLineEdit *>(fw))
                lineFn(le);
        };
    };
    connect(act(QStringLiteral("undo"), tr("&Undo"), QStringLiteral("edit-undo"), QKeySequence::Undo), &QAction::triggered,
            this, route([](QTextEdit *e) { e->undo(); }, [](QPlainTextEdit *e) { e->undo(); }, [](QLineEdit *e) { e->undo(); }));
    connect(act(QStringLiteral("redo"), tr("&Redo"), QStringLiteral("edit-redo"), QKeySequence::Redo), &QAction::triggered,
            this, route([](QTextEdit *e) { e->redo(); }, [](QPlainTextEdit *e) { e->redo(); }, [](QLineEdit *e) { e->redo(); }));
    connect(act(QStringLiteral("cut"), tr("Cu&t"), QStringLiteral("edit-cut"), QKeySequence::Cut), &QAction::triggered, this,
            route([](QTextEdit *e) { e->cut(); }, [](QPlainTextEdit *e) { e->cut(); }, [](QLineEdit *e) { e->cut(); }));
    connect(act(QStringLiteral("copy"), tr("&Copy"), QStringLiteral("edit-copy"), QKeySequence::Copy), &QAction::triggered,
            this, route([](QTextEdit *e) { e->copy(); }, [](QPlainTextEdit *e) { e->copy(); }, [](QLineEdit *e) { e->copy(); }));
    connect(act(QStringLiteral("paste"), tr("&Paste"), QStringLiteral("edit-paste"), QKeySequence::Paste), &QAction::triggered,
            this, route([](QTextEdit *e) { e->paste(); }, [](QPlainTextEdit *e) { e->paste(); }, [](QLineEdit *e) { e->paste(); }));
    connect(act(QStringLiteral("pastePlain"), tr("Paste and &Match Style"), QString(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V)),
            &QAction::triggered, this, [this] {
                if (TextEditor *e = editor()) {
                    QTextCharFormat cf = e->textCursor().charFormat();
                    text::clearNoteProps(cf);
                    const QStringList lines = QApplication::clipboard()->text().split(QLatin1Char('\n'));
                    QTextCursor c = e->textCursor();
                    c.beginEditBlock();
                    for (int i = 0; i < lines.size(); ++i) {
                        if (i)
                            c.insertBlock(display::normalBlockFormat(m_project->formatting));
                        c.insertText(QString(lines[i]).remove(QLatin1Char('\r')), cf);
                    }
                    c.endEditBlock();
                }
            });
    connect(act(QStringLiteral("selectAll"), tr("Select &All"), QStringLiteral("edit-select-all"), QKeySequence::SelectAll),
            &QAction::triggered, this,
            route([](QTextEdit *e) { e->selectAll(); }, [](QPlainTextEdit *e) { e->selectAll(); }, [](QLineEdit *e) { e->selectAll(); }));
    connect(act(QStringLiteral("find"), tr("&Find…"), QStringLiteral("edit-find"), QKeySequence::Find), &QAction::triggered,
            this, [this] { m_active->showFindBar(false); });
    connect(act(QStringLiteral("replace"), tr("Find and &Replace…"), QStringLiteral("edit-find-replace"),
                QKeySequence(Qt::CTRL | Qt::Key_H)),
            &QAction::triggered, this, [this] { m_active->showFindBar(true); });
    connect(act(QStringLiteral("findNext"), tr("Find &Next"), QString(), QKeySequence(Qt::Key_F3)), &QAction::triggered, this,
            [this] { m_active->findNext(false); });
    connect(act(QStringLiteral("findPrev"), tr("Find Pre&vious"), QString(), QKeySequence(Qt::SHIFT | Qt::Key_F3)),
            &QAction::triggered, this, [this] { m_active->findNext(true); });
    connect(act(QStringLiteral("useSelectionForFind"), tr("Use &Selection for Find"), QString(), QKeySequence(Qt::CTRL | Qt::Key_E)),
            &QAction::triggered, this, [this] { m_active->useSelectionForFind(); });
    connect(act(QStringLiteral("projectReplace"), tr("Project &Replace…"), QStringLiteral("edit-find-replace")),
            &QAction::triggered, this, withProject([this] { projectReplace(); }));
    connect(act(QStringLiteral("findFormat"), tr("Find by For&matting…"), QStringLiteral("format-text-color"),
                QKeySequence(Qt::CTRL | Qt::Key_F3)),
            &QAction::triggered, this, withProject([this] { showFormatFind(); }));
    connect(act(QStringLiteral("findFormatNext"), tr("Find Next Formattin&g"), QString(), QKeySequence(Qt::CTRL | Qt::Key_G)),
            &QAction::triggered, this, withProject([this] { findFormatting(false); }));
    connect(act(QStringLiteral("findFormatPrev"), tr("Find Previous Formatting"), QString(),
                QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G)),
            &QAction::triggered, this, withProject([this] { findFormatting(true); }));
    connect(act(QStringLiteral("projectSearch"), tr("&Project Search…"), QStringLiteral("search"),
                QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F)),
            &QAction::triggered, this, withProject([this] {
                m_binder->show();
                a[QStringLiteral("showBinder")]->setChecked(true);
                // the search button shows the field, goes to it, and hides it when the keyboard is in it
                if (m_binder->isSearching() && m_binder->searchField()->hasFocus())
                    m_binder->hideSearch();
                else
                    m_binder->showSearch(true);
            }));
    connect(act(QStringLiteral("docLink"), tr("Link to &Document…"), QStringLiteral("insert-link"),
                QKeySequence(Qt::CTRL | Qt::Key_L)),
            &QAction::triggered, this, withProject([this] {
                TextEditor *e = editor();
                if (!e)
                    return;
                QStringList titles;
                QList<BinderItem *> items;
                for (BinderItem *it : m_project->allItems())
                    if (!it->isRootFolder() && !m_project->isInTrash(it)) {
                        QString path;
                        for (BinderItem *p = it->parent; p && p->parent; p = p->parent)
                            path.prepend(p->title + QStringLiteral(" › "));
                        titles << path + it->title;
                        items << it;
                    }
                bool ok = false;
                const QString t = QInputDialog::getItem(this, tr("Link to Document"), tr("Document:"), titles, 0, true, &ok);
                if (!ok)
                    return;
                const int i = titles.indexOf(t);
                if (i >= 0)
                    e->insertLink(QStringLiteral("qb://") + items[i]->id, items[i]->title);
            }));
    // a link to an object (0.20): a click on it shows the object in the inspector
    connect(act(QStringLiteral("objectLink"), tr("Link to &Object…"), QStringLiteral("qb-objects")), &QAction::triggered, this,
            withProject([this] {
                TextEditor *e = editor();
                if (!e)
                    return;
                const QString id = ObjectChooser::choose(m_project.get(), ObjectChooser::Objects, {}, {}, this, tr("Link to Object"));
                if (const objects::Object *o = id.isEmpty() ? nullptr : m_project->objects()->object(id))
                    e->insertLink(objects::linkTo(id), o->name);
            }));
    // the objects of the project (0.20): a window of their own, and their templates and colors
    connect(act(QStringLiteral("objectsWindow"), tr("&Objects…"), QStringLiteral("qb-objects")), &QAction::triggered, this,
            withProject([this] {
                const QString cur = m_inspector->objectsPanel()->currentObject();
                if (ObjectsWindow *ow = ObjectsWindow::open(m_project.get(), cur, this))
                    shareShortcuts(ow); // Ctrl+B and the like in the text of objects (0.21.1)
            }));
    connect(act(QStringLiteral("objectTemplates"), tr("Object &Templates and Colors…")), &QAction::triggered, this,
            withProject([this] { ObjectTemplatesDialog::run(m_project.get(), m_inspector->objectsPanel()->category(), this); }));
    connect(act(QStringLiteral("hyperlink"), tr("Insert &Web Link…"), QStringLiteral("insert-link")), &QAction::triggered, this,
            [this] {
                TextEditor *e = editor();
                if (!e)
                    return;
                bool ok = false;
                const QString url = QInputDialog::getText(this, tr("Insert Web Link"), tr("Address:"), QLineEdit::Normal,
                                                          QStringLiteral("https://"), &ok);
                if (ok && !url.trimmed().isEmpty())
                    e->insertLink(url.trimmed(), url.trimmed());
            });
    connect(act(QStringLiteral("moveUp"), tr("Move &Up"), QStringLiteral("arrow-up"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Up)),
            &QAction::triggered, this, [this] { moveSelection(0, -1); });
    connect(act(QStringLiteral("moveDown"), tr("Move &Down"), QStringLiteral("arrow-down"),
                QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Down)),
            &QAction::triggered, this, [this] { moveSelection(0, 1); });
    connect(act(QStringLiteral("moveLeft"), tr("Move &Left (out of the folder)"), QStringLiteral("arrow-left"),
                QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Left)),
            &QAction::triggered, this, [this] { moveSelection(-1, 0); });
    connect(act(QStringLiteral("moveRight"), tr("Move &Right (into the item above)"), QStringLiteral("arrow-right"),
                QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Right)),
            &QAction::triggered, this, [this] { moveSelection(1, 0); });

    // ---- view
    m_viewGroup = new QActionGroup(this);
    m_viewGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    auto viewAct = [&](const QString &key, const QString &text, const QString &icon, const QKeySequence &ks, ViewMode mode) {
        QAction *ac = act(key, text, icon, ks);
        ac->setCheckable(true);
        m_viewGroup->addAction(ac);
        connect(ac, &QAction::triggered, this, [this, mode](bool checked) {
            if (!m_project || m_updatingUi)
                return;
            EditorPane *p = m_active;
            if (mode == ViewMode::Combined) {
                if (p->isGroup())
                    p->setViewMode(checked ? ViewMode::Combined : ViewMode::Single);
                else
                    p->setViewMode(ViewMode::Single);
            } else {
                if (checked)
                    p->setViewMode(mode);
                else
                    p->setViewMode(p->isGroup() && p->items().size() == 1 ? ViewMode::Single
                                   : p->isGroup()                         ? ViewMode::Combined
                                                                          : ViewMode::Single);
            }
            updateViewButtons();
        });
    };
    viewAct(QStringLiteral("viewText"), tr("&Combined Text / Document"), QStringLiteral("view-pim-journal"),
            QKeySequence(Qt::CTRL | Qt::Key_1), ViewMode::Combined);
    viewAct(QStringLiteral("viewCork"), tr("&Corkboard"), QStringLiteral("view-list-icons"), QKeySequence(Qt::CTRL | Qt::Key_2),
            ViewMode::Corkboard);
    viewAct(QStringLiteral("viewOutline"), tr("&Outliner"), QStringLiteral("view-list-details"),
            QKeySequence(Qt::CTRL | Qt::Key_3), ViewMode::Outliner);
    connect(act(QStringLiteral("noSplit"), tr("&No Split"), QStringLiteral("view-close")), &QAction::triggered, this,
            [this] { setSplit(Qt::Vertical, false); });
    connect(act(QStringLiteral("splitH"), tr("Split &Horizontally"), QStringLiteral("view-split-top-bottom")),
            &QAction::triggered, this, [this] { setSplit(Qt::Vertical, true); });
    connect(act(QStringLiteral("splitV"), tr("Split &Vertically"), QStringLiteral("view-split-left-right")),
            &QAction::triggered, this, [this] { setSplit(Qt::Horizontal, true); });
    QAction *sb = act(QStringLiteral("showBinder"), tr("Show &Binder"), QStringLiteral("view-list-tree"),
                      QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_B));
    sb->setCheckable(true);
    sb->setChecked(true);
    connect(sb, &QAction::toggled, m_binder, &QWidget::setVisible);
    QAction *si = act(QStringLiteral("showInspector"), tr("Show &Inspector"), QStringLiteral("dialog-information"),
                      QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I));
    si->setCheckable(true);
    si->setChecked(true);
    connect(si, &QAction::toggled, m_inspector, &QWidget::setVisible);
    QAction *cols = act(QStringLiteral("showCollections"), tr("Show &Collections"), QStringLiteral("bookmarks"));
    cols->setCheckable(true);
    connect(cols, &QAction::toggled, m_binder, &BinderPanel::setCollectionsVisible);
    connect(act(QStringLiteral("composition"), tr("&Composition Mode"), QStringLiteral("view-fullscreen"),
                QKeySequence(Qt::Key_F11)),
            &QAction::triggered, this, [this] { enterComposition(); });
    QAction *pv = act(QStringLiteral("pageView"), tr("&Page View"), QStringLiteral("view-pages-continuous"));
    pv->setCheckable(true);
    pv->setToolTip(tr("Show the text on pages"));
    connect(pv, &QAction::triggered, this, [this](bool on) {
        if (m_project)
            m_project->docs()->setPageView(on);
        updateViewButtons();
    });
    connect(act(QStringLiteral("pageSetup"), tr("Page &Size and Margins…"), QStringLiteral("page-simple")), &QAction::triggered,
            this, [this] { showPageSetup(); });
    QAction *tw = act(QStringLiteral("typewriter"), tr("&Typewriter Scrolling"), QString(),
                      QKeySequence(Qt::META | Qt::CTRL | Qt::Key_T));
    tw->setCheckable(true);
    connect(tw, &QAction::toggled, this, [this](bool on) {
        AppSettings::instance().typewriterScrolling = on;
        AppSettings::instance().save();
        for (EditorPane *p : m_panes)
            p->textView()->setTypewriter(on);
    });
    QAction *titles = act(QStringLiteral("scrivTitles"), tr("Show &Titles in Combined Text"));
    titles->setCheckable(true);
    connect(titles, &QAction::toggled, this, [this](bool on) {
        if (m_updatingUi)
            return;
        AppSettings::instance().showTitlesInCombined = on;
        AppSettings::instance().save();
        for (EditorPane *p : m_panes)
            if (p->mode() == ViewMode::Combined)
                p->showItems(p->items(), false, int(ViewMode::Combined));
    });
    QAction *fixed = act(QStringLiteral("fixedWidth"), tr("&Fixed Width Editor"));
    fixed->setCheckable(true);
    connect(fixed, &QAction::toggled, this, [this](bool on) {
        if (m_updatingUi)
            return;
        AppSettings::instance().fixedWidth = on;
        AppSettings::instance().save();
        for (EditorPane *p : m_panes)
            p->textView()->applySettings();
    });
    connect(act(QStringLiteral("zoomIn"), tr("Zoom &In"), QStringLiteral("zoom-in"), QKeySequence::ZoomIn),
            &QAction::triggered, this, withProject([this] { m_project->docs()->setZoom(m_project->docs()->zoom() + 0.1); }));
    connect(act(QStringLiteral("zoomOut"), tr("Zoom &Out"), QStringLiteral("zoom-out"), QKeySequence::ZoomOut),
            &QAction::triggered, this, withProject([this] { m_project->docs()->setZoom(m_project->docs()->zoom() - 0.1); }));
    // 0.21: back to the default text zoom of File > Options > Editing, and the inspector's zoom per tab
    connect(act(QStringLiteral("zoomReset"), tr("&Reset Zoom"), QStringLiteral("zoom-original"), QKeySequence(Qt::CTRL | Qt::Key_0)),
            &QAction::triggered, this, withProject([this] { m_project->docs()->setZoom(zoom::defaultFactor(QLatin1String(zoom::kEditor))); }));
    QAction *perTab = act(QStringLiteral("zoomInspectorPerTab"), tr("Zoom Inspector &Tabs Separately"));
    perTab->setCheckable(true);
    perTab->setChecked(zoom::perTab());
    connect(perTab, &QAction::toggled, this, [](bool on) { zoom::setPerTab(on); });
    connect(&zoom::notifier(), &zoom::Notifier::modeChanged, perTab, [perTab] {
        const QSignalBlocker b(perTab);
        perTab->setChecked(zoom::perTab());
    });
    QAction *stamps = act(QStringLiteral("stamps"), tr("Status &Stamps"));
    stamps->setCheckable(true);
    connect(stamps, &QAction::toggled, this, [this](bool on) {
        if (m_updatingUi)
            return;
        AppSettings::instance().showStatusStamps = on;
        AppSettings::instance().save();
        for (EditorPane *p : m_panes)
            p->corkboard()->viewport()->update();
    });
    QAction *tint = act(QStringLiteral("tintCards"), tr("Label Color in &Index Cards"));
    tint->setCheckable(true);
    connect(tint, &QAction::toggled, this, [this](bool on) {
        if (m_updatingUi)
            return;
        AppSettings::instance().tintCardsWithLabel = on;
        AppSettings::instance().save();
        for (EditorPane *p : m_panes)
            p->corkboard()->rebuild();
    });
    QAction *fb = act(QStringLiteral("showFormatBar"), tr("Show &Format Bar"));
    fb->setCheckable(true);
    fb->setChecked(true);

    // ---- project
    connect(act(QStringLiteral("newText"), tr("New &Text"), QStringLiteral("document-new"), QKeySequence::New),
            &QAction::triggered, this, withProject([this] { newItem(ItemType::Text); }));
    connect(act(QStringLiteral("newFolder"), tr("New &Folder"), QStringLiteral("folder-new"),
                QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_N)),
            &QAction::triggered, this, withProject([this] { newItem(ItemType::Folder); }));
    connect(act(QStringLiteral("projectSettings"), tr("Project &Settings…"), QStringLiteral("document-properties")),
            &QAction::triggered, this, withProject([this] {
                ProjectSettingsDialog dlg(m_project.get(), 0, this);
                dlg.exec();
            }));
    // the project's settings in a file of their own, and back (0.40.2)
    connect(act(QStringLiteral("settingsExport"), tr("Sa&ve Project Settings to File…"), QStringLiteral("document-export")),
            &QAction::triggered, this, withProject([this] { exportProjectSettings(); }));
    connect(act(QStringLiteral("settingsImport"), tr("&Load Project Settings from File…"), QStringLiteral("document-import")),
            &QAction::triggered, this, withProject([this] { importProjectSettings(); }));
    connect(act(QStringLiteral("targets"), tr("Project &Targets…"), QStringLiteral("flag"),
                QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T)),
            &QAction::triggered, this, withProject([this] {
                auto *d = new TargetsDialog(m_project.get(), this);
                d->show();
            }));
    connect(act(QStringLiteral("keywords"), tr("Project &Keywords…"), QStringLiteral("tag"),
                QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_K)),
            &QAction::triggered, this, withProject([this] {
                auto *d = new KeywordsDialog(m_project.get(), this);
                d->setSelectionProvider([this] { return actionItems(); });
                connect(d, &KeywordsDialog::searchRequested, this, [this](const QString &k) {
                    SearchQuery q;
                    q.text = k;
                    q.inTitle = q.inText = q.inSynopsis = q.inNotes = q.inCustom = false;
                    q.inKeywords = true;
                    q.exactPhrase = true;
                    m_binder->show();
                    m_binder->showSearch(false);
                    m_binder->runSearch(q);
                });
                d->show();
            }));
    connect(act(QStringLiteral("statistics"), tr("Project S&tatistics…"), QStringLiteral("view-statistics")),
            &QAction::triggered, this, withProject([this] {
                StatisticsDialog d(m_project.get(), this);
                d.exec();
            }));
    connect(act(QStringLiteral("history"), tr("&Writing History…"), QStringLiteral("view-calendar")), &QAction::triggered,
            this, withProject([this] {
                HistoryDialog d(m_project.get(), this);
                d.exec();
            }));
    connect(act(QStringLiteral("emptyTrash"), tr("&Empty Trash…"), QStringLiteral("trash-empty")), &QAction::triggered, this,
            withProject([this] {
                if (m_project->trash()->children.isEmpty())
                    return;
                if (QMessageBox::question(this, tr("Empty Trash"),
                                          tr("Delete %1 in the Trash forever? This cannot be undone.")
                                              .arg(text::plural(int(m_project->trash()->descendants().size()),
                                                                tr("document"), tr("documents"))))
                    != QMessageBox::Yes)
                    return;
                for (EditorPane *p : m_panes)
                    p->showItems({}, false);
                m_project->model()->emptyTrash();
                m_project->save();
            }));
    connect(act(QStringLiteral("showProjectFolder"), tr("Show Project in File &Manager"), QStringLiteral("folder-open")),
            &QAction::triggered, this, withProject([this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_project->displayFolder())); }));

    // ---- documents
    connect(act(QStringLiteral("openOther"), tr("Open in &Other Editor"), QStringLiteral("view-split-left-right")),
            &QAction::triggered, this, [this] {
                const auto items = actionItems();
                if (items.isEmpty())
                    return;
                EditorPane *o = ensureOtherPane();
                o->showItems(items);
            });
    connect(act(QStringLiteral("quickRef"), tr("Open as &Quick Reference"), QStringLiteral("window-new")), &QAction::triggered,
            this, [this] {
                for (BinderItem *it : actionItems())
                    openQuickReference(it);
            });
    connect(act(QStringLiteral("openCopyholder"), tr("Open in &Copyholder"), QStringLiteral("view-right-new")),
            &QAction::triggered, this, [this] {
                const auto items = actionItems();
                if (!items.isEmpty() && m_active)
                    m_active->openCopyholder(items.first());
            });
    connect(act(QStringLiteral("closeCopyholder"), tr("Close Copy&holder"), QStringLiteral("view-right-close")),
            &QAction::triggered, this, [this] {
                if (m_active)
                    m_active->closeCopyholder();
            });
    connect(act(QStringLiteral("rename"), tr("&Rename"), QStringLiteral("edit-rename"), QKeySequence(Qt::Key_F2)),
            &QAction::triggered, this, [this] {
                const auto items = actionItems();
                if (items.size() != 1)
                    return;
                if (m_active->mode() == ViewMode::Corkboard && m_active->corkboard()->hasFocus())
                    m_active->corkboard()->editTitleOf(items.first());
                else
                    m_binder->editTitle(items.first());
            });
    connect(act(QStringLiteral("duplicate"), tr("&Duplicate"), QStringLiteral("edit-copy"), QKeySequence(Qt::CTRL | Qt::Key_D)),
            &QAction::triggered, this, [this] { duplicateItems(); });
    connect(act(QStringLiteral("trash"), tr("Move to &Trash"), QStringLiteral("edit-delete")), &QAction::triggered, this,
            withProject([this] {
                QList<BinderItem *> items;
                for (BinderItem *it : actionItems())
                    if (!it->isRootFolder())
                        items << it;
                if (items.isEmpty())
                    return;
                if (m_project->isInTrash(items.first())) {
                    QMessageBox::information(this, tr("Trash"), tr("Use Project ▸ Empty Trash to delete documents for ever."));
                    return;
                }
                m_project->model()->moveToTrash(items);
                for (EditorPane *p : m_panes)
                    p->refresh();
            }));
    connect(act(QStringLiteral("split"), tr("&Split at Selection"), QString(), QKeySequence(Qt::CTRL | Qt::Key_K)),
            &QAction::triggered, this, [this] { splitDocument(false); });
    connect(act(QStringLiteral("splitTitle"), tr("Split with Selection as &Title"), QString(),
                QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K)),
            &QAction::triggered, this, [this] { splitDocument(true); });
    connect(act(QStringLiteral("merge"), tr("&Merge Documents"), QString(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M)),
            &QAction::triggered, this, [this] { mergeDocuments(); });
    connect(act(QStringLiteral("toFolder"), tr("Convert to &Folder"), QStringLiteral("folder")), &QAction::triggered, this,
            withProject([this] {
                for (BinderItem *it : actionItems())
                    if (it->type == ItemType::Text)
                        m_project->model()->setItemType(it, ItemType::Folder);
                updateActions();
            }));
    connect(act(QStringLiteral("toText"), tr("Convert to F&ile"), QStringLiteral("text-x-generic")),
            &QAction::triggered, this, withProject([this] {
                for (BinderItem *it : actionItems())
                    if (it->type == ItemType::Folder)
                        m_project->model()->setItemType(it, ItemType::Text);
                updateActions();
            }));
    QAction *inc = act(QStringLiteral("includeCompile"), tr("&Include in Compile"));
    inc->setCheckable(true);
    connect(inc, &QAction::triggered, this, withProject([this] {
        const bool on = a[QStringLiteral("includeCompile")]->isChecked();
        for (BinderItem *it : actionItems()) {
            it->includeInCompile = on;
            m_project->notifyItemChanged(it, false);
        }
    }));
    connect(act(QStringLiteral("snapshot"), tr("Take &Snapshot"), QStringLiteral("camera-photo"), QKeySequence(Qt::CTRL | Qt::Key_5)),
            &QAction::triggered, this, withProject([this] {
                int n = 0;
                for (BinderItem *it : actionItems())
                    if (it->hasText() && snapshots::take(m_project.get(), it->id, tr("Snapshot")))
                        ++n;
                m_inspector->refreshAll();
                notifySnapshots(n);
                statusBar();
                QApplication::beep();
                if (n)
                    setWindowTitle(windowTitle()); // no status bar: inspector shows the new snapshot
            }));
    connect(act(QStringLiteral("snapshotTitle"), tr("Take Snapshot with &Title…"), QStringLiteral("camera-photo"),
                QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_5)),
            &QAction::triggered, this, withProject([this] {
                m_inspector->showTab(Inspector::Snapshots);
                m_inspector->takeSnapshot(true);
            }));
    connect(act(QStringLiteral("showSnapshots"), tr("Show Snapshots")), &QAction::triggered, this, [this] {
        m_inspector->show();
        a[QStringLiteral("showInspector")]->setChecked(true);
        m_inspector->showTab(Inspector::Snapshots);
    });
    connect(act(QStringLiteral("bookmark"), tr("Add to Project &Bookmarks"), QStringLiteral("bookmark-new")),
            &QAction::triggered, this, withProject([this] {
                addToProjectBookmarks(actionItems());
            }));
    connect(act(QStringLiteral("reveal"), tr("Reveal in &Binder"), QStringLiteral("go-jump"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R)),
            &QAction::triggered, this, [this] {
                if (BinderItem *it = m_active->currentItem()) {
                    m_binder->show();
                    m_updatingUi = true;
                    m_binder->reveal(it);
                    m_updatingUi = false;
                }
            });
    QAction *lock = act(QStringLiteral("lock"), tr("&Lock in Place"), QStringLiteral("object-locked"));
    lock->setCheckable(true);
    connect(lock, &QAction::triggered, this, [this](bool on) { m_active->setLocked(on); });

    // ---- format
    auto fmt = [&](const QString &key, const QString &text, const QString &icon, const QKeySequence &ks,
                   std::function<void(TextEditor *)> fn, bool checkable = false) {
        QAction *ac = act(key, text, icon, ks);
        ac->setCheckable(checkable);
        connect(ac, &QAction::triggered, this, [this, fn, key] {
            // the text of an object (0.21.1): only the commands it can hold
            if (ObjectTextEdit *o = objectTextTarget()) {
                objectFormat(key, o);
                updateFormatBar();
                return;
            }
            // the keyboard elsewhere in the Objects window: no text to format there
            if (QWidget *f = QApplication::focusWidget(); f && qobject_cast<ObjectsWindow *>(f->window()))
                return;
            if (TextEditor *e = editor()) {
                fn(e);
                updateFormatBar();
            }
        });
        return ac;
    };
    fmt(QStringLiteral("bold"), tr("&Bold"), QStringLiteral("format-text-bold"), QKeySequence::Bold, [](TextEditor *e) { e->toggleBold(); }, true);
    fmt(QStringLiteral("italic"), tr("&Italic"), QStringLiteral("format-text-italic"), QKeySequence::Italic,
        [](TextEditor *e) { e->toggleItalic(); }, true);
    fmt(QStringLiteral("underline"), tr("&Underline"), QStringLiteral("format-text-underline"), QKeySequence::Underline,
        [](TextEditor *e) { e->toggleUnderline(); }, true);
    fmt(QStringLiteral("strike"), tr("&Strikethrough"), QStringLiteral("format-text-strikethrough"), QKeySequence(),
        [](TextEditor *e) { e->toggleStrike(); }, true);
    fmt(QStringLiteral("superscript"), tr("Su&perscript"), QStringLiteral("format-text-superscript"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Plus), [](TextEditor *e) { e->toggleSuperscript(); }, true);
    fmt(QStringLiteral("subscript"), tr("Su&bscript"), QStringLiteral("format-text-subscript"),
        QKeySequence(Qt::CTRL | Qt::Key_Equal), [](TextEditor *e) { e->toggleSubscript(); }, true);
    fmt(QStringLiteral("highlight"), tr("&Highlight"), QStringLiteral("draw-highlight"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H),
        [](TextEditor *e) { e->toggleHighlight(); });
    fmt(QStringLiteral("alignLeft"), tr("Align &Left"), QStringLiteral("format-justify-left"), QKeySequence(Qt::CTRL | Qt::Key_BraceLeft),
        [](TextEditor *e) { e->setAlignment(Qt::AlignLeft); }, true);
    fmt(QStringLiteral("alignCenter"), tr("&Center"), QStringLiteral("format-justify-center"), QKeySequence(Qt::CTRL | Qt::Key_Bar),
        [](TextEditor *e) { e->setAlignment(Qt::AlignHCenter); }, true);
    fmt(QStringLiteral("alignRight"), tr("Align &Right"), QStringLiteral("format-justify-right"),
        QKeySequence(Qt::CTRL | Qt::Key_BraceRight), [](TextEditor *e) { e->setAlignment(Qt::AlignRight); }, true);
    fmt(QStringLiteral("alignJustify"), tr("&Justify"), QStringLiteral("format-justify-fill"), QKeySequence(),
        [](TextEditor *e) { e->setAlignment(Qt::AlignJustify); }, true);
    fmt(QStringLiteral("bullets"), tr("&Bulleted List"), QStringLiteral("format-list-unordered"), QKeySequence(),
        [](TextEditor *e) { e->toggleList(false); });
    fmt(QStringLiteral("numbers"), tr("&Numbered List"), QStringLiteral("format-list-ordered"), QKeySequence(),
        [](TextEditor *e) { e->toggleList(true); });
    fmt(QStringLiteral("indent"), tr("&Indent List Item"), QStringLiteral("format-indent-more"), QKeySequence(),
        [](TextEditor *e) { e->indentList(1); });
    fmt(QStringLiteral("outdent"), tr("&Outdent List Item"), QStringLiteral("format-indent-less"), QKeySequence(),
        [](TextEditor *e) { e->indentList(-1); });
    fmt(QStringLiteral("clearFormat"), tr("&Clear Formatting"), QStringLiteral("edit-clear"), QKeySequence(),
        [](TextEditor *e) { e->clearFormatting(); });
    fmt(QStringLiteral("textColor"), tr("Text &Color…"), QStringLiteral("format-text-color"), QKeySequence(), [this](TextEditor *e) {
        const QColor now = e->charSpecAtCursor().color;
        const QColor c = QColorDialog::getColor(now.isValid() ? now : e->palette().color(QPalette::Text), this);
        if (c.isValid())
            e->setTextColor(c);
    });
    fmt(QStringLiteral("textColorAuto"), tr("&Automatic Text Color"), QString(), QKeySequence(),
        [](TextEditor *e) { e->setTextColor(QColor()); });
    fmt(QStringLiteral("showFonts"), tr("Show &Fonts…"), QStringLiteral("font"), QKeySequence(Qt::CTRL | Qt::Key_T),
        [this](TextEditor *e) { showFontDialog(e); });
    fmt(QStringLiteral("fontBigger"), tr("Bi&gger"), QStringLiteral("format-font-size-more"), QKeySequence(QStringLiteral("Ctrl+>")),
        [](TextEditor *e) { e->changeFontSize(1); });
    fmt(QStringLiteral("fontSmaller"), tr("S&maller"), QStringLiteral("format-font-size-less"), QKeySequence(QStringLiteral("Ctrl+<")),
        [](TextEditor *e) { e->changeFontSize(-1); });
    fmt(QStringLiteral("paragraph"), tr("&Paragraph…"), QString(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P),
        [this](TextEditor *e) { showParagraphDialog(e); });
    fmt(QStringLiteral("indentMore"), tr("&Increase Indent"), QStringLiteral("format-indent-more"), QKeySequence(),
        [](TextEditor *e) { e->changeLeftIndent(units::toPx(units::metric() ? 1.0 : 0.5)); });
    fmt(QStringLiteral("indentLess"), tr("&Decrease Indent"), QStringLiteral("format-indent-less"), QKeySequence(),
        [](TextEditor *e) { e->changeLeftIndent(-units::toPx(units::metric() ? 1.0 : 0.5)); });
    fmt(QStringLiteral("resetParagraph"), tr("&Reset Paragraph Formatting"), QString(), QKeySequence(),
        [](TextEditor *e) { e->resetParagraphFormatting(); });
    connect(act(QStringLiteral("styleNew"), tr("&New Style From Selection…")), &QAction::triggered, this,
            withProject([this] { newStyleFromSelection(); }));
    connect(act(QStringLiteral("styleRedefine"), tr("&Redefine Style From Selection…")), &QAction::triggered, this,
            withProject([this] { redefineStyleFromSelection(); }));
    connect(act(QStringLiteral("stylesPanel"), tr("Styles &Panel")), &QAction::triggered, this, [this] { showStylesPanel(); });
    connect(act(QStringLiteral("styleImport"), tr("&Import Styles…")), &QAction::triggered, this,
            withProject([this] { importStyles(); }));
    connect(act(QStringLiteral("styleSaveDefault"), tr("&Use These Styles in New Projects")), &QAction::triggered, this,
            withProject([this] { saveStylesAsDefault(); }));
    fmt(QStringLiteral("copyFormat"), tr("Cop&y Formatting"), QString(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C),
        [](TextEditor *e) { e->copyFormatting(); });
    fmt(QStringLiteral("pasteFormat"), tr("Paste Formattin&g"), QString(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_V),
        [](TextEditor *e) { e->pasteFormatting(); });
    for (double sp : {1.0, 1.15, 1.2, 1.5, 2.0})
        fmt(QStringLiteral("line%1").arg(qRound(sp * 100)), QLocale().toString(sp, 'f', sp == 1.15 ? 2 : 1), QString(),
            QKeySequence(), [sp](TextEditor *e) { e->setLineSpacing(sp); });
    {
        QAction *r = act(QStringLiteral("showRuler"), tr("Show &Ruler"), QString(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_R));
        r->setCheckable(true);
        r->setChecked(AppSettings::instance().showRuler);
        connect(r, &QAction::toggled, this, [](bool on) {
            if (AppSettings::instance().showRuler == on)
                return;
            AppSettings::instance().showRuler = on;
            AppSettings::instance().notifyChanged();
        });
    }
    createScriptActions();
    createMediaActions();
    connect(act(QStringLiteral("defaultFormatting"), tr("Text to &Default Formatting…")), &QAction::triggered, this,
            withProject([this] { convertToDefaultFormatting(); }));
    m_revisionGroup = new QActionGroup(this);
    for (int lvl = 0; lvl <= 5; ++lvl) {
        QAction *r = act(QStringLiteral("rev%1").arg(lvl), lvl == 0 ? tr("Off") : tr("Revision %1").arg(lvl));
        r->setCheckable(true);
        r->setChecked(lvl == 0);
        if (lvl > 0) {
            QPixmap pm(12, 12);
            pm.fill(text::revisionColor(lvl));
            r->setIcon(QIcon(pm));
        }
        m_revisionGroup->addAction(r);
        connect(r, &QAction::triggered, this, [lvl] { TextEditor::setRevisionLevel(lvl); });
    }

    // ---- insert
    fmt(QStringLiteral("comment"), tr("&Comment"), QStringLiteral("edit-comment"), QKeySequence(Qt::SHIFT | Qt::Key_F2),
        [this](TextEditor *) {
            m_inspector->show();
            a[QStringLiteral("showInspector")]->setChecked(true);
            m_inspector->addLinkedNote(false);
        });
    fmt(QStringLiteral("footnote"), tr("&Footnote"), QStringLiteral("insert-text-frame"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F2), [this](TextEditor *) {
            m_inspector->show();
            a[QStringLiteral("showInspector")]->setChecked(true);
            m_inspector->addLinkedNote(true);
        });
    fmt(QStringLiteral("annotation"), tr("Inline &Annotation"), QStringLiteral("view-pim-notes"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A), [](TextEditor *e) { e->addInlineNote(NoteKind::InlineAnnotation); });
    fmt(QStringLiteral("inlineFootnote"), tr("&Inline Footnote"), QStringLiteral("format-text-superscript"),
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F), [](TextEditor *e) { e->addInlineNote(NoteKind::InlineFootnote); });
    fmt(QStringLiteral("image"), tr("&Image From File…"), QStringLiteral("insert-image"), QKeySequence(), [this](TextEditor *e) {
        const QString f = QFileDialog::getOpenFileName(this, tr("Insert Image"), QDir::homePath(),
                                                       tr("Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp)"));
        if (!f.isEmpty())
            e->insertImageFile(f);
    });
    fmt(QStringLiteral("insertDate"), tr("Current &Date"), QStringLiteral("view-calendar"), QKeySequence(), [](TextEditor *e) {
        // File > Options > Editing > "Insert the date using a custom format" (0.21)
        const QString f = AppSettings::instance().customDateFormat;
        e->insertPlainText(f.isEmpty() ? QDate::currentDate().toString(Qt::ISODate) : QDateTime::currentDateTime().toString(f));
    });
    // Format > Bibliography/Citations (0.21): starts the program chosen in File > Options > General > Citations
    connect(act(QStringLiteral("citations"), tr("&Bibliography/Citations…")), &QAction::triggered, this, [this] {
        QStringList args = QProcess::splitCommand(AppSettings::instance().citationsApp.trimmed());
        if (args.isEmpty())
            return;
        const QString program = args.takeFirst();
        if (!QProcess::startDetached(program, args))
            QMessageBox::warning(this, tr("Bibliography/Citations"),
                                 tr("\"%1\" could not be started. Check the program under File > Options > General > Citations.").arg(program));
    });
    fmt(QStringLiteral("insertTime"), tr("Current &Time"), QStringLiteral("chronometer"), QKeySequence(),
        [](TextEditor *e) { e->insertPlainText(QTime::currentTime().toString(QStringLiteral("HH:mm"))); });

    // ---- navigate
    connect(act(QStringLiteral("back"), tr("&Back"), QStringLiteral("go-previous"), QKeySequence(Qt::CTRL | Qt::Key_BracketLeft)),
            &QAction::triggered, this, [this] { m_active->goBack(); });
    connect(act(QStringLiteral("forward"), tr("&Forward"), QStringLiteral("go-next"), QKeySequence(Qt::CTRL | Qt::Key_BracketRight)),
            &QAction::triggered, this, [this] { m_active->goForward(); });
    auto sibling = [this](bool next) {
        if (!m_project)
            return;
        BinderItem *cur = m_active->currentItem();
        if (!cur)
            return;
        QList<BinderItem *> order;
        for (BinderItem *it : m_project->allItems())
            if (!it->isRootFolder())
                order << it;
        int i = int(order.indexOf(cur));
        i += next ? 1 : -1;
        if (i < 0 || i >= order.size())
            return;
        loadInPane(m_active, {order[i]});
        m_updatingUi = true;
        m_binder->select({order[i]}, false);
        m_updatingUi = false;
    };
    connect(act(QStringLiteral("prevDoc"), tr("&Previous Document"), QStringLiteral("go-up"), QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_Up)),
            &QAction::triggered, this, [sibling] { sibling(false); });
    connect(act(QStringLiteral("nextDoc"), tr("&Next Document"), QStringLiteral("go-down"), QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_Down)),
            &QAction::triggered, this, [sibling] { sibling(true); });
    connect(act(QStringLiteral("enclosingGroup"), tr("Enclosing &Group"), QStringLiteral("go-parent-folder")),
            &QAction::triggered, this, [this] {
                m_active->goToEnclosingGroup();
                refreshInspector();
                updateActions();
            });
    a[QStringLiteral("enclosingGroup")]->setToolTip(tr("Show the folder or group the document in the editor belongs to"));
    connect(act(QStringLiteral("focusBinder"), tr("Focus Bi&nder"), QString(), QKeySequence(Qt::ALT | Qt::Key_1)),
            &QAction::triggered, this, [this] {
                m_binder->show();
                m_binder->tree()->setFocus();
            });
    connect(act(QStringLiteral("focusEditor"), tr("Focus &Editor"), QString(), QKeySequence(Qt::ALT | Qt::Key_2)),
            &QAction::triggered, this, [this] { m_active->focusEditor(); });
    connect(act(QStringLiteral("switchEditor"), tr("&Switch Editor"), QString(), QKeySequence(Qt::Key_F6)), &QAction::triggered,
            this, [this] {
                if (EditorPane *o = otherPane(m_active); o && o->isVisible()) {
                    setActivePane(o);
                    o->focusEditor();
                }
            });

    // ---- help
    connect(act(QStringLiteral("manual"), tr("&User Manual"), QStringLiteral("help-contents"), QKeySequence::HelpContents),
            &QAction::triggered, this, [this] { showManual(this); });
    connect(act(QStringLiteral("quickStart"), tr("&Quick Start Guide")), &QAction::triggered, this,
            [this] { showQuickStartGuide(this); });
    a[QStringLiteral("quickStart")]->setToolTip(tr("Where the important settings are, and what %1 can do")
                                                    .arg(QLatin1String(QB_APP_NAME)));
    connect(act(QStringLiteral("welcome"), tr("&Welcome Project…"), QStringLiteral("help-hint")), &QAction::triggered, this,
            [this] { openWelcomeProject(); });
    a[QStringLiteral("welcome")]->setToolTip(tr("A short project that shows what %1 has beyond the usual: icon bar, zoom per "
                                                "part, mind maps, objects and more")
                                                 .arg(QLatin1String(QB_APP_NAME)));
    connect(act(QStringLiteral("licenses"), tr("Third-Party &Licenses…")), &QAction::triggered, this,
            [this] { showThirdPartyLicenses(this); });
    connect(act(QStringLiteral("placeholders"), tr("List of All Place&holders…")), &QAction::triggered, this,
            [this] { showPlaceholders(); });
    connect(act(QStringLiteral("pluginFolder"), tr("Open &Plugin Folder")), &QAction::triggered, this, [] {
        const QString d = dataDir() + QLatin1String("/plugins");
        QDir().mkpath(d);
        QDesktopServices::openUrl(QUrl::fromLocalFile(d));
    });
    connect(act(QStringLiteral("integrate"), tr("Add to &Application Menu")), &QAction::triggered, this,
            [this] { integrateDesktop(false); });
    connect(act(QStringLiteral("installPackage"), tr("Install &Update or Add-on…"), QStringLiteral("system-software-update")),
            &QAction::triggered, this, [this] {
                const QString f = QFileDialog::getOpenFileName(
                    this, tr("Install Update or Add-on"), QDir::homePath(),
                    tr("%1 packages (*.qbpkg)").arg(QLatin1String(QB_APP_NAME)));
                if (!f.isEmpty())
                    installPackage(f);
            });
    connect(act(QStringLiteral("addonManager"), tr("Add-on &Manager…"), QStringLiteral("preferences-plugin")),
            &QAction::triggered, this, &MainWindow::showAddonManager);
    connect(act(QStringLiteral("about"), tr("&About %1").arg(QLatin1String(QB_APP_NAME)), QStringLiteral("help-about")),
            &QAction::triggered, this, [this] { showAbout(); });
    connect(act(QStringLiteral("systemInfo"), tr("&System Information…"), QStringLiteral("dialog-information")),
            &QAction::triggered, this, [this] {
                SystemInfoDialog dlg(m_project.get(), this);
                dlg.exec();
            });
    a[QStringLiteral("systemInfo")]->setToolTip(tr("Program, versions and computer at a glance, to copy into a problem report"));
    connect(act(QStringLiteral("crashReports"), tr("&Crash Reports…")), &QAction::triggered, this, [this] {
        CrashReportsDialog dlg(this);
        dlg.exec();
    });
    connect(act(QStringLiteral("keyboardShortcuts"), tr("&Keyboard Shortcuts…"), QStringLiteral("configure-shortcuts")),
            &QAction::triggered, this, [this] {
                OptionsDialog dlg(this, OptionsDialog::Keyboard);
                dlg.exec();
            });

    // ---- edit: select
    fmt(QStringLiteral("selectAnnotation"), tr("Select &Annotation or Note"), QString(), QKeySequence(), [](TextEditor *e) {
        const QString id = e->noteIdAt(e->textCursor().position());
        if (!id.isEmpty())
            e->selectNote(id);
    });

    // ---- view: bars, corkboard and outliner options
    {
        QAction *tb = act(QStringLiteral("showToolbar"), tr("Show &Toolbar"));
        tb->setCheckable(true);
        tb->setChecked(true);
        connect(act(QStringLiteral("customizeToolbars"), tr("&Customize Toolbars…"), QStringLiteral("configure-toolbars")),
                &QAction::triggered, this, [this] { customizeToolbars(0); });
        QAction *fs = act(QStringLiteral("fullScreen"), tr("&Full Screen"), QStringLiteral("view-fullscreen"),
                          QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F11));
        fs->setCheckable(true);
        connect(fs, &QAction::triggered, this, [this](bool on) {
            if (on) {
                m_maximizedBeforeFullScreen = isMaximized();
                showFullScreen();
            } else if (m_maximizedBeforeFullScreen) {
                showMaximized();
            } else {
                showNormal();
            }
        });
        QAction *free = act(QStringLiteral("corkFreeform"), tr("&Freeform"), QStringLiteral("games-config-board"));
        free->setCheckable(true);
        connect(free, &QAction::triggered, this, [this](bool on) {
            m_active->setCorkMode(on ? int(Corkboard::Mode::Freeform) : int(Corkboard::Mode::Grid));
        });
        QAction *threads = act(QStringLiteral("corkLabels"), tr("Arrange by &Label"), QStringLiteral("view-time-schedule-baselined"));
        threads->setCheckable(true);
        connect(threads, &QAction::triggered, this, [this](bool on) {
            m_active->setCorkMode(on ? int(Corkboard::Mode::LabelThreads) : int(Corkboard::Mode::Grid));
        });
        connect(act(QStringLiteral("corkCommit"), tr("&Commit Freeform Order")), &QAction::triggered, this,
                [this] { m_active->corkboard()->commitFreeformOrder(); });
        QAction *lb = act(QStringLiteral("labelBinder"), tr("Label Color in &Binder"));
        lb->setCheckable(true);
        connect(lb, &QAction::triggered, this, [](bool on) {
            AppSettings::instance().labelColorInBinder = on;
            AppSettings::instance().notifyChanged();
        });
        QAction *li = act(QStringLiteral("labelIcons"), tr("Label Color in &Icons"));
        li->setCheckable(true);
        connect(li, &QAction::triggered, this, [](bool on) {
            AppSettings::instance().labelColorInIcons = on;
            AppSettings::instance().notifyChanged();
        });
        QAction *syn = act(QStringLiteral("outlinerSynopsis"), tr("Show &Synopses"));
        syn->setCheckable(true);
        connect(syn, &QAction::triggered, this, [this](bool on) {
            m_active->setOutlinerSynopsis(on);
        });
        QAction *aff = act(QStringLiteral("selectionAffectsOther"), tr("Selection Affects &Other Editor"),
                           QStringLiteral("document-export"));
        aff->setCheckable(true);
        connect(aff, &QAction::triggered, this, [this](bool on) { m_active->setSelectionAffectsOther(on); });
        QAction *affHolder = act(QStringLiteral("selectionAffectsCopyholder"), tr("Selection Affects Copy&holder"),
                                 QStringLiteral("document-import"));
        affHolder->setCheckable(true);
        connect(affHolder, &QAction::triggered, this, [this](bool on) {
            m_active->setSelectionAffects(on ? EditorPane::AffectsCopyholder : EditorPane::AffectsNothing);
        });
    }

    // ---- navigate: open, binder selection, inspector tabs
    connect(act(QStringLiteral("openInEditor"), tr("Open in &Editor"), QStringLiteral("document-open")), &QAction::triggered, this,
            [this] {
                const auto items = actionItems();
                if (!items.isEmpty())
                    loadInPane(m_active, items);
            });
    m_affectsGroup = new QActionGroup(this);
    for (const auto &m : {qMakePair(QStringLiteral("current"), tr("&Current Editor")),
                          qMakePair(QStringLiteral("first"), tr("&Left or Top Editor")),
                          qMakePair(QStringLiteral("second"), tr("&Right or Bottom Editor")),
                          qMakePair(QStringLiteral("other"), tr("&Other Editor"))}) {
        const QString mode = m.first;
        QAction *ac = act(QStringLiteral("binderAffects_") + mode, m.second);
        ac->setCheckable(true);
        ac->setChecked(mode == QLatin1String("current"));
        ac->setData(mode);
        m_affectsGroup->addAction(ac);
        connect(ac, &QAction::triggered, this, [this, mode] { setBinderAffects(mode); });
    }
    {
        const QList<QPair<Inspector::Tab, QString>> tabs = {{Inspector::Notes, tr("&Notes")},
                                                           {Inspector::Bookmarks, tr("&Bookmarks")},
                                                           {Inspector::Metadata, tr("&Metadata")},
                                                           {Inspector::Snapshots, tr("&Snapshots")},
                                                           {Inspector::Comments, tr("&Comments and Footnotes")},
                                                           {Inspector::MindMap, tr("Mind &Map")},
                                                           {Inspector::Objects, tr("&Objects")}};
        for (const auto &t : tabs) {
            const Inspector::Tab tab = t.first;
            connect(act(QStringLiteral("inspect%1").arg(int(tab)), t.second), &QAction::triggered, this, [this, tab] {
                a[QStringLiteral("showInspector")]->setChecked(true);
                m_inspector->showTab(tab);
            });
        }
    }

    // ---- window: layouts, quick reference windows
    connect(act(QStringLiteral("layoutSave"), tr("&Save Current Layout…"), QStringLiteral("document-save-as")),
            &QAction::triggered, this, [this] { saveLayoutAs(); });
    connect(act(QStringLiteral("layoutManage"), tr("&Manage Layouts…")), &QAction::triggered, this, [this] { manageLayouts(); });
    {
        QAction *fl = act(QStringLiteral("floatQuickRef"), tr("&Float Quick Reference Panels"), QStringLiteral("window-pin"));
        fl->setCheckable(true);
        fl->setChecked(AppSettings::instance().floatQuickRef);
        connect(fl, &QAction::triggered, this, [this](bool on) {
            AppSettings::instance().floatQuickRef = on;
            AppSettings::instance().save();
            applyQuickRefFloating();
        });
        connect(act(QStringLiteral("closeQuickRefs"), tr("&Close All Quick Reference Panels")), &QAction::triggered, this, [] {
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *qr = qobject_cast<QuickRefWindow *>(w))
                    qr->close();
        });
    }

    // ---- format bar: the project's default text formatting
    connect(act(QStringLiteral("defaultTextFormatting"), tr("Default Text &Formatting…"), QStringLiteral("configure")),
            &QAction::triggered, this, withProject([this] {
                ProjectSettingsDialog dlg(m_project.get(), ProjectSettingsDialog::Formatting, this);
                dlg.exec();
            }));
    a[QStringLiteral("defaultTextFormatting")]->setToolTip(
        tr("Font, size, line spacing and indents of the text of the whole project"));
}

// ------------------------------------------------------------------ state updates

QString MainWindow::aboutText()
{
    QString version = QLatin1String(QB_APP_VERSION_SHOWN);   // "0.40.0 alpha"
    if (launcher::isOverlay())
        version += tr(" (program update; AppImage %1)").arg(launcher::builtInVersion());
    return tr("<h2>%1 %2</h2><p>A writing studio for long texts: binder, corkboard, outliner, "
              "combined text, snapshots, mind maps, objects and compile.</p>"
              "<p>Built with Qt %3.</p>"
              "<p><b>Credits</b><br>Main Developer: Claude.AI w. Opus 5.5 Extra</p>"
              "<p><b>License</b><br>%1 is free software under the Zero-Clause BSD license (0BSD): anyone may use, "
              "copy, change and pass it on, with or without fee, without conditions and without warranty.</p>"
              "<p>The program uses parts of other projects (Qt, Hunspell, ICU, the Breeze icons and more), each under its "
              "own license. <b>Third-Party Licenses…</b> below (also in the Help menu) lists them with their full "
              "texts.</p>"
              "<p>The add-ons for audio and video use code of FFmpeg, licensed under the GNU LGPL 2.1 or later; its "
              "source code, as used, is in the release \"Third-party source code\" of %4.</p>"
              "<p>Scrivener and Scapple are products of Literature &amp; Latte. %1 is an independent program, not made "
              "by or connected with Literature &amp; Latte; it reads and writes their file formats so that your "
              "projects can move between the programs.</p>")
        .arg(QLatin1String(QB_APP_NAME), version, QLatin1String(qVersion()), QLatin1String(QB_APP_HOMEPAGE));
}

TextEditor *MainWindow::editor() const
{
    // focus in a notes editor or similar wins
    if (auto *fe = qobject_cast<TextEditor *>(QApplication::focusWidget()))
        return fe;
    // the text the keyboard was in last, while it is on the format bar or in a menu (0.21.1:
    // a click in the size field moved the keyboard there, and the main editor got the size
    // meant for the notes or the bookmarked text in the inspector)
    if (auto *last = qobject_cast<TextEditor *>(m_lastTextField.data()); last && last->isVisible() && last->isEnabled())
        return last;
    return m_active ? m_active->currentEditor() : nullptr;
}

ObjectTextEdit *MainWindow::objectTextTarget() const
{
    QWidget *f = QApplication::focusWidget();
    if (auto *o = qobject_cast<ObjectTextEdit *>(f))
        return o->isReadOnly() ? nullptr : o;
    if (qobject_cast<TextEditor *>(f))
        return nullptr;
    if (auto *o = qobject_cast<ObjectTextEdit *>(m_lastTextField.data()); o && o->isVisible() && o->isEnabled() && !o->isReadOnly())
        return o;
    return nullptr;
}

bool MainWindow::objectFormat(const QString &key, ObjectTextEdit *o)
{
    // the commands that make sense for the text of an object (0.21.1)
    if (key == QLatin1String("bold"))
        o->toggle(ObjectTextEdit::Toggle::Bold);
    else if (key == QLatin1String("italic"))
        o->toggle(ObjectTextEdit::Toggle::Italic);
    else if (key == QLatin1String("underline"))
        o->toggle(ObjectTextEdit::Toggle::Underline);
    else if (key == QLatin1String("strike"))
        o->toggle(ObjectTextEdit::Toggle::Strike);
    else if (key == QLatin1String("highlight"))
        o->applyHighlight(o->highlightAtCursor().isValid() ? QColor() : (m_highlightBtn && m_highlightBtn->color().isValid()
                                                                             ? m_highlightBtn->color()
                                                                             : QColor(255, 236, 61)));
    else if (key == QLatin1String("textColor")) {
        const QColor now = o->textColorAtCursor();
        const QColor c = QColorDialog::getColor(now.isValid() ? now : o->palette().color(QPalette::Text), this);
        if (c.isValid())
            o->applyTextColor(c);
    } else if (key == QLatin1String("textColorAuto"))
        o->applyTextColor(QColor());
    else if (key == QLatin1String("clearFormat"))
        o->clearFormatting();
    else
        return false;
    o->setFocus();
    return true;
}

void MainWindow::updateActions()
{
    const bool has = m_project != nullptr;
    if (m_iconBar)
        for (QAction *ac : m_iconBar->actions())
            if (ac->objectName().startsWith(QLatin1String("iconBar_")))
                ac->setEnabled(has);
    for (const char *k : {"save", "saveTemplate", "backupNow", "backupTo", "importFiles", "importWebPage", "importSplit", "exportFiles",
                          "compile", "closeProject", "newText", "newFolder", "projectSettings", "settingsExport", "settingsImport", "targets", "keywords",
                          "statistics", "history", "emptyTrash", "showProjectFolder", "projectSearch", "projectReplace", "findFormat",
                          "findFormatNext", "findFormatPrev", "trash", "duplicate",
                          "rename", "openOther", "quickRef", "split", "splitTitle", "merge", "toFolder", "toText",
                          "includeCompile", "snapshot", "snapshotTitle", "bookmark", "reveal", "lock", "composition",
                          "viewText", "viewCork", "viewOutline", "moveUp", "moveDown", "moveLeft", "moveRight", "docLink",
                          "back", "forward", "prevDoc", "nextDoc", "printSettings", "openInEditor", "closeQuickRefs",
                          "defaultTextFormatting", "pageView", "pageSetup", "openCopyholder", "enclosingGroup", "inspect0", "inspect1", "inspect2", "inspect3", "inspect4", "inspect5", "inspect6",
                          "objectLink", "objectsWindow", "objectTemplates",
                          "resetIcon", "iconFromText", "manageIcons", "compareFile", "compareByChapter", "compareStepByStep",
                          "formatQuillbinder", "formatScrivener", "saveCopyScrivener", "saveCopyQuillbinder"})
        a[QLatin1String(k)]->setEnabled(has);
    // without a project there is no text to search, link, zoom or split, and no binder to go to
    // (their keys must not reach them either); the menus with nothing but such commands are off as a whole
    for (const char *k : {"find", "replace", "findNext", "findPrev", "useSelectionForFind", "selectAnnotation", "hyperlink",
                          "noSplit", "splitH", "splitV", "zoomIn", "zoomOut", "zoomReset", "focusBinder", "focusEditor", "switchEditor"})
        a[QLatin1String(k)]->setEnabled(has);
    for (QAction *top : menuBar()->actions())
        if (top->menu() && top->menu()->property("qbNeedsProject").toBool())
            top->setEnabled(has);
    a[QStringLiteral("citations")]->setEnabled(has && !AppSettings::instance().citationsApp.trimmed().isEmpty());
    {
        const bool scriv = has && !m_project->scrivenerPath.isEmpty();
        a[QStringLiteral("formatQuillbinder")]->setChecked(has && !scriv);
        a[QStringLiteral("formatScrivener")]->setChecked(scriv);
    }
    m_quickSearch->setEnabled(has);
    updateToolActions();
    m_labelMenu->setEnabled(has);
    m_statusMenu->setEnabled(has);
    m_iconMenu->setEnabled(has);
    m_templateMenu->setEnabled(has);
    if (!has) {
        for (QAction *ac : m_rtfOnly + m_markdownOnly)
            ac->setVisible(false);
        return;
    }
    const auto items = actionItems();
    bool anyText = false, anyFolder = false, allInclude = !items.isEmpty();
    for (BinderItem *it : items) {
        anyText = anyText || it->type == ItemType::Text;
        anyFolder = anyFolder || it->type == ItemType::Folder;
        allInclude = allInclude && it->includeInCompile;
    }
    a[QStringLiteral("toFolder")]->setEnabled(anyText);
    a[QStringLiteral("toText")]->setEnabled(anyFolder);
    a[QStringLiteral("merge")]->setEnabled(items.size() > 1);
    a[QStringLiteral("includeCompile")]->setChecked(allInclude);
    a[QStringLiteral("back")]->setEnabled(m_active->canGoBack());
    a[QStringLiteral("forward")]->setEnabled(m_active->canGoForward());
    a[QStringLiteral("enclosingGroup")]->setEnabled(m_active->canGoToEnclosingGroup());
    a[QStringLiteral("lock")]->setChecked(m_active->isLocked());
    a[QStringLiteral("closeCopyholder")]->setEnabled(m_active->hasCopyholder());
    a[QStringLiteral("emptyTrash")]->setEnabled(!m_project->trash()->children.isEmpty());
    const bool rtf = m_project->storage == StorageFormat::Rtf;
    for (QAction *ac : m_rtfOnly)
        ac->setVisible(rtf);
    for (QAction *ac : m_markdownOnly)
        ac->setVisible(!rtf);
    updateViewButtons();
}

void MainWindow::updateViewButtons()
{
    m_updatingUi = true;
    const ViewMode m = m_active ? m_active->mode() : ViewMode::Empty;
    const bool group = m_active && m_active->isGroup();
    a[QStringLiteral("viewText")]->setIcon(icons::action(group ? QStringLiteral("view-pim-journal") : QStringLiteral("text-x-generic")));
    a[QStringLiteral("viewText")]->setText(group ? tr("&Combined Text") : tr("&Document"));
    a[QStringLiteral("viewText")]->setIconText(group ? tr("Combined Text") : tr("Document"));
    a[QStringLiteral("viewText")]->setChecked(m == ViewMode::Combined || (!group && m == ViewMode::Single));
    a[QStringLiteral("viewCork")]->setChecked(m == ViewMode::Corkboard);
    a[QStringLiteral("viewOutline")]->setChecked(m == ViewMode::Outliner);
    a[QStringLiteral("pageView")]->setChecked(m_project && m_project->docs()->pageViewSetting());
    a[QStringLiteral("typewriter")]->setChecked(AppSettings::instance().typewriterScrolling);
    a[QStringLiteral("scrivTitles")]->setChecked(AppSettings::instance().showTitlesInCombined);
    a[QStringLiteral("fixedWidth")]->setChecked(AppSettings::instance().fixedWidth);
    a[QStringLiteral("stamps")]->setChecked(AppSettings::instance().showStatusStamps);
    a[QStringLiteral("tintCards")]->setChecked(AppSettings::instance().tintCardsWithLabel);
    a[QStringLiteral("labelBinder")]->setChecked(AppSettings::instance().labelColorInBinder);
    a[QStringLiteral("labelIcons")]->setChecked(AppSettings::instance().labelColorInIcons);
    const bool cork = m == ViewMode::Corkboard, outline = m == ViewMode::Outliner;
    const int corkMode = m_active ? m_active->corkMode() : 0;
    a[QStringLiteral("corkFreeform")]->setEnabled(cork);
    a[QStringLiteral("corkFreeform")]->setChecked(cork && corkMode == int(Corkboard::Mode::Freeform));
    a[QStringLiteral("corkLabels")]->setEnabled(cork);
    a[QStringLiteral("corkLabels")]->setChecked(cork && corkMode == int(Corkboard::Mode::LabelThreads));
    a[QStringLiteral("corkCommit")]->setEnabled(cork && corkMode == int(Corkboard::Mode::Freeform));
    a[QStringLiteral("outlinerSynopsis")]->setEnabled(outline);
    a[QStringLiteral("outlinerSynopsis")]->setChecked(m_active && m_active->outliner()->showSynopsis());
    a[QStringLiteral("selectionAffectsOther")]->setEnabled(cork || outline);
    a[QStringLiteral("selectionAffectsOther")]->setChecked(m_active && m_active->selectionAffectsOther());
    a[QStringLiteral("selectionAffectsCopyholder")]->setEnabled((cork || outline) && m_active->hasCopyholder());
    a[QStringLiteral("selectionAffectsCopyholder")]->setChecked(m_active
                                                                && m_active->selectionAffects() == EditorPane::AffectsCopyholder);
    a[QStringLiteral("fullScreen")]->setChecked(isFullScreen());
    // printing: what the editor shows must be something that can go on paper
    const bool printable = canPrint();
    a[QStringLiteral("print")]->setEnabled(printable);
    a[QStringLiteral("printPreview")]->setEnabled(printable);
    m_updatingUi = false;
}

void MainWindow::updateFormatBar()
{
    checkFontOffer();
    updateScriptActions();
    if (ObjectTextEdit *o = objectTextTarget()) {
        // the text of an object (0.21.1): bold, italic, underline, strikethrough, colors
        static const QSet<QString> objectKeys = {QStringLiteral("bold"), QStringLiteral("italic"), QStringLiteral("underline"),
                                                 QStringLiteral("strike"), QStringLiteral("highlight"), QStringLiteral("textColor"),
                                                 QStringLiteral("textColorAuto"), QStringLiteral("clearFormat")};
        for (const char *k : {"bold", "italic", "underline", "strike", "superscript", "subscript", "highlight", "alignLeft",
                              "alignCenter", "alignRight", "alignJustify", "bullets", "numbers", "indent", "outdent",
                              "clearFormat", "comment", "footnote", "annotation", "inlineFootnote", "image", "textColor",
                              "textColorAuto", "showFonts", "fontBigger", "fontSmaller", "paragraph", "indentMore", "indentLess",
                              "resetParagraph", "copyFormat", "line100", "line115", "line120", "line150", "line200",
                              "insertDate", "insertTime", "pastePlain", "pasteFormat", "styleNew", "styleRedefine"})
            a[QLatin1String(k)]->setEnabled(objectKeys.contains(QLatin1String(k)));
        for (QAction *ac : m_styleActions)
            ac->setEnabled(false);
        m_styleCombo->setEnabled(false);
        m_fontCombo->setEnabled(false);
        m_sizeSpin->setEnabled(false);
        m_lineCombo->setEnabled(false);
        m_updatingUi = true;
        a[QStringLiteral("bold")]->setChecked(o->isOn(ObjectTextEdit::Toggle::Bold));
        a[QStringLiteral("italic")]->setChecked(o->isOn(ObjectTextEdit::Toggle::Italic));
        a[QStringLiteral("underline")]->setChecked(o->isOn(ObjectTextEdit::Toggle::Underline));
        a[QStringLiteral("strike")]->setChecked(o->isOn(ObjectTextEdit::Toggle::Strike));
        m_updatingUi = false;
        return;
    }
    TextEditor *e = editor();
    const bool enabled = e != nullptr;
    for (const char *k : {"bold", "italic", "underline", "strike", "superscript", "subscript", "highlight", "alignLeft",
                          "alignCenter", "alignRight", "alignJustify", "bullets", "numbers", "indent", "outdent",
                          "clearFormat", "comment", "footnote", "annotation", "inlineFootnote", "image", "textColor",
                          "textColorAuto", "showFonts", "fontBigger", "fontSmaller", "paragraph", "indentMore", "indentLess",
                          "resetParagraph", "copyFormat", "line100", "line115", "line120", "line150", "line200",
                          "insertDate", "insertTime", "pastePlain"})
        a[QLatin1String(k)]->setEnabled(enabled);
    for (QAction *ac : m_styleActions)
        ac->setEnabled(enabled);
    a[QStringLiteral("styleNew")]->setEnabled(enabled && m_project);
    a[QStringLiteral("styleRedefine")]->setEnabled(enabled && m_project);
    m_styleCombo->setEnabled(enabled);
    m_fontCombo->setEnabled(enabled);
    m_sizeSpin->setEnabled(enabled);
    m_lineCombo->setEnabled(enabled);
    a[QStringLiteral("pasteFormat")]->setEnabled(enabled && TextEditor::hasCopiedFormatting());
    if (!e)
        return;
    m_updatingUi = true;
    const QString style = e->paragraphStyleAtCursor();
    const QString charStyle = e->characterStyleAtCursor();
    // The bar shows what is seen in the editor (0.40.3): bold from a heading's style is bold,
    // for all of the selection; a font, size, alignment or line spacing that differs within
    // the selection leaves its field empty. (0.40.1 to 0.40.2 showed only what was set by
    // hand; before 0.40.1 the character in front of the cursor decided.)
    const BarState st = e->barState();
    a[QStringLiteral("bold")]->setChecked(st.toggles.bold);
    a[QStringLiteral("italic")]->setChecked(st.toggles.italic);
    a[QStringLiteral("underline")]->setChecked(st.toggles.underline);
    a[QStringLiteral("strike")]->setChecked(st.toggles.strike);
    a[QStringLiteral("superscript")]->setChecked(st.superscript);
    a[QStringLiteral("subscript")]->setChecked(st.subscript);
    a[QStringLiteral("alignLeft")]->setChecked(st.align == Qt::AlignLeft);
    a[QStringLiteral("alignCenter")]->setChecked(st.align == Qt::AlignHCenter);
    a[QStringLiteral("alignRight")]->setChecked(st.align == Qt::AlignRight);
    a[QStringLiteral("alignJustify")]->setChecked(st.align == Qt::AlignJustify);
    // several paragraph styles in the selection: the list stays empty
    const QString shownStyle = !charStyle.isEmpty() ? charStyle : e->paragraphStylesMixed() ? QStringLiteral("\x01") : style;
    m_styleCombo->setCurrentIndex(m_styleCombo->findData(shownStyle));
    if (m_stylesPanel && m_stylesPanel->isVisible())
        m_stylesPanel->setCurrent(style, charStyle);
    if (m_project) {
        if (st.family.isEmpty()) {
            m_fontCombo->setCurrentIndex(-1);
            m_fontCombo->setEditText(QString());
        } else {
            m_fontCombo->setCurrentFont(QFont(st.family));
        }
        if (!m_sizeSpin->hasFocus()) {
            if (st.size > 0) {
                m_sizeSpin->setValue(st.size);
            } else if (QLineEdit *le = m_sizeSpin->findChild<QLineEdit *>()) {
                le->clear(); // mixed sizes: an empty field, the value stays for the arrows
            }
        }
        int idx = -1;
        if (st.lineSpacing > 0)
            for (int i = 0; i < m_lineCombo->count(); ++i)
                if (qAbs(m_lineCombo->itemData(i).toDouble() - st.lineSpacing) < 0.005)
                    idx = i;
        m_lineCombo->setCurrentIndex(idx >= 0 ? idx : st.lineSpacing < 0 ? -1 : m_lineCombo->count() - 1);
    }
    m_updatingUi = false;
}

// ------------------------------------------------------------------ fonts and paragraphs

void MainWindow::showFontDialog(TextEditor *e)
{
    if (!e || !m_project)
        return;
    QTextCursor c = e->textCursor();
    QTextCharFormat cf = e->currentCharFormat();
    if (c.hasSelection()) {
        QTextCursor p(e->document());
        p.setPosition(c.selectionStart() + 1);
        cf = p.charFormat();
    }
    FontChoice cur;
    cur.spec = direct::charSpec(cf);
    // as the buttons show it: what is seen, for the whole selection (0.40.3; 0.40.1: set by hand)
    const ToggleState own = e->toggleState(false);
    cur.bold = own.bold;
    cur.italic = own.italic;
    cur.underline = own.underline;
    cur.strike = own.strike;
    QString sample = c.selectedText();
    if (sample.trimmed().isEmpty())
        sample = c.block().text();
    FontDialog dlg(cur, m_project->formatting, sample, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const FontChoice ch = dlg.choice();
    QTextCursor edit = e->textCursor();
    edit.beginEditBlock();
    // what was changed in the window is set for the whole selection (before 0.40.1 it was
    // switched over, judged by the character in front of the cursor)
    if (dlg.boldChanged())
        e->setBold(ch.bold);
    if (dlg.italicChanged())
        e->setItalic(ch.italic);
    if (dlg.underlineChanged())
        e->setUnderline(ch.underline);
    if (dlg.strikeChanged())
        e->setStrike(ch.strike);
    if (const int members = dlg.changedMembers())
        e->applyCharSpec(ch.spec, members);
    edit.endEditBlock();
    e->setFocus();
}

void MainWindow::showParagraphDialog(TextEditor *e)
{
    if (!e)
        return;
    ParagraphDialog dlg(e->paraSpecAtCursor(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (dlg.resetRequested())
        e->resetParagraphFormatting();
    else
        e->applyParaSpec(dlg.result());
    e->setFocus();
    for (EditorPane *p : m_panes)
        p->ruler()->update();
}

void MainWindow::convertToDefaultFormatting()
{
    QList<BinderItem *> items;
    for (BinderItem *it : actionItems())
        if (it->hasText())
            items << it;
    if (items.isEmpty()) {
        QMessageBox::information(this, tr("Text to Default Formatting"), tr("Select one or more text documents first."));
        return;
    }
    DefaultFormattingDialog dlg(items.size(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const bool fonts = dlg.removeFonts();
    const int bits = dlg.paragraphBits();
    if (!fonts && !bits)
        return;
    for (BinderItem *it : items) {
        snapshots::take(m_project.get(), it->id, tr("Before default formatting"));
        QTextDocument *doc = m_project->docs()->text(it->id);
        direct::removeDirect(doc, m_project->formatting, fonts, bits);
    }
    refreshInspector();
    updateFormatBar();
    for (EditorPane *p : m_panes)
        p->ruler()->update();
}

void MainWindow::applyFontFromBar(const direct::CharSpec &spec, int members)
{
    TextEditor *e = editor();
    if (!e || !members)
        return;
    const QTextCursor keep = e->textCursor();
    QTextCursor target = keep;
    if (!target.hasSelection()) {
        // without a selection the whole paragraph gets the font (one word would hardly be seen)
        target.movePosition(QTextCursor::StartOfBlock);
        target.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    }
    const QString paraStyle = e->paragraphStyleAtCursor();
    const QString charStyle = e->characterStyleAtCursor();
    if (target.hasSelection()) {
        e->setTextCursor(target);
        e->applyCharSpec(spec, members);
        e->setTextCursor(keep);
    } else {
        e->applyCharSpec(spec, members); // an empty paragraph: what is typed next
    }
    if (!m_project)
        return;
    // offer the same for the style of the text or for the whole project
    m_fontOffer = FontOffer();
    m_fontOffer.editor = e;
    m_fontOffer.doc = e->document();
    m_fontOffer.from = target.selectionStart();
    m_fontOffer.to = target.selectionEnd();
    m_fontOffer.members = members;
    m_fontOffer.spec = spec;
    m_fontOffer.character = !charStyle.isEmpty();
    m_fontOffer.style = m_fontOffer.character ? charStyle : paraStyle;
    QString what;
    if (members == direct::Family)
        what = tr("the font “%1”").arg(spec.family);
    else if (members == direct::Size)
        what = tr("the size %1 pt").arg(QLocale().toString(spec.size, 'g', 4));
    else
        what = tr("this font");
    if (!m_fontOffer.style.isEmpty()) {
        const QString shown = styles::find(m_project->formatting, m_fontOffer.style)
                                  ? styles::find(m_project->formatting, m_fontOffer.style)->name
                                  : m_fontOffer.style;
        m_fontScope->offer(tr("Changed here only. Use %1 for all “%2” text in the whole project?").arg(what, shown),
                           tr("Change the Style “%1”").arg(shown));
    } else {
        m_fontScope->offer(tr("Changed here only. Use %1 for all text of the project?").arg(what),
                           members == direct::Size ? tr("Use for All Text (Project Size)")
                                                   : tr("Use for All Text (Project Font)"));
    }
}

void MainWindow::checkFontOffer()
{
    if (!m_fontScope || m_fontScope->isHidden())
        return;
    TextEditor *e = editor();
    bool stays = e && e == m_fontOffer.editor && e->document() == m_fontOffer.doc && m_project;
    if (stays) {
        // as long as the cursor stays in the paragraphs that got the font
        const QTextCursor c = e->textCursor();
        QTextDocument *doc = e->document();
        const QTextBlock first = doc->findBlock(m_fontOffer.from);
        const QTextBlock last = doc->findBlock(m_fontOffer.to);
        stays = first.isValid() && last.isValid() && c.selectionStart() >= first.position()
                && c.selectionEnd() <= last.position() + last.length();
    }
    if (!stays)
        m_fontScope->hide();
}

void MainWindow::acceptFontOffer()
{
    if (!m_project)
        return;
    const FontOffer o = m_fontOffer;
    m_fontScope->hide();
    QString err;
    int n = 0;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (!o.style.isEmpty()) {
        StyleSheet sheet = styles::sheet(m_project->formatting);
        const int idx = sheet.indexOf(o.style);
        if (idx < 0) {
            QApplication::restoreOverrideCursor();
            return;
        }
        Style &s = sheet.styles[idx];
        if (o.members & direct::Family)
            s.chars.family = o.spec.family;
        if (o.members & direct::Size) {
            s.chars.size = o.spec.size;
            s.sizeAdjust = 0;
        }
        QApplication::restoreOverrideCursor();
        // commitStyles also gives the text that had the old font set by hand the style's new
        // one; it says so itself when a document could not be written
        commitStyles(sheet);
        QApplication::setOverrideCursor(Qt::WaitCursor);
    } else {
        const Formatting oldF = m_project->formatting;
        if (o.members & direct::Family)
            m_project->formatting.fontFamily = o.spec.family;
        if (o.members & direct::Size)
            m_project->formatting.fontSize = o.spec.size;
        m_project->docs()->applyFormatting();
        n = formatchange::followProject(m_project.get(), oldF, true, &err);
        m_project->notifySettingsChanged();
    }
    QApplication::restoreOverrideCursor();
    if (n < 0)
        QMessageBox::warning(this, tr("Format"), tr("Some documents could not be changed:\n%1").arg(err));
    refreshInspector();
    updateFormatBar();
    for (EditorPane *p : m_panes)
        p->ruler()->update();
    if (TextEditor *e = editor())
        e->setFocus();
}

void MainWindow::updateWindowTitle()
{
    if (!m_project)
        setWindowTitle(QStringLiteral(QB_APP_NAME));
    else if (AppSettings::instance().showFullPath) // File > Options > Appearance > General Interface (0.21)
        setWindowTitle(QDir::toNativeSeparators(m_project->folderPath())
                       + (m_project->scrivenerPath.isEmpty() ? QString() : tr(" (Scrivener format)"))
                       + QStringLiteral(" — ") + QStringLiteral(QB_APP_NAME));
    else
        setWindowTitle(m_project->title()
                       + (m_project->scrivenerPath.isEmpty() ? QString() : tr(" (Scrivener format)"))
                       + QStringLiteral(" — ") + QStringLiteral(QB_APP_NAME));
}

void MainWindow::updateTargetsDisplay()
{
    if (!m_project) {
        m_quickSearch->draft = m_quickSearch->session = -1;
        m_quickSearch->update();
        return;
    }
    const Targets &t = m_project->targets;
    const int draft = t.draftChars ? m_project->draftCharCount() : m_project->draftWordCount();
    const int session = t.sessionChars ? m_project->sessionChars() : m_project->sessionWords();
    const int st = m_project->sessionTargetToday();
    m_quickSearch->draft = t.draftTarget > 0 ? double(draft) / t.draftTarget : -1;
    m_quickSearch->session = st > 0 ? double(session) / st : -1;
    QLocale loc;
    m_quickSearch->setToolTip(tr("Draft: %1 words · This session: %2 words%3")
                                  .arg(loc.toString(m_project->draftWordCount()), loc.toString(m_project->sessionWords()),
                                       t.draftTarget > 0 ? tr(" · Target: %1").arg(loc.toString(t.draftTarget)) : QString()));
    m_quickSearch->update();
}

void MainWindow::applySettings()
{
    theme::apply();
    const AppSettings &s = AppSettings::instance();
    SpellChecker::setEnabled(s.spellCheck);
    SpellChecker::setDisplay({s.spellIgnoreNumbers,
                              s.spellUnderline == QLatin1String("dots")   ? 1
                              : s.spellUnderline == QLatin1String("line") ? 2
                                                                          : 0,
                              s.noColorInlineAnnotations});
    if (s.spellCheck && !SpellChecker::instance().setLanguage(s.spellLanguage)) {
        const QStringList langs = SpellChecker::instance().availableLanguages();
        if (!langs.isEmpty())
            SpellChecker::instance().setLanguage(langs.contains(QStringLiteral("en_US")) ? QStringLiteral("en_US") : langs.first());
    }
    SpellChecker::rehighlightAll();
    if (m_project)
        m_project->setAutosaveDelay(qMax(1, s.autosaveSeconds) * 1000);
    sysinfo::refresh(m_project.get());
    a[QStringLiteral("showRuler")]->setChecked(s.showRuler);
    for (EditorPane *p : m_panes) {
        p->updateRuler();
        p->textView()->applySettings();
        p->textView()->setTypewriter(s.typewriterScrolling);
        p->corkboard()->rebuild();
        p->refreshHeader();
    }
    icons::clearCache();
    icons::setLabelTint(s.labelColorInIcons);
    a[QStringLiteral("labelIcons")]->setChecked(s.labelColorInIcons);
    m_binder->tree()->viewport()->update();
    BinderModel::dropOnTextDocuments = s.dropOnTextDocuments;
    rebuildToolBars();
    installToolbarDropTargets();
    updateViewButtons();
    updateWindowTitle();
    a[QStringLiteral("citations")]->setEnabled(m_project && !s.citationsApp.trimmed().isEmpty());
    crash::setFreezeSeconds(qBound(10, s.freezeSeconds, 600));
}

void MainWindow::rebuildRecentMenu()
{
    m_recentMenu->clear();
    for (const QString &p : AppSettings::instance().recentProjects) {
        m_recentMenu->addAction(QFileInfo(p).completeBaseName(), this, [this, p] { openProject(p); })->setToolTip(p);
    }
    if (m_recentMenu->isEmpty())
        m_recentMenu->addAction(tr("No recent projects"))->setEnabled(false);
    m_recentMenu->addSeparator();
    m_recentMenu->addAction(tr("Clear List"), this, [] {
        AppSettings::instance().recentProjects.clear();
        AppSettings::instance().save();
    });
}

// ------------------------------------------------------------------ panes

EditorPane *MainWindow::otherPane(EditorPane *p) const { return p == m_panes[0] ? m_panes[1] : m_panes[0]; }

bool MainWindow::isSplit() const { return m_panes[1]->isVisible(); }

void MainWindow::setActivePane(EditorPane *p)
{
    if (!p || !p->isVisible())
        return;
    if (m_active == p) {
        return;
    }
    m_active = p;
    for (EditorPane *x : m_panes)
        x->setActive(x == m_active, isSplit());
    refreshInspector();
    updateViewButtons();
    updateActions();
    updateFormatBar();
}

void MainWindow::setSplit(Qt::Orientation o, bool on)
{
    if (!on) {
        if (m_active == m_panes[1]) {
            // keep what the active pane shows in the remaining pane
            m_panes[0]->showItems(m_panes[1]->items());
        }
        m_panes[1]->hide();
        m_active = m_panes[0];
    } else {
        m_editorSplit->setOrientation(o);
        if (!m_panes[1]->isVisible()) {
            m_panes[1]->show();
            m_panes[1]->setProject(m_project.get());
            if (m_project)
                m_panes[1]->showItems(m_active->items());
            const int total = o == Qt::Vertical ? m_editorSplit->height() : m_editorSplit->width();
            m_editorSplit->setSizes({total / 2, total / 2});
        }
        for (EditorPane *p : m_panes)
            p->setSplitIcon(o);
    }
    for (EditorPane *x : m_panes)
        x->setActive(x == m_active, isSplit());
    updateActions();
}

EditorPane *MainWindow::ensureOtherPane()
{
    if (!isSplit())
        setSplit(Qt::Vertical, true);
    return otherPane(m_active);
}

void MainWindow::loadInPane(EditorPane *pane, const QList<BinderItem *> &items)
{
    if (!pane || !m_project)
        return;
    pane->showItems(items);
    if (pane == m_active)
        refreshInspector();
    updateActions();
}

void MainWindow::refreshInspector()
{
    if (!m_project || !m_active)
        return;
    // the binder marks the document of the active editor while another one is selected
    {
        const QList<BinderItem *> shown = m_active->items();
        m_binder->setEditorItem(shown.size() == 1 ? shown.first() : nullptr);
    }
    if (TextEditor *held = m_active->focusedCopyholderEditor()) {
        // the keyboard is in the copyholder: its document is the one being worked on
        m_inspector->setItem(m_active->copyholderItem());
        m_inspector->setEditor(held);
        return;
    }
    BinderItem *it = m_active->currentItem();
    m_inspector->setItem(it);
    TextEditor *ed = m_active->currentEditor();
    if (!ed && it && it->hasText()) {
        // inspector comments need an editor on the document: use the text view if any
        for (TextEditor *e : m_active->textView()->editors())
            if (e->item() == it)
                ed = e;
    }
    m_inspector->setEditor(ed);
    // comments and footnotes of several documents (0.17): those of a folder and
    // what is below it, those shown in combined text, or those of a document no editor shows
    QList<BinderItem *> scope;
    if (m_active->mode() == ViewMode::Combined) {
        for (TextEditor *e : m_active->textView()->editors())
            if (e->item() && !scope.contains(e->item()))
                scope << e->item();
        if (scope.size() < 2)
            scope.clear();
    } else if (it && it->isContainer()) {
        std::function<void(BinderItem *)> walk = [&](BinderItem *b) {
            if (b->hasText())
                scope << b;
            for (BinderItem *c : b->children)
                walk(c);
        };
        walk(it);
        if (scope.isEmpty())
            scope << it; // "no comments" rather than the comments of another document
    } else if (it && it->hasText() && !ed) {
        scope << it;
    }
    m_inspector->setCommentScope(scope);
}

// ------------------------------------------------------------------ project lifecycle

static QString lockPath(const Project *p) { return p->settingsDir() + QLatin1String("/lock"); }

QString MainWindow::projectFileFilter()
{
    return tr("All projects (*%1 *.scrivx);;%2 projects (*%1);;Scrivener projects (*.scrivx)")
        .arg(QLatin1String(QB_PROJECT_FILE_SUFFIX), QLatin1String(QB_APP_NAME));
}

bool MainWindow::openProject(const QString &path)
{
    if (scrivener::isScrivenerPath(path) || !ScrivenerImporter::findScrivx(path).isEmpty())
        return openScrivenerProject(path);
    const QString file = Project::resolveProjectFile(path);
    if (file.isEmpty()) {
        QMessageBox::warning(this, tr("Open Project"), tr("No project was found at\n%1").arg(path));
        AppSettings::instance().removeRecent(path);
        return false;
    }
    if (m_project && QFileInfo(m_project->indexPath()) == QFileInfo(file))
        return true;
    // lock check
    const QString lock = QFileInfo(file).absolutePath() + QLatin1String("/settings/lock");
    QFile lf(lock);
    if (lf.open(QIODevice::ReadOnly)) {
        const QStringList parts = QString::fromUtf8(lf.readAll()).split(QLatin1Char(' '));
        lf.close();
        if (parts.size() == 2) {
            const bool sameHost = parts[0] == QSysInfo::machineHostName();
            const bool alive = sameHost && QFile::exists(QStringLiteral("/proc/") + parts[1])
                               && parts[1].toLongLong() != QCoreApplication::applicationPid();
            if (alive || !sameHost) {
                if (QMessageBox::question(this, tr("Open Project"),
                                          tr("This project seems to be open in another window or on another computer (%1).\n\n"
                                             "Opening it twice can lose changes. Open it anyway?")
                                              .arg(parts[0]))
                    != QMessageBox::Yes)
                    return false;
            }
        }
    }
    // saved by a newer version with something this version would lose
    const QString needs = Project::neededVersion(file);
    if (!needs.isEmpty() && Project::compareVersionText(needs, QStringLiteral(QB_APP_VERSION)) > 0) {
        QMessageBox box(QMessageBox::Warning, tr("Open Project"),
                        tr("This project was saved by a newer version of %1. It uses things that version %2 does not know "
                           "(it needs version %3 or later).\n\nIf you work on it here, parts of it can be lost when it is "
                           "saved. Better switch to the newer version first (Help > Add-on Manager), or work on a copy of "
                           "the project.")
                            .arg(QStringLiteral(QB_APP_NAME), QStringLiteral(QB_APP_VERSION), needs),
                        QMessageBox::NoButton, this);
        box.setObjectName(QStringLiteral("newerProjectWarning"));
        QPushButton *open = box.addButton(tr("Open Anyway"), QMessageBox::AcceptRole);
        box.setDefaultButton(box.addButton(QMessageBox::Cancel));
        box.exec();
        if (box.clickedButton() != open)
            return false;
    }
    if (m_project && !closeProject())
        return false;
    QString err;
    auto p = Project::open(file, &err);
    if (!p && QFile::exists(Project::spareCopyOf(file))) {
        // the project file is damaged: the copy kept when the project last opened (0.20)
        QMessageBox box(QMessageBox::Warning, tr("Open Project"),
                        tr("The project file cannot be read:\n%1\n\nThere is a copy of it from %2, the last time the project "
                           "opened. Use that copy? Changes to the binder made since then (titles, order, labels ...) are "
                           "missing in it; the texts of the documents are not affected. The damaged file is kept.")
                            .arg(err, QFileInfo(Project::spareCopyOf(file)).lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"))),
                        QMessageBox::NoButton, this);
        box.setObjectName(QStringLiteral("spareCopyQuestion"));
        QPushButton *use = box.addButton(tr("Use the Copy"), QMessageBox::AcceptRole);
        use->setObjectName(QStringLiteral("useSpareCopy"));
        box.setDefaultButton(box.addButton(QMessageBox::Cancel));
        box.exec();
        if (box.clickedButton() != use)
            return false;
        QString keptAs;
        if (!Project::restoreSpareCopy(file, &keptAs, &err)) {
            QMessageBox::warning(this, tr("Open Project"), err);
            return false;
        }
        p = Project::open(file, &err);
        if (p) {
            p->openNotice = tr("The project was opened from the copy of its project file. The damaged file is kept as %1.").arg(keptAs);
            QString folder;
            if (const int n = p->recoverUnlistedDocuments(&folder))
                p->openNotice += QLatin1Char(' ')
                                 + tr("%n document(s) written after that copy are in the folder \u201c%1\u201d of Research.", nullptr, n)
                                       .arg(folder);
        }
    }
    if (!p) {
        QMessageBox::warning(this, tr("Open Project"), err);
        return false;
    }
    QString notice = p->openNotice;
    if (!adoptProject(std::move(p)))
        return false;
    // object files that could not be read (0.20): they stay as they are, the writer is told
    if (m_project && !m_project->objects()->warnings().isEmpty())
        notice += (notice.isEmpty() ? QString() : QStringLiteral("\n\n")) + m_project->objects()->warnings().join(QLatin1Char('\n'));
    if (!notice.isEmpty() && !qEnvironmentVariableIsSet("QB_NO_DIALOGS")) {
        QMessageBox box(QMessageBox::Information, tr("Open Project"), notice, QMessageBox::Ok, this);
        box.setObjectName(QStringLiteral("openNotice"));
        box.exec();
    }
    return true;
}

bool MainWindow::adoptProject(std::unique_ptr<Project> p, bool isNew)
{
    if (!p)
        return false;
    if (m_project && !closeProject())
        return false;
    m_project = std::move(p);
    Project *proj = m_project.get();
    palette::setProject(proj); // the label colors in every color choice (0.21)
    // every project keeps its own look (0.40.2): a project opened for the first time by this
    // version keeps the look it had, a new one gets the look for new projects
    look::activate(proj, look::settle(proj, isNew));
    {
        QFile lf(lockPath(proj));
        if (lf.open(QIODevice::WriteOnly))
            lf.write(QStringLiteral("%1 %2").arg(QSysInfo::machineHostName()).arg(QCoreApplication::applicationPid()).toUtf8());
    }
    proj->setAutosaveDelay(qMax(1, AppSettings::instance().autosaveSeconds) * 1000);
    // a project imported from Scrivener by a version before 0.14.0 still has Scrivener's style
    // marks ("<$Scr_H::1>Title<!$Scr_H::1>") as text: taken out before anything shows it (0.21.2)
    if (const int fixed = ScrivenerImporter::removeLeftoverMarkers(proj); fixed > 0) {
        QTimer::singleShot(0, this, [this, fixed] {
            statusBar()->showMessage(fixed == 1 ? tr("Scrivener's style marks were taken out of 1 document.")
                                                : tr("Scrivener's style marks were taken out of %1 documents.").arg(fixed),
                                     15000);
        });
    }
    // File > Options > Editing: the editors open with the zoom they had, or with the default text zoom (0.21)
    if (!AppSettings::instance().reloadZoom || !proj->uiState.contains(QStringLiteral("zoom")))
        proj->docs()->setZoom(zoom::defaultFactor(QLatin1String(zoom::kEditor)));
    m_editedSinceSave.clear();
    connect(proj->docs(), &DocumentStore::textEdited, this, [this](const QString &id) {
        m_editedSinceSave.insert(id);
        queueAutoTitle(id);
    });
    m_autoTitlePending.clear();
    queueUntitledDocuments(); // documents named "Untitled" that have text (0.40.3)
    m_binder->setProject(proj);
    for (EditorPane *pane : m_panes)
        pane->setProject(proj);
    m_inspector->setProject(proj);
    rebuildIconBar(); // the project may have icons of its own in the bar
    connect(proj, &Project::countsChanged, this, &MainWindow::updateTargetsDisplay);
    connect(proj, &Project::settingsChanged, this, [this] {
        updateTargetsDisplay();
        updateActions();
        m_binder->refreshCollections();
    });
    connect(proj, &Project::stylesChanged, this, &MainWindow::refreshStyleUi);
    connect(proj, &Project::scriptFormatChanged, this, [this] {
        rebuildScriptActions();
        for (EditorPane *p : m_panes)
            if (p)
                p->updateFooter();
    });
    refreshStyleUi();
    rebuildScriptActions();
    connect(proj, &Project::titleChanged, this, &MainWindow::updateWindowTitle);
    // a text file that is there but cannot be read: shown empty, read-only, never saved over (0.20)
    connect(proj->docs(), &DocumentStore::unreadableFile, this,
            [this](const QString &id, bool notes, const QString &path, const QString &err) {
                BinderItem *it = m_project ? m_project->item(id) : nullptr;
                const QString title = it ? it->title : id;
                QTimer::singleShot(0, this, [this, title, notes, path, err] {
                    QMessageBox box(QMessageBox::Warning, tr("Document Cannot Be Read"),
                                    tr("The %1 of \"%2\" cannot be read (%3):\n%4\n\nIt is shown empty and cannot be "
                                       "edited, so that the file is not overwritten. Check the file's permissions or the "
                                       "drive it is on, then close and reopen the project.")
                                        .arg(notes ? tr("notes") : tr("text"), title, err, path),
                                    QMessageBox::Ok, this);
                    box.setObjectName(QStringLiteral("unreadableDocumentNotice"));
                    box.exec();
                });
            });
    connect(proj, &Project::saveFailed, this, [this](const QString &err) {
        static QDateTime last;
        if (last.isValid() && last.secsTo(QDateTime::currentDateTime()) < 60)
            return;
        last = QDateTime::currentDateTime();
        QMessageBox::warning(this, tr("Saving Failed"),
                             tr("Your latest changes could not be saved:\n%1\n\nCheck that the disk is not full and the "
                                "project folder is writable.")
                                 .arg(err));
    });
    connect(proj->model(), &BinderModel::filesDropped, this, [this](BinderItem *parent, int row, const QList<QUrl> &urls) {
        importDropped(parent, row, urls);
    });
    connect(proj->model(), &BinderModel::textDropped, this,
            [this](BinderItem *parent, int row, const QString &text, const QByteArray &rtf) {
                // text dragged into the binder becomes a new document (the text stays where it came from)
                if (text.trimmed().isEmpty())
                    return;
                const QString firstLine = text.trimmed().section(QLatin1Char('\n'), 0, 0);
                BinderItem *it = m_project->model()->createItem(parent, row, ItemType::Text,
                                                                text::previewText(firstLine, 40).remove(QChar(0x2026)));
                if (!it)
                    return;
                if (!rtf.isEmpty()) {
                    // from one of our editors or a word processor: with its formatting
                    QTextDocument tmp;
                    rtf::ReadOptions o;
                    o.keepFormatting = true;
                    rtf::read(rtf, &tmp, m_project->formatting, o);
                    m_project->docs()->setContent(it->id, &tmp);
                } else {
                    templates::setMarkdown(m_project.get(), it, md::fromPlainText(text));
                }
                m_updatingUi = true;
                m_binder->select({it}, false);
                m_updatingUi = false;
            });
    auto structural = [this] {
        QTimer::singleShot(0, this, [this] {
            for (EditorPane *pane : m_panes)
                pane->refresh();
            updateActions();
        });
    };
    connect(proj->model(), &QAbstractItemModel::rowsMoved, this, structural);
    connect(proj->model(), &QAbstractItemModel::rowsRemoved, this, structural);
    connect(proj->model(), &QAbstractItemModel::rowsInserted, this, structural);

    AppSettings::instance().addRecent(proj->displayFile());
    AppSettings::instance().lastProjectDir = QFileInfo(proj->displayFolder()).absolutePath();
    AppSettings::instance().save();
    // for crash reports: which project, and how it is kept
    crash::setSessionProject(proj->displayFile());
    crash::note(QStringLiteral("project opened: %1 (%2 items)").arg(proj->title()).arg(proj->allItems().size()));
    sysinfo::refresh(proj);
    restoreUiState();
    updateWindowTitle();
    updateTargetsDisplay();
    updateActions();
    if (AppSettings::instance().backupOnOpen) {
        QString out, err;
        backup::backupProject(proj, AppSettings::instance().backupDir, AppSettings::instance().backupKeep, &out, &err);
    }
    toolhost::host().notifyProjectChanged();
    return true;
}

bool MainWindow::closeProject()
{
    if (!m_project)
        return true;
    if (m_composition)
        m_composition->close();
    // audio and video stop; where they were goes into the project's state
    for (MediaView *v : playback::hub().views())
        v->release();
    saveUiState();
    // mind maps: saved, their windows closed, their boards let go (before the project goes)
    m_inspector->mindMapPanel()->setItem(nullptr, QString());
    mindmaps::closeAll(m_project.get());
    ObjectsWindow::closeFor(m_project.get());
    QString err;
    if (!m_project->save(&err)) {
        if (QMessageBox::warning(this, tr("Close Project"),
                                 tr("The project could not be saved:\n%1\n\nClose anyway and lose unsaved changes?").arg(err),
                                 QMessageBox::Yes | QMessageBox::No)
            != QMessageBox::Yes)
            return false;
    }
    if (AppSettings::instance().backupOnClose) {
        QString out;
        if (!backup::backupProject(m_project.get(), AppSettings::instance().backupDir, AppSettings::instance().backupKeep,
                                   &out, &err))
            QMessageBox::warning(this, tr("Backup"), tr("The automatic backup failed:\n%1").arg(err));
    }
    QFile::remove(lockPath(m_project.get()));
    for (QWidget *w : QApplication::topLevelWidgets())
        if (auto *qr = qobject_cast<QuickRefWindow *>(w))
            qr->close();
    if (m_formatFind)
        m_formatFind->close();
    m_binder->setProject(nullptr);
    for (EditorPane *pane : m_panes)
        pane->setProject(nullptr);
    m_inspector->setProject(nullptr);
    setSplit(Qt::Vertical, false);
    palette::setProject(nullptr);
    look::activate(nullptr); // the areas show the look for new projects again
    m_project.reset();
    crash::setSessionProject(QString());
    crash::note(QStringLiteral("project closed"));
    sysinfo::refresh(nullptr);
    refreshStyleUi();
    updateWindowTitle();
    updateTargetsDisplay();
    updateActions();
    toolhost::host().notifyProjectChanged();
    return true;
}

void MainWindow::saveUiState()
{
    if (!m_project)
        return;
    QVariantMap &ui = m_project->uiState;
    ui[QStringLiteral("expanded")] = m_binder->expandedIds();
    QStringList sel;
    for (BinderItem *it : m_binder->selectedItems())
        sel << it->id;
    ui[QStringLiteral("binderSelection")] = sel;
    ui[QStringLiteral("pane0")] = m_panes[0]->saveState();
    ui[QStringLiteral("split")] = isSplit();
    ui[QStringLiteral("splitOrientation")] = int(m_editorSplit->orientation());
    if (isSplit())
        ui[QStringLiteral("pane1")] = m_panes[1]->saveState();
    ui[QStringLiteral("activePane")] = m_active == m_panes[1] ? 1 : 0;
    ui[QStringLiteral("collections")] = m_binder->collectionsVisible();
    ui[QStringLiteral("binderAffects")] = m_binderAffects;
    for (int i = 0; i < 2; ++i) {
        ui[QStringLiteral("corkMode%1").arg(i)] = m_panes[i]->corkMode();
        ui[QStringLiteral("affectsOther%1").arg(i)] = m_panes[i]->selectionAffectsOther();
        ui[QStringLiteral("affectsCopyholder%1").arg(i)] = m_panes[i]->selectionAffects() == EditorPane::AffectsCopyholder;
    }
    // the open Quick Reference panels come back with the project
    QVariantList panels;
    for (QuickRefWindow *qr : quickRefWindows())
        panels << qr->saveState();
    ui[QStringLiteral("quickRefs")] = panels;
    // where the writer was in the documents shown, and the inspector (0.21.1)
    for (QWidget *w : QApplication::allWidgets())
        if (auto *v = qobject_cast<TextColumnView *>(w); v && v->project() == m_project.get())
            v->rememberPlace();
        else if (auto *mv = qobject_cast<MediaView *>(w); mv && mv->project() == m_project.get())
            mv->rememberMediaPlace();
        else if (auto *bv = qobject_cast<MindMapView *>(w))
            bv->saveView();
    viewmem::setInspectorState(m_project.get(), m_inspector->saveState());
    m_project->markIndexDirty();
}

void MainWindow::restoreUiState()
{
    const QVariantMap ui = m_project->uiState;
    m_binder->restoreExpanded(ui.value(QStringLiteral("expanded")).toStringList());
    const bool cols = ui.value(QStringLiteral("collections")).toBool();
    a[QStringLiteral("showCollections")]->setChecked(cols);
    m_binder->setCollectionsVisible(cols);
    QList<BinderItem *> sel;
    for (const QString &id : ui.value(QStringLiteral("binderSelection")).toStringList())
        if (BinderItem *it = m_project->item(id))
            sel << it;
    m_updatingUi = true;
    m_binder->select(sel, false);
    m_updatingUi = false;
    setBinderAffects(ui.value(QStringLiteral("binderAffects"), QStringLiteral("current")).toString());
    for (int i = 0; i < 2; ++i) {
        m_panes[i]->setCorkMode(ui.value(QStringLiteral("corkMode%1").arg(i), 0).toInt());
        m_panes[i]->setSelectionAffectsOther(ui.value(QStringLiteral("affectsOther%1").arg(i), false).toBool());
    }
    if (ui.contains(QStringLiteral("pane0")))
        m_panes[0]->restoreState(ui.value(QStringLiteral("pane0")).toMap());
    else if (!sel.isEmpty())
        m_panes[0]->showItems(sel);
    else if (!m_project->draft()->children.isEmpty())
        m_panes[0]->showItems({m_project->draft()->children.first()});
    if (ui.value(QStringLiteral("split")).toBool()) {
        setSplit(Qt::Orientation(ui.value(QStringLiteral("splitOrientation"), int(Qt::Vertical)).toInt()), true);
        m_panes[1]->restoreState(ui.value(QStringLiteral("pane1")).toMap());
        if (ui.value(QStringLiteral("activePane")).toInt() == 1)
            setActivePane(m_panes[1]);
    }
    for (int i = 0; i < 2; ++i)
        if (ui.value(QStringLiteral("affectsCopyholder%1").arg(i)).toBool())
            m_panes[i]->setSelectionAffects(EditorPane::AffectsCopyholder);
    refreshInspector();
    // the inspector's tab, dividers, bookmark and object as they were (0.21.1)
    m_inspector->restoreState(viewmem::inspectorState(m_project.get()));
    if (AppSettings::instance().reopenQuickRefs) {
        for (const QVariant &v : ui.value(QStringLiteral("quickRefs")).toList()) {
            const QVariantMap st = v.toMap();
            if (BinderItem *it = m_project->item(st.value(QStringLiteral("item")).toString()))
                if (!m_project->isInTrash(it))
                    if (QuickRefWindow *qr = openQuickReference(it))
                        qr->restoreState(st);
        }
    }
    QTimer::singleShot(100, this, [this] {
        if (m_active) {
            activateWindow(); // (reopened panels must not keep the keyboard)
            m_active->focusEditor();
        }
    });
}

void MainWindow::showStartDialog(bool recentFirst)
{
    StartDialog dlg(this, recentFirst);
    if (dlg.exec() != QDialog::Accepted)
        return;
    switch (dlg.action()) {
    case StartDialog::Action::Create: {
        QString err;
        const QString scrivDir = QDir(dlg.location()).filePath(text::safeFileName(dlg.projectName()) + QLatin1String(".scriv"));
        if (dlg.scrivenerFormat() && QFileInfo::exists(scrivDir)) {
            QMessageBox::warning(this, tr("New Project"), tr("There is a file or folder with this name already:\n%1").arg(scrivDir));
            return;
        }
        const QString madeIn = dlg.scrivenerFormat() ? scrivener::workingCopiesDir() + QLatin1String("/.new-")
                                                           + QString::number(QCoreApplication::applicationPid())
                                                     : dlg.location();
        QDir().mkpath(madeIn);
        auto p = templates::createProject(dlg.selectedTemplate(), madeIn, dlg.projectName(), &err);
        if (!p) {
            QMessageBox::warning(this, tr("New Project"), err);
            return;
        }
        {
            // File > Options > Editing: formatting of new projects
            const AppSettings &as = AppSettings::instance();
            Formatting nf;
            nf.fontFamily = as.newFontFamily;
            nf.fontSize = as.newFontSize;
            nf.lineSpacing = as.newLineSpacing;
            nf.firstLineIndent = as.newFirstLineIndent;
            nf.paragraphSpacing = as.newParagraphSpacing;
            nf.align = as.newAlign;
            // styles: those of the template, else the ones saved for new projects
            nf.styles = p->formatting.styles;
            nf.script = p->formatting.script;
            StyleSheet saved;
            if (!nf.styles && styles::fromXmlString(as.newStylesXml, &saved))
                nf = styles::withSheet(nf, saved);
            p->formatting = nf;
            p->docs()->applyFormatting();
            p->save();
        }
        if (dlg.scrivenerFormat()) {
            // made in a place of its own first, then written as .scriv
            const QString scrivDir = QDir(dlg.location()).filePath(text::safeFileName(dlg.projectName()) + QLatin1String(".scriv"));
            const QString tmpFolder = p->folderPath();
            auto wc = scrivener::convertToScrivener(p.get(), scrivDir, &err);
            p.reset();
            QDir(tmpFolder).removeRecursively();
            QDir(madeIn).removeRecursively();
            if (!wc) {
                QMessageBox::warning(this, tr("New Project"), err);
                return;
            }
            adoptProject(std::move(wc), true);
            break;
        }
        adoptProject(std::move(p), true);
        break;
    }
    case StartDialog::Action::Open:
        openProject(dlg.openPath());
        break;
    case StartDialog::Action::ImportScrivener: {
        auto p = runScrivenerImport(this);
        if (p)
            adoptProject(std::move(p), true);
        break;
    }
    case StartDialog::Action::None: break;
    }
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (!closeProject()) {
        e->ignore();
        return;
    }
    toolhost::host().shutdownTools(); // tools save their work (scratch pad notes)
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/state"), saveState());
    const QList<int> sizes = m_mainSplit->sizes();
    s.setValue(QStringLiteral("window/mainSplit"), QVariantList{sizes.value(0), sizes.value(1), sizes.value(2)});
    s.setValue(QStringLiteral("window/inspector"), a[QStringLiteral("showInspector")]->isChecked());
    s.setValue(QStringLiteral("window/binder"), a[QStringLiteral("showBinder")]->isChecked());
    s.setValue(QStringLiteral("window/toolbar"), a[QStringLiteral("showToolbar")]->isChecked());
    s.setValue(QStringLiteral("window/formatBar"), a[QStringLiteral("showFormatBar")]->isChecked());
    e->accept();
}

void MainWindow::saveForTermination()
{
    // SIGTERM, SIGINT or SIGHUP (log out, shutdown, a kill command): everything saved
    // without questions; the program ends right after this (core/CrashGuard.cpp).
    if (m_project) {
        saveUiState();
        mindmaps::saveAll(m_project.get());
        QString err;
        if (!m_project->save(&err))
            std::fprintf(stderr, "%s: saving before the end failed: %s\n", QB_APP_NAME, qPrintable(err));
        QFile::remove(lockPath(m_project.get()));
    }
    toolhost::host().shutdownTools(); // the writing tools save their work (scratch pad notes)
    QLocalServer::removeServer(toolhost::host().linkPath());
}

void MainWindow::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::ActivationChange && !isActiveWindow() && m_project)
        m_project->save(); // save when switching to another program
    QMainWindow::changeEvent(e);
}

// ------------------------------------------------------------------ item operations

QList<BinderItem *> MainWindow::actionItems() const
{
    if (!m_project)
        return {};
    QWidget *fw = QApplication::focusWidget();
    if (m_active && (m_active->mode() == ViewMode::Corkboard || m_active->mode() == ViewMode::Outliner)
        && fw && m_active->isAncestorOf(fw)) {
        const auto sel = m_active->selectedItems();
        if (!sel.isEmpty())
            return sel;
    }
    if (fw && m_active && m_active->hasCopyholder() && m_active->copyholder()->isAncestorOf(fw))
        return {m_active->copyholderItem()}; // the keyboard is in the copyholder: its document
    if (fw && m_active && m_active->isAncestorOf(fw)) {
        if (BinderItem *it = m_active->currentItem())
            return {it};
    }
    const auto sel = m_binder->selectedItems();
    if (!sel.isEmpty())
        return sel;
    if (m_active && m_active->currentItem())
        return {m_active->currentItem()};
    return {};
}

BinderItem *MainWindow::insertionPoint(int *row) const
{
    *row = -1;
    QList<BinderItem *> sel = actionItems();
    // inside a corkboard/outliner: add to the shown container
    QWidget *fw = QApplication::focusWidget();
    if (m_active && (m_active->mode() == ViewMode::Corkboard || m_active->mode() == ViewMode::Outliner) && fw
        && m_active->isAncestorOf(fw)) {
        const auto cards = m_active->selectedItems();
        if (!cards.isEmpty()) {
            *row = cards.last()->row() + 1;
            return cards.last()->parent;
        }
        if (m_active->items().size() == 1)
            return m_active->items().first();
    }
    BinderItem *target = sel.isEmpty() ? nullptr : sel.last();
    if (!target)
        return m_project->draft();
    if (m_project->isInTrash(target))
        return m_project->draft();
    if (target->isFolder())
        return target;
    *row = target->row() + 1;
    return target->parent;
}

BinderItem *MainWindow::newItem(ItemType type, const QString &templateId)
{
    int row = -1;
    BinderItem *parent = insertionPoint(&row);
    QString tmpl = templateId;
    if (tmpl.isEmpty() && type == ItemType::Text)
        tmpl = parent->defaultTemplate;
    BinderItem *tpl = tmpl.isEmpty() ? nullptr : m_project->item(tmpl);
    const QString title = type == ItemType::Folder ? tr("New Folder") : (tpl ? tpl->title : tr("Untitled"));
    BinderItem *it = m_project->model()->createItem(parent, row, type, title);
    if (tpl) {
        m_project->docs()->setContent(it->id, m_project->docs()->text(tpl->id));
        it->synopsis = tpl->synopsis;
        it->icon = tpl->icon;
        it->label = tpl->label;
        it->status = tpl->status;
        it->keywords = tpl->keywords;
        it->custom = tpl->custom;
        if (tpl->scriptMode) // (an empty script sheet: the first paragraph gets the first element)
            m_project->setScriptMode(it, true);
    } else if (type == ItemType::Text) {
        // among scripts a new document is a script as well
        int texts = 0, scripts = 0;
        for (BinderItem *c : parent->children)
            if (c != it && c->type == ItemType::Text) {
                ++texts;
                scripts += c->scriptMode;
            }
        if (texts > 0 && scripts == texts)
            m_project->setScriptMode(it, true);
    }
    // show it and let the user type the title
    m_binder->show();
    m_updatingUi = true;
    m_binder->select({it}, false);
    m_updatingUi = false;
    if (!m_active->isLocked()) {
        if (m_active->mode() == ViewMode::Corkboard || m_active->mode() == ViewMode::Outliner) {
            m_active->refresh();
            if (m_active->mode() == ViewMode::Corkboard) {
                m_active->corkboard()->rebuild();
                m_active->corkboard()->selectItems({it});
                m_active->corkboard()->editTitleOf(it);
                return it;
            }
        } else {
            loadInPane(m_active, {it});
        }
    }
    m_binder->editTitle(it);
    return it;
}

void MainWindow::moveSelection(int dx, int dy)
{
    if (!m_project)
        return;
    QList<BinderItem *> items = BinderModel::topLevelOnly(actionItems());
    if (items.isEmpty())
        return;
    BinderModel *m = m_project->model();
    BinderItem *first = items.first();
    BinderItem *parent = first->parent;
    for (BinderItem *it : items)
        if (it->parent != parent || it->isRootFolder())
            return;
    if (dy < 0) {
        const int r = first->row();
        if (r > 0)
            m->moveItems(items, parent, r - 1);
    } else if (dy > 0) {
        const int r = items.last()->row();
        if (r + 1 < parent->children.size())
            m->moveItems(items, parent, r + 2);
    } else if (dx < 0) {
        if (parent->parent)
            m->moveItems(items, parent->parent, parent->row() + 1);
    } else if (dx > 0) {
        const int r = first->row();
        if (r > 0) {
            BinderItem *target = parent->children.at(r - 1);
            m->moveItems(items, target, target->children.size());
            m_binder->tree()->expand(m->indexFor(target));
        }
    }
    m_updatingUi = true;
    m_binder->select(items, false);
    m_updatingUi = false;
}

void MainWindow::duplicateItems()
{
    if (!m_project)
        return;
    QList<BinderItem *> created;
    std::function<BinderItem *(BinderItem *, BinderItem *, int, bool)> dup = [&](BinderItem *src, BinderItem *parent, int row,
                                                                                 bool top) -> BinderItem * {
        BinderItem *it = m_project->model()->createItem(parent, row, src->type, top ? src->title + tr(" (copy)") : src->title);
        it->synopsis = src->synopsis;
        it->label = src->label;
        it->status = src->status;
        it->keywords = src->keywords;
        it->custom = src->custom;
        it->icon = src->icon;
        it->scriptMode = src->scriptMode;
        it->fileExt = src->fileExt;
        it->includeInCompile = src->includeInCompile;
        it->sectionType = src->sectionType;
        it->target = src->target;
        it->targetChars = src->targetChars;
        m_project->docs()->copyDocument(src->id, it->id);
        it->wordCount = src->wordCount;
        it->charCount = src->charCount;
        for (BinderItem *c : src->children)
            dup(c, it, -1, false);
        return it;
    };
    for (BinderItem *src : BinderModel::topLevelOnly(actionItems())) {
        if (src->isRootFolder())
            continue;
        created << dup(src, src->parent, src->row() + 1, true);
    }
    m_project->markIndexDirty();
    m_binder->select(created, true);
}

void MainWindow::splitDocument(bool selectionAsTitle)
{
    TextEditor *e = m_active->currentEditor();
    if (!e || !e->item() || !m_project || e->isNotes())
        return;
    BinderItem *item = e->item();
    QTextCursor c = e->textCursor();
    QString title = tr("Untitled");
    int pos = c.position();
    if (selectionAsTitle) {
        if (!c.hasSelection()) {
            QMessageBox::information(this, tr("Split"), tr("Select the text to use as the title of the new document."));
            return;
        }
        title = c.selectedText().simplified().left(120);
        pos = c.selectionStart();
    }
    QTextDocument *doc = e->document();
    QTextCursor rest(doc);
    rest.setPosition(pos);
    rest.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    const QTextDocumentFragment frag = rest.selection();
    QTextDocument part;
    display::prepareDocument(&part, m_project->formatting, 1.0);
    QTextCursor pc(&part);
    pc.insertFragment(frag);
    {
        // a fragment does not bring the paragraph format (style, script element) of its first paragraph along
        const QTextBlock from = doc->findBlock(pos);
        QTextCursor fc(part.begin());
        fc.setBlockFormat(from.blockFormat());
        fc.setBlockCharFormat(from.charFormat());
    }
    // drop a leading empty paragraph
    if (part.blockCount() > 1 && part.begin().text().isEmpty()) {
        QTextCursor del(&part);
        del.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
        del.removeSelectedText();
    }
    rest.beginEditBlock();
    // split at the start of a paragraph: the paragraph break before it goes as well,
    // so that no empty paragraph stays behind
    if (pos > 0 && doc->findBlock(pos).position() == pos) {
        rest.setPosition(pos - 1);
        rest.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    }
    rest.removeSelectedText();
    rest.endEditBlock();
    BinderItem *it = m_project->model()->createItem(item->parent, item->row() + 1, ItemType::Text, title);
    m_project->docs()->setContent(it->id, &part);
    it->label = item->label;
    it->status = item->status;
    it->scriptMode = item->scriptMode;
    m_project->docs()->recount(item->id);
    if (m_active->mode() == ViewMode::Single)
        loadInPane(m_active, {it});
    m_updatingUi = true;
    m_binder->select({it}, false);
    m_updatingUi = false;
}

void MainWindow::mergeDocuments()
{
    if (!m_project)
        return;
    QList<BinderItem *> items;
    for (BinderItem *it : actionItems())
        if (it->hasText() && !it->isRootFolder())
            items << it;
    if (items.size() < 2)
        return;
    std::sort(items.begin(), items.end(), [](BinderItem *a, BinderItem *b) {
        QList<int> pa, pb;
        for (BinderItem *x = a; x->parent; x = x->parent)
            pa.prepend(x->row());
        for (BinderItem *x = b; x->parent; x = x->parent)
            pb.prepend(x->row());
        return pa < pb;
    });
    if (QMessageBox::question(this, tr("Merge Documents"),
                              tr("Merge %1 documents into \"%2\"? The others are moved to the Trash.")
                                  .arg(items.size())
                                  .arg(items.first()->title))
        != QMessageBox::Yes)
        return;
    BinderItem *target = items.first();
    QTextDocument *doc = m_project->docs()->text(target->id);
    QTextCursor c(doc);
    c.beginEditBlock();
    const AppSettings &as = AppSettings::instance();
    for (int i = 1; i < items.size(); ++i) {
        c.movePosition(QTextCursor::End);
        // File > Options > General > Separators (0.21)
        display::insertSeparator(c, as.sepMerge, as.sepMergeCustom, m_project->formatting);
        c.insertFragment(QTextDocumentFragment(m_project->docs()->text(items[i]->id)));
        if (!items[i]->synopsis.isEmpty())
            target->synopsis += (target->synopsis.isEmpty() ? QString() : QStringLiteral("\n")) + items[i]->synopsis;
        for (const QString &k : items[i]->keywords)
            if (!target->keywords.contains(k))
                target->keywords << k;
    }
    c.endEditBlock();
    m_project->notifyItemChanged(target, true);
    m_project->model()->moveToTrash(items.mid(1));
    loadInPane(m_active, {target});
    m_binder->select({target}, false);
}

void MainWindow::setIconFor(const QString &icon)
{
    if (!m_project)
        return;
    for (BinderItem *it : actionItems()) {
        it->icon = icon;
        m_project->notifyItemChanged(it, false);
    }
    m_project->markIndexDirty();
    icons::clearCache();
    rememberIcon(icon);
}

void MainWindow::openLink(EditorPane *from, const QString &href)
{
    // an object (0.20): shown in the Objects tab of the inspector
    if (!objects::idFromLink(href).isEmpty()) {
        const QString id = objects::idFromLink(href);
        if (!m_project || !m_project->objects()->object(id)) {
            QMessageBox::information(this, tr("Object Link"), tr("The linked object no longer exists."));
            return;
        }
        showObject(id);
        return;
    }
    if (href.startsWith(QLatin1String("qb://"))) {
        BinderItem *it = m_project ? m_project->item(href.mid(5)) : nullptr;
        if (!it) {
            QMessageBox::information(this, tr("Document Link"), tr("The linked document no longer exists."));
            return;
        }
        const QString mode = AppSettings::instance().linkOpens;
        if (mode == QLatin1String("window"))
            openQuickReference(it);
        else if (mode == QLatin1String("current") || !from)
            (from ? from : m_active)->showItems({it});
        else if (mode == QLatin1String("copyholder"))
            from->openCopyholder(it);
        else {
            if (!isSplit())
                setSplit(Qt::Vertical, true);
            otherPane(from)->showItems({it});
        }
        return;
    }
    QDesktopServices::openUrl(QUrl::fromUserInput(href));
}

void MainWindow::showObject(const QString &objectId)
{
    if (!m_project || !m_project->objects()->object(objectId))
        return;
    a[QStringLiteral("showInspector")]->setChecked(true);
    m_inspector->showTab(Inspector::Objects);
    m_inspector->objectsPanel()->showObject(objectId);
}

QuickRefWindow *MainWindow::openQuickReference(BinderItem *item)
{
    if (!m_project || !item)
        return nullptr;
    auto *w = new QuickRefWindow(m_project.get(), item, this);
    if (AppSettings::instance().floatQuickRef)
        w->setFloating(true);
    connect(w, &QuickRefWindow::revealRequested, this, [this](BinderItem *it) {
        if (!it || !m_project)
            return;
        if (!m_binder->isVisible())
            a[QStringLiteral("showBinder")]->trigger();
        m_updatingUi = true;
        m_binder->reveal(it);
        m_updatingUi = false;
        raise();
        activateWindow();
    });
    connect(w, &QuickRefWindow::openInEditorRequested, this, [this](BinderItem *it) {
        if (!it)
            return;
        loadInPane(m_active, {it});
        raise();
        activateWindow();
    });
    // a document link in a panel: where the options say, but never by replacing the panel's own text
    connect(w, &QuickRefWindow::linkActivated, this, [this](const QString &href) {
        if (!href.startsWith(QLatin1String("qb://"))) {
            openLink(nullptr, href);
            return;
        }
        BinderItem *it = m_project ? m_project->item(href.mid(5)) : nullptr;
        if (!it)
            return;
        if (AppSettings::instance().linkOpens == QLatin1String("window"))
            openQuickReference(it);
        else
            loadInPane(m_active, {it});
    });
    shareShortcuts(w);
    w->show();
    return w;
}

QList<QuickRefWindow *> MainWindow::quickRefWindows() const
{
    QList<QuickRefWindow *> out;
    for (QWidget *w : QApplication::topLevelWidgets())
        if (auto *qr = qobject_cast<QuickRefWindow *>(w))
            if (qr->parentWidget() == this && !qr->testAttribute(Qt::WA_WState_Hidden))
                out << qr;
    return out;
}

void MainWindow::shareShortcuts(QWidget *window)
{
    // A shortcut works in the window its command belongs to. The commands that
    // act on "the text with the cursor" (editor() looks at the keyboard focus)
    // are useful in Quick Reference panels and composition mode as well.
    for (const commands::Command &c : commands::all()) {
        if (!c.action)
            continue;
        const bool text = c.category == tr("Format") || c.category == tr("Insert") || c.key.startsWith(QLatin1String("style:")) || c.key.startsWith(QLatin1String("script:"))
                          || c.key == QLatin1String("save")
                          || c.key.startsWith(QLatin1String("media")); // play and pause while typing anywhere
        if (text && !window->actions().contains(c.action))
            window->addAction(c.action);
    }
}

void MainWindow::addToProjectBookmarks(const QList<BinderItem *> &items)
{
    if (!m_project)
        return;
    bool any = false;
    for (BinderItem *it : items) {
        const QString target = QStringLiteral("qb://") + it->id;
        bool exists = false;
        for (const Bookmark &b : m_project->projectBookmarks)
            exists = exists || b.target == target;
        if (!exists) {
            m_project->projectBookmarks.append({it->title, target});
            any = true;
        }
    }
    if (!any)
        return;
    m_project->notifyBookmarksChanged();
    // show where it went: the project's section of Inspector ▸ Bookmarks
    if (m_inspector->isVisible()) {
        m_inspector->showTab(Inspector::Bookmarks);
        if (QListWidget *list = m_inspector->projectBookmarkList(); list && list->count() > 0) {
            list->setCurrentRow(list->count() - 1);
            list->scrollToItem(list->currentItem());
        }
    }
}

void MainWindow::fillItemMenu(QMenu *menu)
{
    menu->addAction(a[QStringLiteral("newText")]);
    menu->addAction(a[QStringLiteral("newFolder")]);
    menu->addMenu(m_templateMenu);
    menu->addSeparator();
    menu->addAction(a[QStringLiteral("openOther")]);
    menu->addAction(a[QStringLiteral("openCopyholder")]);
    menu->addAction(a[QStringLiteral("quickRef")]);
    menu->addSeparator();
    menu->addAction(a[QStringLiteral("rename")]);
    menu->addAction(a[QStringLiteral("duplicate")]);
    menu->addAction(a[QStringLiteral("trash")]);
    menu->addSeparator();
    menu->addAction(a[QStringLiteral("toFolder")]);
    menu->addAction(a[QStringLiteral("toText")]);
    menu->addMenu(m_labelMenu);
    menu->addMenu(m_statusMenu);
    menu->addAction(a[QStringLiteral("includeCompile")]);
    menu->addMenu(m_iconMenu);
    menu->addMenu(m_defaultTemplateMenu);
    menu->addSeparator();
    menu->addAction(a[QStringLiteral("bookmark")]);
    if (m_project && !m_project->collections.isEmpty()) {
        QMenu *cm = menu->addMenu(tr("Add to Collection"));
        for (const Collection &c : m_project->collections) {
            if (c.isSearch)
                continue;
            const QString id = c.id;
            cm->addAction(c.name, this, [this, id] { m_binder->addToCollection(id, actionItems()); });
        }
    }
    menu->addAction(a[QStringLiteral("snapshot")]);
    menu->addAction(a[QStringLiteral("merge")]);
    menu->addAction(a[QStringLiteral("exportFiles")]);
    if (m_project) {
        const auto items = actionItems();
        if (items.size() == 1 && items.first() == m_project->trash())
            menu->addAction(a[QStringLiteral("emptyTrash")]);
    }
}

void MainWindow::showItemContextMenu(const QPoint &globalPos, int lookPart)
{
    updateActions();
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("itemContextMenu"));
    fillItemMenu(&menu);
    if (m_binder->showsArbitraryCollection() && m_binder->resultsList()->hasFocus()) {
        menu.addSeparator();
        menu.addAction(tr("Remove From Collection"), this,
                       [this] { m_binder->removeFromCollection(m_binder->selectedItems()); });
    }
    // the colors and fonts of the binder, corkboard or outliner (0.40.2)
    if (lookPart >= 0)
        lookmenu::add(&menu, lookmenu::Part(lookPart), this);
    menu.exec(globalPos);
}

void MainWindow::showHeaderMenu(EditorPane *pane, const QPoint &globalPos)
{
    setActivePane(pane);
    QMenu menu(this);
    QAction *lock = menu.addAction(tr("Lock in Place"));
    lock->setCheckable(true);
    lock->setChecked(pane->isLocked());
    connect(lock, &QAction::toggled, pane, &EditorPane::setLocked);
    menu.addAction(a[QStringLiteral("reveal")]);
    menu.addAction(a[QStringLiteral("quickRef")]);
    menu.addAction(a[QStringLiteral("openOther")]);
    if (pane->hasCopyholder())
        menu.addAction(a[QStringLiteral("closeCopyholder")]);
    menu.addSeparator();
    menu.addAction(a[QStringLiteral("snapshot")]);
    menu.addAction(a[QStringLiteral("bookmark")]);
    menu.addMenu(m_labelMenu);
    menu.addMenu(m_statusMenu);
    menu.addMenu(m_iconMenu);
    lookmenu::add(&menu, lookmenu::Part::HeaderBar, this); // the header bar's colors and font (0.40.2)
    menu.setObjectName(QStringLiteral("headerContextMenu"));
    menu.exec(globalPos);
    updateActions();
}

void MainWindow::showPageSetup()
{
    if (!m_project)
        return;
    PageSetupDialog dlg(m_project->docs()->pageSetup(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const PageSetup s = dlg.setup();
    m_project->docs()->setPageSetup(s);
    if (dlg.useForNewProjects()) {
        AppSettings::instance().newPageSetup =
            QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(s.toVariant())).toJson(QJsonDocument::Compact));
        AppSettings::instance().save();
        PageSetup::setStandard(s);
    }
}

void MainWindow::enterComposition()
{
    if (!m_project || m_composition)
        return;
    QList<BinderItem *> items = m_active->items();
    const bool scriv = m_active->mode() == ViewMode::Combined;
    if (items.isEmpty() || (m_active->mode() != ViewMode::Single && !scriv)) {
        BinderItem *it = m_active->currentItem();
        if (!it || !it->hasText())
            return;
        items = {it};
    }
    TextEditor *e = m_active->currentEditor();
    // File > Options > Behaviors > Composition Mode: the main window out of the way (0.21; minimized,
    // not hidden, so that the program does not end when composition mode is closed)
    const bool hideMain = AppSettings::instance().compositionHideMain && !isMinimized();
    const bool wasMaximized = isMaximized();
    m_composition = new CompositionWindow(m_project.get(), items, scriv, e ? e->item() : nullptr,
                                          e ? e->textCursor().position() : 0);
    if (hideMain)
        showMinimized();
    connect(m_composition, &CompositionWindow::closed, this, [this, hideMain, wasMaximized](BinderItem *it, int pos) {
        if (hideMain)
            wasMaximized ? showMaximized() : showNormal();
        activateWindow();
        if (!it)
            return;
        for (TextEditor *ed : m_active->textView()->editors()) {
            if (ed->item() == it) {
                ed->setFocus();
                QTextCursor c = ed->textCursor();
                c.setPosition(qBound(0, pos, ed->document()->characterCount() - 1));
                ed->setTextCursor(c);
                m_active->textView()->ensureCursorVisible(ed);
            }
        }
    });
    // Moving on to another document (Alt+Shift+Up / Down) or through the
    // history (Ctrl+[ / Ctrl+]) happens in the editor behind; composition mode
    // then shows what the editor shows, as long as that is text.
    auto follow = [this] {
        if (!m_composition || !m_active)
            return;
        QList<BinderItem *> texts;
        for (BinderItem *it : m_active->items())
            if (it->hasText())
                texts << it;
        if (!texts.isEmpty())
            m_composition->showItems(texts, m_active->mode() == ViewMode::Combined);
    };
    connect(m_composition, &CompositionWindow::navigate, this, [this, follow](bool next) {
        a[next ? QStringLiteral("nextDoc") : QStringLiteral("prevDoc")]->trigger();
        follow();
    });
    connect(m_composition, &CompositionWindow::historyRequested, this, [this, follow](bool back) {
        if (back)
            m_active->goBack();
        else
            m_active->goForward();
        follow();
    });
    shareShortcuts(m_composition);
}

void MainWindow::importFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import Files"), QDir::homePath(),
                                                            tr("All files (*);;") + importer::textFileFilters().first());
    if (files.isEmpty())
        return;
    int row = -1;
    BinderItem *parent = insertionPoint(&row);
    QStringList warnings;
    const auto items = importer::importFiles(m_project.get(), files, parent, row, &warnings);
    if (!warnings.isEmpty())
        QMessageBox::information(this, tr("Import Files"), warnings.join(QLatin1Char('\n')));
    if (!items.isEmpty())
        m_binder->select(items, true);
}

void MainWindow::backupNow(bool chooseLocation)
{
    QString dir = AppSettings::instance().backupDir;
    if (chooseLocation) {
        dir = QFileDialog::getExistingDirectory(this, tr("Back Up To"), QDir::homePath());
        if (dir.isEmpty())
            return;
    }
    QString out, err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = backup::backupProject(m_project.get(), dir, chooseLocation ? 0 : AppSettings::instance().backupKeep, &out, &err);
    QApplication::restoreOverrideCursor();
    if (ok && !err.isEmpty()) // saved, but some files could not be read (0.20)
        QMessageBox::warning(this, tr("Back Up"), tr("Backup saved:\n%1\n\n%2").arg(out, err));
    else if (ok)
        QMessageBox::information(this, tr("Back Up"), tr("Backup saved:\n%1").arg(out));
    else
        QMessageBox::warning(this, tr("Back Up"), err);
}

void MainWindow::convertFileTool()
{
    const QString in = QFileDialog::getOpenFileName(this, tr("Convert File — choose a Markdown or RTF file"), QDir::homePath(),
                                                    tr("Markdown or RTF (*.md *.markdown *.txt *.rtf)"));
    if (in.isEmpty())
        return;
    const bool toRtf = !in.endsWith(QLatin1String(".rtf"), Qt::CaseInsensitive);
    const QString suggested = QFileInfo(in).absolutePath() + QLatin1Char('/') + QFileInfo(in).completeBaseName()
                              + (toRtf ? QStringLiteral(".rtf") : QStringLiteral(".md"));
    const QString out = QFileDialog::getSaveFileName(this, tr("Save Converted File"), suggested,
                                                     toRtf ? tr("RTF (*.rtf)") : tr("Markdown (*.md)"));
    if (out.isEmpty())
        return;
    bool standard = false;
    if (!toRtf) {
        QMessageBox box(QMessageBox::Question, tr("Convert File"),
                        tr("Which kind of Markdown should be written?"), QMessageBox::NoButton, this);
        box.setInformativeText(tr("Standard Markdown separates paragraphs with empty lines; use it for other "
                                  "programs. %1 Markdown writes one paragraph per line, like the documents "
                                  "inside a project.")
                                   .arg(QLatin1String(QB_APP_NAME)));
        QPushButton *std = box.addButton(tr("Standard Markdown"), QMessageBox::AcceptRole);
        box.addButton(tr("%1 Markdown").arg(QLatin1String(QB_APP_NAME)), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == box.button(QMessageBox::Cancel))
            return;
        standard = box.clickedButton() == std;
    }
    QString err;
    const Formatting f = m_project ? m_project->formatting : Formatting();
    if (convertFile(in, out, f, &err, standard))
        QMessageBox::information(this, tr("Convert File"), tr("Saved\n%1").arg(out));
    else
        QMessageBox::warning(this, tr("Convert File"), err);
}

bool MainWindow::canPrint() const
{
    if (!m_project || !m_active)
        return false;
    switch (m_active->mode()) {
    case ViewMode::Single:
    case ViewMode::Combined: return !m_active->textView()->items().isEmpty();
    case ViewMode::Corkboard: return !m_active->corkboard()->shownItems().isEmpty();
    case ViewMode::Outliner: return !m_active->outliner()->listedRows(false).isEmpty();
    case ViewMode::Media: {
        const BinderItem *it = m_active->items().value(0);
        return it && (it->type == ItemType::Image || it->type == ItemType::Pdf || it->type == ItemType::WebPage);
    }
    case ViewMode::Empty: break;
    }
    return false;
}

void MainWindow::printDocument()
{
    if (!canPrint())
        return;
    const printing::Job job = printing::currentJob(m_active, editor());
    if (job.kind == printing::Kind::None)
        return;
    const PrintSettings s = PrintSettings::ofProject(m_project.get());
    QPrinter printer(QPrinter::HighResolution);
    printing::preparePrinter(&printer, job, s);
    QPrintDialog dlg(&printer, this);
    dlg.setWindowTitle(tr("Print %1").arg(job.title.isEmpty() ? printing::describe(job) : QStringLiteral("\u201c%1\u201d").arg(job.title)));
    dlg.setOption(QAbstractPrintDialog::PrintPageRange, true);
    dlg.setOption(QAbstractPrintDialog::PrintSelection, job.hasSelection());
    if (dlg.exec() != QDialog::Accepted)
        return;
    QString err;
    int pages = 0;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = printing::render(job, s, &printer, &err, &pages);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, tr("Print"), err.isEmpty() ? tr("Printing did not work.") : err);
        return;
    }
    statusBar()->showMessage(tr("Printed: %1 (%2)").arg(printing::describe(job), text::plural(pages, tr("page"), tr("pages"))), 6000);
}

void MainWindow::printPreview(const PrintSettings *settings)
{
    if (!canPrint())
        return;
    const printing::Job job = printing::currentJob(m_active, editor());
    if (job.kind == printing::Kind::None)
        return;
    const PrintSettings s = settings ? *settings : PrintSettings::ofProject(m_project.get());
    QPrinter printer(QPrinter::HighResolution);
    printing::preparePrinter(&printer, job, s);
    QPrintPreviewDialog dlg(&printer, this);
    dlg.setObjectName(QStringLiteral("printPreviewDialog"));
    dlg.setWindowTitle(tr("Print Preview \u2013 %1").arg(job.title.isEmpty() ? printing::describe(job) : job.title));
    QString err;
    connect(&dlg, &QPrintPreviewDialog::paintRequested, this, [&job, &s, &err](QPrinter *pr) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        if (!printing::render(job, s, pr, &err))
            QPainter(pr).drawText(QPointF(100, 300), err); // the reason, on the page
        QApplication::restoreOverrideCursor();
    });
    dlg.resize(qMin(1100, width() - 40), qMin(900, height() - 20));
    dlg.exec();
}

void MainWindow::showPrintSettings()
{
    if (!m_project)
        return;
    PrintSettings current = PrintSettings::ofProject(m_project.get());
    if (current.fontFamily.isEmpty()) {
        // "One font" starts with the project's font
        current.fontFamily = display::resolvedFontFamily(m_project->formatting);
        current.fontSize = m_project->formatting.fontSize;
    }
    PrintSettingsDialog dlg(current, m_project->docs()->pageSetup(), this);
    // open at the kind of printout the editor shows
    switch (m_active ? m_active->mode() : ViewMode::Empty) {
    case ViewMode::Single:
    case ViewMode::Combined: dlg.showTab(PrintSettingsDialog::TextTab); break;
    case ViewMode::Corkboard: dlg.showTab(PrintSettingsDialog::CardsTab); break;
    case ViewMode::Outliner: dlg.showTab(PrintSettingsDialog::OutlinerTab); break;
    default: dlg.showTab(PrintSettingsDialog::PageTab); break;
    }
    if (canPrint())
        dlg.setPreviewFunction([this](const PrintSettings &s) { printPreview(&s); });
    if (dlg.exec() != QDialog::Accepted)
        return;
    const PrintSettings s = dlg.settings();
    PrintSettings::storeInProject(m_project.get(), s);
    if (dlg.useForNewProjects()) {
        AppSettings::instance().newPrintSettings =
            QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(s.toVariant())).toJson(QJsonDocument::Compact));
        AppSettings::instance().save();
        PrintSettings::setStandardMap(s.toVariant());
    }
}

void MainWindow::installPackage(const QString &file)
{
    bool restart = false;
    if (!runInstallPackage(this, file, &restart))
        return;
    AppSettings::instance().notifyChanged(); // themes may have changed
    if (restart)
        restartApplication();
}

void MainWindow::showAddonManager()
{
    AddonManagerDialog dlg(this);
    connect(&dlg, &AddonManagerDialog::contentChanged, this, [] { AppSettings::instance().notifyChanged(); });
    const int r = dlg.exec();
    if (r == 2) { // an update was installed and the user chose to restart
        restartApplication();
    } else if (dlg.restartRequested()) {
        if (QMessageBox::question(this, tr("Add-on Manager"),
                                  tr("The changes take effect after a restart. Restart %1 now?").arg(QLatin1String(QB_APP_NAME)))
            == QMessageBox::Yes)
            restartApplication();
    }
}

void MainWindow::restartApplication()
{
    const QString projectFile = m_project ? m_project->displayFile() : QString();
    if (!closeProject())
        return;
    // closeProject saved everything and released the project; reopen it afterwards
    if (!projectFile.isEmpty())
        AppSettings::instance().addRecent(projectFile);
    AppSettings::instance().save();
    if (!relaunchApplication()) {
        QMessageBox::warning(this, tr("Restart"), tr("%1 could not be started again. Please start it yourself.")
                                                      .arg(QLatin1String(QB_APP_NAME)));
    }
    m_closing = true;
    close();
    QCoreApplication::quit();
}

void MainWindow::showStartNotice()
{
    const QString notice = addons::takeStartNotice();
    if (!notice.isEmpty())
        QMessageBox::warning(this, tr("Program Update"), notice);
}

void MainWindow::offerDesktopIntegration()
{
    const QString appimage = qEnvironmentVariable("APPIMAGE");
    if (appimage.isEmpty())
        return;
    QSettings s;
    const QString done = s.value(QStringLiteral("app/integrated")).toString();
    if (done == appimage) {
        integrateDesktop(true); // refresh silently (icon, mime type)
        return;
    }
    if (s.value(QStringLiteral("app/integrationDeclined")).toString() == appimage)
        return;
    const auto r = QMessageBox::question(
        this, tr("Application Menu"),
        tr("Add %1 to the application menu so you can start it like any other program and open "
           "projects by double-clicking them?\n\nYou can do this later with Help > Add to Application Menu.")
            .arg(QLatin1String(QB_APP_NAME)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (r == QMessageBox::Yes)
        integrateDesktop(false);
    else
        s.setValue(QStringLiteral("app/integrationDeclined"), appimage);
}

void MainWindow::integrateDesktop(bool quiet)
{
    const QString appimage = qEnvironmentVariable("APPIMAGE");
    const QString exec = appimage.isEmpty() ? QCoreApplication::applicationFilePath() : appimage;
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QDir().mkpath(data + QLatin1String("/applications"));
    QDir().mkpath(data + QLatin1String("/icons/hicolor/scalable/apps"));
    QDir().mkpath(data + QLatin1String("/mime/packages"));
    const QString icon = data + QLatin1String("/icons/hicolor/scalable/apps/" QB_APP_ID ".svg");
    QFile::remove(icon);
    QFile::copy(QStringLiteral(":/app/quillbinder.svg"), icon);
    QFile::setPermissions(icon, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadOther | QFileDevice::ReadGroup);
    // writing tools that run on their own (the scratch pad) become actions of the
    // menu entry; the desktop can give them a shortcut that works in every program
    QStringList actionIds;
    QString actionSections;
    for (const tools::Command &c : toolhost::host().allCommands()) {
        if (!c.standalone)
            continue;
        QString id = c.key;
        id.replace(QLatin1Char('.'), QLatin1Char('-'));
        QString name = c.text;
        name.remove(QLatin1Char('&'));
        if (name.endsWith(QChar(0x2026)))
            name.chop(1);
        actionIds << id;
        actionSections += QStringLiteral("\n[Desktop Action %1]\nName=%2\nExec=\"%3\" --tool %4\n").arg(id, name, exec, c.key);
    }
    QFile desktop(data + QLatin1String("/applications/" QB_APP_ID ".desktop"));
    if (desktop.open(QIODevice::WriteOnly)) {
        desktop.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=%1\nGenericName=Writing Studio\n"
                                     "Comment=Write, organize and compile long texts\nExec=\"%2\" %f\nIcon=%3\n"
                                     "Categories=Office;WordProcessor;\nMimeType=application/x-%3-project;application/x-%3-package;\nTerminal=false\n"
                                     "StartupWMClass=%3\n")
                          .arg(QLatin1String(QB_APP_NAME), exec, QLatin1String(QB_APP_ID))
                          .toUtf8());
        if (!actionIds.isEmpty())
            desktop.write((QStringLiteral("Actions=") + actionIds.join(QLatin1Char(';')) + QStringLiteral(";\n") + actionSections).toUtf8());
        desktop.close();
    }
    QFile mime(data + QLatin1String("/mime/packages/" QB_APP_ID ".xml"));
    if (mime.open(QIODevice::WriteOnly)) {
        mime.write(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                  "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
                                  "  <mime-type type=\"application/x-%1-project\">\n"
                                  "    <comment>%2 project</comment>\n"
                                  "    <icon name=\"%1\"/>\n"
                                  "    <glob pattern=\"*%3\"/>\n"
                                  "  </mime-type>\n"
                                  "  <mime-type type=\"application/x-%1-package\">\n"
                                  "    <comment>%2 update or add-on</comment>\n"
                                  "    <icon name=\"%1\"/>\n"
                                  "    <glob pattern=\"*.qbpkg\"/>\n"
                                  "  </mime-type>\n</mime-info>\n")
                       .arg(QLatin1String(QB_APP_ID), QLatin1String(QB_APP_NAME), QLatin1String(QB_PROJECT_FILE_SUFFIX))
                       .toUtf8());
        mime.close();
    }
    QProcess::startDetached(QStringLiteral("update-mime-database"), {data + QLatin1String("/mime")});
    QProcess::startDetached(QStringLiteral("update-desktop-database"), {data + QLatin1String("/applications")});
    QProcess::startDetached(QStringLiteral("kbuildsycoca5"), {});
    QProcess::startDetached(QStringLiteral("kbuildsycoca6"), {});
    QSettings().setValue(QStringLiteral("app/integrated"), exec);
    if (!quiet)
        QMessageBox::information(this, tr("Application Menu"),
                                 tr("%1 now appears in the application menu, and double-clicking a project file (*%2) "
                                    "opens it.\n\nIf you move the AppImage, choose this menu entry again.")
                                     .arg(QLatin1String(QB_APP_NAME), QLatin1String(QB_PROJECT_FILE_SUFFIX)));
}

// ------------------------------------------------------------------ options carried out here (0.21)

bool MainWindow::manualSave()
{
    if (!m_project)
        return false;
    QString err;
    if (!m_project->save(&err)) {
        QMessageBox::warning(this, tr("Save"), err);
        return false;
    }
    const AppSettings &s = AppSettings::instance();
    // File > Options > General > Saving: snapshots of the texts changed since the last File > Save
    if (s.snapshotOnManualSave) {
        int n = 0;
        for (const QString &id : editedSinceManualSave()) {
            BinderItem *it = m_project->item(id);
            if (it && it->hasText() && !m_project->isInTrash(it) && snapshots::take(m_project.get(), id, tr("Saved")))
                ++n;
        }
        if (n) {
            m_inspector->refreshAll();
            notifySnapshots(n);
        }
    }
    m_editedSinceSave.clear();
    // File > Options > Backup: a backup with each manual save
    if (s.backupOnManualSave) {
        QString out;
        if (!backup::backupProject(m_project.get(), s.backupDir, s.backupKeep, &out, &err))
            statusBar()->showMessage(tr("The backup could not be made: %1").arg(err), 8000);
    }
    return true;
}

QStringList MainWindow::editedSinceManualSave() const
{
    QStringList out(m_editedSinceSave.cbegin(), m_editedSinceSave.cend());
    out.sort();
    return out;
}

void MainWindow::notifySnapshots(int count)
{
    if (count > 0 && AppSettings::instance().snapshotNotify)
        statusBar()->showMessage(tr("Snapshot taken of %1.").arg(text::plural(count, tr("document"), tr("documents"))), 5000);
}

void MainWindow::noteInput(const QDateTime &when)
{
    m_lastInput = when;
}

bool MainWindow::autoQuitDue() const
{
    const AppSettings &s = AppSettings::instance();
    return s.autoQuit && !m_closing && m_lastInput.isValid()
           && m_lastInput.secsTo(QDateTime::currentDateTime()) >= qint64(qMax(1, s.autoQuitMinutes)) * 60
           && !QApplication::activeModalWidget();
}

} // namespace qb
