// 0.90.1: menus filled again each time they open keep no copies of their old submenus.
//
// Found with heaptrack in the system test's writing day (2026-11-13): QMenu::clear() deletes only the actions a menu
// owns, so the submenus made by addMenu() (the binder as nested menus in Navigate > Go To and Open Quick Reference,
// the icon groups of Documents > Change Icon, Edit Style and Delete Style in Format > Style) stayed with all their
// entries at every opening; about 70 % of what the program held after 10 minutes. Each menu is opened 30 times; the
// number of its submenus must stay the same, and its entries must still work.
//
//   xvfb-run -a ./qb_v0901_tests /tmp/shots
// Exit code = number of failed checks.

#include "compile/PluginManager.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/PageSetup.h"
#include "core/ProjectTemplates.h"
#include "core/SettingsLayer.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/Inspector.h"
#include "ui/LinkOpen.h"
#include "ui/StyleList.h"
#include "text/TextUtil.h"
#include "ui/CompileDialog.h"
#include "ui/HintLabel.h"
#include "ui/LicensesDialog.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/MenuClear.h"
#include "ui/OptionStorage.h"
#include "ui/OptionsDialog.h"
#include "ui/QuickRef.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"
#include "ui/WindowCare.h"

#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QScreen>
#include <QToolButton>
#include <QComboBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QLineEdit>
#include <QListWidget>
#include <QRadioButton>
#include <QScrollArea>
#include <QDir>
#include <QElapsedTimer>
#include <QMenu>
#include <QMenuBar>
#include <QSplitter>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <memory>
#include <cstdio>
#include <functional>

using namespace qb;

static int failures = 0;
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

// lets the menus' deferred deletions happen
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

static QString plain(QString s)
{
    return s.remove(QLatin1Char('&')).section(QLatin1Char('\t'), 0, 0).trimmed();
}

static QMenu *menuTitled(QMenu *in, const QString &title)
{
    for (QAction *a : in->actions())
        if (a->menu() && plain(a->text()) == title)
            return a->menu();
    return nullptr;
}

static QMenu *topMenu(MainWindow &w, const QString &title)
{
    for (QAction *a : w.menuBar()->actions())
        if (a->menu() && plain(a->text()) == title)
            return a->menu();
    return nullptr;
}

// every submenu that lives inside the menu (also the deeper ones)
static int submenus(QMenu *m)
{
    return int(m->findChildren<QMenu *>().size());
}

// the menu opened n times (its aboutToShow, as Qt sends it when the menu opens)
static void openTimes(QMenu *m, int n)
{
    for (int i = 0; i < n; ++i) {
        emit m->aboutToShow();
        settle(20);
    }
}

// the first entry (not a submenu) titled `title`, searched through the submenus
static QAction *entryTitled(QMenu *m, const QString &title)
{
    for (QAction *a : m->actions()) {
        if (a->menu()) {
            emit a->menu()->aboutToShow();
            if (QAction *in = entryTitled(a->menu(), title))
                return in;
        } else if (plain(a->text()) == title) {
            return a;
        }
    }
    return nullptr;
}

// the quick reference panels (inside the window, or floating on their own)
static QList<QuickRefWindow *> panels(MainWindow &w)
{
    QList<QuickRefWindow *> out = w.findChildren<QuickRefWindow *>();
    for (QWidget *t : QApplication::topLevelWidgets())
        if (auto *q = qobject_cast<QuickRefWindow *>(t); q && !out.contains(q))
            out << q;
    return out;
}

static EditorPane *firstPane(MainWindow &w)
{
    for (QSplitter *s : w.findChildren<QSplitter *>())
        for (int i = 0; i < s->count(); ++i)
            if (auto *p = qobject_cast<EditorPane *>(s->widget(i)))
                return p;
    return nullptr;
}

static void steadyMenu(MainWindow &w, QMenu *m, const QString &name, int expectAtLeast)
{
    if (!m) {
        check(false, name + QStringLiteral(": the menu is found"));
        return;
    }
    openTimes(m, 1);
    const int first = submenus(m);
    openTimes(m, 30);
    const int after = submenus(m);
    info(QStringLiteral("%1: %2 submenus after the first opening, %3 after 30 more").arg(name).arg(first).arg(after));
    check(first >= expectAtLeast, name + QStringLiteral(": it has its submenus"), QString::number(first));
    check(after == first, name + QStringLiteral(": opened 30 times, it keeps no copies of its old submenus"),
          QStringLiteral("%1 then %2").arg(first).arg(after));
    Q_UNUSED(w)
}

static QString shotDir;
static void shot(QWidget *w, const QString &name)
{
    if (shotDir.isEmpty() || !w)
        return;
    settle(80);
    w->grab().save(shotDir + QLatin1Char('/') + name + QStringLiteral(".png"));
}

// an input of an option (not a part of another control)
static bool isInput(QWidget *w)
{
    if (qobject_cast<QLineEdit *>(w)
        && (qobject_cast<QAbstractSpinBox *>(w->parentWidget()) || qobject_cast<QComboBox *>(w->parentWidget())
            || qobject_cast<QKeySequenceEdit *>(w->parentWidget())))
        return false;
    if (qobject_cast<QScrollBar *>(w))
        return false;
    // a color button of an option is named opt... and shows a color (no text of its own); Appearance's color and font
    // buttons are named color_<key> and font_<key>
    if (auto *b = qobject_cast<QPushButton *>(w)) {
        const QString n = w->objectName();
        if (n.startsWith(QLatin1String("color_")) || n.startsWith(QLatin1String("font_")))
            return true;
        if (n.startsWith(QLatin1String("opt")) && !b->icon().isNull() && b->text().isEmpty())
            return true;
    }
    return qobject_cast<QCheckBox *>(w) || qobject_cast<QRadioButton *>(w) || qobject_cast<QComboBox *>(w)
           || qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QLineEdit *>(w) || qobject_cast<QAbstractSlider *>(w)
           || qobject_cast<QKeySequenceEdit *>(w);
}

// controls of the window that are no options of their own
static bool notAnOption(QWidget *w)
{
    static const QStringList names = {
        QStringLiteral("optScope"),      // "Settings for": which values the window shows
        QStringLiteral("optLookScope"),  // "Show and change the look of"
        QStringLiteral("optSearch"),     // the search field of the window
        QStringLiteral("shortcutFilter"), // Keyboard: the search field of the commands
        QStringLiteral("optZoomArea"),   // Editing: which area's zoom the field beside it shows
    };
    for (QWidget *x = w; x; x = x->parentWidget()) {
        if (names.contains(x->objectName()))
            return true;
        if (qobject_cast<QAbstractScrollArea *>(x) && qobject_cast<QAbstractItemView *>(x))
            return true;   // inside a list or table (their own editors)
        if (x->objectName() == QLatin1String("keyboardPage"))
            return false;
    }
    return false;
}

static QString describe(QWidget *w)
{
    QString s = QString::fromLatin1(w->metaObject()->className()) + QLatin1Char(' ') + w->objectName();
    if (auto *b = qobject_cast<QAbstractButton *>(w))
        s += QStringLiteral(" \"") + b->text() + QLatin1Char('"');
    for (QWidget *x = w->parentWidget(); x; x = x->parentWidget())
        if (!x->objectName().isEmpty() && !x->objectName().startsWith(QLatin1String("qt_"))) {
            s += QStringLiteral(" in ") + x->objectName();
            break;
        }
    return s;
}

// File > Options: a tool tip of where each option is kept (UR-10; the badges went in 0.90.2), the pages scroll (UR-02)
static void optionsChecks(MainWindow &w)
{
    using optstore::keyOf;
    auto *odp = new OptionsDialog(&w, 0); // deleted before the next window: it pauses the project's values while open
    OptionsDialog &od = *odp;
    od.resize(900, 640);
    od.show();
    settle(200);
    QStringList unmarked, noTip;
    int marked = 0, project = 0;
    auto namesPlace = [](const QString &tip) {
        return tip.contains(QLatin1String("Fixed:")) || tip.contains(QLatin1String("Depends on the setting:"))
               || tip.contains(QLatin1String("Part of the look:"));
    };
    for (QWidget *c : od.findChildren<QWidget *>()) {
        if (!isInput(c) || notAnOption(c))
            continue;
        if (keyOf(c).isEmpty()) {
            unmarked << describe(c);
            continue;
        }
        ++marked;
        if (c->property("qbPlace").toString() == QLatin1String("P"))
            ++project;
        if (!namesPlace(c->toolTip()))
            noTip << describe(c);
    }
    // 0.90.2: no badges and no holders any more (the writer's wish); the tool tip tells
    const int badges = int(od.findChildren<QLabel *>(QStringLiteral("storageBadge")).size());
    const int holders = int(od.findChildren<QWidget *>(QStringLiteral("storageHolder")).size());
    info(QStringLiteral("%1 option controls marked, %2 of them P; %3 badges, %4 holders").arg(marked).arg(project).arg(badges).arg(holders));
    check(marked > 90 && unmarked.isEmpty(), QStringLiteral("Options: every option control tells where it is kept"),
          unmarked.join(QLatin1String("\n      ")));
    check(noTip.isEmpty(), QStringLiteral("Options: each tool tip names the place (fixed, depending, or part of the look)"),
          noTip.join(QLatin1String("\n      ")));
    check(badges == 0 && holders == 0, QStringLiteral("Options: no P/A circles and no holders before the controls (0.90.2)"),
          QStringLiteral("%1 badges, %2 holders").arg(badges).arg(holders));
    auto *scroll = od.findChild<QScrollArea *>(QStringLiteral("optionsScroll"));
    check(scroll && scroll->widgetResizable(), QStringLiteral("Options: the pages are in a scrolling area (UR-02)"));
    for (int s = 0; s < OptionsDialog::SectionCount; ++s) {
        const QStringList pages = od.pageNames(s);
        od.showSection(s, pages.isEmpty() ? QString() : pages.first());
        shot(&od, QStringLiteral("options-%1").arg(s, 2, 10, QLatin1Char('0')));
    }
    auto placeOf = [](QWidget *c) { return c ? c->property("qbPlace").toString() : QStringLiteral("?"); };
    auto *quotes = od.findChild<QCheckBox *>(QStringLiteral("optSmartQuotes"));
    auto *language = od.findChild<QComboBox *>(QStringLiteral("optLanguage"));
    auto *paper = od.findChild<QPushButton *>(QStringLiteral("color_editorPaper"));
    check(placeOf(quotes) == QLatin1String("A") && quotes->toolTip().contains(QLatin1String("Depends on the setting")),
          QStringLiteral("all projects: smart quotes A, \"depends on the setting\""), quotes ? quotes->toolTip() : QString());
    check(placeOf(language) == QLatin1String("A") && language->toolTip().contains(QLatin1String("Fixed")),
          QStringLiteral("the language: A, fixed"), language ? language->toolTip() : QString());
    check(paper && placeOf(paper) == QLatin1String("P") && paper->toolTip().contains(QLatin1String("#"))
              && paper->toolTip().contains(QLatin1String("kept with this project")),
          QStringLiteral("the editor's paper color: P (this project's look), its tool tip names the color and the place"),
          paper ? paper->toolTip() : QString());
    od.reject();
    delete odp;
    settle(50);

    // "Settings for: This project": a value changed there is the project's (P); a fixed one stays A
    OptionsDialog::setNextScope(OptionsDialog::ThisProject);
    {
        OptionsDialog d(&w, OptionsDialog::Editing);
        d.show();
        settle(150);
        check(d.optionScope() == OptionsDialog::ThisProject, QStringLiteral("(the window opens for this project)"));
        auto *q = d.findChild<QCheckBox *>(QStringLiteral("optSmartQuotes"));
        check(placeOf(q) == QLatin1String("A") && q->toolTip().contains(QLatin1String("uses the value for all projects")),
              QStringLiteral("this project, nothing of its own yet: A, and the tool tip says how to keep its own"), q ? q->toolTip() : QString());
        if (q)
            q->setChecked(!q->isChecked());
        d.accept();
        settle(100);
        info(QStringLiteral("project values: %1, file %2").arg(QStringList(settingslayer::projectValues().keys()).join(QLatin1Char(' ')), settingslayer::projectFile()));
    }
    OptionsDialog::setNextScope(OptionsDialog::ThisProject);
    {
        OptionsDialog d(&w, OptionsDialog::Editing);
        d.show();
        settle(150);
        auto *q = d.findChild<QCheckBox *>(QStringLiteral("optSmartQuotes"));
        auto *lang = d.findChild<QComboBox *>(QStringLiteral("optLanguage"));
        check(placeOf(q) == QLatin1String("P") && q->toolTip().contains(QLatin1String("this project keeps a value of its own")),
              QStringLiteral("this project after the change: smart quotes P"), q ? q->toolTip() : QString());
        check(placeOf(lang) == QLatin1String("A"), QStringLiteral("this project: the language stays A (fixed)"));
        d.showSection(OptionsDialog::SEditing);
        shot(&d, QStringLiteral("options-this-project"));
        d.reject();
        settle(50);
    }
    {
        OptionsDialog d(&w, OptionsDialog::Editing); // all projects again
        d.show();
        settle(150);
        auto *q = d.findChild<QCheckBox *>(QStringLiteral("optSmartQuotes"));
        check(placeOf(q) == QLatin1String("A") && q->toolTip().contains(QLatin1String("This project keeps a value of its own")),
              QStringLiteral("all projects: smart quotes A, the tool tip says this project keeps its own"), q ? q->toolTip() : QString());
        d.reject();
        settle(50);
    }
}

// a dialog the command opens, closed with Esc once it is there
static QString closeWithEscape(MainWindow &w, const QString &command)
{
    QAction *a = w.findChild<QAction *>(command);
    if (!a)
        return QStringLiteral("no command");
    const QSet<QWidget *> before = [] {
        QSet<QWidget *> s;
        for (QWidget *t : QApplication::topLevelWidgets())
            if (t->isVisible())
                s.insert(t);
        return s;
    }();
    auto found = std::make_shared<QString>();
    auto tries = std::make_shared<int>(0);
    auto poll = std::make_shared<std::function<void()>>();
    *poll = [&w, before, found, tries, poll] {
        QWidget *d = QApplication::activeModalWidget();
        if (!d)
            for (QWidget *t : QApplication::topLevelWidgets())
                if (t->isVisible() && qobject_cast<QDialog *>(t) && !before.contains(t))
                    d = t;
        if (d) {
            *found = d->windowTitle();
            d->activateWindow();
            QTest::keyClick(d, Qt::Key_Escape);
            if (d->isVisible())
                if (auto *dd = qobject_cast<QDialog *>(d))
                    dd->reject();
            return;
        }
        if (++*tries < 60)
            QTimer::singleShot(50, &w, *poll);
    };
    QTimer::singleShot(50, &w, *poll);
    a->trigger();
    settle(400);
    return found->isEmpty() ? QStringLiteral("no dialog came") : *found;
}

// UR-01 (the keyboard back in the text after a dialog), UR-02 (dialogs on a small screen), UR-13 (placeholders)
static void windowChecks(MainWindow &w)
{
    EditorPane *pane = firstPane(w);
    TextEditor *ed = pane ? pane->currentEditor() : nullptr;
    w.activateWindow();
    settle(100);
    if (ed)
        ed->setFocus();
    settle(100);
    check(ed && QApplication::focusWidget() == ed, QStringLiteral("(the text has the keyboard)"));
    for (const char *cmd : {"options", "targets", "snapshotTitle", "projectSettings", "compile"}) {
        if (!ed)
            break;
        w.activateWindow();
        ed->setFocus();
        settle(60);
        const QString dlg = closeWithEscape(w, QLatin1String(cmd));
        QWidget *f = QApplication::focusWidget();
        check(f == ed, QStringLiteral("UR-01: after %1 (\"%2\") is closed with Esc, the text has the keyboard again").arg(QLatin1String(cmd), dlg),
              f ? QString::fromLatin1(f->metaObject()->className()) + QLatin1Char(' ') + f->objectName() : QStringLiteral("none"));
    }
    // typing goes on in the text
    if (ed) {
        const int before = ed->document()->characterCount();
        closeWithEscape(w, QStringLiteral("options"));
        if (QWidget *f = QApplication::focusWidget())
            QTest::keyClicks(f, QStringLiteral("tide"));
        settle(60);
        check(ed->document()->characterCount() == before + 4, QStringLiteral("UR-01: words typed after closing Options reach the text"));
    }

    // UR-02: a 1366 x 768 screen less a 44 px panel; the windows shrink to it with their buttons in view
    const QRect small(0, 0, 1366, 768 - 44);
    auto buttonsInView = [](QWidget *win, const QString &name) {
        for (QAbstractButton *b : win->findChildren<QAbstractButton *>())
            if (b->isVisible() && plain(b->text()) == name) {
                const QRect r(b->mapTo(win, QPoint(0, 0)), b->size());
                return win->rect().contains(r);
            }
        return false;
    };
    {
        OptionsDialog d(&w, 0);
        d.resize(1080, 760);
        d.show();
        settle(100);
        const bool fits = windowcare::fitInto(&d, small);
        settle(100);
        check(fits && d.height() <= small.height() - 34 && buttonsInView(&d, QStringLiteral("OK")),
              QStringLiteral("UR-02: Options fits a 1366 x 768 screen, OK in view"), QStringLiteral("%1 x %2").arg(d.width()).arg(d.height()));
        d.reject();
    }
    settle(50);
    {
        CompileDialog d(w.project(), &w);
        d.show();
        settle(150);
        const bool fits = windowcare::fitInto(&d, small);
        settle(100);
        check(fits && d.height() <= small.height() - 34, QStringLiteral("UR-02: Compile fits a 1366 x 768 screen"),
              QStringLiteral("%1 x %2, least %3").arg(d.width()).arg(d.height()).arg(d.minimumSizeHint().height()));
        shot(&d, QStringLiteral("compile-small"));
        d.reject();
    }
    settle(50);
    showThirdPartyLicenses(&w);
    settle(200);
    if (auto *lic = qobject_cast<QDialog *>(w.findChild<QDialog *>(QStringLiteral("licensesWindow")))) {
        const bool fits = windowcare::fitInto(lic, small);
        settle(100);
        check(fits && lic->height() <= small.height() - 34, QStringLiteral("UR-02: Third-Party Licenses fits a 1366 x 768 screen"),
              QStringLiteral("%1 x %2").arg(lic->width()).arg(lic->height()));
        lic->close();
    } else {
        check(false, QStringLiteral("UR-02: the license window opens"));
    }
    settle(100);
    // shown on a screen smaller than the window: made smaller when it is shown
    {
        const QRect area = windowcare::usableArea(&w);
        QDialog big(&w);
        big.resize(area.width() + 300, area.height() + 300);
        big.show();
        settle(100);
        check(big.height() + 34 <= area.height() + 1 && big.width() <= area.width(),
              QStringLiteral("UR-02: a dialog larger than the screen is made to fit when it is shown"),
              QStringLiteral("%1 x %2 on %3 x %4").arg(big.width()).arg(big.height()).arg(area.width()).arg(area.height()));
        big.reject();
    }

    // UR-13: placeholder texts at 4.5:1 or more
    auto placeholderContrast = [](QWidget *f) {
        const QPalette p = f->palette();
        return hintlabel::contrast(p.color(QPalette::Active, QPalette::PlaceholderText), p.color(QPalette::Active, QPalette::Base));
    };
    QWidget *quick = w.findChild<QLineEdit *>(QStringLiteral("quickSearch"));
    QWidget *synopsis = nullptr;
    for (QPlainTextEdit *e : w.findChildren<QPlainTextEdit *>())
        if (e->placeholderText().startsWith(QLatin1String("Synopsis:")))
            synopsis = e;
    if (!synopsis)
        for (QTextEdit *e : w.findChildren<QTextEdit *>())
            if (e->placeholderText().startsWith(QLatin1String("Synopsis:")))
                synopsis = e;
    check(quick && placeholderContrast(quick) >= 4.5, QStringLiteral("UR-13: the quick search's hint has a contrast of 4.5:1 or more"),
          quick ? QString::number(placeholderContrast(quick), 'f', 2) : QStringLiteral("not found"));
    check(synopsis && placeholderContrast(synopsis) >= 4.5, QStringLiteral("UR-13: the synopsis' hint has a contrast of 4.5:1 or more"),
          synopsis ? QString::number(placeholderContrast(synopsis), 'f', 2) : QStringLiteral("not found"));
    // in a dark look too
    AppSettings::instance().theme = QStringLiteral("dark");
    theme::apply();
    settle(200);
    check(quick && placeholderContrast(quick) >= 4.5 && synopsis && placeholderContrast(synopsis) >= 4.5,
          QStringLiteral("UR-13: both hints stay at 4.5:1 in the dark look"),
          QStringLiteral("%1, %2").arg(quick ? placeholderContrast(quick) : 0.0, 0, 'f', 2).arg(synopsis ? placeholderContrast(synopsis) : 0.0, 0, 'f', 2));
    AppSettings::instance().theme = QStringLiteral("light");
    theme::apply();
    settle(100);
}

// UR-19: the Formats column shows its longest name without a sideways scroll bar; the columns' widths are kept
static void compileChecks(Project *p, const QString &tag)
{
    int widest = 0, view = 0;
    bool sideways = true;
    {
        CompileDialog d(p);
        d.show();
        settle(200);
        auto *list = d.findChild<QListWidget *>(QStringLiteral("compileFormats"));
        if (list) {
            for (int r = 0; r < list->count(); ++r)
                widest = qMax(widest, list->visualItemRect(list->item(r)).left() + list->fontMetrics().horizontalAdvance(list->item(r)->text())
                                          + list->iconSize().width() + 8);
            view = list->viewport()->width();
            sideways = list->horizontalScrollBar()->isVisible();
        }
        check(list && widest <= view && !sideways,
              tag + QStringLiteral("UR-19: the Formats column shows its longest name, no sideways scroll bar"),
              QStringLiteral("text needs %1 px, the list shows %2").arg(widest).arg(view));
        auto *cats = d.findChild<QPushButton *>(QStringLiteral("compileObjectCategories"));
        check(cats && cats->width() >= cats->sizeHint().width(), tag + QStringLiteral("UR-19: \"All categories\" has the room it needs"),
              cats ? QStringLiteral("%1 of %2").arg(cats->width()).arg(cats->sizeHint().width()) : QStringLiteral("not found"));
        shot(&d, tag.isEmpty() ? QStringLiteral("compile") : QStringLiteral("compile-scale2"));
        if (!tag.isEmpty())
            return;
        // widths set by hand are kept
        auto *split = d.findChild<QSplitter *>(QStringLiteral("compileSplit"));
        if (split) {
            QList<int> sizes = split->sizes();
            sizes[0] += 60;
            sizes[2] -= 60;
            split->setSizes(sizes);
            emit split->splitterMoved(sizes[0], 1);
            const QList<int> set = split->sizes();
            d.reject();
            CompileDialog again(p);
            again.show();
            settle(200);
            auto *s2 = again.findChild<QSplitter *>(QStringLiteral("compileSplit"));
            check(s2 && s2->sizes().value(0) == set.value(0), QStringLiteral("UR-19: the widths of the columns set by hand are kept"),
                  s2 ? QStringLiteral("%1 then %2").arg(set.value(0)).arg(s2->sizes().value(0)) : QString());
            again.reject();
        }
    }
}

// the popup menu that opens while fn runs (its entries' texts), closed again
static QStringList popupEntries(const std::function<void()> &fn, QString *name = nullptr)
{
    auto entries = std::make_shared<QStringList>();
    auto menuName = std::make_shared<QString>();
    auto tries = std::make_shared<int>(0);
    auto poll = std::make_shared<std::function<void()>>();
    *poll = [entries, menuName, tries, poll] {
        if (auto *m = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            *menuName = m->objectName();
            for (QAction *a : m->actions())
                if (!a->isSeparator())
                    *entries << plain(a->text());
            m->close();
            return;
        }
        if (++*tries < 40)
            QTimer::singleShot(50, qApp, *poll);
    };
    QTimer::singleShot(50, qApp, *poll);
    fn();
    settle(100);
    if (name)
        *name = *menuName;
    return *entries;
}

static void usabilityChecks(MainWindow &w)
{
    EditorPane *pane = firstPane(w);
    TextEditor *ed = pane ? pane->currentEditor() : nullptr;
    auto action = [&w](const char *n) { return w.findChild<QAction *>(QLatin1String(n)); };

    // ---- UR-12: Undo names its step; off where there is nothing to undo; the list beside the toolbar's Undo
    if (ed) {
        w.activateWindow();
        ed->setFocus();
        settle(50);
        QTextCursor c = ed->textCursor();
        c.movePosition(QTextCursor::End);
        ed->setTextCursor(c);
        QTest::keyClicks(ed, QStringLiteral(" harbor"));
        settle(150);
        QAction *undo = action("undo");
        QAction *redo = action("redo");
        check(undo && undo->isEnabled() && plain(undo->text()) == QLatin1String("Undo Typing"),
              QStringLiteral("UR-12: after typing, Edit > Undo reads \"Undo Typing\""), undo ? undo->text() : QString());
        // a word made bold by the command, then a paste
        c = ed->textCursor();
        c.movePosition(QTextCursor::PreviousWord, QTextCursor::KeepAnchor);
        ed->setTextCursor(c);
        action("bold")->trigger();
        settle(150);
        check(plain(undo->text()) == QLatin1String("Undo Bold"), QStringLiteral("UR-12: after Format > Bold: \"Undo Bold\""), undo->text());
        c = ed->textCursor();
        c.movePosition(QTextCursor::End);
        ed->setTextCursor(c);
        QApplication::clipboard()->setText(QStringLiteral(" and the gulls"));
        action("paste")->trigger();
        settle(150);
        check(plain(undo->text()) == QLatin1String("Undo Paste"), QStringLiteral("UR-12: after Edit > Paste: \"Undo Paste\""), undo->text());
        QTest::keyClick(ed, Qt::Key_Backspace);
        settle(150);
        check(plain(undo->text()) == QLatin1String("Undo Delete"), QStringLiteral("UR-12: after Backspace: \"Undo Delete\""), undo->text());
        // the list beside the toolbar's Undo
        QToolButton *ub = w.findChild<QToolButton *>(QStringLiteral("undoButton"));
        check(ub && ub->menu() && ub->popupMode() == QToolButton::MenuButtonPopup,
              QStringLiteral("UR-12: the toolbar's Undo has an arrow with the list of steps"));
        QMenu *list = w.findChild<QMenu *>(QStringLiteral("undoList"));
        QStringList steps;
        if (list) {
            emit list->aboutToShow();
            for (QAction *a : list->actions())
                steps << a->text();
        }
        info(QStringLiteral("the list: %1").arg(steps.mid(0, 5).join(QLatin1String(" | "))));
        check(steps.size() >= 4 && steps.value(0) == QLatin1String("1  Delete") && steps.value(1) == QLatin1String("2  Paste")
                  && steps.value(2) == QLatin1String("3  Bold") && steps.value(3) == QLatin1String("4  Typing"),
              QStringLiteral("UR-12: the list names the steps, the next one first"), steps.join(QLatin1String(" | ")));
        const QString before = ed->toPlainText();
        if (list && list->actions().size() >= 3) {
            list->actions().at(2)->trigger(); // "3  Bold": Delete, Paste and Bold go
            settle(150);
        }
        check(!ed->toPlainText().contains(QLatin1String("gulls")) && ed->toPlainText().endsWith(QLatin1String("harbor"))
                  && plain(undo->text()) == QLatin1String("Undo Typing") && redo && plain(redo->text()) == QLatin1String("Redo Bold"),
              QStringLiteral("UR-12: choosing the third step undoes three steps; Undo Typing and Redo Bold are next"),
              ed->toPlainText().right(40) + QStringLiteral(" / ") + undo->text() + QStringLiteral(" / ") + (redo ? redo->text() : QString()));
        QMenu *rlist = w.findChild<QMenu *>(QStringLiteral("redoList"));
        QStringList rsteps;
        if (rlist) {
            emit rlist->aboutToShow();
            for (QAction *a : rlist->actions())
                rsteps << a->text();
        }
        check(rsteps == QStringList({QStringLiteral("1  Bold"), QStringLiteral("2  Paste"), QStringLiteral("3  Delete")}),
              QStringLiteral("UR-12: the Redo list holds the three steps, the next one first"), rsteps.join(QLatin1String(" | ")));
        Q_UNUSED(before)
        // the keyboard in the binder: nothing to undo there, Undo is off and says nothing
        if (auto *tree = w.findChild<BinderTree *>()) {
            tree->setFocus();
            settle(150);
            check(!undo->isEnabled() && plain(undo->text()) == QLatin1String("Undo"),
                  QStringLiteral("UR-12: with the keyboard in the binder, Undo is off (it cannot bring documents back)"), undo->text());
        }
        ed->setFocus();
        settle(100);
        check(undo->isEnabled(), QStringLiteral("UR-12: back in the text, Undo is on again"));
    }

    // ---- UR-14: the binder's empty space
    if (auto *tree = w.findChild<BinderTree *>()) {
        const QPoint below(10, tree->viewport()->height() - 6);
        check(!tree->indexAt(below).isValid(), QStringLiteral("(the place below the items is empty)"));
        QString name;
        const QStringList entries = popupEntries([&] { emit tree->customContextMenuRequested(below); }, &name);
        info(QStringLiteral("empty space: %1").arg(entries.join(QLatin1String(" | "))));
        check(name == QLatin1String("binderBackgroundMenu") && entries.size() == 4 && entries.value(0).startsWith(QLatin1String("New Text"))
                  && entries.value(1).startsWith(QLatin1String("New Folder")) && entries.value(2) == QLatin1String("New From Template")
                  && entries.value(3) == QLatin1String("Appearance"),
              QStringLiteral("UR-14: right-click on the binder's empty space: New Text, New Folder, New From Template, Appearance"),
              entries.join(QLatin1String(" | ")));
        // on a document the full menu is there
        const QModelIndex first = tree->model()->index(0, 0);
        const QStringList onItem = popupEntries([&] { emit tree->customContextMenuRequested(tree->visualRect(first).center()); }, &name);
        check(name == QLatin1String("itemContextMenu") && onItem.size() > 8 && onItem.contains(QStringLiteral("Rename")),
              QStringLiteral("UR-14: on a document, the document's commands are there as before"), onItem.join(QLatin1String(" | ")));
    }

    // ---- UR-16: Close in Find by Formatting and in Print Preview
    if (QAction *ff = action("findFormat")) {
        ff->trigger();
        settle(200);
        auto *win = w.findChild<QDialog *>(QStringLiteral("formatFindWindow"));
        auto *close = win ? win->findChild<QPushButton *>(QStringLiteral("formatClose")) : nullptr;
        check(close && close->isVisible(), QStringLiteral("UR-16: Find by Formatting has a Close button"));
        if (close) {
            close->click();
            settle(100);
        }
        check(win && !win->isVisible(), QStringLiteral("UR-16: Close closes Find by Formatting"));
    }
    if (ed) {
        ed->setFocus();
        settle(50);
        auto found = std::make_shared<bool>(false);
        auto tries = std::make_shared<int>(0);
        auto poll = std::make_shared<std::function<void()>>();
        *poll = [found, tries, poll] {
            if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                d && d->objectName() == QLatin1String("printPreviewDialog")) {
                if (auto *box = d->findChild<QDialogButtonBox *>(QStringLiteral("windowClose"))) {
                    if (QPushButton *b = box->button(QDialogButtonBox::Close)) {
                        *found = b->isVisible();
                        b->click();
                        return;
                    }
                }
                d->reject();
                return;
            }
            if (++*tries < 100)
                QTimer::singleShot(50, qApp, *poll);
        };
        QTimer::singleShot(50, qApp, *poll);
        action("printPreview")->trigger();
        settle(200);
        check(*found && !QApplication::activeModalWidget(), QStringLiteral("UR-16: Print Preview has a Close button, and it closes the preview"));
    }

    // ---- UR-08: the busy cursor while Project Statistics counts
    {
        auto busy = std::make_shared<bool>(false);
        QTimer::singleShot(0, qApp, [busy] {
            *busy = QApplication::overrideCursor() && QApplication::overrideCursor()->shape() == Qt::WaitCursor;
        });
        auto tries = std::make_shared<int>(0);
        auto poll = std::make_shared<std::function<void()>>();
        *poll = [tries, poll] {
            if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                d->reject();
                return;
            }
            if (++*tries < 60)
                QTimer::singleShot(50, qApp, *poll);
        };
        QTimer::singleShot(50, qApp, *poll);
        action("statistics")->trigger();
        settle(100);
        check(*busy && !QApplication::overrideCursor(),
              QStringLiteral("UR-08: Project Statistics shows the busy cursor while it counts, and takes it away"));
    }

    // ---- UR-07: shortcuts for the synopsis, the notes, Back Up Now and the first styles of the style list
    {
        QAction *syn = action("goSynopsis"), *notes = action("goNotes"), *backup = action("backupNow");
        check(syn && syn->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Y) && notes
                  && notes->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N) && backup
                  && backup->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B),
              QStringLiteral("UR-07: Go to Synopsis Ctrl+Alt+Y, Go to Document Notes Ctrl+Alt+N, Back Up Now Ctrl+Shift+B"));
        if (ed)
            ed->setFocus();
        settle(50);
        if (syn && syn->isEnabled()) {
            QTest::keySequence(&w, syn->shortcut());
            settle(150);
        }
        QWidget *f = QApplication::focusWidget();
        check(f && qobject_cast<QPlainTextEdit *>(f) && static_cast<QPlainTextEdit *>(f)->placeholderText().startsWith(QLatin1String("Synopsis:")),
              QStringLiteral("UR-07: Ctrl+Alt+Y puts the keyboard into the synopsis"), f ? QString::fromLatin1(f->metaObject()->className()) : QStringLiteral("none"));
        if (notes && notes->isEnabled()) {
            QTest::keySequence(&w, notes->shortcut());
            settle(150);
        }
        f = QApplication::focusWidget();
        bool inInspector = false;
        for (QWidget *x = f; x; x = x->parentWidget())
            inInspector = inInspector || qobject_cast<Inspector *>(x);
        check(f && qobject_cast<TextEditor *>(f) && inInspector && f != ed,
              QStringLiteral("UR-07: Ctrl+Alt+N puts the keyboard into the document notes"), f ? QString::fromLatin1(f->metaObject()->className()) : QStringLiteral("none"));
        // the first styles of the style list without a shortcut get Ctrl+Alt+4 ... 9
        QStringList expect;
        for (const stylelist::Entry &e : stylelist::all(w.project()))
            if (e.shown && !e.name.isEmpty() && e.name != QLatin1String("Heading 1") && e.name != QLatin1String("Heading 2")
                && e.name != QLatin1String("Heading 3") && expect.size() < 6)
                expect << e.name;
        QStringList got;
        for (int k = 4; k <= 9; ++k)
            for (QAction *a : w.actions())
                if (a->objectName().startsWith(QLatin1String("style:")) && a->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key(Qt::Key_0 + k)))
                    got << a->data().toString();
        check(!expect.isEmpty() && got == expect.mid(0, got.size()) && got.size() == qMin<int>(6, int(expect.size())),
              QStringLiteral("UR-07: Ctrl+Alt+4 ... 9 apply the first styles of the style list"), got.join(QLatin1String(", ")) + QStringLiteral(" / ") + expect.join(QLatin1String(", ")));
        // no shortcut of the window is given twice
        QMap<QString, QStringList> byKey;
        for (QAction *a : w.actions())
            for (const QKeySequence &k : a->shortcuts())
                if (!k.isEmpty())
                    byKey[k.toString()] << a->objectName();
        QStringList twice;
        for (auto it = byKey.cbegin(); it != byKey.cend(); ++it)
            if (it.value().size() > 1)
                twice << it.key() + QStringLiteral(": ") + it.value().join(QLatin1String(", "));
        check(twice.isEmpty(), QStringLiteral("UR-07: no shortcut of the window belongs to two commands"), twice.join(QLatin1String("; ")));
    }

    // ---- UR-18: digits grouped as the language writes them, switchable
    {
        AppSettings &as = AppSettings::instance();
        check(as.groupDigits && QLocale().toString(44825) == QLatin1String("44,825") && text::plural(1234, QStringLiteral("word"), QStringLiteral("words")) == QLatin1String("1,234 words"),
              QStringLiteral("UR-18: 44825 is written 44,825 (English)"), QLocale().toString(44825));
        as.groupDigits = false;
        as.save();
        check(QLocale().toString(44825) == QLatin1String("44825"), QStringLiteral("UR-18: the option off: 44825"), QLocale().toString(44825));
        as.groupDigits = true;
        as.save();
        OptionsDialog d(&w, 0);
        auto *box = d.findChild<QCheckBox *>(QStringLiteral("optGroupDigits"));
        check(box && box->isChecked() && box->text().contains(QLatin1String("44,825")) && optstore::keyOf(box) == QLatin1String("app/groupDigits"),
              QStringLiteral("UR-18: File > Options > General > Startup: Group the digits of large numbers (44,825), kept by the program"));
        if (box) {
            box->setChecked(false);
            d.apply();
        }
        check(!as.groupDigits && QLocale().toString(44825) == QLatin1String("44825"), QStringLiteral("UR-18: unticked and applied: 44825"));
        if (box) {
            box->setChecked(true);
            d.apply();
        }
        d.reject();
        check(as.groupDigits && QLocale().toString(44825) == QLatin1String("44,825"), QStringLiteral("UR-18: ticked again: 44,825"));
        // only the digits change: units "as in the system settings" and the paper of new projects follow the system as
        // before (the first release run of 0.90.1 set en_US: inches and US Letter on a German desktop; m6, v0212)
        const bool systemMetric = QLocale::system().measurementSystem() == QLocale::MetricSystem;
        const QString savedUnit = as.rulerUnit;
        as.rulerUnit = QString(); // as in the system settings
        check(units::metric() == systemMetric && (PageSetup::forLocale().paper == QLatin1String("US Letter")) == !systemMetric
                  && QLocale().decimalPoint() == QLocale::system().decimalPoint(),
              QStringLiteral("UR-18: units, the paper of new projects and the decimal mark still follow the system"),
              QStringLiteral("system metric %1, units metric %2, paper %3").arg(systemMetric).arg(units::metric()).arg(PageSetup::forLocale().paper));
        as.rulerUnit = savedUnit;
    }

    // ---- UR-05: the question before a link to a file opens
    {
        QTemporaryDir dir;
        const QString name = QString(QLatin1Char('n')).repeated(236) + QStringLiteral("-end.txt");
        const QString path = dir.path() + QStringLiteral("/a folder/") + name;
        QDir().mkpath(QFileInfo(path).absolutePath());
        {
            QFile f(path);
            if (f.open(QIODevice::WriteOnly))
                f.write("A plain text.\n");
        }
        const QString q = links::fileQuestion(path);
        QString flat = q;
        flat.remove(QChar(0x200B));
        check(flat.contains(name) && flat.contains(QLatin1String("Kind: ")) && flat.contains(QLatin1String("(.txt)"))
                  && q.contains(QChar(0x200B)),
              QStringLiteral("UR-05: the question names the kind of file and the whole name, which may wrap"), flat.left(300));
        links::setOpenerForTests([](const QUrl &) {});
        auto seen = std::make_shared<QString>();
        auto tries = std::make_shared<int>(0);
        auto poll = std::make_shared<std::function<void()>>();
        *poll = [seen, tries, poll] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                QLabel *label = box->findChild<QLabel *>(QStringLiteral("qt_msgbox_label"));
                const QRect avail = box->screen()->availableGeometry();
                bool allIn = label && box->width() <= avail.width();
                if (label) {
                    // every line of the label fits its width: nothing runs past the edge
                    const QRect inBox(label->mapTo(box, QPoint(0, 0)), label->size());
                    allIn = allIn && box->rect().contains(inBox) && label->wordWrap()
                            && label->heightForWidth(label->width()) <= label->height() + 2;
                }
                *seen = allIn ? QStringLiteral("fits") : QStringLiteral("cut %1 x %2").arg(box->width()).arg(box->height());
                box->reject();
                return;
            }
            if (++*tries < 60)
                QTimer::singleShot(50, qApp, *poll);
        };
        QTimer::singleShot(50, qApp, *poll);
        const bool opened = links::open(&w, QUrl::fromLocalFile(path).toString());
        settle(100);
        check(!opened && *seen == QLatin1String("fits"), QStringLiteral("UR-05: the question shows all of the long name inside the window"), *seen);
        links::setOpenerForTests({});
    }
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV0901Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV0901Test"));
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

    // the helper alone: a submenu of the menu goes, a menu kept elsewhere and added again stays
    {
        QMenu outer;
        QMenu kept(QStringLiteral("Kept"));
        QPointer<QMenu> made = outer.addMenu(QStringLiteral("Made"));
        made->addMenu(QStringLiteral("Deeper"))->addAction(QStringLiteral("x"));
        outer.addMenu(&kept);
        outer.addAction(QStringLiteral("An entry"));
        clearMenu(&outer);
        settle(30);
        check(outer.actions().isEmpty() && made.isNull() && kept.actions().isEmpty() && !kept.title().isEmpty(),
              QStringLiteral("clearMenu: the menu is empty, its own submenu is gone, a menu kept elsewhere stays"));
    }

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
    if (app.arguments().contains(QStringLiteral("--compile-only"))) {
        compileChecks(p, QStringLiteral("scale 2: "));
        std::printf("\n%d failure(s)\n", failures);
        return failures;
    }
    BinderItem *target = nullptr;
    for (int c = 1; c <= 6; ++c) {
        BinderItem *ch = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Chapter %1").arg(c));
        for (int s = 1; s <= 4; ++s) {
            BinderItem *sc = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("Scene %1.%2").arg(c).arg(s));
            QTextCursor(p->docs()->text(sc->id)).insertText(QStringLiteral("The tide came in at scene %1.%2.").arg(c).arg(s));
            if (c == 5 && s == 3)
                target = sc;
        }
    }
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

    QMenu *nav = topMenu(w, QStringLiteral("Navigate"));
    QMenu *docs = topMenu(w, QStringLiteral("Documents"));
    QMenu *goTo = w.findChild<QMenu *>(QStringLiteral("goToMenu"));
    QMenu *openQr = w.findChild<QMenu *>(QStringLiteral("openQuickRefMenu"));
    QMenu *styleMenu = w.findChild<QMenu *>(QStringLiteral("styleMenu"));
    QMenu *iconMenu = docs ? menuTitled(docs, QStringLiteral("Change Icon")) : nullptr;
    check(nav && docs && goTo && openQr && styleMenu && iconMenu, QStringLiteral("(the four menus found)"));

    // the binder as nested menus: the Draft, Research and the six chapters at least
    steadyMenu(w, goTo, QStringLiteral("Navigate > Go To"), 6);
    steadyMenu(w, openQr, QStringLiteral("Navigate > Open Quick Reference"), 6);
    steadyMenu(w, iconMenu, QStringLiteral("Documents > Change Icon"), 2);
    steadyMenu(w, styleMenu, QStringLiteral("Format > Style"), 2);

    // the entries of the menu made last still work
    if (goTo && target) {
        emit goTo->aboutToShow();
        QAction *go = entryTitled(goTo, target->title);
        check(go != nullptr, QStringLiteral("Go To: the scene is in the menu after 32 openings"));
        if (go) {
            go->trigger();
            settle(200);
            EditorPane *pane = firstPane(w);
            check(pane && pane->items().contains(target), QStringLiteral("Go To: its entry shows the scene"));
        }
    }
    if (openQr && target) {
        emit openQr->aboutToShow();
        QAction *open = entryTitled(openQr, target->title);
        check(open != nullptr, QStringLiteral("Open Quick Reference: the scene is in the menu"));
        if (open) {
            const int before = int(panels(w).size());
            open->trigger();
            settle(200);
            bool shown = false;
            for (QuickRefWindow *q : panels(w))
                shown = shown || q->item() == target;
            check(shown && int(panels(w).size()) == before + 1,
                  QStringLiteral("Open Quick Reference: its entry opens the scene in a panel"));
        }
    }
    // a binder change reaches the menu (it is filled from the binder each time)
    if (goTo && p) {
        BinderItem *added = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Epilogue"));
        emit goTo->aboutToShow();
        settle(30);
        check(added && entryTitled(goTo, QStringLiteral("Epilogue")) != nullptr, QStringLiteral("Go To: a new document is in the menu"));
    }

    optionsChecks(w);
    windowChecks(w);
    compileChecks(w.project(), QString());
    usabilityChecks(w);
    {
        // the same at scale 2, in a child run (the scale is read when the program starts)
        QProcess child;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_SCALE_FACTOR"), QStringLiteral("2"));
        child.setProcessEnvironment(env);
        QStringList args = {shotDir.isEmpty() ? QStringLiteral("") : shotDir, QStringLiteral("--compile-only")};
        child.start(QCoreApplication::applicationFilePath(), args);
        const bool done = child.waitForFinished(120000);
        const QString out = QString::fromUtf8(child.readAllStandardOutput());
        for (const QString &line : out.split(QLatin1Char('\n')))
            if (line.startsWith(QLatin1String("PASS")) || line.startsWith(QLatin1String("FAIL")) || line.startsWith(QLatin1String("      ")))
                info(line.trimmed());
        check(done && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
              QStringLiteral("UR-19: the compile window's checks pass at scale 2 (a child run)"), QString::number(child.exitCode()));
    }

    w.closeProject();
    delete wp;
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
