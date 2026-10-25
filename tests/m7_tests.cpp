// Scripted run for drag and drop, navigation and Quick Reference panels
// (milestone M7). Saves screenshots into the folder given as first argument.
// Exit code = number of failed checks.
//
//   QT_QPA_PLATFORM=offscreen ./qb_m7_tests /tmp/shots
//   xvfb-run -a -s "-screen 0 1920x1080x24" ./qb_m7_tests /tmp/shots   (also drags with the mouse)
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

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderM7Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderM7Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    const bool x11 = QGuiApplication::platformName() == QLatin1String("xcb");

    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.startWithCorkboard = true;
    theme::apply();
    compile::PluginManager::instance().load();

    check(as.dropOnTextDocuments && as.dragExportFormat == QLatin1String("rtf") && as.dragExportChildren && as.reopenQuickRefs
              && as.linkOpens == QLatin1String("other"),
          QStringLiteral("standard behaviors: drop onto text documents allowed, RTF for dragged-out text, panels reopen"));

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
    BinderItem *chapter = m->createItem(proj->draft(), 0, ItemType::Folder, QStringLiteral("The Keeper"));
    BinderItem *ships = m->createItem(chapter, 0, ItemType::Text, QStringLiteral("Counting Ships"));
    BinderItem *logbook = m->createItem(chapter, 1, ItemType::Text, QStringLiteral("The Logbook"));
    BinderItem *fog = m->createItem(chapter, 2, ItemType::Text, QStringLiteral("Morning Fog"));
    BinderItem *chapter2 = m->createItem(proj->draft(), 1, ItemType::Folder, QStringLiteral("The Storm"));
    BinderItem *wreck = m->createItem(chapter2, 0, ItemType::Text, QStringLiteral("The Wreck"));
    BinderItem *deep = m->createItem(wreck, 0, ItemType::Text, QStringLiteral("Salvage"));
    BinderItem *tides = m->createItem(proj->research(), 0, ItemType::Text, QStringLiteral("Tide Tables"));
    templates::setMarkdown(proj, ships, QStringLiteral("He counted the ships from the gallery, **forty-one** of them.\nThe wind leaned on the glass.\n"));
    templates::setMarkdown(proj, logbook, QStringLiteral("He wrote the number in the margin.\nSee the tide tables for the day.\n"));
    templates::setMarkdown(proj, fog, QStringLiteral("The fog came in before dawn and stayed.\n"));
    templates::setMarkdown(proj, wreck, QStringLiteral("The hull lay on the rocks.\n"));
    templates::setMarkdown(proj, deep, QStringLiteral("What the sea gave back.\n"));
    templates::setMarkdown(proj, tides, QStringLiteral("High water 04:12 and 16:40.\nLow water 10:25 and 22:51.\n"));
    // a picture in the research folder
    QTemporaryDir files;
    const QString pngPath = files.filePath(QStringLiteral("Lamp.png"));
    {
        QImage img(240, 160, QImage::Format_RGB32);
        img.fill(QColor(230, 180, 60));
        for (int x = 40; x < 200; ++x)
            for (int y = 60; y < 100; ++y)
                img.setPixelColor(x, y, QColor(40, 60, 120));
        img.save(pngPath);
    }
    QStringList warnings;
    const QList<BinderItem *> imported = importer::importFiles(proj, {pngPath}, proj->research(), -1, &warnings);
    BinderItem *lamp = imported.value(0);
    check(lamp && lamp->type == ItemType::Image, QStringLiteral("a picture is in the research folder"), warnings.join(QLatin1Char(' ')));
    // keywords: a group with two names, and a single one
    const QString kwGroup = newId(), kwJohn = newId(), kwMary = newId(), kwTheme = newId();
    proj->keywords << Keyword{kwGroup, QStringLiteral("Characters"), QColor(200, 80, 80), QString()}
                   << Keyword{kwJohn, QStringLiteral("John"), QColor(80, 120, 200), kwGroup}
                   << Keyword{kwMary, QStringLiteral("Mary"), QColor(80, 180, 120), kwGroup}
                   << Keyword{kwTheme, QStringLiteral("Solitude"), QColor(150, 150, 150), QString()};
    proj->notifySettingsChanged();
    m->refreshAll();
    proj->save();
    settle(200);
    if (!lamp)
        return 1;

    auto *binder = w.findChild<BinderPanel *>();
    auto *editorSplit = qobject_cast<QSplitter *>(w.findChild<EditorPane *>()->parentWidget());
    auto *pane0 = editorSplit ? qobject_cast<EditorPane *>(editorSplit->widget(0)) : nullptr;
    auto *pane1 = editorSplit ? qobject_cast<EditorPane *>(editorSplit->widget(1)) : nullptr;
    auto *inspector = w.findChild<Inspector *>();
    check(binder && pane0 && pane1 && inspector, QStringLiteral("window parts found"));
    if (!binder || !pane0 || !pane1 || !inspector)
        return 1;
    BinderTree *tree = binder->tree();
    tree->expandAll();
    settle(200);
    auto rowCenter = [&](BinderItem *it) { return tree->visualRect(m->indexFor(it)).center(); };
    QWidget *header0 = pane0->findChild<QWidget *>(QStringLiteral("editorHeader"));
    QWidget *header1 = pane1->findChild<QWidget *>(QStringLiteral("editorHeader"));

    // ================================================================ DRAG AND DROP
    // ---- what a drag of documents carries
    {
        QMimeData *mime = BinderModel::mimeForItems({ships}, proj);
        const bool formats = mime->hasFormat(QString::fromLatin1(kItemsMime)) && mime->hasText() && mime->hasUrls()
                             && mime->text() == QLatin1String("Counting Ships");
        const QList<QUrl> urls = mime->urls();
        const QString file = urls.value(0).toLocalFile();
        QFile f(file);
        const bool exported = urls.size() == 1 && QFileInfo(file).fileName() == QLatin1String("Counting Ships.rtf") && f.open(QIODevice::ReadOnly)
                              && f.readAll().contains("forty-one");
        const bool once = mime->urls() == urls; // asked again: the same files
        check(formats && exported && once && BinderModel::itemsFromMime(mime, proj) == QList<BinderItem *>({ships}),
              QStringLiteral("a dragged document is itself, its title as text and, for other programs, an RTF file written on request"),
              QStringLiteral("formats %1 exported %2 (%3) once %4").arg(formats).arg(exported).arg(file).arg(once));
        delete mime;

        as.dragExportFormat = QStringLiteral("docx");
        QMimeData *group = BinderModel::mimeForItems({chapter, lamp}, proj);
        QStringList names;
        for (const QUrl &u : group->urls())
            names << QFileInfo(u.toLocalFile()).fileName();
        const QString folder = QFileInfo(group->urls().value(0).toLocalFile()).absolutePath() + QLatin1String("/The Keeper");
        const QStringList inside = QDir(folder).entryList(QDir::Files, QDir::Name);
        check(names == QStringList({QStringLiteral("The Keeper"), QStringLiteral("Lamp.png")})
                  && inside == QStringList({QStringLiteral("Counting Ships.docx"), QStringLiteral("Morning Fog.docx"), QStringLiteral("The Logbook.docx")}),
              QStringLiteral("a dragged folder becomes a folder with its documents (here as Word files); a picture stays the picture"),
              names.join(QLatin1String(" | ")) + QLatin1String(" / ") + inside.join(QLatin1String(" | ")));
        delete group;
        as.dragExportChildren = false;
        QMimeData *alone = BinderModel::mimeForItems({wreck}, proj);
        names.clear();
        for (const QUrl &u : alone->urls())
            names << QFileInfo(u.toLocalFile()).fileName();
        check(names == QStringList({QStringLiteral("The Wreck.docx")}), QStringLiteral("without subdocuments a group is one file"),
              names.join(QLatin1String(" | ")));
        delete alone;
        as.dragExportFormat = QStringLiteral("rtf");
        as.dragExportChildren = true;

        // the export dialog's function: names that are taken get a number
        QTemporaryDir out;
        const QString first = dnd::exportItem(proj, fog, out.path(), QStringLiteral("txt"), false, false);
        const QString second = dnd::exportItem(proj, fog, out.path(), QStringLiteral("txt"), false, false);
        check(QFileInfo(first).fileName() == QLatin1String("Morning Fog.txt") && QFileInfo(second).fileName() == QLatin1String("Morning Fog 2.txt")
                  && QFileInfo::exists(second),
              QStringLiteral("exporting twice into one folder keeps both files"), first + QLatin1String(" ") + second);
    }

    // ---- the header icon is the document
    {
        binder->select({ships}, true);
        settle(300);
        auto *icon0 = header0->findChild<QToolButton *>(QStringLiteral("headerIcon"));
        check(icon0 && pane0->items() == QList<BinderItem *>({ships}), QStringLiteral("the header bar has the document icon"));
        if (x11 && icon0) {
            byKey(&w, QStringLiteral("splitV")) ? byKey(&w, QStringLiteral("splitV"))->trigger() : void();
            settle(300);
            if (!pane1->isVisible()) {
                std::printf("      (split by command not available, using the header button)\n");
                emit pane0->splitButtonClicked(pane0, false);
                settle(300);
            }
            pane1->showItems({fog});
            settle(200);
            mouseDrag(icon0, center(icon0), header1, center(header1));
            const bool shown = pane1->items() == QList<BinderItem *>({ships}) && pane0->items() == QList<BinderItem *>({ships});
            // ... and onto a folder in the binder: it moves there
            mouseDrag(icon0, center(icon0), tree->viewport(), rowCenter(chapter2));
            const bool moved = ships->parent == chapter2;
            check(shown && moved,
                  QStringLiteral("dragging the header icon with the mouse: onto the other editor's header bar shows the document there, onto a folder in the binder moves it"),
                  QStringLiteral("shown %1 moved %2 (parent %3)").arg(shown).arg(moved).arg(ships->parent->title));
            m->moveItems({ships}, chapter, 0);
            emit pane0->splitButtonClicked(pane0, false); // close the split
            settle(300);
            tree->expandAll();
            pane0->showItems({ships});
            settle(200);
        }
    }

    // ---- toolbar: Quick Reference and Bookmarks take documents
    QToolButton *bmButton = w.findChild<QToolButton *>(QStringLiteral("bookmarksButton"));
    QWidget *qrButton = nullptr;
    for (QToolBar *bar : w.findChildren<QToolBar *>())
        if (QWidget *b = bar->widgetForAction(byKey(&w, QStringLiteral("quickRef"))))
            qrButton = b;
    {
        check(qrButton && bmButton && dnd::DropTarget::installedOn(qrButton) && dnd::DropTarget::installedOn(bmButton),
              QStringLiteral("the Quick Reference and Bookmarks buttons of the toolbar take drops"));
        const bool took = qrButton && dropItems(qrButton, center(qrButton), proj, {lamp, tides});
        settle(300);
        QStringList shown;
        for (QuickRefWindow *qr : quickRefs())
            shown << qr->item()->title;
        shown.sort();
        check(took && shown == QStringList({QStringLiteral("Lamp"), QStringLiteral("Tide Tables")}) && pane0->items() == QList<BinderItem *>({ships}),
              QStringLiteral("two documents dropped on the Quick Reference button open in two panels; the editor keeps its document"),
              shown.join(QLatin1String(", ")));
        QMimeData *plain = new QMimeData;
        plain->setText(QStringLiteral("just text"));
        const bool refused = qrButton && !dropMime(qrButton, center(qrButton), plain);
        delete plain;
        closeQuickRefs();

        const int before = int(proj->projectBookmarks.size());
        const bool tookBm = bmButton && dropItems(bmButton, center(bmButton), proj, {tides, logbook});
        const int after = int(proj->projectBookmarks.size());
        dropItems(bmButton, center(bmButton), proj, {tides}); // a second time: no second bookmark
        emit bmButton->menu()->aboutToShow();
        const QStringList menu = menuTexts(bmButton->menu());
        check(refused && tookBm && after == before + 2 && proj->projectBookmarks.size() == after && menu.contains(QStringLiteral("Tide Tables"))
                  && menu.contains(QStringLiteral("The Logbook")),
              QStringLiteral("documents dropped on the Bookmarks button become project bookmarks (once); other things are refused"),
              QStringLiteral("refused %1 took %2: %3").arg(refused).arg(tookBm).arg(bookmarkTitles(proj)));
    }

    // ---- collections
    {
        Collection picks;
        picks.id = newId();
        picks.name = QStringLiteral("Problems");
        picks.color = QColor(200, 120, 60);
        picks.items << fog->id;
        Collection search;
        search.id = newId();
        search.name = QStringLiteral("Fog search");
        search.isSearch = true;
        search.query.text = QStringLiteral("fog");
        Collection later;
        later.id = newId();
        later.name = QStringLiteral("Later");
        proj->collections << picks << search << later;
        proj->notifySettingsChanged();
        byKey(&w, QStringLiteral("showCollections"))->setChecked(true);
        binder->setCollectionsVisible(true);
        binder->refreshCollections();
        settle(200);
        CollectionTabs *tabs = binder->collectionTabs();
        auto tabOf = [&](const QString &id) {
            for (int i = 0; i < tabs->count(); ++i)
                if (tabs->tabData(i).toString() == id)
                    return i;
            return -1;
        };
        auto collection = [&](const QString &id) -> Collection * {
            for (Collection &c : proj->collections)
                if (c.id == id)
                    return &c;
            return nullptr;
        };
        const bool took = dropItems(tabs, tabs->tabRect(tabOf(picks.id)).center(), proj, {ships, logbook});
        const bool again = dropItems(tabs, tabs->tabRect(tabOf(picks.id)).center(), proj, {ships});
        const bool searchRefuses = !dropItems(tabs, tabs->tabRect(tabOf(search.id)).center(), proj, {ships});
        const bool binderRefuses = !dropItems(tabs, tabs->tabRect(0).center(), proj, {ships});
        check(took && again && searchRefuses && binderRefuses
                  && collection(picks.id)->items == QStringList({fog->id, ships->id, logbook->id}) && ships->parent == chapter,
              QStringLiteral("documents dropped on the tab of a collection are added to it (once) and stay in the binder; a search collection takes none"),
              QStringLiteral("took %1 search %2 binder %3 items %4").arg(took).arg(searchRefuses).arg(binderRefuses).arg(collection(picks.id)->items.size()));

        // the list of the collection: reorder by dragging, add at a place, remove
        binder->showCollection(picks.id);
        settle(200);
        ItemListWidget *list = binder->resultsList();
        auto listTitles = [&] {
            QStringList t;
            for (int i = 0; i < list->count(); ++i)
                t << list->item(i)->text();
            return t.join(QLatin1String(", "));
        };
        const bool reorderable = list->isReorderable() && binder->showsArbitraryCollection() && list->count() == 3;
        // "The Logbook" (last) to the top
        dropItems(list->viewport(), list->visualItemRect(list->item(0)).topLeft() + QPoint(20, 2), proj, {logbook});
        const QString afterMove = listTitles();
        // a document from the binder between the first and second row
        dropItems(list->viewport(), list->visualItemRect(list->item(0)).bottomLeft() + QPoint(20, -2), proj, {tides});
        const QString afterAdd = listTitles();
        check(reorderable && afterMove == QLatin1String("The Logbook, Morning Fog, Counting Ships")
                  && afterAdd == QLatin1String("The Logbook, Tide Tables, Morning Fog, Counting Ships")
                  && collection(picks.id)->items == QStringList({logbook->id, tides->id, fog->id, ships->id}),
              QStringLiteral("in the list of a collection, documents can be dragged into another order and dropped in at a place"),
              afterMove + QLatin1String(" / ") + afterAdd);
        shot(&w, QStringLiteral("m7-01-collection"));

        list->setFocus();
        list->setCurrentRow(1);
        list->item(1)->setSelected(true);
        settle(100);
        QTest::keyClick(list, Qt::Key_Space);
        settle(300);
        const bool spaceOpens = quickRefs().size() == 1 && quickRefs().first()->item() == tides;
        closeQuickRefs();
        list->setFocus();
        list->clearSelection();
        list->setCurrentRow(1);
        list->item(1)->setSelected(true);
        QTest::keyClick(list, Qt::Key_Delete);
        settle(200);
        check(spaceOpens && listTitles() == QLatin1String("The Logbook, Morning Fog, Counting Ships") && proj->item(tides->id) == tides
                  && !proj->isInTrash(tides),
              QStringLiteral("in a collection, Space opens a Quick Reference panel and Delete takes the document out of the collection only"),
              QStringLiteral("space %1 list %2").arg(spaceOpens).arg(listTitles()));

        // keywords dropped on a row of the list
        dropKeywords(list->viewport(), list->visualItemRect(list->item(1)).center(), proj, {kwTheme});
        check(fog->keywords == QStringList({kwTheme}) && logbook->keywords.isEmpty(),
              QStringLiteral("a keyword dropped on a document in the list is assigned to it"));
        fog->keywords.clear();

        if (x11) {
            // with the mouse: a row of the list onto the editor's header bar, and a binder document onto a tab
            mouseDrag(list->viewport(), list->visualItemRect(list->item(1)).center(), header0, center(header0));
            const bool loaded = pane0->items() == QList<BinderItem *>({fog}) && list->count() == 3;
            binder->showCollection(QString()); // the binder
            tabs->setCurrentIndex(0);
            settle(300);
            tree->expandAll();
            settle(200);
            mouseDrag(tree->viewport(), rowCenter(wreck), tabs, tabs->tabRect(tabOf(picks.id)).center());
            const bool added = collection(picks.id)->items.contains(wreck->id) && wreck->parent == chapter2;
            collection(picks.id)->items.removeAll(wreck->id);
            check(loaded && added,
                  QStringLiteral("with the mouse: a row of a collection dragged to the header bar loads the document (the row stays); a binder document dragged to a tab joins that collection"),
                  QStringLiteral("loaded %1 (%2, rows %3) added %4").arg(loaded).arg(titles(pane0->items())).arg(list->count()).arg(added));
            pane0->showItems({ships});
        }

        // the tabs can be put into another order; "Binder" stays first
        tabs->moveTab(tabOf(later.id), 1);
        settle(200);
        const bool orderStored = proj->collections.value(0).id == later.id && proj->collections.value(1).id == picks.id;
        tabs->moveTab(0, 2);
        settle(300);
        // (three collections, "Binder" before them and, since 0.9, "Search Results" after them)
        check(orderStored && tabs->tabData(0).toString().isEmpty() && tabs->count() == 5 && proj->collections.value(0).id == later.id
                  && tabs->tabData(4).toString() == QLatin1String("__search"),
              QStringLiteral("collection tabs can be dragged into another order (kept in the project); the Binder tab stays the first"),
              QStringLiteral("stored %1, first tab '%2'").arg(orderStored).arg(tabs->tabText(0)));
        tabs->setCurrentIndex(0);
        binder->setCollectionsVisible(false);
        byKey(&w, QStringLiteral("showCollections"))->setChecked(false);
        settle(200);
        tree->expandAll();
    }

    // ---- keywords: from the Project Keywords window onto documents
    {
        byKey(&w, QStringLiteral("keywords"))->trigger();
        settle(300);
        auto *dialog = w.findChild<KeywordsDialog *>();
        auto *kwTree = dialog ? dialog->findChild<QTreeWidget *>(QStringLiteral("keywordTree")) : nullptr;
        check(dialog && kwTree && kwTree->dragEnabled() && kwTree->selectionMode() == QAbstractItemView::ExtendedSelection,
              QStringLiteral("Project > Project Keywords lists the keywords as a tree that can be dragged from"));
        if (dialog)
            dialog->move(1320, 60);
        settle(200);

        // binder: on a document that is not selected -> that one only
        binder->select({ships}, false);
        dropKeywords(tree->viewport(), rowCenter(logbook), proj, {kwJohn});
        const bool single = logbook->keywords == QStringList({kwJohn}) && ships->keywords.isEmpty();
        // on one of several selected -> all selected; with Alt the group comes along
        binder->select({ships, fog}, false);
        settle(100);
        dropKeywords(tree->viewport(), rowCenter(fog), proj, {kwMary}, Qt::AltModifier);
        const bool multi = ships->keywords == QStringList({kwGroup, kwMary}) && fog->keywords == QStringList({kwGroup, kwMary})
                           && logbook->keywords == QStringList({kwJohn});
        const bool emptySpace = !dropKeywords(tree->viewport(), QPoint(tree->viewport()->width() / 2, tree->viewport()->height() - 4), proj, {kwJohn});
        check(single && multi && emptySpace,
              QStringLiteral("keywords dropped in the binder go to the document under the mouse, or to all selected ones if it is one of them; with Alt the group is assigned too"),
              QStringLiteral("single %1 multi %2 empty %3").arg(single).arg(multi).arg(emptySpace));

        // inspector keyword list
        binder->select({tides}, true);
        settle(300);
        inspector->show();
        inspector->showTab(Inspector::Metadata);
        settle(200);
        QListWidget *inspKw = inspector->keywordList();
        const bool tookInsp = dropKeywords(inspKw->viewport(), QPoint(20, 10), proj, {kwJohn, kwTheme});
        check(tookInsp && tides->keywords == QStringList({kwJohn, kwTheme}) && inspKw->count() == 2,
              QStringLiteral("keywords dropped into the keyword list of the inspector are assigned to its document"),
              QStringLiteral("%1 keywords, list %2").arg(tides->keywords.size()).arg(inspKw->count()));

        // outliner and corkboard
        binder->select({chapter}, true);
        settle(300);
        pane0->setViewMode(ViewMode::Outliner);
        settle(300);
        Outliner *outliner = pane0->outliner();
        QModelIndex rowOfLogbook;
        for (int r = 0; r < outliner->model()->rowCount(outliner->rootIndex()); ++r)
            if (outliner->model()->index(r, 0, outliner->rootIndex()).data().toString() == QLatin1String("The Logbook"))
                rowOfLogbook = outliner->model()->index(r, 0, outliner->rootIndex());
        const bool tookOutline = rowOfLogbook.isValid()
                                 && dropKeywords(outliner->viewport(), outliner->visualRect(rowOfLogbook).center(), proj, {kwTheme});
        pane0->setViewMode(ViewMode::Corkboard);
        settle(400);
        Corkboard *cork = pane0->corkboard();
        CardItem *fogCard = nullptr;
        for (QGraphicsItem *gi : cork->scene()->items())
            if (auto *card = dynamic_cast<CardItem *>(gi))
                if (card->item() == fog)
                    fogCard = card;
        const bool tookCard = fogCard && dropKeywords(cork->viewport(), cork->mapFromScene(fogCard->sceneBoundingRect().center()), proj, {kwJohn});
        check(tookOutline && tookCard && logbook->keywords == QStringList({kwJohn, kwTheme})
                  && fog->keywords == QStringList({kwGroup, kwMary, kwJohn}),
              QStringLiteral("keywords can be dropped on a row of the outliner and on an index card"),
              QStringLiteral("outliner %1 card %2").arg(tookOutline).arg(tookCard));
        shot(&w, QStringLiteral("m7-02-keywords-on-cards"));

        // inside the keyword tree: onto another keyword = into its group
        if (kwTree) {
            QTreeWidgetItem *theme = nullptr, *group = nullptr;
            for (QTreeWidgetItem *ti : kwTree->findItems(QString(), Qt::MatchContains | Qt::MatchRecursive)) {
                if (ti->data(0, Qt::UserRole).toString() == kwTheme)
                    theme = ti;
                if (ti->data(0, Qt::UserRole).toString() == kwGroup)
                    group = ti;
            }
            bool regrouped = false;
            if (theme && group && x11) {
                mouseDrag(kwTree->viewport(), kwTree->visualItemRect(theme).center(), kwTree->viewport(), kwTree->visualItemRect(group).center());
                regrouped = proj->keywordById(kwTheme)->parentId == kwGroup;
                check(regrouped, QStringLiteral("with the mouse: a keyword dragged onto another one in the Project Keywords window joins its group"));
                for (Keyword &k : proj->keywords)
                    if (k.id == kwTheme)
                        k.parentId.clear();
                proj->notifySettingsChanged();
            }
            // with the mouse from the keywords window onto a document in the binder (another window)
            if (x11) {
                tree->expandAll();
                settle(300);
                QTreeWidgetItem *john = nullptr;
                for (QTreeWidgetItem *ti : kwTree->findItems(QStringLiteral("John"), Qt::MatchExactly | Qt::MatchRecursive))
                    john = ti;
                wreck->keywords.clear();
                if (john) {
                    dialog->raise();
                    kwTree->clearSelection();
                    john->setSelected(true);
                    kwTree->setCurrentItem(john);
                    settle(200);
                    mouseDrag(kwTree->viewport(), kwTree->visualItemRect(john).center(), tree->viewport(), rowCenter(wreck));
                }
                check(john && wreck->keywords == QStringList({kwJohn}),
                      QStringLiteral("with the mouse: a keyword dragged from the Project Keywords window onto a document in the binder is assigned"),
                      QStringLiteral("%1 keywords").arg(wreck->keywords.size()));
            }
            shot(dialog, QStringLiteral("m7-03-project-keywords"));
        }
        if (dialog)
            dialog->close();
        pane0->setViewMode(ViewMode::Single);
        binder->select({ships}, true);
        settle(300);
    }

    // ---- text editor: link, text and picture; text to the binder
    {
        binder->select({logbook}, true);
        settle(300);
        TextEditor *ed = pane0->currentEditor();
        check(ed && ed->item() == logbook, QStringLiteral("the logbook is in the editor"));
        // a document onto selected text: a link
        QTextCursor c = ed->document()->find(QStringLiteral("tide tables"));
        ed->setTextCursor(c);
        const QPoint onSelection = ed->cursorRect(c).center() - QPoint(12, 0);
        dropItems(ed->viewport(), onSelection, proj, {tides});
        const bool linked = anchorOf(ed->document(), QStringLiteral("tide tables")) == QLatin1String("qb://") + tides->id;
        // with Alt: the text of the document
        c = ed->textCursor();
        c.movePosition(QTextCursor::End);
        ed->setTextCursor(c);
        dropItems(ed->viewport(), ed->cursorRect(c).center(), proj, {fog}, Qt::AltModifier);
        const bool inserted = ed->toPlainText().contains(QLatin1String("The fog came in before dawn"));
        // a picture of the binder: the picture itself
        c = ed->textCursor();
        c.movePosition(QTextCursor::End);
        ed->setTextCursor(c);
        dropItems(ed->viewport(), ed->cursorRect(c).center(), proj, {lamp});
        const bool picture = hasImage(ed->document()) && lamp->parent == proj->research();
        check(linked && inserted && picture,
              QStringLiteral("a document dropped on selected text links it, with Alt its text is inserted, a picture from the binder is put in as a picture"),
              QStringLiteral("linked %1 inserted %2 picture %3").arg(linked).arg(inserted).arg(picture));

        // text into the binder: a new document with that text and its formatting; the source keeps it
        binder->select({ships}, true);
        settle(300);
        ed = pane0->currentEditor();
        c = ed->document()->find(QStringLiteral("forty-one"));
        c.movePosition(QTextCursor::StartOfBlock);
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        ed->setTextCursor(c);
        const int docsBefore = int(chapter->children.size());
        if (x11) {
            // (inside the word: a press exactly between two differently formatted characters starts a new selection)
            const QPoint inSelection = ed->cursorRect(ed->document()->find(QStringLiteral("forty-one"))).center() - QPoint(14, 0);
            ed->setFocus();
            settle(200);
            ed->setTextCursor(c);
            settle(300);
            mouseDrag(ed->viewport(), inSelection, tree->viewport(), rowCenter(chapter));
        } else {
            auto *md = new QMimeData;
            md->setText(c.selectedText());
            QTextDocument tmp;
            QTextCursor(&tmp).insertFragment(c.selection());
            md->setHtml(tmp.toHtml());
            // as the editor's own drag would: through the model
            m->dropMimeData(md, Qt::CopyAction, -1, 0, m->indexFor(chapter));
            delete md;
        }
        settle(300);
        if (chapter->children.size() != docsBefore + 1) {
            std::printf("      (after the text drag: chapter has %s; draft has %s; selection '%s')\n", qPrintable(titles(chapter->children)),
                        qPrintable(titles(proj->draft()->children)), qPrintable(ed->textCursor().selectedText().left(30)));
        }
        BinderItem *fresh = chapter->children.size() == docsBefore + 1 ? chapter->children.last() : nullptr;
        bool bold = false;
        if (fresh) {
            QTextDocument *nd = proj->docs()->text(fresh->id);
            const QTextCursor found = nd->find(QStringLiteral("forty-one"));
            bold = !found.isNull() && found.charFormat().fontWeight() > QFont::Normal;
        }
        check(fresh && fresh->title.startsWith(QLatin1String("He counted the ships")) && (bold || !x11)
                  && proj->docs()->text(ships->id)->toPlainText().contains(QLatin1String("forty-one")),
              x11 ? QStringLiteral("with the mouse: selected text dragged into the binder becomes a new document with its formatting; the text stays where it was")
                  : QStringLiteral("text dropped into the binder becomes a new document; the text stays where it was"),
              QStringLiteral("new %1 '%2' bold %3").arg(fresh != nullptr).arg(fresh ? fresh->title : QString()).arg(bold));
        if (fresh)
            m->moveToTrash({fresh});
        settle(200);
        tree->expandAll();
    }

    // ---- synopsis picture by drop
    {
        binder->select({fog}, true);
        settle(300);
        inspector->showTab(Inspector::Notes);
        auto *imageToggle = inspector->findChild<QToolButton *>(QStringLiteral("synopsisImageToggle"));
        QLabel *area = inspector->synopsisImageArea();
        if (imageToggle)
            imageToggle->setChecked(true);
        settle(200);
        const bool invites = area->isVisible() && area->text().startsWith(QLatin1String("Drop an image here")) && fog->synopsisImage.isEmpty();
        const bool refusesText = !dropItems(area, center(area), proj, {tides});
        const bool took = dropItems(area, center(area), proj, {lamp});
        const QString stored = proj->contentDir(fog->id) + QLatin1Char('/') + fog->synopsisImage;
        const bool set = !fog->synopsisImage.isEmpty() && QFileInfo::exists(stored) && !area->pixmap(Qt::ReturnByValue).isNull();
        shot(inspector, QStringLiteral("m7-04-synopsis-picture"));
        // an image file from the file manager replaces it
        auto *md = new QMimeData;
        md->setUrls({QUrl::fromLocalFile(pngPath)});
        const bool tookFile = dropMime(area, center(area), md);
        delete md;
        inspector->setSynopsisImage(QString());
        check(imageToggle && invites && refusesText && took && set && tookFile && fog->synopsisImage.isEmpty() && !QFileInfo::exists(stored),
              QStringLiteral("the index card in the inspector takes a picture from the binder or an image file as the document's picture; text documents are refused"),
              QStringLiteral("toggle %1 invites %2 refuses %3 took %4 set %5 file %6").arg(imageToggle != nullptr).arg(invites).arg(refusesText).arg(took).arg(set).arg(tookFile));
        if (imageToggle)
            imageToggle->setChecked(false);
    }

    // ---- dropping onto documents: the option
    {
        QMimeData *md = BinderModel::mimeForItems({fog}, proj);
        const bool onText = m->canDropMimeData(md, Qt::MoveAction, -1, 0, m->indexFor(logbook));
        OptionsDialog od(&w, OptionsDialog::Behaviors);
        auto *dropOnText = od.findChild<QCheckBox *>(QStringLiteral("dropOnText"));
        auto *format = od.findChild<QComboBox *>(QStringLiteral("dragFormat"));
        auto *links = od.findChild<QComboBox *>(QStringLiteral("linkOpens"));
        od.show();
        shot(&od, QStringLiteral("m7-05-options-behaviors"));
        const bool page = dropOnText && format && links && dropOnText->isChecked() && links->findData(QStringLiteral("copyholder")) >= 0
                          && format->count() == 4;
        if (dropOnText)
            dropOnText->setChecked(false);
        if (format)
            format->setCurrentIndex(format->findData(QStringLiteral("md")));
        od.apply();
        settle(200);
        const bool offText = !m->canDropMimeData(md, Qt::MoveAction, -1, 0, m->indexFor(logbook));
        const bool folderStill = m->canDropMimeData(md, Qt::MoveAction, -1, 0, m->indexFor(chapter2));
        const bool betweenStill = m->canDropMimeData(md, Qt::MoveAction, 1, 0, m->indexFor(chapter));
        const bool stored = !as.dropOnTextDocuments && as.dragExportFormat == QLatin1String("md");
        dropOnText->setChecked(true);
        format->setCurrentIndex(format->findData(QStringLiteral("rtf")));
        od.apply();
        od.hide();
        check(page && onText && offText && folderStill && betweenStill && stored && as.dropOnTextDocuments
                  && m->canDropMimeData(md, Qt::MoveAction, -1, 0, m->indexFor(logbook)),
              QStringLiteral("File > Options > Behaviors: with \"drop onto text documents\" off, documents still go between others and into folders"),
              QStringLiteral("page %1 on %2 off %3 folder %4 between %5 stored %6").arg(page).arg(onText).arg(offText).arg(folderStill).arg(betweenStill).arg(stored));
        delete md;
    }

    // ---- bookmarks in the inspector: order, links from a browser
    {
        binder->select({ships}, true);
        settle(200);
        inspector->showTab(Inspector::Bookmarks);
        // the project's bookmarks have a list of their own in the tab since 0.14.1
        QWidget *scope = inspector->projectBookmarkList();
        settle(200);
        BookmarkListWidget *list = inspector->projectBookmarkList();
        const int n = list->count();
        const QString first = list->item(0) ? list->item(0)->text() : QString();
        list->moveBookmark(0, n); // the first one to the end
        const bool moved = list->count() == n && list->item(n - 1)->text() == first && bookmarkTitles(proj).endsWith(first);
        auto *md = new QMimeData;
        md->setUrls({QUrl(QStringLiteral("https://example.org/tides"))});
        md->setText(QStringLiteral("Tide predictions"));
        const bool tookLink = dropMime(list->viewport(), QPoint(20, 4), md);
        delete md;
        const Bookmark top = list->bookmarkAt(0);
        const bool dedupe = !dropItems(list->viewport(), QPoint(20, 4), proj, {tides}) || list->count() == n + 1;
        check(scope && moved && tookLink && top.title == QLatin1String("Tide predictions") && top.target == QLatin1String("https://example.org/tides")
                  && !top.isInternal() && dedupe && list->count() == n + 1,
              QStringLiteral("bookmarks can be put into another order; a web link dropped in keeps the page title; a document is bookmarked only once"),
              QStringLiteral("scope %1 moved %2 link %3 '%4' count %5/%6").arg(scope != nullptr).arg(moved).arg(tookLink).arg(top.title).arg(list->count()).arg(n + 1));
        list->removeBookmark(0);
    }

    // ================================================================ NAVIGATION
    // ---- history: the buttons' lists, the menus, the grey bar in the binder
    {
        binder->select({ships}, true);
        settle(150);
        binder->select({logbook}, true);
        settle(150);
        binder->select({fog}, true);
        settle(150);
        binder->select({tides}, true);
        settle(150);
        QMenu backMenu;
        pane0->fillHistoryMenu(&backMenu, true);
        const QStringList back = menuTexts(&backMenu).mid(0, 3);
        byKey(&w, QStringLiteral("back"))->trigger();
        byKey(&w, QStringLiteral("back"))->trigger();
        settle(300);
        QMenu forwardMenu;
        pane0->fillHistoryMenu(&forwardMenu, false);
        const QStringList forward = menuTexts(&forwardMenu);
        const bool marked = pane0->items() == QList<BinderItem *>({logbook}) && tree->editorItemId() == logbook->id
                            && binder->selectedItems() == QList<BinderItem *>({tides});
        check(back == QStringList({QStringLiteral("Morning Fog"), QStringLiteral("The Logbook"), QStringLiteral("Counting Ships")})
                  && forward == QStringList({QStringLiteral("Morning Fog"), QStringLiteral("Tide Tables")}) && marked,
              QStringLiteral("the history lists the documents to go back and forward to; after going back, the binder marks the editor's document while another stays selected"),
              back.join(QLatin1String(", ")) + QLatin1String(" / ") + forward.join(QLatin1String(", ")) + QStringLiteral(" / marked %1").arg(marked));
        shot(&w, QStringLiteral("m7-06-binder-marks-editor-document"));
        // the grey bar is painted: the row of the logbook differs from a plain row
        {
            const QImage img = tree->viewport()->grab().toImage();
            const QRect r = tree->visualRect(m->indexFor(logbook));
            const QRect plain = tree->visualRect(m->indexFor(fog));
            const QColor barPixel = img.pixelColor(tree->viewport()->width() - 12, r.center().y());
            const QColor plainPixel = img.pixelColor(tree->viewport()->width() - 12, plain.center().y());
            check(barPixel.rgb() != plainPixel.rgb(), QStringLiteral("the row of the editor's document carries a grey bar"),
                  barPixel.name() + QLatin1Char(' ') + plainPixel.name());
        }
        // pick an entry of the list: straight to it
        QMenu pick;
        pane0->fillHistoryMenu(&pick, false);
        pick.actions().value(1)->trigger(); // Tide Tables
        settle(300);
        const bool jumped = pane0->items() == QList<BinderItem *>({tides}) && !pane0->canGoForward() && tree->editorItemId() == tides->id;
        // menus
        QAction *backTo = menuPath(&w, {QStringLiteral("Navigate"), QStringLiteral("Back To")});
        QAction *forwardTo = menuPath(&w, {QStringLiteral("Navigate"), QStringLiteral("Forward To")});
        QAction *goTo = menuPath(&w, {QStringLiteral("Navigate"), QStringLiteral("Go To")});
        QStringList inBackTo;
        if (backTo && backTo->menu()) {
            emit backTo->menu()->aboutToShow();
            inBackTo = menuTexts(backTo->menu());
        }
        auto *backButton = header0->findChild<QToolButton *>(QStringLiteral("historyBack"));
        check(jumped && backTo && forwardTo && goTo && inBackTo.value(0) == QLatin1String("Morning Fog") && backButton
                  && backButton->findChild<QMenu *>(QStringLiteral("historyMenu")),
              QStringLiteral("an entry of the history list goes straight to that document; Navigate has Back To, Forward To and Go To"),
              QStringLiteral("jumped %1 menus %2%3%4 '%5'").arg(jumped).arg(backTo != nullptr).arg(forwardTo != nullptr).arg(goTo != nullptr).arg(inBackTo.join(QLatin1String(", "))));
    }

    // ---- Go To and Enclosing Group
    {
        QAction *goTo = menuPath(&w, {QStringLiteral("Navigate"), QStringLiteral("Go To")});
        QAction *enclosing = byKey(&w, QStringLiteral("enclosingGroup"));
        bool viaMenu = false;
        QStringList top;
        if (goTo && goTo->menu()) {
            emit goTo->menu()->aboutToShow();
            top = menuTexts(goTo->menu());
            // Draft > The Storm > The Wreck > Salvage
            QMenu *level = goTo->menu();
            for (const QString &name : {proj->draft()->title, QStringLiteral("The Storm"), QStringLiteral("The Wreck")}) {
                QMenu *next = nullptr;
                for (QAction *a : level->actions())
                    if (a->menu() && menuText(a->text()) == name)
                        next = a->menu();
                level = next;
                if (!level)
                    break;
            }
            if (level)
                for (QAction *a : level->actions())
                    if (!a->menu() && menuText(a->text()) == QLatin1String("Salvage")) {
                        a->trigger();
                        viaMenu = true;
                    }
        }
        settle(300);
        const bool loaded = viaMenu && pane0->items() == QList<BinderItem *>({deep});
        const bool canUp = enclosing && enclosing->isEnabled();
        pane0->setGroupMode(ViewMode::Corkboard); // groups open as corkboard
        if (enclosing)
            enclosing->trigger();
        settle(300);
        const bool up1 = pane0->items() == QList<BinderItem *>({wreck});
        if (enclosing)
            enclosing->trigger();
        settle(300);
        // (the group opens in the view groups were last shown in)
        const QList<BinderItem *> selectedInGroup = pane0->mode() == ViewMode::Corkboard ? pane0->corkboard()->selectedItems()
                                                    : pane0->mode() == ViewMode::Outliner ? pane0->outliner()->selectedItems()
                                                                                          : QList<BinderItem *>();
        const bool up2 = pane0->items() == QList<BinderItem *>({chapter2}) && selectedInGroup == QList<BinderItem *>({wreck});
        check(top.mid(0, 3) == QStringList({QStringLiteral("Previous Document"), QStringLiteral("Next Document"), QStringLiteral("Enclosing Group")})
                  && loaded && canUp && up1 && up2,
              QStringLiteral("Navigate > Go To loads a document from the binder as a menu; Enclosing Group goes up to the group, with the document it came from selected"),
              QStringLiteral("menu %1 loaded %2 up %3 %4 (%5)").arg(top.mid(0, 3).join(QLatin1Char('|'))).arg(loaded).arg(up1).arg(up2).arg(titles(pane0->items())));
    }

    // ---- outliner and corkboard: Space, double click on the icon, Alt+click on the arrow
    {
        binder->select({proj->draft()}, true);
        settle(300);
        pane0->setViewMode(ViewMode::Outliner);
        settle(400);
        Outliner *outliner = pane0->outliner();
        outliner->collapseAll();
        settle(200);
        QModelIndex storm;
        for (int r = 0; r < outliner->model()->rowCount(outliner->rootIndex()); ++r)
            if (outliner->model()->index(r, 0, outliner->rootIndex()).data().toString() == QLatin1String("The Storm"))
                storm = outliner->model()->index(r, 0, outliner->rootIndex());
        bool expandedAll = false, collapsedAll = false, openedByIcon = false, spaceOpens = false;
        if (storm.isValid()) {
            const QRect r = outliner->visualRect(storm);
            const QPoint arrow(r.left() - 8, r.center().y());
            QTest::mouseClick(outliner->viewport(), Qt::LeftButton, Qt::AltModifier, arrow);
            settle(300);
            const QModelIndex wreckIdx = outliner->model()->index(0, 0, storm);
            expandedAll = outliner->isExpanded(storm) && wreckIdx.isValid() && outliner->isExpanded(wreckIdx);
            QTest::mouseClick(outliner->viewport(), Qt::LeftButton, Qt::AltModifier, QPoint(outliner->visualRect(storm).left() - 8, outliner->visualRect(storm).center().y()));
            settle(300);
            collapsedAll = !outliner->isExpanded(storm) && !outliner->isExpanded(outliner->model()->index(0, 0, storm));
            // Space: Quick Reference for the selected row
            outliner->setFocus();
            outliner->selectionModel()->select(storm, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            outliner->setCurrentIndex(storm);
            settle(100);
            QTest::keyClick(outliner, Qt::Key_Space);
            settle(300);
            spaceOpens = quickRefs().size() == 1 && quickRefs().first()->item() == chapter2;
            closeQuickRefs();
            // double click on the icon: into the folder
            const QRect r2 = outliner->visualRect(storm);
            QTest::mouseDClick(outliner->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(r2.left() + 8, r2.center().y()));
            settle(400);
            openedByIcon = pane0->items() == QList<BinderItem *>({chapter2});
        }
        check(storm.isValid() && expandedAll && collapsedAll && spaceOpens && openedByIcon,
              QStringLiteral("outliner: Alt+click on an arrow opens and closes all levels below, Space opens a Quick Reference panel, a double click on the icon opens the document"),
              QStringLiteral("expanded %1 collapsed %2 space %3 icon %4").arg(expandedAll).arg(collapsedAll).arg(spaceOpens).arg(openedByIcon));

        // binder: Alt+click
        tree->collapse(m->indexFor(chapter2));
        tree->collapse(m->indexFor(wreck));
        settle(200);
        const QRect br = tree->visualRect(m->indexFor(chapter2));
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::AltModifier, QPoint(br.left() - 8, br.center().y()));
        settle(300);
        const bool binderAll = tree->isExpanded(m->indexFor(chapter2)) && tree->isExpanded(m->indexFor(wreck));
        // corkboard: Space
        pane0->showItems({chapter});
        pane0->setViewMode(ViewMode::Corkboard);
        settle(400);
        pane0->corkboard()->selectItems({logbook});
        pane0->corkboard()->setFocus();
        QTest::keyClick(pane0->corkboard(), Qt::Key_Space);
        settle(300);
        const bool corkSpace = quickRefs().size() == 1 && quickRefs().first()->item() == logbook;
        closeQuickRefs();
        check(binderAll && corkSpace,
              QStringLiteral("binder: Alt+click on an arrow opens the whole branch; corkboard: Space opens the selected card as Quick Reference"),
              QStringLiteral("binder %1 corkboard %2").arg(binderAll).arg(corkSpace));
        pane0->setViewMode(ViewMode::Single);
    }

    // ---- split divider, links into the copyholder, inspector with the copyholder
    {
        binder->select({logbook}, true);
        settle(300);
        emit pane0->splitButtonClicked(pane0, false);
        settle(300);
        const int total = editorSplit->sizes().value(0) + editorSplit->sizes().value(1);
        editorSplit->setSizes({total / 4, total - total / 4});
        settle(200);
        QSplitterHandle *handle = editorSplit->handle(1);
        QTest::mouseDClick(handle, Qt::LeftButton, Qt::NoModifier, QPoint(handle->width() / 2, handle->height() / 2));
        settle(300);
        const QList<int> sizes = editorSplit->sizes();
        check(pane1->isVisible() && qAbs(sizes.value(0) - sizes.value(1)) <= 2,
              QStringLiteral("a double click on the divider between the editors makes them the same size"),
              QStringLiteral("%1 / %2").arg(sizes.value(0)).arg(sizes.value(1)));
        emit pane0->splitButtonClicked(pane0, false); // one editor again
        settle(300);

        // a document link opens in the copyholder when the options say so
        as.linkOpens = QStringLiteral("copyholder");
        pane0->showItems({logbook});
        settle(300);
        TextEditor *ed = pane0->currentEditor();
        emit ed->linkActivated(QLatin1String("qb://") + tides->id);
        settle(400);
        const bool held = pane0->hasCopyholder() && pane0->copyholderItem() == tides && pane0->items() == QList<BinderItem *>({logbook})
                          && !pane1->isVisible();
        as.linkOpens = QStringLiteral("other");
        // the inspector follows the keyboard into the copyholder and back
        inspector->showTab(Inspector::Notes);
        w.activateWindow();
        settle(200);
        TextEditor *heldEditor = pane0->copyholder()->editor();
        if (heldEditor)
            heldEditor->setFocus();
        settle(300);
        auto *cardTitle = inspector->findChild<QLineEdit *>(QStringLiteral("cardTitle"));
        const QString inHolder = cardTitle ? cardTitle->text() : QString();
        ed->setFocus();
        settle(300);
        const QString inEditor = cardTitle ? cardTitle->text() : QString();
        // the copyholder's icon is a handle for its document
        auto *holderIcon = pane0->copyholder()->findChild<QLabel *>(QStringLiteral("copyholderIcon"));
        bool iconDrag = holderIcon != nullptr;
        if (x11 && holderIcon && bmButton) {
            const int before = int(proj->projectBookmarks.size());
            for (int i = int(proj->projectBookmarks.size()) - 1; i >= 0; --i)
                if (proj->projectBookmarks[i].target == QLatin1String("qb://") + tides->id)
                    proj->projectBookmarks.removeAt(i);
            const int without = int(proj->projectBookmarks.size());
            mouseDrag(holderIcon, center(holderIcon), bmButton, center(bmButton));
            iconDrag = without == before - 1 && proj->projectBookmarks.size() == before && bookmarkTitles(proj).contains(QLatin1String("Tide Tables"));
        }
        check(held && cardTitle && inHolder == QLatin1String("Tide Tables") && inEditor == QLatin1String("The Logbook") && iconDrag,
              x11 ? QStringLiteral("links can open in the copyholder; the inspector shows the copyholder's document while the keyboard is in it; its icon can be dragged (here onto the Bookmarks button)")
                  : QStringLiteral("links can open in the copyholder; the inspector shows the copyholder's document while the keyboard is in it"),
              QStringLiteral("held %1 inspector '%2' / '%3' icon %4").arg(held).arg(inHolder, inEditor).arg(iconDrag));
        pane0->closeCopyholder();
        settle(200);
    }

    // ---- closing a side-by-side split gives the text its full width back
    {
        binder->select({ships}, true);
        settle(300);
        const int full = pane0->currentEditor()->lineWrapColumnOrWidth();
        byKey(&w, QStringLiteral("splitV"))->trigger(); // both editors show the same document
        settle(500);
        const bool same = pane1->isVisible() && pane1->items() == pane0->items();
        const int half = pane0->currentEditor()->lineWrapColumnOrWidth();
        emit pane0->splitButtonClicked(pane0, false);
        settle(500);
        const int back = pane0->currentEditor()->lineWrapColumnOrWidth();
        check(same && half < full && back == full,
              QStringLiteral("after closing the split, the hidden second editor no longer narrows the text of a document both had shown"),
              QStringLiteral("full %1 half %2 back %3").arg(full).arg(half).arg(back));
    }

    // ---- comments: Alt+click opens a small window
    {
        binder->select({fog}, true);
        settle(300);
        TextEditor *ed = pane0->currentEditor();
        QTextCursor c = ed->document()->find(QStringLiteral("before dawn"));
        ed->setTextCursor(c);
        const QString note = ed->addLinkedNote(NoteKind::Comment, QStringLiteral("Which dawn?"));
        settle(200);
        c = ed->textCursor();
        c.clearSelection();
        ed->setTextCursor(c);
        const QTextCursor inNote = ed->document()->find(QStringLiteral("before dawn"));
        QTextCursor mid(ed->document());
        mid.setPosition(inNote.selectionStart() + 3);
        const bool inspectorWasVisible = inspector->isVisible();
        inspector->hide();
        QTest::mouseClick(ed->viewport(), Qt::LeftButton, Qt::AltModifier, ed->cursorRect(mid).center());
        settle(400);
        QWidget *pop = nullptr;
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (tl->objectName() == QLatin1String("notePopover") && tl->isVisible())
                pop = tl;
        auto *field = pop ? pop->findChild<QPlainTextEdit *>(QStringLiteral("notePopoverText")) : nullptr;
        const bool shown = field && field->toPlainText() == QLatin1String("Which dawn?") && !inspector->isVisible();
        if (pop)
            shot(pop, QStringLiteral("m7-07-comment-popover"));
        if (field)
            field->setPlainText(QStringLiteral("Which dawn? The second one."));
        if (pop)
            pop->close();
        settle(300);
        check(!note.isEmpty() && shown && ed->noteText(note) == QLatin1String("Which dawn? The second one."),
              QStringLiteral("Alt+click on a comment opens it in a small window at the mouse (the inspector stays closed); its text is stored when the window closes"),
              QStringLiteral("shown %1, text '%2'").arg(shown).arg(ed->noteText(note)));
        inspector->setVisible(inspectorWasVisible);
    }

    // ---- composition mode: back and forward through the history
    {
        binder->select({ships}, true);
        settle(200);
        binder->select({logbook}, true);
        settle(300);
        byKey(&w, QStringLiteral("composition"))->trigger();
        settle(600);
        CompositionWindow *comp = nullptr;
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *c = qobject_cast<CompositionWindow *>(tl))
                comp = c;
        bool back = false, forward = false, next = false, boldThere = false;
        if (comp) {
            auto shownItem = [&] { return comp->view()->items().value(0); };
            QTest::keyClick(comp, Qt::Key_BracketLeft, Qt::ControlModifier);
            settle(400);
            back = shownItem() == ships;
            QTest::keyClick(comp, Qt::Key_BracketRight, Qt::ControlModifier);
            settle(400);
            forward = shownItem() == logbook;
            QTest::keyClick(comp, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier);
            settle(400);
            next = shownItem() == fog;
            // a Format command by its shortcut (Ctrl+B) works here as well
            if (TextEditor *e = comp->view()->activeEditor()) {
                e->setFocus();
                QTextCursor c = e->document()->find(QStringLiteral("stayed"));
                e->setTextCursor(c);
                settle(200);
                QTest::keySequence(comp->windowHandle(), QKeySequence(Qt::CTRL | Qt::Key_B));
                settle(300);
                boldThere = e->document()->find(QStringLiteral("stayed")).charFormat().fontWeight() > QFont::Normal;
            }
            comp->close();
            settle(400);
        }
        check(comp && back && forward && next && boldThere,
              QStringLiteral("composition mode: Ctrl+[ and Ctrl+] go back and forth in the history, Alt+Shift+Down to the next document, Ctrl+B makes bold"),
              QStringLiteral("window %1 back %2 forward %3 next %4 bold %5").arg(comp != nullptr).arg(back).arg(forward).arg(next).arg(boldThere));
    }

    // ================================================================ QUICK REFERENCE
    QRect panelGeometry;
    {
        binder->select({logbook}, true);
        settle(300);
        byKey(&w, QStringLiteral("quickRef"))->trigger();
        settle(400);
        QuickRefWindow *qr = quickRefs().value(0);
        check(qr && qr->item() == logbook, QStringLiteral("Navigate > Open > as Quick Reference opens the panel"));
        if (!qr)
            return failures + 1;
        qr->move(1320, 40);
        qr->resize(560, 700);
        settle(300);

        // header: title, icon; footer: count, label, status
        auto *title = qr->findChild<QLineEdit *>(QStringLiteral("quickRefTitle"));
        auto *icon = qr->findChild<QToolButton *>(QStringLiteral("quickRefIcon"));
        auto *count = qr->findChild<QLabel *>(QStringLiteral("quickRefCount"));
        auto *status = qr->findChild<QComboBox *>(QStringLiteral("quickRefStatus"));
        auto *label = qr->findChild<QComboBox *>(QStringLiteral("quickRefLabel"));
        title->setFocus();
        title->setText(QStringLiteral("The Log"));
        emit title->editingFinished();
        settle(200);
        const bool renamed = logbook->title == QLatin1String("The Log") && qr->windowTitle().startsWith(QLatin1String("The Log"));
        title->setText(QStringLiteral("The Logbook"));
        emit title->editingFinished();
        qr->textView()->activeEditor()->setFocus();
        settle(200);
        bool statusSet = false;
        if (status && status->count() > 1) {
            status->setCurrentIndex(1);
            emit status->activated(1);
            statusSet = logbook->status == status->itemData(1).toInt();
        }
        check(title && icon && count && label && renamed && logbook->title == QLatin1String("The Logbook") && statusSet
                  && count->text().endsWith(QLatin1String("words")) && !count->text().startsWith(QLatin1Char('0')),
              QStringLiteral("a panel shows the title (which renames), the word count, label and status of its document"),
              QStringLiteral("renamed %1 status %2 count '%3'").arg(renamed).arg(statusSet).arg(count ? count->text() : QString()));

        // lower pane: synopsis, notes, keywords, metadata
        auto *panelButton = qr->findChild<QToolButton *>(QStringLiteral("quickRefPanelButton"));
        const QStringList panelMenu = panelButton && panelButton->menu() ? menuTexts(panelButton->menu()) : QStringList();
        qr->setPanel(QuickRefWindow::Synopsis);
        settle(200);
        auto *syn = qr->findChild<QPlainTextEdit *>(QStringLiteral("quickRefSynopsis"));
        syn->setPlainText(QStringLiteral("He corrects the count."));
        settle(200);
        const bool synopsis = syn->isVisible() && logbook->synopsis == QLatin1String("He corrects the count.");
        qr->setPanel(QuickRefWindow::Notes);
        settle(200);
        auto *notes = qr->findChild<TextEditor *>(QStringLiteral("quickRefNotes"));
        notes->setFocus();
        QTest::keyClicks(notes, QStringLiteral("Check the date."));
        settle(200);
        const bool notesShared = notes->isVisible() && proj->docs()->notes(logbook->id)->toPlainText() == QLatin1String("Check the date.");
        qr->setPanel(QuickRefWindow::Keywords);
        settle(200);
        auto *kwList = qr->findChild<QListWidget *>(QStringLiteral("quickRefKeywords"));
        const int kwBefore = kwList->count();
        const bool tookKw = dropKeywords(kwList->viewport(), QPoint(20, 10), proj, {kwMary});
        const bool keywords = tookKw && kwList->count() == kwBefore + 1 && logbook->keywords.contains(kwMary);
        qr->setPanel(QuickRefWindow::Metadata);
        settle(200);
        auto *include = qr->findChild<QCheckBox *>(QStringLiteral("quickRefInclude"));
        auto *section = qr->findChild<QComboBox *>(QStringLiteral("quickRefSection"));
        bool metadata = include && section && include->isVisible() && include->isChecked() && include->isEnabled() && section->count() >= 1;
        if (include) {
            include->setChecked(false);
            settle(200);
            metadata = metadata && !logbook->includeInCompile;
            include = qr->findChild<QCheckBox *>(QStringLiteral("quickRefInclude")); // (the form is built anew after a change)
            include->setChecked(true);
            settle(200);
        }
        check(panelMenu == QStringList({QStringLiteral("Document Only"), QStringLiteral("Synopsis"), QStringLiteral("Notes"), QStringLiteral("Keywords"), QStringLiteral("Metadata")})
                  && synopsis && notesShared && keywords && metadata && logbook->includeInCompile,
              QStringLiteral("the button at the top right of a panel shows synopsis, notes, keywords or metadata below the text; changes there change the document"),
              QStringLiteral("menu %1 synopsis %2 notes %3 keywords %4 metadata %5").arg(panelMenu.size()).arg(synopsis).arg(notesShared).arg(keywords).arg(metadata));
        qr->setPanel(QuickRefWindow::Notes);

        // bookmarks sidebar
        proj->projectBookmarks.clear();
        proj->projectBookmarks.append({QStringLiteral("Tide Tables"), QLatin1String("qb://") + tides->id});
        proj->projectBookmarks.append({QStringLiteral("Morning Fog"), QLatin1String("qb://") + fog->id});
        proj->projectBookmarks.append({QStringLiteral("Tide predictions"), QStringLiteral("https://example.org/tides")});
        proj->notifyBookmarksChanged();
        auto *toggle = qr->findChild<QToolButton *>(QStringLiteral("quickRefBookmarkToggle"));
        const int widthBefore = qr->width();
        toggle->click();
        settle(400);
        BookmarkListWidget *bms = qr->bookmarkList();
        const bool sidebar = qr->bookmarksShown() && bms->isVisible() && bms->count() == 3 && qr->width() > widthBefore
                             && bms->currentRow() == -1 && qr->item() == logbook;
        QTest::mouseClick(bms->viewport(), Qt::LeftButton, Qt::NoModifier, bms->visualItemRect(bms->item(0)).center());
        settle(300);
        const bool navigates = qr->item() == tides && qr->windowTitle().startsWith(QLatin1String("Tide Tables"))
                               && notes->document() == proj->docs()->notes(tides->id) && pane0->items() == QList<BinderItem *>({logbook});
        bms->setCurrentRow(1);
        settle(300);
        const bool byKeyboard = qr->item() == fog;
        bms->setCurrentRow(2); // a web link: nothing to show in the panel
        settle(200);
        const bool linkKeeps = qr->item() == fog;
        check(toggle && sidebar && navigates && byKeyboard && linkKeeps,
              QStringLiteral("the bookmark button in the footer opens the project bookmarks beside the document; a click on one shows it in the panel (the main editor keeps its document)"),
              QStringLiteral("sidebar %1 navigates %2 keyboard %3 link %4").arg(sidebar).arg(navigates).arg(byKeyboard).arg(linkKeeps));
        shot(qr, QStringLiteral("m7-08-quick-reference-panel"));

        // reorder, add by drop, new document, remove
        bms->moveBookmark(1, 0);
        const bool reordered = bookmarkTitles(proj) == QLatin1String("Morning Fog, Tide Tables, Tide predictions") && bms->item(0)->text() == QLatin1String("Morning Fog");
        dropItems(bms->viewport(), bms->visualItemRect(bms->item(0)).topLeft() + QPoint(10, 1), proj, {ships});
        const bool dropped = bookmarkTitles(proj).startsWith(QLatin1String("Counting Ships, Morning Fog"));
        BinderItem *notesFolder = m->createItem(proj->research(), -1, ItemType::Folder, QStringLiteral("Notes"));
        const bool folders = QuickRefWindow::noteFolders(proj).contains(notesFolder) && QuickRefWindow::noteFolders(proj).contains(proj->research())
                             && !QuickRefWindow::noteFolders(proj).contains(proj->trash());
        BinderItem *idea = qr->createBookmarkedDocument(notesFolder, QStringLiteral("Idea"));
        settle(300);
        const bool created = idea && idea->parent == notesFolder && qr->item() == idea && bookmarkTitles(proj).endsWith(QLatin1String("Idea"))
                             && bms->currentRow() == bms->count() - 1;
        auto *removeButton = qr->findChild<QToolButton *>(QStringLiteral("quickRefRemoveBookmark"));
        removeButton->click();
        settle(200);
        const bool removed = !bookmarkTitles(proj).contains(QLatin1String("Idea")) && proj->item(idea->id) == idea && qr->item() == idea;
        // the inspector's list of project bookmarks shows the same
        inspector->showTab(Inspector::Bookmarks);
        settle(200);
        const bool inspectorSame = inspector->projectBookmarkList()->count() == proj->projectBookmarks.size()
                                   && inspector->projectBookmarkList()->item(0)->text() == QLatin1String("Counting Ships");
        check(reordered && dropped && folders && created && removed && inspectorSame,
              QStringLiteral("in the sidebar, bookmarks can be reordered, documents dropped in, a new bookmarked document created in a chosen folder, and bookmarks removed (the document stays)"),
              QStringLiteral("reordered %1 dropped %2 folders %3 created %4 removed %5 inspector %6: %7").arg(reordered).arg(dropped).arg(folders).arg(created).arg(removed).arg(inspectorSame).arg(bookmarkTitles(proj)));

        // Project Settings: the folder and "ask each time"
        {
            ProjectSettingsDialog ps(proj, ProjectSettingsDialog::SpecialFolders, &w);
            auto *ask = ps.findChild<QCheckBox *>(QStringLiteral("notesFolderAsk"));
            check(ask && ask->isChecked(), QStringLiteral("Project Settings > Special Folders has the folder for new bookmarked documents and \"ask each time\""));
        }

        // a document dropped on the panel's header bar; the panel's icon dragged away
        QWidget *qrHeader = qr->findChild<QWidget *>(QStringLiteral("quickRefHeader"));
        const bool headerTakes = dropItems(qrHeader, center(qrHeader), proj, {logbook}) && qr->item() == logbook;
        bool iconDrags = true;
        if (x11) {
            w.raise();
            qr->raise();
            pane0->showItems({ships});
            settle(300);
            mouseDrag(icon, center(icon), header0, center(header0));
            iconDrags = pane0->items() == QList<BinderItem *>({logbook}) && qr->item() == logbook;
            // and from the binder onto the panel's header bar
            tree->expandAll();
            settle(200);
            mouseDrag(tree->viewport(), rowCenter(fog), qrHeader, center(qrHeader));
            iconDrags = iconDrags && qr->item() == fog && fog->parent == chapter;
            qr->showItem(logbook);
            settle(300);
        }
        check(headerTakes && iconDrags,
              x11 ? QStringLiteral("with the mouse: the panel's icon dragged to an editor's header bar loads the document there; a binder document dragged to the panel's header bar is shown in the panel")
                  : QStringLiteral("a document dropped on a panel's header bar is shown in the panel"),
              QStringLiteral("header %1 icon %2 (editor: %3, panel: %4)").arg(headerTakes).arg(iconDrags).arg(titles(pane0->items()), qr->item()->title));

        // shortcuts of Format commands work in the panel
        TextEditor *qe = qr->textView()->editors().value(0);
        bool boldInPanel = false;
        if (qe) {
            qr->activateWindow();
            qe->setFocus();
            settle(300);
            QTextCursor c = qe->document()->find(QStringLiteral("margin"));
            qe->setTextCursor(c);
            settle(100);
            QTest::keySequence(qr->windowHandle(), QKeySequence(Qt::CTRL | Qt::Key_B));
            settle(300);
            boldInPanel = qe->document()->find(QStringLiteral("margin")).charFormat().fontWeight() > QFont::Normal;
        }
        check(boldInPanel, QStringLiteral("Ctrl+B in a Quick Reference panel makes the selected text of the panel bold"));

        // the header menu
        QStringList headerEntries;
        QTimer::singleShot(300, &w, [&] {
            if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                headerEntries = menuTexts(menu);
                for (QAction *a : menu->actions())
                    if (menuText(a->text()) == QLatin1String("Open in the Editor"))
                        a->trigger();
                menu->close();
            }
        });
        pane0->showItems({ships});
        icon->click();
        settle(400);
        check(headerEntries.contains(QStringLiteral("Reveal in Binder")) && headerEntries.contains(QStringLiteral("Add to Project Bookmarks"))
                  && pane0->items() == QList<BinderItem *>({logbook}),
              QStringLiteral("a click on the panel's icon offers Reveal in Binder, Open in the Editor and the project bookmarks"),
              headerEntries.join(QLatin1String(" | ")));
        panelGeometry = qr->geometry();
    }

    // ---- panels and history come back with the project
    {
        // history to come back to
        binder->select({ships}, true);
        settle(150);
        binder->select({fog}, true);
        settle(150);
        binder->select({logbook}, true);
        settle(300);
        const int openPanels = int(quickRefs().size());
        w.closeProject();
        settle(400);
        const bool closed = quickRefs().isEmpty();
        const bool reopened = w.openProject(projectFile);
        settle(900);
        proj = w.project();
        m = proj ? proj->model() : nullptr;
        if (!proj)
            return failures + 1;
        QuickRefWindow *qr = quickRefs().value(0);
        BinderItem *logbook2 = nullptr, *fog2 = nullptr;
        for (BinderItem *it : proj->allItems()) {
            if (it->title == QLatin1String("The Logbook"))
                logbook2 = it;
            if (it->title == QLatin1String("Morning Fog"))
                fog2 = it;
        }
        const bool panelBack = openPanels == 1 && closed && reopened && qr && qr->item() == logbook2 && qr->panel() == QuickRefWindow::Notes
                               && qr->bookmarksShown() && qr->size() == panelGeometry.size();
        check(panelBack, QStringLiteral("the Quick Reference panel is back after reopening the project: document, size, lower pane and sidebar"),
              QStringLiteral("open %1 closed %2 reopened %3 panel %4 (%5 %6 %7x%8, was %9x%10)").arg(openPanels).arg(closed).arg(reopened)
                  .arg(qr != nullptr).arg(qr ? int(qr->panel()) : -1).arg(qr && qr->bookmarksShown()).arg(qr ? qr->width() : 0).arg(qr ? qr->height() : 0)
                  .arg(panelGeometry.width()).arg(panelGeometry.height()));
        const bool historyBack = pane0->items() == QList<BinderItem *>({logbook2}) && pane0->canGoBack();
        pane0->goBack();
        settle(300);
        check(historyBack && pane0->items() == QList<BinderItem *>({fog2}) && pane0->canGoForward(),
              QStringLiteral("the history of the editor is kept with the project: Back works after reopening"),
              QStringLiteral("back possible %1, shows %2").arg(historyBack).arg(titles(pane0->items())));

        // a panel whose document is deleted for good closes
        BinderItem *gone = m->createItem(proj->research(), -1, ItemType::Text, QStringLiteral("Scrap"));
        closeQuickRefs();
        byKey(&w, QStringLiteral("closeQuickRefs"))->trigger();
        settle(200);
        binder->select({gone}, false);
        settle(200);
        byKey(&w, QStringLiteral("quickRef"))->trigger();
        settle(300);
        binder->select({logbook2}, true); // (the editor shows something else)
        settle(200);
        const bool opened = quickRefs().size() == 1 && quickRefs().first()->item() == gone;
        m->moveToTrash({gone});
        settle(200);
        const bool stillOpen = quickRefs().size() == 1;
        m->emptyTrash();
        settle(400);
        check(opened && stillOpen && quickRefs().isEmpty(),
              QStringLiteral("a panel stays open for a document in the trash and closes when the trash is emptied"),
              QStringLiteral("opened %1 still %2 now %3").arg(opened).arg(stillOpen).arg(quickRefs().size()));

        // switched off in the options: no panels after reopening
        binder->select({logbook2}, true);
        settle(200);
        byKey(&w, QStringLiteral("quickRef"))->trigger();
        settle(300);
        as.reopenQuickRefs = false;
        w.closeProject();
        settle(300);
        w.openProject(projectFile);
        settle(800);
        check(quickRefs().isEmpty() && w.project(), QStringLiteral("with the option off, panels are not reopened"));
        as.reopenQuickRefs = true;
        shot(&w, QStringLiteral("m7-09-reopened"));
    }

    w.closeProject();
    settle(200);
    delete wp;
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
