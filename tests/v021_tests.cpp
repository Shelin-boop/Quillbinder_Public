// Version 0.21: File > Options in Scrivener's layout (General, Editing, Behaviors,
// Appearance, Corrections, Keyboard, Sharing, Backup and Extras) with options that
// work, zooming with Ctrl + mouse wheel everywhere (the inspector with a zoom of its
// own), the shared color palette, the mind map's right-click menus (colors, shapes,
// icons on notes, arrows, colors and icons of single lines) and the Compile Overview
// window (font override, replacements, object sheets as back matter).
// Saves screenshots into the folder given as first argument.
// Exit code = number of failed checks.
//
//   QT_QPA_PLATFORM=offscreen ./qb_v021_tests /tmp/shots
#include "AppInfo.h"
#include "compile/CompileFormat.h"
#include "compile/Compiler.h"
#include "compile/PluginManager.h"
#include "core/BinderModel.h"
#include "core/IconCatalog.h"
#include "core/DocumentStore.h"
#include "core/MindMap.h"
#include "core/Objects.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "text/TextUtil.h"
#include "core/Backup.h"
#include "core/CrashGuard.h"
#include "core/Snapshots.h"
#include "import/FileImporter.h"
#include "text/AutoCorrect.h"
#include "text/DisplayFormat.h"
#include "text/TextProps.h"
#include "ui/Corkboard.h"
#include "ui/CrashDialogs.h"
#include "ui/Outliner.h"
#include "ui/Windows.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/ColorPalette.h"
#include "ui/CompileDialog.h"
#include "ui/EditorPane.h"
#include "ui/Inspector.h"
#include "ui/MainWindow.h"
#include "ui/MindMapUi.h"
#include "ui/MindMapController.h"
#include "ui/MindMapDoc.h"
#include "ui/MindMapScene.h"
#include "ui/MindMapView.h"
#include "ui/OptionsDialog.h"
#include "ui/SpellChecker.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"
#include "ui/Zoom.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextLayout>
#include <QStyleFactory>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cstdio>
#include <functional>

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
static void info(const QString &s)
{
    std::printf("      %s\n", qPrintable(s));
    std::fflush(stdout);
}
static void section(const QString &s)
{
    std::printf("\n== %s\n", qPrintable(s));
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
// answers the next message box (by its object name, "" = any) with the button `button`
// (its object name or its text); `seen` gets the text of the box
static void answerBox(int delayMs, const QString &name, const QString &button, QString *seen = nullptr, bool *found = nullptr)
{
    QTimer::singleShot(delayMs, qApp, [=] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box || (!name.isEmpty() && box->objectName() != name)) {
            info(QStringLiteral("(message box %1 did not open; %2 is open)")
                     .arg(name, QApplication::activeModalWidget() ? QApplication::activeModalWidget()->objectName() : QStringLiteral("nothing")));
            if (QWidget *m = QApplication::activeModalWidget())
                m->close();
            return;
        }
        if (seen)
            *seen = box->text();
        if (found)
            *found = true;
        for (QAbstractButton *b : box->buttons())
            if (b->objectName() == button || b->text().remove(QLatin1Char('&')) == button) {
                b->click();
                return;
            }
        box->close();
    });
}

static void setupApp(const QString &name)
{
    QCoreApplication::setOrganizationName(name);
    QCoreApplication::setApplicationName(name);
}
static void styleApp()
{
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
    as.backupOnOpen = false;
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();
}


static std::unique_ptr<Project> novelProject(const QString &dir, const QString &title, QString *err)
{
    templates::Info novel;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("novel"))
            novel = t;
    return templates::createProject(novel, dir, title, err);
}

static QAction *byKey(QWidget *w, const QString &name)
{
    for (QAction *a : w->findChildren<QAction *>())
        if (a->objectName() == name)
            return a;
    return nullptr;
}


static EditorPane *firstPane(MainWindow &w)
{
    const auto panes = w.findChildren<EditorPane *>();
    for (EditorPane *p : panes)
        if (p->isVisible())
            return p;
    return panes.isEmpty() ? nullptr : panes.first();
}

static void setText(Project *p, BinderItem *it, const QString &text)
{
    QTextDocument *d = p->docs()->text(it->id);
    QTextCursor c(d);
    c.select(QTextCursor::Document);
    c.insertText(text);
}

static QString plain(Project *p, BinderItem *it)
{
    return p->docs()->text(it->id)->toPlainText();
}

// a text editor with the given text, cursor at the end, ready for typing
static TextEditor *editorWith(MainWindow &w, BinderItem *it, const QString &text)
{
    Project *p = w.project();
    setText(p, it, text);
    w.findChild<BinderPanel *>()->select({it}, true);
    settle(200);
    EditorPane *pane = firstPane(w);
    TextEditor *e = pane ? pane->currentEditor() : nullptr;
    if (e) {
        e->setFocus();
        QTextCursor c = e->textCursor();
        c.movePosition(QTextCursor::End);
        e->setTextCursor(c);
    }
    return e;
}

static void wheel(QWidget *target, int steps, Qt::KeyboardModifiers mods = Qt::ControlModifier)
{
    const QPoint pos = target->rect().center();
    QWheelEvent ev(QPointF(pos), QPointF(target->mapToGlobal(pos)), QPoint(), QPoint(0, 120 * steps), Qt::NoButton, mods,
                   Qt::NoScrollPhase, false);
    QApplication::sendEvent(target, &ev);
    settle(60);
}

// ------------------------------------------------------------------ File > Options

static void optionsChecks()
{
    section(QStringLiteral("File > Options in Scrivener's layout"));
    QTemporaryDir dir;
    QString err;
    auto proj = novelProject(dir.path(), QStringLiteral("Options"), &err);
    check(proj != nullptr, QStringLiteral("create project"), err);
    if (!proj)
        return;
    MainWindow w;
    w.resize(1200, 800);
    w.show();
    w.adoptProject(std::move(proj));
    settle(300);

    OptionsDialog od(&w, 0);
    od.resize(900, 640);
    od.show();
    settle(200);
    auto *icons = od.findChild<QListWidget *>(QStringLiteral("optionPages"));
    QStringList names;
    for (int i = 0; icons && i < icons->count(); ++i)
        names << icons->item(i)->text();
    check(names == QStringList({QStringLiteral("General"), QStringLiteral("Editing"), QStringLiteral("Behaviors"), QStringLiteral("Appearance"),
                                QStringLiteral("Corrections"), QStringLiteral("Keyboard"), QStringLiteral("Sharing"), QStringLiteral("Backup"),
                                QStringLiteral("Extras")}),
          QStringLiteral("the icon row has Scrivener's eight sections and Extras"), names.join(QLatin1String(", ")));
    const QStringList general = od.pageNames(OptionsDialog::SGeneral);
    check(general.mid(0, 3) == QStringList({QStringLiteral("Startup"), QStringLiteral("Saving"), QStringLiteral("Author Information")})
              && general.contains(QStringLiteral("Separators")) && general.contains(QStringLiteral("Warnings")),
          QStringLiteral("General: Startup, Saving, Author Information, ..., Separators, Warnings"), general.join(QLatin1String(", ")));
    check(od.pageNames(OptionsDialog::SEditing) == QStringList({QStringLiteral("Options"), QStringLiteral("Formatting"), QStringLiteral("Revisions")}),
          QStringLiteral("Editing: the tabs Options, Formatting, Revisions"), od.pageNames(OptionsDialog::SEditing).join(QLatin1String(", ")));
    const QStringList beh = od.pageNames(OptionsDialog::SBehaviors);
    check(beh.contains(QStringLiteral("Composition Mode")) && beh.contains(QStringLiteral("Return Key")) && beh.contains(QStringLiteral("Playback"))
              && beh.contains(QStringLiteral("Snapshots")),
          QStringLiteral("Behaviors: Composition Mode ... Return Key, Snapshots"), beh.join(QLatin1String(", ")));
    check(od.pageNames(OptionsDialog::SAppearance).size() == 12, QStringLiteral("Appearance: twelve parts of the window"),
          od.pageNames(OptionsDialog::SAppearance).join(QLatin1String(", ")));
    check(od.pageNames(OptionsDialog::SCorrections) == QStringList({QStringLiteral("Corrections"), QStringLiteral("Spelling")}),
          QStringLiteral("Corrections: Corrections and Spelling"));
    check(od.pageNames(OptionsDialog::SSharing) == QStringList({QStringLiteral("Import"), QStringLiteral("Export"), QStringLiteral("Conversion")}),
          QStringLiteral("Sharing: Import, Export, Conversion"));
    check(od.pageNames(OptionsDialog::SExtras)
              == QStringList({QStringLiteral("Mind Map"), QStringLiteral("Objects"), QStringLiteral("Add-ons and Plugins"), QStringLiteral("Crash Reports")}),
          QStringLiteral("Extras: Mind Map, Objects, Add-ons and Plugins, Crash Reports"), od.pageNames(OptionsDialog::SExtras).join(QLatin1String(", ")));

    // every page, for the screenshots
    int n = 0;
    for (int s = 0; s < OptionsDialog::SectionCount; ++s) {
        const QStringList pages = od.pageNames(s);
        if (pages.isEmpty()) {
            od.showSection(s);
            shot(&od, QStringLiteral("01-options-%1").arg(++n, 2, 10, QLatin1Char('0')));
            continue;
        }
        for (const QString &p : pages) {
            od.showSection(s, p);
            QString safe = p;
            safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z]+")), QStringLiteral("-"));
            shot(&od, QStringLiteral("01-options-%1-%2").arg(++n, 2, 10, QLatin1Char('0')).arg(safe.toLower()));
        }
    }
    od.reject();

    // what is set here is stored with OK, and shown again
    AppSettings &as = AppSettings::instance();
    as.recentProjects = {QStringLiteral("/tmp/a.qbproj"), QStringLiteral("/tmp/b.qbproj")};
    as.save();
    {
        OptionsDialog d(&w, 0);
        d.show();
        settle(100);
        d.findChild<QSpinBox *>(QStringLiteral("optRecentCount"))->setValue(7);
        d.findChild<QCheckBox *>(QStringLiteral("optCapSentences"))->setChecked(true);
        auto *sep = d.findChild<QComboBox *>(QStringLiteral("optSepMerge"));
        sep->setCurrentIndex(sep->findData(QStringLiteral("custom")));
        d.findChild<QLineEdit *>(QStringLiteral("optSepMergeCustom"))->setText(QStringLiteral("~ ~ ~"));
        d.findChild<QSpinBox *>(QStringLiteral("optMmIconSize"))->setValue(24);
        d.findChild<QSpinBox *>(QStringLiteral("optFreezeSeconds"))->setValue(45);
        auto *quotes = d.findChild<QComboBox *>(QStringLiteral("optQuoteStyle"));
        quotes->setCurrentIndex(quotes->findData(QStringLiteral("de")));
        check(as.recentCount == 12 && !as.capitalizeSentences, QStringLiteral("nothing changes before OK"));
        d.accept();
    }
    check(as.recentCount == 7 && as.capitalizeSentences && as.sepMerge == QLatin1String("custom") && as.sepMergeCustom == QLatin1String("~ ~ ~")
              && MindMapPrefs::instance().iconSize == 24 && as.freezeSeconds == 45 && as.quoteStyle == QLatin1String("de"),
          QStringLiteral("OK stores the options of every section (General, Corrections, Extras)"));
    {
        OptionsDialog d(&w, 0);
        check(d.findChild<QSpinBox *>(QStringLiteral("optRecentCount"))->value() == 7
                  && d.findChild<QCheckBox *>(QStringLiteral("optCapSentences"))->isChecked()
                  && d.findChild<QLineEdit *>(QStringLiteral("optSepMergeCustom"))->isEnabled(),
              QStringLiteral("... and the window shows them when opened again"));
    }

    // Manage > Save Options to File, Load Options from File; Defaults
    const QString optFile = dir.filePath(QStringLiteral("my.qboptions"));
    {
        OptionsDialog d(&w, 0);
        QString e;
        check(d.saveOptionsToFile(optFile, &e) && QFile::exists(optFile), QStringLiteral("Manage > Save Options to File writes a file"), e);
        QSettings f(optFile, QSettings::IniFormat);
        check(!f.contains(QStringLiteral("app/recent")) && f.value(QStringLiteral("editing/capitalizeSentences")).isValid(),
              QStringLiteral("... with the options, without the recent projects of this computer"), f.allKeys().filter(QStringLiteral("app/")).join(QLatin1Char(',')));
    }
    {
        OptionsDialog d(&w, 0);
        d.restoreDefaults(false);
    }
    check(as.recentCount == 12 && !as.capitalizeSentences && as.sepMerge == QLatin1String("return") && MindMapPrefs::instance().iconSize == 16
              && as.recentProjects.size() == 2,
          QStringLiteral("Defaults: every option back to the standard, the recent projects kept"));
    {
        OptionsDialog d(&w, 0);
        d.show();
        bool seen = false;
        answerBox(300, QString(), QStringLiteral("OK"), nullptr, &seen);
        QString e;
        const bool ok = d.loadOptionsFromFile(optFile, &e);
        check(ok && seen && as.recentCount == 7 && as.capitalizeSentences && MindMapPrefs::instance().iconSize == 24,
              QStringLiteral("Manage > Load Options from File brings them back"), e);
        QString bad;
        check(!d.loadOptionsFromFile(dir.filePath(QStringLiteral("none.ini")), &bad) && !bad.isEmpty(),
              QStringLiteral("... and refuses a file that holds no options"));
    }
    {
        OptionsDialog d(&w, 0);
        d.restoreDefaults(false);
    }
    as.recentProjects.clear();
    as.save();
    w.closeProject();
}

// ------------------------------------------------------------------ General, Editing, Behaviors: what the options do

static void generalOptionChecks()
{
    section(QStringLiteral("Options at work: General"));
    AppSettings &as = AppSettings::instance();
    QTemporaryDir dir;
    QString err;
    auto proj = novelProject(dir.path(), QStringLiteral("General"), &err);
    if (!proj)
        return;
    Project *p = proj.get();
    BinderItem *a = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Alpha"));
    BinderItem *b = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Beta"));
    BinderItem *c = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Gamma"));
    setText(p, a, QStringLiteral("First."));
    setText(p, b, QStringLiteral("Second."));
    setText(p, c, QStringLiteral("Third."));
    MainWindow w;
    w.resize(1200, 800);
    w.show();
    w.activateWindow();
    w.adoptProject(std::move(proj));
    settle(300);
    auto *binder = w.findChild<BinderPanel *>();

    // Separators: Documents > Merge
    as.sepMerge = QStringLiteral("custom");
    as.sepMergeCustom = QStringLiteral("* * *");
    as.notifyChanged();
    binder->select({a, b}, true);
    settle(100);
    answerBox(200, QString(), QStringLiteral("Yes"));
    byKey(&w, QStringLiteral("merge"))->trigger();
    settle(300);
    check(plain(p, a) == QLatin1String("First.\n* * *\nSecond."), QStringLiteral("Separators: merged documents get the custom separator on a line of its own"),
          plain(p, a));
    as.sepMerge = QStringLiteral("empty");
    as.notifyChanged();
    binder->select({a, c}, true);
    settle(100);
    answerBox(200, QString(), QStringLiteral("Yes"));
    byKey(&w, QStringLiteral("merge"))->trigger();
    settle(300);
    check(plain(p, a).endsWith(QLatin1String("Second.\n\nThird.")), QStringLiteral("... or an empty line"), plain(p, a));
    {
        QTextDocument d;
        QTextCursor cur(&d);
        cur.insertText(QStringLiteral("One"));
        display::insertSeparator(cur, QStringLiteral("page"), QString(), p->formatting);
        cur.insertText(QStringLiteral("Two"));
        display::insertSeparator(cur, QStringLiteral("none"), QString(), p->formatting);
        cur.insertText(QStringLiteral("Three"));
        check(d.blockCount() == 2 && d.lastBlock().text() == QLatin1String("TwoThree")
                  && d.lastBlock().blockFormat().pageBreakPolicy() == QTextFormat::PageBreak_AlwaysBefore,
              QStringLiteral("... a page break, or nothing at all"));
    }
    as.sepMerge = QStringLiteral("return");
    as.notifyChanged();

    // Shared Templates
    {
        QTemporaryDir shared;
        QString e;
        check(templates::saveAsTemplate(p, QStringLiteral("Team Novel"), QStringLiteral("Fiction"), QStringLiteral("shared"), &e),
              QStringLiteral("(a template saved)"), e);
        const QString made = QDir(templates::userTemplatesDir()).filePath(QStringLiteral("Team Novel.zip"));
        QFile::rename(made, QDir(shared.path()).filePath(QStringLiteral("Team Novel.zip")));
        auto names = [] {
            QStringList n;
            for (const auto &t : templates::all())
                n << t.id;
            return n;
        };
        check(!names().contains(QStringLiteral("shared:Team Novel")), QStringLiteral("Shared Templates: without a folder, no shared templates"));
        as.sharedTemplatesDir = shared.path();
        as.notifyChanged();
        check(names().contains(QStringLiteral("shared:Team Novel")), QStringLiteral("... with one, its templates are offered for new projects"),
              names().join(QLatin1String(", ")));
        as.sharedTemplatesDir.clear();
        as.notifyChanged();
    }

    // Citations: Format > Bibliography/Citations
    QAction *cite = byKey(&w, QStringLiteral("citations"));
    check(cite && !cite->isEnabled(), QStringLiteral("Format > Bibliography/Citations is off while no program is chosen"));
    as.citationsApp = QStringLiteral("true");
    as.notifyChanged();
    check(cite && cite->isEnabled(), QStringLiteral("... and on with one (General > Citations)"));
    as.citationsApp.clear();
    as.notifyChanged();

    // Automatic Quit
    as.autoQuit = true;
    as.autoQuitMinutes = 5;
    as.notifyChanged();
    w.noteInput(QDateTime::currentDateTime().addSecs(-60));
    const bool notYet = !w.autoQuitDue();
    w.noteInput(QDateTime::currentDateTime().addSecs(-6 * 60));
    check(notYet && w.autoQuitDue(), QStringLiteral("Automatic Quit: due after the minutes without input, not before"));
    as.autoQuit = false;
    as.notifyChanged();
    check(!w.autoQuitDue(), QStringLiteral("... and never when it is off"));
    w.noteInput(QDateTime::currentDateTime());

    // Warnings: Reset All Warnings, Show Internal Log Console
    p->uiState[QStringLiteral("notesFolderDontAsk")] = true;
    {
        OptionsDialog d(&w, 0);
        d.showSection(OptionsDialog::SGeneral, QStringLiteral("Warnings"));
        d.findChild<QPushButton *>(QStringLiteral("optResetWarnings"))->click();
        settle(50);
    }
    check(!p->uiState.contains(QStringLiteral("notesFolderDontAsk")), QStringLiteral("Warnings: Reset All Warnings asks the questions again (also those of the project)"));
    crash::install();
    qWarning("v021 log console line");
    showLogConsole(&w);
    settle(700);
    QWidget *console = nullptr;
    for (QWidget *t : QApplication::topLevelWidgets())
        if (t->objectName() == QLatin1String("logConsole") && t->isVisible())
            console = t;
    auto *logText = console ? console->findChild<QPlainTextEdit *>(QStringLiteral("logText")) : nullptr;
    check(logText && logText->toPlainText().contains(QLatin1String("v021 log console line")),
          QStringLiteral("... Show Internal Log Console shows the program's messages"), logText ? logText->toPlainText().right(300) : QString());
    if (console) {
        shot(console, QStringLiteral("02-log-console"));
        qWarning("v021 a later line");
        settle(700);
        check(logText->toPlainText().contains(QLatin1String("v021 a later line")), QStringLiteral("... and new ones while it is open"));
        console->close();
    }

    // Saving: snapshots and a backup with File > Save
    as.snapshotOnManualSave = true;
    as.snapshotNotify = true;
    as.backupOnManualSave = true;
    QTemporaryDir backups;
    as.backupDir = backups.path();
    as.notifyChanged();
    // (Beta and Gamma went to the Trash with the merges)
    b = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Delta"));
    TextEditor *e = editorWith(w, b, QStringLiteral("Delta text"));
    if (e) {
        w.manualSave();   // (what was edited until now)
        const int before = snapshots::list(p, b->id).size();
        const int beforeA = snapshots::list(p, a->id).size();
        settle(1100); // (snapshots are named by the second they are taken in)
        QTest::keyClicks(e, QStringLiteral(" more"));
        settle(100);
        check(w.editedSinceManualSave().contains(b->id), QStringLiteral("(the edited text is noted)"));
        const int backupsBefore = QDir(backups.path()).entryList(QDir::Files).size();
        w.manualSave();
        settle(200);
        check(snapshots::list(p, b->id).size() == before + 1 && snapshots::list(p, a->id).size() == beforeA,
              QStringLiteral("General > Saving: File > Save takes snapshots of the texts changed since the last save, of no others"),
              QStringLiteral("b %1 -> %2, a %3 -> %4").arg(before).arg(snapshots::list(p, b->id).size()).arg(beforeA).arg(snapshots::list(p, a->id).size()));
        check(w.statusBar()->currentMessage().contains(QLatin1String("Snapshot")),
              QStringLiteral("Behaviors > Snapshots: a message says that snapshots were taken"), w.statusBar()->currentMessage());
        check(QDir(backups.path()).entryList(QDir::Files).size() == backupsBefore + 1,
              QStringLiteral("Backup: a backup with each manual save"), QDir(backups.path()).entryList(QDir::Files).join(QLatin1Char(',')));
    }
    as.snapshotOnManualSave = false;
    as.snapshotNotify = false;
    as.backupOnManualSave = false;
    as.backupDir.clear();
    as.notifyChanged();

    // Editing: custom date format
    if (e) {
        as.customDateFormat = QStringLiteral("dd.MM.yyyy");
        as.notifyChanged();
        setText(p, b, QString());
        byKey(&w, QStringLiteral("insertDate"))->trigger();
        check(plain(p, b) == QDate::currentDate().toString(QStringLiteral("dd.MM.yyyy")),
              QStringLiteral("Editing: Insert > Current Date in the custom format"), plain(p, b));
        as.customDateFormat.clear();
        as.notifyChanged();
    }

    // Appearance > General Interface: the full path in the title bar
    as.showFullPath = true;
    as.notifyChanged();
    check(w.windowTitle().contains(QDir::toNativeSeparators(p->folderPath())), QStringLiteral("Appearance: the project's full path in the title bar"),
          w.windowTitle());
    as.showFullPath = false;
    as.notifyChanged();
    check(!w.windowTitle().contains(QLatin1Char('/')), QStringLiteral("... or only its name"));

    // Editing: default text zoom of the editors
    {
        OptionsDialog d(&w, 0);
        d.showSection(OptionsDialog::SEditing, QStringLiteral("Options"));
        auto *area = d.findChild<QComboBox *>(QStringLiteral("optZoomArea"));
        auto *pct = d.findChild<QSpinBox *>(QStringLiteral("optZoomPercent"));
        area->setCurrentIndex(area->findData(QLatin1String(zoom::kEditor)));
        pct->setValue(150);
        d.accept();
    }
    settle(100);
    check(qFuzzyCompare(zoom::defaultFactor(QLatin1String(zoom::kEditor)), 1.5) && qFuzzyCompare(p->docs()->zoom(), 1.5),
          QStringLiteral("Editing: a new default text zoom reaches the editors at once"), QString::number(p->docs()->zoom()));
    p->docs()->setZoom(1.2);
    byKey(&w, QStringLiteral("zoomReset"))->trigger();
    check(qFuzzyCompare(p->docs()->zoom(), 1.5), QStringLiteral("... View > Reset Zoom goes back to it"));
    p->docs()->setZoom(1.3);
    QString saveErr;
    p->save(&saveErr);
    const QString path = p->folderPath();
    w.closeProject();
    as.reloadZoom = false;
    as.notifyChanged();
    w.openProject(path);
    settle(300);
    check(w.project() && qFuzzyCompare(w.project()->docs()->zoom(), 1.5),
          QStringLiteral("... without \"Reload the last zoom\" a project opens with the default zoom"));
    w.project()->docs()->setZoom(1.3);
    w.project()->save(&saveErr);
    w.closeProject();
    as.reloadZoom = true;
    as.notifyChanged();
    w.openProject(path);
    settle(300);
    check(w.project() && qFuzzyCompare(w.project()->docs()->zoom(), 1.3), QStringLiteral("... with it, with the zoom it had"));
    zoom::setDefaultFactor(QLatin1String(zoom::kEditor), 1.0);
    w.closeProject();
}

static void editingOptionChecks()
{
    section(QStringLiteral("Options at work: Editing, Corrections"));
    AppSettings &as = AppSettings::instance();
    QTemporaryDir dir;
    QString err;
    auto proj = novelProject(dir.path(), QStringLiteral("Editing"), &err);
    if (!proj)
        return;
    Project *p = proj.get();
    BinderItem *a = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Lighthouse"));
    BinderItem *b = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Harbor"));
    MainWindow w;
    w.resize(1200, 800);
    w.show();
    w.activateWindow();
    w.adoptProject(std::move(proj));
    settle(300);
    TextEditor *e = editorWith(w, a, QStringLiteral("abc"));
    if (!e) {
        check(false, QStringLiteral("(an editor)"));
        return;
    }

    // overtype with the Insert key
    QTest::keyClick(e, Qt::Key_Insert);
    const bool unchanged = !e->overwriteMode();
    as.overtypeKey = true;
    as.notifyChanged();
    QTest::keyClick(e, Qt::Key_Insert);
    const bool on = e->overwriteMode();
    QTextCursor cur = e->textCursor();
    cur.setPosition(0);
    e->setTextCursor(cur);
    QTest::keyClicks(e, QStringLiteral("X"));
    check(unchanged && on && plain(p, a) == QLatin1String("Xbc"), QStringLiteral("Editing: the Insert key switches overtype on (only when chosen)"), plain(p, a));
    QTest::keyClick(e, Qt::Key_Insert);
    check(!e->overwriteMode(), QStringLiteral("... and off"));
    as.overtypeKey = false;

    // block insertion point
    as.blockCursor = true;
    as.blockCursorWidth = 6;
    as.notifyChanged();
    check(e->cursorWidth() == 6, QStringLiteral("Editing: a block insertion point of the chosen width"));
    as.blockCursor = false;
    as.notifyChanged();
    check(e->cursorWidth() == 1, QStringLiteral("... or the thin one"));

    // footnotes and comments before punctuation
    setText(p, a, QStringLiteral("The light, the sea."));
    {
        QTextCursor s(e->document());
        s.setPosition(4);
        s.setPosition(10, QTextCursor::KeepAnchor); // "light,"
        e->setTextCursor(s);
        as.notesBeforePunctuation = true;
        const QString id = e->addLinkedNote(NoteKind::Comment, QStringLiteral("Which light?"));
        QTextCursor t(e->document());
        t.setPosition(10);
        const bool commaFree = t.charFormat().stringProperty(props::NoteId).isEmpty();
        t.setPosition(9);
        check(!id.isEmpty() && commaFree && t.charFormat().stringProperty(props::NoteId) == id,
              QStringLiteral("Editing: a comment on \"light,\" stops before the comma"));
        as.notesBeforePunctuation = false;
    }

    // inline annotations without color (display only)
    {
        QTextDocument d;
        QTextCursor c(&d);
        c.insertText(QStringLiteral("Text "));
        c.insertText(QStringLiteral("note"), text::noteFormat(NoteKind::InlineAnnotation, QStringLiteral("n1")));
        SpellChecker::attach(&d);
        auto foreground = [&d] {
            QColor shown;
            for (const QTextLayout::FormatRange &r : d.firstBlock().layout()->formats())
                if (r.start >= 5 && r.format.hasProperty(QTextFormat::ForegroundBrush))
                    shown = r.format.foreground().color();
            return shown;
        };
        SpellChecker::Display disp = SpellChecker::display();
        disp.plainAnnotations = true;
        SpellChecker::setDisplay(disp);
        SpellChecker::rehighlightAll();
        const QColor plainColor = foreground();
        disp.plainAnnotations = false;
        SpellChecker::setDisplay(disp);
        SpellChecker::rehighlightAll();
        check(plainColor == theme::color(theme::Role::EditorText) && !foreground().isValid(),
              QStringLiteral("Editing: inline annotations can be shown in the text color (the stored color stays)"));
    }

    // Corrections, as functions
    {
        auto run = [](const QString &text, const autocorrect::Options &o) {
            QTextDocument d;
            QTextCursor c(&d);
            c.insertText(text);
            autocorrect::afterWord(c, o);
            return d.toPlainText();
        };
        autocorrect::Options o;
        o.capSentences = true;
        o.capI = true;
        QStringList seen;
        for (const char *t : {"the sea. it", "it", "Ask Dr. smith", "e.g. this", "Really? yes", "so i", "and i’m", "He said. “well"})
            seen << run(QString::fromUtf8(t), o);
        check(run(QStringLiteral("the sea. it"), o) == QLatin1String("the sea. It") && run(QStringLiteral("it"), o) == QLatin1String("It")
                  && run(QStringLiteral("Ask Dr. smith"), o) == QLatin1String("Ask Dr. smith") && run(QStringLiteral("e.g. this"), o) == QLatin1String("e.g. this")
                  && run(QStringLiteral("Really? yes"), o) == QLatin1String("Really? Yes") && run(QStringLiteral("so i"), o) == QLatin1String("so I")
                  && run(QStringLiteral("and i’m"), o) == QStringLiteral("and I’m") && run(QStringLiteral("He said. “well"), o) == QStringLiteral("He said. “Well"),
              QStringLiteral("Corrections: capital letters at sentences (not after Dr. or e.g.) and for \"i\""), seen.join(QLatin1String(" | ")));
        autocorrect::Options s;
        s.substitutions = true;
        s.substitutionList = AppSettings::standardSubstitutions();
        check(run(QStringLiteral("(c)"), s) == QStringLiteral("©") && run(QStringLiteral("a->"), s) == QStringLiteral("a→"),
              QStringLiteral("... the substitutions of the list"), run(QStringLiteral("(c)"), s) + QLatin1Char(' ') + run(QStringLiteral("a->"), s));
        check(autocorrect::smartQuote(QLatin1Char('"'), QLatin1Char(' '), QStringLiteral("de")) == QChar(0x201E)
                  && autocorrect::smartQuote(QLatin1Char('"'), QLatin1Char('t'), QStringLiteral("de")) == QChar(0x201C)
                  && autocorrect::smartQuote(QLatin1Char('\''), QLatin1Char('n'), QStringLiteral("fr")) == QChar(0x2019)
                  && autocorrect::smartQuote(QLatin1Char('"'), QLatin1Char(' '), QStringLiteral("fr")) == QChar(0x00AB),
              QStringLiteral("... the quotes of the chosen style („…“, «…»), apostrophes inside words"));
    }

    // Corrections while typing
    as.capitalizeSentences = true;
    as.capitalizeI = true;
    as.detectUrls = true;
    as.substitutionsOn = true;
    as.substitutions = AppSettings::standardSubstitutions();
    as.quoteStyle = QStringLiteral("de");
    as.ellipsis = true;
    as.notifyChanged();
    e = editorWith(w, a, QString());
    QTest::keyClicks(e, QStringLiteral("the sea was calm. i saw it (c) \"there\" and so..."));
    QTest::keyClick(e, Qt::Key_Return);
    QTest::keyClicks(e, QStringLiteral("see www.example.org now"));
    settle(100);
    const QString typed = plain(p, a);
    check(typed.startsWith(QStringLiteral("The sea was calm. I saw it © „there“ and so…\n")),
          QStringLiteral("Corrections while typing: capitals, \"I\", (c), German quotes, the ellipsis"), typed);
    QString href;
    for (QTextBlock bl = e->document()->begin(); bl.isValid(); bl = bl.next())
        for (auto it = bl.begin(); !it.atEnd(); ++it)
            if (it.fragment().charFormat().isAnchor())
                href = it.fragment().charFormat().anchorHref() + QLatin1Char('|') + it.fragment().text();
    check(href == QLatin1String("http://www.example.org|www.example.org"), QStringLiteral("... a typed web address becomes a link"), href);
    as.capitalizeSentences = as.capitalizeI = as.detectUrls = as.substitutionsOn = as.ellipsis = false;
    as.quoteStyle = QStringLiteral("en");
    as.notifyChanged();
    e = editorWith(w, a, QString());
    QTest::keyClicks(e, QStringLiteral("the end. i \"x\""));
    check(plain(p, a) == QStringLiteral("the end. i “x”"), QStringLiteral("... and none of it when they are off (English quotes are the standard)"), plain(p, a));

    // [[document links]] and back-links
    as.detectDocLinks = true;
    as.backLinks = true;
    as.notifyChanged();
    e = editorWith(w, a, QStringLiteral("Go to "));
    QTest::keyClicks(e, QStringLiteral("[[harbor]] and [[New Place]]"));
    settle(100);
    QStringList links;
    for (QTextBlock bl = e->document()->begin(); bl.isValid(); bl = bl.next())
        for (auto it = bl.begin(); !it.atEnd(); ++it)
            if (it.fragment().charFormat().isAnchor())
                links << it.fragment().text() + QLatin1Char('=') + it.fragment().charFormat().anchorHref();
    BinderItem *made = nullptr;
    for (BinderItem *it : p->allItems())
        if (it->title == QLatin1String("New Place"))
            made = it;
    check(plain(p, a) == QLatin1String("Go to Harbor and New Place") && links.size() == 2
              && links.first() == QStringLiteral("Harbor=qb://") + b->id && made && made->parent == a->parent,
          QStringLiteral("Corrections: [[Title]] becomes a link to that document; a missing one is made next to the text"),
          plain(p, a) + QLatin1String(" / ") + links.join(QLatin1String(", ")));
    bool back = false;
    for (const Bookmark &bm : b->bookmarks)
        back = back || bm.target == QStringLiteral("qb://") + a->id;
    check(back, QStringLiteral("Behaviors > Document Links: the linked document gets a bookmark back"));
    as.detectDocLinks = false;
    as.backLinks = false;
    as.notifyChanged();

    // Spelling: the personal word list, words with numbers, the underline
    {
        SpellChecker &sc = SpellChecker::instance();
        const QStringList before = sc.personalWords();
        sc.addToPersonal(QStringLiteral("Quillwort"));
        const bool added = sc.personalWords().contains(QStringLiteral("Quillwort"));
        QStringList without = sc.personalWords();
        without.removeAll(QStringLiteral("Quillwort"));
        sc.setPersonalWords(without);
        check(added && !sc.personalWords().contains(QStringLiteral("Quillwort")), QStringLiteral("Spelling: the personal word list can be shown and words removed from it"));
        if (sc.isAvailable() || sc.setLanguage(QStringLiteral("en_US"))) {
            QTextDocument d;
            d.setPlainText(QStringLiteral("Route66 xqzt"));
            SpellChecker::attach(&d);
            SpellChecker::setEnabled(true);
            auto marked = [&d](int from) {
                for (const QTextLayout::FormatRange &r : d.firstBlock().layout()->formats())
                    if (r.start == from && r.format.underlineStyle() != QTextCharFormat::NoUnderline)
                        return int(r.format.underlineStyle());
                return -1;
            };
            SpellChecker::Display disp = SpellChecker::display();
            disp.ignoreNumbers = true;
            disp.underline = 1;
            SpellChecker::setDisplay(disp);
            SpellChecker::rehighlightAll();
            const bool ignored = marked(0) < 0 && marked(8) == int(QTextCharFormat::DotLine);
            disp.ignoreNumbers = false;
            disp.underline = 0;
            SpellChecker::setDisplay(disp);
            SpellChecker::rehighlightAll();
            check(ignored && marked(0) >= 0, QStringLiteral("... words with numbers are left out or checked; the underline style is the chosen one"));
            disp.ignoreNumbers = true;
            SpellChecker::setDisplay(disp);
            SpellChecker::setEnabled(false);
        } else {
            info(QStringLiteral("(no dictionary installed: spelling marks not checked)"));
        }
    }
    w.closeProject();
}

static void behaviorOptionChecks()
{
    section(QStringLiteral("Options at work: Behaviors, Sharing"));
    AppSettings &as = AppSettings::instance();
    QTemporaryDir dir;
    QString err;
    auto proj = novelProject(dir.path(), QStringLiteral("Behaviors"), &err);
    if (!proj)
        return;
    Project *p = proj.get();
    BinderItem *folder = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Act One"));
    BinderItem *s1 = p->model()->createItem(folder, -1, ItemType::Text, QStringLiteral("Scene 1"));
    p->model()->createItem(folder, -1, ItemType::Text, QStringLiteral("Scene 2"));
    BinderItem *parentText = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Letter"));
    p->model()->createItem(parentText, -1, ItemType::Text, QStringLiteral("Postscript"));
    setText(p, parentText, QStringLiteral("Dear reader."));
    MainWindow w;
    w.resize(1200, 800);
    w.show();
    w.activateWindow();
    w.adoptProject(std::move(proj));
    settle(300);
    auto *binder = w.findChild<BinderPanel *>();
    EditorPane *pane = firstPane(w);

    // corkboard: double click on the background, Return
    binder->select({folder}, true);
    settle(300);
    Corkboard *cork = pane ? pane->corkboard() : nullptr;
    if (pane && cork && pane->mode() == ViewMode::Corkboard) {
        const int before = folder->children.size();
        QTest::mouseDClick(cork->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(cork->viewport()->width() - 20, cork->viewport()->height() - 20));
        settle(300);
        check(folder->children.size() == before + 1, QStringLiteral("Behaviors > Double-Clicking: a double click on the corkboard makes a new card"));
        as.corkDoubleClickNew = false;
        as.notifyChanged();
        QTest::mouseDClick(cork->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(cork->viewport()->width() - 20, cork->viewport()->height() - 20));
        settle(300);
        check(folder->children.size() == before + 1, QStringLiteral("... not when switched off"));
        as.corkDoubleClickNew = true;
        as.returnCreatesItem = true;
        as.notifyChanged();
        binder->select({folder}, true);
        settle(300);
        cork->setFocus();
        cork->scene()->clearSelection();
        for (QGraphicsItem *gi : cork->scene()->items())
            if (gi->flags() & QGraphicsItem::ItemIsSelectable) {
                gi->setSelected(true);
                break;
            }
        const int n = folder->children.size();
        QTest::keyClick(cork, Qt::Key_Return);
        settle(300);
        check(folder->children.size() == n + 1, QStringLiteral("Behaviors > Return Key: Return on a card makes a new one"));
        as.returnCreatesItem = false;
        as.notifyChanged();
    } else {
        check(false, QStringLiteral("(the folder shows the corkboard)"));
    }

    // Folders & Files: a text with subdocuments
    binder->select({parentText}, true);
    settle(300);
    const bool group = pane && pane->mode() != ViewMode::Single;
    as.subdocsAsFolders = false;
    as.notifyChanged();
    binder->select({s1}, true);
    settle(100);
    binder->select({parentText}, true);
    settle(300);
    check(group && pane && pane->mode() == ViewMode::Single, QStringLiteral("Behaviors > Folders & Files: a text with subdocuments shows its own text when chosen"));
    as.subdocsAsFolders = true;
    as.notifyChanged();

    // Composition mode: Escape
    as.compositionEscCloses = false;
    as.notifyChanged();
    binder->select({s1}, true);
    settle(200);
    byKey(&w, QStringLiteral("composition"))->trigger();
    settle(400);
    QPointer<CompositionWindow> comp;
    for (QWidget *t : QApplication::topLevelWidgets())
        if (auto *cw = qobject_cast<CompositionWindow *>(t))
            if (cw->isVisible())
                comp = cw;
    if (comp) {
        QTest::keyClick(comp, Qt::Key_Escape);
        settle(200);
        const bool stays = comp && comp->isVisible();
        QTest::keyClick(comp, Qt::Key_F11);
        settle(300);
        check(stays && (!comp || !comp->isVisible()), QStringLiteral("Behaviors > Composition Mode: Escape leaves it open when chosen; F11 closes it"));
    } else {
        check(false, QStringLiteral("(composition mode opened)"));
    }
    as.compositionEscCloses = true;
    as.notifyChanged();

    // Sharing > Import: comments and footnotes, encodings, web pages, styles
    {
        QTextDocument d;
        QTextCursor c(&d);
        c.insertText(QStringLiteral("A "), QTextCharFormat());
        c.insertText(QStringLiteral("phrase"), text::noteFormat(NoteKind::Comment, QStringLiteral("c1"), QStringLiteral("Check it.")));
        c.insertText(QStringLiteral(" and the end"), QTextCharFormat());
        c.insertText(QStringLiteral("A source."), text::noteFormat(NoteKind::InlineFootnote, QStringLiteral("f1")));
        c.insertText(QStringLiteral("."), QTextCharFormat());
        const int n = text::convertNotes(&d, true, false, false);
        NoteKind atComment = NoteKind::None, atEnd = NoteKind::None;
        QString linkedText;
        for (auto it = d.firstBlock().begin(); !it.atEnd(); ++it) {
            const QTextCharFormat cf = it.fragment().charFormat();
            if (it.fragment().text() == QLatin1String("Check it."))
                atComment = NoteKind(cf.intProperty(props::NoteKind));
            if (it.fragment().text() == QLatin1String("end")) {
                atEnd = NoteKind(cf.intProperty(props::NoteKind));
                linkedText = cf.stringProperty(props::NoteText);
            }
        }
        check(n == 2 && atComment == NoteKind::InlineAnnotation && atEnd == NoteKind::LinkedFootnote && linkedText == QLatin1String("A source.")
                  && d.toPlainText() == QLatin1String("A phraseCheck it. and the end."),
              QStringLiteral("Sharing > Import: comments become inline annotations, inline footnotes linked ones (on the word before)"), d.toPlainText());
        const QByteArray cp = QByteArray("Caf\xe9 \x80 \x96", 8);
        check(importer::decodeText(cp, QStringLiteral("windows-1252")) == QStringLiteral("Café € –")
                  && importer::decodeText(cp, QStringLiteral("auto")) == QStringLiteral("Café € –")
                  && importer::decodeText(QByteArray("\xa4", 1), QStringLiteral("ISO-8859-15")) == QStringLiteral("€")
                  && importer::decodeText(QByteArray("\xa4", 1), QStringLiteral("ISO-8859-1")) == QStringLiteral("¤"),
              QStringLiteral("... plain text in the chosen encoding (automatic: UTF-8, else Windows Western)"));
        const QString html = dir.filePath(QStringLiteral("page.html"));
        QFile hf(html);
        hf.open(QIODevice::WriteOnly);
        hf.write("<html><body><h1>Head</h1><p>Body</p></body></html>");
        hf.close();
        const bool page = importer::classify(html) == importer::FileKind::WebPage;
        as.htmlAsText = true;
        as.notifyChanged();
        check(page && importer::classify(html) == importer::FileKind::Text, QStringLiteral("... HTML files as web pages or as texts"));
        as.htmlAsText = false;
        // an RTF file with a heading style
        const QString rtfFile = dir.filePath(QStringLiteral("styled.rtf"));
        QFile rf(rtfFile);
        rf.open(QIODevice::WriteOnly);
        rf.write("{\\rtf1\\ansi{\\stylesheet{\\s0 Normal;}{\\s1 Heading 1;}}\\pard\\s1 Big Title\\par\\pard\\s0 Body text with {\\b bold}.\\par}");
        rf.close();
        auto styleOf = [&](bool ignore) {
            as.importIgnoreStyles = ignore;
            as.notifyChanged();
            QTextDocument t;
            QString e2;
            importer::readTextFile(rtfFile, &t, p->formatting, false, &e2);
            bool bold = false;
            for (QTextBlock bl = t.begin(); bl.isValid(); bl = bl.next())
                for (auto it = bl.begin(); !it.atEnd(); ++it)
                    bold = bold || (it.fragment().text().contains(QLatin1String("bold")) && it.fragment().charFormat().fontWeight() >= QFont::Bold);
            return t.firstBlock().blockFormat().stringProperty(props::Style) + (bold ? QStringLiteral("+bold") : QString());
        };
        const QString kept = styleOf(false);
        const QString left = styleOf(true);
        check(!kept.startsWith(QLatin1Char('+')) && left == QLatin1String("+bold"),
              QStringLiteral("... the styles of imported files kept or left out (bold stays)"), kept + QLatin1String(" / ") + left);
        as.importIgnoreStyles = false;
        as.notifyChanged();
    }
    w.closeProject();
}

// ------------------------------------------------------------------ zoom

static void zoomChecks()
{
    section(QStringLiteral("Ctrl + mouse wheel zooms the area under the mouse"));
    QTemporaryDir dir;
    QString err;
    auto proj = novelProject(dir.path(), QStringLiteral("Zoom"), &err);
    if (!proj)
        return;
    Project *p = proj.get();
    BinderItem *folder = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Part"));
    BinderItem *a = p->model()->createItem(folder, -1, ItemType::Text, QStringLiteral("Morning"));
    p->model()->createItem(folder, -1, ItemType::Text, QStringLiteral("Evening"));
    setText(p, a, QStringLiteral("The harbor was quiet."));
    MainWindow w;
    w.resize(1300, 820);
    w.show();
    w.activateWindow();
    w.adoptProject(std::move(proj));
    settle(300);
    if (QAction *si = byKey(&w, QStringLiteral("showInspector")); si && !si->isChecked())
        si->trigger();
    auto *binder = w.findChild<BinderPanel *>();
    auto *insp = w.findChild<Inspector *>();
    binder->select({a}, true);
    settle(300);

    // the binder
    QWidget *tree = binder->tree()->viewport();
    const double f0 = binder->tree()->font().pointSizeF();
    wheel(tree, 2);
    check(qFuzzyCompare(zoom::factor(QLatin1String(zoom::kBinder)), 1.2) && binder->tree()->font().pointSizeF() > f0 * 1.15,
          QStringLiteral("over the binder: the binder's text grows (two notches: 120 %)"),
          QStringLiteral("%1, %2 -> %3 pt").arg(zoom::factor(QLatin1String(zoom::kBinder))).arg(f0).arg(binder->tree()->font().pointSizeF()));
    wheel(tree, 1, Qt::NoModifier);
    check(qFuzzyCompare(zoom::factor(QLatin1String(zoom::kBinder)), 1.2), QStringLiteral("... the wheel alone scrolls and does not zoom"));

    // the editor
    EditorPane *pane = firstPane(w);
    TextEditor *e = pane ? pane->currentEditor() : nullptr;
    const double z0 = p->docs()->zoom();
    if (e)
        wheel(e->viewport(), 1);
    check(e && qFuzzyCompare(p->docs()->zoom(), z0 + 0.1) && qFuzzyCompare(zoom::factor(QLatin1String(zoom::kBinder)), 1.2),
          QStringLiteral("over the editor: the text zoom of the editors, the binder stays"), QString::number(p->docs()->zoom()));

    // the inspector, its own zoom, also for the notes
    insp->showTab(Inspector::Notes);
    settle(200);
    TextEditor *notes = nullptr;
    for (TextEditor *t : insp->findChildren<TextEditor *>())
        if (t->isVisible() && t->isNotes())
            notes = t;
    if (notes)
        wheel(notes->viewport(), 3);
    auto *box = insp->findChild<QComboBox *>(QStringLiteral("inspectorZoom"));
    // (since 0.21.1 the notes are a part with a zoom of their own, see qb_v0211_tests)
    check(notes && qFuzzyCompare(zoom::factor(QLatin1String(zoom::kInspNotes)), 1.3) && qFuzzyCompare(p->docs()->notesZoom(), 1.3)
              && qFuzzyCompare(p->docs()->zoom(), z0 + 0.1),
          QStringLiteral("over the inspector's notes: their own zoom; the editors stay"), QString::number(p->docs()->notesZoom()));
    check(box && box->currentText().startsWith(QLatin1String("130")), QStringLiteral("... the zoom list of the inspector shows it"),
          box ? box->currentText() : QString());
    shot(&w, QStringLiteral("03-zoom-inspector-130"));
    if (box) {
        box->setCurrentIndex(box->findText(QStringLiteral("100 %")) >= 0 ? box->findText(QStringLiteral("100 %")) : 0);
        emit box->activated(box->currentIndex());
        settle(100);
    }
    check(qFuzzyCompare(zoom::factor(QLatin1String(zoom::kInspNotes)), 1.0) || (box && box->currentText().startsWith(QLatin1String("50"))),
          QStringLiteral("... and the list sets it"), box ? box->currentText() : QString());

    // one zoom per inspector tab (View > Zoom Inspector Tabs Separately); since 0.21.1 for
    // the tabs of one part (Metadata, Comments), the others have a zoom per part anyway
    zoom::setFactor(QLatin1String(zoom::kInspector), 1.0);
    byKey(&w, QStringLiteral("zoomInspectorPerTab"))->trigger();
    settle(100);
    insp->showTab(Inspector::Metadata);
    settle(100);
    QWidget *meta = nullptr;
    for (QListWidget *l : insp->findChildren<QListWidget *>())
        if (l->isVisible())
            meta = l->viewport();
    if (meta)
        wheel(meta, 2);
    const bool perMeta = qFuzzyCompare(zoom::factor(zoom::inspectorKey(QStringLiteral("metadata"))), 1.2)
                         && qFuzzyCompare(zoom::factor(QLatin1String(zoom::kInspector)), 1.0);
    insp->showTab(Inspector::Comments);
    settle(100);
    check(zoom::perTab() && perMeta && box && box->currentText().startsWith(QLatin1String("100")),
          QStringLiteral("each tab its own zoom when chosen: the metadata 120 %, the comments 100 %"), box ? box->currentText() : QString());
    byKey(&w, QStringLiteral("zoomInspectorPerTab"))->trigger();
    check(!zoom::perTab(), QStringLiteral("... and one for all again"));

    // the corkboard: the cards with their text
    binder->select({folder}, true);
    settle(300);
    if (pane && pane->mode() == ViewMode::Corkboard) {
        wheel(pane->corkboard()->viewport(), 2);
        check(qFuzzyCompare(zoom::factor(QLatin1String(zoom::kCorkboard)), 1.2) && qAbs(pane->corkboard()->transform().m11() - 1.2) < 0.01,
              QStringLiteral("over the corkboard: cards and their text grow together"));
        shot(&w, QStringLiteral("03-zoom-corkboard-120"));
    }

    // a window of its own (a tool window, a dialog without a zoom of its own)
    {
        QDialog d(&w);
        d.setObjectName(QStringLiteral("zoomTestWindow"));
        auto *l = new QVBoxLayout(&d);
        auto *label = new QLabel(QStringLiteral("Some text"), &d);
        l->addWidget(label);
        d.show();
        settle(100);
        const double before = label->font().pointSizeF();
        wheel(label, 2);
        check(label->font().pointSizeF() > before * 1.15 && qFuzzyCompare(zoom::factor(QStringLiteral("window/zoomTestWindow")), 1.2),
              QStringLiteral("over any other window: the whole window, remembered by its name"));
        d.close();
    }
    for (const char *k : {zoom::kBinder, zoom::kInspector, zoom::kCorkboard})
        zoom::reset(QLatin1String(k));
    zoom::reset(QStringLiteral("window/zoomTestWindow"));
    zoom::reset(QLatin1String(zoom::kInspNotes));
    zoom::reset(QStringLiteral("inspector/metadata"));
    w.closeProject();
}

// ------------------------------------------------------------------ mind map

static void mindMapChecks()
{
    section(QStringLiteral("Mind map: colors, shapes and icons of notes; arrows, colors and icons of lines"));
    QTemporaryDir dir;
    QString err;
    auto proj = novelProject(dir.path(), QStringLiteral("Map"), &err);
    if (!proj)
        return;
    Project *p = proj.get();
    BinderItem *item = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Plot"));
    MainWindow w;
    w.resize(1200, 800);
    w.show();
    w.adoptProject(std::move(proj));
    settle(300);
    MindMapDoc *doc = mindmaps::docFor(p, item->id);
    MindMapWindow *mw = MindMapWindow::open(doc, &w);
    settle(300);
    MindMapController *ctl = mw ? mw->controller() : nullptr;
    if (!ctl) {
        check(false, QStringLiteral("(the mind map window)"));
        return;
    }
    mindmap::Board &b = ctl->board();
    const int n1 = ctl->newNoteAt(QPointF(80, 80), Qt::NoModifier, QStringLiteral("Harbor"));
    const int n2 = ctl->newNoteAt(QPointF(420, 90), Qt::NoModifier, QStringLiteral("Lighthouse"));
    const int n3 = ctl->newNoteAt(QPointF(250, 320), Qt::NoModifier, QStringLiteral("Storm"));
    settle(100);
    MindMapScene *scene = doc->scene();
    scene->endEditing(); // (a new note is being edited)

    // colors from the shared palette: the project's labels, the six colors, Other...
    scene->selectIds({n1});
    QMenu fill;
    ctl->fillColorMenu(&fill, 1);
    QStringList names;
    for (QAction *a : fill.actions())
        names << a->text().remove(QLatin1Char('&'));
    const QList<palette::Entry> labels = palette::labels(p);
    bool hasLabels = !labels.isEmpty();
    for (const palette::Entry &l : labels)
        hasLabels = hasLabels && names.contains(l.name);
    check(hasLabels && names.contains(QStringLiteral("Red")) && names.contains(QStringLiteral("Blue")) && names.contains(QStringLiteral("Other..."))
              && names.contains(QStringLiteral("No Fill")),
          QStringLiteral("Fill Color: the label colors of the project, the six colors of the format bar, No Fill, Other..."), names.join(QLatin1String(", ")));
    for (QAction *a : fill.actions())
        if (a->text().remove(QLatin1Char('&')) == QLatin1String("Blue"))
            a->trigger();
    check(b.note(n1)->look.fill == QColor(31, 100, 200), QStringLiteral("... choosing Blue fills the note"));
    if (!labels.isEmpty()) {
        for (QAction *a : fill.actions())
            if (a->text().remove(QLatin1Char('&')) == labels.first().name)
                a->trigger();
        check(b.note(n1)->look.fill == labels.first().color, QStringLiteral("... a label's color, the same as in the binder"));
    }
    ctl->setColor(0, QColor(214, 39, 40));
    check(b.note(n1)->look.textColor == QColor(214, 39, 40), QStringLiteral("Text Color as well"));

    // the shape
    QMenu shapes;
    ctl->fillShapeMenu(&shapes);
    QStringList shapeNames;
    for (QAction *a : shapes.actions())
        shapeNames << a->text().remove(QLatin1Char('&'));
    check(shapeNames.contains(QStringLiteral("Rectangle")) && shapeNames.contains(QStringLiteral("Rounded Rectangle"))
              && shapeNames.contains(QStringLiteral("Cloud")),
          QStringLiteral("Shape: Rectangle, Rounded Rectangle, Cloud (and Jagged)"), shapeNames.join(QLatin1String(", ")));
    for (QAction *a : shapes.actions())
        if (a->text().remove(QLatin1Char('&')) == QLatin1String("Cloud"))
            a->trigger();
    check(b.note(n1)->look.borderStyle == mindmap::BorderStyle::Cloud && b.note(n1)->look.borderWeight > 0,
          QStringLiteral("... a cloud (with a border, so that it shows)"));

    // up to eight icons at the corners and the middles of the sides
    QStringList ids;
    for (const icons::CatalogEntry &ce : icons::catalog())
        if (ids.size() < 8)
            ids << ce.id;
    for (int i = 0; i < 8; ++i)
        ctl->setIconAt(i, ids.value(i));
    const mindmap::IconMap im = b.iconsOf(n1);
    check(im.size() == 8 && im.value(int(mindmap::IconPos::TopRight)) == ids.value(2) && im.value(int(mindmap::IconPos::Left)) == ids.value(7),
          QStringLiteral("Icons: eight places, each with an icon of the binder's set"));
    scene->selectIds({n1});
    QMenu ctx;
    ctl->fillContextMenu(&ctx);
    QMenu *iconsMenu = nullptr;
    for (QAction *a : ctx.actions())
        if (a->menu() && a->menu()->objectName() == QLatin1String("mm_menuIcons"))
            iconsMenu = a->menu();
    int places = 0;
    if (iconsMenu)
        for (QAction *a : iconsMenu->actions())
            if (a->menu() && a->menu()->objectName().startsWith(QLatin1String("mm_iconPlace")))
                ++places;
    QStringList ctxNames;
    for (QAction *a : ctx.actions())
        ctxNames << a->text() + (a->menu() ? QLatin1Char('>') + a->menu()->objectName() : QString());
    check(iconsMenu && places == 8, QStringLiteral("... the right-click menu of a note has Icons with the eight places"),
          QStringLiteral("%1 places; %2; editing %3").arg(places).arg(ctxNames.join(QLatin1String(", "))).arg(scene->editingNote() ? 1 : 0));
    ctl->setIconAt(int(mindmap::IconPos::Left), QString());
    check(b.iconsOf(n1).size() == 7, QStringLiteral("... and an icon can be taken off again"));

    // lines: arrows only of the line clicked
    b.connect(n1, n2);
    b.connect(n1, n3);
    scene->syncAll();
    QMenu lm;
    ctl->fillLineMenu(&lm, n1, n2);
    QAction *toB = nullptr;
    for (QAction *a : lm.actions())
        if (a->objectName() == QLatin1String("mm_lineArrowToB"))
            toB = a;
    if (toB)
        toB->trigger();
    check(toB && b.pointsTo(n1, n2) && !b.pointsTo(n2, n1) && !b.pointsTo(n1, n3) && !b.pointsTo(n3, n1),
          QStringLiteral("Lines: the right-click menu of a line sets its arrows, the other lines stay"));
    ctl->setLineArrows(n1, n2, true, true);
    check(b.pointsTo(n1, n2) && b.pointsTo(n2, n1), QStringLiteral("... arrows at both ends"));
    ctl->setLineColor(n1, n2, QColor(44, 160, 44));
    ctl->setLineIcon(n1, n2, ids.value(0));
    check(b.lineLook(n1, n2).color == QColor(44, 160, 44) && b.lineLook(n1, n2).icon == ids.value(0) && b.lineLook(n1, n3).isEmpty(),
          QStringLiteral("... a color and an icon in the middle of one line"));
    doc->undoStack()->undo();
    check(b.lineLook(n1, n2).icon.isEmpty() && b.lineLook(n1, n2).color.isValid(), QStringLiteral("... each change one undo step"));
    doc->undoStack()->redo();

    // the right-click on a line opens the line's menu (not the notes')
    {
        QStringList shown;
        QTimer::singleShot(400, qApp, [&shown] {
            if (auto *m = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                for (QAction *a : m->actions())
                    shown << a->objectName();
                m->close();
            }
        });
        MindMapView *view = mw->view();
        const QPoint at = view->mapFromScene(scene->lineMiddle(n1, n3));
        QContextMenuEvent ce(QContextMenuEvent::Mouse, at, view->viewport()->mapToGlobal(at));
        QApplication::sendEvent(view->viewport(), &ce);
        settle(500);
        check(shown.contains(QStringLiteral("mm_lineArrowToB")) && !b.pointsTo(n1, n3), QStringLiteral("... a right-click on a line shows the menu of that line"),
              shown.join(QLatin1Char(',')));
    }

    // kept in the board file (the program's own elements; Scapple ignores them)
    const QByteArray xml = b.toXml();
    mindmap::Board again;
    QString e;
    check(again.fromXml(xml, &e) && again.iconsOf(n1) == b.iconsOf(n1) && again.lineLook(n1, n2) == b.lineLook(n1, n2)
              && again.note(n1)->look.borderStyle == mindmap::BorderStyle::Cloud && again.note(n1)->look.fill == b.note(n1)->look.fill,
          QStringLiteral("icons, line colors and line icons are saved with the board and read back"), e);
    check(xml.contains("<Icons>") && xml.contains("<ConnectionLooks>"), QStringLiteral("... as elements of their own in the board file"));
    mw->view()->zoomToFit();
    settle(200);
    shot(mw, QStringLiteral("04-mind-map-colors-icons-lines"));
    mw->close();
    settle(100);
    w.closeProject();
}

// ------------------------------------------------------------------ compile

static void compileChecks()
{
    section(QStringLiteral("Compile Overview: font, replacements, object sheets"));
    QTemporaryDir dir;
    QString err;
    auto proj = novelProject(dir.path(), QStringLiteral("Book"), &err);
    if (!proj)
        return;
    Project *p = proj.get();
    for (BinderItem *c : p->draft()->children)
        for (BinderItem *t : c->children)
            if (t->hasText())
                setText(p, t, QStringLiteral("The colour of the sea."));
    BinderItem *extra = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Coda"));
    setText(p, extra, QStringLiteral("The colour of the sea."));
    const QString shelin = p->objects()->create(QStringLiteral("characters"), QString(), QStringLiteral("Shelin"));
    p->objects()->create(QStringLiteral("locations"), QString(), QStringLiteral("Ashvale"));

    auto build = [p] {
        compile::Compiler c(p, compile::formatByName(p->compile.formatName), p->compile, {QStringLiteral("Ada"), QStringLiteral("Writer")});
        return c.build();
    };
    auto allText = [](const compile::Manuscript &m) {
        QString t;
        for (const compile::Paragraph &para : m.paragraphs)
            for (const compile::Run &r : para.runs)
                t += r.text;
        return t;
    };
    p->compile.fontOverride = QStringLiteral("DejaVu Serif");
    p->compile.replacements = {{QStringLiteral("colour"), QStringLiteral("color")}};
    compile::Manuscript m = build();
    bool ownFont = false;
    for (const compile::Paragraph &para : m.paragraphs)
        for (const compile::Run &r : para.runs)
            ownFont = ownFont || (!r.fontFamily.isEmpty() && r.fontFamily != QLatin1String("DejaVu Serif"));
    check(m.fontFamily == QLatin1String("DejaVu Serif") && !ownFont, QStringLiteral("Font: one font for the whole manuscript when chosen"), m.fontFamily);
    check(allText(m).contains(QLatin1String("The color of the sea")) && !allText(m).contains(QLatin1String("colour")),
          QStringLiteral("Replacements: the project's own replacements are made"));
    check(!allText(m).contains(QLatin1String("Shelin")), QStringLiteral("Object sheets: none unless chosen"));
    p->compile.objectSheets = true;
    m = build();
    QStringList heads;
    for (const compile::Paragraph &para : m.paragraphs)
        if (para.style == compile::Paragraph::Heading1 || para.style == compile::Paragraph::Heading2) {
            QString t;
            for (const compile::Run &r : para.runs)
                t += r.text;
            heads << t;
        }
    check(heads.contains(QStringLiteral("Characters")) && heads.contains(QStringLiteral("Shelin")) && heads.contains(QStringLiteral("Ashvale"))
              && heads.indexOf(QStringLiteral("Shelin")) > heads.indexOf(QStringLiteral("Characters")),
          QStringLiteral("... chosen: a sheet per object after the back matter, under the name of its category"), heads.mid(qMax(0, int(heads.size()) - 6)).join(QLatin1String(", ")));
    p->compile.objectCategories = {QStringLiteral("locations")};
    m = build();
    check(allText(m).contains(QLatin1String("Ashvale")) && !allText(m).contains(QLatin1String("Shelin")),
          QStringLiteral("... only of the categories chosen"));
    Q_UNUSED(shelin);

    // kept in the project file
    QString e;
    check(p->save(&e), QStringLiteral("(saved)"), e);
    auto again = Project::open(p->folderPath(), &e);
    check(again && again->compile.fontOverride == QLatin1String("DejaVu Serif") && again->compile.objectSheets
              && again->compile.objectCategories == QStringList({QStringLiteral("locations")}) && again->compile.replacements.size() == 1
              && again->compile.replacements.first().replace == QLatin1String("color"),
          QStringLiteral("the compile settings of 0.21 are kept in the project file"), e);
    again.reset();

    // the window
    MainWindow w;
    w.resize(1200, 800);
    w.show();
    w.adoptProject(std::move(proj));
    settle(300);
    {
        CompileDialog dlg(w.project(), &w);
        dlg.resize(1150, 720);
        dlg.show();
        settle(300);
        auto *tabs = dlg.findChild<QTabWidget *>(QStringLiteral("compileTabs"));
        QStringList tabNames;
        for (int i = 0; tabs && i < tabs->count(); ++i)   // icon tabs, as in Scrivener; the names in their tool tips
            tabNames << tabs->tabToolTip(i).section(QLatin1Char(':'), 0, 0).section(QLatin1Char(' '), 0, 0);
        check(tabNames == QStringList({QStringLiteral("Contents"), QStringLiteral("Metadata"), QStringLiteral("Options"), QStringLiteral("Replacements")}),
              QStringLiteral("the window: Contents, Metadata, Options and Replacements on the right (Compile Overview)"), tabNames.join(QLatin1String(", ")));
        auto *font = dlg.findChild<QComboBox *>(QStringLiteral("compileFontOverride"));
        auto *assign = dlg.findChild<QTableWidget *>(QStringLiteral("compileAssignments"));
        auto *rep = dlg.findChild<QTableWidget *>(QStringLiteral("compileReplacements"));
        auto *sheets = dlg.findChild<QCheckBox *>(QStringLiteral("compileObjectSheets"));
        check(font && font->currentData().toString() == QLatin1String("DejaVu Serif") && assign && assign->rowCount() > 0 && rep && rep->rowCount() == 1
                  && sheets && sheets->isChecked(),
              QStringLiteral("... the font, the section layouts, the replacements and the object sheets as saved"));
        shot(&dlg, QStringLiteral("05-compile-overview"));
        if (tabs) {
            tabs->setCurrentIndex(3);
            shot(&dlg, QStringLiteral("05-compile-replacements"));
            tabs->setCurrentIndex(0);
        }
        bool sawAssign = false;
        QTimer::singleShot(500, qApp, [&sawAssign] {
            QWidget *m = QApplication::activeModalWidget();
            if (m && m->objectName() == QLatin1String("assignLayoutsDialog")) {
                sawAssign = true;
                shot(m, QStringLiteral("05-compile-assign-layouts"));
            }
            if (m)
                m->close();
        });
        if (auto *btn = dlg.findChild<QPushButton *>(QStringLiteral("compileAssignButton")))
            btn->click();
        settle(200);
        check(sawAssign, QStringLiteral("... Assign Section Layouts opens the window with the layouts"));
        if (font)
            font->setCurrentIndex(0);
        if (QPushButton *save = dlg.findChild<QPushButton *>(QStringLiteral("compileSave")))
            save->click();
        settle(100);
        dlg.reject();
    }
    check(w.project()->compile.fontOverride.isEmpty(), QStringLiteral("... Save Settings stores \"Determined by Section Layout\""));
    w.closeProject();
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qunsetenv("QB_NO_DIALOGS");
    setupApp(QStringLiteral("QuillbinderV021Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 && argv[1][0] != '-' ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    styleApp();
    zoom::install();
    // a window nobody answers would stop the run: it is named and closed after 6 s
    auto *stray = new QTimer(&app);
    stray->setInterval(1000);
    QObject::connect(stray, &QTimer::timeout, [] {
        static QPointer<QWidget> seen;
        static int ticks = 0;
        QWidget *m = QApplication::activeModalWidget();
        if (!m) {
            ticks = 0;
            return;
        }
        if (m != seen) {
            seen = m;
            ticks = 0;
        }
        if (++ticks < 6)
            return;
        const auto *box = qobject_cast<QMessageBox *>(m);
        check(false, QStringLiteral("(a window stayed open: %1 \"%2\")").arg(QString::fromLatin1(m->metaObject()->className()), box ? box->text() : m->windowTitle()));
        m->close();
        ticks = 0;
    });
    stray->start();

    optionsChecks();
    generalOptionChecks();
    editingOptionChecks();
    behaviorOptionChecks();
    zoomChecks();
    mindMapChecks();
    compileChecks();

    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
