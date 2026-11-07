// Scripted run for version 0.60.3:
//  1. the binder's tool tip of a folder counts the folder and everything inside it;
//  2. the folder total in the footer of the corkboard and the outliner sits in the middle and
//     shows the details of a text count (words, characters, characters without spaces,
//     paragraphs, pages) for the whole folder;
//  3. "Lists and text fields" became "Inspector Background" (General Interface, every project):
//     it colors the inspector's lists and text fields only; the fields of the toolbars, the
//     ruler and the page lists of the options take the color of the window; the inspector's
//     right-click Appearance menu offers it; the inspector's old "Background" is now "Inspector
//     Strips"; a color chosen for "Lists and text fields" before stays, for the inspector;
//  4. font names that are not families ("DejaVuSerif-Italic", "DejaVuSerifItalic" ...: Scrivener
//     for Windows wrote "SitkaTextItalic") are shown in the family they stand for, not in a
//     fallback font, and the text keeps the name;
//  5. a double-click beside the text shows its document in the binder (folders opened) and the
//     row blinks twice; Reveal in Binder blinks too.
// Saves screenshots into the folder given as first argument. Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_v0603_tests /tmp/shots
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "compile/PluginManager.h"
#include "text/DisplayFormat.h"
#include "text/FontFaces.h"
#include "text/RtfIO.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/EditorPane.h"
#include "ui/TextEditor.h"
#include "ui/Inspector.h"
#include "ui/LookMenu.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/Ruler.h"
#include "ui/TextColumnView.h"
#include "ui/Theme.h"

#include "AppInfo.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFontInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <cstdio>
#include <memory>

using namespace qb;

static int failures = 0;
static QString outDir;
static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail.left(3000)));
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

static void shot(QWidget *w, const QString &name) { w->grab().save(outDir + QLatin1Char('/') + name + QStringLiteral(".png")); }

static EditorPane *visiblePane(MainWindow &w)
{
    for (EditorPane *ep : w.findChildren<EditorPane *>())
        if (ep->isVisible())
            return ep;
    return nullptr;
}

static BinderItem *item(Project *p, BinderItem *parent, ItemType type, const QString &title, const QStringList &paragraphs)
{
    BinderItem *it = p->model()->createItem(parent, -1, type, title);
    QTextCursor c(p->docs()->text(it->id));
    for (int i = 0; i < paragraphs.size(); ++i) {
        if (i > 0)
            c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
        c.insertText(paragraphs[i]);
    }
    return it;
}

static QString words(int n) { return QLocale().toString(n) + (n == 1 ? QStringLiteral(" word") : QStringLiteral(" words")); }

// The lines of the menu a click on the button opens (the menu is closed again).
static QStringList popupLines(QAbstractButton *b)
{
    auto lines = std::make_shared<QStringList>();
    QTimer::singleShot(400, [lines] {
        if (auto *m = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            for (QAction *a : m->actions())
                *lines << a->text();
            m->close();
        }
    });
    b->click();
    settle(500);
    return *lines;
}

static bool same(const QColor &a, const QColor &b) { return a.rgb() == b.rgb(); }

static QAction *submenuAction(QMenu *m, const QString &name)
{
    for (QAction *a : m->actions())
        if (a->menu() && a->menu()->objectName() == name)
            return a;
    return nullptr;
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV0603Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV0603Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);

    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.60.3")) >= 0
              && QLatin1String(QB_APP_VERSION_SHOWN).endsWith(QLatin1String(" alpha")),
          QStringLiteral("version 0.60.3 or newer, shown with \"alpha\""), QStringLiteral(QB_APP_VERSION_SHOWN));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));

    // ---- a color chosen for "Lists and text fields" before 0.60.3 becomes the inspector's
    {
        QSettings s;
        s.setValue(QStringLiteral("appearance/colors/base"), QStringLiteral("#123456"));
        s.sync();
    }
    AppSettings &as = AppSettings::instance();
    as.load();
    check(!as.ownColors.contains(QStringLiteral("base")) && same(as.ownColors.value(QStringLiteral("inspectorFields")), QColor(0x12, 0x34, 0x56)),
          QStringLiteral("an own color for \"Lists and text fields\" is kept as the own \"Inspector Background\""));
    as.ownColors.remove(QStringLiteral("inspectorFields"));
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.showRuler = true;
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();

    // built-in themes: the inspector's lists keep the color they had (that of all lists)
    {
        QStringList differ;
        for (const theme::Entry &te : theme::available()) {
            theme::Theme t;
            if (te.kind != theme::Kind::BuiltIn || !theme::load(te.id, &t))
                continue;
            const QMap<theme::Role, QColor> c = theme::resolvedColors(t, {});
            if (c.value(theme::Role::InspectorFields) != c.value(theme::Role::Base)
                || c.value(theme::Role::NotesPaper) != c.value(theme::Role::Base))
                differ << te.id;
        }
        check(differ.isEmpty(), QStringLiteral("every built-in theme: Inspector Background and the notes as its lists were before"),
              differ.join(QLatin1Char(' ')));
    }

    // ======================================================== 4: font names that are not families
    {
        const fontfaces::Resolved ps = fontfaces::resolve(QStringLiteral("DejaVuSerif-Italic"));
        const fontfaces::Resolved run = fontfaces::resolve(QStringLiteral("DejaVuSerifBoldItalic"));
        const fontfaces::Resolved spaced = fontfaces::resolve(QStringLiteral("DejaVu Serif Italic"));
        const fontfaces::Resolved design = fontfaces::resolve(QStringLiteral("DejaVuSerifCaptionItalic"));
        const fontfaces::Resolved family = fontfaces::resolve(QStringLiteral("DejaVu Serif"));
        const fontfaces::Resolved none = fontfaces::resolve(QStringLiteral("NoSuchTypefaceItalic"));
        check(ps.family == QLatin1String("DejaVu Serif") && ps.italic && !ps.bold && run.family == QLatin1String("DejaVu Serif") && run.italic
                  && run.bold && spaced.family == QLatin1String("DejaVu Serif") && spaced.italic,
              QStringLiteral("face names (\"DejaVuSerif-Italic\", \"DejaVuSerifBoldItalic\", \"DejaVu Serif Italic\") stand for the family, italic and bold"),
              ps.family + QLatin1Char('/') + run.family + QLatin1Char('/') + spaced.family);
        check(design.family == QLatin1String("DejaVu Serif") && design.italic && family.family == QLatin1String("DejaVu Serif") && !family.italic
                  && none.family.isEmpty(),
              QStringLiteral("... a design that is no family here gives its typeface; a family stays itself; an unknown name stays unknown"),
              design.family + QLatin1Char('/') + none.family);
        // a text as Scrivener for Windows wrote it: the italic words under a face name
        const QByteArray rtfData = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fnil\\fcharset0 DejaVu Sans;}{\\f1\\fnil\\fcharset0 DejaVuSerifItalic;}"
                                   "{\\f2\\fnil\\fcharset0 DejaVuSerif-BoldItalic;}}\n"
                                   "\\pard\\plain {\\f0\\fs22 Plain words. }{\\f1\\fs22\\i Farewell.}{\\f2\\fs22  Gone.}\\par\n}";
        QTextDocument d;
        Formatting fmt;
        rtf::ReadOptions o;
        o.keepFormatting = true;
        rtf::read(rtfData, &d, fmt, o);
        auto fontAt = [&d](const QString &text) {
            QTextCursor c(&d);
            c.setPosition(int(d.toPlainText().indexOf(text)) + 1);
            return c.charFormat();
        };
        const QTextCharFormat fare = fontAt(QStringLiteral("Farewell")), gone = fontAt(QStringLiteral("Gone"));
        const QFontInfo fareInfo(fare.font()), goneInfo(gone.font());
        check(fare.fontFamilies().toStringList().value(0) == QLatin1String("DejaVuSerifItalic") && fareInfo.family() == QLatin1String("DejaVu Serif")
                  && fareInfo.italic(),
              QStringLiteral("RTF: italic words named \"DejaVuSerifItalic\" show in DejaVu Serif Italic (not a fallback font) and keep the name"),
              fareInfo.family() + (fareInfo.italic() ? QStringLiteral(" italic") : QStringLiteral(" upright")));
        check(goneInfo.family() == QLatin1String("DejaVu Serif") && goneInfo.italic() && goneInfo.bold() && gone.fontItalic(),
              QStringLiteral("... \"DejaVuSerif-BoldItalic\" without \\b and \\i: bold italic DejaVu Serif"),
              goneInfo.family() + QStringLiteral(" bold %1 italic %2").arg(goneInfo.bold()).arg(goneInfo.italic()));
    }

    QTemporaryDir work;
    templates::Info blank;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("blank"))
            blank = t;
    QString err;
    auto proj = templates::createProject(blank, work.path(), QStringLiteral("Folder Totals"), &err);
    check(proj != nullptr, QStringLiteral("(project made)"), err);
    if (!proj)
        return 1;
    Project *p = proj.get();
    // a folder without text of its own: 6 + 7 + 1 words in four paragraphs, 66 characters, 56 without spaces
    BinderItem *folder = item(p, p->draft(), ItemType::Folder, QStringLiteral("The Warmth of Home"), {});
    BinderItem *barn = item(p, folder, ItemType::Text, QStringLiteral("In the Barn"), {QStringLiteral("The barn was warm."), QStringLiteral("Rain fell.")});
    BinderItem *gettingHome = item(p, folder, ItemType::Text, QStringLiteral("Getting Home"), {QStringLiteral("She dried her hair with the quilt.")});
    BinderItem *sub = item(p, folder, ItemType::Folder, QStringLiteral("Later"), {});
    BinderItem *tea = item(p, sub, ItemType::Text, QStringLiteral("Tea"), {QStringLiteral("Tea.")});
    // a folder with text of its own (3 words) and one document (1 word)
    BinderItem *own = item(p, p->draft(), ItemType::Folder, QStringLiteral("Storm"), {QStringLiteral("Before the storm.")});
    item(p, own, ItemType::Text, QStringLiteral("Wind"), {QStringLiteral("Wind.")});
    p->save(&err);

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1500, 950);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(proj)), QStringLiteral("(project open)"));
    settle(1500); // the counts follow the texts 0.6 s after a change

    // ======================================================== 1: the binder's tool tip
    const QModelIndex fi = p->model()->indexFor(folder);
    const QString tip = p->model()->data(fi, Qt::ToolTipRole).toString();
    check(folder->totalWordCount() == 14 && tip == QStringLiteral("The Warmth of Home\n") + words(14),
          QStringLiteral("binder tool tip of a folder: the words of everything inside it (14, not the folder's own 0)"), tip);
    const QString ownTip = p->model()->data(p->model()->indexFor(own), Qt::ToolTipRole).toString();
    check(ownTip == QStringLiteral("Storm\n") + words(4) + QStringLiteral("\n(its own text: ") + words(3) + QLatin1Char(')'),
          QStringLiteral("... a folder with text of its own: the total and its own words"), ownTip);
    const QString docTip = p->model()->data(p->model()->indexFor(barn), Qt::ToolTipRole).toString();
    check(docTip == QStringLiteral("In the Barn\n") + words(6), QStringLiteral("... a document: its words, as before"), docTip);

    // ======================================================== 2: the folder total in the footer
    EditorPane *pane = visiblePane(w);
    if (!pane)
        return 1;
    const QStringList expected = {QStringLiteral("Words: 14"), QStringLiteral("Characters: 66"), QStringLiteral("Characters (no spaces): 56"),
                                  QStringLiteral("Paragraphs: 4"),
                                  QStringLiteral("Pages (250 words): %1").arg(QLocale().toString(14 / 250.0, 'f', 1))};
    struct View {
        ViewMode mode;
        const char *button;
        const char *name;
    };
    for (const View &v : {View{ViewMode::Corkboard, "folderCountCorkboard", "corkboard"}, View{ViewMode::Outliner, "folderCountOutliner", "outliner"}}) {
        pane->showItems({folder}, false, int(v.mode));
        settle(500);
        auto *b = pane->findChild<QToolButton *>(QLatin1String(v.button));
        QWidget *bar = b ? b->parentWidget() : nullptr;
        const int off = b && bar ? b->mapTo(bar, b->rect().center()).x() - bar->width() / 2 : 9999;
        check(b && b->isVisible() && b->text() == QStringLiteral("Folder: 14 words") && qAbs(off) <= 2,
              QStringLiteral("%1 of the folder: \"Folder: 14 words\" in the middle of the footer").arg(QLatin1String(v.name)),
              b ? QStringLiteral("'%1', %2 px from the middle").arg(b->text()).arg(off) : QStringLiteral("no button"));
        if (v.mode == ViewMode::Corkboard)
            shot(&w, QStringLiteral("01-folder-total-corkboard"));
        const QStringList lines = b ? popupLines(b) : QStringList();
        check(lines == expected, QStringLiteral("... a click shows words, characters, characters without spaces, paragraphs and pages of the whole folder"),
              lines.join(QStringLiteral(" | ")));
    }
    // narrower: still in the middle while the tools fit
    w.resize(1100, 950);
    settle(400);
    {
        pane->showItems({folder}, false, int(ViewMode::Corkboard));
        settle(400);
        auto *b = pane->findChild<QToolButton *>(QStringLiteral("folderCountCorkboard"));
        const int off = b ? b->mapTo(b->parentWidget(), b->rect().center()).x() - b->parentWidget()->width() / 2 : 9999;
        check(b && qAbs(off) <= 2, QStringLiteral("... and stays in the middle in a narrower window"), QString::number(off));
    }
    w.resize(1500, 950);
    settle(300);
    // the folder shown as text: the total beside its own count, with the same details
    pane->showItems({folder}, false, int(ViewMode::Single));
    settle(400);
    {
        auto *b = pane->findChild<QToolButton *>(QStringLiteral("folderCount"));
        const QStringList lines = b && b->isVisible() ? popupLines(b) : QStringList();
        check(lines == expected, QStringLiteral("a folder shown as text: its total gives the same details"), lines.join(QStringLiteral(" | ")));
    }
    // a document's own count still opens its details
    pane->showItems({barn}, false, int(ViewMode::Single));
    settle(400);
    {
        QToolButton *count = nullptr;
        for (QToolButton *b : pane->findChildren<QToolButton *>())
            if (b->isVisible() && b->text().startsWith(QLatin1String("Words:")))
                count = b;
        const QStringList lines = count ? popupLines(count) : QStringList();
        check(lines.value(0) == QLatin1String("Words: 6") && lines.value(3) == QLatin1String("Paragraphs: 2"),
              QStringLiteral("a document's count shows its details as before"), lines.join(QStringLiteral(" | ")));
    }

    // ======================================================== 3: Inspector Background and the color of the toolbars
    const QColor window = theme::color(theme::Role::Window), base = theme::color(theme::Role::Base);
    check(!same(window, base), QStringLiteral("(the light theme's window and lists differ)"));
    {
        OptionsDialog od(&w, OptionsDialog::SAppearance);
        od.show();
        settle(300);
        QStringList labels;
        for (QLabel *l : od.findChildren<QLabel *>())
            labels << l->text();
        check(od.findChild<QPushButton *>(QStringLiteral("color_inspectorFields")) && !od.findChild<QPushButton *>(QStringLiteral("color_base"))
                  && labels.contains(QStringLiteral("Inspector Background:")) && !labels.contains(QStringLiteral("Lists and text fields:")),
              QStringLiteral("Options > Appearance > General Interface: \"Inspector Background\" instead of \"Lists and text fields\""));
        check(labels.contains(QStringLiteral("Inspector Strips:")) && od.findChild<QPushButton *>(QStringLiteral("color_inspectorBackground")),
              QStringLiteral("... the inspector's own \"Background\" is called \"Inspector Strips\""));
        auto *pages = od.findChild<QListWidget *>(QStringLiteral("optionPages"));
        auto *areas = od.findChild<QListWidget *>(QStringLiteral("appearanceAreas"));
        check(pages && areas && same(pages->viewport()->palette().color(QPalette::Base), window)
                  && same(areas->viewport()->palette().color(QPalette::Base), window),
              QStringLiteral("... the row of pages and the list of parts have the color of the window, as the menus"));
        shot(&od, QStringLiteral("02-options-general-interface"));
    }

    auto *quick = w.findChild<QLineEdit *>(QStringLiteral("quickSearch"));
    QComboBox *fontBox = nullptr;
    for (QToolBar *tb : w.findChildren<QToolBar *>())
        for (QComboBox *c : tb->findChildren<QComboBox *>())
            if (c->isEditable() && !fontBox)
                fontBox = c;
    check(quick && fontBox && same(quick->palette().color(QPalette::Base), window) && same(fontBox->palette().color(QPalette::Base), window)
              && same(quick->palette().color(QPalette::Text), theme::color(theme::Role::WindowText)),
          QStringLiteral("the fields of the toolbars (quick search, font ...) have the color of the toolbars"));

    pane->showItems({barn}, false, int(ViewMode::Single));
    settle(400);
    {
        auto *ruler = pane->findChild<Ruler *>();
        int win = 0, lists = 0;
        if (ruler && ruler->isVisible()) {
            const QImage img = ruler->grab().toImage();
            for (int x = img.width() / 3; x < img.width() * 2 / 3; ++x) {
                const QColor c = img.pixelColor(x, 5);
                win += same(c, window);
                lists += same(c, base);
            }
            img.save(outDir + QStringLiteral("/03-ruler.png"));
        }
        check(ruler && win > 50 && lists == 0, QStringLiteral("the ruler has the color of the toolbars, not of lists"),
              QStringLiteral("window %1, lists %2 pixels").arg(win).arg(lists));
    }

    // an own Inspector Background: the inspector's lists and fields only
    auto *insp = w.findChild<Inspector *>();
    const QColor red(200, 40, 40);
    as.ownColors.insert(QStringLiteral("inspectorFields"), red);
    as.save();
    as.notifyChanged();
    settle(300);
    QListWidget *inList = insp ? insp->findChild<QListWidget *>() : nullptr;
    check(insp && inList && same(insp->palette().color(QPalette::Base), red) && same(inList->viewport()->palette().color(QPalette::Base), red)
              && same(theme::color(theme::Role::NotesPaper), red),
          QStringLiteral("own Inspector Background: the inspector's lists, fields and notes take it"));
    check(same(QApplication::palette().color(QPalette::Base), base) && quick && same(quick->palette().color(QPalette::Base), window)
              && same(theme::color(theme::Role::BinderBackground), base) && same(theme::color(theme::Role::OutlinerBackground), base),
          QStringLiteral("... and nothing else: other lists, the toolbars, the binder and the outliner keep their colors"));
    check(insp && insp->palette().color(QPalette::Text).lightness() > 200,
          QStringLiteral("... the inspector's text turns light on a dark background"), insp ? insp->palette().color(QPalette::Text).name() : QString());
    w.grab().save(outDir + QStringLiteral("/04-inspector-background.png"));

    // the inspector's right-click Appearance menu offers it (for all projects)
    {
        QMenu m;
        lookmenu::fill(&m, lookmenu::Part::Inspector, &w);
        QAction *fields = submenuAction(&m, QStringLiteral("lookColor_inspectorFields"));
        QAction *strips = submenuAction(&m, QStringLiteral("lookColor_inspectorBackground"));
        check(fields && fields->text().startsWith(QLatin1String("Inspector Background (All Projects)")) && strips
                  && strips->text().startsWith(QLatin1String("Inspector Strips")),
              QStringLiteral("inspector's right-click Appearance: \"Inspector Background (All Projects)\" and \"Inspector Strips\""),
              fields ? fields->text() : QString());
        QAction *pick = nullptr;
        if (fields)
            for (QAction *a : fields->menu()->actions())
                if (!pick && a->data().value<QColor>().isValid() && !same(a->data().value<QColor>(), red))
                    pick = a;
        const QColor chosen = pick ? pick->data().value<QColor>() : QColor();
        if (pick)
            pick->trigger();
        settle(300);
        QSettings s;
        check(pick && same(insp->palette().color(QPalette::Base), chosen)
                  && same(QColor(s.value(QStringLiteral("appearance/colors/inspectorFields")).toString()), chosen),
              QStringLiteral("... a color chosen there colors the inspector and is kept in the settings"), chosen.name());
    }
    {
        QMenu m;
        lookmenu::fill(&m, lookmenu::Part::Inspector, &w);
        QAction *fields = submenuAction(&m, QStringLiteral("lookColor_inspectorFields"));
        QAction *reset = fields ? fields->menu()->findChild<QAction *>(QStringLiteral("lookColorReset_inspectorFields")) : nullptr;
        if (!reset && fields)
            for (QAction *a : fields->menu()->actions())
                if (a->objectName() == QLatin1String("lookColorReset_inspectorFields"))
                    reset = a;
        const bool on = reset && reset->isEnabled();
        if (reset)
            reset->trigger();
        settle(300);
        check(on && !as.ownColors.contains(QStringLiteral("inspectorFields")) && same(insp->palette().color(QPalette::Base), base),
              QStringLiteral("... \"The Theme's Color\" gives the inspector the theme's color back"));
    }

    // ======================================================== 5: a double-click beside the text
    auto *binder = w.findChild<BinderPanel *>();
    QTreeView *tree = binder ? binder->tree() : nullptr;
    if (binder && tree) {
        const QModelIndex folderIdx = p->model()->indexFor(folder), subIdx = p->model()->indexFor(sub);
        tree->collapse(subIdx);
        tree->collapse(folderIdx);
        binder->select({own}, false);
        pane->showItems({tea}, false, int(ViewMode::Single));
        settle(400);
        TextColumnView *view = nullptr; // the editor's, not the hidden one of the copyholder
        for (TextColumnView *v : pane->findChildren<TextColumnView *>())
            if (v->isVisible())
                view = v;
        QWidget *beside = view ? view->widget() : nullptr;
        QWidget *flash = nullptr;
        int blinks = 0, onSamples = 0;
        QColor seen;
        if (beside) {
            QTest::mouseDClick(beside, Qt::LeftButton, Qt::NoModifier, QPoint(5, 40));
            bool was = false;
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < 1400) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                QTest::qWait(15);
                if (!flash)
                    flash = tree->viewport()->findChild<QWidget *>(QStringLiteral("binderFlash"));
                const bool on = flash && flash->isVisible();
                if (on && !was) {
                    ++blinks;
                    if (blinks == 1) {
                        const QImage img = tree->viewport()->grab().toImage();
                        const QRect r = tree->visualRect(p->model()->indexFor(tea));
                        seen = img.pixelColor(tree->viewport()->width() - 4, r.center().y());
                        w.grab().save(outDir + QStringLiteral("/05-reveal-blink.png"));
                    } else {
                        w.grab().save(outDir + QStringLiteral("/06-reveal-second-blink.png"));
                    }
                }
                onSamples += on;
                was = on;
            }
        }
        const QList<BinderItem *> sel = binder->selectedItems();
        check(beside && sel.size() == 1 && sel.first() == tea && tree->isExpanded(folderIdx) && tree->isExpanded(subIdx),
              QStringLiteral("double-click beside the text: its document is selected in the binder, the closed folders above it open"),
              QStringLiteral("selected %1, open %2 %3").arg(sel.isEmpty() ? QStringLiteral("nothing") : sel.first()->title).arg(tree->isExpanded(folderIdx)).arg(tree->isExpanded(subIdx)));
        check(pane->items() == QList<BinderItem *>{tea} && pane->mode() == ViewMode::Single,
              QStringLiteral("... the editor keeps showing what it showed"));
        check(blinks == 2 && flash && !flash->isVisible() && seen.red() > 200 && seen.blue() < 120,
              QStringLiteral("... the row blinks twice in orange, then the blink is gone"),
              QStringLiteral("%1 blinks, color %2").arg(blinks).arg(seen.name()));

        // combined text: the document beside the click
        tree->collapse(folderIdx);
        binder->select({own}, false);
        pane->showItems({folder}, false, int(ViewMode::Combined));
        settle(600);
        TextEditor *homeEditor = nullptr;
        for (TextEditor *e : view->findChildren<TextEditor *>())
            if (e->document() == p->docs()->text(gettingHome->id) && e->isVisible())
                homeEditor = e;
        if (homeEditor && beside) {
            const QPoint at(5, homeEditor->mapTo(beside, QPoint(0, homeEditor->height() / 2)).y());
            QTest::mouseDClick(beside, Qt::LeftButton, Qt::NoModifier, at);
            settle(300);
        }
        const QList<BinderItem *> sel2 = binder->selectedItems();
        check(homeEditor && sel2.size() == 1 && sel2.first() == gettingHome && pane->mode() == ViewMode::Combined && pane->items() == QList<BinderItem *>{folder},
              QStringLiteral("... in Combined Text: the document beside the click, and the combined text stays"),
              sel2.isEmpty() ? QStringLiteral("nothing selected") : sel2.first()->title);
        settle(900);

        // Reveal in Binder blinks as well
        binder->select({own}, false);
        pane->showItems({barn}, false, int(ViewMode::Single));
        settle(300);
        w.findChild<QAction *>(QStringLiteral("reveal"))->trigger();
        bool blinked = false;
        for (int i = 0; i < 30 && !blinked; ++i) {
            settle(20);
            if (!flash)
                flash = tree->viewport()->findChild<QWidget *>(QStringLiteral("binderFlash"));
            blinked = flash && flash->isVisible();
        }
        const QList<BinderItem *> sel3 = binder->selectedItems();
        check(blinked && sel3 == QList<BinderItem *>{barn}, QStringLiteral("Navigate > Reveal in Binder: the row blinks as well"),
              QStringLiteral("blinked %1, selected %2").arg(blinked).arg(sel3.isEmpty() ? QStringLiteral("nothing") : sel3.first()->title));
    } else {
        check(false, QStringLiteral("(binder there)"));
    }

    settle(200);
    std::printf("\n%d failure(s)\n", failures);
    delete wp;
    return failures;
}
