// 0.90.2: the writer's list of 2026-11-13.
//
// - File > Options: the P/A circles are gone (the tool tips stay; v0901 checks them); the color buttons of the
//   Mind Map page sit beside their labels again (the circles' holders had pushed them to the far right).
// - Appearance: the lone "Page View" entry is gone, its color is in Main Editor; Show Invisibles has a color of its own
//   (Textual Marks), by default what it drew before (the text at 45 % on the paper), kept in the project's look.
// - The theme list in the main toolbar: Bright / Dark / Both narrow the list, a theme chosen there is used as with
//   Window > Themes; a main toolbar arranged by the writer gets it once.
// - The download page lists the other downloads of each system (the small AppImage, the update, the audio and video
//   add-ons) under the full version.
//
//   xvfb-run -a ./qb_v0902_tests /tmp/shots
// Exit code = number of failed checks.

#include "compile/PluginManager.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "ui/AppSettings.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/ProjectLook.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"
#include "ui/ThemeDialogs.h"
#include "ui/ThemePicker.h"
#include "ui/ToolbarConfig.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QStatusBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <cstdio>

using namespace qb;

static int failures = 0;
static QString shotDir;

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

static void settle(int ms = 60)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(5);
    }
}

static void shot(QWidget *w, const QString &name)
{
    if (shotDir.isEmpty() || !w)
        return;
    settle(80);
    w->grab().save(shotDir + QLatin1Char('/') + name + QStringLiteral(".png"));
}

static EditorPane *firstPane(MainWindow &w)
{
    for (QSplitter *s : w.findChildren<QSplitter *>())
        for (int i = 0; i < s->count(); ++i)
            if (auto *p = qobject_cast<EditorPane *>(s->widget(i)))
                return p;
    return nullptr;
}

static bool near(const QColor &a, const QColor &b, int tol = 2)
{
    return qAbs(a.red() - b.red()) <= tol && qAbs(a.green() - b.green()) <= tol && qAbs(a.blue() - b.blue()) <= tol;
}

// answers the "Change Theme" question (own colors on top of the theme) with "Keep My Changes"
static void keepChangesWhenAsked()
{
    QTimer::singleShot(150, [] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box)
            return;
        for (QAbstractButton *b : box->buttons())
            if (b->text().remove(QLatin1Char('&')) == QLatin1String("Keep My Changes")) {
                b->click();
                return;
            }
        box->reject();
    });
}

// ---- 0.91.0: an arranged main toolbar of an earlier version is split over the new bars; the theme list comes once
static void arrangedToolbarChecks()
{
    AppSettings &as = AppSettings::instance();
    QSettings s;
    // the settings as an earlier version left them: the list of its main toolbar, with or without the mark of 0.90.2
    auto former = [&](const QStringList &main, bool mark0902) {
        if (main.isEmpty())
            s.remove(QStringLiteral("toolbars/main"));
        else
            s.setValue(QStringLiteral("toolbars/main"), main);
        if (mark0902)
            s.setValue(QStringLiteral("toolbars/themePicker0902"), true);
        else
            s.remove(QStringLiteral("toolbars/themePicker0902"));
        s.remove(QStringLiteral("toolbars/split0910"));
        s.remove(QStringLiteral("toolbars/hidden"));
        for (toolbars::Bar b : toolbars::allBars())
            s.remove(QLatin1String("toolbars/bar-") + toolbars::barKey(b));
        s.sync();
        as.load();
    };
    const QStringList arranged = {QStringLiteral("view"), QStringLiteral("|"), QStringLiteral("add"), QStringLiteral("undo"),
                                  QStringLiteral("redo"), QStringLiteral("<>"), QStringLiteral("searchMenu"),
                                  QStringLiteral("search"), QStringLiteral("<>"), QStringLiteral("compile")};
    former(arranged, false);
    check(as.toolbarMain == QStringList({QStringLiteral("view"), QStringLiteral("|"), QStringLiteral("add"), QStringLiteral("undo"),
                                         QStringLiteral("redo")})
              && as.toolbarSearch == QStringList({QStringLiteral("searchMenu"), QStringLiteral("search")})
              && as.toolbarTools == QStringList({QStringLiteral("compile")}),
          QStringLiteral("0.91.0: an arranged main toolbar of an earlier version is split over the bars Main, Search and Tools"),
          as.toolbarMain.join(QLatin1Char(' ')) + QLatin1String(" / ") + as.toolbarSearch.join(QLatin1Char(' ')) + QLatin1String(" / ")
              + as.toolbarTools.join(QLatin1Char(' ')));
    check(as.toolbarThemes.isEmpty() && !as.toolbarHidden.contains(QStringLiteral("themes")),
          QStringLiteral("it gets the theme list once: the Themes bar with its standard set, switched on"),
          as.toolbarHidden.join(QLatin1Char(' ')));
    check(s.value(QStringLiteral("toolbars/main")).toStringList() == arranged && s.value(QStringLiteral("toolbars/split0910")).toBool(),
          QStringLiteral("the old list stays as it was (an older version started again finds its toolbar)"));
    // taken away by the writer in 0.90.2 (the mark set, no theme list on the old bar): its bar stays switched off
    former(arranged, true);
    check(as.toolbarHidden.contains(QStringLiteral("themes")) && as.toolbarThemes.isEmpty(),
          QStringLiteral("taken away under Customize Toolbars before, the Themes bar is switched off"),
          as.toolbarHidden.join(QLatin1Char(' ')));
    // split once: a second start reads the new lists and does not split again
    s.setValue(QStringLiteral("toolbars/main"), QStringList{QStringLiteral("view")});
    s.sync();
    as.load();
    check(as.toolbarSearch == QStringList({QStringLiteral("searchMenu"), QStringLiteral("search")}),
          QStringLiteral("the split is done once"), as.toolbarSearch.join(QLatin1Char(' ')));
    // back to the standard sets
    former(QStringList(), false);
    bool allStandard = as.toolbarHidden.isEmpty();
    for (toolbars::Bar b : toolbars::allBars())
        allStandard = allStandard && as.toolbarList(int(b)).isEmpty();
    check(allStandard && toolbars::isSpecial(QStringLiteral("themePicker")) && !toolbars::label(QStringLiteral("themePicker")).isEmpty()
              && toolbars::standard(toolbars::Bar::Themes) == QStringList{QStringLiteral("themePicker")},
          QStringLiteral("the standard: the theme list alone on the Themes bar; Customize Toolbars offers it"));
}

// ---- File > Options: Appearance and the Mind Map page
static void optionsChecks(MainWindow &w)
{
    OptionsDialog od(&w, 0);
    od.resize(1000, 700);
    od.show();
    settle(200);
    auto *areas = od.findChild<QListWidget *>(QStringLiteral("appearanceAreas"));
    QStringList names;
    for (int i = 0; areas && i < areas->count(); ++i)
        names << areas->item(i)->text();
    check(areas && !names.contains(QStringLiteral("Page View")) && names.contains(QStringLiteral("Main Editor")),
          QStringLiteral("Appearance: no lone \"Page View\" entry any more"), names.join(QStringLiteral(", ")));
    // the color around the pages is on the Main Editor page, the invisibles' color on Textual Marks
    od.showSection(OptionsDialog::SAppearance);
    settle(100);
    auto pageOf = [&](const QString &button) -> QString {
        auto *b = od.findChild<QPushButton *>(button);
        for (int i = 0; b && areas && i < areas->count(); ++i) {
            areas->setCurrentRow(i);
            settle(20);
            if (b->isVisibleTo(&od))
                return areas->item(i)->text();
        }
        return QString();
    };
    const QString surround = pageOf(QStringLiteral("color_editorPageSurround"));
    const QString invisibles = pageOf(QStringLiteral("color_invisibles"));
    check(surround == QLatin1String("Main Editor"), QStringLiteral("the color around the pages is on Main Editor"), surround);
    check(invisibles == QLatin1String("Textual Marks"), QStringLiteral("the invisibles' color is on Textual Marks"), invisibles);
    if (auto *b = od.findChild<QPushButton *>(QStringLiteral("color_invisibles")))
        check(b->toolTip().contains(QLatin1String("Part of the look")),
              QStringLiteral("its tool tip says it is part of the look (where it is kept)"), b->toolTip());
    for (int i = 0; areas && i < areas->count(); ++i)
        if (areas->item(i)->text() == QLatin1String("Main Editor"))
            areas->setCurrentRow(i);
    od.showSection(OptionsDialog::SAppearance);
    shot(&od, QStringLiteral("appearance-main-editor"));
    for (int i = 0; areas && i < areas->count(); ++i)
        if (areas->item(i)->text() == QLatin1String("Textual Marks"))
            areas->setCurrentRow(i);
    shot(&od, QStringLiteral("appearance-textual-marks"));

    // the Mind Map page: each color button right after its label, in the column of the fields above
    QString mmSection;
    int mmIndex = -1;
    for (int s = 0; s < OptionsDialog::SectionCount && mmIndex < 0; ++s)
        if (od.pageNames(s).contains(QStringLiteral("Mind Map"))) {
            mmIndex = s;
            od.showSection(s, QStringLiteral("Mind Map"));
        }
    settle(150);
    auto *fill = od.findChild<QPushButton *>(QStringLiteral("optMmNoteFill"));
    auto *light = od.findChild<QPushButton *>(QStringLiteral("optMmBackgroundLight"));
    auto *dark = od.findChild<QPushButton *>(QStringLiteral("optMmBackgroundDark"));
    check(fill && light && dark, QStringLiteral("(the Mind Map page's color buttons found)"));
    if (fill && light && dark) {
        auto besideLabel = [](QPushButton *b) -> int {
            auto *form = b->parentWidget() ? qobject_cast<QFormLayout *>(b->parentWidget()->layout()) : nullptr;
            QWidget *label = form ? form->labelForField(b) : nullptr;
            if (!label)
                return 100000;
            return b->geometry().left() - label->geometry().right();
        };
        auto *box = qobject_cast<QGroupBox *>(fill->parentWidget());
        QWidget *size = nullptr;
        if (auto *form = box ? qobject_cast<QFormLayout *>(box->layout()) : nullptr)
            if (QLayoutItem *it = form->itemAt(0, QFormLayout::FieldRole))
                size = it->widget();
        const int gapFill = besideLabel(fill), gapLight = besideLabel(light), gapDark = besideLabel(dark);
        info(QStringLiteral("gaps between label and button: fill %1, light %2, dark %3 px; fill at x %4, the size field at x %5")
                 .arg(gapFill).arg(gapLight).arg(gapDark).arg(fill->x()).arg(size ? size->x() : -1));
        check(box && size && qAbs(fill->x() - size->x()) <= 2,
              QStringLiteral("Mind Map: the Fill button starts where the fields above it start"));
        check(gapFill < 120 && gapLight < 80 && gapDark < 80,
              QStringLiteral("Mind Map: the color buttons sit beside their labels, not at the far right"));
        if (box)
            box->parentWidget()->setMinimumHeight(0);
        shot(od.findChild<QWidget *>(QStringLiteral("optionsScroll")) ? od.findChild<QWidget *>(QStringLiteral("optionsScroll")) : &od,
             QStringLiteral("options-mind-map"));
    }
    od.reject();
}

// ---- Show Invisibles: its own color
static void invisiblesChecks(MainWindow &w, Project *p, BinderItem *doc)
{
    EditorPane *pane = firstPane(w);
    if (pane)
        pane->showItems({doc});
    settle(300);
    TextEditor *ed = pane ? pane->currentEditor() : nullptr;
    check(ed != nullptr, QStringLiteral("(the scene is in the editor)"));
    if (!ed)
        return;
    const QColor paper = theme::color(theme::Role::EditorPaper), text = theme::color(theme::Role::EditorText);
    const QColor expect(int(paper.red() * 0.55 + text.red() * 0.45), int(paper.green() * 0.55 + text.green() * 0.45),
                        int(paper.blue() * 0.55 + text.blue() * 0.45));
    const QColor def = theme::color(theme::Role::Invisibles);
    check(near(def, expect, 2) && near(ed->invisiblesColor(), def, 0),
          QStringLiteral("by default the invisibles are the text at 45 % on the paper, as before"),
          QStringLiteral("%1, expected %2, editor %3").arg(def.name(), expect.name(), ed->invisiblesColor().name()));

    QAction *show = w.findChild<QAction *>(QStringLiteral("showInvisibles"));
    if (show && !show->isChecked())
        show->trigger();
    // an own color in the project's look
    look::Values v = look::active();
    v.colors.insert(QStringLiteral("invisibles"), QColor(230, 20, 20));
    QString err;
    check(look::change(v, &err), QStringLiteral("(the project's look takes a red for the invisibles)"), err);
    settle(300);
    check(near(ed->invisiblesColor(), QColor(230, 20, 20), 0), QStringLiteral("the editor draws the invisibles in the project's color"),
          ed->invisiblesColor().name());
    // (read and closed at once: Windows refuses to replace a file that is still open, and the look is saved again below;
    // 0.90.4, found by the first run of this suite on Windows)
    bool kept = false;
    {
        QFile look(p->folderPath() + QStringLiteral("/settings/look.json"));
        kept = look.open(QIODevice::ReadOnly) && look.readAll().contains("\"invisibles\"");
    }
    check(kept, QStringLiteral("the color is kept in the project's look (settings/look.json)"));
    // the marks on the screen are red
    ed->setFocus();
    settle(200);
    const QImage img = ed->viewport()->grab().toImage();
    int red = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 180 && c.green() < 90 && c.blue() < 90)
                ++red;
        }
    info(QStringLiteral("red pixels of the marks: %1").arg(red));
    check(red > 20, QStringLiteral("the dots and the pilcrows are drawn in that color"));
    shot(pane, QStringLiteral("invisibles-red"));
    // back to the theme's
    v.colors.remove(QStringLiteral("invisibles"));
    const bool changedBack = look::change(v, &err);
    settle(200);
    check(changedBack && near(ed->invisiblesColor(), theme::color(theme::Role::Invisibles), 0) && !near(ed->invisiblesColor(), QColor(230, 20, 20), 30),
          QStringLiteral("taken away again: the theme's color"),
          QStringLiteral("%1 editor %2, theme %3").arg(err, ed->invisiblesColor().name(), theme::color(theme::Role::Invisibles).name()));
    if (show && show->isChecked())
        show->trigger();
}

// ---- the theme control of the Themes toolbar (0.91.0: Bright, Dark, U1, U2 as switches, and the list)
static void pickerChecks(MainWindow &w)
{
    auto *bar = w.findChild<QToolBar *>(QStringLiteral("themesBar"));
    auto *picker = w.findChild<ThemePicker *>(QStringLiteral("themePicker"));
    check(bar && picker && picker->isVisible() && bar->isAncestorOf(picker),
          QStringLiteral("the theme list is on the Themes toolbar and visible"));
    if (!bar || !picker)
        return;
    shot(bar, QStringLiteral("toolbar-light"));
    auto *combo = picker->findChild<QComboBox *>(QStringLiteral("themeCombo"));
    auto *bBright = picker->findChild<QToolButton *>(QStringLiteral("themeBright"));
    auto *bDark = picker->findChild<QToolButton *>(QStringLiteral("themeDark"));
    auto *u1 = picker->findChild<QToolButton *>(QStringLiteral("themeUser1"));
    auto *u2 = picker->findChild<QToolButton *>(QStringLiteral("themeUser2"));
    check(combo && bBright && bDark && u1 && u2 && !picker->findChild<QToolButton *>(QStringLiteral("themeFilterBoth"))
              && u1->text() == QLatin1String("U1") && u2->text() == QLatin1String("U2"),
          QStringLiteral("the list and the four buttons Bright, Dark, U1, U2 (no \"Both\")"));
    if (!combo || !bBright || !bDark || !u1 || !u2)
        return;
    QStringList ids = picker->shownIds();
    int all = 0;
    for (const theme::Entry &e : theme::available())
        all += ids.contains(e.id) ? 1 : 0;
    check(all == theme::available().size() && all >= 8, QStringLiteral("the list holds every theme, whatever button was clicked"),
          QString::number(all));
    check(picker->themeOf(ThemePicker::Bright) == QLatin1String("light") && picker->themeOf(ThemePicker::Dark) == QLatin1String("dark")
              && picker->themeOf(ThemePicker::User1).isEmpty() && picker->themeOf(ThemePicker::User2).isEmpty()
              && bBright->isChecked() && bBright->font().bold() && !bDark->isChecked(),
          QStringLiteral("at first: Bright shows Light and Dark shows Dark Mode, U1 and U2 are empty; the button of the theme "
                         "shown is checked and bold"));

    // the writer's look: a color of their own on top of the bright theme
    QString err;
    look::Values v = look::active();
    v.colors.insert(QStringLiteral("invisibles"), QColor(230, 20, 20));
    look::change(v, &err);
    settle(200);
    const int ownBefore = themeui::ownChanges();
    // Dark: the look changes for real, and the own look goes into the empty U1 first
    bDark->click();
    settle(400);
    check(ownBefore > 0 && look::active().theme == QLatin1String("dark") && AppSettings::instance().theme == QLatin1String("dark")
              && theme::isDark() && themeui::ownChanges() == 0,
          QStringLiteral("Dark: one click shows the dark theme as it is, for the project and for new projects (the window is dark)"),
          QStringLiteral("own before %1, theme %2, own now %3").arg(ownBefore).arg(look::active().theme).arg(themeui::ownChanges()));
    check(picker->themeOf(ThemePicker::User1) == QLatin1String("user:User_1") && picker->themeOf(ThemePicker::User2).isEmpty()
              && w.statusBar()->currentMessage().contains(QLatin1String("U1")),
          QStringLiteral("the look with the writer's own colors was saved in the empty U1 first, and the status bar says so"),
          w.statusBar()->currentMessage());
    check(bDark->isChecked() && bDark->font().bold() && !bBright->isChecked() && !bBright->font().bold(),
          QStringLiteral("Dark is the checked, bold button now"));
    ids = picker->shownIds();
    check(ids.contains(QStringLiteral("user:User_1")) && combo->itemText(combo->findData(QStringLiteral("user:User_1"))) == QLatin1String("User_1"),
          QStringLiteral("the saved look is in the list as \"User_1\""));
    shot(bar, QStringLiteral("toolbar-dark"));
    shot(&w, QStringLiteral("window-dark"));
    // U1 brings the own look back
    u1->click();
    settle(400);
    check(look::active().theme == QLatin1String("user:User_1") && !theme::isDark() && near(theme::color(theme::Role::Invisibles), QColor(230, 20, 20), 2)
              && themeui::ownChanges() == 0 && u1->isChecked() && u1->font().bold(),
          QStringLiteral("U1: the look saved there is back, with the writer's color"),
          look::active().theme + QLatin1Char(' ') + theme::color(theme::Role::Invisibles).name());
    // Bright: no question, nothing of the writer's own lies on top
    bBright->click();
    settle(400);
    check(look::active().theme == QLatin1String("light") && !near(theme::color(theme::Role::Invisibles), QColor(230, 20, 20), 30),
          QStringLiteral("Bright: the bright theme as it is"), look::active().theme);
    // an empty U2 takes the look shown now at once
    u2->click();
    settle(400);
    check(picker->themeOf(ThemePicker::User2) == QLatin1String("user:User_2") && look::active().theme == QLatin1String("user:User_2")
              && u2->isChecked() && w.statusBar()->currentMessage().contains(QLatin1String("U2")),
          QStringLiteral("a click on the empty U2 saves the look shown now there"), w.statusBar()->currentMessage());
    // a change on top of U2 with both buttons taken: the question. Cancelled: nothing happens
    v = look::active();
    v.colors.insert(QStringLiteral("invisibles"), QColor(20, 20, 230));
    look::change(v, &err);
    settle(200);
    ThemePicker::setAnswerForTests(-2);
    bDark->click();
    settle(300);
    check(look::active().theme == QLatin1String("user:User_2") && themeui::ownChanges() > 0 && near(theme::color(theme::Role::Invisibles), QColor(20, 20, 230), 2),
          QStringLiteral("own colors not saved in U1 or U2, both taken: the question; cancelled, the look stays as it is"),
          look::active().theme);
    // the question itself: its buttons, and the standard answer is to save back into the button the look came from
    {
        ThemePicker::setAnswerForTests(-3);
        QString buttons, standard;
        QTimer::singleShot(300, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box)
                return;
            for (QAbstractButton *b : box->buttons())
                buttons += b->text().remove(QLatin1Char('&')) + QLatin1Char('|');
            standard = box->defaultButton() ? box->defaultButton()->text().remove(QLatin1Char('&')) : QString();
            box->reject();
        });
        bDark->click();
        settle(300);
        check(buttons.contains(QLatin1String("Save in U1|")) && buttons.contains(QLatin1String("Save in U2|"))
                  && buttons.contains(QLatin1String("Switch Without Saving|")) && buttons.contains(QLatin1String("Cancel|"))
                  && standard == QLatin1String("Save in U2") && look::active().theme == QLatin1String("user:User_2"),
              QStringLiteral("the question offers Save in U1, Save in U2, Switch Without Saving and Cancel; Esc changes nothing"),
              buttons + QLatin1String(" standard: ") + standard);
    }
    // saved in U2 first, then Dark; U2 has the changed look afterwards
    ThemePicker::setAnswerForTests(int(ThemePicker::User2));
    bDark->click();
    settle(400);
    const bool darkNow = look::active().theme == QLatin1String("dark");
    u2->click();
    settle(400);
    check(darkNow && look::active().theme == QLatin1String("user:User_2") && near(theme::color(theme::Role::Invisibles), QColor(20, 20, 230), 2),
          QStringLiteral("answered \"Save in U2\": the changed look is saved there, then the dark theme shows; U2 brings it back"),
          look::active().theme + QLatin1Char(' ') + theme::color(theme::Role::Invisibles).name());
    // switching without saving
    v = look::active();
    v.colors.insert(QStringLiteral("invisibles"), QColor(20, 230, 20));
    look::change(v, &err);
    settle(200);
    ThemePicker::setAnswerForTests(-1);
    bBright->click();
    settle(400);
    u2->click();
    settle(400);
    check(near(theme::color(theme::Role::Invisibles), QColor(20, 20, 230), 2),
          QStringLiteral("answered \"Switch Without Saving\": U2 keeps the look it had"), theme::color(theme::Role::Invisibles).name());
    ThemePicker::setAnswerForTests(-3);

    // the theme of the buttons Bright and Dark: assigned from the right-click menu
    {
        QMenu menu;
        picker->fillButtonMenu(&menu, ThemePicker::Dark);
        QStringList offered;
        QAction *midnight = nullptr;
        for (QAction *a : menu.actions()) {
            offered << a->data().toString();
            if (a->data().toString() == QLatin1String("midnight"))
                midnight = a;
        }
        check(midnight && offered.contains(QStringLiteral("dark")) && offered.contains(QStringLiteral("system"))
                  && !offered.contains(QStringLiteral("light")) && !offered.contains(QStringLiteral("sepia")),
              QStringLiteral("right-click on Dark: the dark themes (and the one that follows the desktop) to choose from"),
              offered.join(QLatin1Char(' ')));
        if (midnight)
            midnight->trigger();
        settle(50);
        check(picker->themeOf(ThemePicker::Dark) == QLatin1String("midnight")
                  && QSettings().value(QStringLiteral("window/themeDark")).toString() == QLatin1String("midnight")
                  && look::active().theme == QLatin1String("user:User_2") && bDark->toolTip().contains(QLatin1String("Midnight")),
              QStringLiteral("Midnight assigned to Dark: kept by the program, named in the tool tip; the look shown does not change"),
              picker->themeOf(ThemePicker::Dark));
        bDark->click();
        settle(400);
        check(look::active().theme == QLatin1String("midnight") && theme::isDark(), QStringLiteral("a click on Dark shows Midnight now"),
              look::active().theme);
        shot(&w, QStringLiteral("window-midnight"));
        ThemePicker again;
        check(again.themeOf(ThemePicker::Dark) == QLatin1String("midnight") && !picker->assign(ThemePicker::Dark, QStringLiteral("no-such-theme"))
                  && !picker->assign(ThemePicker::User1, QStringLiteral("light")),
              QStringLiteral("a new theme control (the window built again) knows the assigned theme; a theme that does not exist is refused"));
        QMenu bright;
        picker->fillButtonMenu(&bright, ThemePicker::Bright);
        offered.clear();
        for (QAction *a : bright.actions())
            offered << a->data().toString();
        check(offered.contains(QStringLiteral("light")) && offered.contains(QStringLiteral("sepia")) && offered.contains(QStringLiteral("user:User_1"))
                  && !offered.contains(QStringLiteral("midnight")),
              QStringLiteral("right-click on Bright: the bright themes, the writer's own bright looks among them"),
              offered.join(QLatin1Char(' ')));
    }
    // right-click on U1: Save the Current Look Here (asks before it replaces a look), Show This Look
    {
        QMenu menu;
        picker->fillButtonMenu(&menu, ThemePicker::User1);
        auto *save = menu.findChild<QAction *>(QStringLiteral("themeSaveHere"));
        auto *show = menu.findChild<QAction *>(QStringLiteral("themeShowThis"));
        check(save && show && show->isEnabled(), QStringLiteral("right-click on U1: Save the Current Look Here, Show This Look"));
        QString asked;
        QTimer::singleShot(300, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                asked = box->text();
                for (QAbstractButton *b : box->buttons())
                    if (b->text().remove(QLatin1Char('&')).startsWith(QLatin1String("Replace")))
                        return b->click();
                box->reject();
            }
        });
        if (save)
            save->trigger();
        settle(400);
        check(asked.contains(QLatin1String("U1")) && look::active().theme == QLatin1String("user:User_1") && theme::isDark(),
              QStringLiteral("Save the Current Look Here on the filled U1 asks first, then U1 holds the look shown (Midnight) and shows it"),
              asked + QLatin1Char(' ') + look::active().theme);
    }
    // a theme chosen in the list is used as with Window > Themes
    const int sepia = combo->findData(QStringLiteral("sepia"));
    keepChangesWhenAsked();
    emit combo->activated(sepia);
    settle(400);
    check(look::active().theme == QLatin1String("sepia") && AppSettings::instance().theme == QLatin1String("sepia")
              && combo->currentData().toString() == QLatin1String("sepia") && !bBright->isChecked() && !bDark->isChecked() && !u1->isChecked(),
          QStringLiteral("Sepia chosen in the list: the project and new projects use it; no button is checked"),
          look::active().theme + QLatin1Char(' ') + AppSettings::instance().theme);
    // Window > Themes changes the list and the buttons too
    keepChangesWhenAsked();
    themeui::choose(&w, QStringLiteral("light"));
    settle(300);
    check(combo->currentData().toString() == QLatin1String("light") && bBright->isChecked(),
          QStringLiteral("a theme chosen elsewhere shows in the list and on its button"), combo->currentText());
    // (the standard again for what follows)
    picker->assign(ThemePicker::Dark, QStringLiteral("dark"));
    QString e2;
    theme::removeUserTheme(QStringLiteral("user:User_1"), &e2);
    theme::removeUserTheme(QStringLiteral("user:User_2"), &e2);
}

// ---- a dialog closed after the field (or window) that had the keyboard before it was deleted (0.90.1 kept plain
// pointers to them and read freed memory at the dialog's hide: a rare crash of the ui suite, gdb in WindowCare's filter)
static void dialogAfterDeletionChecks()
{
    int done = 0;
    for (int round = 0; round < 20; ++round) {
        auto *owner = new QWidget;
        auto *lay = new QVBoxLayout(owner);
        auto *field = new QLineEdit(owner);
        lay->addWidget(field);
        owner->show();
        owner->activateWindow();
        field->setFocus();
        settle(30);
        auto *d = new QDialog(round % 2 ? owner : nullptr); // with a parent, and a dialog of the active window
        d->resize(200, 100);
        d->show();
        settle(30);
        delete field; // the keyboard's former place goes
        if (round % 2 == 0) {
            delete owner; // and, without a parent, the whole window it came from
            owner = nullptr;
        }
        settle(10);
        d->hide();
        settle(30);
        delete d;
        delete owner;
        settle(300); // the give-back runs at 0 and 250 ms
        ++done;
    }
    check(done == 20, QStringLiteral("dialogs closed after the field or window that had the keyboard was deleted: no crash (20 rounds)"));
}

// ---- the download page
static void downloadPageChecks()
{
    const QString src = QDir::cleanPath(QStringLiteral(QB_TEST_SOURCE_DIR "/.."));
    QTemporaryDir dir;
    QDir(dir.path()).mkpath(QStringLiteral("names"));
    auto touch = [&](const QString &n) {
        QFile f(dir.path() + QStringLiteral("/names/") + n);
        f.open(QIODevice::WriteOnly);
    };
    const QStringList linuxFiles = {QStringLiteral("Quillbinder-0.90.2-beta-x86_64.AppImage"),
                               QStringLiteral("Quillbinder-0.90.2-full-beta-x86_64.AppImage"),
                               QStringLiteral("quillbinder-0.90.2-update.qbpkg"), QStringLiteral("media-all-formats.qbpkg")};
    for (const QString &n : linuxFiles)
        touch(n);
    for (const char *n : {"media-playback.qbpkg", "quillbinder-0.90.2-symbols.xz", "SHA256SUMS.txt"})
        touch(QLatin1String(n));
    auto make = [&](QString *out) {
        QProcess pr;
        pr.start(QStringLiteral("python3"), {QStringLiteral("-I"), src + QStringLiteral("/tools/download-page.py"),
                                             dir.path() + QStringLiteral("/names"), QStringLiteral("v0.90.2-beta"),
                                             dir.path() + QStringLiteral("/site")});
        const bool ok = pr.waitForFinished(30000) && pr.exitCode() == 0;
        QFile f(dir.path() + QStringLiteral("/site/index.html"));
        *out = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        return ok;
    };
    QString page;
    check(make(&page), QStringLiteral("(tools/download-page.py writes the page)"));
    // a system's card: from its section to the end of that section (the page lists Windows, Linux, Android)
    auto cardOf = [&page](const char *system) {
        const int at = int(page.indexOf(QStringLiteral("data-system=\"%1\"").arg(QLatin1String(system))));
        const int end = at < 0 ? -1 : int(page.indexOf(QLatin1String("</section>"), at));
        return at < 0 || end < 0 ? QString() : page.mid(at, end - at);
    };
    const int linuxAt = int(page.indexOf(QLatin1String("data-system=\"linux\"")));
    const QString linuxCard = cardOf("linux");
    QStringList missing;
    for (const QString &n : linuxFiles)
        if (!linuxCard.contains(QStringLiteral("releases/download/v0.90.2-beta/") + n))
            missing << n;
    check(linuxAt >= 0 && missing.isEmpty() && linuxCard.contains(QLatin1String("Other downloads"))
              && linuxCard.contains(QLatin1String("The program alone")) && linuxCard.contains(QLatin1String("Program update"))
              && linuxCard.contains(QLatin1String("Audio and video add-ons")),
          QStringLiteral("Linux: the full version and the small AppImage, the update and the audio and video add-ons"),
          missing.join(QLatin1Char(' ')));
    check(!linuxCard.contains(QLatin1String("media-playback.qbpkg")) && !linuxCard.contains(QLatin1String("symbols")),
          QStringLiteral("the single add-ons and the symbols stay on the release's page"));
    const QString windowsCard = cardOf("windows");
    check(windowsCard.contains(QLatin1String("Comes later")) && !windowsCard.contains(QLatin1String("Other downloads")),
          QStringLiteral("Windows without files: \"Comes later\", no list"));
    // Windows files in a release: its own list
    for (const char *n : {"Quillbinder-0.90.2-windows-x64.zip", "Quillbinder-0.90.2-full-windows-x64.zip",
                          "quillbinder-0.90.2-windows-update.qbpkg", "Quillbinder-0.90.2-windows-x64-symbols.zip"})
        touch(QLatin1String(n));
    check(make(&page), QStringLiteral("(the page again, with Windows files)"));
    const QString wCard = cardOf("windows");
    check(wCard.contains(QLatin1String("/Quillbinder-0.90.2-full-windows-x64.zip"))
              && wCard.contains(QLatin1String("/Quillbinder-0.90.2-windows-x64.zip")) && wCard.contains(QLatin1String("/quillbinder-0.90.2-windows-update.qbpkg"))
              && !wCard.contains(QLatin1String("symbols")) && !wCard.contains(QLatin1String("media-all-formats.qbpkg")),
          QStringLiteral("Windows: the full version, the program alone and its update; the Linux add-ons not"));
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV0902Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV0902Test"));
    QApplication app(argc, argv);
    if (argc > 1) {
        shotDir = QString::fromLocal8Bit(argv[1]);
        QDir().mkpath(shotDir);
    }
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();

    arrangedToolbarChecks();
    downloadPageChecks();

    QTemporaryDir work;
    templates::Info novel;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("novel"))
            novel = t;
    QString err;
    auto proj = templates::createProject(novel, work.path(), QStringLiteral("The Harbor"), &err);
    check(proj != nullptr, QStringLiteral("(project made)"), err);
    if (!proj)
        return 1;
    Project *p = proj.get();
    BinderItem *doc = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Scene"));
    QTextCursor(p->docs()->text(doc->id))
        .insertText(QStringLiteral("The tide came in. The boats\tlay still.\nThe harbor slept.\nMorning came late that day."));
    p->save(&err);

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1400, 950);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(proj)), QStringLiteral("(project open)"));
    settle(400);
    p = w.project();

    optionsChecks(w);
    invisiblesChecks(w, p, doc);
    pickerChecks(w);
    dialogAfterDeletionChecks(); // after the window: it installs the filter that remembers the keyboard's place

    w.close();
    delete wp;
    settle(100);
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
