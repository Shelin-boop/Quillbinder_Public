// Scripted run for version 0.60.4: the four paragraph buttons of the format bar (space before,
// space after, first line indent, indent of the whole paragraph): their place after the list
// buttons, their icons, the values they set on the selected paragraphs, "Other...", the value
// at the cursor ticked, undo, saving, off without a text; a format bar the writer arranged gets
// them once; the README says that Quillbinder works under Kubuntu 26.04 LTS with Wayland.
// Saves screenshots into the folder given as first argument. Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_v0604_tests /tmp/shots
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "compile/PluginManager.h"
#include "text/DirectFormat.h"
#include "text/DisplayFormat.h"
#include "ui/AppSettings.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include "AppInfo.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QInputDialog>
#include <QMenu>
#include <QPainter>
#include <QSettings>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>
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

static EditorPane *visiblePane(MainWindow &w)
{
    for (EditorPane *ep : w.findChildren<EditorPane *>())
        if (ep->isVisible())
            return ep;
    return nullptr;
}

static const char *kIds[4] = {"spaceBefore", "spaceAfter", "indentFirst", "indentLeft"};

// the menu of a button as it is when it opens
static QMenu *openMenu(MainWindow &w, int which)
{
    auto *b = w.findChild<QToolButton *>(QLatin1String("para_") + QLatin1String(kIds[which]));
    if (!b || !b->menu())
        return nullptr;
    emit b->menu()->aboutToShow();
    return b->menu();
}

static QAction *entry(QMenu *m, double value)
{
    for (QAction *a : m->actions())
        if (!a->isSeparator() && a->data().isValid() && qAbs(a->data().toDouble() - value) < 1e-6)
            return a;
    return nullptr;
}

static QAction *ticked(QMenu *m)
{
    for (QAction *a : m->actions())
        if (a->isChecked())
            return a;
    return nullptr;
}

static bool near(double a, double b) { return qAbs(a - b) < 0.05; }

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV0604Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV0604Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);

    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.60.4")) >= 0
              && QLatin1String(QB_APP_VERSION_SHOWN).endsWith(QLatin1String(" " QB_APP_VERSION_LABEL)),
          QStringLiteral("version 0.60.4 or newer, shown with its label"), QStringLiteral(QB_APP_VERSION_SHOWN));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));

    // ---- a format bar arranged by the writer gets the buttons once, after the list buttons
    const QStringList own = {QStringLiteral("styleCombo"), QStringLiteral("bold"), QStringLiteral("bullets"), QStringLiteral("numbers"),
                             QStringLiteral("|"), QStringLiteral("comment")};
    {
        QSettings s;
        s.setValue(QStringLiteral("toolbars/format"), own);
        s.sync();
    }
    AppSettings &as = AppSettings::instance();
    as.load();
    const QStringList added = as.toolbarFormat;
    check(added == QStringList{QStringLiteral("styleCombo"), QStringLiteral("bold"), QStringLiteral("bullets"), QStringLiteral("numbers"),
                               QStringLiteral("|"), QStringLiteral("spaceBefore"), QStringLiteral("spaceAfter"), QStringLiteral("indentFirst"),
                               QStringLiteral("indentLeft"), QStringLiteral("|"), QStringLiteral("comment")},
          QStringLiteral("a format bar arranged by the writer gets the four buttons after the list buttons"), added.join(QLatin1Char(' ')));
    {
        QSettings s;
        s.setValue(QStringLiteral("toolbars/format"), own); // the writer took them away again
        s.sync();
    }
    as.load();
    check(as.toolbarFormat == own, QStringLiteral("... only once: taken away, they stay away"), as.toolbarFormat.join(QLatin1Char(' ')));
    as.toolbarFormat.clear(); // the standard bar for the rest of the run
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();

    // ---- the README
    {
        QFile f(QStringLiteral(QB_TEST_SOURCE_DIR "/../README.md"));
        const QString readme = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        // the writer's own words, first written into the public README on GitHub (2026-11-06)
        const int download = int(readme.indexOf(QLatin1String("## Download")));
        const int wayland = int(readme.indexOf(QLatin1String("Works under Kubuntu 26.04 LTS\nWayland.")));
        check(download > 0 && wayland > 0 && wayland < download,
              QStringLiteral("README: \"Works under Kubuntu 26.04 LTS Wayland.\" in the introduction"));
    }

    // ---- the icons: drawn, all different, light and dark
    {
        QList<QImage> imgs;
        bool drawn = true;
        for (const char *name : {icons::kSpaceBefore, icons::kSpaceAfter, icons::kIndentFirst, icons::kIndentLeft}) {
            const QImage im = icons::action(QLatin1String(name)).pixmap(22, 22).toImage();
            int ink = 0;
            for (int y = 0; y < im.height(); ++y)
                for (int x = 0; x < im.width(); ++x)
                    ink += qAlpha(im.pixel(x, y)) > 100;
            drawn = drawn && ink > 40;
            imgs << im;
        }
        bool distinct = true;
        for (int i = 0; i < imgs.size(); ++i)
            for (int j = i + 1; j < imgs.size(); ++j)
                distinct = distinct && imgs[i] != imgs[j];
        check(drawn && distinct, QStringLiteral("the four icons are drawn and differ from each other"));
        QImage sheet(4 * 40, 40, QImage::Format_ARGB32);
        sheet.fill(Qt::white);
        QPainter p(&sheet);
        for (int i = 0; i < 4; ++i)
            p.drawImage(i * 40 + 4, 4, icons::action(QLatin1String(i == 0 ? icons::kSpaceBefore : i == 1 ? icons::kSpaceAfter : i == 2 ? icons::kIndentFirst : icons::kIndentLeft)).pixmap(32, 32).toImage());
        p.end();
        sheet.save(outDir + QStringLiteral("/01-icons.png"));
    }

    QTemporaryDir work;
    templates::Info blank;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("blank"))
            blank = t;
    QString err;
    auto proj = templates::createProject(blank, work.path(), QStringLiteral("Paragraph Buttons"), &err);
    check(proj != nullptr, QStringLiteral("(project made)"), err);
    if (!proj)
        return 1;
    Project *p = proj.get();
    BinderItem *doc = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Barn"));
    {
        QTextCursor c(p->docs()->text(doc->id));
        c.insertText(QStringLiteral("The barn was warm."));
        c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
        c.insertText(QStringLiteral("Rain fell on the roof."));
        c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
        c.insertText(QStringLiteral("She slept."));
    }
    const QString projectFolder = p->folderPath();
    p->save(&err);

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1500, 900);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(proj)), QStringLiteral("(project open)"));
    settle(800);

    // ---- place on the format bar
    auto *bar = w.findChild<QToolBar *>(QStringLiteral("formatBar"));
    QStringList order;
    if (bar)
        for (QAction *a : bar->actions()) {
            if (auto *wa = qobject_cast<QWidgetAction *>(a); wa && wa->defaultWidget())
                order << wa->defaultWidget()->objectName();
            else
                order << (a->isSeparator() ? QStringLiteral("|") : a->objectName());
        }
    const int at = int(order.indexOf(QStringLiteral("numbers")));
    check(at >= 0 && order.mid(at + 1, 6)
                         == QStringList{QStringLiteral("|"), QStringLiteral("para_spaceBefore"), QStringLiteral("para_spaceAfter"),
                                        QStringLiteral("para_indentFirst"), QStringLiteral("para_indentLeft"), QStringLiteral("|")},
          QStringLiteral("format bar: space before, space after, first line indent, whole paragraph indent after the list buttons"),
          order.join(QLatin1Char(' ')));

    EditorPane *pane = visiblePane(w);
    if (!pane)
        return 1;
    pane->showItems({doc}, false, int(ViewMode::Single));
    settle(400);
    TextEditor *e = pane->currentEditor();
    if (!e)
        return 1;
    e->setFocus();
    settle(100);
    QTextDocument *d = e->document();
    auto block = [d](int i) { return d->findBlockByNumber(i).blockFormat(); };

    // ---- space after: 12 pt on the first two paragraphs
    {
        QTextCursor c(d);
        c.setPosition(2);
        c.setPosition(d->findBlockByNumber(1).position() + 3, QTextCursor::KeepAnchor);
        e->setTextCursor(c);
        settle(100);
        const double before2 = block(2).bottomMargin();
        QMenu *m = openMenu(w, 1);
        QStringList texts;
        if (m)
            for (QAction *a : m->actions())
                texts << (a->isSeparator() ? QStringLiteral("|") : a->text());
        check(texts == QStringList{QStringLiteral("0 pt"), QStringLiteral("3 pt"), QStringLiteral("6 pt"), QStringLiteral("9 pt"),
                                   QStringLiteral("12 pt"), QStringLiteral("18 pt"), QStringLiteral("|"),
                                   texts.value(7).startsWith(QLatin1String("Other")) ? texts.value(7) : QStringLiteral("Other...")}
                  && texts.size() == 8,
              QStringLiteral("space after: 0, 3, 6, 9, 12, 18 pt and Other..."), texts.join(QLatin1Char(' ')));
        if (QAction *a = m ? entry(m, 12) : nullptr)
            a->trigger();
        settle(150);
        const double px = direct::ptToPx(12);
        check(near(block(0).bottomMargin(), px) && near(block(1).bottomMargin(), px) && near(block(2).bottomMargin(), before2),
              QStringLiteral("... 12 pt after both selected paragraphs, the third one unchanged"),
              QStringLiteral("%1 %2 %3 px").arg(block(0).bottomMargin()).arg(block(1).bottomMargin()).arg(block(2).bottomMargin()));
        m = openMenu(w, 1);
        QAction *t = m ? ticked(m) : nullptr;
        check(t && t->text() == QLatin1String("12 pt"), QStringLiteral("... the menu ticks the value of the paragraph at the cursor"),
              t ? t->text() : QStringLiteral("nothing ticked"));
        w.grab().save(outDir + QStringLiteral("/02-space-after.png"));
        e->undo();
        settle(100);
        check(!near(block(0).bottomMargin(), px) && !near(block(1).bottomMargin(), px), QStringLiteral("... one Undo takes it back"));
    }

    // ---- space before: 6 pt, and Other... 7.5 pt
    {
        QTextCursor c(d);
        c.setPosition(d->findBlockByNumber(1).position() + 2);
        e->setTextCursor(c);
        settle(100);
        QMenu *m = openMenu(w, 0);
        if (QAction *a = m ? entry(m, 6) : nullptr)
            a->trigger();
        settle(150);
        check(near(block(1).topMargin(), direct::ptToPx(6)), QStringLiteral("space before 6 pt: the paragraph at the cursor"),
              QString::number(block(1).topMargin()));
        m = openMenu(w, 0);
        QAction *other = nullptr;
        for (QAction *a : m ? m->actions() : QList<QAction *>())
            if (a->objectName() == QLatin1String("paraOther"))
                other = a;
        bool asked = false;
        QTimer::singleShot(300, [&asked] {
            if (auto *dlg = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
                asked = true;
                dlg->setDoubleValue(7.5);
                dlg->accept();
            }
        });
        if (other)
            other->trigger();
        settle(200);
        check(asked && near(block(1).topMargin(), direct::ptToPx(7.5)), QStringLiteral("... Other... asks and sets 7.5 pt"),
              QString::number(block(1).topMargin()));
        m = openMenu(w, 0);
        QAction *t = m ? ticked(m) : nullptr;
        check(t && t->objectName() == QLatin1String("paraOther") && t->text().contains(QLatin1String("7.5 pt")),
              QStringLiteral("... a value off the list shows in \"Other (7.5 pt)...\""), t ? t->text() : QString());
    }

    // ---- the indents, in the ruler's unit
    double firstValue = 0, leftValue = 0;
    {
        QTextCursor c(d);
        c.setPosition(d->findBlockByNumber(2).position() + 1);
        e->setTextCursor(c);
        settle(100);
        QMenu *m = openMenu(w, 2);
        QList<double> vals;
        for (QAction *a : m ? m->actions() : QList<QAction *>())
            if (a->data().isValid())
                vals << a->data().toDouble();
        firstValue = vals.value(3);
        if (QAction *a = m ? entry(m, firstValue) : nullptr)
            a->trigger();
        settle(150);
        check(vals.size() == 6 && firstValue > 0 && near(block(2).textIndent(), units::toPx(firstValue)),
              QStringLiteral("first line indent: the fourth value of the list (%1%2)").arg(firstValue).arg(units::suffix()),
              QString::number(block(2).textIndent()));
        m = openMenu(w, 3);
        vals.clear();
        for (QAction *a : m ? m->actions() : QList<QAction *>())
            if (a->data().isValid())
                vals << a->data().toDouble();
        leftValue = vals.value(2);
        if (QAction *a = m ? entry(m, leftValue) : nullptr)
            a->trigger();
        settle(150);
        check(vals.size() == 6 && near(block(2).leftMargin(), units::toPx(leftValue)) && near(block(2).textIndent(), units::toPx(firstValue)),
              QStringLiteral("whole paragraph indent (%1%2); the first line indent stays").arg(leftValue).arg(units::suffix()),
              QString::number(block(2).leftMargin()));
        w.grab().save(outDir + QStringLiteral("/03-indents.png"));
    }

    // ---- kept by saving
    {
        w.findChild<QAction *>(QStringLiteral("save"))->trigger();
        settle(400);
        QString e2;
        auto again = Project::open(projectFolder, &e2);
        QTextDocument *saved = again ? again->docs()->text(doc->id) : nullptr;
        const QTextBlockFormat b1 = saved ? saved->findBlockByNumber(1).blockFormat() : QTextBlockFormat();
        const QTextBlockFormat b2 = saved ? saved->findBlockByNumber(2).blockFormat() : QTextBlockFormat();
        check(saved && near(b1.topMargin(), direct::ptToPx(7.5)) && near(b2.textIndent(), units::toPx(firstValue))
                  && near(b2.leftMargin(), units::toPx(leftValue)),
              QStringLiteral("saved and opened again: the spacing and the indents are there"),
              QStringLiteral("%1 %2 %3").arg(b1.topMargin()).arg(b2.textIndent()).arg(b2.leftMargin()));
    }

    // ---- off without a text
    pane->showItems({doc}, false, int(ViewMode::Corkboard));
    settle(300);
    bool allOff = true;
    for (const char *id : kIds)
        if (auto *b = w.findChild<QToolButton *>(QLatin1String("para_") + QLatin1String(id)))
            allOff = allOff && !b->isEnabled();
    check(allOff, QStringLiteral("on the corkboard (no text) the four buttons are off"));
    pane->showItems({doc}, false, int(ViewMode::Single));
    settle(300);
    if (TextEditor *e3 = pane->currentEditor())
        e3->setFocus();
    settle(150);
    bool allOn = true;
    for (const char *id : kIds)
        if (auto *b = w.findChild<QToolButton *>(QLatin1String("para_") + QLatin1String(id)))
            allOn = allOn && b->isEnabled();
    check(allOn, QStringLiteral("... and on again in the text"));

    settle(200);
    std::printf("\n%d failure(s)\n", failures);
    delete wp;
    return failures;
}
