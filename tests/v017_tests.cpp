// Scripted run for version 0.17: the binder (narrower steps, horizontal
// scroll bar), Navigate > Fonts, the color buttons of the format bar, the
// icon set of Documents > Change Icon (label tint, Scrivener names, own
// icons), the icon bar, comments and footnotes of a folder, the mind maps
// (Scapple functions, file, Scrivener format) and Help > Developer Guide.
// Saves screenshots into the folder given as first argument.
// Exit code = number of failed checks.
//
//   QT_QPA_PLATFORM=offscreen ./qb_v017_tests /tmp/shots
#include "compile/PluginManager.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/IconCatalog.h"
#include "core/ItemIcons.h"
#include "core/MindMap.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "core/Zip.h"
#include "import/ScrivenerFormat.h"
#include "import/ScrivenerImporter.h"
#include "text/TextProps.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/ColorPickButton.h"
#include "ui/EditorPane.h"
#include "ui/FontInstall.h"
#include "ui/IconDialogs.h"
#include "ui/Inspector.h"
#include "ui/MainWindow.h"
#include "ui/MindMapController.h"
#include "ui/MindMapDoc.h"
#include "ui/MindMapScene.h"
#include "ui/MindMapUi.h"
#include "ui/MindMapView.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QRegularExpression>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPrinter>
#include <QPushButton>
#include <QScrollBar>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <QUrl>
#include <cstdio>
#include <functional>

using namespace qb;
using namespace qb::mindmap;

static int failures = 0;
static QString outDir;
static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail.left(2000)));
    }
    std::fflush(stdout);
}

static void info(const QString &s)
{
    std::printf("      %s\n", qPrintable(s));
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
    settle(250);
    w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}

static QAction *byKey(QWidget *w, const QString &key) { return w->findChild<QAction *>(key); }

template<typename T>
static void withModal(int delayMs, std::function<void(T *)> fn)
{
    QTimer::singleShot(delayMs, qApp, [fn] {
        if (auto *m = qobject_cast<T *>(QApplication::activeModalWidget())) {
            fn(m);
            return;
        }
        info(QStringLiteral("(the expected window (%1) did not open)").arg(QString::fromLatin1(T::staticMetaObject.className())));
        if (QWidget *m = QApplication::activeModalWidget())
            m->close();
    });
}

static void mouse(QWidget *vp, QEvent::Type t, const QPoint &pos, Qt::MouseButton b, Qt::MouseButtons bs,
                  Qt::KeyboardModifiers m = Qt::NoModifier)
{
    QMouseEvent ev(t, QPointF(pos), QPointF(vp->mapToGlobal(pos)), b, bs, m);
    QApplication::sendEvent(vp, &ev);
}

static void drag(QWidget *vp, const QPoint &from, const QPoint &to, Qt::KeyboardModifiers m = Qt::NoModifier)
{
    mouse(vp, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, m);
    const int steps = 8;
    for (int i = 1; i <= steps; ++i)
        mouse(vp, QEvent::MouseMove, from + (to - from) * i / steps, Qt::NoButton, Qt::LeftButton, m);
    mouse(vp, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, m);
    settle(60);
}

static void doubleClick(QWidget *vp, const QPoint &pos, Qt::KeyboardModifiers m = Qt::NoModifier)
{
    mouse(vp, QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton, m);
    mouse(vp, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton, m);
    mouse(vp, QEvent::MouseButtonDblClick, pos, Qt::LeftButton, Qt::LeftButton, m);
    mouse(vp, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton, m);
    settle(60);
}

static QStringList menuTexts(QMenu *m)
{
    QStringList l;
    for (QAction *a : m->actions())
        if (!a->isSeparator() && !a->text().isEmpty())
            l << QString(a->text()).remove(QLatin1Char('&'));
    return l;
}

static QMenu *subMenu(QMenu *m, const QString &text)
{
    for (QAction *a : m->actions())
        if (a->menu() && QString(a->text()).remove(QLatin1Char('&')) == text)
            return a->menu();
    return nullptr;
}

static QMenu *menuBarMenu(QMenuBar *mb, const QString &text)
{
    for (QAction *a : mb->actions())
        if (QString(a->text()).remove(QLatin1Char('&')) == text)
            return a->menu();
    return nullptr;
}

static QColor colorAtWord(QTextDocument *doc, const QString &word)
{
    const QTextCursor f = doc->find(word);
    if (f.isNull())
        return QColor();
    QTextCursor c(doc);
    c.setPosition(f.selectionStart() + 1);
    return c.charFormat().foreground().color();
}

static bool highlightedAt(QTextDocument *doc, const QString &word)
{
    const QTextCursor f = doc->find(word);
    if (f.isNull())
        return false;
    QTextCursor c(doc);
    c.setPosition(f.selectionStart() + 1);
    return c.charFormat().property(props::Highlight).toBool();
}

static TextEditor *editorShowing(MainWindow &w, const QString &needle)
{
    for (TextEditor *e : w.findChildren<TextEditor *>())
        if (e->isVisible() && !e->isReadOnly() && e->item() && e->toPlainText().contains(needle))
            return e;
    return nullptr;
}

static void selectWord(TextEditor *e, const QString &word)
{
    const QTextCursor f = e->document()->find(word);
    e->setTextCursor(f);
}

// a font file of the system that the tests may install as a copy
static QString someFontFile()
{
    for (const QString &p : {QStringLiteral("/usr/share/fonts/truetype/freefont/FreeSerif.ttf"),
                             QStringLiteral("/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"),
                             QStringLiteral("/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf")})
        if (QFile::exists(p))
            return p;
    return QString();
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qputenv("QB_FONT_MANAGER", "none");
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV015Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV015Test"));
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
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();

    QTemporaryDir work;
    templates::Info novel;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("novel"))
            novel = t;
    QString err;
    auto proj = templates::createProject(novel, work.path(), QStringLiteral("The Lighthouse"), &err);
    check(proj != nullptr, QStringLiteral("(project made)"), err);
    if (!proj)
        return 1;
    Project *p = proj.get();
    BinderItem *ch = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Chapter One"));
    BinderItem *d1 = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("The Storm"));
    BinderItem *d2 = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("The Calm"));
    BinderItem *d3 = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("The Ships"));
    BinderItem *deep = p->model()->createItem(ch, -1, ItemType::Folder, QStringLiteral("Scenes Below"));
    BinderItem *d4 = p->model()->createItem(deep, -1, ItemType::Text,
                                            QStringLiteral("A scene with a rather long title that does not fit into a narrow binder at all"));
    QTextCursor(p->docs()->text(d1->id)).insertText(QStringLiteral("The wind rose in the night and the sea came up over the rocks."));
    QTextCursor(p->docs()->text(d2->id)).insertText(QStringLiteral("By morning the boats were gone and the keeper counted the gulls."));
    QTextCursor(p->docs()->text(d3->id)).insertText(QStringLiteral("Three ships passed the reef at noon."));
    QTextCursor(p->docs()->text(d4->id)).insertText(QStringLiteral("Deep below, the cellar held the old lamps."));
    p->save(&err);

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1500, 980);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(proj)), QStringLiteral("(project open)"));
    settle(400);
    auto *binder = w.findChild<BinderPanel *>();
    auto *inspector = w.findChild<Inspector *>();
    check(binder && inspector, QStringLiteral("(window parts found)"));
    if (!binder || !inspector)
        return 1;
    BinderTree *tree = binder->tree();
    for (BinderItem *it : {ch, deep})
        tree->expand(p->model()->indexFor(it));
    settle(200);

    // ============================================================ 1. binder
    {
        check(tree->indentation() == 12, QStringLiteral("Binder: steps of 12 px per level (Qt's standard 20 px; about 60 %)"),
              QString::number(tree->indentation()));
        // a narrow binder: the long title is reached with a horizontal scroll bar
        QWidget *binderDock = binder;
        const int before = binderDock->width();
        binderDock->setMinimumWidth(0);
        binderDock->setMaximumWidth(220);
        settle(400);
        tree->updateColumnWidth();
        settle(100);
        const int col = tree->header()->sectionSize(0);
        const int view = tree->viewport()->width();
        QScrollBar *hb = tree->horizontalScrollBar();
        check(col > view && hb->maximum() > 0 && col >= tree->contentWidth(),
              QStringLiteral("Binder: a title wider than the binder makes the column wider and gives a horizontal scroll bar"),
              QStringLiteral("column %1, view %2, content %3, scroll max %4").arg(col).arg(view).arg(tree->contentWidth()).arg(hb->maximum()));
        shot(binderDock, QStringLiteral("v017-01-binder-narrow"));
        hb->setValue(hb->maximum());
        shot(binderDock, QStringLiteral("v017-02-binder-scrolled"));
        hb->setValue(0);
        binderDock->setMaximumWidth(QWIDGETSIZE_MAX);
        settle(300);
        tree->updateColumnWidth();
        check(tree->header()->sectionSize(0) >= tree->viewport()->width(),
              QStringLiteral("Binder: in a wide binder the column fills the view (label dots on the right as before)"));
        Q_UNUSED(before);
        // the option
        as.binderIndent = 8;
        as.notifyChanged();
        settle(100);
        check(tree->indentation() == 8, QStringLiteral("Binder: the indentation follows File > Options > Appearance > Binder"));
        as.binderIndent = 12;
        as.notifyChanged();
    }

    // ============================================================ 2. fonts
    {
        QMenu *nav = menuBarMenu(w.menuBar(), QStringLiteral("Navigate"));
        QMenu *fonts = nav ? subMenu(nav, QStringLiteral("Fonts")) : nullptr;
        check(fonts && menuTexts(fonts) == QStringList({QStringLiteral("Install Font Files…"), QStringLiteral("Open Font Folder"),
                                                         QStringLiteral("Open Font Manager of the Desktop"), QStringLiteral("Reload Font List")}),
              QStringLiteral("Navigate > Fonts: Install Font Files, Open Font Folder, Open Font Manager, Reload Font List"),
              fonts ? menuTexts(fonts).join(QStringLiteral(" | ")) : QStringLiteral("no menu"));
        const QString src = someFontFile();
        if (src.isEmpty()) {
            info(QStringLiteral("(no system font file to try the installation with)"));
        } else {
            // a font file and a ZIP archive with one inside, and a file that is no font
            QTemporaryDir fdir;
            QFile::copy(src, fdir.path() + QStringLiteral("/MyFont.ttf"));
            {
                ZipWriter z(fdir.path() + QStringLiteral("/fonts.zip"));
                QFile f(src);
                f.open(QIODevice::ReadOnly);
                z.addFile(QStringLiteral("family/Other-Regular.ttf"), f.readAll());
                z.addFile(QStringLiteral("README.txt"), "read me");
                z.close();
            }
            QFile bad(fdir.path() + QStringLiteral("/broken.otf"));
            bad.open(QIODevice::WriteOnly);
            bad.write("this is not a font at all, only some words in a file");
            bad.close();
            const fontinstall::Result r = fontinstall::install(
                {fdir.path() + QStringLiteral("/MyFont.ttf"), fdir.path() + QStringLiteral("/fonts.zip"), fdir.path() + QStringLiteral("/broken.otf")});
            const QString dir = fontinstall::userFontDir();
            check(dir.startsWith(home.path()) && QFile::exists(dir + QStringLiteral("/MyFont.ttf")) && QFile::exists(dir + QStringLiteral("/Other-Regular.ttf")),
                  QStringLiteral("Fonts: font files and fonts inside a ZIP archive are copied into ~/.local/share/fonts"),
                  dir + QStringLiteral(": ") + QDir(dir).entryList(QDir::Files).join(QStringLiteral(", ")));
            check(!r.families.isEmpty() && QFontDatabase::families().contains(r.families.first()),
                  QStringLiteral("Fonts: the new families are in the font lists at once"), r.families.join(QStringLiteral(", ")));
            check(r.skipped.size() == 1 && r.skipped.first().startsWith(QStringLiteral("broken.otf")) && !QFile::exists(dir + QStringLiteral("/broken.otf")),
                  QStringLiteral("Fonts: a file that is no font is refused and not left behind"), r.skipped.join(QStringLiteral("; ")));
            // through the menu: the file dialog, then the report
            withModal<QDialog>(400, [&](QDialog *d) {
                // Qt's own file dialog (tests): type the file name
                if (auto *le = d->findChild<QLineEdit *>(QStringLiteral("fileNameEdit")))
                    le->setText(fdir.path() + QStringLiteral("/MyFont.ttf"));
                QTimer::singleShot(400, qApp, [] {
                    if (auto *mb = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                        mb->grab().save(outDir + QStringLiteral("/v017-03-fonts-installed.png"));
                        mb->accept();
                    }
                });
                QMetaObject::invokeMethod(d, "accept", Qt::QueuedConnection);
            });
            byKey(&w, QStringLiteral("installFonts"))->trigger();
            settle(300);
            check(QFile::exists(outDir + QStringLiteral("/v017-03-fonts-installed.png")),
                  QStringLiteral("Fonts: Navigate > Fonts > Install Font Files asks for the files and reports what was installed"));
            // no font manager on this machine: a message with the folder instead
            QString msg;
            withModal<QMessageBox>(300, [&](QMessageBox *mb) {
                msg = mb->text();
                mb->accept();
            });
            byKey(&w, QStringLiteral("fontManager"))->trigger();
            settle(200);
            check(msg.contains(dir), QStringLiteral("Fonts: without a font manager on the computer the message names the font folder"), msg);
        }
    }

    // ============================================================ 3. color buttons of the format bar
    {
        binder->select({d1}, true);
        settle(400);
        auto *formatBar = w.findChild<QToolBar *>(QStringLiteral("formatBar"));
        auto *textBtn = formatBar ? formatBar->findChild<ColorPickButton *>(QStringLiteral("textColorButton")) : nullptr;
        auto *hiBtn = formatBar ? formatBar->findChild<ColorPickButton *>(QStringLiteral("highlightButton")) : nullptr;
        check(textBtn && hiBtn && textBtn->isVisible() && hiBtn->isVisible(),
              QStringLiteral("Format bar: the highlight and text color buttons are color buttons"));
        TextEditor *e = editorShowing(w, QStringLiteral("wind rose"));
        check(e != nullptr, QStringLiteral("(editor with The Storm)"));
        if (textBtn && hiBtn && e) {
            // the mouse rests on the button: the colors open below it
            textBtn->showPopup();
            settle(200);
            ColorPickPopup *pop = textBtn->popup();
            QStringList names;
            for (QToolButton *b : pop->swatches())
                names << b->toolTip();
            check(pop && pop->isVisible() && names.mid(0, 6) == QStringList({QStringLiteral("Red"), QStringLiteral("Blue"), QStringLiteral("Yellow"),
                                                                             QStringLiteral("Green"), QStringLiteral("White"), QStringLiteral("Black")})
                      && pop->geometry().top() >= textBtn->mapToGlobal(QPoint(0, textBtn->height())).y() - 1,
                  QStringLiteral("Format bar: resting on the text color button shows red, blue, yellow, green, white, black below it"),
                  names.join(QStringLiteral(", ")));
            QPixmap both = w.grab(QRect(formatBar->mapTo(&w, QPoint(0, 0)), QSize(formatBar->width(), formatBar->height() + 60)));
            {
                QPainter pp(&both);
                const QPoint at = pop->mapToGlobal(QPoint(0, 0)) - w.mapToGlobal(formatBar->mapTo(&w, QPoint(0, 0)));
                pp.drawPixmap(at, pop->grab());
            }
            both.save(outDir + QStringLiteral("/v017-04-text-color-popup.png"));
            // a color for the selected word
            selectWord(e, QStringLiteral("wind"));
            pop->swatches().at(1)->click(); // blue
            settle(150);
            check(!pop->isVisible() && colorAtWord(e->document(), QStringLiteral("wind")) == ColorPickButton::palette().at(1).second
                      && textBtn->color() == ColorPickButton::palette().at(1).second,
                  QStringLiteral("... a click on Blue colors the selected word and the button shows blue"),
                  colorAtWord(e->document(), QStringLiteral("wind")).name());
            // without a selection: the text typed next
            QTextCursor c = e->textCursor();
            c.movePosition(QTextCursor::End);
            e->setTextCursor(c);
            textBtn->showPopup();
            textBtn->popup()->swatches().at(0)->click(); // red
            QTest::keyClicks(e, QStringLiteral(" Redword"));
            settle(100);
            check(colorAtWord(e->document(), QStringLiteral("Redword")) == ColorPickButton::palette().at(0).second
                      && colorAtWord(e->document(), QStringLiteral("rocks")) != ColorPickButton::palette().at(0).second,
                  QStringLiteral("... without a selection the color applies to the text typed next (the rest stays)"));
            // the button itself applies its color
            selectWord(e, QStringLiteral("night"));
            textBtn->click();
            settle(100);
            check(colorAtWord(e->document(), QStringLiteral("night")) == ColorPickButton::palette().at(0).second,
                  QStringLiteral("... a click on the button applies the color it shows"));
            // highlight
            selectWord(e, QStringLiteral("sea"));
            hiBtn->showPopup();
            hiBtn->popup()->swatches().at(3)->click(); // green
            settle(100);
            check(highlightedAt(e->document(), QStringLiteral("sea")) && hiBtn->color() == ColorPickButton::palette().at(3).second,
                  QStringLiteral("Format bar: the highlight button highlights the selection in the color chosen (green) and shows it"));
            hiBtn->showPopup();
            const QList<QToolButton *> sw = hiBtn->popup()->swatches();
            sw.at(6)->click(); // no highlight
            settle(100);
            check(!highlightedAt(e->document(), QStringLiteral("sea")), QStringLiteral("... \"No Highlight\" takes it away"));
            AppSettings::instance().load();
            check(!AppSettings::instance().barHighlightColor.isValid() && !hiBtn->color().isValid(),
                  QStringLiteral("... and is remembered as \"no highlight\" (not as black) for the next start"),
                  AppSettings::instance().barHighlightColor.name());
            hiBtn->setColor(QColor(255, 221, 0));
            AppSettings::instance().barHighlightColor = QColor(255, 221, 0);
            AppSettings::instance().save();
            check(AppSettings::instance().barTextColor == ColorPickButton::palette().at(0).second,
                  QStringLiteral("... the colors chosen are kept for the next start"));
            shot(formatBar, QStringLiteral("v017-05-format-bar-colors"));
            e->document()->undo();
        }
    }

    // ============================================================ 4. icons
    {
        // every icon of the catalog exists as a picture, in both icon themes and all sizes
        QStringList missing;
        for (const icons::CatalogEntry &ce : icons::catalog())
            for (const char *t : {"qbicons", "qbicons-dark"})
                for (int s : {16, 22, 32, 48, 64})
                    if (!QFile::exists(QStringLiteral(":/icons/%1/status/%2/%3.svg").arg(QLatin1String(t)).arg(s).arg(ce.id)))
                        missing << ce.id;
        check(missing.isEmpty() && icons::catalog().size() == 76,
              QStringLiteral("Icons: %1 icons, each drawn for 16 to 64 px in both icon themes").arg(icons::catalog().size()),
              missing.join(QStringLiteral(", ")));
        // the menu: as in Scrivener's
        binder->select({d2}, true);
        settle(200);
        QMenu *docs = menuBarMenu(w.menuBar(), QStringLiteral("Documents"));
        QMenu *iconMenu = docs ? subMenu(docs, QStringLiteral("Change Icon")) : nullptr;
        check(iconMenu != nullptr, QStringLiteral("(Documents > Change Icon)"));
        if (iconMenu) {
            emit iconMenu->aboutToShow();
            const QStringList top = menuTexts(iconMenu);
            check(top.mid(0, 3) == QStringList({QStringLiteral("Reset Icon to Default"), QStringLiteral("Icon from Text…"), QStringLiteral("Manage Icons…")}),
                  QStringLiteral("Change Icon: Reset Icon to Default, Icon from Text, Manage Icons at the top"), top.mid(0, 4).join(QStringLiteral(" | ")));
            QStringList groups;
            for (const QString &g : {QStringLiteral("Book"), QStringLiteral("Characters"), QStringLiteral("Flag"), QStringLiteral("Locations"),
                                     QStringLiteral("Notes"), QStringLiteral("To Do"), QStringLiteral("TV"), QStringLiteral("Way-Station")})
                if (subMenu(iconMenu, g))
                    groups << g;
            check(groups.size() == 8, QStringLiteral("Change Icon: the submenus Book, Characters, Flag, Locations, Notes, To Do, TV, Way-Station"),
                  groups.join(QStringLiteral(", ")));
            const QStringList book = menuTexts(subMenu(iconMenu, QStringLiteral("Book")));
            const QStringList flag = menuTexts(subMenu(iconMenu, QStringLiteral("Flag")));
            const QStringList notes = menuTexts(subMenu(iconMenu, QStringLiteral("Notes")));
            check(book == QStringList({QStringLiteral("Blue Book"), QStringLiteral("Blue Notebook"), QStringLiteral("Brown Book"), QStringLiteral("Composition"),
                                       QStringLiteral("Green Book"), QStringLiteral("Green Notebook"), QStringLiteral("Orange Notebook"),
                                       QStringLiteral("Red Book"), QStringLiteral("Yellow Book")}),
                  QStringLiteral("... Book: the nine entries of the screenshot, in its order"), book.join(QStringLiteral(", ")));
            check(flag == QStringList({QStringLiteral("Blue Flag"), QStringLiteral("Green Flag"), QStringLiteral("Orange Flag"), QStringLiteral("Purple Flag"),
                                       QStringLiteral("Red Flag"), QStringLiteral("White Flag"), QStringLiteral("Yellow Flag")}),
                  QStringLiteral("... Flag: Blue, Green, Orange, Purple, Red, White, Yellow"), flag.join(QStringLiteral(", ")));
            check(notes == QStringList({QStringLiteral("Blue Notepad"), QStringLiteral("Green Notepad"), QStringLiteral("Red Notepad"),
                                        QStringLiteral("White Notepad"), QStringLiteral("Yellow Notepad")}),
                  QStringLiteral("... Notes: five notepads"), notes.join(QStringLiteral(", ")));
            int flat = 0;
            for (const QString &t : {QStringLiteral("Back Matter"), QStringLiteral("Blackboard"), QStringLiteral("Calendar"), QStringLiteral("Clapper Board"),
                                     QStringLiteral("Clock"), QStringLiteral("Cog"), QStringLiteral("Eye"), QStringLiteral("Figures and Graphs"),
                                     QStringLiteral("Filing Cabinet"), QStringLiteral("Front Matter"), QStringLiteral("Information"), QStringLiteral("Lectern"),
                                     QStringLiteral("Light Bulb"), QStringLiteral("Magnifying Glass"), QStringLiteral("Music"), QStringLiteral("Musical Notes"),
                                     QStringLiteral("Outline"), QStringLiteral("Pencil"), QStringLiteral("Proofread"), QStringLiteral("Question Mark"),
                                     QStringLiteral("Reviewer Comments"), QStringLiteral("Scissors"), QStringLiteral("Speech Bubble"),
                                     QStringLiteral("Speech Marks"), QStringLiteral("Statistics"), QStringLiteral("Sync Folder"), QStringLiteral("Tables"),
                                     QStringLiteral("Test Tube"), QStringLiteral("Thought Bubble"), QStringLiteral("Warning")})
                if (top.contains(t))
                    ++flat;
            check(flat == 30, QStringLiteral("Change Icon: the 30 icons below the submenus (Back Matter ... Warning)"), QString::number(flat));
            // choosing one
            QAction *greenBook = nullptr;
            for (QAction *a : subMenu(iconMenu, QStringLiteral("Book"))->actions())
                if (a->data().toString() == QLatin1String("qb-book-green"))
                    greenBook = a;
            greenBook->trigger();
            settle(100);
            check(d2->icon == QLatin1String("qb-book-green"), QStringLiteral("Change Icon: a click gives the selected document the icon"));
            emit iconMenu->aboutToShow();
            const QStringList again = menuTexts(iconMenu);
            check(again.indexOf(QStringLiteral("Green Book")) > 2 && again.indexOf(QStringLiteral("Green Book")) < again.indexOf(QStringLiteral("Back Matter")),
                  QStringLiteral("Change Icon: the icon chosen appears under Recent"));
            QAction *pr = nullptr;
            for (QAction *a : iconMenu->actions())
                if (a->data().toString() == QLatin1String("qb-proofread"))
                    pr = a;
            binder->select({d3}, true);
            settle(100);
            pr->trigger();
            check(d3->icon == QLatin1String("qb-proofread"), QStringLiteral("... Proofread on another document"));
            byKey(&w, QStringLiteral("resetIcon"))->trigger();
            check(d3->icon.isEmpty(), QStringLiteral("Change Icon: Reset Icon to Default"));
            pr->trigger();
        }
        // label color shining through the white parts
        {
            const Label *lab = p->labels.isEmpty() ? nullptr : &p->labels.first();
            check(lab != nullptr, QStringLiteral("(a label of the template)"));
            if (lab) {
                d3->label = lab->id;
                d1->label = p->labels.size() > 1 ? p->labels.at(1).id : lab->id;
                d1->icon = QStringLiteral("qb-flag-white");
                p->notifyItemChanged(d3, false);
                p->notifyItemChanged(d1, false);
                icons::setLabelTint(true);
                const QImage tinted = icons::forItem(p, d3).pixmap(32, 32).toImage();
                // the white box of "PR" (near the left edge of the box) takes the label color
                const QColor px = tinted.pixelColor(16, 7);
                const QColor lc = lab->color;
                const bool near = qAbs(px.red() - lc.red()) < 30 && qAbs(px.green() - lc.green()) < 30 && qAbs(px.blue() - lc.blue()) < 30;
                const QColor letters = tinted.pixelColor(12, 17);
                icons::setLabelTint(false);
                const QImage plain = icons::forItem(p, d3).pixmap(32, 32).toImage();
                const QColor px2 = plain.pixelColor(16, 7);
                icons::setLabelTint(true);
                check(near && px2.lightness() > 240,
                      QStringLiteral("Icons: with View > Use Label Color In > Icons the label color shines through the white parts"),
                      QStringLiteral("tinted %1 (label %2), without %3, letters %4").arg(px.name(), lc.name(), px2.name(), letters.name()));
                QAction *li = byKey(&w, QStringLiteral("labelIcons"));
                check(li && li->isCheckable() && li->isChecked(), QStringLiteral("... View > Use Label Color In > Icons (on by default)"));
                settle(200);
                shot(binder, QStringLiteral("v017-06-binder-icons-label-color"));
            }
        }
        // Scrivener's names
        {
            QStringList bad;
            for (const icons::CatalogEntry &ce : icons::catalog())
                if (icons::fromScrivener(ce.scrivener) != ce.id)
                    bad << ce.id;
            check(bad.isEmpty(), QStringLiteral("Icons: every icon goes to its Scrivener name and back"), bad.join(QStringLiteral(", ")));
            check(icons::fromScrivener(QStringLiteral("Notes (Yellow Notepad)")) == QLatin1String("qb-notepad-yellow")
                      && icons::fromScrivener(QStringLiteral("Characters (Character Sheet)")) == QLatin1String("qb-character-sheet")
                      && icons::fromScrivener(QStringLiteral("Locations (Map)")) == QLatin1String("qb-location-map")
                      && icons::fromScrivener(QStringLiteral("Front Matter")) == QLatin1String("qb-front-matter")
                      && icons::fromScrivener(QStringLiteral("Information")) == QLatin1String("qb-information")
                      && icons::scrivenerName(QStringLiteral("qb-book-green")) == QLatin1String("Book (Green Book)"),
                  QStringLiteral("... the names seen in a real Scrivener 3 project (Notes (Yellow Notepad), Characters (Character Sheet), "
                                 "Locations (Map), Front Matter, Information)"));
        }
        // own icons: Icon from Text, Manage Icons
        {
            binder->select({d4}, true);
            settle(100);
            withModal<IconFromTextDialog>(300, [](IconFromTextDialog *d) {
                d->textField()->setText(QStringLiteral("Ch1"));
                d->nameField()->setText(QStringLiteral("Chapter Mark"));
                d->grab().save(outDir + QStringLiteral("/v017-07-icon-from-text.png"));
                d->accept();
            });
            byKey(&w, QStringLiteral("iconFromText"))->trigger();
            settle(100);
            check(d4->icon == QLatin1String("custom:Chapter Mark") && QFile::exists(p->folderPath() + QStringLiteral("/icons/Chapter Mark.png")),
                  QStringLiteral("Icon from Text: a picture of the characters, kept in the project's icons folder, given to the document"), d4->icon);
            QString renamed;
            withModal<ManageIconsDialog>(300, [&](ManageIconsDialog *d) {
                d->list()->setCurrentRow(0);
                renamed = d->list()->currentItem() ? d->list()->currentItem()->text() : QString();
                d->grab().save(outDir + QStringLiteral("/v017-08-manage-icons.png"));
                d->reject();
            });
            byKey(&w, QStringLiteral("manageIcons"))->trigger();
            settle(100);
            check(renamed == QLatin1String("Chapter Mark"), QStringLiteral("Manage Icons: lists the icons of the project"), renamed);
        }
    }

    // ============================================================ 5. icon bar
    {
        auto *bar = w.findChild<QToolBar *>(QStringLiteral("iconBar"));
        check(bar && bar->isVisible(), QStringLiteral("Icon bar: shown below the format bar"));
        if (bar) {
            QStringList ids;
            for (QAction *a : bar->actions())
                if (a->objectName().startsWith(QLatin1String("iconBar_")) && a->objectName() != QLatin1String("iconBar_reset"))
                    ids << a->data().toString();
            check(ids == standardIconBar() && ids.size() == 10, QStringLiteral("Icon bar: ten icons to begin with"), ids.join(QStringLiteral(", ")));
            binder->select({d1, d2}, true);
            settle(100);
            tree->setFocus();
            byKey(bar, QStringLiteral("iconBar_qb-warning"))->trigger();
            check(d1->icon == QLatin1String("qb-warning") && d2->icon == QLatin1String("qb-warning"),
                  QStringLiteral("Icon bar: a click gives every selected document the icon"));
            byKey(bar, QStringLiteral("iconBar_reset"))->trigger();
            check(d1->icon.isEmpty() && d2->icon.isEmpty(), QStringLiteral("Icon bar: the last button gives the standard icon back"));
            // choosing the icons: at most ten
            bool refusedEleventh = false, removedByDoubleClick = false;
            QStringList chosen;
            withModal<IconBarDialog>(300, [&](IconBarDialog *d) {
                refusedEleventh = !d->addIcon(QStringLiteral("qb-cog"));
                // a double-click on an icon of the bar takes it out
                const int before = d->chosenList()->count();
                QListWidgetItem *first = d->chosenList()->item(0);
                d->chosenList()->setCurrentItem(first);
                emit d->chosenList()->itemDoubleClicked(first);
                removedByDoubleClick = d->chosenList()->count() == before - 1;
                d->chosenList()->clear();
                for (const QString &id : {QStringLiteral("qb-flag-blue"), QStringLiteral("qb-flag-green"), QStringLiteral("qb-clock"),
                                          QStringLiteral("custom:Chapter Mark")})
                    d->addIcon(id);
                chosen = d->icons();
                d->grab().save(outDir + QStringLiteral("/v017-09-icon-bar-dialog.png"));
                d->accept();
            });
            byKey(&w, QStringLiteral("customizeIconBar"))->trigger();
            settle(150);
            QStringList now;
            for (QAction *a : bar->actions())
                if (a->objectName().startsWith(QLatin1String("iconBar_")) && a->objectName() != QLatin1String("iconBar_reset"))
                    now << a->data().toString();
            check(removedByDoubleClick, QStringLiteral("Icon bar: in Choose Icons a double-click takes an icon out of the bar"));
            check(refusedEleventh && now == chosen && now.size() == 4 && AppSettings::instance().iconBar == chosen,
                  QStringLiteral("Icon bar: Choose Icons takes up to ten (an eleventh is refused); the bar shows the choice, which is kept"),
                  now.join(QStringLiteral(", ")));
            binder->select({d3}, true);
            settle(100);
            byKey(bar, QStringLiteral("iconBar_custom:Chapter Mark"))->trigger();
            check(d3->icon == QLatin1String("custom:Chapter Mark"), QStringLiteral("... also with an icon of the project"));
            byKey(bar, QStringLiteral("iconBar_qb-flag-blue"))->trigger();
            shot(&w, QStringLiteral("v017-10-window-icon-bar"));
        }
    }

    // ============================================================ 6. comments and footnotes of a folder
    {
        // comments in two documents (one of them below a sub-folder), a footnote in a third
        auto addNote = [&](BinderItem *it, const QString &word, NoteKind kind, const QString &text) {
            binder->select({it}, true);
            settle(250);
            TextEditor *e = nullptr;
            for (TextEditor *x : w.findChildren<TextEditor *>())
                if (x->isVisible() && x->item() == it)
                    e = x;
            if (!e)
                return QString();
            selectWord(e, word);
            return e->addLinkedNote(kind, text);
        };
        const QString n1 = addNote(d1, QStringLiteral("wind"), NoteKind::Comment, QStringLiteral("Too windy?"));
        const QString n2 = addNote(d2, QStringLiteral("gulls"), NoteKind::LinkedFootnote, QStringLiteral("Herring gulls."));
        const QString n3 = addNote(d4, QStringLiteral("lamps"), NoteKind::Comment, QStringLiteral("Oil or electric?"));
        p->save(&err);
        check(!n1.isEmpty() && !n2.isEmpty() && !n3.isEmpty(), QStringLiteral("(comments and a footnote made)"));
        binder->select({ch}, true);
        settle(500);
        inspector->showTab(Inspector::Comments);
        settle(300);
        QStringList heads;
        for (QLabel *l : inspector->findChildren<QLabel *>(QStringLiteral("scopeHeader")))
            if (l->isVisible())
                heads << l->text();
        QStringList texts;
        for (QPlainTextEdit *pe : inspector->findChildren<QPlainTextEdit *>())
            if (pe->isVisible() && pe->parentWidget() && pe->parentWidget()->inherits("QFrame"))
                texts << pe->toPlainText();
        check(heads.size() == 3 && heads.at(0).contains(QStringLiteral("The Storm")) && heads.at(1).contains(QStringLiteral("The Calm"))
                  && heads.at(2).contains(QStringLiteral("A scene with")) && texts.contains(QStringLiteral("Too windy?"))
                  && texts.contains(QStringLiteral("Herring gulls.")) && texts.contains(QStringLiteral("Oil or electric?")),
              QStringLiteral("Folder: the inspector lists the comments and footnotes of every document below the folder, by document"),
              heads.join(QStringLiteral(" / ")) + QStringLiteral(" :: ") + texts.join(QStringLiteral(" / ")));
        shot(inspector, QStringLiteral("v017-11-folder-comments"));
        // a note's text changed in the list goes into its document (also one that is not open)
        QPlainTextEdit *deepEdit = nullptr;
        for (QPlainTextEdit *pe : inspector->findChildren<QPlainTextEdit *>())
            if (pe->isVisible() && pe->toPlainText() == QLatin1String("Oil or electric?"))
                deepEdit = pe;
        if (deepEdit) {
            deepEdit->setPlainText(QStringLiteral("Oil lamps, surely."));
            settle(800);
            QString stored;
            p->docs()->editStored(d4->id, false, [&](QTextDocument *doc) {
                for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
                    for (auto it = b.begin(); !it.atEnd(); ++it)
                        if (it.fragment().charFormat().stringProperty(props::NoteId) == n3)
                            stored = it.fragment().charFormat().stringProperty(props::NoteText);
                return 0;
            });
            check(stored == QLatin1String("Oil lamps, surely."), QStringLiteral("... a comment changed in the list is changed in its document"), stored);
        } else {
            check(false, QStringLiteral("... a comment changed in the list is changed in its document"), QStringLiteral("no card"));
        }
        // a click on a note's heading opens its document with the note selected
        QLabel *calmHead = nullptr;
        for (QLabel *l : inspector->findChildren<QLabel *>())
            if (l->isVisible() && l->text().contains(QStringLiteral("Footnote")))
                calmHead = l;
        if (calmHead) {
            emit calmHead->linkActivated(QStringLiteral("note"));
            settle(400);
            TextEditor *e = editorShowing(w, QStringLiteral("gulls"));
            check(e && e->textCursor().hasSelection(), QStringLiteral("... a click on a note opens its document with the commented text selected"));
        }
        // a folder without notes
        binder->select({p->research()}, true);
        settle(400);
        QStringList none;
        for (QLabel *l : inspector->findChildren<QLabel *>(QStringLiteral("scopeHeader")))
            if (l->isVisible())
                none << l->text();
        check(none.size() == 1 && none.first().contains(QStringLiteral("No comments")), QStringLiteral("... a folder without notes says so"),
              none.join(QStringLiteral(" / ")));
    }

    // ============================================================ 7. mind maps
    MindMapPanel *panel = inspector->mindMapPanel();
    {
        binder->select({ch}, true);
        settle(300);
        auto *btn = inspector->findChild<QToolButton *>(QStringLiteral("inspectorMindMapButton"));
        QList<QToolButton *> row;
        if (btn)
            for (QToolButton *b : btn->parentWidget()->findChildren<QToolButton *>(QString(), Qt::FindDirectChildrenOnly))
                if (b->isVisible())
                    row << b;
        std::sort(row.begin(), row.end(), [](QToolButton *a, QToolButton *b) { return a->x() < b->x(); });
        QStringList tips;
        for (QToolButton *b : row)
            tips << b->toolTip().remove(QStringLiteral(" •"));
        const int at = row.indexOf(btn);
        // (since 0.20 the Objects button sits right of it, before Metadata)
        check(btn && at > 0 && tips.value(at - 1) == QLatin1String("Bookmarks") && tips.value(at + 1).startsWith(QLatin1String("Objects"))
                  && tips.value(at + 2) == QLatin1String("Metadata"),
              QStringLiteral("Mind map: its button sits between Bookmarks and Objects (then Metadata) in the inspector"), tips.join(QStringLiteral(" | ")));
        btn->click();
        settle(300);
        check(panel && panel->isVisible() && panel->doc() && panel->doc()->itemId() == ch->id,
              QStringLiteral("Mind map: the button shows the board of the folder selected in the binder"));
    }
    MindMapDoc *doc = panel ? panel->doc() : nullptr;
    MindMapView *view = panel ? panel->view() : nullptr;
    MindMapController *ctl = panel ? panel->controller() : nullptr;
    if (!doc || !view || !ctl) {
        std::printf("%d failure(s)\n", failures);
        return failures ? failures : 1;
    }
    // every place "Menu > ..." the manual names is in the menus of the board
    {
        QFile mf(QStringLiteral(":/docs/manual.md"));
        mf.open(QIODevice::ReadOnly);
        const QString manual = QString::fromUtf8(mf.readAll());
        auto *menuBtn = panel->findChild<QToolButton *>(QStringLiteral("mmMenuButton"));
        auto plain = [](QString t) {
            t.remove(QLatin1Char('&'));
            t = t.section(QLatin1Char('\t'), 0, 0).trimmed();
            return t;
        };
        static const QRegularExpression re(QStringLiteral("\\*\\*Menu > ([^*\\n]+)\\*\\*"));
        QStringList missing, seen;
        auto it = re.globalMatch(manual);
        while (it.hasNext()) {
            const QString path = it.next().captured(1);
            if (seen.contains(path))
                continue;
            seen << path;
            QMenu *level = menuBtn ? menuBtn->menu() : nullptr;
            const QStringList parts = path.split(QStringLiteral(" > "));
            for (int i = 0; i < parts.size() && level; ++i) {
                emit level->aboutToShow();
                QAction *found = nullptr;
                for (QAction *a : level->actions())
                    if (plain(a->text()) == parts[i])
                        found = a;
                if (!found) {
                    missing << path;
                    break;
                }
                level = found->menu();
                if (!level && i + 1 < parts.size()) {
                    missing << path;
                    break;
                }
            }
        }
        check(menuBtn && seen.size() >= 15 && missing.isEmpty(),
              QStringLiteral("Manual: every \"Menu > ...\" place of the mind map chapter is in the board's menus (%1 places)").arg(seen.size()),
              missing.join(QStringLiteral("; ")));
    }
    Board &b = doc->board;
    MindMapScene *scene = doc->scene();
    QWidget *vp = view->viewport();
    w.activateWindow();
    view->setFocus();
    {
        // double-click on the background: a new note with "New Note" selected for typing
        doubleClick(vp, QPoint(140, 120));
        check(b.notes.size() == 1 && scene->editingNote() && scene->editingNote()->textItem()->textCursor().selectedText() == QLatin1String("New Note"),
              QStringLiteral("Mind map: a double-click on the board makes a note, \"New Note\" selected for typing"));
        QTest::keyClicks(view, QStringLiteral("Keeper"));
        mouse(vp, QEvent::MouseButtonPress, QPoint(20, 300), Qt::LeftButton, Qt::LeftButton);
        mouse(vp, QEvent::MouseButtonRelease, QPoint(20, 300), Qt::LeftButton, Qt::NoButton);
        settle(100);
        check(!scene->editingNote() && b.notes.size() == 1 && b.notes.first().text == QLatin1String("Keeper"),
              QStringLiteral("Mind map: typing replaces the placeholder; a click elsewhere ends editing and keeps the text"),
              b.notes.isEmpty() ? QString() : b.notes.first().text);
        check(b.notes.first().width < 100, QStringLiteral("... a short note becomes as narrow as its text (Auto-size notes)"),
              QString::number(b.notes.first().width));
        const int keeper = b.notes.first().id;
        // more notes through the menu commands
        scene->selectIds({keeper});
        ctl->action(QStringLiteral("mm_connRight"))->trigger();
        QTest::keyClicks(view, QStringLiteral("Lamp"));
        scene->endEditing();
        const int lamp = b.notes.last().id;
        check(b.isConnected(keeper, lamp) && !b.pointsTo(keeper, lamp) && b.rectOf(lamp).left() > b.rectOf(keeper).right(),
              QStringLiteral("Notes > New Connected Note > On Right (Alt+Right): a note to the right, joined by a dashed line"));
        scene->selectIds({keeper});
        ctl->action(QStringLiteral("mm_connArrowBelow"))->trigger();
        QTest::keyClicks(view, QStringLiteral("Storm"));
        scene->endEditing();
        const int storm = b.notes.last().id;
        check(b.pointsTo(keeper, storm) && b.rectOf(storm).top() > b.rectOf(keeper).bottom(),
              QStringLiteral("... Below With Arrow: below, with an arrow pointing to the new note"));
        // a stack: New Stacked Note under the selected note
        scene->selectIds({storm});
        ctl->action(QStringLiteral("mm_newNote"))->trigger();
        QTest::keyClicks(view, QStringLiteral("Rain"));
        scene->endEditing();
        const int rain = b.notes.last().id;
        check(qAbs(b.rectOf(rain).top() - b.rectOf(storm).bottom()) < 0.6 && qAbs(b.rectOf(rain).left() - b.rectOf(storm).left()) < 0.6
                  && b.stackBelow(storm).contains(rain),
              QStringLiteral("Notes > New Stacked Note (Ctrl+Return): directly under the selected note, in its stack"));
        // the note above grows: the stack follows
        scene->startEditing(storm, false);
        QTest::keyClicks(view, QStringLiteral(" and a long, long line of thunder that needs more than one line in this narrow note"));
        scene->endEditing();
        check(qAbs(b.rectOf(rain).top() - b.rectOf(storm).bottom()) < 0.6,
              QStringLiteral("... a note of a stack grows: the notes below it move down"));
        // connect by dragging one note onto another
        const QPointF lampC = scene->liveRect(lamp).center(), stormC = scene->liveRect(storm).center();
        const QPointF lampPos = b.note(lamp)->pos;
        drag(vp, view->mapFromScene(lampC), view->mapFromScene(stormC));
        check(b.isConnected(lamp, storm) && b.note(lamp)->pos == lampPos,
              QStringLiteral("Mind map: dropping a note on another connects them; the note goes back to where it was"));
        drag(vp, view->mapFromScene(lampC), view->mapFromScene(stormC), Qt::AltModifier);
        check(b.pointsTo(lamp, storm), QStringLiteral("... with Alt: an arrow to the note dropped on"));
        drag(vp, view->mapFromScene(lampC), view->mapFromScene(stormC), Qt::AltModifier);
        drag(vp, view->mapFromScene(lampC), view->mapFromScene(stormC));
        check(!b.isConnected(lamp, storm), QStringLiteral("... dropping again: no arrow, then no line"));
        // moving with the mouse
        const QPointF before = b.note(lamp)->pos;
        drag(vp, view->mapFromScene(lampC), view->mapFromScene(lampC + QPointF(60, 40)));
        check(QLineF(b.note(lamp)->pos, before + QPointF(60, 40)).length() < 3, QStringLiteral("Mind map: dragging moves a note"),
              QStringLiteral("%1,%2").arg(b.note(lamp)->pos.x()).arg(b.note(lamp)->pos.y()));
        // Ctrl at the drop copies
        const int count = b.notes.size();
        drag(vp, view->mapFromScene(scene->liveRect(lamp).center()), view->mapFromScene(scene->liveRect(lamp).center() + QPointF(0, 120)),
             Qt::ControlModifier);
        check(b.notes.size() == count + 1 && b.notes.last().text == QLatin1String("Lamp"), QStringLiteral("... with Ctrl at the drop it copies"));
        const int lampCopy = b.notes.last().id;
        // undo / redo
        doc->undoStack()->undo();
        check(b.notes.size() == count && !b.note(lampCopy), QStringLiteral("Edit > Undo takes the copy back"));
        doc->undoStack()->redo();
        check(b.notes.size() == count + 1, QStringLiteral("Edit > Redo"));
        scene->selectIds({lampCopy});
        ctl->deleteSelection();
        check(!b.note(lampCopy), QStringLiteral("Edit > Delete"));
        // a marquee selects
        scene->selectIds({});
        const QRectF all = scene->contentRect();
        drag(vp, view->mapFromScene(all.topLeft() - QPointF(15, 15)), view->mapFromScene(all.bottomRight() + QPointF(15, 15)));
        check(scene->selectedIds().size() == b.notes.size(), QStringLiteral("Mind map: a marquee drawn on the background selects what it touches"),
              QString::number(scene->selectedIds().size()));
        // connecting through the menu
        scene->selectIds({keeper, lamp, rain});
        ctl->action(QStringLiteral("mm_connectArrow"))->trigger();
        check(b.pointsTo(keeper, rain) && b.pointsTo(keeper, lamp), QStringLiteral("Notes > Connect With Arrow: from the first note selected"));
        ctl->action(QStringLiteral("mm_removeArrows"))->trigger();
        check(b.isConnected(keeper, rain) && !b.pointsTo(keeper, rain), QStringLiteral("Notes > Remove Arrows"));
        scene->selectIds({keeper, rain});
        ctl->action(QStringLiteral("mm_disconnect"))->trigger();
        check(!b.isConnected(keeper, rain), QStringLiteral("Notes > Disconnect"));
        scene->selectIds({keeper, lamp});
        withModal<QInputDialog>(300, [](QInputDialog *d) {
            d->setTextValue(QStringLiteral("guides"));
            d->accept();
        });
        ctl->action(QStringLiteral("mm_label"))->trigger();
        settle(100);
        check(b.lineLabel(keeper, lamp) == QLatin1String("guides"), QStringLiteral("Notes > Connection Label"));
        scene->selectIds({keeper});
        ctl->action(QStringLiteral("mm_selConnected"))->trigger();
        check(scene->selectedIds().size() == 3, QStringLiteral("Edit > Select > Connected Notes"),
              QString::number(scene->selectedIds().size()));
        ctl->action(QStringLiteral("mm_invert"))->trigger();
        check(scene->selectedIds().size() == b.notes.size() - 3, QStringLiteral("Edit > Invert Selection"));

        // a background shape around notes; magnetic: it takes them along
        scene->selectIds({storm, rain});
        ctl->action(QStringLiteral("mm_newShape"))->trigger();
        check(b.shapes.size() == 1 && b.shapes.first().rect.contains(b.rectOf(storm)) && b.shapes.first().rect.contains(b.rectOf(rain)),
              QStringLiteral("Notes > New Background Shape Around Selection"));
        const int shape = b.shapes.first().id;
        const QPointF stormBefore = b.note(storm)->pos;
        const QPointF grip = b.shapes.first().rect.topLeft() + QPointF(8, 8);
        drag(vp, view->mapFromScene(grip), view->mapFromScene(grip + QPointF(30, 20)));
        check(QLineF(b.note(storm)->pos, stormBefore + QPointF(30, 20)).length() < 3,
              QStringLiteral("Mind map: a magnetic shape takes the notes on it along when it is moved"));
        scene->selectIds({shape});
        ctl->action(QStringLiteral("mm_magnetic"))->trigger();
        check(!b.shape(shape)->magnetic, QStringLiteral("Notes > Magnetic switches it off"));
        ctl->action(QStringLiteral("mm_fade"))->trigger();
        check(b.shape(shape)->look.faded && scene->isFaded(storm), QStringLiteral("Format > Fade: a faded shape fades what lies on it"));
        ctl->action(QStringLiteral("mm_fade"))->trigger();

        // appearance
        scene->selectIds({keeper});
        ctl->action(QStringLiteral("mm_border2"))->trigger();
        ctl->action(QStringLiteral("mm_borderStyle3"))->trigger();
        ctl->action(QStringLiteral("mm_shadow"))->trigger();
        ctl->action(QStringLiteral("mm_bold"))->trigger();
        ctl->action(QStringLiteral("mm_bigger"))->trigger();
        const Note *kn = b.note(keeper);
        check(kn->look.borderWeight == 2 && kn->look.borderStyle == BorderStyle::Cloud && kn->look.shadow && kn->bold && kn->fontSize > 12,
              QStringLiteral("Format: border 2 px, cloud, shadow, bold, bigger"));
        // a style
        NoteStyle yellow;
        for (const NoteStyle &s : b.styles)
            if (s.name == QLatin1String("Yellow Bubble"))
                yellow = s;
        scene->selectIds({lamp});
        ctl->applyStyle(yellow, {lamp});
        check(b.note(lamp)->look.fill == yellow.look.fill && b.note(lamp)->look.borderWeight == 1,
              QStringLiteral("Format > Note Style > Apply: Yellow Bubble"));
        withModal<QDialog>(300, [](QDialog *d) {
            if (auto *le = d->findChild<QLineEdit *>(QStringLiteral("mmStyleName")))
                le->setText(QStringLiteral("Thunder"));
            d->accept();
        });
        scene->selectIds({keeper});
        ctl->action(QStringLiteral("mm_newStyle"))->trigger();
        settle(100);
        bool hasThunder = false;
        for (const NoteStyle &s : b.styles)
            if (s.name == QLatin1String("Thunder") && s.look.borderStyle == BorderStyle::Cloud && s.bold)
                hasThunder = true;
        check(hasThunder, QStringLiteral("Format > Note Style > New Note Style From Selection"));
        // stacks, alignment
        scene->selectIds({keeper, lamp});
        ctl->action(QStringLiteral("mm_align2"))->trigger();
        check(qAbs(b.rectOf(lamp).top() - b.rectOf(keeper).top()) < 0.6, QStringLiteral("Notes > Align > Top Edges"));
        scene->selectIds({keeper, lamp});
        ctl->action(QStringLiteral("mm_sameWidth"))->trigger();
        check(qAbs(b.note(lamp)->width - b.note(keeper)->width) < 0.1, QStringLiteral("Notes > Make Same Width"));
        // split and merge
        scene->startEditing(storm, false);
        {
            NoteItem *ni = scene->editingNote();
            QTextCursor c = ni->textItem()->textCursor();
            c.setPosition(5); // "Storm| and a long ..."
            ni->textItem()->setTextCursor(c);
        }
        ctl->action(QStringLiteral("mm_split"))->trigger();
        const int tail = b.notes.last().id;
        check(b.note(storm)->text == QLatin1String("Storm") && b.note(tail)->text.startsWith(QStringLiteral("and a long"))
                  && b.stackBelow(storm).value(0) == tail,
              QStringLiteral("Notes > Split (Ctrl+Shift+K): the text after the cursor becomes a note stacked below"));
        scene->selectIds({storm, tail});
        ctl->action(QStringLiteral("mm_merge"))->trigger();
        check(!b.note(tail) && b.note(storm)->text.contains(QStringLiteral("thunder")), QStringLiteral("Notes > Merge"));
        // find and replace
        int replaced = ctl->replaceAll(QStringLiteral("Lamp"), QStringLiteral("Lantern"), 2, true, false);
        check(replaced == 1 && b.note(lamp)->text == QLatin1String("Lantern"), QStringLiteral("Edit > Find: Replace All (whole word)"));
        check(ctl->find(QStringLiteral("thunder"), false, 0, true) && scene->editingNote() && scene->editingNote()->id() == storm,
              QStringLiteral("Edit > Find: the next match is selected in its note"));
        scene->endEditing();
        // copy and paste
        scene->selectIds({keeper, lamp});
        ctl->action(QStringLiteral("mm_copy"))->trigger();
        check(QApplication::clipboard()->text().contains(QStringLiteral("Keeper")), QStringLiteral("Edit > Copy: the text of the notes for other programs"));
        const int n0 = b.notes.size();
        scene->selectIds({});
        ctl->action(QStringLiteral("mm_paste"))->trigger();
        check(b.notes.size() == n0 + 2 && b.isConnected(b.notes.at(n0).id, b.notes.at(n0 + 1).id),
              QStringLiteral("Edit > Paste: the notes again, with the line between them"));
        doc->undoStack()->undo();
        // drop a text file (split at empty lines) and a picture
        QTemporaryDir files;
        {
            QFile t(files.path() + QStringLiteral("/ideas.txt"));
            t.open(QIODevice::WriteOnly);
            t.write("First idea\n\nSecond idea\n\nThird idea\n");
        }
        {
            QImage img(120, 80, QImage::Format_RGB32);
            img.fill(QColor(30, 120, 200));
            img.save(files.path() + QStringLiteral("/picture.png"));
        }
        withModal<QDialog>(300, [](QDialog *d) {
            if (auto *cb = d->findChild<QCheckBox *>(QStringLiteral("mmSplitImport")))
                cb->setChecked(true);
            d->accept();
        });
        auto *md = new QMimeData;
        md->setUrls({QUrl::fromLocalFile(files.path() + QStringLiteral("/ideas.txt"))});
        ctl->dropData(md, QPointF(600, 80), Qt::NoModifier);
        delete md;
        settle(100);
        QStringList last3;
        for (int i = qMax(0, int(b.notes.size()) - 3); i < b.notes.size(); ++i)
            last3 << b.notes.at(i).text;
        check(last3 == QStringList({QStringLiteral("First idea"), QStringLiteral("Second idea"), QStringLiteral("Third idea")}),
              QStringLiteral("Mind map: a text file dropped on the board becomes a stack of notes (split at empty lines)"),
              last3.join(QStringLiteral(" | ")));
        md = new QMimeData;
        md->setUrls({QUrl::fromLocalFile(files.path() + QStringLiteral("/picture.png"))});
        ctl->dropData(md, QPointF(420, 300), Qt::NoModifier);
        delete md;
        check(b.notes.last().isImage() && b.notes.last().image.width() == 120, QStringLiteral("... a picture becomes a picture note"));
        // documents of the binder dropped on the board
        md = BinderModel::mimeForItems({d1, d2}, p);
        ctl->dropData(md, QPointF(700, 340), Qt::NoModifier);
        delete md;
        const Note &calm = b.notes.last();
        check(calm.text.startsWith(QStringLiteral("The Calm")) && !calm.ranges.isEmpty() && calm.ranges.first().link == QStringLiteral("qb://") + d2->id,
              QStringLiteral("Mind map: documents dropped from the binder become notes with their titles, linked to the documents"));
        {
            auto showsCalm = [&w, d2] {
                for (TextEditor *e : w.findChildren<TextEditor *>())
                    if (e->isVisible() && e->item() == d2)
                        return true;
                return false;
            };
            const bool before = showsCalm();
            settle(100);
            NoteItem *ci = scene->noteItem(calm.id);
            bool after = false;
            if (ci) {
                const QPointF onLink = ci->textItem()->mapToScene(QPointF(ci->textItem()->document()->documentMargin() + 8, ci->textItem()->boundingRect().height() / 2));
                view->centerOn(onLink);
                settle(50);
                QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, view->mapFromScene(onLink));
                settle(400);
                after = showsCalm();
            }
            check(!before && after, QStringLiteral("Mind map: a click on a linked note opens the document in the editor"),
                  QStringLiteral("before %1, after %2").arg(before).arg(after));
        }
        // the reading order and the text exports
        const QString txt = b.toPlainText({}, QString());
        check(txt.indexOf(QStringLiteral("Keeper")) >= 0 && txt.indexOf(QStringLiteral("Keeper")) < txt.indexOf(QStringLiteral("Lantern")),
              QStringLiteral("Mind map: text exports follow the reading order (left to right, top to bottom, connected notes after)"));
        shot(panel, QStringLiteral("v017-12-mind-map-in-inspector"));
    }
    // the file
    {
        doc->save();
        const QString path = mindmap::pathFor(p->contentDir(ch->id));
        QFile f(path);
        const bool ok = f.open(QIODevice::ReadOnly);
        const QByteArray xml = f.readAll();
        check(ok && xml.contains("<ScappleDocument") && xml.contains("<ConnectedNoteIDs>") && xml.contains("<PointsToNoteIDs>")
                  && xml.contains("<BackgroundShapes>") && xml.contains("<NoteStyles>"),
              QStringLiteral("Mind map: saved by itself as content/<folder>/mindmap.scap in the layout of Scapple files"), path);
        Board again;
        QString e2;
        check(again.load(path, &e2) && again.notes.size() == b.notes.size() && again.shapes.size() == b.shapes.size()
                  && again.lineLabels == b.lineLabels && again.toXml() == b.toXml(),
              QStringLiteral("... read back: the same notes, lines, labels, shapes, styles"), e2);
        // a file in the layout another program writes (no styles, Appearance/Border as text)
        const QByteArray foreign = R"(<?xml version="1.0" encoding="UTF-8"?>
<ScappleDocument Version="1.2" ID="ABC">
  <Notes>
    <Note ID="0" FontSize="12.0" Position="10.0,20.0" Width="140.0">
      <Appearance><Alignment>Center</Alignment><Border Weight="1" Style="Square">0.0 0.0 0.0</Border><Fill>1.0 0.9 0.6</Fill></Appearance>
      <String>Hub</String>
      <ConnectedNoteIDs>1-2</ConnectedNoteIDs>
      <PointsToNoteIDs>2</PointsToNoteIDs>
    </Note>
    <Note ID="1" FontSize="12.0" Position="200.0,20.0" Width="100.0"><String>Left</String><ConnectedNoteIDs>0</ConnectedNoteIDs></Note>
    <Note ID="2" FontSize="14.0" Position="200.0,120.0" Width="100.0"><Appearance><Border>Cloud</Border></Appearance><String>Right</String></Note>
  </Notes>
  <BackgroundColor>1.0 1.0 1.0</BackgroundColor>
</ScappleDocument>)";
        Board fb;
        const bool read = fb.fromXml(foreign);
        auto idOf = [&](const QString &t) {
            for (const Note &n : fb.notes)
                if (n.text == t)
                    return n.id;
            return -1;
        };
        const int hub = idOf(QStringLiteral("Hub")), left = idOf(QStringLiteral("Left")), right = idOf(QStringLiteral("Right"));
        check(read && fb.notes.size() == 3 && hub >= 1 && fb.isConnected(hub, right) && fb.pointsTo(hub, right) && fb.isConnected(left, hub)
                  && fb.note(hub)->align == Align::Center && fb.note(hub)->look.borderStyle == BorderStyle::Square
                  && fb.note(right)->look.borderStyle == BorderStyle::Cloud && fb.settings.background == QColor(255, 255, 255),
              QStringLiteral("Mind map: reads Scapple files as written by another program (ids from 0, ID ranges, lines listed at one end only)"));
        // exports
        QTemporaryDir out;
        QString e3;
        check(ctl->exportPdf(out.path() + QStringLiteral("/map.pdf"), true, false, &e3) && QFileInfo(out.path() + QStringLiteral("/map.pdf")).size() > 1000,
              QStringLiteral("File > Export > PDF"), e3);
        check(ctl->exportPng(out.path() + QStringLiteral("/map.png"), &e3) && !QImage(out.path() + QStringLiteral("/map.png")).isNull(),
              QStringLiteral("File > Export > PNG"), e3);
        check(ctl->exportText(out.path() + QStringLiteral("/map.opml"), 3, false, QString(), true, &e3), QStringLiteral("File > Export > OPML"), e3);
        QFile op(out.path() + QStringLiteral("/map.opml"));
        op.open(QIODevice::ReadOnly);
        const QByteArray opml = op.readAll();
        check(opml.contains("<opml version=\"2.0\">") && opml.contains("text=\"Group\"") && opml.contains("_note="),
              QStringLiteral("... OPML: the background shape is a group holding its notes; the notes field"));
        check(ctl->exportText(out.path() + QStringLiteral("/map.rtf"), 2, false, QStringLiteral("***"), false, &e3), QStringLiteral("File > Export > Rich Text"), e3);
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(out.path() + QStringLiteral("/printed.pdf"));
        ctl->printBoard(&printer);
        check(QFileInfo(out.path() + QStringLiteral("/printed.pdf")).size() > 1000, QStringLiteral("File > Print: the board scaled to one page"));
        // notes into the binder
        const int children = ch->children.size();
        withModal<QMessageBox>(300, [](QMessageBox *mb) { mb->button(QMessageBox::Ok)->click(); });
        scene->selectIds({b.notes.first().id});
        ctl->action(QStringLiteral("mm_toBinder"))->trigger();
        settle(200);
        check(ch->children.size() == children + 1 && ch->children.last()->title == QLatin1String("Keeper"),
              QStringLiteral("Notes > Add Notes to the Binder: a document per note, inside the board's folder"));
        // lock
        ctl->action(QStringLiteral("mm_lock"))->trigger();
        const int locked = b.notes.size();
        ctl->newNoteAt(QPointF(50, 50));
        check(doc->locked() && b.notes.size() == locked && !ctl->action(QStringLiteral("mm_delete"))->isEnabled(),
              QStringLiteral("File > Prevent Editing: the board cannot be changed"));
        ctl->action(QStringLiteral("mm_lock"))->trigger();
    }
    // the large window
    {
        MindMapWindow *mw = MindMapWindow::open(doc, &w);
        settle(400);
        check(mw && mw->isVisible() && mw->view()->scene() == view->scene(),
              QStringLiteral("Mind map: Open in Window shows the same board in a large window"));
        if (mw) {
            QStringList menus;
            for (QAction *a : mw->menuBar()->actions())
                menus << QString(a->text()).remove(QLatin1Char('&'));
            // (0.18 added Help with the Mind Map Help after Scapple's five menus)
            check(menus == QStringList({QStringLiteral("File"), QStringLiteral("Edit"), QStringLiteral("View"), QStringLiteral("Notes"), QStringLiteral("Format"),
                                        QStringLiteral("Help")}),
                  QStringLiteral("... with Scapple's menus File, Edit, View, Notes, Format (and Help)"), menus.join(QStringLiteral(", ")));
            MindMapFormatPanel *fp = mw->formatPanel();
            doc->scene()->selectIds({b.notes.first().id});
            settle(100);
            auto *size = fp->findChild<QDoubleSpinBox *>(QStringLiteral("mmFontSize"));
            size->setValue(20);
            settle(100);
            check(qFuzzyCompare(b.notes.first().fontSize, 20.0), QStringLiteral("... the inspector of the window: Note Format > Font size"));
            auto *thick = fp->findChild<QComboBox *>(QStringLiteral("mmThickness"));
            thick->setCurrentIndex(3);
            emit thick->activated(3);
            check(b.notes.first().look.borderWeight == 3, QStringLiteral("... Note Format > Thickness"));
            auto *styles = fp->findChild<QListWidget *>(QStringLiteral("mmStyleList"));
            check(styles && styles->count() == b.styles.size() + 1, QStringLiteral("... Note Styles: Default and the styles of the board"));
            mw->view()->zoomToFit();
            shot(mw, QStringLiteral("v017-13-mind-map-window"));
            mw->close();
            settle(200);
        }
    }
    // keys reach the board while it has the keyboard, also where the main window has the same keys
    {
        view->setFocus();
        scene->selectIds({b.notes.first().id});
        const bool wasFaded = b.notes.first().look.faded;
        QTest::keyClick(view, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier); // Fade here, Project Search in the main window
        settle(100);
        check(b.notes.first().look.faded != wasFaded, QStringLiteral("Mind map: Ctrl+Shift+F fades the note while the board has the keyboard"));
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
        settle(100);
        check(b.notes.first().look.faded == wasFaded, QStringLiteral("... Ctrl+Z undoes it there"));
    }
    // Scrivener format: the board goes into the .scriv and comes back
    {
        QTemporaryDir sd;
        const QString scriv = sd.path() + QStringLiteral("/Lighthouse.scriv");
        p->save(&err);
        doc->save();
        scrivener::WriteReport rep;
        const bool ok = scrivener::saveCopyAsScrivener(p, scriv, &rep, &err);
        QString uuid;
        QDir data(scriv + QStringLiteral("/Files/Data"));
        for (const QString &dname : data.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (QFile::exists(data.filePath(dname) + QLatin1Char('/') + mindmap::scrivenerFileName()))
                uuid = dname;
        QFile sx(ScrivenerImporter::findScrivx(scriv));
        sx.open(QIODevice::ReadOnly);
        const QByteArray scrivx = sx.readAll();
        check(ok && !uuid.isEmpty(), QStringLiteral("Scrivener format: the board is written beside the folder's files (Files/Data/<UUID>/quillbinder-mindmap.scap)"), err);
        check(scrivx.contains("<IconFileName>Chapter Mark</IconFileName>") && QFile::exists(scriv + QStringLiteral("/Icons/Chapter Mark.png"))
                  && scrivx.contains("<IconFileName>Flag (Blue Flag)</IconFileName>"),
              QStringLiteral("Scrivener format: icons by Scrivener's names; an own icon goes into the Icons folder of the .scriv"));
        QTemporaryDir back;
        ScrivenerImportReport irep;
        auto np = ScrivenerImporter::importProject(scriv, back.path(), QStringLiteral("Back"), StorageFormat::Markdown, &irep, &err);
        BinderItem *nch = nullptr, *nd3 = nullptr, *nd4 = nullptr;
        if (np)
            for (BinderItem *it : np->allItems()) {
                if (it->title == QLatin1String("Chapter One"))
                    nch = it;
                if (it->title == QLatin1String("The Ships"))
                    nd3 = it;
                if (it->title.startsWith(QStringLiteral("A scene with")))
                    nd4 = it;
            }
        Board nb;
        check(nch && nb.load(mindmap::pathFor(np->contentDir(nch->id))) && nb.notes.size() == b.notes.size(),
              QStringLiteral("... imported again: the folder has its mind map"));
        check(nd3 && nd3->icon == QLatin1String("qb-flag-blue") && nd4 && nd4->icon == QLatin1String("custom:Chapter Mark")
                  && QFile::exists(np->folderPath() + QStringLiteral("/icons/Chapter Mark.png")),
              QStringLiteral("... and the documents their icons (also the own one, with its picture)"),
              QStringLiteral("%1 / %2").arg(nd3 ? nd3->icon : QString(), nd4 ? nd4->icon : QString()));
        // an icon this program has no picture for is kept by name
        check(icons::fromScrivener(QStringLiteral("Fancy (Unknown Thing)")).isEmpty()
                  && icons::scrivenerName(QStringLiteral("scriv:Fancy (Unknown Thing)")) == QLatin1String("Fancy (Unknown Thing)"),
              QStringLiteral("... a Scrivener icon without a picture here keeps its name (shown as the nearest icon)"));
    }
    // reopened: the board is read from its file
    {
        doc->save();
        const int notesBefore = b.notes.size();
        binder->select({d1}, true);
        settle(300);
        check(!mindmaps::hasMap(p, d1->id) && panel->doc() && panel->doc()->itemId() == d1->id && panel->doc()->board.isEmpty(),
              QStringLiteral("Mind map: another document has a board of its own (empty, no file until something is on it)"));
        binder->select({ch}, true);
        settle(300);
        check(panel->doc() == doc && doc->board.notes.size() == notesBefore, QStringLiteral("... back to the folder: its board"));
        check(inspector->findChild<QToolButton *>(QStringLiteral("inspectorMindMapButton"))->toolTip().endsWith(QStringLiteral(" •")),
              QStringLiteral("... the button is marked when the document has a mind map"));
    }
    // a document link clicked in the inspector's map with "links open in the current editor":
    // the inspector moves on to that document (its map view is replaced while the click is handled)
    {
        const QString keep = AppSettings::instance().linkOpens;
        AppSettings::instance().linkOpens = QStringLiteral("current");
        MindMapView *pv = panel->view();
        int calmId = 0;
        for (const Note &n : doc->board.notes)
            for (const mindmap::TextRange &r : n.ranges)
                if (r.link == QStringLiteral("qb://") + d2->id)
                    calmId = n.id;
        NoteItem *ci = pv ? pv->boardScene()->noteItem(calmId) : nullptr;
        if (ci) {
            const QPointF onLink = ci->textItem()->mapToScene(QPointF(ci->textItem()->document()->documentMargin() + 8, ci->textItem()->boundingRect().height() / 2));
            pv->centerOn(onLink);
            settle(50);
            QTest::mouseClick(pv->viewport(), Qt::LeftButton, Qt::NoModifier, pv->mapFromScene(onLink));
            settle(400);
        }
        check(ci && panel->doc() && panel->doc()->itemId() == d2->id,
              QStringLiteral("Mind map: with links opening in the current editor the inspector follows to the linked document (no crash)"),
              panel->doc() ? panel->doc()->itemId() : QString());
        AppSettings::instance().linkOpens = keep;
        binder->select({ch}, true);
        settle(300);
    }

    // ============================================================ 8. Help > Developer Guide
    {
        QMenu *help = menuBarMenu(w.menuBar(), QStringLiteral("Help"));
        check(help && menuTexts(help).contains(QStringLiteral("Developer Guide: Updates and Add-ons")),
              QStringLiteral("Help > Developer Guide: Updates and Add-ons"));
        byKey(&w, QStringLiteral("developerGuide"))->trigger();
        settle(300);
        auto *tb = qApp->findChild<QTextBrowser *>(QStringLiteral("developerGuide"));
        if (!tb)
            for (QWidget *tl : QApplication::topLevelWidgets())
                if (auto *x = tl->findChild<QTextBrowser *>(QStringLiteral("developerGuide")))
                    tb = x;
        const QString text = tb ? tb->toPlainText() : QString();
        check(text.contains(QStringLiteral("manifest.json")) && text.contains(QStringLiteral("selftest")) && text.contains(QStringLiteral("make-package.py"))
                  && text.contains(QStringLiteral("ToolInterface/1.0")) && !text.contains(QStringLiteral("{{")),
              QStringLiteral("... it explains the package file, the installation, the loader, the plugins and the tools"));
        if (tb)
            shot(tb->window(), QStringLiteral("v017-14-developer-guide"));
    }

    // dark theme: the new parts
    {
        as.theme = QStringLiteral("dark");
        as.save();
        theme::apply();
        settle(400);
        shot(&w, QStringLiteral("v017-15-dark-theme"));
        as.theme = QStringLiteral("light");
        as.save();
        theme::apply();
    }

    w.close();
    delete wp;
    std::printf("%d failure(s)\n", failures);
    return failures;
}
