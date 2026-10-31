// Scripted UI run: creates a project, writes, switches views, opens the
// dialogs and saves screenshots of everything into the folder given as
// first argument. Exit code = number of failed checks.
//
//   QT_QPA_PLATFORM=offscreen ./qb_ui_tests /tmp/shots
#include "compile/Compiler.h"
#include "compile/PluginManager.h"
#include "compile/CompileFormat.h"
#include "core/Addons.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "core/Snapshots.h"
#include "ui/AddonDialogs.h"
#include "ui/AppSettings.h"
#include "ui/ProjectLook.h"
#include "ui/BinderPanel.h"
#include "ui/Commands.h"
#include "ui/KeyboardPage.h"
#include "ui/Layouts.h"
#include "ui/ToolbarConfig.h"
#include "ui/CompileDialog.h"
#include "ui/EditorPane.h"
#include "ui/FormatDialogs.h"
#include "ui/Ruler.h"
#include "text/DirectFormat.h"
#include "ui/Inspector.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/ProjectSettingsDialog.h"
#include "ui/StartDialog.h"
#include "ui/StyleDialogs.h"
#include "text/Styles.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"
#include "ui/TextColumnView.h"
#include "ui/Corkboard.h"
#include "ui/Outliner.h"
#include "ui/ThemeDialogs.h"
#include "ui/TextEditor.h"
#include "ui/ToolDialogs.h"
#include "ui/Windows.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QFile>
#include <QIcon>
#include <QGraphicsScene>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QMessageBox>
#include <QRadioButton>
#include <QRegularExpression>
#include <QToolButton>
#include <QMenu>
#include <QMenuBar>
#include <QSplitter>
#include <QToolBar>
#include <QTreeWidget>
#include <QKeySequenceEdit>
#include <QFontComboBox>
#include <functional>
#include <QSettings>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLayout>
#include <QTimer>
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
    settle(700);
    const QPixmap pm = w->grab();
    pm.save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}

static QAction *action(QWidget *w, const QString &text)
{
    for (QAction *a : w->findChildren<QAction *>()) {
        QString t = a->text();
        t.remove(QLatin1Char('&'));
        if (t == text)
            return a;
    }
    std::printf("      (action not found: %s)\n", qPrintable(text));
    return nullptr;
}

static QAction *byKey(QWidget *w, const QString &key)
{
    QAction *a = w->findChild<QAction *>(key);
    if (!a)
        std::printf("      (action key not found: %s)\n", qPrintable(key));
    return a;
}

// view buttons toggle: only trigger when not already active
static void setView(QWidget *w, const QString &key)
{
    if (QAction *a = byKey(w, key))
        if (!a->isChecked())
            a->trigger();
    settle(200);
}

static void trigger(QWidget *w, const QString &text)
{
    if (QAction *a = action(w, text))
        a->trigger();
    settle();
}

// "&Files…\tCtrl+I" -> "Files…"
static QString menuText(QString t)
{
    const int tab = int(t.indexOf(QLatin1Char('\t')));
    if (tab >= 0)
        t.truncate(tab);
    t.remove(QLatin1Char('&'));
    return t;
}

// Entry of the main menu by its path, e.g. {"File", "Import", "Files…"}.
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

static BinderItem *findTitle(BinderItem *root, const QString &title)
{
    if (root->title == title)
        return root;
    for (BinderItem *c : root->children)
        if (BinderItem *f = findTitle(c, title))
            return f;
    return nullptr;
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qputenv("QB_PLUGIN_PATH", QByteArray(QB_TEST_PLUGIN_DIR));
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderUiTest"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderUiTest"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QFont f(QStringLiteral("Noto Sans"), 10);
    QApplication::setFont(f);

    const QPalette desktopPalette = QApplication::palette();
    AppSettings::instance().load();
    AppSettings::instance().forename = QStringLiteral("Ada");
    AppSettings::instance().surname = QStringLiteral("Writer");
    // (0.40.2) these checks were written for one look for the whole program: the open project
    // shows the look of the settings, as before, and has no border beside the text
    // (tests/v0402_tests.cpp checks the look per project)
    look::setFollowSettings(true);
    AppSettings::instance().textBorder = 0;
    theme::apply();
    compile::PluginManager::instance().load();
    {
        // settings must survive a round trip through the INI file
        AppSettings::instance().save();
        const QString file = QSettings().fileName();
        QSettings fresh(file, QSettings::IniFormat);
        check(fresh.value(QStringLiteral("app/forename")).toString() == QLatin1String("Ada"),
              QStringLiteral("settings round trip"), fresh.allKeys().join(QLatin1Char(',')));
    }

    // ---------------------------------------------------------------- start window
    {
        StartDialog sd(nullptr, false);
        sd.resize(900, 620);
        sd.show();
        shot(&sd, QStringLiteral("01-start-window"));
        sd.close();
    }

    // ---------------------------------------------------------------- project
    QTemporaryDir projectsTmp;
    // optional second argument: keep the test project in this folder
    const QString projectsPath = argc > 2 ? QString::fromLocal8Bit(argv[2]) : projectsTmp.path();
    QDir().mkpath(projectsPath);
    templates::Info novel;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("novel"))
            novel = t;
    QString err;
    auto p = templates::createProject(novel, projectsPath, QStringLiteral("The Lighthouse"), &err);
    check(p != nullptr, QStringLiteral("create project from Novel template"), err);
    if (!p)
        return 1;
    const QString projectFile = p->indexPath();

    MainWindow w;
    w.resize(1440, 900);
    w.show();
    check(w.adoptProject(std::move(p)), QStringLiteral("open project in main window"));
    Project *proj = w.project();
    settle(400);
    shot(&w, QStringLiteral("02-main-window-new-project"));

    auto *binder = w.findChild<BinderPanel *>();
    QList<EditorPane *> panes = w.findChildren<EditorPane *>();
    check(binder && panes.size() == 2, QStringLiteral("binder and two editor panes exist"));

    // add chapters and scenes
    BinderModel *m = proj->model();
    BinderItem *ch1 = findTitle(proj->draft(), QStringLiteral("Chapter"));
    if (!ch1)
        ch1 = m->createItem(proj->draft(), 0, ItemType::Folder, QStringLiteral("Chapter"));
    ch1->title = QStringLiteral("The Keeper");
    BinderItem *scene1 = ch1->children.isEmpty() ? m->createItem(ch1, 0, ItemType::Text, QStringLiteral("Scene"))
                                                 : ch1->children.first();
    scene1->title = QStringLiteral("Counting Ships");
    scene1->synopsis = QStringLiteral("Tomas counts the ships and the numbers disagree.");
    BinderItem *scene2 = m->createItem(ch1, 1, ItemType::Text, QStringLiteral("The Logbook"));
    scene2->synopsis = QStringLiteral("He writes the difference in the margin.");
    BinderItem *scene3 = m->createItem(ch1, 2, ItemType::Text, QStringLiteral("Morning Fog"));
    scene3->synopsis = QStringLiteral("A ship that was never counted appears.");
    BinderItem *ch2 = m->createItem(proj->draft(), 1, ItemType::Folder, QStringLiteral("The Visitor"));
    BinderItem *scene4 = m->createItem(ch2, 0, ItemType::Text, QStringLiteral("A Knock at Night"));
    scene4->synopsis = QStringLiteral("Someone climbs the stairs in the storm.");
    templates::setMarkdown(proj, scene2,
                           QStringLiteral("He wrote the number in the margin: **forty-one**, not forty.\n"
                                          "The ink ran a little in the damp air.{>>check tide tables<<}\n"));
    templates::setMarkdown(proj, scene3, QStringLiteral("The fog came in *before* dawn.\n"));
    templates::setMarkdown(proj, scene4, QStringLiteral("# A Knock\nThe storm had lasted three days.\n"));
    if (!proj->labels.isEmpty())
        scene1->label = proj->labels.value(1).id;
    if (!proj->statuses.isEmpty())
        scene2->status = proj->statuses.value(2).id;
    m->refreshAll();
    settle();

    // write into scene 1 through the editor
    binder->select({scene1}, true);
    settle(300);
    EditorPane *pane = nullptr;
    for (EditorPane *ep : panes)
        if (ep->isVisible())
            pane = ep;
    TextEditor *ed = pane ? pane->currentEditor() : nullptr;
    check(ed != nullptr, QStringLiteral("scene opens in a text editor"));
    if (ed) {
        ed->setFocus();
        QTest::keyClicks(ed, QStringLiteral("The lighthouse keeper counted the ships twice each evening. "));
        QTest::keyClicks(ed, QStringLiteral("\"Forty,\" he said -- and wrote it down."));
        QTest::keyClick(ed, Qt::Key_Return);
        QTest::keyClicks(ed, QStringLiteral("The numbers never quite agreed."));
        // bold the word "twice"
        QTextCursor c(ed->document());
        const int pos = ed->document()->toPlainText().indexOf(QLatin1String("twice"));
        c.setPosition(pos);
        c.setPosition(pos + 5, QTextCursor::KeepAnchor);
        ed->setTextCursor(c);
        trigger(&w, QStringLiteral("Bold"));
        // a linked comment on "counted"
        const int pos2 = ed->document()->toPlainText().indexOf(QLatin1String("counted"));
        c.setPosition(pos2);
        c.setPosition(pos2 + 7, QTextCursor::KeepAnchor);
        ed->setTextCursor(c);
        ed->addLinkedNote(NoteKind::Comment, QStringLiteral("Is 'counted' too plain?"));
        c.movePosition(QTextCursor::End);
        ed->setTextCursor(c);
        settle(300);
        const QString plain = ed->document()->toPlainText();
        check(plain.contains(QChar(0x201C)) && plain.contains(QChar(0x2014)),
              QStringLiteral("smart quotes and em dash while typing"), plain);
    }
    shot(&w, QStringLiteral("03-editor-with-text"));

    // inspector tabs
    if (auto *insp = w.findChild<Inspector *>()) {
        shot(insp, QStringLiteral("04-inspector"));
    }

    // corkboard of chapter 1
    binder->select({ch1}, true);
    settle(300);
    setView(&w, QStringLiteral("viewCork"));
    shot(&w, QStringLiteral("05-corkboard"));
    check(pane && pane->mode() == ViewMode::Corkboard, QStringLiteral("folder shows as corkboard"));

    // outliner of the draft
    binder->select({proj->draft()}, true);
    settle(200);
    setView(&w, QStringLiteral("viewOutline"));
    shot(&w, QStringLiteral("06-outliner"));
    check(pane && pane->mode() == ViewMode::Outliner, QStringLiteral("outliner view"));

    // combined-text of chapter 1
    binder->select({ch1}, true);
    settle(200);
    setView(&w, QStringLiteral("viewText"));
    shot(&w, QStringLiteral("07-combined-text"));
    check(pane && pane->mode() == ViewMode::Combined, QStringLiteral("combined-text view"));

    // split editor
    trigger(&w, QStringLiteral("Split Horizontally"));
    binder->select({scene4}, true);
    settle(300);
    shot(&w, QStringLiteral("08-split"));
    trigger(&w, QStringLiteral("No Split"));

    // project search
    trigger(&w, QStringLiteral("Project Search…"));
    SearchQuery q;
    q.text = QStringLiteral("margin");
    binder->runSearch(q);
    settle(300);
    shot(&w, QStringLiteral("09-project-search"));
    binder->hideSearch();

    // format bar/menus
    if (QMenu *fm = w.findChild<QMenu *>()) {
        Q_UNUSED(fm);
    }

    // dark theme
    AppSettings::instance().theme = QStringLiteral("dark");
    theme::apply();
    binder->select({scene1}, true);
    settle(300);
    shot(&w, QStringLiteral("10-dark-theme"));
    AppSettings::instance().theme = QStringLiteral("system");
    theme::apply();

    // composition mode
    binder->select({scene1}, true);
    settle(200);
    trigger(&w, QStringLiteral("Composition Mode"));
    settle(500);
    CompositionWindow *comp = nullptr;
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (auto *cw = qobject_cast<CompositionWindow *>(tl))
            if (cw->isVisible())
                comp = cw;
    check(comp != nullptr, QStringLiteral("composition mode opens"));
    if (comp) {
        comp->resize(1440, 900);
        shot(comp, QStringLiteral("11-composition"));
        comp->close();
        settle(300);
    }

    // dialogs
    {
        ProjectSettingsDialog d(proj, 0, &w);
        d.resize(820, 600);
        d.show();
        shot(&d, QStringLiteral("12-project-settings"));
        d.reject();
    }
    {
        OptionsDialog d(&w, 0);
        d.resize(760, 560);
        d.show();
        shot(&d, QStringLiteral("13-options"));
        d.reject();
    }
    {
        CompileDialog d(proj, &w);
        d.resize(1100, 700);
        d.show();
        shot(&d, QStringLiteral("14-compile"));
        d.reject();
    }
    {
        TargetsDialog d(proj, &w);
        d.show();
        shot(&d, QStringLiteral("15-targets"));
        d.reject();
    }
    {
        KeywordsDialog d(proj, &w);
        d.show();
        shot(&d, QStringLiteral("16-keywords"));
        d.reject();
    }
    {
        StatisticsDialog d(proj, &w);
        d.show();
        shot(&d, QStringLiteral("17-statistics"));
        d.reject();
    }

    // ---------------------------------------------------------------- updates and add-ons
    {
        const QString dist = QStringLiteral(QB_TEST_PLUGIN_DIR "/../addons-dist");
        if (!QFile::exists(dist + QLatin1String("/themes-extra.qbpkg"))) {
            std::printf("      (add-on packages not built; run packaging/build-addons.sh <build> <build>/addons-dist)\n");
        } else {
            // the install window: grab it while it is open, then press its button
            auto answer = [](const QString &shotName, const QString &buttonText) {
                QTimer::singleShot(400, qApp, [shotName, buttonText] {
                    QWidget *dlg = QApplication::activeModalWidget();
                    if (!dlg)
                        return;
                    if (!shotName.isEmpty())
                        dlg->grab().save(outDir + QLatin1Char('/') + shotName + QLatin1String(".png"));
                    for (QPushButton *b : dlg->findChildren<QPushButton *>()) {
                        QString t = b->text();
                        t.remove(QLatin1Char('&'));
                        if (t == buttonText) {
                            b->click();
                            return;
                        }
                    }
                    dlg->close();
                });
            };
            answer(QStringLiteral("19-install-window"), QStringLiteral("Install"));
            QTimer::singleShot(1200, qApp, [] {
                if (QWidget *box = QApplication::activeModalWidget())
                    box->close(); // the "installed" message
            });
            check(runInstallPackage(&w, dist + QLatin1String("/themes-extra.qbpkg")), QStringLiteral("install theme add-on through the install window"));
            QStringList log;
            QString aerr;
            bool restart = false;
            check(addons::install(dist + QLatin1String("/template-memoir.qbpkg"), &log, &aerr, &restart)
                      && addons::install(dist + QLatin1String("/format-paperback-a5.qbpkg"), &log, &aerr, &restart),
                  QStringLiteral("install template and compile format add-ons"), aerr);

            QString themeId;
            for (const theme::Entry &te : theme::available())
                if (te.name == QLatin1String("Northern Night"))
                    themeId = te.id;
            check(!themeId.isEmpty(), QStringLiteral("add-on theme is offered"));
            AppSettings::instance().theme = themeId;
            theme::apply();
            binder->select({scene1}, true);
            shot(&w, QStringLiteral("20-addon-theme-northern-night"));
            check(theme::isDark(), QStringLiteral("add-on theme is applied"));
            for (const theme::Entry &te : theme::available())
                if (te.name == QLatin1String("Warm Paper"))
                    AppSettings::instance().theme = te.id;
            theme::apply();
            shot(&w, QStringLiteral("21-addon-theme-warm-paper"));

            {
                AddonManagerDialog d(&w);
                d.show();
                shot(&d, QStringLiteral("22-addon-manager"));
                d.reject();
            }
            // switching the add-on off falls back to the desktop colors
            addons::setEnabled(QStringLiteral("themes-extra"), false);
            theme::apply();
            bool offered = false;
            for (const theme::Entry &te : theme::available())
                offered = offered || te.name == QLatin1String("Northern Night");
            check(!offered, QStringLiteral("switched-off add-on no longer offers its themes"));
            AppSettings::instance().theme = QStringLiteral("system");
            theme::apply();

            templates::Info memoir;
            for (const auto &t : templates::all())
                if (t.name == QLatin1String("Memoir"))
                    memoir = t;
            check(!memoir.zipPath.isEmpty(), QStringLiteral("add-on project template is offered"));
            QTemporaryDir mdir;
            QString merr;
            auto mp = templates::createProject(memoir, mdir.path(), QStringLiteral("My Life"), &merr);
            check(mp && mp->labelTitle == QLatin1String("Period") && mp->draft()->children.size() == 5,
                  QStringLiteral("project from the add-on template"), merr);
            bool a5 = false;
            for (const compile::Format &cf : compile::allFormats())
                a5 = a5 || cf.name == QLatin1String("Paperback (A5)");
            check(a5, QStringLiteral("add-on compile format is offered"));
            {
                StartDialog sd(nullptr, false);
                sd.resize(900, 620);
                sd.show();
                if (auto *cats = sd.findChild<QListWidget *>())
                    for (int i = 0; i < cats->count(); ++i)
                        if (cats->item(i)->text() == QLatin1String("Nonfiction"))
                            cats->setCurrentRow(i);
                shot(&sd, QStringLiteral("23-start-window-with-addon-template"));
                sd.close();
            }
        }
    }

    // compile through the built-in exporter
    {
        const QString out = outDir + QLatin1String("/The Lighthouse.docx");
        QFile::remove(out);
        compile::Format fmt;
        for (const compile::Format &cf : compile::builtinFormats())
            if (cf.name == QLatin1String("Manuscript (Times)"))
                fmt = cf;
        compile::AuthorInfo author;
        author.forename = QStringLiteral("Ada");
        author.surname = QStringLiteral("Writer");
        compile::Compiler compiler(proj, fmt, proj->compile, author);
        const compile::Manuscript ms = compiler.build();
        ExporterInterface *docx = compile::PluginManager::instance().exporter(QStringLiteral("docx"));
        QString cerr;
        check(docx && docx->exportManuscript(ms, out, &cerr) && QFile::exists(out), QStringLiteral("compile to DOCX"), cerr);
        for (const QString &id : {QStringLiteral("libreoffice-pdf"), QStringLiteral("libreoffice-odt"), QStringLiteral("pandoc-epub")}) {
            ExporterInterface *ex = compile::PluginManager::instance().exporter(id);
            if (!ex) {
                std::printf("      (%s not available)\n", qPrintable(id));
                continue;
            }
            const QString o = outDir + QLatin1String("/The Lighthouse.") + ex->fileExtension();
            QFile::remove(o);
            QString e2;
            check(ex->exportManuscript(ms, o, &e2) && QFile::exists(o), QStringLiteral("compile via ") + id, e2);
        }
        if (ExporterInterface *mdx = compile::PluginManager::instance().exporter(QStringLiteral("markdown"))) {
            const QString mdOut = outDir + QLatin1String("/The Lighthouse.md");
            check(mdx->exportManuscript(ms, mdOut, &cerr), QStringLiteral("compile via Markdown plugin"), cerr);
        } else {
            check(false, QStringLiteral("Markdown plugin loaded"),
                  compile::PluginManager::instance().messages().join(QLatin1String("; ")));
        }
    }

    // save, close, reopen: the typed text must be there
    check(w.closeProject(), QStringLiteral("close project"));
    check(w.openProject(projectFile), QStringLiteral("reopen project"));
    settle(300);
    proj = w.project();
    BinderItem *again = proj ? findTitle(proj->draft(), QStringLiteral("Counting Ships")) : nullptr;
    check(again != nullptr, QStringLiteral("binder structure persisted"));
    if (again) {
        QFile tf(proj->docs()->textPath(again->id));
        tf.open(QIODevice::ReadOnly);
        const QString md = QString::fromUtf8(tf.readAll());
        check(md.contains(QLatin1String("**twice**")) && md.contains(QLatin1String("{==counted==}{>>")),
              QStringLiteral("typed text, bold and comment persisted"), md);
        check(again->synopsis.startsWith(QLatin1String("Tomas")), QStringLiteral("synopsis persisted"));
    }
    shot(&w, QStringLiteral("18-reopened"));

    // ---------------------------------------------------------------- fonts, paragraphs, ruler (0.3.0)
    // QB_TEST_ONLY_NEW=1 skips the checks of earlier versions (quick runs while working on new ones)
    const bool onlyNew = qEnvironmentVariableIsSet("QB_TEST_ONLY_NEW");
    if (again && !onlyNew) {
        AppSettings::instance().rulerUnit = QStringLiteral("cm");
        binder = w.findChild<BinderPanel *>();
        binder->select({again}, true);
        settle(300);
        EditorPane *fp = nullptr;
        for (EditorPane *ep : w.findChildren<EditorPane *>())
            if (ep->isVisible() && !fp)
                fp = ep;
        TextEditor *fe = fp ? fp->currentEditor() : nullptr;
        check(fe != nullptr, QStringLiteral("editor for the formatting checks"));
        if (fe) {
            fe->setFocus();
            QTextCursor c = fe->textCursor();
            // font on a selection: the words "lighthouse keeper"
            const int at = fe->toPlainText().indexOf(QLatin1String("lighthouse keeper"));
            c.setPosition(at);
            c.setPosition(at + 17, QTextCursor::KeepAnchor);
            fe->setTextCursor(c);
            QTimer::singleShot(500, qApp, [&] {
                auto *dlg = qobject_cast<FontDialog *>(QApplication::activeModalWidget());
                check(dlg != nullptr, QStringLiteral("Format > Font > Show Fonts opens the font dialog"));
                if (!dlg)
                    return;
                if (auto *fams = dlg->findChild<QListWidget *>(QStringLiteral("fontFamilies"))) {
                    const auto hits = fams->findItems(QStringLiteral("DejaVu Sans"), Qt::MatchExactly);
                    if (!hits.isEmpty())
                        fams->setCurrentItem(hits.first());
                }
                if (auto *sz = dlg->findChild<QDoubleSpinBox *>(QStringLiteral("fontSize")))
                    sz->setValue(18);
                if (auto *spc = dlg->findChild<QSpinBox *>(QStringLiteral("fontSpacing")))
                    spc->setValue(110);
                if (auto *caps = dlg->findChild<QComboBox *>(QStringLiteral("fontCaps")))
                    caps->setCurrentIndex(1);
                shot(dlg, QStringLiteral("19-font-dialog"));
                dlg->accept();
            });
            byKey(&w, QStringLiteral("showFonts"))->trigger();
            settle(300);
            c.setPosition(at + 3);
            direct::CharSpec sp = direct::charSpec(c.charFormat());
            check(sp.family == QLatin1String("DejaVu Sans") && qAbs(sp.size - 18) < 0.01 && sp.spacing == 110 && sp.caps == 1,
                  QStringLiteral("font dialog applies family, size, letter spacing and small caps"),
                  QStringLiteral("%1 %2 %3 %4").arg(sp.family).arg(sp.size).arg(sp.spacing).arg(sp.caps));
            c.setPosition(at + 30);
            check(direct::charSpec(c.charFormat()).isEmpty(), QStringLiteral("text outside the selection keeps the project font"));
            {
                // a font change over bold and plain text keeps the bold part bold
                const int tw = fe->toPlainText().indexOf(QLatin1String("twice"));
                QTextCursor bc(fe->document());
                bc.setPosition(tw + 2);
                const bool wasBold = bc.charFormat().fontWeight() >= QFont::DemiBold;
                bc.setPosition(tw - 6);
                bc.setPosition(tw + 12, QTextCursor::KeepAnchor);
                fe->setTextCursor(bc);
                fe->setFontFamily(QStringLiteral("DejaVu Sans Mono"));
                QTextCursor t(fe->document());
                t.setPosition(tw + 2);
                const bool bold = t.charFormat().fontWeight() >= QFont::DemiBold;
                t.setPosition(tw - 3);
                check(wasBold && bold && t.charFormat().fontWeight() < QFont::DemiBold
                          && direct::charSpec(t.charFormat()).family == QLatin1String("DejaVu Sans Mono"),
                      QStringLiteral("a font change keeps bold and plain parts of the selection as they were"));
                fe->setFontFamily(QString());
                t.setPosition(tw + 2);
                check(direct::charSpec(t.charFormat()).isEmpty(), QStringLiteral("the project font removes the font again"));
            }
            // bigger / smaller and colour
            c.setPosition(at);
            c.setPosition(at + 17, QTextCursor::KeepAnchor);
            fe->setTextCursor(c);
            byKey(&w, QStringLiteral("fontBigger"))->trigger();
            byKey(&w, QStringLiteral("fontBigger"))->trigger();
            byKey(&w, QStringLiteral("fontSmaller"))->trigger();
            fe->setTextColor(QColor(0x1a, 0x5f, 0xb4));
            c.setPosition(at + 3);
            sp = direct::charSpec(c.charFormat());
            check(qAbs(sp.size - 19) < 0.01 && sp.color == QColor(0x1a, 0x5f, 0xb4),
                  QStringLiteral("Bigger, Smaller and text colour work on the selection"),
                  QStringLiteral("%1 %2").arg(sp.size).arg(sp.color.name()));
            // choosing the project's own size removes the size
            c.setPosition(at);
            c.setPosition(at + 17, QTextCursor::KeepAnchor);
            fe->setTextCursor(c);
            fe->setFontSize(proj->formatting.fontSize);
            c.setPosition(at + 3);
            check(direct::charSpec(c.charFormat()).size == 0, QStringLiteral("the project's size is stored as no size"));
            c.setPosition(at);
            c.setPosition(at + 17, QTextCursor::KeepAnchor);
            fe->setTextCursor(c);
            fe->setFontSize(18);

            // paragraph dialog on the second paragraph
            c.setPosition(fe->document()->findBlockByNumber(1).position());
            fe->setTextCursor(c);
            QTimer::singleShot(500, qApp, [&] {
                auto *dlg = qobject_cast<ParagraphDialog *>(QApplication::activeModalWidget());
                check(dlg != nullptr, QStringLiteral("Format > Paragraph opens the paragraph dialog"));
                if (!dlg)
                    return;
                dlg->findChild<QComboBox *>(QStringLiteral("paraAlign"))->setCurrentIndex(3);
                dlg->findChild<QDoubleSpinBox *>(QStringLiteral("paraLeft"))->setValue(units::fromPx(direct::mmToPx(15)));
                dlg->findChild<QDoubleSpinBox *>(QStringLiteral("paraBefore"))->setValue(6);
                dlg->findChild<QDoubleSpinBox *>(QStringLiteral("paraLine"))->setValue(1.5);
                dlg->findChild<QDoubleSpinBox *>(QStringLiteral("paraTabPos"))->setValue(units::fromPx(direct::mmToPx(60)));
                dlg->findChild<QComboBox *>(QStringLiteral("paraTabType"))->setCurrentIndex(2);
                dlg->findChild<QPushButton *>(QStringLiteral("paraTabAdd"))->click();
                shot(dlg, QStringLiteral("20-paragraph-dialog"));
                dlg->accept();
            });
            byKey(&w, QStringLiteral("paragraph"))->trigger();
            settle(300);
            QTextBlockFormat bf = fe->document()->findBlockByNumber(1).blockFormat();
            check((bf.alignment() & Qt::AlignJustify) && qAbs(bf.leftMargin() - direct::mmToPx(15)) < 0.3
                      && qAbs(bf.topMargin() - 8) < 0.1 && qAbs(bf.lineHeight() - 150) < 0.1 && bf.tabPositions().size() == 1
                      && bf.tabPositions().first().type == QTextOption::RightTab,
                  QStringLiteral("paragraph dialog applies alignment, indent, spacing and a tab stop"));
            check(bf.intProperty(props::ParaDirect)
                      == (direct::Align | direct::Left | direct::Before | direct::Line | direct::Tabs),
                  QStringLiteral("only the changed attributes are marked as set by hand"),
                  QString::number(bf.intProperty(props::ParaDirect)));
            check(fe->document()->begin().blockFormat().intProperty(props::ParaDirect) == 0,
                  QStringLiteral("other paragraphs are untouched"));

            // ruler
            Ruler *ruler = fp->ruler();
            check(ruler->isVisible() && ruler->editor() == fe, QStringLiteral("ruler is shown above the text"));
            const int y = ruler->height() - 4;
            int lx = ruler->markerX(Ruler::LeftIndent);
            check(qAbs(lx - ruler->originX() - direct::mmToPx(15)) <= 1, QStringLiteral("ruler shows the left indent"));
            QTest::mousePress(ruler, Qt::LeftButton, Qt::NoModifier, QPoint(lx, y));
            QMouseEvent mv1(QEvent::MouseMove, QPointF(lx + 20, y), ruler->mapToGlobal(QPointF(lx + 20, y)), Qt::NoButton,
                            Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(ruler, &mv1);
            const int target = ruler->originX() + qRound(units::toPx(units::metric() ? 2.5 : 1.0));
            QMouseEvent mv2(QEvent::MouseMove, QPointF(target, y), ruler->mapToGlobal(QPointF(target, y)), Qt::NoButton,
                            Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(ruler, &mv2);
            QTest::mouseRelease(ruler, Qt::LeftButton, Qt::NoModifier, QPoint(target, y));
            settle(100);
            bf = fe->document()->findBlockByNumber(1).blockFormat();
            check(qAbs(bf.leftMargin() - units::toPx(units::metric() ? 2.5 : 1.0)) < 0.5,
                  QStringLiteral("dragging the left marker changes the indent"), QString::number(bf.leftMargin()));
            // click adds a tab stop, right marker drag
            const int clickX = ruler->originX() + qRound(bf.leftMargin() + bf.textIndent() + units::toPx(units::metric() ? 3.0 : 1.25));
            QTest::mouseClick(ruler, Qt::LeftButton, Qt::NoModifier, QPoint(clickX, y));
            settle(100);
            bf = fe->document()->findBlockByNumber(1).blockFormat();
            check(bf.tabPositions().size() == 2 && qAbs(bf.tabPositions().first().position - units::toPx(units::metric() ? 3.0 : 1.25)) < 0.5,
                  QStringLiteral("a click on the ruler adds a tab stop"),
                  QString::number(bf.tabPositions().isEmpty() ? -1 : bf.tabPositions().first().position));
            // drag the new tab stop off the ruler: removed
            const int tx = ruler->markerX(Ruler::Tab, 0);
            QTest::mousePress(ruler, Qt::LeftButton, Qt::NoModifier, QPoint(tx, y));
            QMouseEvent mv3(QEvent::MouseMove, QPointF(tx + 5, y + 60), ruler->mapToGlobal(QPointF(tx + 5, y + 60)), Qt::NoButton,
                            Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(ruler, &mv3);
            QTest::mouseRelease(ruler, Qt::LeftButton, Qt::NoModifier, QPoint(tx + 5, y + 60));
            settle(100);
            bf = fe->document()->findBlockByNumber(1).blockFormat();
            check(bf.tabPositions().size() == 1, QStringLiteral("a tab stop dragged off the ruler is removed"));
            shot(&w, QStringLiteral("21-fonts-paragraph-ruler"));

            // undo takes back one ruler action
            fe->undo();
            check(fe->document()->findBlockByNumber(1).blockFormat().tabPositions().size() == 2,
                  QStringLiteral("undo restores the tab stop"));
            fe->redo();

            // copy / paste formatting
            c.setPosition(fe->document()->findBlockByNumber(1).position() + 1);
            fe->setTextCursor(c);
            fe->copyFormatting();
            c.setPosition(fe->document()->begin().position() + 1);
            fe->setTextCursor(c);
            // (first paragraph has the font span: copy from it to the second instead)
            c.setPosition(at + 3);
            fe->setTextCursor(c);
            fe->copyFormatting();
            c.setPosition(fe->document()->findBlockByNumber(1).position() + 1);
            fe->setTextCursor(c);
            fe->pasteFormatting();
            c.setPosition(fe->document()->findBlockByNumber(1).position() + 2);
            check(direct::charSpec(c.charFormat()).family == QLatin1String("DejaVu Sans")
                      && fe->document()->findBlockByNumber(1).blockFormat().intProperty(props::ParaDirect) == 0,
                  QStringLiteral("Paste Formatting copies font and paragraph formatting"));
            fe->undo();
            check(fe->document()->findBlockByNumber(1).blockFormat().tabPositions().size() == 1
                      && direct::charSpec(c.charFormat()).isEmpty(),
                  QStringLiteral("Paste Formatting is one undo step"));

            // zoom: stored sizes stay, the display scales
            c.setPosition(at + 3);
            const int h100 = fe->cursorRect(c).height();
            proj->docs()->setZoom(2.0);
            settle(400);
            const int h200 = fe->cursorRect(c).height();
            check(qAbs(direct::charSpec(c.charFormat()).size - 18) < 0.01 && h200 > h100 * 1.7,
                  QStringLiteral("zoom enlarges text with its own font size without changing the stored size"),
                  QStringLiteral("%1 -> %2").arg(h100).arg(h200));
            proj->docs()->setZoom(1.0);
            settle(300);

            // saved as markers, read back
            proj->save();
            QFile tf(proj->docs()->textPath(again->id));
            tf.open(QIODevice::ReadOnly);
            const QString md = QString::fromUtf8(tf.readAll());
            tf.close();
            check(md.contains(QLatin1String("<span style=\"font-family:'DejaVu Sans'; font-size:18pt; color:#1a5fb4; letter-spacing:0.05em; font-variant:small-caps\">lighthouse keeper</span>"))
                      && md.contains(QLatin1String("{: align=justify left=25mm before=6pt line=1.5 tabs=60mmR} ")),
                  QStringLiteral("fonts and paragraph formatting are saved as markers"), md);

            // project formatting: Use Current
            c.setPosition(at + 3);
            fe->setTextCursor(c);
            {
                ProjectSettingsDialog ps(proj, ProjectSettingsDialog::Formatting, &w);
                ps.show();
                auto *use = ps.findChild<QPushButton *>(QStringLiteral("useCurrentFormatting"));
                check(use && use->isEnabled(), QStringLiteral("Use Current is available in the project settings"));
                if (use)
                    use->click();
                shot(&ps, QStringLiteral("22-project-formatting-use-current"));
                ps.accept();
            }
            settle(300);
            check(proj->formatting.fontFamily == QLatin1String("DejaVu Sans") && qAbs(proj->formatting.fontSize - 18) < 0.01,
                  QStringLiteral("Use Current makes the font at the cursor the project font"));
            bf = fe->document()->findBlockByNumber(1).blockFormat();
            check(qAbs(bf.lineHeight() - 150) < 0.1 && qAbs(bf.leftMargin() - direct::mmToPx(25)) < 0.5,
                  QStringLiteral("formatting set by hand survives a change of the project formatting"));

            // Documents > Convert > Text to Default Formatting
            binder->select({again}, true);
            binder->setFocus();
            QTimer::singleShot(500, qApp, [&] {
                auto *dlg = qobject_cast<DefaultFormattingDialog *>(QApplication::activeModalWidget());
                check(dlg != nullptr, QStringLiteral("Text to Default Formatting asks what to remove"));
                if (!dlg)
                    return;
                shot(dlg, QStringLiteral("23-default-formatting"));
                dlg->accept();
            });
            byKey(&w, QStringLiteral("defaultFormatting"))->trigger();
            settle(300);
            proj->save();
            QFile tf2(proj->docs()->textPath(again->id));
            tf2.open(QIODevice::ReadOnly);
            const QString md2 = QString::fromUtf8(tf2.readAll());
            check(!md2.contains(QLatin1String("<span")) && md2.contains(QLatin1String("{: align=justify tabs=60mmR} "))
                      && md2.contains(QLatin1String("**twice**")),
                  QStringLiteral("default formatting removes fonts and indents, keeps alignment, tabs and bold"), md2);
            check(!snapshots::list(proj, again->id).isEmpty(), QStringLiteral("a snapshot was taken before converting"));

            // options page
            {
                OptionsDialog od(&w);
                od.show();
                od.showSection(OptionsDialog::SEditing);
                shot(&od, QStringLiteral("24-options-editing"));
                od.reject();
            }
            // ruler can be hidden
            byKey(&w, QStringLiteral("showRuler"))->trigger();
            settle(100);
            check(!ruler->isVisible() && !AppSettings::instance().showRuler, QStringLiteral("View > Show Ruler hides the ruler"));
            byKey(&w, QStringLiteral("showRuler"))->trigger();
            settle(100);
            check(ruler->isVisible(), QStringLiteral("and shows it again"));

            // ------------------------------------------------------------ styles (0.4.0)
            using direct::mmToPx;
            templates::setMarkdown(proj, again,
                                   QStringLiteral("Chapter heading\nA verse line to style.\nBody text with a special term in it.\n"
                                                  "Another verse line.\nLast paragraph.\n"));
            settle(300);
            fe = fp->currentEditor();
            fe->setFocus();
            auto blockAt = [&](int n) { return fe->document()->findBlockByNumber(n); };
            auto cursorIn = [&](int n, int offset = 2) {
                QTextCursor t(fe->document());
                t.setPosition(blockAt(n).position() + offset);
                fe->setTextCursor(t);
            };
            auto selectIn = [&](int n, const QString &words) {
                const int pos = blockAt(n).position() + blockAt(n).text().indexOf(words);
                QTextCursor t(fe->document());
                t.setPosition(pos);
                t.setPosition(pos + words.size(), QTextCursor::KeepAnchor);
                fe->setTextCursor(t);
            };
            auto storedText = [&] {
                proj->save();
                QFile tf(proj->docs()->textPath(again->id));
                tf.open(QIODevice::ReadOnly);
                return QString::fromUtf8(tf.readAll());
            };
            auto *combo = w.findChild<QComboBox *>(QStringLiteral("styleCombo"));
            check(combo && combo->findData(QStringLiteral("Heading 1")) > 0 && combo->findData(QStringLiteral("Emphasis")) > 0
                      && combo->itemText(combo->findData(QStringLiteral("Emphasis"))).startsWith(QLatin1String("a ")),
                  QStringLiteral("the format bar lists paragraph and character styles"));

            // format a paragraph by hand, then make a style from it
            selectIn(1, QStringLiteral("A verse line to style."));
            fe->toggleItalic();
            fe->setFontFamily(QStringLiteral("DejaVu Sans Mono"));
            direct::ParaSpec hand;
            hand.bits = direct::Left | direct::First;
            hand.left = mmToPx(15);
            hand.first = 0;
            fe->applyParaSpec(hand);
            QTimer::singleShot(500, qApp, [&] {
                auto *dlg = qobject_cast<StyleFromSelectionDialog *>(QApplication::activeModalWidget());
                check(dlg != nullptr, QStringLiteral("Format > Style > New Style From Selection opens its dialog"));
                if (!dlg)
                    return;
                dlg->findChild<QLineEdit *>(QStringLiteral("styleName"))->setText(QStringLiteral("Verse"));
                shot(dlg, QStringLiteral("25-new-style-from-selection"));
                dlg->accept();
            });
            byKey(&w, QStringLiteral("styleNew"))->trigger();
            settle(300);
            const Style *verse = styles::find(proj->formatting, QStringLiteral("Verse"));
            check(verse && verse->italic && verse->chars.family == QLatin1String("DejaVu Sans Mono") && qAbs(verse->para.left - mmToPx(15)) < 0.2
                      && verse->para.bits == (direct::Left | direct::First),
                  QStringLiteral("the new paragraph style holds font, italics and indents"),
                  verse ? styleui::describe(*verse) : QStringLiteral("no style"));
            check(blockAt(1).blockFormat().stringProperty(props::Style) == QLatin1String("Verse")
                      && blockAt(1).blockFormat().intProperty(props::ParaDirect) == 0,
                  QStringLiteral("the paragraph it was made from uses the style"));
            QString stored = storedText();
            check(stored.contains(QLatin1String("\n::Verse:: A verse line to style.\n")), QStringLiteral("and is stored with the style name only"), stored);
            check(combo->findData(QStringLiteral("Verse")) > 0, QStringLiteral("the new style is in the format bar list"));

            // apply it from the format bar
            cursorIn(3);
            emit combo->activated(combo->findData(QStringLiteral("Verse")));
            settle(100);
            QTextCursor probe(fe->document());
            probe.setPosition(blockAt(3).position() + 3);
            check(qAbs(blockAt(3).blockFormat().leftMargin() - mmToPx(15)) < 0.2 && probe.charFormat().fontItalic()
                      && direct::charSpec(probe.charFormat()).family == QLatin1String("DejaVu Sans Mono")
                      && combo->currentData().toString() == QLatin1String("Verse"),
                  QStringLiteral("the format bar applies a paragraph style"));

            // character style from a selection
            selectIn(2, QStringLiteral("special term"));
            fe->toggleBold();
            fe->setTextColor(QColor(0x1a, 0x5f, 0xb4));
            QTimer::singleShot(500, qApp, [&] {
                auto *dlg = qobject_cast<StyleFromSelectionDialog *>(QApplication::activeModalWidget());
                if (!dlg)
                    return;
                dlg->findChild<QLineEdit *>(QStringLiteral("styleName"))->setText(QStringLiteral("Term"));
                dlg->findChild<QRadioButton *>(QStringLiteral("styleKindCharacter"))->setChecked(true);
                shot(dlg, QStringLiteral("26-new-character-style"));
                dlg->accept();
            });
            byKey(&w, QStringLiteral("styleNew"))->trigger();
            settle(300);
            const Style *termStyle = styles::findChar(proj->formatting, QStringLiteral("Term"));
            stored = storedText();
            check(termStyle && termStyle->bold && termStyle->chars.color == QColor(0x1a, 0x5f, 0xb4) && termStyle->para.bits == 0
                      && stored.contains(QLatin1String("with a <span class=\"Term\">special term</span> in it.")),
                  QStringLiteral("a character style is made from the selection and applied to it"), stored);
            selectIn(4, QStringLiteral("Last"));
            emit combo->activated(combo->findData(QStringLiteral("Term")));
            settle(100);
            check(storedText().contains(QLatin1String("<span class=\"Term\">Last</span> paragraph.")),
                  QStringLiteral("the format bar applies a character style to the selection"), storedText());
            check(combo->currentData().toString() == QLatin1String("Term"), QStringLiteral("and shows the character style at the cursor"));
            emit combo->activated(0);
            settle(100);
            check(storedText().contains(QLatin1String("\nLast paragraph.")), QStringLiteral("No Style removes the character style"), storedText());

            // redefine from the selection: every paragraph of the style follows
            cursorIn(1);
            hand.bits = direct::Left;
            hand.left = mmToPx(25);
            fe->applyParaSpec(hand);
            QTimer::singleShot(500, qApp, [&] {
                auto *dlg = qobject_cast<StyleFromSelectionDialog *>(QApplication::activeModalWidget());
                check(dlg && dlg->findChild<QComboBox *>(QStringLiteral("styleTarget"))->currentData().toString() == QLatin1String("Verse"),
                      QStringLiteral("Redefine Style From Selection proposes the style at the cursor"));
                if (!dlg)
                    return;
                shot(dlg, QStringLiteral("27-redefine-style"));
                dlg->accept();
            });
            byKey(&w, QStringLiteral("styleRedefine"))->trigger();
            settle(300);
            stored = storedText();
            check(qAbs(blockAt(3).blockFormat().leftMargin() - mmToPx(25)) < 0.2 && qAbs(blockAt(1).blockFormat().leftMargin() - mmToPx(25)) < 0.2
                      && blockAt(1).blockFormat().intProperty(props::ParaDirect) == 0
                      && stored.contains(QLatin1String("\n::Verse:: A verse line to style.\n")) && stored.contains(QLatin1String("\n::Verse:: Another verse line.\n")),
                  QStringLiteral("a redefined style changes all its paragraphs, the stored text stays the same"), stored);

            // styles panel
            byKey(&w, QStringLiteral("stylesPanel"))->trigger();
            settle(300);
            auto *panel = w.findChild<StylesPanel *>();
            auto *plist = panel ? panel->findChild<QListWidget *>(QStringLiteral("stylesList")) : nullptr;
            check(panel && panel->isVisible() && plist && plist->count() > 15, QStringLiteral("Format > Style > Styles Panel opens the panel"));
            if (plist) {
                auto row = [&](const QString &name) -> QListWidgetItem * {
                    for (int i = 0; i < plist->count(); ++i)
                        if (plist->item(i)->data(Qt::UserRole).toString() == name && !plist->item(i)->data(Qt::UserRole + 1).toBool())
                            return plist->item(i);
                    return nullptr;
                };
                cursorIn(4);
                plist->setCurrentItem(row(QStringLiteral("Verse")));
                emit plist->itemDoubleClicked(row(QStringLiteral("Verse")));
                settle(200);
                check(blockAt(4).blockFormat().stringProperty(props::Style) == QLatin1String("Verse"),
                      QStringLiteral("a double-click in the panel applies the style"));
                shot(&w, QStringLiteral("28-styled-text"));
                shot(panel, QStringLiteral("29-styles-panel"));
                emit plist->itemDoubleClicked(row(QString()));
                settle(200);
                check(blockAt(4).blockFormat().stringProperty(props::Style).isEmpty()
                          && qAbs(blockAt(4).blockFormat().leftMargin()) < 0.1,
                      QStringLiteral("“No Style” in the panel gives body text again"));

                // edit: rename and centre via the paragraph dialog
                plist->setCurrentItem(row(QStringLiteral("Verse")));
                QTimer::singleShot(500, qApp, [&] {
                    auto *dlg = qobject_cast<StyleEditDialog *>(QApplication::activeModalWidget());
                    check(dlg != nullptr, QStringLiteral("the panel's edit button opens the style editor"));
                    if (!dlg)
                        return;
                    dlg->findChild<QLineEdit *>(QStringLiteral("styleName"))->setText(QStringLiteral("Stanza"));
                    QTimer::singleShot(400, qApp, [&] {
                        auto *pd = qobject_cast<ParagraphDialog *>(QApplication::activeModalWidget());
                        if (!pd)
                            return;
                        pd->findChild<QComboBox *>(QStringLiteral("paraAlign"))->setCurrentIndex(1);
                        pd->findChild<QDoubleSpinBox *>(QStringLiteral("paraBefore"))->setValue(6);
                        pd->accept();
                    });
                    dlg->findChild<QPushButton *>(QStringLiteral("styleParagraph"))->click();
                    shot(dlg, QStringLiteral("30-edit-style"));
                    dlg->accept();
                });
                panel->findChild<QToolButton *>(QStringLiteral("stylesEdit"))->click();
                settle(300);
                stored = storedText();
                const Style *stanza = styles::find(proj->formatting, QStringLiteral("Stanza"));
                check(stanza && stanza->name == QLatin1String("Stanza") && (stanza->para.bits & direct::Align) && (stanza->para.bits & direct::Before)
                          && styles::sheet(proj->formatting).indexOf(QStringLiteral("Verse")) < 0
                          && (blockAt(1).blockFormat().alignment() & Qt::AlignHCenter)
                          && blockAt(1).blockFormat().stringProperty(props::Style) == QLatin1String("Stanza")
                          && stored.contains(QLatin1String("\n::Stanza:: A verse line to style.\n")) && !stored.contains(QLatin1String("Verse::")),
                      QStringLiteral("the style editor renames and reformats; the text follows"), stored);
                check(row(QStringLiteral("Stanza")) != nullptr && combo->findData(QStringLiteral("Stanza")) > 0 && combo->findData(QStringLiteral("Verse")) < 0,
                      QStringLiteral("panel and format bar show the renamed style"));

                // following paragraph: body text after a heading, the same style after a stanza
                cursorIn(0);
                fe->applyStyle(QStringLiteral("Heading 1"));
                QTextCursor endc(fe->document());
                endc.setPosition(blockAt(0).position() + blockAt(0).length() - 1);
                fe->setTextCursor(endc);
                QTest::keyClick(fe, Qt::Key_Return);
                QTest::keyClicks(fe, QStringLiteral("after heading"));
                endc.setPosition(blockAt(2).position() + blockAt(2).length() - 1);
                fe->setTextCursor(endc);
                QTest::keyClick(fe, Qt::Key_Return);
                QTest::keyClicks(fe, QStringLiteral("second line"));
                settle(100);
                check(blockAt(1).text() == QLatin1String("after heading") && blockAt(1).blockFormat().stringProperty(props::Style).isEmpty()
                          && blockAt(3).text() == QLatin1String("second line")
                          && blockAt(3).blockFormat().stringProperty(props::Style) == QLatin1String("Stanza")
                          && fe->currentCharFormat().fontItalic(),
                      QStringLiteral("Enter after a heading gives body text, after a stanza another stanza"),
                      blockAt(1).text() + QLatin1Char('|') + blockAt(3).text());

                // delete the character style: the look stays
                plist->setCurrentItem(row(QStringLiteral("Term")));
                QTimer::singleShot(500, qApp, [&] {
                    if (auto *mb = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                        mb->button(QMessageBox::Yes)->click();
                });
                panel->findChild<QToolButton *>(QStringLiteral("stylesDelete"))->click();
                settle(300);
                stored = storedText();
                check(!styles::sheet(proj->formatting).find(QStringLiteral("Term"))
                          && stored.contains(QLatin1String("with a <span style=\"color:#1a5fb4\">**special term**</span> in it.")),
                      QStringLiteral("deleting a style keeps the look of its text"), stored);
                check(!panel->findChild<QToolButton *>(QStringLiteral("stylesDelete"))->isEnabled() || plist->currentItem() == nullptr
                          || !styles::isFixedName(panel->selectedStyle()),
                      QStringLiteral("(delete button state)"));
                plist->setCurrentItem(row(QStringLiteral("Heading 1")));
                check(!panel->findChild<QToolButton *>(QStringLiteral("stylesDelete"))->isEnabled()
                          && panel->findChild<QToolButton *>(QStringLiteral("stylesEdit"))->isEnabled(),
                      QStringLiteral("built-in headings can be edited but not deleted"));
                panel->close();
            }

            // the Style menu
            if (auto *menu = w.findChild<QMenu *>(QStringLiteral("styleMenu"))) {
                cursorIn(2);
                emit menu->aboutToShow();
                bool hasStanza = false, stanzaChecked = false, hasNew = false, hasPanel = false;
                for (QAction *ac : menu->actions()) {
                    if (ac->text().endsWith(QLatin1String("Stanza"))) {
                        hasStanza = true;
                        stanzaChecked = ac->isChecked();
                    }
                    hasNew = hasNew || ac == byKey(&w, QStringLiteral("styleNew"));
                    hasPanel = hasPanel || ac == byKey(&w, QStringLiteral("stylesPanel"));
                }
                check(hasStanza && stanzaChecked && hasNew && hasPanel, QStringLiteral("Format > Style lists the styles and marks the one at the cursor"));
                menu->show();
                shot(menu, QStringLiteral("31-style-menu"));
                menu->hide();
            }

            // styles are part of the project file; they can become the styles of new projects
            proj->save();
            QFile idx(proj->indexPath());
            idx.open(QIODevice::ReadOnly);
            const QString index = QString::fromUtf8(idx.readAll());
            check(index.contains(QLatin1String("<Styles>")) && index.contains(QLatin1String("name=\"Stanza\""))
                      && index.contains(QLatin1String("<Alias")) && index.contains(QLatin1String("from=\"Verse\"")),
                  QStringLiteral("the project file holds the styles and the former name"), index.mid(index.indexOf(QLatin1String("<Styles>")), 400));
            QTimer::singleShot(500, qApp, [&] {
                if (auto *mb = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                    mb->accept();
            });
            byKey(&w, QStringLiteral("styleSaveDefault"))->trigger();
            settle(300);
            check(AppSettings::instance().newStylesXml.contains(QLatin1String("Stanza")), QStringLiteral("Use These Styles in New Projects saves them"));
            {
                OptionsDialog od(&w);
                od.show();
                od.showSection(OptionsDialog::SEditing, QStringLiteral("Formatting"));
                auto *builtin = od.findChild<QPushButton *>(QStringLiteral("newStylesBuiltin"));
                check(builtin && builtin->isEnabled(), QStringLiteral("the options show that new projects get saved styles"));
                if (builtin)
                    builtin->click();
                od.accept();
                check(AppSettings::instance().newStylesXml.isEmpty(), QStringLiteral("and go back to the built-in styles"));
            }
        }
    }

    // ---------------------------------------------------------------- themes and appearance (0.5.0)
    if (proj && !onlyNew) {
        using theme::Role;
        AppSettings &as = AppSettings::instance();
        as.ownColors.clear();
        as.ownFonts.clear();
        addons::setEnabled(QStringLiteral("themes-extra"), true);
        binder = w.findChild<BinderPanel *>();
        BinderItem *item = findTitle(proj->draft(), QStringLiteral("Counting Ships"));
        BinderItem *folder = item ? item->parent : nullptr;
        auto lightDiff = [](const QColor &a, const QColor &b) { return qAbs(a.lightness() - b.lightness()); };
        auto near = [](const QColor &a, const QColor &b, int tol = 6) {
            return qAbs(a.red() - b.red()) <= tol && qAbs(a.green() - b.green()) <= tol && qAbs(a.blue() - b.blue()) <= tol;
        };
        auto firstPane = [&]() -> EditorPane * {
            for (EditorPane *ep : w.findChildren<EditorPane *>())
                if (ep->isVisible())
                    return ep;
            return nullptr;
        };

        // ---- the theme list
        int builtIn = 0, fromAddon = 0;
        bool darkMode = false;
        for (const theme::Entry &te : theme::available()) {
            builtIn += te.kind == theme::Kind::BuiltIn;
            fromAddon += te.kind == theme::Kind::Addon;
            darkMode = darkMode || (te.id == QLatin1String("dark") && te.name == QLatin1String("Dark Mode"));
        }
        check(builtIn == 8 && darkMode, QStringLiteral("eight built-in themes including Dark Mode"), QString::number(builtIn));
        check(fromAddon == 5 || !QFile::exists(QStringLiteral(QB_TEST_PLUGIN_DIR "/../addons-dist/themes-extra.qbpkg")),
              QStringLiteral("the add-on \"Extra themes\" 2.0 offers five themes"), QString::number(fromAddon));

        // ---- every theme gives every area a color, and text can be read on its background
        {
            QStringList bad;
            for (const theme::Entry &te : theme::available()) {
                theme::Theme t;
                if (!theme::load(te.id, &t)) {
                    bad << te.name + QLatin1String(": not loaded");
                    continue;
                }
                const QMap<Role, QColor> c = theme::resolvedColors(t, {});
                if (c.size() != int(Role::Count))
                    bad << te.name + QLatin1String(": roles missing");
                for (const QColor &col : c)
                    if (!col.isValid())
                        bad << te.name + QLatin1String(": invalid color");
                const QPair<Role, Role> pairs[] = {{Role::Window, Role::WindowText},
                                                   {Role::Base, Role::Text},
                                                   {Role::Button, Role::ButtonText},
                                                   {Role::Highlight, Role::HighlightedText},
                                                   {Role::BinderBackground, Role::BinderText},
                                                   {Role::EditorPaper, Role::EditorText},
                                                   {Role::EditorPaper, Role::EditorLink},
                                                   {Role::HeaderBar, Role::HeaderBarText},
                                                   {Role::CardPaper, Role::CardTitle},
                                                   {Role::CardPaper, Role::CardText},
                                                   {Role::OutlinerBackground, Role::OutlinerText},
                                                   {Role::InspectorBackground, Role::InspectorText},
                                                   {Role::SynopsisPaper, Role::SynopsisText},
                                                   {Role::NotesPaper, Role::NotesText},
                                                   {Role::CompositionPaper, Role::CompositionText}};
                for (const auto &pr : pairs)
                    if (lightDiff(c[pr.first], c[pr.second]) < 70)
                        bad << QStringLiteral("%1: %2 on %3 (%4 / %5)")
                                   .arg(te.name, QLatin1String(theme::key(pr.second)), QLatin1String(theme::key(pr.first)),
                                        c[pr.second].name(), c[pr.first].name());
            }
            check(bad.isEmpty(), QStringLiteral("all themes: every area has colors and readable text"), bad.join(QStringLiteral("; ")));
        }

        // ---- theme files
        {
            theme::Theme t;
            // a file as written for versions before 0.5: palette colors only
            const QByteArray oldFile = R"json({"name":"Old","colors":{"window":"#202830","text":"#e0e8f0","highlight":"#cc6600"}})json";
            const bool ok = theme::Theme::fromJson(QJsonDocument::fromJson(oldFile).object(), &t);
            const QMap<Role, QColor> c = theme::resolvedColors(t, {});
            check(ok && c[Role::CardPaper].lightness() < 128 && c[Role::EditorText] == QColor(0xe0, 0xe8, 0xf0)
                      && c[Role::BinderBackground] == c[Role::Base] && c[Role::EditorLink].lightness() > 128,
                  QStringLiteral("a theme of three colors is a complete theme (dark cards, light links)"));
            theme::Theme u;
            check(!theme::Theme::fromJson(QJsonDocument::fromJson("{\"name\":\"x\"}").object(), &u)
                      && !theme::Theme::fromJson(QJsonDocument::fromJson("{\"name\":\"x\",\"colors\":{\"window\":\"#111111\"}}").object(), &u),
                  QStringLiteral("files without colors or without a text color are refused"));
            t.fonts.insert(theme::FontRole::Binder, theme::FontSpec{QStringLiteral("DejaVu Sans"), 11.5, true, false});
            t.colors.insert(Role::CardPaper, QColor(0x12, 0x34, 0x56));
            theme::Theme back;
            const bool again2 = theme::Theme::fromJson(QJsonDocument::fromJson(QJsonDocument(t.toJson()).toJson()).object(), &back);
            check(again2 && back.colors == t.colors && back.fonts == t.fonts && back.name == t.name,
                  QStringLiteral("theme file round trip (colors and fonts)"));
            const theme::FontSpec fs{QStringLiteral("Noto Serif"), 12.5, true, true};
            QFont base(QStringLiteral("Noto Sans"), 10);
            QFont chosen(QStringLiteral("Noto Sans"), 13);
            chosen.setItalic(true);
            const theme::FontSpec diff = theme::FontSpec::fromFont(chosen, base);
            check(theme::FontSpec::fromString(fs.toString()) == fs && !theme::FontSpec::fromString(QString()).isSet()
                      && diff.family.isEmpty() && qFuzzyCompare(diff.size, 13.0) && diff.italic && !diff.bold
                      && diff.applied(base).pointSizeF() == 13.0,
                  QStringLiteral("font changes store only what differs from the area's font"));
        }

        // ---- applying a theme reaches every area, without a restart
        check(themeui::choose(&w, QStringLiteral("system"), false) && QApplication::palette() == desktopPalette,
              QStringLiteral("Default leaves the desktop's colors untouched"));
        {
            // the desktop changes its color scheme while the program runs
            QPalette other = desktopPalette;
            other.setColor(QPalette::Base, QColor(0xdd, 0xee, 0xff));
            other.setColor(QPalette::Window, QColor(0xcc, 0xdd, 0xee));
            QApplication::setPalette(other);
            settle(300);
            EditorPane *dp = firstPane();
            check(binder->tree()->palette().color(QPalette::Base) == QColor(0xdd, 0xee, 0xff)
                      && theme::color(Role::EditorPaper) == QColor(0xdd, 0xee, 0xff)
                      && theme::color(Role::HeaderBar) == QColor(0xcc, 0xdd, 0xee) && dp,
                  QStringLiteral("Default follows a color scheme change of the desktop"),
                  binder->tree()->palette().color(QPalette::Base).name());
            QApplication::setPalette(desktopPalette);
            settle(300);
            check(theme::color(Role::EditorPaper) == desktopPalette.color(QPalette::Base), QStringLiteral("and back"));
        }
        QAction *compileAct = byKey(&w, QStringLiteral("compile"));
        auto iconLightness = [&] {
            const QImage img = compileAct->icon().pixmap(32, 32).toImage().convertToFormat(QImage::Format_ARGB32);
            qint64 sum = 0, n = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x) {
                    const QColor px = img.pixelColor(x, y);
                    if (px.alpha() > 128) {
                        sum += px.lightness();
                        ++n;
                    }
                }
            return n ? double(sum) / n : -1.0;
        };
        const double iconOnLight = iconLightness();
        themeui::choose(&w, QStringLiteral("midnight"), false);
        binder->select({item}, true);
        settle(300);
        EditorPane *fp = firstPane();
        TextEditor *fe = fp ? fp->currentEditor() : nullptr;
        QWidget *header = fp ? fp->findChild<QWidget *>(QStringLiteral("editorHeader")) : nullptr;
        auto *insp = w.findChild<Inspector *>();
        check(theme::isDark() && QApplication::palette().color(QPalette::Window) == QColor(0x1b, 0x22, 0x33)
                  && theme::currentName() == QLatin1String("Midnight"),
              QStringLiteral("Midnight: window colors"));
        check(fe && fe->palette().color(QPalette::Base) == QColor(0x10, 0x15, 0x1f)
                  && fe->palette().color(QPalette::Text) == QColor(0xd5, 0xdb, 0xe6),
              QStringLiteral("Midnight: editor paper and text"), fe ? fe->palette().color(QPalette::Base).name() : QString());
        check(binder->tree()->palette().color(QPalette::Base) == QColor(0x17, 0x1d, 0x2b),
              QStringLiteral("Midnight: binder background"));
        check(header && header->palette().color(QPalette::Window) == theme::color(Role::HeaderBar),
              QStringLiteral("header bar color"));
        if (fp) {
            fp->setLocked(true);
            check(header && header->palette().color(QPalette::Window) == theme::color(Role::HeaderBarLocked)
                      && lightDiff(header->palette().color(QPalette::Window), header->palette().color(QPalette::WindowText)) > 70,
                  QStringLiteral("locked header bar: its own color, readable title"));
            fp->setLocked(false);
        }
        check(insp && insp->palette().color(QPalette::Window) == theme::color(Role::InspectorBackground),
              QStringLiteral("inspector background"));
        if (fe) {
            // links are stored with one blue and shown in the theme's link color
            QTextCursor lc(fe->document());
            lc.movePosition(QTextCursor::End);
            const int linkAt = lc.position();
            lc.insertText(QStringLiteral(" a link"), text::linkFormat(QStringLiteral("https://example.org")));
            settle(300);
            bool shown = false;
            for (const QTextLayout::FormatRange &r : fe->document()->findBlock(linkAt + 2).layout()->formats())
                shown = shown || (r.format.hasProperty(QTextFormat::ForegroundBrush)
                                  && r.format.foreground().color() == theme::color(Role::EditorLink));
            QTextCursor probe(fe->document());
            probe.setPosition(linkAt + 3);
            check(shown && probe.charFormat().foreground().color() == QColor(30, 90, 200)
                      && theme::color(Role::EditorLink).lightness() > 128,
                  QStringLiteral("links on dark paper are shown in a light blue; the stored text is unchanged"));
            lc.setPosition(linkAt);
            lc.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
            lc.removeSelectedText();
        }
        const double iconOnDark = iconLightness();
        check(iconOnLight >= 0 && iconOnDark > iconOnLight + 60,
              QStringLiteral("toolbar icons switch to their light variant at once (no restart)"),
              QStringLiteral("%1 -> %2").arg(iconOnLight).arg(iconOnDark));
        shot(&w, QStringLiteral("32-theme-midnight"));

        // corkboard: board and cards
        if (folder && fp) {
            binder->select({folder}, true);
            setView(&w, QStringLiteral("viewCork"));
            settle(400);
            Corkboard *cork = fp->corkboard();
            cork->scene()->clearSelection();
            settle(200);
            CardItem *card = nullptr;
            for (QGraphicsItem *gi : cork->scene()->items())
                if (auto *ci = dynamic_cast<CardItem *>(gi))
                    card = ci;
            const QImage img = cork->viewport()->grab().toImage();
            bool cardOk = false, boardOk = false;
            if (card) {
                const QPoint inCard = cork->mapFromScene(card->mapToScene(QPointF(2.5, card->cardSize().height() / 2)));
                const QPoint onBoard = cork->mapFromScene(card->mapToScene(QPointF(-9, card->cardSize().height() / 2)));
                cardOk = near(img.pixelColor(inCard), theme::color(Role::CardPaper), 10);
                boardOk = near(img.pixelColor(onBoard), theme::color(Role::CorkboardBackground), 16);
                if (!cardOk || !boardOk)
                    std::printf("      card %s board %s\n", qPrintable(img.pixelColor(inCard).name()), qPrintable(img.pixelColor(onBoard).name()));
            }
            check(cardOk && boardOk, QStringLiteral("Midnight: corkboard and index cards are painted in the theme's colors"));
            shot(&w, QStringLiteral("33-theme-midnight-corkboard"));
            setView(&w, QStringLiteral("viewOutline"));
            settle(300);
            check(fp->outliner()->palette().color(QPalette::Base) == theme::color(Role::OutlinerBackground)
                      && fp->outliner()->palette().color(QPalette::AlternateBase) == theme::color(Role::OutlinerAlternate),
                  QStringLiteral("outliner colors"));
            shot(&w, QStringLiteral("34-theme-midnight-outliner"));
            setView(&w, QStringLiteral("viewText"));
        }

        // one picture per theme
        binder->select({folder ? folder : item}, true);
        setView(&w, QStringLiteral("viewCork"));
        trigger(&w, QStringLiteral("Split Horizontally"));
        binder->select({item}, true);
        settle(300);
        {
            int n = 0;
            for (const theme::Entry &te : theme::available()) {
                themeui::choose(&w, te.id, false);
                QString file = te.name.toLower();
                file.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
                shot(&w, QStringLiteral("35-themes-%1-%2").arg(++n, 2, 10, QLatin1Char('0')).arg(file));
            }
        }
        trigger(&w, QStringLiteral("No Split"));
        binder->select({item}, true);
        setView(&w, QStringLiteral("viewText"));
        settle(300);
        fp = firstPane();
        fe = fp ? fp->currentEditor() : nullptr;

        // ---- settings of versions before 0.5 (composition and corkboard colors)
        {
            themeui::choose(&w, QStringLiteral("light"), false);
            QSettings raw;
            raw.setValue(QStringLiteral("appearance/compText"), QColor(10, 200, 30));
            raw.setValue(QStringLiteral("appearance/compPaper"), QColor(40, 45, 56)); // the old default: not a change
            raw.setValue(QStringLiteral("appearance/corkColor"), QStringLiteral("#112233"));
            raw.sync();
            as.load();
            check(as.ownColors.value(QStringLiteral("compositionText")) == QColor(10, 200, 30)
                      && as.ownColors.value(QStringLiteral("corkboardBackground")) == QColor(0x11, 0x22, 0x33)
                      && !as.ownColors.contains(QStringLiteral("compositionPaper")),
                  QStringLiteral("colors set in earlier versions become own colors"));
            as.save();
            QSettings fresh(QSettings().fileName(), QSettings::IniFormat);
            check(!fresh.contains(QStringLiteral("appearance/compText")) && !fresh.contains(QStringLiteral("appearance/corkColor"))
                      && fresh.value(QStringLiteral("appearance/colors/compositionText")).toString() == QLatin1String("#0ac81e"),
                  QStringLiteral("and are stored in the new place"), fresh.allKeys().join(QLatin1Char(',')));
            as.forename = QStringLiteral("Ada");
            as.surname = QStringLiteral("Writer");
            as.ownColors.clear();
            as.notifyChanged();
        }

        // ---- File > Options > Appearance: own colors and fonts on top of the theme
        QString myTheme;
        {
            themeui::choose(&w, QStringLiteral("sepia"), false);
            OptionsDialog od(&w, 1);
            od.show();
            settle(200);
            auto *areas = od.findChild<QListWidget *>(QStringLiteral("appearanceAreas"));
            auto *combo = od.findChild<QComboBox *>(QStringLiteral("themeCombo"));
            check(areas && areas->count() == 12 && combo && combo->currentData().toString() == QLatin1String("sepia"),
                  QStringLiteral("Appearance: the twelve areas of Scrivener's list, the theme in use is selected"));
            auto *paperBtn = od.findChild<QPushButton *>(QStringLiteral("color_editorPaper"));
            auto *paperReset = od.findChild<QToolButton *>(QStringLiteral("colorReset_editorPaper"));
            check(paperBtn && paperReset && !paperReset->isEnabled() && od.shownColor(QStringLiteral("editorPaper")) == QColor(0xfa, 0xf3, 0xe4)
                      && od.shownColor(QStringLiteral("binderBackground")) == od.shownColor(QStringLiteral("base")),
                  QStringLiteral("colors the theme leaves open show what they follow"));
            shot(&od, QStringLiteral("36-options-appearance-general"));
            od.setOwnColor(QStringLiteral("base"), QColor(0xf0, 0xe0, 0xc0));
            check(od.shownColor(QStringLiteral("binderBackground")) == QColor(0xf0, 0xe0, 0xc0)
                      && od.shownColor(QStringLiteral("outlinerBackground")) == QColor(0xf0, 0xe0, 0xc0),
                  QStringLiteral("changing a general color carries the areas that follow it"));
            od.setOwnColor(QStringLiteral("base"), QColor());
            od.setOwnColor(QStringLiteral("editorPaper"), QColor(0xff, 0xee, 0xdd));
            od.setOwnColor(QStringLiteral("cardPaper"), QColor(0xe8, 0xf4, 0xff));
            od.setOwnFont(QStringLiteral("binder"), theme::FontSpec{QString(), 14, true, false});
            od.setOwnFont(QStringLiteral("cardTitle"), theme::FontSpec{QStringLiteral("DejaVu Serif"), 0, false, true});
            auto *binderFont = od.findChild<QPushButton *>(QStringLiteral("font_binder"));
            check(od.ownChanges() == 4 && paperReset->isEnabled() && od.shownColor(QStringLiteral("editorPaper")) == QColor(0xff, 0xee, 0xdd)
                      && binderFont && binderFont->text().contains(QLatin1String("14")) && binderFont->text().contains(QLatin1String("bold"))
                      && od.findChild<QPushButton *>(QStringLiteral("resetLook"))->isEnabled(),
                  QStringLiteral("own colors and fonts are shown and counted"), binderFont ? binderFont->text() : QString());
            check(as.ownColors.isEmpty() && fe && fe->palette().color(QPalette::Base) == QColor(0xfa, 0xf3, 0xe4),
                  QStringLiteral("nothing changes before Apply or OK"));
            od.showSection(OptionsDialog::SAppearance, QStringLiteral("Main Editor"));
            shot(&od, QStringLiteral("37-options-appearance-editor"));
            od.showSection(OptionsDialog::SAppearance, QStringLiteral("Corkboard"));
            shot(&od, QStringLiteral("38-options-appearance-corkboard"));
            od.apply();
            settle(300);
            const QColor paintedPaper = fe ? fe->viewport()->grab().toImage().pixelColor(2, 2) : QColor();
            check(as.ownColors.size() == 2 && as.ownFonts.size() == 2 && as.theme == QLatin1String("sepia") && fe
                      && fe->palette().color(QPalette::Base) == QColor(0xff, 0xee, 0xdd) && paintedPaper == QColor(0xff, 0xee, 0xdd)
                      && theme::color(Role::CardPaper) == QColor(0xe8, 0xf4, 0xff),
                  QStringLiteral("Apply: own colors on top of the theme, live"), paintedPaper.name());
            const QFont bf = binder->tree()->font();
            check(qFuzzyCompare(bf.pointSizeF(), 14.0) && bf.bold() && fp && !fp->outliner()->font().bold()
                      && qFuzzyCompare(fp->outliner()->font().pointSizeF(), QApplication::font().pointSizeF())
                      && theme::font(theme::FontRole::CardTitle).family() == QLatin1String("DejaVu Serif")
                      && theme::font(theme::FontRole::CardTitle).italic(),
                  QStringLiteral("Apply: binder and card title fonts; other areas keep theirs"),
                  QStringLiteral("%1 pt").arg(bf.pointSizeF()));
            {
                QSettings fresh(QSettings().fileName(), QSettings::IniFormat);
                check(fresh.value(QStringLiteral("appearance/colors/editorPaper")).toString() == QLatin1String("#ffeedd")
                          && fresh.value(QStringLiteral("appearance/fonts/binder")).toString() == QLatin1String("|14|b|"),
                      QStringLiteral("own colors and fonts are stored in the settings"),
                      fresh.value(QStringLiteral("appearance/fonts/binder")).toString());
            }
            binder->select({folder ? folder : item}, true);
            setView(&w, QStringLiteral("viewCork"));
            settle(400);
            shot(&w, QStringLiteral("39-own-colors-and-fonts"));
            setView(&w, QStringLiteral("viewText"));
            binder->select({item}, true);
            settle(300);
            fp = firstPane();
            fe = fp ? fp->currentEditor() : nullptr;

            // the interface font
            const double before = QApplication::font().pointSizeF();
            od.setOwnFont(QStringLiteral("interface"), theme::FontSpec{QString(), before + 2, false, false});
            od.apply();
            settle(200);
            const bool bigger = qFuzzyCompare(QApplication::font().pointSizeF(), before + 2);
            od.setOwnFont(QStringLiteral("interface"), theme::FontSpec());
            od.apply();
            settle(200);
            check(bigger && qFuzzyCompare(QApplication::font().pointSizeF(), before),
                  QStringLiteral("the interface font can be changed and set back"));

            // one color back to the theme
            if (paperReset)
                paperReset->click();
            check(od.ownChanges() == 3 && od.shownColor(QStringLiteral("editorPaper")) == QColor(0xfa, 0xf3, 0xe4),
                  QStringLiteral("↺ gives one color back to the theme"));
            od.setOwnColor(QStringLiteral("editorPaper"), QColor(0xff, 0xee, 0xdd));

            // save as a theme of one's own
            QString err;
            myTheme = od.saveLookAsTheme(QStringLiteral("My Sepia"), &err);
            check(myTheme == QLatin1String("user:my-sepia") && od.ownChanges() == 0 && combo->currentData().toString() == myTheme
                      && QFile::exists(theme::userThemeDir() + QLatin1String("/my-sepia.json"))
                      && od.shownColor(QStringLiteral("editorPaper")) == QColor(0xff, 0xee, 0xdd),
                  QStringLiteral("Save as Theme: a theme file of one's own, selected, the changes are part of it"), err + myTheme);
            QString err2;
            check(od.saveLookAsTheme(QStringLiteral("my sepia"), &err2).isEmpty() && !err2.isEmpty()
                      && od.saveLookAsTheme(QStringLiteral("Dark Mode"), &err2).isEmpty(),
                  QStringLiteral("a name that is in use is refused"));
            od.accept();
            settle(300);
            theme::Theme mine;
            check(as.theme == myTheme && as.ownColors.isEmpty() && as.ownFonts.isEmpty() && theme::load(myTheme, &mine)
                      && mine.colors.value(Role::EditorPaper) == QColor(0xff, 0xee, 0xdd) && mine.colors.value(Role::Window) == QColor(0xec, 0xe3, 0xd0)
                      && mine.fonts.value(theme::FontRole::Binder).bold && fe && fe->palette().color(QPalette::Base) == QColor(0xff, 0xee, 0xdd)
                      && qFuzzyCompare(binder->tree()->font().pointSizeF(), 14.0),
                  QStringLiteral("OK: the own theme is in use and looks the same"));
        }

        // ---- choosing another theme with own changes on top
        {
            as.ownColors.insert(QStringLiteral("editorText"), QColor(0x20, 0x40, 0x60));
            as.notifyChanged();
            themeui::choose(&w, QStringLiteral("light"), false);
            check(as.theme == QLatin1String("light") && as.ownColors.size() == 1 && theme::color(Role::EditorText) == QColor(0x20, 0x40, 0x60),
                  QStringLiteral("own changes stay when they are kept"));
            QTimer::singleShot(400, qApp, [] {
                auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (!box)
                    return;
                box->grab().save(outDir + QLatin1String("/40-change-theme-question.png"));
                for (QAbstractButton *b : box->buttons())
                    if (b->text().contains(QLatin1String("as It Is")))
                        b->click();
            });
            const bool chosen = themeui::choose(&w, QStringLiteral("ocean"));
            check(chosen && as.theme == QLatin1String("ocean") && as.ownColors.isEmpty()
                      && theme::color(Role::EditorText) == QColor(0x17, 0x30, 0x42),
                  QStringLiteral("the question \"Use the Theme as It Is\" drops them"));
            as.ownColors.insert(QStringLiteral("editorText"), QColor(0x20, 0x40, 0x60));
            QTimer::singleShot(400, qApp, [] {
                if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                    box->reject();
            });
            check(!themeui::choose(&w, QStringLiteral("dark")) && as.theme == QLatin1String("ocean"),
                  QStringLiteral("Cancel leaves theme and changes alone"));
            as.ownColors.clear();
            as.notifyChanged();
        }

        // ---- View > Theme
        if (auto *menu = w.findChild<QMenu *>(QStringLiteral("themeMenu"))) {
            emit menu->aboutToShow();
            int checked = 0, themesListed = 0;
            QString checkedName;
            bool hasSave = false, hasManage = false, hasImport = false, hasCustomize = false;
            for (QAction *ac : menu->actions()) {
                if (ac->isCheckable()) {
                    ++themesListed;
                    if (ac->isChecked()) {
                        ++checked;
                        checkedName = ac->text();
                    }
                }
                hasSave = hasSave || ac == byKey(&w, QStringLiteral("themeSave"));
                hasManage = hasManage || ac == byKey(&w, QStringLiteral("themeManage"));
                hasImport = hasImport || ac == byKey(&w, QStringLiteral("themeImport"));
                hasCustomize = hasCustomize || ac == byKey(&w, QStringLiteral("themeCustomize"));
            }
            check(themesListed == theme::available().size() && checked == 1 && checkedName == QLatin1String("Ocean") && hasSave
                      && hasManage && hasImport && hasCustomize,
                  QStringLiteral("View > Theme lists all themes, marks the one in use and has the theme commands"));
            menu->show();
            shot(menu, QStringLiteral("41-theme-menu"));
            menu->hide();
        }

        // ---- the theme list: duplicate, rename, export, import, delete
        {
            ThemeManagerDialog tm(&w);
            tm.show();
            settle(200);
            auto *list = tm.findChild<QListWidget *>(QStringLiteral("themeList"));
            auto *del = tm.findChild<QPushButton *>(QStringLiteral("themeDelete"));
            auto *ren = tm.findChild<QPushButton *>(QStringLiteral("themeRename"));
            auto *use = tm.findChild<QPushButton *>(QStringLiteral("themeUse"));
            check(list && list->count() == theme::available().size() && tm.selectedId() == QLatin1String("ocean") && !use->isEnabled()
                      && !del->isEnabled() && !ren->isEnabled(),
                  QStringLiteral("Themes window: all themes, the one in use selected, built-in themes cannot be deleted"));
            tm.select(myTheme);
            check(del->isEnabled() && ren->isEnabled() && use->isEnabled(), QStringLiteral("own themes can be renamed and deleted"));
            shot(&tm, QStringLiteral("42-themes-window"));
            QString err;
            const QString copy = tm.duplicate(QStringLiteral("midnight"), QStringLiteral("Midnight Copy"), &err);
            theme::Theme ct, orig;
            theme::load(QStringLiteral("midnight"), &orig);
            check(copy == QLatin1String("user:midnight-copy") && theme::load(copy, &ct) && ct.colors == orig.colors
                      && tm.selectedId() == copy && del->isEnabled(),
                  QStringLiteral("Duplicate makes an own theme from a built-in one"), err);
            QString newId;
            check(theme::renameUserTheme(copy, QStringLiteral("Deep Night"), &newId, &err) && newId == copy
                      && theme::load(copy, &ct) && ct.name == QLatin1String("Deep Night"),
                  QStringLiteral("Rename keeps the theme and changes its name"), err);
            const QString exported = home.path() + QLatin1String("/deep-night.qbtheme");
            check(theme::exportTheme(copy, exported, &err) && QFile::exists(exported), QStringLiteral("Export writes a theme file"), err);
            const QString imported = theme::importTheme(exported, &err);
            theme::Theme it;
            check(!imported.isEmpty() && imported != copy && theme::load(imported, &it) && it.name == QLatin1String("Deep Night 2")
                      && it.colors == orig.colors,
                  QStringLiteral("Import adds the file as a new theme and keeps names apart"), err + imported);
            QFile junk(home.path() + QLatin1String("/junk.qbtheme"));
            junk.open(QIODevice::WriteOnly);
            junk.write("not a theme");
            junk.close();
            check(theme::importTheme(junk.fileName(), &err).isEmpty() && !err.isEmpty(), QStringLiteral("a file that is no theme is refused"));
            check(!theme::removeUserTheme(QStringLiteral("dark"), &err) && theme::removeUserTheme(imported, &err),
                  QStringLiteral("only own themes can be deleted"));
            // deleting the theme in use falls back to the default
            themeui::choose(&w, copy, false);
            tm.reload(copy);
            check(theme::isDark() && !use->isEnabled(), QStringLiteral("the copy is in use"));
            QTimer::singleShot(400, qApp, [] {
                if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                    for (QAbstractButton *b : box->buttons())
                        if (box->buttonRole(b) == QMessageBox::YesRole)
                            b->click();
            });
            del->click();
            settle(300);
            check(as.theme == QLatin1String("system") && !QFile::exists(theme::userThemeDir() + QLatin1String("/midnight-copy.json"))
                      && QApplication::palette() == desktopPalette,
                  QStringLiteral("deleting the theme in use returns to Default"), as.theme);
            tm.close();
        }

        // ---- a switched-off add-on: its theme is no longer there, the look falls back
        if (fromAddon > 0) {
            QString graphite;
            for (const theme::Entry &te : theme::available())
                if (te.name == QLatin1String("Graphite"))
                    graphite = te.id;
            themeui::choose(&w, graphite, false);
            const bool wasDark = theme::isDark() && theme::color(Role::CardPaper) == QColor(0x52, 0x52, 0x52);
            addons::setEnabled(QStringLiteral("themes-extra"), false);
            as.notifyChanged();
            check(wasDark && !theme::isDark() && QApplication::palette() == desktopPalette,
                  QStringLiteral("add-on theme with area colors; without the add-on the desktop colors are used"));
            addons::setEnabled(QStringLiteral("themes-extra"), true);
        }

        // ---- composition mode takes its colors from the theme
        themeui::choose(&w, QStringLiteral("ocean"), false);
        binder->select({item}, true);
        settle(200);
        trigger(&w, QStringLiteral("Composition Mode"));
        settle(500);
        CompositionWindow *cw2 = nullptr;
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *cw = qobject_cast<CompositionWindow *>(tl))
                if (cw->isVisible())
                    cw2 = cw;
        if (cw2) {
            cw2->resize(1440, 900);
            auto *view = cw2->findChild<TextColumnView *>();
            TextEditor *ce = view ? view->activeEditor() : nullptr;
            check(ce && ce->palette().color(QPalette::Base) == QColor(0x1a, 0x3d, 0x4b)
                      && ce->palette().color(QPalette::Text) == QColor(0xe6, 0xf1, 0xf5),
                  QStringLiteral("composition mode: paper and text of the theme"));
            as.ownColors.insert(QStringLiteral("compositionPaper"), QColor(0xff, 0xff, 0xff));
            as.ownColors.insert(QStringLiteral("compositionText"), QColor(0x10, 0x10, 0x10));
            as.notifyChanged();
            settle(200);
            const QColor painted = ce ? ce->viewport()->grab().toImage().pixelColor(2, 2) : QColor();
            check(ce && ce->palette().color(QPalette::Base) == QColor(0xff, 0xff, 0xff)
                      && ce->palette().color(QPalette::Text) == QColor(0x10, 0x10, 0x10) && painted == QColor(0xff, 0xff, 0xff),
                  QStringLiteral("composition mode: black on white as an own change, while it is open"), painted.name());
            shot(cw2, QStringLiteral("43-composition-own-colors"));
            cw2->close();
            settle(300);
        } else {
            check(false, QStringLiteral("composition mode opens (themes)"));
        }
        as.ownColors.clear();
        as.ownFonts.clear();
        theme::removeUserTheme(myTheme, nullptr);
        themeui::choose(&w, QStringLiteral("system"), false);
    }
    // ---------------------------------------------------------------- menus, toolbars, shortcuts, layouts (0.6.0)
    if (proj) {
        AppSettings &as = AppSettings::instance();
        binder = w.findChild<BinderPanel *>();
        auto *inspector = w.findChild<Inspector *>();
        // the first editor is the left or upper one
        auto *editorSplit = w.findChild<EditorPane *>() ? qobject_cast<QSplitter *>(w.findChild<EditorPane *>()->parentWidget()) : nullptr;
        auto *pane0 = editorSplit ? qobject_cast<EditorPane *>(editorSplit->widget(0)) : nullptr;
        auto *pane1 = editorSplit ? qobject_cast<EditorPane *>(editorSplit->widget(1)) : nullptr;
        BinderItem *item = findTitle(proj->draft(), QStringLiteral("Counting Ships"));
        BinderItem *folder = item ? item->parent : nullptr;
        BinderItem *folder2 = findTitle(proj->draft(), QStringLiteral("The Visitor"));
        auto *mainBar = w.findChild<QToolBar *>(QStringLiteral("mainToolbar"));
        auto *formatBar = w.findChild<QToolBar *>(QStringLiteral("formatBar"));
        check(pane0 && pane1 && item && folder && folder2 && mainBar && formatBar && editorSplit && inspector,
              QStringLiteral("0.6: window parts found"));
        commands::setCustom({});
        as.toolbarMain.clear();
        as.toolbarFormat.clear();
        as.toolbarIconSize = 22;
        as.toolbarStyle = QStringLiteral("icons");
        as.notifyChanged();
        w.applyLayoutById(QStringLiteral("default"));
        binder->select({item}, true);
        settle(200);

        // ---- the menu bar
        {
            QStringList titles;
            for (QAction *a : w.menuBar()->actions())
                titles << menuText(a->text());
            check(titles == QStringList({QStringLiteral("File"), QStringLiteral("Edit"), QStringLiteral("Insert"), QStringLiteral("View"),
                                         QStringLiteral("Navigate"), QStringLiteral("Project"), QStringLiteral("Documents"),
                                         QStringLiteral("Format"), QStringLiteral("Window"), QStringLiteral("Help")}),
                  QStringLiteral("menu bar: File, Edit, Insert, View, Navigate, Project, Documents, Format, Window, Help"),
                  titles.join(QLatin1String(", ")));

            // the places the tutorial names
            const QList<QStringList> paths = {
                {"File", "New Project…"}, {"File", "Save"}, {"File", "Save as Template…"}, {"File", "Import", "Files…"},
                {"File", "Import", "Import and Split…"}, {"File", "Export", "Files…"}, {"File", "Back Up", "Back Up Now"},
                {"File", "Back Up", "Back Up To…"}, {"File", "Compile…"}, {"File", "Print Current Document…"},
                {"File", "Show Project in File Manager"}, {"File", "Options…"}, {"File", "Exit"},
                {"Edit", "Find", "Find…"}, {"Edit", "Find", "Project Search…"}, {"Edit", "Move", "Move Left (out of the folder)"},
                {"Edit", "Link to Document…"}, {"Edit", "Select", "Select Annotation or Note"},
                {"Insert", "Image From File…"}, {"Insert", "Comment"}, {"Insert", "Footnote"}, {"Insert", "Inline Footnote"},
                {"Insert", "Inline Annotation"}, {"Insert", "Auto Number"},
                {"View", "Corkboard"}, {"View", "Outliner"}, {"View", "Editor Layout", "Split Horizontally"},
                {"View", "Editor Layout", "Split Vertically"}, {"View", "Text Editing", "Page View"}, {"View", "Text Editing", "Typewriter Scrolling"},
                {"View", "Text Editing", "Show Titles in Combined Text"}, {"View", "Corkboard Options", "Status Stamps"},
                {"View", "Corkboard Options", "Freeform"}, {"View", "Corkboard Options", "Arrange by Label"},
                {"View", "Outliner Options", "Show Synopses"}, {"View", "Use Label Color In", "Index Cards"},
                {"View", "Toolbars", "Customize Toolbars…"}, {"View", "Composition Mode"},
                {"Navigate", "Reveal in Binder"}, {"Navigate", "Open", "as Quick Reference"}, {"Navigate", "Open", "in Other Editor"}, {"Navigate", "Open", "in Copyholder"},
                {"Navigate", "Open Quick Reference"}, {"Navigate", "Editor", "Lock in Place"},
                {"Navigate", "Binder Selection Affects", "Left or Top Editor"}, {"Navigate", "Inspect", "Snapshots"},
                {"Project", "New From Template"}, {"Project", "Project Settings…"}, {"Project", "Project Targets…"},
                {"Project", "Project Keywords…"}, {"Project", "Writing History…"}, {"Project", "Empty Trash…"},
                {"Documents", "Snapshots", "Take Snapshot"}, {"Documents", "Split", "at Selection"},
                {"Documents", "Split", "with Selection as Title"}, {"Documents", "Merge"},
                {"Documents", "Convert", "Convert to Folder"}, {"Documents", "Convert", "Text to Default Formatting…"},
                {"Documents", "Change Icon"}, {"Documents", "Default Template for Subdocuments"},
                {"Documents", "Add to Project Bookmarks"},
                {"Format", "Font", "Show Fonts…"}, {"Format", "Style", "New Style From Selection…"},
                {"Format", "Style", "Styles Panel"}, {"Format", "Revision Mode"},
                {"Window", "Themes", "Manage Themes…"}, {"Window", "Layouts", "Manage Layouts…"},
                {"Window", "Layouts", "Save Current Layout…"}, {"Window", "Float Quick Reference Panels"},
                {"Help", "User Manual"}, {"Help", "Keyboard Shortcuts…"}};
            QStringList missing;
            for (const QStringList &path : paths)
                if (!menuPath(&w, path))
                    missing << path.join(QLatin1String(" > "));
            check(missing.isEmpty(), QStringLiteral("the menu commands of the tutorial are where it says (%1 places)").arg(paths.size()),
                  missing.join(QLatin1String("; ")));

            // short entries stand for the commands: text, shortcut, state, action
            QAction *splitAlias = menuPath(&w, {QStringLiteral("Documents"), QStringLiteral("Split"), QStringLiteral("at Selection")});
            check(splitAlias && splitAlias->text().endsWith(QLatin1String("\tCtrl+K")) && splitAlias != byKey(&w, QStringLiteral("split")),
                  QStringLiteral("Documents > Split > at Selection shows the shortcut of the command"),
                  splitAlias ? splitAlias->text() : QString());
            QAction *cards = menuPath(&w, {QStringLiteral("View"), QStringLiteral("Use Label Color In"), QStringLiteral("Index Cards")});
            const bool before = as.tintCardsWithLabel;
            if (cards)
                cards->trigger();
            settle(100);
            const bool flipped = as.tintCardsWithLabel != before && cards && cards->isChecked() == as.tintCardsWithLabel
                                 && byKey(&w, QStringLiteral("tintCards"))->isChecked() == as.tintCardsWithLabel;
            if (cards)
                cards->trigger();
            settle(100);
            check(flipped && as.tintCardsWithLabel == before,
                  QStringLiteral("View > Use Label Color In > Index Cards switches the option and shows its state"));
            if (QMenu *windowMenu = w.findChild<QMenu *>(QStringLiteral("windowMenu"))) {
                windowMenu->show();
                shot(windowMenu, QStringLiteral("44-window-menu"));
                windowMenu->hide();
            }
        }

        // ---- every command is known, with one shortcut each
        {
            const QList<commands::Command> all = commands::all();
            int unlisted = 0, other = 0, styles = 0, layoutCmds = 0;
            for (QAction *a : w.actions())
                if (!a->objectName().isEmpty() && !commands::find(a->objectName()))
                    ++unlisted;
            QHash<QString, QString> bySeq;
            QStringList doubles;
            for (const commands::Command &c : all) {
                other += c.category == QLatin1String("Other");
                styles += c.key.startsWith(QLatin1String("style:"));
                layoutCmds += c.key.startsWith(QLatin1String("layout:"));
                if (c.standard.isEmpty())
                    continue;
                const QString seq = c.standard.toString(QKeySequence::PortableText);
                if (bySeq.contains(seq))
                    doubles << seq + QLatin1String(": ") + bySeq.value(seq) + QLatin1String(" / ") + c.key;
                bySeq.insert(seq, c.key);
            }
            QStringList outside;
            for (const commands::Command &c : all)
                if (c.category == QLatin1String("Other"))
                    outside << c.key;
            check(all.size() > 150 && unlisted == 0 && other == 0,
                  QStringLiteral("all commands are listed under their menu (%1 commands)").arg(all.size()),
                  QStringLiteral("unlisted %1, outside the menus: %2").arg(unlisted).arg(outside.join(QLatin1Char(' '))));
            check(doubles.isEmpty(), QStringLiteral("no standard shortcut is used twice"), doubles.join(QLatin1String("; ")));
            check(styles == styles::sheet(proj->formatting).styles.size() + 1 && layoutCmds == layouts::available().size(),
                  QStringLiteral("the styles of the project and the layouts are commands too"),
                  QStringLiteral("%1 styles, %2 layouts").arg(styles).arg(layoutCmds));
            const QList<QPair<QString, QString>> tutorial = {
                {"newText", "Ctrl+N"}, {"newFolder", "Alt+Shift+N"}, {"find", "Ctrl+F"}, {"projectSearch", "Ctrl+Shift+F"},
                {"findNext", "F3"}, {"findPrev", "Shift+F3"}, {"viewText", "Ctrl+1"}, {"viewCork", "Ctrl+2"}, {"viewOutline", "Ctrl+3"},
                {"composition", "F11"}, {"snapshot", "Ctrl+5"}, {"comment", "Shift+F2"}, {"prevDoc", "Alt+Shift+Up"},
                {"nextDoc", "Alt+Shift+Down"}, {"back", "Ctrl+["}, {"forward", "Ctrl+]"}, {"save", "Ctrl+S"}, {"manual", "F1"},
                {"typewriter", "Meta+Ctrl+T"}};
            QStringList wrong;
            for (const auto &t : tutorial) {
                const commands::Command *c = commands::find(t.first);
                if (!c || c->standard != QKeySequence(t.second, QKeySequence::PortableText))
                    wrong << t.first + QLatin1String("=") + (c ? c->standard.toString(QKeySequence::PortableText) : QStringLiteral("?"));
            }
            check(wrong.isEmpty(), QStringLiteral("the keyboard shortcuts of the tutorial are the standard ones"), wrong.join(QLatin1String(", ")));
        }

        // ---- File > Options > Keyboard
        {
            OptionsDialog od(&w, OptionsDialog::Keyboard);
            od.show();
            settle(200);
            KeyboardPage *kp = od.keyboardPage();
            auto *tree = kp->findChild<QTreeWidget *>(QStringLiteral("shortcutTree"));
            auto *pages = od.findChild<QListWidget *>(QStringLiteral("optionPages"));
            QStringList groups;
            for (int i = 0; tree && i < tree->topLevelItemCount(); ++i)
                groups << tree->topLevelItem(i)->text(0);
            check(pages && pages->currentItem() && pages->currentItem()->text() == QLatin1String("Keyboard") && tree
                      && kp->visibleCommands() == commands::all().size() && groups.contains(QStringLiteral("File"))
                      && groups.contains(QStringLiteral("Window")) && groups.contains(QStringLiteral("Styles"))
                      && groups.contains(QStringLiteral("Layouts")) && kp->changes() == 0,
                  QStringLiteral("Options > Keyboard lists every command by menu, plus styles and layouts"),
                  groups.join(QLatin1String(", ")));

            const QKeySequence free(QStringLiteral("Ctrl+Alt+M"));
            check(kp->assign(QStringLiteral("merge"), free, false) == KeyboardPage::Result::Done && kp->shown(QStringLiteral("merge")) == free
                      && kp->changes() == 1 && byKey(&w, QStringLiteral("merge"))->shortcut() != free,
                  QStringLiteral("a new shortcut is noted in the options, not yet in the program"));
            QString other;
            const QKeySequence ctrlK(QStringLiteral("Ctrl+K"));
            const bool refused = kp->assign(QStringLiteral("duplicate"), ctrlK, false, &other) == KeyboardPage::Result::Conflict
                                 && other == QLatin1String("split") && kp->shown(QStringLiteral("duplicate")) == QKeySequence(QStringLiteral("Ctrl+D"));
            const bool taken = kp->assign(QStringLiteral("duplicate"), ctrlK, true) == KeyboardPage::Result::Done
                               && kp->shown(QStringLiteral("duplicate")) == ctrlK && kp->shown(QStringLiteral("split")).isEmpty();
            check(refused && taken, QStringLiteral("a shortcut in use is reported; taking it over leaves the other command without one"), other);
            check(kp->assign(QStringLiteral("merge"), QKeySequence(QStringLiteral("A")), false) == KeyboardPage::Result::Unusable
                      && kp->assign(QStringLiteral("merge"), QKeySequence(QStringLiteral("Shift+B")), false) == KeyboardPage::Result::Unusable
                      && kp->shown(QStringLiteral("merge")) == free
                      && kp->assign(QStringLiteral("stylesPanel"), QKeySequence(QStringLiteral("F9")), false) == KeyboardPage::Result::Done,
                  QStringLiteral("letters without Ctrl, Alt or Meta are refused; function keys are fine"));
            check(kp->assign(QStringLiteral("style:Block Quote"), QKeySequence(QStringLiteral("Ctrl+Alt+9")), false) == KeyboardPage::Result::Done
                      && kp->assign(QStringLiteral("layout:editor-only"), QKeySequence(QStringLiteral("Ctrl+Alt+F9")), false)
                             == KeyboardPage::Result::Done,
                  QStringLiteral("styles and layouts take shortcuts"));

            kp->setFilter(QStringLiteral("snapshot"));
            const int snapshotRows = kp->visibleCommands();
            kp->setFilter(QStringLiteral("Ctrl+K"));
            const int ctrlKRows = kp->visibleCommands();
            kp->setFilter(QString());
            check(snapshotRows >= 3 && snapshotRows <= 6 && ctrlKRows >= 1 && ctrlKRows <= 3
                      && kp->visibleCommands() == commands::all().size(),
                  QStringLiteral("the list can be searched by command and by shortcut"),
                  QStringLiteral("%1 / %2").arg(snapshotRows).arg(ctrlKRows));

            kp->select(QStringLiteral("duplicate"));
            auto *edit = kp->findChild<QKeySequenceEdit *>(QStringLiteral("shortcutEdit"));
            auto *hint = kp->findChild<QLabel *>(QStringLiteral("shortcutHint"));
            check(edit && edit->keySequence() == ctrlK && hint && hint->text().startsWith(QLatin1String("6 shortcuts")),
                  QStringLiteral("the selected command shows its shortcut; the changes are counted"), hint ? hint->text() : QString());
            shot(&od, QStringLiteral("45-options-keyboard"));

            const QString keysFile = projectsTmp.path() + QLatin1String("/my.qbkeys");
            QString kerr;
            const bool exported = kp->exportTo(keysFile, &kerr);
            od.accept();
            settle(200);
            check(byKey(&w, QStringLiteral("merge"))->shortcut() == free && byKey(&w, QStringLiteral("duplicate"))->shortcut() == ctrlK
                      && byKey(&w, QStringLiteral("split"))->shortcut().isEmpty()
                      && commands::shortcutOf(QStringLiteral("style:Block Quote")) == QKeySequence(QStringLiteral("Ctrl+Alt+9"))
                      && commands::custom().size() == 6,
                  QStringLiteral("OK puts the shortcuts on the commands and stores them"), QString::number(commands::custom().size()));
            QAction *splitAlias = menuPath(&w, {QStringLiteral("Documents"), QStringLiteral("Split"), QStringLiteral("at Selection")});
            QAction *mergeAlias = menuPath(&w, {QStringLiteral("Documents"), QStringLiteral("Merge")});
            check(splitAlias && !splitAlias->text().contains(QLatin1Char('\t')) && mergeAlias
                      && mergeAlias->text().endsWith(QLatin1String("\tCtrl+Alt+M")),
                  QStringLiteral("the menus show the new shortcuts"));

            // the shortcut of a style works, also after the styles of the project changed
            binder->select({item}, true);
            settle(200);
            TextEditor *ed = pane0->currentEditor();
            w.activateWindow();
            if (ed)
                ed->setFocus();
            settle(200);
            QAction *quote = byKey(&w, QStringLiteral("style:Block Quote"));
            if (ed && quote) {
                QTextCursor c = ed->textCursor();
                c.movePosition(QTextCursor::End);
                ed->setTextCursor(c);
                quote->trigger();
                settle(100);
            }
            const bool styled = ed && ed->paragraphStyleAtCursor() == QLatin1String("Block Quote");
            if (ed && byKey(&w, QStringLiteral("style:")))
                byKey(&w, QStringLiteral("style:"))->trigger();
            emit proj->stylesChanged();
            settle(100);
            QAction *quote2 = byKey(&w, QStringLiteral("style:Block Quote"));
            check(styled && ed->paragraphStyleAtCursor().isEmpty() && quote2
                      && quote2->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+9")),
                  QStringLiteral("style commands apply their style and keep their shortcut when the styles change"));

            // a key press reaches the command
            w.activateWindow();
            w.raise();
            if (ed)
                ed->setFocus();
            settle(200);
            const bool binderShown = byKey(&w, QStringLiteral("showBinder"))->isChecked();
            QTest::keySequence(w.windowHandle(), QKeySequence(QStringLiteral("Ctrl+Alt+B")));
            settle(150);
            const bool toggled = byKey(&w, QStringLiteral("showBinder"))->isChecked() != binderShown && binder->isVisible() != binderShown;
            byKey(&w, QStringLiteral("showBinder"))->setChecked(true);
            settle(100);
            QTest::keySequence(w.windowHandle(), QKeySequence(QStringLiteral("Ctrl+Alt+F9")));
            settle(200);
            const bool layoutByKey = binderShown && !byKey(&w, QStringLiteral("showInspector"))->isChecked() && !binder->isVisible();
            check(toggled && layoutByKey, QStringLiteral("pressing Ctrl+Alt+B toggles the binder; the own shortcut switches the layout"),
                  QStringLiteral("%1 %2").arg(toggled).arg(layoutByKey));
            w.applyLayoutById(QStringLiteral("default"));
            settle(150);

            // back to standard, file round trip
            OptionsDialog od2(&w, OptionsDialog::Keyboard);
            KeyboardPage *kp2 = od2.keyboardPage();
            const bool sees = kp2->changes() == 6 && kp2->shown(QStringLiteral("merge")) == free;
            kp2->resetOne(QStringLiteral("split"));
            const bool back = kp2->shown(QStringLiteral("split")) == ctrlK && kp2->shown(QStringLiteral("duplicate")).isEmpty();
            kp2->resetAll();
            const bool none = kp2->changes() == 0 && kp2->shown(QStringLiteral("duplicate")) == QKeySequence(QStringLiteral("Ctrl+D"));
            const bool imported = kp2->importFrom(keysFile, &kerr) && kp2->changes() == 6 && kp2->shown(QStringLiteral("duplicate")) == ctrlK;
            QString bad;
            QFile junk(projectsTmp.path() + QLatin1String("/junk.qbkeys"));
            if (junk.open(QIODevice::WriteOnly))
                junk.write("{\"name\":\"no shortcuts here\"}");
            junk.close();
            const bool refusedFile = !kp2->importFrom(junk.fileName(), &bad) && !bad.isEmpty() && kp2->changes() == 6;
            kp2->resetAll();
            od2.accept();
            settle(100);
            check(sees && back && none && exported && imported && refusedFile,
                  QStringLiteral("shortcuts: standard for one command and for all, export and import of a set"),
                  QStringLiteral("%1 %2 %3 %4 %5 %6 %7").arg(sees).arg(back).arg(none).arg(exported).arg(imported).arg(refusedFile).arg(kerr));
            check(commands::custom().isEmpty() && byKey(&w, QStringLiteral("split"))->shortcut() == ctrlK
                      && byKey(&w, QStringLiteral("merge"))->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+M")),
                  QStringLiteral("after All to Standard the program has its own shortcuts again"));
        }

        // ---- toolbars
        {
            auto barKeys = [](QToolBar *bar) {
                QStringList keys;
                for (QAction *a : bar->actions())
                    keys << (a->isSeparator() ? QStringLiteral("|") : a->objectName());
                return keys;
            };
            auto *viewBtn = w.findChild<QToolButton *>(QStringLiteral("viewButton"));
            auto *quick = w.findChild<QLineEdit *>(QStringLiteral("quickSearch"));
            auto *styleCombo = w.findChild<QComboBox *>(QStringLiteral("styleCombo"));
            const QStringList standardKeys = barKeys(mainBar);
            check(viewBtn && viewBtn->isVisible() && quick && quick->isVisible() && mainBar->actions().size() == 20
                      && mainBar->widgetForAction(mainBar->actions().first()) == viewBtn
                      && standardKeys.contains(QStringLiteral("viewCork")) && standardKeys.last() == QLatin1String("showInspector")
                      && viewBtn->x() < quick->x(),
                  QStringLiteral("main toolbar: View on the left, search and view modes in the middle, inspector on the right"),
                  standardKeys.join(QLatin1Char(' ')));
            QStringList viewEntries;
            if (viewBtn && viewBtn->menu())
                for (QAction *a : viewBtn->menu()->actions())
                    viewEntries << (a->menu() ? menuText(a->menu()->title()) : a->objectName());
            check(viewEntries.contains(QStringLiteral("showBinder")) && viewEntries.contains(QStringLiteral("showCollections"))
                      && viewEntries.contains(QStringLiteral("showInspector")) && viewEntries.contains(QStringLiteral("Layouts")),
                  QStringLiteral("the View button offers binder, collections, inspector and the layouts"), viewEntries.join(QLatin1Char(' ')));

            ToolbarDialog td(&w);
            td.show();
            settle(200);
            auto *available = td.findChild<QListWidget *>(QStringLiteral("toolbarAvailable"));
            const int offered = available ? available->count() : 0;
            const QStringList start = td.items();
            const bool noTwice = !td.addItem(QStringLiteral("compile")) && !td.addItem(QStringLiteral("styleCombo"))
                                 && !td.addItem(QStringLiteral("style:Heading 1")) && td.items() == start;
            const bool removed = td.removeItem(QStringLiteral("compile"));
            const bool added = td.addItem(QStringLiteral("snapshot"), 3) && td.addItem(QStringLiteral("|"), 3)
                               && td.addItem(QStringLiteral("layoutsButton"), 1);
            const bool moved = td.moveItem(4, 1) && td.items().value(4) == QLatin1String("snapshot") && td.items().value(5) == QLatin1String("|");
            check(start == toolbars::standard(toolbars::Bar::Main) && offered > 150 && noTwice && removed && added && moved
                      && available->count() == offered - 1,
                  QStringLiteral("Customize Toolbars: add, remove and move buttons; a command is in the bar once"),
                  QStringLiteral("%1 offered, %2 %3 %4 %5").arg(offered).arg(noTwice).arg(removed).arg(added).arg(moved));
            td.findChild<QComboBox *>(QStringLiteral("toolbarIconSize"))->setCurrentIndex(2);
            td.findChild<QComboBox *>(QStringLiteral("toolbarStyle"))->setCurrentIndex(1);
            shot(&td, QStringLiteral("46-customize-toolbars"));
            auto *fontCombo = formatBar->findChild<QFontComboBox *>();
            td.setBar(toolbars::Bar::Format);
            const bool formatList = td.items() == toolbars::standard(toolbars::Bar::Format) && !td.addItem(QStringLiteral("search"));
            td.removeItem(QStringLiteral("fontCombo"));
            td.addItem(QStringLiteral("superscript"));
            td.apply();
            settle(300);
            const QStringList now = barKeys(mainBar);
            check(formatList && now.contains(QStringLiteral("snapshot")) && !now.contains(QStringLiteral("compile"))
                      && now.indexOf(QStringLiteral("snapshot")) == 4 && mainBar->iconSize() == QSize(32, 32)
                      && mainBar->toolButtonStyle() == Qt::ToolButtonTextBesideIcon && viewBtn->iconSize() == QSize(32, 32)
                      && viewBtn->toolButtonStyle() == Qt::ToolButtonTextBesideIcon
                      && w.findChild<QToolButton *>(QStringLiteral("layoutsButton"))->isVisible(),
                  QStringLiteral("Apply rebuilds the main toolbar: new buttons, large icons, text beside them"), now.join(QLatin1Char(' ')));
            check(barKeys(formatBar).contains(QStringLiteral("superscript")) && fontCombo && !fontCombo->isVisible() && styleCombo
                      && styleCombo->isVisible() && w.findChild<QLineEdit *>(QStringLiteral("quickSearch")) == quick && quick->isVisible()
                      && as.toolbarMain.contains(QStringLiteral("snapshot")) && !as.toolbarFormat.contains(QStringLiteral("fontCombo")),
                  QStringLiteral("format bar without the font list; the other controls stay and the choice is stored"));
            // the controls still work after a rebuild
            binder->select({item}, true);
            settle(200);
            TextEditor *ed = pane0->currentEditor();
            if (ed)
                ed->setFocus();
            settle(100);
            if (ed && styleCombo) {
                const int idx = styleCombo->findData(QStringLiteral("Centered"));
                styleCombo->setCurrentIndex(idx);
                emit styleCombo->activated(idx);
                settle(100);
            }
            const bool comboWorks = ed && ed->paragraphStyleAtCursor() == QLatin1String("Centered");
            if (ed)
                ed->applyStyle(QString());
            check(comboWorks, QStringLiteral("the style list of the rebuilt format bar applies styles"));
            shot(&w, QStringLiteral("47-own-toolbars"));

            byKey(&w, QStringLiteral("showToolbar"))->setChecked(false);
            settle(100);
            const bool hidden = !mainBar->isVisible() && formatBar->isVisible();
            byKey(&w, QStringLiteral("showToolbar"))->setChecked(true);
            td.setBar(toolbars::Bar::Main);
            td.resetToStandard();
            td.setBar(toolbars::Bar::Format);
            td.resetToStandard();
            td.findChild<QComboBox *>(QStringLiteral("toolbarIconSize"))->setCurrentIndex(1);
            td.findChild<QComboBox *>(QStringLiteral("toolbarStyle"))->setCurrentIndex(0);
            td.accept();
            settle(300);
            check(hidden && mainBar->isVisible() && barKeys(mainBar) == standardKeys && as.toolbarMain.isEmpty() && as.toolbarFormat.isEmpty()
                      && fontCombo->isVisible() && mainBar->iconSize() == QSize(22, 22),
                  QStringLiteral("View > Toolbars hides the toolbar; Standard Set brings both bars back"));
        }

        // ---- layouts
        {
            const QList<layouts::Layout> list = layouts::available();
            QStringList names;
            for (const layouts::Layout &l : list)
                names << l.name;
            check(list.size() == 7 && names.first() == QLatin1String("Default") && names.contains(QStringLiteral("Editor Only"))
                      && names.contains(QStringLiteral("Outliner and Editor")) && names.contains(QStringLiteral("Corkboard and Editor")),
                  QStringLiteral("seven built-in layouts"), names.join(QLatin1String(", ")));

            binder->select({item}, true);
            settle(200);
            w.applyLayoutById(QStringLiteral("editor-only"));
            settle(300);
            check(!binder->isVisible() && !inspector->isVisible() && !pane1->isVisible() && pane0->mode() == ViewMode::Single
                      && !byKey(&w, QStringLiteral("showBinder"))->isChecked() && mainBar->isVisible(),
                  QStringLiteral("layout Editor Only: just the text and the bars"));
            shot(&w, QStringLiteral("48-layout-editor-only"));

            // outliner left, text right; the binder feeds the outliner, the outliner feeds the text
            w.applyLayoutById(QStringLiteral("default"));
            binder->select({folder}, true);
            settle(200);
            w.applyLayoutById(QStringLiteral("outliner-editor"));
            settle(400);
            const bool shape = binder->isVisible() && !inspector->isVisible() && pane1->isVisible()
                               && editorSplit->orientation() == Qt::Horizontal && pane0->mode() == ViewMode::Outliner
                               && pane0->selectionAffectsOther() && w.binderAffects() == QLatin1String("first")
                               && pane0->width() < pane1->width();
            check(shape, QStringLiteral("layout Outliner and Editor: binder, outliner on the left, text on the right"),
                  QStringLiteral("%1 / %2").arg(pane0->width()).arg(pane1->width()));
            {
                // a click on the row "Counting Ships"
                QAbstractItemModel *om = pane0->outliner()->model();
                const QModelIndexList rows = om->match(om->index(0, 0), Qt::DisplayRole, item->title, 1,
                                                       Qt::MatchRecursive | Qt::MatchExactly);
                if (!rows.isEmpty())
                    pane0->outliner()->selectionModel()->select(rows.first(),
                                                                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            }
            settle(300);
            const bool rowOpens = pane1->items() == QList<BinderItem *>({item}) && pane1->mode() == ViewMode::Single;
            pane1->focusEditor();
            settle(150);
            binder->select({folder2}, true);
            settle(300);
            const bool binderFeeds = pane0->items() == QList<BinderItem *>({folder2}) && pane0->mode() == ViewMode::Outliner
                                     && pane1->items() == QList<BinderItem *>({item});
            check(rowOpens && binderFeeds && pane0->groupMode() == ViewMode::Outliner && pane1->groupMode() == ViewMode::Combined,
                  QStringLiteral("a row of the outliner opens on the right; the binder loads into the outliner; each editor keeps its view for groups"),
                  QStringLiteral("%1 %2").arg(rowOpens).arg(binderFeeds));
            shot(&w, QStringLiteral("49-layout-outliner-editor"));

            binder->select({folder}, true);
            settle(200);
            w.applyLayoutById(QStringLiteral("corkboard-editor"));
            settle(400);
            check(editorSplit->orientation() == Qt::Vertical && pane0->mode() == ViewMode::Corkboard && pane1->isVisible()
                      && pane0->height() < pane1->height() && pane0->selectionAffectsOther(),
                  QStringLiteral("layout Corkboard and Editor: corkboard above, text below"));
            shot(&w, QStringLiteral("50-layout-corkboard-editor"));

            // a layout of one's own
            byKey(&w, QStringLiteral("corkFreeform"))->trigger(); // acts on the active editor ...
            settle(100);
            const bool freeformNeedsCork = pane0->corkMode() == int(Corkboard::Mode::Grid);
            pane0->setCorkMode(int(Corkboard::Mode::Freeform));
            byKey(&w, QStringLiteral("showInspector"))->setChecked(true);
            byKey(&w, QStringLiteral("inspect2"))->trigger();
            byKey(&w, QStringLiteral("showFormatBar"))->setChecked(false);
            settle(300);
            const QVariantMap mine = w.captureLayout(true);
            const QVariantMap paneState = mine.value(QStringLiteral("panes")).toList().value(0).toMap();
            check(freeformNeedsCork && mine.value(QStringLiteral("split")).toString() == QLatin1String("horizontal")
                      && mine.value(QStringLiteral("inspector")).toBool() && mine.value(QStringLiteral("inspectorTab")).toInt() == 2
                      && !mine.value(QStringLiteral("formatBar")).toBool() && paneState.value(QStringLiteral("view")).toString() == QLatin1String("corkboard")
                      && paneState.value(QStringLiteral("cork")).toString() == QLatin1String("freeform")
                      && mine.value(QStringLiteral("binderAffects")).toString() == QLatin1String("first")
                      && mine.value(QStringLiteral("window")).toMap().value(QStringLiteral("width")).toInt() == w.width()
                      && !w.captureLayout(false).contains(QStringLiteral("window")),
                  QStringLiteral("the window as it is can be described as a layout (window size only when asked for)"));

            LayoutsDialog ld([&w](bool size) { return w.captureLayout(size); }, [&w](const QVariantMap &st) { w.applyLayout(st); }, &w);
            ld.show();
            settle(200);
            auto *llist = ld.findChild<QListWidget *>(QStringLiteral("layoutList"));
            auto *ldel = ld.findChild<QPushButton *>(QStringLiteral("layoutDelete"));
            auto *lren = ld.findChild<QPushButton *>(QStringLiteral("layoutRename"));
            auto *lupd = ld.findChild<QPushButton *>(QStringLiteral("layoutUpdate"));
            QString lerr;
            const bool builtInLocked = llist && llist->count() == 7 && !ldel->isEnabled() && !lren->isEnabled() && !lupd->isEnabled()
                                       && !layouts::removeUser(QStringLiteral("default"), &lerr) && !lerr.isEmpty();
            const bool clash = ld.saveCurrentAs(QStringLiteral("editor only"), false, &lerr).isEmpty() && !lerr.isEmpty();
            const QString myId = ld.saveCurrentAs(QStringLiteral("Research Desk"), false, &lerr);
            check(builtInLocked && clash && myId == QLatin1String("user:research-desk") && llist->count() == 8 && ld.selectedId() == myId
                      && ldel->isEnabled() && QFile::exists(layouts::userDir() + QLatin1String("/research-desk.json")),
                  QStringLiteral("Manage Layouts: built-in layouts are fixed; Save Current adds a layout of one's own"), lerr);
            shot(&ld, QStringLiteral("51-manage-layouts"));
            QAction *mineAction = menuPath(&w, {QStringLiteral("Window"), QStringLiteral("Layouts"), QStringLiteral("Research Desk")});
            check(mineAction && mineAction == byKey(&w, QStringLiteral("layout:") + myId) && commands::find(QStringLiteral("layout:") + myId),
                  QStringLiteral("the new layout is in Window > Layouts and in the list of commands"));

            ld.select(QStringLiteral("default"));
            ld.useSelected();
            settle(300);
            const bool defaultAgain = !pane1->isVisible() && inspector->isVisible() && formatBar->isVisible()
                                      && w.binderAffects() == QLatin1String("current") && !pane0->selectionAffectsOther();
            binder->select({folder}, true);
            settle(200);
            if (mineAction)
                mineAction->trigger();
            settle(400);
            QVariantMap again = w.captureLayout(false);
            QVariantMap expected = mine;
            expected.remove(QStringLiteral("window"));
            auto close = [&](const QString &key) {
                return qAbs(again.value(key).toDouble() - expected.value(key).toDouble()) < 0.03;
            };
            bool same = close(QStringLiteral("binderShare")) && close(QStringLiteral("inspectorShare")) && close(QStringLiteral("splitShare"));
            for (const QString &key : {QStringLiteral("binderShare"), QStringLiteral("inspectorShare"), QStringLiteral("splitShare")}) {
                expected.remove(key);
                again.remove(key);
            }
            same = same && again == expected;
            check(defaultAgain && same && inspector->currentTab() == 2 && !formatBar->isVisible()
                      && pane0->corkMode() == int(Corkboard::Mode::Freeform),
                  QStringLiteral("Default puts the window back; the own layout restores split, views, inspector tab and widths"),
                  QStringLiteral("%1 %2").arg(defaultAgain).arg(same));

            // rename, update, export, import, delete
            ld.select(myId);
            const bool renamed = ld.renameSelected(QStringLiteral("Corkboard Desk"), &lerr) && ld.selectedId() == myId
                                 && byKey(&w, QStringLiteral("layout:") + myId)->text() == QLatin1String("Corkboard Desk")
                                 && !ld.renameSelected(QStringLiteral("Default"), &lerr);
            byKey(&w, QStringLiteral("showFormatBar"))->setChecked(true);
            settle(100);
            layouts::Layout stored;
            const bool updated = ld.updateSelected(&lerr) && layouts::load(myId, &stored) && stored.state.value(QStringLiteral("formatBar")).toBool()
                                 && stored.name == QLatin1String("Corkboard Desk");
            const QString exportFile = projectsTmp.path() + QLatin1String("/desk.qblayout");
            const bool exportedLayout = layouts::exportLayout(myId, exportFile, &lerr);
            const QString importedId = layouts::importLayout(exportFile, &lerr);
            layouts::Layout importedLayout;
            const bool importedOk = !importedId.isEmpty() && layouts::load(importedId, &importedLayout)
                                    && importedLayout.name == QLatin1String("Corkboard Desk 2") && importedLayout.state == stored.state
                                    && layouts::importLayout(projectFile, &lerr).isEmpty();
            ld.select(myId);
            const bool removedLayout = ld.removeSelected(&lerr) && layouts::removeUser(importedId, &lerr) && layouts::available().size() == 7
                                       && !w.findChild<QAction *>(QStringLiteral("layout:") + myId);
            check(renamed && updated && exportedLayout && importedOk && removedLayout,
                  QStringLiteral("own layouts: rename, update, export, import under a free name, delete"),
                  QStringLiteral("%1 %2 %3 %4 %5 %6").arg(renamed).arg(updated).arg(exportedLayout).arg(importedOk).arg(removedLayout).arg(lerr));
            ld.close();
            w.applyLayoutById(QStringLiteral("default"));
            settle(300);
        }

        // ---- the new commands of the menus
        {
            // Navigate > Binder Selection Affects
            trigger(&w, QStringLiteral("Split Vertically"));
            settle(300);
            binder->select({item}, true);
            settle(200);
            pane0->focusEditor();
            settle(100);
            byKey(&w, QStringLiteral("binderAffects_second"))->trigger();
            binder->select({folder2}, true);
            settle(300);
            const bool second = pane1->items() == QList<BinderItem *>({folder2}) && pane0->items() == QList<BinderItem *>({item});
            byKey(&w, QStringLiteral("binderAffects_current"))->trigger();
            trigger(&w, QStringLiteral("No Split"));
            settle(200);
            check(second && w.binderAffects() == QLatin1String("current"),
                  QStringLiteral("Navigate > Binder Selection Affects > Right or Bottom Editor loads binder clicks there"));

            // View > Corkboard Options and Outliner Options act on the editor in front
            binder->select({folder}, true);
            settle(200);
            pane0->setCorkMode(int(Corkboard::Mode::Grid));
            setView(&w, QStringLiteral("viewCork"));
            QAction *freeform = byKey(&w, QStringLiteral("corkFreeform"));
            const bool offered = freeform->isEnabled() && !freeform->isChecked();
            freeform->trigger();
            settle(200);
            const bool freeOn = pane0->corkMode() == int(Corkboard::Mode::Freeform) && freeform->isChecked()
                              && byKey(&w, QStringLiteral("corkCommit"))->isEnabled();
            byKey(&w, QStringLiteral("corkLabels"))->trigger();
            settle(200);
            const bool threads = pane0->corkMode() == int(Corkboard::Mode::LabelThreads) && !freeform->isChecked();
            byKey(&w, QStringLiteral("corkLabels"))->trigger();
            settle(100);
            setView(&w, QStringLiteral("viewOutline"));
            QMenu *outl = w.findChild<QMenu *>(QStringLiteral("outlinerOptionsMenu"));
            int columns = 0;
            if (outl) {
                emit outl->aboutToShow();
                for (QAction *a : outl->actions())
                    columns += a->isCheckable() && a->objectName().isEmpty();
            }
            QAction *syn = byKey(&w, QStringLiteral("outlinerSynopsis"));
            const bool synOn = syn->isEnabled() && syn->isChecked() && !freeform->isEnabled();
            syn->trigger();
            settle(100);
            const bool synOff = !pane0->outliner()->showSynopsis() && !syn->isChecked();
            syn->trigger();
            check(offered && freeOn && threads && pane0->corkMode() == int(Corkboard::Mode::Grid) && columns >= 5 && synOn && synOff,
                  QStringLiteral("View > Corkboard Options (Freeform, Arrange by Label) and Outliner Options (columns, synopses)"),
                  QStringLiteral("%1 %2 %3 %4 %5 %6").arg(offered).arg(freeOn).arg(threads).arg(columns).arg(synOn).arg(synOff));
            setView(&w, QStringLiteral("viewText"));

            // Navigate > Inspect
            byKey(&w, QStringLiteral("showInspector"))->setChecked(false);
            byKey(&w, QStringLiteral("inspect3"))->trigger();
            settle(100);
            check(inspector->isVisible() && inspector->currentTab() == int(Inspector::Snapshots),
                  QStringLiteral("Navigate > Inspect > Snapshots opens the inspector on that tab"));
            inspector->showTab(Inspector::Notes);

            // Navigate > Open Quick Reference, Window > Float Quick Reference Panels
            QMenu *qrMenu = w.findChild<QMenu *>(QStringLiteral("openQuickRefMenu"));
            QAction *entry = nullptr;
            if (qrMenu) {
                emit qrMenu->aboutToShow();
                std::function<void(QMenu *)> search = [&](QMenu *m) {
                    for (QAction *a : m->actions()) {
                        if (a->menu())
                            search(a->menu());
                        else if (a->text() == QLatin1String("Morning Fog"))
                            entry = a;
                    }
                };
                search(qrMenu);
            }
            if (entry)
                entry->trigger();
            settle(300);
            auto quickRefs = [] {
                QList<QuickRefWindow *> out;
                for (QWidget *tl : QApplication::topLevelWidgets())
                    if (auto *qr = qobject_cast<QuickRefWindow *>(tl))
                        if (qr->isVisible())
                            out << qr;
                return out;
            };
            const QList<QuickRefWindow *> open = quickRefs();
            const bool opened = entry && open.size() == 1 && open.first()->item()->title == QLatin1String("Morning Fog")
                                && !open.first()->isFloating();
            byKey(&w, QStringLiteral("floatQuickRef"))->trigger();
            settle(300);
            const bool floats = opened && open.first()->isFloating() && open.first()->isVisible() && as.floatQuickRef;
            QMenu *windowMenu = w.findChild<QMenu *>(QStringLiteral("windowMenu"));
            bool listed = false;
            if (windowMenu) {
                emit windowMenu->aboutToShow();
                for (QAction *a : windowMenu->actions())
                    listed = listed || a->text().startsWith(QLatin1String("Morning Fog"));
            }
            byKey(&w, QStringLiteral("floatQuickRef"))->trigger();
            settle(200);
            const bool sinks = opened && !open.first()->isFloating();
            byKey(&w, QStringLiteral("closeQuickRefs"))->trigger();
            settle(300);
            check(opened && floats && listed && sinks && quickRefs().isEmpty() && !as.floatQuickRef,
                  QStringLiteral("Navigate > Open Quick Reference opens a document; Window floats, lists and closes the panels"),
                  QStringLiteral("%1 %2 %3 %4").arg(opened).arg(floats).arg(listed).arg(sinks));
        }
        w.applyLayoutById(QStringLiteral("default"));
        settle(200);
    }
    w.closeProject();
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
