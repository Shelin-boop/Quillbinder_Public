// Scripted run for search and replace: project search and its options,
// find bar, filter bar of corkboard and outliner, Project Replace, Find by
// Formatting (milestone M8). Saves screenshots into the folder given as first argument.
// Exit code = number of failed checks.
//
//   QT_QPA_PLATFORM=offscreen ./qb_m8_tests /tmp/shots
//   xvfb-run -a -s "-screen 0 1920x1080x24" ./qb_m8_tests /tmp/shots
#include "compile/PluginManager.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "import/FileImporter.h"
#include "text/TextProps.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/BookmarkList.h"
#include "ui/Commands.h"
#include "ui/Copyholder.h"
#include "ui/Corkboard.h"
#include "ui/DragDrop.h"
#include "ui/EditorPane.h"
#include "ui/FilterBar.h"
#include "ui/SearchTools.h"
#include "core/Search.h"
#include "core/Snapshots.h"
#include "text/TextUtil.h"
#include "ui/Inspector.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/Outliner.h"
#include "ui/ProjectSettingsDialog.h"
#include "ui/QuickRef.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"
#include "ui/ToolDialogs.h"
#include "ui/Windows.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QInputDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardItemModel>
#include <QTabBar>
#include <QTreeView>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QWindow>
#include <algorithm>
#include <cstdio>

using namespace qb;

static int failures = 0;
static QString outDir;
static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail));
    }
    std::fflush(stdout);
}

static void settle(int ms = 150)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }
}

static void shot(QWidget *w, const QString &name)
{
    settle(400);
    w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}

static QAction *byKey(QWidget *w, const QString &key)
{
    QAction *a = w->findChild<QAction *>(key);
    if (!a)
        std::printf("      (action key not found: %s)\n", qPrintable(key));
    return a;
}

static QString menuText(QString t)
{
    const int tab = int(t.indexOf(QLatin1Char('\t')));
    if (tab >= 0)
        t.truncate(tab);
    t.remove(QLatin1Char('&'));
    return t;
}

static QAction *menuPath(QMainWindow *w, const QStringList &path)
{
    QList<QAction *> level = w->menuBar()->actions();
    QAction *found = nullptr;
    for (int i = 0; i < path.size(); ++i) {
        found = nullptr;
        for (QAction *a : level)
            if (menuText(a->text()) == path[i]) {
                found = a;
                break;
            }
        if (!found)
            return nullptr;
        if (i + 1 < path.size()) {
            if (!found->menu())
                return nullptr;
            emit found->menu()->aboutToShow();
            level = found->menu()->actions();
        }
    }
    return found;
}

static QStringList menuTexts(QMenu *menu)
{
    QStringList out;
    for (QAction *a : menu->actions())
        if (!a->isSeparator())
            out << menuText(a->text());
    return out;
}

static QList<QuickRefWindow *> quickRefs()
{
    QList<QuickRefWindow *> out;
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (auto *qr = qobject_cast<QuickRefWindow *>(tl))
            if (qr->isVisible())
                out << qr;
    return out;
}

static void closeQuickRefs()
{
    for (QuickRefWindow *qr : quickRefs())
        qr->close();
    settle(200);
}

// What a drag would do, sent as events: enter, move and (if the place takes it) drop.
// Returns whether the place accepted.
static bool dropMime(QWidget *target, const QPoint &pos, const QMimeData *mime, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    const Qt::DropActions actions = Qt::MoveAction | Qt::CopyAction | Qt::LinkAction;
    QDragEnterEvent enter(pos, actions, mime, Qt::LeftButton, mods);
    QApplication::sendEvent(target, &enter);
    QDragMoveEvent move(pos, actions, mime, Qt::LeftButton, mods);
    QApplication::sendEvent(target, &move);
    const bool ok = move.isAccepted();
    if (ok) {
        QDropEvent drop(QPointF(pos), actions, mime, Qt::LeftButton, mods);
        QApplication::sendEvent(target, &drop);
    } else {
        QDragLeaveEvent leave;
        QApplication::sendEvent(target, &leave);
    }
    settle(250);
    return ok;
}

static bool dropItems(QWidget *target, const QPoint &pos, Project *proj, const QList<BinderItem *> &items,
                      Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMimeData *mime = BinderModel::mimeForItems(items, proj);
    const bool ok = dropMime(target, pos, mime, mods);
    delete mime;
    return ok;
}

static bool dropKeywords(QWidget *target, const QPoint &pos, Project *proj, const QStringList &ids,
                         Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMimeData *mime = dnd::mimeForKeywords(proj, ids);
    const bool ok = dropMime(target, pos, mime, mods);
    delete mime;
    return ok;
}

// A drag with the mouse through Qt's own drag and drop, also from one window
// to another: press on the source, move, release on the target. The drag runs
// its own event loop inside the move that starts it, so the rest of the
// gesture is scheduled beforehand.
static void mouseDrag(QWidget *source, const QPoint &from, QWidget *target, const QPoint &to,
                      Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QWindow *win = source->window()->windowHandle();
    const QPoint ga = source->mapToGlobal(from), gb = target->mapToGlobal(to);
    auto local = [win](const QPoint &g) { return win->mapFromGlobal(g); };
    const QPoint a = local(ga), mid = local((ga + gb) / 2), b = local(gb);
    QTimer::singleShot(400, win, [=] { QTest::mouseEvent(QTest::MouseMove, win, Qt::LeftButton, mods, mid); });
    QTimer::singleShot(600, win, [=] { QTest::mouseEvent(QTest::MouseMove, win, Qt::LeftButton, mods, b - QPoint(3, 0)); });
    QTimer::singleShot(800, win, [=] { QTest::mouseEvent(QTest::MouseMove, win, Qt::LeftButton, mods, b); });
    QTimer::singleShot(1000, win, [=] { QTest::mouseEvent(QTest::MouseRelease, win, Qt::LeftButton, mods, b); });
    QTest::mouseEvent(QTest::MousePress, win, Qt::LeftButton, mods, a);
    QTest::mouseEvent(QTest::MouseMove, win, Qt::LeftButton, mods, a + QPoint(4, 6));
    QTest::mouseEvent(QTest::MouseMove, win, Qt::LeftButton, mods, a + QPoint(12, 18)); // (returns when the drag is over)
    settle(1300);
}

static QPoint center(QWidget *w) { return QPoint(w->width() / 2, w->height() / 2); }

static QString titles(const QList<BinderItem *> &items)
{
    QStringList t;
    for (BinderItem *it : items)
        t << it->title;
    return t.join(QLatin1String(", "));
}

static QString bookmarkTitles(Project *p)
{
    QStringList t;
    for (const Bookmark &b : p->projectBookmarks)
        t << (b.isInternal() && p->item(b.internalId()) ? p->item(b.internalId())->title : b.title);
    return t.join(QLatin1String(", "));
}

static bool hasImage(QTextDocument *doc)
{
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it)
            if (it.fragment().charFormat().isImageFormat())
                return true;
    return false;
}

static QString anchorOf(QTextDocument *doc, const QString &text)
{
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it)
            if (it.fragment().text().contains(text))
                return it.fragment().charFormat().anchorHref();
    return QStringLiteral("(text not found)");
}


// Do something with a window that opens modally (found by its class) while it is open.
template<typename T>
static void withModal(int delayMs, std::function<void(T *)> fn)
{
    QTimer::singleShot(delayMs, qApp, [fn] {
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *d = qobject_cast<T *>(tl))
                if (d->isVisible()) {
                    fn(d);
                    return;
                }
        std::printf("      (the modal window did not open)\n");
        if (QWidget *m = QApplication::activeModalWidget())
            m->close();
    });
}

static QString selectedText(TextEditor *e) { return e ? e->textCursor().selectedText() : QStringLiteral("(no editor)"); }

static QString listTitles(QListWidget *list)
{
    QStringList t;
    for (int i = 0; i < list->count(); ++i)
        t << list->item(i)->text();
    return t.join(QLatin1String(" | "));
}

static QAction *menuAction(QMenu *menu, const QString &text)
{
    for (QAction *a : menu->actions())
        if (menuText(a->text()) == text)
            return a;
    std::printf("      (menu entry not found: %s)\n", qPrintable(text));
    return nullptr;
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderM8Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderM8Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));

    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.startWithCorkboard = true;
    theme::apply();
    compile::PluginManager::instance().load();

    // ---------------------------------------------------------------- project
    QTemporaryDir projectsTmp;
    const QString projectsPath = argc > 2 ? QString::fromLocal8Bit(argv[2]) : projectsTmp.path();
    QDir().mkpath(projectsPath);
    templates::Info novel;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("novel"))
            novel = t;
    QString err;
    auto p = templates::createProject(novel, projectsPath, QStringLiteral("The Lighthouse"), &err);
    check(p != nullptr, QStringLiteral("create project"), err);
    if (!p)
        return 1;
    const QString projectFile = p->indexPath();

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1300, 950);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(p)), QStringLiteral("open project in main window"));
    Project *proj = w.project();
    settle(400);

    BinderModel *m = proj->model();
    m->moveToTrash(QList<BinderItem *>(proj->draft()->children)); // what the template brought
    settle(200);
    BinderItem *chapter = m->createItem(proj->draft(), 0, ItemType::Folder, QStringLiteral("The Keeper"));
    BinderItem *ships = m->createItem(chapter, 0, ItemType::Text, QStringLiteral("Counting Ships"));
    BinderItem *logbook = m->createItem(chapter, 1, ItemType::Text, QStringLiteral("The Logbook"));
    BinderItem *fog = m->createItem(chapter, 2, ItemType::Text, QStringLiteral("Morning Fog"));
    BinderItem *chapter2 = m->createItem(proj->draft(), 1, ItemType::Folder, QStringLiteral("The Storm"));
    BinderItem *wreck = m->createItem(chapter2, 0, ItemType::Text, QStringLiteral("The Wreck"));
    BinderItem *tides = m->createItem(proj->research(), 0, ItemType::Text, QStringLiteral("Tide Tables"));
    templates::setMarkdown(proj, ships,
                           QStringLiteral("He counted the ships from the gallery, **forty-one** of them.\nThe wind leaned on the glass. The keeper ==wrote the number down==.\n"));
    templates::setMarkdown(proj, logbook,
                           QStringLiteral("The keeper wrote the number in the margin.\nSee the tide tables for the day, {==the keeper said==}{>>Which keeper? Check the year<<}.\n"));
    templates::setMarkdown(proj, fog, QStringLiteral("The fog came in before dawn and stayed. ==No ships today==, the Keeper thought.\n"));
    templates::setMarkdown(proj, wreck, QStringLiteral("The hull lay on the rocks. <rev1>The keeper ran down the stairs.</rev1>\n"));
    templates::setMarkdown(proj, tides, QStringLiteral("High water 04:12 and 16:40.\nLow water 10:25 and 22:51.\n"));
    const QStringList ids = {chapter->id, ships->id, logbook->id, fog->id, chapter2->id, wreck->id, tides->id};
    ships->synopsis = QStringLiteral("The keeper counts ships at night.");
    logbook->synopsis = QStringLiteral("A number goes into the logbook at night.");
    fog->synopsis = QStringLiteral("Fog.");
    Label thread;
    thread.id = proj->nextLabelId();
    thread.name = QStringLiteral("Storm thread");
    thread.color = QColor(60, 110, 200);
    proj->labels << thread;
    Status todo;
    todo.id = proj->nextStatusId();
    todo.name = QStringLiteral("Needs Checking");
    proj->statuses << todo;
    fog->label = thread.id;
    wreck->label = thread.id;
    logbook->status = todo.id;
    tides->status = todo.id;
    proj->notifySettingsChanged();
    m->refreshAll();
    proj->save();
    settle(300);

    auto *binder = w.findChild<BinderPanel *>();
    auto *editorSplit = qobject_cast<QSplitter *>(w.findChild<EditorPane *>()->parentWidget());
    auto *pane0 = editorSplit ? qobject_cast<EditorPane *>(editorSplit->widget(0)) : nullptr;
    check(binder && pane0, QStringLiteral("window parts found"));
    if (!binder || !pane0)
        return 1;
    BinderTree *tree = binder->tree();
    tree->expandAll();
    QLineEdit *field = binder->searchField();
    ItemListWidget *results = binder->resultsList();
    auto firstEditor = [&]() -> TextEditor * { return pane0->textView()->editors().value(0); };
    auto search = [&](const QString &text) {
        field->setText(text);
        emit field->returnPressed();
        settle(200);
    };
    auto resultsTitle = [&]() {
        for (QLabel *l : binder->findChildren<QLabel *>())
            if (l->text().startsWith(QLatin1String("Search Results")))
                return l->text();
        return QString();
    };
    auto pick = [&](BinderItem *it) {
        for (int i = 0; i < results->count(); ++i)
            if (results->item(i)->data(Qt::UserRole).toString() == it->id) {
                results->setCurrentRow(i);
                settle(300);
                return true;
            }
        return false;
    };
    binder->select({ships}, true);
    settle(300);

    // ================================================================ menus
    {
        QStringList missing;
        const QList<QStringList> places = {{"Edit", "Find", "Find…"},
                                           {"Edit", "Find", "Find and Replace…"},
                                           {"Edit", "Find", "Find Next"},
                                           {"Edit", "Find", "Find Previous"},
                                           {"Edit", "Find", "Use Selection for Find"},
                                           {"Edit", "Find", "Project Search…"},
                                           {"Edit", "Find", "Project Replace…"},
                                           {"Edit", "Find", "Find by Formatting…"},
                                           {"Edit", "Find", "Find Next Formatting"},
                                           {"Edit", "Find", "Find Previous Formatting"}};
        for (const QStringList &pl : places)
            if (!menuPath(&w, pl))
                missing << pl.join(QLatin1String(" > "));
        check(missing.isEmpty(), QStringLiteral("Edit > Find has find, replace, project search, project replace and find by formatting"),
              missing.join(QLatin1String("; ")));
        auto *searchButton = w.findChild<QToolButton *>(QStringLiteral("searchButton"));
        const QStringList tools = searchButton && searchButton->menu() ? menuTexts(searchButton->menu()) : QStringList();
        check(tools.contains(QStringLiteral("Project Search…")) && tools.contains(QStringLiteral("Search in Document…"))
                  && tools.contains(QStringLiteral("Project Replace…")) && tools.contains(QStringLiteral("Find by Formatting…")),
              QStringLiteral("the arrow beside the toolbar's search button lists the search tools"), tools.join(QLatin1String(", ")));
        QHash<QString, QString> used;
        QStringList twice;
        for (QAction *a : w.findChildren<QAction *>()) {
            if (a->shortcut().isEmpty() || a->objectName().isEmpty() || a->property("qbAliasOf").isValid())
                continue;
            const QString ks = a->shortcut().toString();
            if (used.contains(ks) && used.value(ks) != a->objectName())
                twice << ks + QLatin1String(": ") + used.value(ks) + QLatin1String(" / ") + a->objectName();
            used.insert(ks, a->objectName());
        }
        check(twice.isEmpty() && byKey(&w, QStringLiteral("findFormat"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_F3)
                  && byKey(&w, QStringLiteral("findFormatNext"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_G)
                  && byKey(&w, QStringLiteral("useSelectionForFind"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_E)
                  && byKey(&w, QStringLiteral("projectSearch"))->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F),
              QStringLiteral("shortcuts of the new commands; no standard shortcut is used twice"), twice.join(QLatin1String("; ")));
        check(commands::find(QStringLiteral("projectReplace")) && commands::find(QStringLiteral("findFormat")),
              QStringLiteral("the new commands are in the list for shortcuts and toolbars"));
    }

    // ======================================================= project search
    {
        byKey(&w, QStringLiteral("projectSearch"))->trigger();
        settle(300);
        const bool shown = binder->isSearching() && field->isVisible() && field->hasFocus();
        search(QStringLiteral("keeper"));
        const QString found = listTitles(results);
        check(shown && binder->showsSearchResults() && results->count() == 5 && resultsTitle() == QLatin1String("Search Results (5)"),
              QStringLiteral("Project Search: the field appears above the binder and the results take the binder's place"),
              QStringLiteral("shown %1: %2 [%3]").arg(shown).arg(found, resultsTitle()));
        const bool picked = pick(logbook);
        TextEditor *e = firstEditor();
        const bool highlighted = e && e->item() == logbook && e->searchMatchCount() == 2 && e->extraSelections().size() == 2;
        check(picked && pane0->items() == QList<BinderItem *>({logbook}) && highlighted,
              QStringLiteral("a result opens in the editor with the matches highlighted"),
              QStringLiteral("matches %1").arg(e ? e->searchMatchCount() : -1));
        shot(&w, QStringLiteral("m8-01-project-search"));
        e->moveCursor(QTextCursor::Start);
        byKey(&w, QStringLiteral("findNext"))->trigger();
        settle(100);
        const QString first = selectedText(e);
        const int firstPos = e->textCursor().selectionStart();
        byKey(&w, QStringLiteral("findNext"))->trigger();
        settle(100);
        const int secondPos = e->textCursor().selectionStart();
        byKey(&w, QStringLiteral("findPrev"))->trigger();
        settle(100);
        check(first == QLatin1String("keeper") && secondPos > firstPos && e->textCursor().selectionStart() == firstPos,
              QStringLiteral("Find Next (F3) and Find Previous walk through the matches of the project search"),
              QStringLiteral("'%1' %2 %3").arg(first).arg(firstPos).arg(secondPos));
        byKey(&w, QStringLiteral("find"))->trigger();
        settle(200);
        auto *findField = pane0->findChild<QLineEdit *>(QStringLiteral("findField"));
        auto *findCount = pane0->findChild<QLabel *>(QStringLiteral("findCount"));
        check(pane0->findBarVisible() && findField->text() == QLatin1String("keeper") && findField->hasFocus()
                  && findCount->text() == QLatin1String("2 matches"),
              QStringLiteral("Find (Ctrl+F) opens with the term of the project search and says how many matches there are"),
              findField->text() + QLatin1String(" / ") + findCount->text());
        // another result: its matches are highlighted as well
        pick(fog);
        TextEditor *e2 = firstEditor();
        check(e2 && e2->item() == fog && e2->searchMatchCount() == 1, QStringLiteral("the next result is highlighted too (\"Keeper\": case does not matter)"),
              QStringLiteral("matches %1").arg(e2 ? e2->searchMatchCount() : -1));
    }

    // ============================================================== options
    {
        QMenu menu;
        binder->fillSearchMenu(&menu);
        const QStringList entries = menuTexts(&menu);
        QStringList missing;
        for (const QString &t : {QStringLiteral("All"), QStringLiteral("Title"), QStringLiteral("Text"), QStringLiteral("Synopsis"),
                                 QStringLiteral("Notes"), QStringLiteral("Keywords"), proj->labelTitle, proj->statusTitle,
                                 QStringLiteral("Custom Metadata"), QStringLiteral("Exact Phrase"), QStringLiteral("All Words"),
                                 QStringLiteral("Any Word"), QStringLiteral("Regular Expression"), QStringLiteral("Whole Words"),
                                 QStringLiteral("Case Sensitive"), QStringLiteral("Invert Results"), QStringLiteral("Search Draft Only"),
                                 QStringLiteral("Search Binder Selection Only"), QStringLiteral("Search \"Included\" Documents"),
                                 QStringLiteral("Search \"Excluded\" Documents"), QStringLiteral("Include Trash"),
                                 QStringLiteral("Save Search as Collection…")})
            if (!entries.contains(t))
                missing << t;
        check(missing.isEmpty() && menuAction(&menu, QStringLiteral("All"))->isChecked() && menuAction(&menu, QStringLiteral("All Words"))->isChecked(),
              QStringLiteral("the options menu of the search field: where to search, operators, options"), missing.join(QLatin1String(", ")));

        search(QStringLiteral("keeper"));
        binder->chooseSearchIn(&SearchQuery::inTitle, false);
        settle(100);
        const QString onlyTitles = listTitles(results);
        binder->chooseSearchIn(&SearchQuery::inSynopsis, true); // with Alt: one more place
        settle(100);
        const QString titlesAndSynopses = listTitles(results);
        QMenu menu2;
        binder->fillSearchMenu(&menu2);
        const bool ticks = !menuAction(&menu2, QStringLiteral("All"))->isChecked() && menuAction(&menu2, QStringLiteral("Title"))->isChecked()
                           && menuAction(&menu2, QStringLiteral("Synopsis"))->isChecked() && !menuAction(&menu2, QStringLiteral("Text"))->isChecked();
        menuAction(&menu2, QStringLiteral("All"))->trigger();
        settle(100);
        check(onlyTitles == QLatin1String("The Keeper") && titlesAndSynopses == QLatin1String("The Keeper | Counting Ships") && ticks
                  && results->count() == 5,
              QStringLiteral("Search In: one place by a click, several with Alt, All"), onlyTitles + QLatin1String(" / ") + titlesAndSynopses);

        // the tutorial's example: search for a status, then limit the search to the status
        search(QStringLiteral("Needs Checking"));
        const int everywhere = results->count();
        QMenu menu3;
        binder->fillSearchMenu(&menu3);
        menuAction(&menu3, proj->statusTitle)->trigger();
        settle(100);
        const QString byStatus = listTitles(results);
        check(everywhere == 2 && byStatus == QLatin1String("The Logbook | Tide Tables") && binder->searchQuery().inStatus
                  && !binder->searchQuery().inText,
              QStringLiteral("searching for a status lists the documents that have it"), byStatus);
        binder->chooseSearchIn(nullptr, false);

        search(QStringLiteral("fog rocks"));
        const int allWords = results->count();
        QMenu menu4;
        binder->fillSearchMenu(&menu4);
        menuAction(&menu4, QStringLiteral("Any Word"))->trigger();
        settle(100);
        const QString anyWord = listTitles(results);
        pick(wreck);
        const int wreckMatches = firstEditor() ? firstEditor()->searchMatchCount() : -1;
        QMenu menu5;
        binder->fillSearchMenu(&menu5);
        const bool anyTicked = menuAction(&menu5, QStringLiteral("Any Word"))->isChecked();
        menuAction(&menu5, QStringLiteral("Regular Expression"))->trigger();
        search(QStringLiteral("fort[yi]-\\w+|\\d\\d:\\d\\d"));
        const QString regex = listTitles(results);
        pick(tides);
        const int timeMatches = firstEditor() ? firstEditor()->searchMatchCount() : -1;
        search(QStringLiteral("(oops"));
        const bool mistake = results->count() == 0 && resultsTitle().contains(QLatin1String("mistake")) && !field->toolTip().isEmpty();
        QMenu menu6;
        binder->fillSearchMenu(&menu6);
        menuAction(&menu6, QStringLiteral("All Words"))->trigger();
        check(allWords == 0 && anyWord == QLatin1String("Morning Fog | The Wreck") && wreckMatches == 1 && anyTicked
                  && regex == QLatin1String("Counting Ships | Tide Tables") && timeMatches == 4 && mistake,
              QStringLiteral("operators: all words, any word, regular expression (a mistake in it is reported)"),
              QStringLiteral("%1 / %2 / %3 / matches %4 %5 mistake %6").arg(allWords).arg(anyWord, regex).arg(wreckMatches).arg(timeMatches).arg(mistake));

        // scope
        search(QStringLiteral("keeper"));
        tree->selectionModel()->select(m->indexFor(chapter2), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        QMenu menu7;
        binder->fillSearchMenu(&menu7);
        menuAction(&menu7, QStringLiteral("Search Binder Selection Only"))->trigger();
        settle(100);
        const QString inSelection = listTitles(results);
        QMenu menu8;
        binder->fillSearchMenu(&menu8);
        const bool selectionTicked = menuAction(&menu8, QStringLiteral("Search Binder Selection Only"))->isChecked();
        menuAction(&menu8, QStringLiteral("Search Binder Selection Only"))->trigger();
        search(QStringLiteral("water"));
        const QString water = listTitles(results);
        QMenu menu9;
        binder->fillSearchMenu(&menu9);
        menuAction(&menu9, QStringLiteral("Search Draft Only"))->trigger();
        settle(100);
        const int waterInDraft = results->count();
        menuAction(&menu9, QStringLiteral("Search Draft Only"))->trigger();
        search(QStringLiteral("keeper"));
        QMenu menu10;
        binder->fillSearchMenu(&menu10);
        menuAction(&menu10, QStringLiteral("Invert Results"))->trigger();
        settle(100);
        const QString inverted = listTitles(results);
        pick(tides);
        const int invertedMatches = firstEditor() ? firstEditor()->searchMatchCount() : -1;
        menuAction(&menu10, QStringLiteral("Invert Results"))->trigger();
        settle(100);
        check(inSelection == QLatin1String("The Wreck") && selectionTicked && water == QLatin1String("Tide Tables") && waterInDraft == 0
                  && inverted.contains(QLatin1String("Tide Tables")) && !inverted.contains(QLatin1String("Counting Ships")) && invertedMatches == 0
                  && results->count() == 5,
              QStringLiteral("options: binder selection only, Draft only, inverted results"),
              inSelection + QLatin1String(" / ") + water + QLatin1String(" / ") + inverted);
    }

    // ========================================== closing, tab, saved searches
    {
        search(QStringLiteral("keeper"));
        pick(logbook);
        auto *closeResults = binder->findChild<QToolButton *>(QStringLiteral("closeResults"));
        closeResults->click();
        settle(200);
        const bool back = !binder->showsSearchResults() && tree->isVisible() && binder->isSearching() && field->isVisible();
        const bool cleared = firstEditor() && firstEditor()->item() == logbook && firstEditor()->searchMatchCount() == 0
                             && !pane0->searchPattern().isValid();
        field->setFocus();
        byKey(&w, QStringLiteral("projectSearch"))->trigger();
        settle(200);
        check(back && cleared && !binder->isSearching(),
              QStringLiteral("the X of the results returns to the binder and takes the highlights away; the search button hides the field"),
              QStringLiteral("back %1 cleared %2 hidden %3").arg(back).arg(cleared).arg(!binder->isSearching()));
        // the editor kept the result; the binder still has its own selection: a click on it loads it
        const QList<BinderItem *> kept = pane0->items();
        const QList<BinderItem *> inBinder = binder->selectedItems();
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualRect(m->indexFor(chapter2)).center());
        settle(300);
        check(kept == QList<BinderItem *>({logbook}) && inBinder == QList<BinderItem *>({chapter2}) && pane0->items() == QList<BinderItem *>({chapter2}),
              QStringLiteral("after the results are closed the editor keeps its document; a click on the row selected in the binder loads that one"),
              titles(kept) + QLatin1String(" / ") + titles(inBinder) + QLatin1String(" / ") + titles(pane0->items()));

        byKey(&w, QStringLiteral("showCollections"))->setChecked(true);
        settle(200);
        CollectionTabs *tabs = binder->collectionTabs();
        const int last = tabs->count() - 1;
        const bool tabThere = tabs->isVisible() && tabs->tabText(0) == QLatin1String("Binder") && tabs->tabText(last) == QLatin1String("Search Results");
        tabs->setCurrentIndex(last);
        settle(300);
        const bool ranAgain = binder->showsSearchResults() && results->count() == 5 && field->text() == QLatin1String("keeper");
        pick(ships);
        const bool highlightedAgain = firstEditor() && firstEditor()->searchMatchCount() == 1;
        check(tabThere && ranAgain && highlightedAgain, QStringLiteral("the \"Search Results\" tab of the collections runs the last search again"),
              QStringLiteral("tab %1 ran %2 (%3) highlights %4").arg(tabThere).arg(ranAgain).arg(results->count()).arg(highlightedAgain));
        shot(&w, QStringLiteral("m8-02-search-results-tab"));

        // Save Search as Collection
        binder->showSearch(false);
        search(QStringLiteral("Needs Checking"));
        binder->chooseSearchIn(&SearchQuery::inStatus, false);
        settle(100);
        const int before = int(proj->collections.size());
        withModal<QInputDialog>(400, [](QInputDialog *d) {
            d->setTextValue(QStringLiteral("To Check"));
            d->accept();
        });
        QMenu menu;
        binder->fillSearchMenu(&menu);
        menuAction(&menu, QStringLiteral("Save Search as Collection…"))->trigger();
        settle(400);
        const Collection saved = proj->collections.value(proj->collections.size() - 1);
        const bool savedOk = proj->collections.size() == before + 1 && saved.isSearch && saved.name == QLatin1String("To Check")
                             && saved.query.inStatus && !saved.query.inText && binder->currentCollectionId() == saved.id
                             && listTitles(results) == QLatin1String("The Logbook | Tide Tables");
        // a document gets the status: the collection finds it the next time it is opened
        ships->status = todo.id;
        proj->notifyItemChanged(ships);
        tabs->setCurrentIndex(0);
        settle(200);
        binder->showCollection(saved.id);
        settle(200);
        check(savedOk && listTitles(results) == QLatin1String("Counting Ships | The Logbook | Tide Tables")
                  && tabs->tabText(tabs->count() - 1) == QLatin1String("Search Results"),
              QStringLiteral("Save Search as Collection keeps the search with its options; it runs again each time it is opened"),
              QStringLiteral("saved %1: %2").arg(savedOk).arg(listTitles(results)));
        ships->status = -1;
        proj->notifyItemChanged(ships);
        tabs->setCurrentIndex(0);
        binder->chooseSearchIn(nullptr, false);
        binder->hideSearch();
        settle(200);
    }

    // ========================================================= quick search
    {
        auto *quick = w.findChild<QLineEdit *>(QStringLiteral("quickSearch"));
        quick->setFocus();
        QTest::keyClicks(quick, QStringLiteral("keeper"));
        settle(600);
        QCompleter *completer = quick->completer();
        QStringList rows;
        int headers = 0;
        for (int i = 0; completer && i < completer->model()->rowCount(); ++i) {
            const QModelIndex idx = completer->model()->index(i, 0);
            const bool header = !(completer->model()->flags(idx) & Qt::ItemIsSelectable);
            headers += header;
            rows << (header ? QStringLiteral("[") + idx.data().toString() + QLatin1Char(']') : idx.data().toString());
        }
        const QString list = rows.join(QLatin1String(" | "));
        check(list == QLatin1String("[Titles] | The Keeper | [Synopses] | Counting Ships | [Text] | The Logbook | Morning Fog | The Wreck")
                  && headers == 3 && completer->popup()->isVisible(),
              QStringLiteral("quick search lists titles, synopses and text under headings"), list);
        shot(&w, QStringLiteral("m8-03-quick-search"));
        completer->popup()->hide();
        quick->clear();
        settle(200);
    }

    // ======================================================== find and replace
    {
        w.activateWindow(); // (the list of the quick search was the active window)
        (void)QTest::qWaitForWindowActive(&w, 2000);
        binder->select({logbook}, true);
        settle(300);
        TextEditor *e = firstEditor();
        e->setFocus();
        byKey(&w, QStringLiteral("find"))->trigger();
        settle(200);
        auto *findField = pane0->findChild<QLineEdit *>(QStringLiteral("findField"));
        auto *replaceField = pane0->findChild<QLineEdit *>(QStringLiteral("replaceField"));
        auto *findCount = pane0->findChild<QLabel *>(QStringLiteral("findCount"));
        QCheckBox *matchCase = nullptr, *wholeWords = nullptr;
        for (QCheckBox *c : pane0->findChildren<QCheckBox *>()) {
            if (c->text() == QLatin1String("Match case"))
                matchCase = c;
            if (c->text() == QLatin1String("Whole words"))
                wholeWords = c;
        }
        findField->setText(QStringLiteral("The"));
        settle(100);
        const QString loose = findCount->text();
        const int looseCount = e->searchMatchCount();
        matchCase->setChecked(true);
        settle(100);
        const QString cased = findCount->text();
        matchCase->setChecked(false);
        findField->setText(QStringLiteral("table"));
        const QString part = findCount->text();
        wholeWords->setChecked(true);
        settle(100);
        const QString whole = part + QLatin1String(", ") + findCount->text();
        wholeWords->setChecked(false);
        check(loose == QLatin1String("6 matches") && cased == QLatin1String("1 match") && whole == QLatin1String("1 match, 0 matches")
                  && looseCount == 6 && !replaceField->isVisible(),
              QStringLiteral("find bar: the number of matches follows the text, Match case and Whole words"),
              loose + QLatin1String(" / ") + cased + QLatin1String(" / ") + whole);

        byKey(&w, QStringLiteral("replace"))->trigger();
        settle(200);
        findField->setText(QStringLiteral("number"));
        replaceField->setText(QStringLiteral("figure"));
        e->moveCursor(QTextCursor::Start);
        auto *one = pane0->findChild<QPushButton *>(QStringLiteral("replaceOne"));
        auto *all = pane0->findChild<QPushButton *>(QStringLiteral("replaceAll"));
        one->click();
        settle(100);
        const QString selectedFirst = selectedText(e);
        one->click();
        settle(100);
        const bool replacedOne = e->toPlainText().contains(QLatin1String("wrote the figure in the margin"));
        findField->setText(QStringLiteral("keeper"));
        replaceField->setText(QStringLiteral("warden"));
        all->click();
        settle(200);
        const QString after = e->toPlainText();
        const QString said = findCount->text();
        e->document()->undo();
        const QString undone = e->toPlainText();
        check(replaceField->isVisible() && selectedFirst == QLatin1String("number") && replacedOne
                  && after.contains(QLatin1String("The warden wrote")) && after.contains(QLatin1String("the warden said")) && said == QLatin1String("2 replacements")
                  && undone.contains(QLatin1String("The keeper wrote")) && undone.contains(QLatin1String("the keeper said")),
              QStringLiteral("Find and Replace: Replace, Replace All (one step for Undo)"), after + QLatin1String(" / ") + said);
        // the comment is still attached to its words
        check(e->document()->toPlainText().contains(QLatin1String("the keeper said")) && !e->noteIdAt(e->document()->toPlainText().indexOf(QLatin1String("keeper said")) + 2).isEmpty(),
              QStringLiteral("replaced text keeps what was attached to it (the comment)"));

        QTextCursor c = e->textCursor();
        c.setPosition(int(e->toPlainText().indexOf(QLatin1String("margin"))));
        c.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
        e->setTextCursor(c);
        byKey(&w, QStringLiteral("useSelectionForFind"))->trigger();
        settle(100);
        check(findField->text() == QLatin1String("margin") && findCount->text() == QLatin1String("1 match"),
              QStringLiteral("Use Selection for Find puts the selected text into the find bar"), findField->text());
        shot(&w, QStringLiteral("m8-04-find-replace"));

        // copyholder
        pane0->openCopyholder(fog);
        settle(300);
        TextEditor *holder = pane0->copyholder()->editor();
        holder->setFocus();
        settle(100);
        byKey(&w, QStringLiteral("find"))->trigger();
        settle(200);
        findField->setText(QStringLiteral("fog"));
        settle(100);
        const bool inHolder = holder->searchMatchCount() == 1 && findField->placeholderText().contains(QLatin1String("copyholder"));
        const int mainMatches = firstEditor()->searchMatchCount();
        byKey(&w, QStringLiteral("findNext"))->trigger();
        settle(100);
        const QString holderSelection = selectedText(holder);
        for (QToolButton *b : pane0->findChildren<QToolButton *>())
            if (b->toolTip() == QLatin1String("Close") && b->parentWidget() == findField->parentWidget())
                b->click();
        settle(200);
        check(inHolder && holderSelection == QLatin1String("fog") && holder->searchMatchCount() == 0 && !pane0->findBarVisible(),
              QStringLiteral("Find works in the copyholder while the keyboard is in it"),
              QStringLiteral("in holder %1 (%2 matches, main %3) selected '%4'").arg(inHolder).arg(holder->searchMatchCount()).arg(mainMatches).arg(holderSelection));
        pane0->closeCopyholder();
        settle(200);
    }

    // =========================================================== filter bar
    {
        binder->select({chapter}, true);
        settle(400);
        pane0->setViewMode(ViewMode::Corkboard);
        settle(300);
        Corkboard *cork = pane0->corkboard();
        auto cards = [&]() {
            QStringList t;
            for (QGraphicsItem *gi : cork->scene()->items(Qt::AscendingOrder))
                if (auto *c = dynamic_cast<CardItem *>(gi))
                    t << c->item()->title;
            t.sort();
            return t.join(QLatin1String(" | "));
        };
        const QString allCards = cards();
        cork->setFocus();
        byKey(&w, QStringLiteral("find"))->trigger();
        settle(200);
        FilterBar *bar = pane0->filterBar();
        auto *count = bar->findChild<QLabel *>(QStringLiteral("filterCount"));
        const bool opened = pane0->filterBarVisible() && bar->field()->hasFocus() && !pane0->findBarVisible() && !bar->optionsVisible();
        bar->field()->setText(QStringLiteral("night"));
        settle(200);
        const QString nightCards = cards();
        check(opened && allCards == QLatin1String("Counting Ships | Morning Fog | The Logbook") && nightCards == QLatin1String("Counting Ships | The Logbook")
                  && count->text() == QLatin1String("2 of 3"),
              QStringLiteral("Find on the corkboard opens the filter bar; typing leaves the cards that match"),
              QStringLiteral("opened %1: %2 [%3]").arg(opened).arg(nightCards, count->text()));
        shot(&w, QStringLiteral("m8-05-filter-corkboard"));

        // a card dropped behind the last card shown lands behind that document, not at "place 2"
        {
            CardItem *lastCard = nullptr;
            for (QGraphicsItem *gi : cork->scene()->items())
                if (auto *c = dynamic_cast<CardItem *>(gi))
                    if (c->item() == logbook)
                        lastCard = c;
            const QRectF r = lastCard ? lastCard->sceneBoundingRect() : QRectF();
            const QPoint at = cork->mapFromScene(QPointF(r.right() + 6, r.center().y()));
            bar->field()->setText(QStringLiteral("logbook"));
            settle(200);
            const bool took = dropItems(cork->viewport(), at, proj, {ships});
            settle(200);
            const QString order = titles(chapter->children);
            check(took && order == QLatin1String("The Logbook, Counting Ships, Morning Fog"),
                  QStringLiteral("with a filter, a dropped card goes to the right place among all documents of the group"), order);
            m->moveItems({ships}, chapter, 0);
            settle(200);
            bar->field()->setText(QStringLiteral("night"));
            settle(200);
        }

        auto *optionsButton = bar->findChild<QToolButton *>(QStringLiteral("filterOptionsButton"));
        optionsButton->click();
        settle(200);
        QCheckBox *inSynopses = nullptr, *inText = nullptr, *invert = nullptr;
        for (QCheckBox *c : bar->findChildren<QCheckBox *>()) {
            if (c->text() == QLatin1String("Synopses"))
                inSynopses = c;
            if (c->text() == QLatin1String("Text"))
                inText = c;
            if (c->text() == QLatin1String("Show what does not match"))
                invert = c;
        }
        const bool optionsShown = bar->optionsVisible() && inSynopses && inSynopses->isVisible();
        inSynopses->setChecked(false);
        settle(200);
        const QString titlesOnly = cards() + QLatin1String(" [") + count->text() + QLatin1Char(']');
        inSynopses->setChecked(true);
        bar->field()->setText(QStringLiteral("dawn"));
        inText->setChecked(true);
        settle(200);
        const QString byText = cards();
        inText->setChecked(false);
        bar->field()->clear();
        auto *labelBox = bar->findChild<QComboBox *>(QStringLiteral("filterLabel"));
        auto *statusBox = bar->findChild<QComboBox *>(QStringLiteral("filterStatus"));
        labelBox->setCurrentIndex(labelBox->findText(QStringLiteral("Storm thread")));
        settle(200);
        const QString byLabel = cards();
        invert->setChecked(true);
        settle(200);
        const QString notLabel = cards();
        invert->setChecked(false);
        labelBox->setCurrentIndex(0);
        statusBox->setCurrentIndex(statusBox->findText(QStringLiteral("Needs Checking")));
        settle(200);
        const QString byStatus = cards();
        statusBox->setCurrentIndex(0);
        settle(200);
        check(optionsShown && titlesOnly == QLatin1String(" [0 of 3]") && byText == QLatin1String("Morning Fog") && byLabel == QLatin1String("Morning Fog")
                  && notLabel == QLatin1String("Counting Ships | The Logbook") && byStatus == QLatin1String("The Logbook") && cards() == allCards
                  && labelBox->count() >= 3,
              QStringLiteral("filter options behind the triangle: where to look, label, status, what does not match"),
              titlesOnly + QLatin1String(" / ") + byText + QLatin1String(" / ") + byLabel + QLatin1String(" / ") + notLabel + QLatin1String(" / ") + byStatus);
        shot(&w, QStringLiteral("m8-06-filter-options"));

        // outliner
        bar->field()->setText(QStringLiteral("night"));
        pane0->setViewMode(ViewMode::Outliner);
        settle(300);
        Outliner *outliner = pane0->outliner();
        auto rows = [&]() {
            QStringList t;
            const QAbstractItemModel *mod = outliner->model();
            std::function<void(const QModelIndex &)> walk = [&](const QModelIndex &parent) {
                for (int i = 0; i < mod->rowCount(parent); ++i) {
                    const QModelIndex idx = mod->index(i, 0, parent);
                    t << idx.data().toString().section(QLatin1Char('\n'), 0, 0);
                    walk(idx);
                }
            };
            walk(outliner->rootIndex());
            return t.join(QLatin1String(" | "));
        };
        const QString outlinerNight = rows();
        const bool barStays = pane0->filterBarVisible() && count->text() == QLatin1String("2 of 3");
        bar->field()->setText(QStringLiteral("zzz"));
        settle(200);
        const QString none = rows();
        const bool rootKept = outliner->rootIndex().isValid() && outliner->rootIndex().data().toString().startsWith(QLatin1String("The Keeper"));
        check(barStays && outlinerNight == QLatin1String("Counting Ships | The Logbook") && none.isEmpty() && rootKept && count->text() == QLatin1String("0 of 3"),
              QStringLiteral("the filter also works in the outliner; with no match the list is empty (not the whole binder)"),
              outlinerNight + QLatin1String(" / '") + none + QLatin1String("' root ") + outliner->rootIndex().data().toString());
        // groups of matches stay
        binder->select({proj->draft()}, true);
        settle(300);
        pane0->setViewMode(ViewMode::Outliner);
        bar->field()->setText(QStringLiteral("wreck"));
        settle(300);
        const QString nested = rows();
        check(nested == QLatin1String("The Storm | The Wreck") && pane0->filterBarVisible(),
              QStringLiteral("in the outliner, the group of a match stays in the list; the filter stays while other groups are opened"), nested);
        shot(&w, QStringLiteral("m8-07-filter-outliner"));

        // text has no filter bar; it comes back with the corkboard
        binder->select({ships}, true);
        settle(300);
        const bool hiddenForText = !pane0->filterBarVisible();
        binder->select({chapter}, true);
        settle(300);
        pane0->setViewMode(ViewMode::Corkboard);
        bar->field()->setText(QStringLiteral("fog"));
        settle(300);
        const bool backWithCards = pane0->filterBarVisible() && cards() == QLatin1String("Morning Fog");
        auto *doneButton = bar->findChild<QPushButton *>(QStringLiteral("filterDone"));
        doneButton->click();
        settle(300);
        check(hiddenForText && backWithCards && !pane0->filterBarVisible() && cards() == allCards && bar->field()->text().isEmpty(),
              QStringLiteral("the bar hides while text is shown; Done removes the filter"),
              QStringLiteral("hidden %1 back %2 cards %3").arg(hiddenForText).arg(backWithCards).arg(cards()));
        pane0->setViewMode(ViewMode::Outliner);
        settle(200);
        check(rows() == QLatin1String("Counting Ships | The Logbook | Morning Fog"), QStringLiteral("after Done the outliner lists everything again"), rows());
        pane0->setViewMode(ViewMode::Corkboard);
        settle(200);
    }

    // ====================================================== project replace
    {
        // reopen: only what the editor shows is in memory, the other texts are in their files
        binder->select({ships}, true);
        settle(300);
        pane0->setViewMode(ViewMode::Single);
        w.closeProject();
        settle(300);
        check(w.openProject(projectFile), QStringLiteral("reopen the project (documents closed)"));
        settle(800);
        proj = w.project();
        m = proj->model();
        chapter = proj->item(ids[0]);
        ships = proj->item(ids[1]);
        logbook = proj->item(ids[2]);
        fog = proj->item(ids[3]);
        chapter2 = proj->item(ids[4]);
        wreck = proj->item(ids[5]);
        tides = proj->item(ids[6]);
        tree->expandAll();
        binder->select({ships}, true);
        settle(300);
        TextEditor *e = firstEditor();
        auto fileOf = [&](BinderItem *it) {
            QFile f(proj->docs()->textPath(it->id));
            f.open(QIODevice::ReadOnly);
            return QString::fromUtf8(f.readAll());
        };
        const bool wreckClosed = proj->docs()->loadedText(wreck->id) == nullptr;
        ReplaceResult counted, done;
        QString countText, doneText, selectionLabel;
        bool buttonsOff = false;
        withModal<ProjectReplaceDialog>(500, [&](ProjectReplaceDialog *d) {
            auto *replaceButton = d->findChild<QPushButton *>(QStringLiteral("replaceAllButton"));
            buttonsOff = replaceButton && !replaceButton->isEnabled();
            for (QRadioButton *r : d->findChildren<QRadioButton *>())
                if (r->text().startsWith(QLatin1String("Selected")))
                    selectionLabel = menuText(r->text());
            ReplaceOptions o;
            o.find = QStringLiteral("keeper");
            o.with = QStringLiteral("warden");
            o.snapshotFirst = true;
            d->setOptions(o, false);
            counted = d->count();
            countText = d->resultText();
            d->grab().save(outDir + QLatin1String("/m8-08-project-replace.png"));
            done = d->replaceAll(false);
            doneText = d->resultText();
            d->reject();
        });
        byKey(&w, QStringLiteral("projectReplace"))->trigger();
        settle(400);
        check(buttonsOff && selectionLabel.contains(QLatin1String("(1,")) && counted.total() == 8 && counted.texts == 5 && counted.titles == 1
                  && counted.synopses == 1 && counted.comments == 1 && countText.startsWith(QLatin1String("8 occurrences found in 5 documents")),
              QStringLiteral("Edit > Find > Project Replace: the window counts what it would change"),
              countText + QLatin1String(" [") + selectionLabel + QLatin1Char(']'));
        const QString wreckFile = fileOf(wreck);
        const QString logbookFile = fileOf(logbook);
        check(done.total() == 8 && doneText.contains(QLatin1String("replaced")) && chapter->title == QLatin1String("The warden")
                  && ships->synopsis == QLatin1String("The warden counts ships at night.")
                  && e->toPlainText().contains(QLatin1String("The warden wrote the number down"))
                  && wreckFile.contains(QLatin1String("<rev1>The warden ran down the stairs.</rev1>"))
                  && logbookFile.contains(QLatin1String("{==the warden said==}{>>Which warden? Check the year<<}"))
                  && fileOf(fog).contains(QLatin1String("the warden thought")),
              QStringLiteral("Replace All changes titles, synopses, the open text, the texts in files and comment texts; formatting stays"),
              doneText + QLatin1String(" / ") + chapter->title + QLatin1String(" / ") + wreckFile + logbookFile);
        const QModelIndex chapterIndex = m->indexFor(chapter);
        check(wreckClosed && proj->docs()->loadedText(wreck->id) == nullptr && chapterIndex.data().toString() == QLatin1String("The warden") && done.snapshots == 4
                  && snapshots::list(proj, wreck->id).size() == 1 && wreck->hasSnapshots,
              QStringLiteral("the binder shows the new title; every changed text got a snapshot first"),
              QStringLiteral("closed %1 title '%2' snapshots %3").arg(wreckClosed).arg(chapterIndex.data().toString()).arg(done.snapshots));
        // "Keeper" at the start of a sentence: case-sensitive replacement in the selected documents only
        binder->select({chapter2}, false);
        settle(100);
        ReplaceResult scoped;
        bool selectionOffered = false;
        QString mistakeText;
        withModal<ProjectReplaceDialog>(500, [&](ProjectReplaceDialog *d) {
            for (QRadioButton *r : d->findChildren<QRadioButton *>())
                if (r->text().startsWith(QLatin1String("Selected")))
                    selectionOffered = r->isEnabled();
            ReplaceOptions bad;
            bad.find = QStringLiteral("(oops");
            bad.regex = true;
            d->setOptions(bad, false);
            d->count();
            mistakeText = d->resultText();
            ReplaceOptions o;
            o.find = QStringLiteral("warden");
            o.with = QStringLiteral("keeper");
            o.caseSensitive = true;
            d->setOptions(o, true);
            scoped = d->replaceAll(false);
            d->reject();
        });
        byKey(&w, QStringLiteral("projectReplace"))->trigger();
        settle(400);
        check(selectionOffered && scoped.total() == 1 && scoped.documents == 1 && fileOf(wreck).contains(QLatin1String("The keeper ran"))
                  && fileOf(fog).contains(QLatin1String("the warden thought")) && chapter->title == QLatin1String("The warden")
                  && mistakeText.contains(QLatin1String("regular expression")),
              QStringLiteral("Project Replace in the selected documents only; a bad regular expression is reported"),
              QStringLiteral("%1 in %2: %3").arg(scoped.total()).arg(scoped.documents).arg(mistakeText));
        // back to "keeper" everywhere for the rest of the run
        ReplaceOptions undo;
        undo.find = QStringLiteral("warden");
        undo.with = QStringLiteral("keeper");
        const ReplaceResult r2 = replace::run(proj, undo, false);
        chapter->title = QStringLiteral("The Keeper");
        fog->title = QStringLiteral("Morning Fog");
        proj->notifyItemChanged(chapter);
        settle(200);
        check(r2.total() == 7 && e->toPlainText().contains(QLatin1String("The keeper wrote the number down")),
              QStringLiteral("(replaced back)"), QString::number(r2.total()));
    }

    // =================================================== find by formatting
    {
        binder->select({logbook}, true);
        settle(300);
        firstEditor()->moveCursor(QTextCursor::Start);
        byKey(&w, QStringLiteral("findFormat"))->trigger();
        settle(300);
        auto *win = w.findChild<FormatFindWindow *>();
        check(win && win->isVisible() && win->query().kind == fmtfind::Kind::Highlight && win->allDocuments(),
              QStringLiteral("Edit > Find > Find by Formatting opens its window"));
        if (win) {
            auto *kind = win->findChild<QComboBox *>(QStringLiteral("formatKind"));
            QStringList kinds;
            for (int i = 0; i < kind->count(); ++i)
                kinds << kind->itemText(i);
            auto *next = win->findChild<QPushButton *>(QStringLiteral("formatNext"));
            auto *prev = win->findChild<QPushButton *>(QStringLiteral("formatPrevious"));
            // the logbook has no highlight: the search goes on into the next document that has one
            next->click();
            settle(400);
            const bool wentOn = pane0->items() == QList<BinderItem *>({fog}) && selectedText(firstEditor()) == QLatin1String("No ships today")
                                && binder->selectedItems() == QList<BinderItem *>({fog}) && win->status().contains(QLatin1String("Morning Fog"));
            next->click(); // nothing after it: around to the first document
            settle(400);
            const bool around = pane0->items() == QList<BinderItem *>({ships}) && selectedText(firstEditor()) == QLatin1String("wrote the number down");
            prev->click();
            settle(400);
            const bool backAgain = pane0->items() == QList<BinderItem *>({fog}) && selectedText(firstEditor()) == QLatin1String("No ships today");
            check(kinds.size() == 10 && kinds.contains(QStringLiteral("Highlighted Text")) && kinds.contains(QStringLiteral("Comments and Footnotes"))
                      && kinds.contains(QStringLiteral("Revision Color")) && kinds.contains(QStringLiteral("Character Format")) && wentOn && around && backAgain,
                  QStringLiteral("Next and Previous select the highlighted text and go on through the documents of the project"),
                  QStringLiteral("on %1 around %2 back %3: '%4' in %5").arg(wentOn).arg(around).arg(backAgain).arg(selectedText(firstEditor()), titles(pane0->items())));
            shot(&w, QStringLiteral("m8-09-find-by-formatting"));
            win->grab().save(outDir + QLatin1String("/m8-10-format-window.png"));

            win->setAllDocuments(false);
            next->click();
            settle(300);
            const bool stays = pane0->items() == QList<BinderItem *>({fog}) && selectedText(firstEditor()) == QLatin1String("No ships today")
                               && win->status().contains(QLatin1String("beginning"));
            fmtfind::Query q;
            q.kind = fmtfind::Kind::Revision;
            win->setQuery(q);
            next->click();
            settle(300);
            const bool notHere = win->status() == QLatin1String("Not found in this editor.") && pane0->items() == QList<BinderItem *>({fog});
            win->setAllDocuments(true);
            next->click();
            settle(400);
            const bool revision = pane0->items() == QList<BinderItem *>({wreck}) && selectedText(firstEditor()) == QLatin1String("The keeper ran down the stairs.");
            check(stays && notHere && revision,
                  QStringLiteral("\"The Current Editor Only\" stays in the document; revision colour is found in another document"),
                  QStringLiteral("stays %1 not here %2 revision %3 [%4]").arg(stays).arg(notHere).arg(revision).arg(win->status()));

            q = fmtfind::Query();
            q.kind = fmtfind::Kind::CommentOrFootnote;
            q.containing = QStringLiteral("year");
            win->setQuery(q);
            byKey(&w, QStringLiteral("findFormatNext"))->trigger(); // Ctrl+G
            settle(400);
            const bool comment = pane0->items() == QList<BinderItem *>({logbook}) && selectedText(firstEditor()) == QLatin1String("the keeper said");
            q = fmtfind::Query();
            q.kind = fmtfind::Kind::CharFormat;
            win->setQuery(q);
            byKey(&w, QStringLiteral("findFormatPrev"))->trigger();
            settle(400);
            const bool bold = pane0->items() == QList<BinderItem *>({ships}) && selectedText(firstEditor()) == QLatin1String("forty-one");
            q.kind = fmtfind::Kind::Style;
            q.style = QStringLiteral("Attribution"); // a style no text uses
            win->setQuery(q);
            auto *styleBox = win->findChild<QComboBox *>(QStringLiteral("formatStyle"));
            bool hasHeading = false;
            for (int i = 0; i < styleBox->count(); ++i)
                hasHeading |= styleBox->itemData(i).toString() == QLatin1String("Heading 1");
            next->click();
            settle(400);
            const bool noStyle = win->status() == QLatin1String("Not found.");
            check(comment && bold && styleBox->isVisible() && styleBox->count() > 10 && hasHeading && noStyle,
                  QStringLiteral("comments by their text (Find Next Formatting, Ctrl+G), bold text, styles of the project; \"Not found\" when nothing has it"),
                  QStringLiteral("comment %1 bold %2 styles %3 status '%4'").arg(comment).arg(bold).arg(styleBox->count()).arg(win->status()));

            // Combined text: through the documents shown without leaving the view
            binder->select({chapter}, true);
            settle(300);
            pane0->setViewMode(ViewMode::Combined);
            settle(400);
            q = fmtfind::Query();
            win->setQuery(q);
            const QList<TextEditor *> eds = pane0->textView()->editors();
            if (!eds.isEmpty()) {
                eds.first()->setFocus();
                eds.first()->moveCursor(QTextCursor::Start);
            }
            settle(100);
            next->click();
            settle(300);
            TextEditor *firstHit = nullptr, *secondHit = nullptr;
            for (TextEditor *x : pane0->textView()->editors())
                if (x->textCursor().hasSelection() && x->textCursor().selectedText() == QLatin1String("wrote the number down"))
                    firstHit = x;
            next->click();
            settle(300);
            for (TextEditor *x : pane0->textView()->editors())
                if (x->textCursor().selectedText() == QLatin1String("No ships today"))
                    secondHit = x;
            check(pane0->mode() == ViewMode::Combined && firstHit && firstHit->item() == ships && secondHit && secondHit->item() == fog
                      && pane0->items() == QList<BinderItem *>({chapter}),
                  QStringLiteral("in Combined text, Find by Formatting goes from section to section"),
                  QStringLiteral("first %1 second %2").arg(firstHit != nullptr).arg(secondHit != nullptr));
            win->close();
            settle(200);
        }
    }

    // ============================================================ reopening
    {
        byKey(&w, QStringLiteral("projectSearch"))->trigger();
        settle(200);
        search(QStringLiteral("fog rocks"));
        QMenu menu;
        binder->fillSearchMenu(&menu);
        menuAction(&menu, QStringLiteral("Any Word"))->trigger();
        settle(200);
        const int before = results->count();
        w.closeProject();
        settle(300);
        check(w.openProject(projectFile), QStringLiteral("reopen the project"));
        settle(800);
        proj = w.project();
        binder = w.findChild<BinderPanel *>();
        results = binder->resultsList();
        CollectionTabs *tabs = binder->collectionTabs();
        const bool quiet = !binder->showsSearchResults() && !binder->isSearching();
        tabs->setCurrentIndex(tabs->count() - 1);
        settle(300);
        check(before == 2 && quiet && binder->showsSearchResults() && results->count() == 2 && binder->searchQuery().anyWord
                  && binder->searchField()->text() == QLatin1String("fog rocks") && !w.findChild<FormatFindWindow *>(),
              QStringLiteral("the last search of a project comes back with it: \"Search Results\" runs it again with its options"),
              QStringLiteral("before %1 quiet %2 now %3").arg(before).arg(quiet).arg(results->count()));
        shot(&w, QStringLiteral("m8-11-reopened"));
    }

    w.closeProject();
    settle(200);
    delete wp;
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
