// Scripted run for the writing tools (milestone M10): the tool interface,
// the add-ons Name Generator, Scratch Pad and Text Statistics, the link
// between running instances and the standalone scratch pad. Saves
// screenshots into the folder given as first argument. Exit code = number of
// failed checks.
//
//   packaging/build-addons.sh build build/addons-dist
//   QT_QPA_PLATFORM=offscreen ./qb_m10_tests /tmp/shots
//   xvfb-run -a -s "-screen 0 1920x1080x24" ./qb_m10_tests /tmp/shots
#include "AppInfo.h"
#include "compile/PluginManager.h"
#include "core/Addons.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "name-generator/NameLists.h"
#include "text-statistics/TextAnalysis.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/Commands.h"
#include "ui/Corkboard.h"
#include "ui/EditorPane.h"
#include "ui/KeyboardPage.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"
#include "ui/ToolHost.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStyleFactory>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>

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

static QString fileText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

template<typename R>
static R call(QObject *o, const char *method)
{
    R r{};
    QMetaObject::invokeMethod(o, method, Qt::DirectConnection, QReturnArgument<R>(QMetaType::fromType<R>().name(), r));
    return r;
}

// A request to an instance link from another thread, so this process can
// answer requests to itself while the test waits.
static QJsonObject threadQuery(const QString &socket, const QJsonObject &req)
{
    QJsonObject result;
    std::atomic<bool> done{false};
    std::thread t([&] {
        result = toolhost::Host::query(socket, req, 8000);
        done = true;
    });
    QElapsedTimer timer;
    timer.start();
    while (!done && timer.elapsed() < 15000)
        settle(20);
    t.join();
    return result;
}

static bool waitFor(const std::function<bool()> &cond, int ms = 5000)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        if (cond())
            return true;
        settle(50);
    }
    return cond();
}

static QJsonObject findNode(const QJsonArray &nodes, const QString &title)
{
    for (const QJsonValue &v : nodes) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("title")).toString() == title)
            return o;
        const QJsonObject inner = findNode(o.value(QStringLiteral("children")).toArray(), title);
        if (!inner.isEmpty())
            return inner;
    }
    return {};
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8()); // the scratch pad's default folder is ~/Documents/...
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qputenv("XDG_RUNTIME_DIR", (home.path() + QLatin1String("/run")).toUtf8());
    QDir().mkpath(home.path() + QLatin1String("/run"));
    QFile::setPermissions(home.path() + QLatin1String("/run"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("QB_NO_DIALOGS", "1");
    qputenv("QB_NO_SPECTACLE", "1");
    QCoreApplication::setOrganizationName(QStringLiteral("Quillbinder"));
    QCoreApplication::setApplicationName(QStringLiteral("Quillbinder"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    const QString buildDir = QStringLiteral(QB_TEST_BUILD_DIR);
    const bool x11 = QGuiApplication::platformName() == QLatin1String("xcb");

    // ================================================================ direct checks of the counting parts
    {
        using namespace textstats;
        check(syllables(QStringLiteral("make"), Language::English) == 1 && syllables(QStringLiteral("table"), Language::English) == 2
                  && syllables(QStringLiteral("walked"), Language::English) == 1 && syllables(QStringLiteral("lighthouse"), Language::English) == 2
                  && syllables(QStringLiteral("beautiful"), Language::English) == 3 && syllables(QStringLiteral("Leuchtturm"), Language::German) == 2
                  && syllables(QStringLiteral("Wärterin"), Language::German) == 3,
              QStringLiteral("syllables by rule of thumb: make 1, table 2, walked 1, lighthouse 2, beautiful 3, Leuchtturm 2, Wärterin 3"));
        const QStringList s = sentences(QStringLiteral("Mr. Smith arrived at 3.5 knots. \"Wait!\" he said. Then... nothing. Z. B. Herr Dr. Weber kam."));
        check(s.size() == 4, QStringLiteral("sentences: abbreviations (Mr., Dr., z. B.), decimals and quotes do not end a sentence"),
              s.join(QStringLiteral(" | ")));
        check(tokenize(QStringLiteral("Don't stop — well-known O’Brien, 1984.")) == QStringList({QStringLiteral("Don't"), QStringLiteral("stop"),
                                                                                                    QStringLiteral("well-known"), QStringLiteral("O’Brien"),
                                                                                                    QStringLiteral("1984")}),
              QStringLiteral("words keep apostrophes and hyphens inside"));
        check(detect(QStringLiteral("The keeper climbed the stairs and lit the lamp in the tower of the island.")) == Language::English
                  && detect(QStringLiteral("Der Wärter stieg die Treppe hinauf und zündete die Lampe im Turm der Insel an.")) == Language::German,
              QStringLiteral("language detection: English and German"));
        // a text whose numbers can be checked by hand: 3 sentences, 16 words of one syllable
        QList<Input> in{{QString(), QStringLiteral("t"), QString(), QStringLiteral("The cat sat on the mat. The dog ran to the old barn. A bird sang.")}};
        const Result r = analyze(in, Language::English, nullptr);
        const double wps = 16.0 / 3, spw = double(r.syllables) / 16;
        // 206.835 - 1.015 * 16/3 - 84.6 * 1 = 116.8217
        check(r.sentences == 3 && r.tokens == 16 && r.syllables == 16
                  && qAbs(r.fleschEase - (206.835 - 1.015 * wps - 84.6 * spw)) < 1e-9 && qAbs(r.fleschEase - 116.8217) < 0.001,
              QStringLiteral("Flesch Reading Ease of a hand-checked text (16 one-syllable words in 3 sentences): 116.82"),
              QStringLiteral("sentences %1 words %2 syllables %3 FRE %4").arg(r.sentences).arg(r.tokens).arg(r.syllables).arg(r.fleschEase));
        check(qAbs(r.lix - 16.0 / 3) < 1e-9 && qAbs(r.fleschKincaid - (0.39 * 16 / 3 + 11.8 - 15.59)) < 1e-9,
              QStringLiteral("LIX 5.3 (no word over 6 letters) and Flesch-Kincaid grade -1.71 of the same text"));
        int from = 0, to = 0;
        namegen::band(1000, 0, &from, &to);
        const bool common = from == 0 && to == 100;
        namegen::band(1000, 50, &from, &to);
        const bool all = from == 0 && to == 1000;
        namegen::band(1000, 100, &from, &to);
        check(common && all && from == 500 && to == 1000, QStringLiteral("name obscurity: 0 = the most common tenth, 50 = all, 100 = the rarer half"));
    }

    // ================================================================ add-ons with the tools
    const QStringList pkgs = {QStringLiteral("tool-name-generator"), QStringLiteral("tool-scratch-pad"), QStringLiteral("tool-text-statistics")};
    {
        QStringList problems;
        for (const QString &id : pkgs) {
            const QString pkg = buildDir + QStringLiteral("/addons-dist/") + id + QStringLiteral(".qbpkg");
            QStringList log;
            QString err;
            bool restart = false;
            const addons::PackageInfo info = addons::inspect(pkg);
            if (!info.isValid() || !addons::install(pkg, &log, &err, &restart))
                problems << id + QLatin1String(": ") + info.error + err;
        }
        check(problems.isEmpty(), QStringLiteral("the three add-on packages install"), problems.join(QLatin1String("; ")));
        const addons::PackageInfo older = addons::inspect(buildDir + QStringLiteral("/addons-dist/tool-scratch-pad.qbpkg"));
        check(older.manifest.minApp == QLatin1String("0.11.0"), QStringLiteral("the tool add-ons need program version 0.11.0"));
    }
    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.smartQuotes = false;
    theme::apply();
    compile::PluginManager::instance().load();
    {
        QStringList ids;
        for (tools::ToolInterface *t : compile::PluginManager::instance().tools())
            ids << t->id();
        ids.sort();
        check(ids == QStringList({QStringLiteral("name-generator"), QStringLiteral("scratch-pad"), QStringLiteral("text-statistics")}),
              QStringLiteral("the plugin manager loads three writing tools from the add-ons"),
              ids.join(QLatin1Char(',')) + QLatin1String(" / ") + compile::PluginManager::instance().messages().join(QLatin1String("; ")));
        bool exporterStill = compile::PluginManager::instance().exporter(QStringLiteral("docx")) != nullptr;
        check(exporterStill, QStringLiteral("exporters still load next to the tools"));
    }

    // ---------------------------------------------------------------- project
    QTemporaryDir projectsTmp;
    templates::Info novel;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("novel"))
            novel = t;
    QString err;
    auto p = templates::createProject(novel, projectsTmp.path(), QStringLiteral("The Lighthouse"), &err);
    check(p != nullptr, QStringLiteral("create project"), err);
    if (!p)
        return 1;
    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1300, 950);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    toolhost::Host &host = toolhost::host();
    check(host.startLink(), QStringLiteral("the instance link listens on a local socket"), host.linkPath());
    check(w.adoptProject(std::move(p)), QStringLiteral("open project in main window"));
    Project *proj = w.project();
    settle(300);
    BinderModel *m = proj->model();
    m->moveToTrash(QList<BinderItem *>(proj->draft()->children));
    BinderItem *ch1 = m->createItem(proj->draft(), 0, ItemType::Text, QStringLiteral("The Keeper"));
    BinderItem *ch2 = m->createItem(proj->draft(), 1, ItemType::Text, QStringLiteral("The Storm"));
    BinderItem *notesDoc = m->createItem(proj->research(), 0, ItemType::Text, QStringLiteral("Harbour Log"));
    BinderItem *german = m->createItem(proj->research(), 1, ItemType::Text, QStringLiteral("Notizen"));
    templates::setMarkdown(proj, ch1, QStringLiteral("The keeper climbed the tower. The keeper lit the lamp! Did the sea answer? It did not.\n"
                                                     "The old keeper waited for the morning boat.\n"));
    templates::setMarkdown(proj, ch2, QStringLiteral("The storm came at night. The old keeper waited for the morning boat again.\n"));
    templates::setMarkdown(proj, notesDoc, QStringLiteral("Supplies arrive on Tuesdays.\n"));
    templates::setMarkdown(proj, german, QStringLiteral("Der Wärter stieg die Treppe hinauf und zündete die Lampe an. "
                                                        "Das Meer war ruhig, und der Wind kam aus dem Westen.\n"));
    m->refreshAll();
    proj->save();
    auto *binder = w.findChild<BinderPanel *>();
    auto *editorSplit = qobject_cast<QSplitter *>(w.findChild<EditorPane *>()->parentWidget());
    auto *pane0 = editorSplit ? qobject_cast<EditorPane *>(editorSplit->widget(0)) : nullptr;
    check(binder && pane0, QStringLiteral("window parts found"));
    if (!binder || !pane0)
        return 1;
    binder->tree()->expandAll();
    auto ed = [&]() -> TextEditor * { return pane0->textView()->editors().value(0); };

    // ================================================================ menus and commands
    QAction *ngAction = w.findChild<QAction *>(QStringLiteral("tool:name-generator.show"));
    QAction *spAction = w.findChild<QAction *>(QStringLiteral("tool:scratch-pad.show"));
    QAction *tsAction = w.findChild<QAction *>(QStringLiteral("tool:text-statistics.show"));
    {
        QStringList missing;
        for (const QStringList &pl : QList<QStringList>{{"Edit", "Writing Tools", "Name Generator…"},
                                                        {"Window", "Scratch Pad"},
                                                        {"Project", "Text Statistics…"},
                                                        {"Edit", "Writing Tools", "Add-on Manager…"}})
            if (!menuPath(&w, pl))
                missing << pl.join(QLatin1String(" > "));
        check(missing.isEmpty() && ngAction && spAction && tsAction,
              QStringLiteral("Edit > Writing Tools > Name Generator, Window > Scratch Pad, Project > Text Statistics (as in the tutorial)"),
              missing.join(QLatin1String("; ")));
        QMenu *projMenu = menuPath(&w, {"Project"})->menu();
        const QList<QAction *> pa = projMenu->actions();
        int si = -1, ti = -1;
        for (int i = 0; i < pa.size(); ++i) {
            if (pa[i]->objectName() == QLatin1String("statistics"))
                si = i;
            if (pa[i] == tsAction)
                ti = i;
        }
        check(si >= 0 && ti == si + 1, QStringLiteral("Text Statistics follows Project Statistics in the Project menu"));
        check(spAction && spAction->shortcut() == QKeySequence(QStringLiteral("Alt+Shift+Return")),
              QStringLiteral("the scratch pad's standard shortcut is Alt+Shift+Return (as in the tutorial)"));
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
        bool registered = commands::find(QStringLiteral("tool:name-generator.show")) && commands::find(QStringLiteral("tool:scratch-pad.show"))
            && commands::find(QStringLiteral("tool:text-statistics.show"));
        check(twice.isEmpty() && registered, QStringLiteral("the tool commands are commands with shortcuts of their own; no shortcut is used twice"),
              twice.join(QLatin1String("; ")));
        // a new shortcut through the keyboard settings
        QMap<QString, QString> custom = commands::custom();
        custom.insert(QStringLiteral("tool:name-generator.show"), QStringLiteral("Ctrl+Alt+N"));
        commands::setCustom(custom);
        const bool changed = ngAction->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+N"));
        custom.remove(QStringLiteral("tool:name-generator.show"));
        commands::setCustom(custom);
        check(changed && ngAction->shortcut().isEmpty(), QStringLiteral("File > Options > Keyboard can give a writing tool a shortcut and take it back"));
        check(tsAction->isEnabled(), QStringLiteral("Text Statistics is available with a project open"));
    }

    // ================================================================ name generator
    QWidget *ng = nullptr;
    {
        ngAction->trigger();
        settle(300);
        ng = w.findChild<QWidget *>(QStringLiteral("nameGenerator"));
        check(ng && ng->isVisible(), QStringLiteral("Edit > Writing Tools > Name Generator opens the window"));
        if (!ng)
            return 1;
        auto *fore = ng->findChild<QListWidget *>(QStringLiteral("ngForenames"));
        auto *sur = ng->findChild<QListWidget *>(QStringLiteral("ngSurnames"));
        auto *gender = ng->findChild<QComboBox *>(QStringLiteral("ngGender"));
        auto *count = ng->findChild<QSpinBox *>(QStringLiteral("ngCount"));
        auto *show = ng->findChild<QComboBox *>(QStringLiteral("ngShow"));
        auto *same = ng->findChild<QCheckBox *>(QStringLiteral("ngSameOrigin"));
        auto *middle = ng->findChild<QCheckBox *>(QStringLiteral("ngMiddle"));
        auto *allit = ng->findChild<QSlider *>(QStringLiteral("ngAlliteration"));
        auto *dbl = ng->findChild<QSlider *>(QStringLiteral("ngDoubleBarrelled"));
        auto *foreObsc = ng->findChild<QSlider *>(QStringLiteral("ngForeObscurity"));
        auto *starts = ng->findChild<QLineEdit *>(QStringLiteral("ngForeStarts"));
        auto *results = ng->findChild<QListWidget *>(QStringLiteral("ngResults"));
        int freqLabels = 0;
        for (int i = 0; i < fore->count(); ++i)
            if (fore->item(i)->text().contains(QLatin1String("(by frequency)")))
                ++freqLabels;
        check(fore->count() >= 60 && sur->count() >= 60 && freqLabels >= 10,
              QStringLiteral("62 name lists from the add-on; the lists with frequencies are marked"),
              QStringLiteral("%1 forename lists, %2 surname lists, %3 by frequency").arg(fore->count()).arg(sur->count()).arg(freqLabels));
        // the data, read here to check the results
        const QStringList dirs = host.contentDirs(QStringLiteral("names"));
        const QList<namegen::NameList> lists = namegen::loadLists(dirs, QString());
        auto list = [&](const QString &id) {
            for (const namegen::NameList &l : lists)
                if (l.id == id)
                    return l;
            return namegen::NameList();
        };
        // "Forename Surname" where either part may have spaces of its own
        auto splits = [](const QString &n, const QStringList &fores, const QStringList &surs) {
            for (const QString &f : fores)
                if (n.startsWith(f + QLatin1Char(' ')) && surs.contains(n.mid(f.size() + 1)))
                    return true;
            return false;
        };
        auto generate = [&]() {
            QMetaObject::invokeMethod(ng, "generate");
            settle(50);
            return call<QStringList>(ng, "results");
        };
        QMetaObject::invokeMethod(ng, "chooseLists", Q_ARG(QStringList, {QStringLiteral("german")}), Q_ARG(QStringList, {QStringLiteral("german")}));
        gender->setCurrentIndex(gender->findData(int(namegen::Gender::Female)));
        count->setValue(30);
        QStringList names = generate();
        const namegen::NameList de = list(QStringLiteral("german"));
        bool allGerman = names.size() == 30 && QSet<QString>(names.begin(), names.end()).size() == 30;
        for (const QString &n : names)
            if (!splits(n, de.female.names, de.surnames.names))
                allGerman = false;
        check(allGerman, QStringLiteral("30 different German women's names, forename and surname from the German lists"), names.mid(0, 5).join(QLatin1String(", ")));
        shot(ng, QStringLiteral("m10-name-generator"));

        QMetaObject::invokeMethod(ng, "chooseLists", Q_ARG(QStringList, {QStringLiteral("polish")}), Q_ARG(QStringList, {QStringLiteral("polish")}));
        count->setValue(300); // most Polish surnames have one form for both; about 3 % end in -ska
        names = generate();
        count->setValue(30);
        int ska = 0, ski = 0;
        for (const QString &n : names) {
            ska += n.endsWith(QLatin1String("ska")) || n.endsWith(QLatin1String("cka")) ? 1 : 0;
            ski += n.endsWith(QLatin1String("ski")) || n.endsWith(QLatin1String("cki")) ? 1 : 0;
        }
        check(ska > 0 && ski == 0, QStringLiteral("Polish women get the women's surname forms (-ska, not -ski)"),
              QStringLiteral("%1 x -ska, %2 x -ski: %3").arg(ska).arg(ski).arg(names.mid(0, 4).join(QLatin1String(", "))));

        QMetaObject::invokeMethod(ng, "chooseLists", Q_ARG(QStringList, {QStringLiteral("english")}), Q_ARG(QStringList, {QStringLiteral("english")}));
        gender->setCurrentIndex(gender->findData(int(namegen::Gender::Both)));
        allit->setValue(100);
        names = generate();
        QSet<QString> initialsThere;
        for (const QString &sn : list(QStringLiteral("english")).surnames.names)
            initialsThere.insert(sn.left(1).toUpper());
        bool alliterate = !names.isEmpty();
        for (const QString &n : names)
            if (initialsThere.contains(n.left(1).toUpper()) && n.left(1).compare(n.section(QLatin1Char(' '), -1).left(1), Qt::CaseInsensitive) != 0)
                alliterate = false;
        allit->setValue(0);
        check(alliterate, QStringLiteral("alliteration 100 %: every surname starts with the forename's letter"), names.mid(0, 4).join(QLatin1String(", ")));
        dbl->setValue(100);
        names = generate();
        bool hyphen = !names.isEmpty();
        for (const QString &n : names)
            if (!n.section(QLatin1Char(' '), -1).contains(QLatin1Char('-')))
                hyphen = false;
        dbl->setValue(0);
        check(hyphen, QStringLiteral("double surnames 100 %: Smith-Jones"), names.mid(0, 3).join(QLatin1String(", ")));
        middle->setChecked(true);
        names = generate();
        bool three = !names.isEmpty();
        for (const QString &n : names)
            if (n.split(QLatin1Char(' ')).size() < 3)
                three = false;
        middle->setChecked(false);
        check(three, QStringLiteral("with a middle name: three parts"), names.value(0));
        show->setCurrentIndex(show->findData(int(namegen::Show::InitialAndSurname)));
        names = generate();
        bool initials = !names.isEmpty();
        for (const QString &n : names)
            if (!QRegularExpression(QStringLiteral("^\\S\\. \\S")).match(n).hasMatch())
                initials = false;
        show->setCurrentIndex(0);
        check(initials, QStringLiteral("show initials and surname: \"J. Smith\""), names.value(0));
        starts->setText(QStringLiteral("Ma"));
        names = generate();
        bool ma = !names.isEmpty();
        for (const QString &n : names)
            if (!n.startsWith(QLatin1String("Ma")))
                ma = false;
        starts->clear();
        check(ma, QStringLiteral("forename starts with \"Ma\""), names.mid(0, 4).join(QLatin1String(", ")));
        // common and rare names (American: US census frequencies)
        QMetaObject::invokeMethod(ng, "chooseLists", Q_ARG(QStringList, {QStringLiteral("american")}), Q_ARG(QStringList, {QStringLiteral("american")}));
        const namegen::NameList us = list(QStringLiteral("american"));
        auto rankBand = [&](const QStringList &ns, bool top) {
            for (const QString &n : ns) {
                const QString f = n.section(QLatin1Char(' '), 0, 0);
                const QStringList &all = us.female.names;
                const int rank = int(all.indexOf(f));
                if (rank < 0 || (top ? rank >= std::ceil(all.size() * 0.1) : rank < all.size() / 2))
                    return false;
            }
            return !ns.isEmpty();
        };
        gender->setCurrentIndex(gender->findData(int(namegen::Gender::Female)));
        foreObsc->setValue(0);
        const bool commonOnly = rankBand(generate(), true);
        foreObsc->setValue(100);
        const bool rareOnly = rankBand(generate(), false);
        foreObsc->setValue(30);
        gender->setCurrentIndex(gender->findData(int(namegen::Gender::Both)));
        check(us.hasFrequencies() && commonOnly && rareOnly,
              QStringLiteral("common ↔ rare: at the left only the most common tenth of the US forenames, at the right only the rarer half"));
        QMetaObject::invokeMethod(ng, "chooseLists", Q_ARG(QStringList, {QStringLiteral("japanese")}), Q_ARG(QStringList, {QStringLiteral("japanese")}));
        names = generate();
        const namegen::NameList ja = list(QStringLiteral("japanese"));
        check(!names.isEmpty() && ja.surnames.names.contains(names.first().section(QLatin1Char(' '), 0, 0)),
              QStringLiteral("Japanese names: family name first (\"Sato Akira\")"), names.value(0));
        same->setChecked(false);
        QMetaObject::invokeMethod(ng, "chooseLists", Q_ARG(QStringList, {QStringLiteral("italian")}), Q_ARG(QStringList, {QStringLiteral("irish")}));
        names = generate();
        const namegen::NameList it = list(QStringLiteral("italian")), ie = list(QStringLiteral("irish"));
        bool mixed = !names.isEmpty();
        for (const QString &n : names)
            if (!splits(n, it.male.names + it.female.names, ie.surnames.names))
                mixed = false;
        same->setChecked(true);
        check(mixed, QStringLiteral("forenames from one list, surnames from another (Italian + Irish)"), names.value(0));

        // insert into the text, copy
        binder->select({notesDoc}, true);
        settle(300);
        ed()->setFocus();
        QTextCursor c = ed()->textCursor();
        c.movePosition(QTextCursor::End);
        ed()->setTextCursor(c);
        results->setCurrentRow(0);
        const QString chosen = results->currentItem()->text();
        ng->findChild<QPushButton *>(QStringLiteral("ngInsert"))->click();
        settle(150);
        const bool inserted = ed()->toPlainText().contains(chosen);
        ed()->document()->undo();
        check(inserted && !ed()->toPlainText().contains(chosen), QStringLiteral("Insert in Text puts the name at the cursor; one Undo takes it away"), chosen);
        results->clearSelection();
        ng->findChild<QPushButton *>(QStringLiteral("ngCopy"))->click();
        check(QApplication::clipboard()->text().split(QLatin1Char('\n')).size() == results->count(),
              QStringLiteral("Copy puts all names on the clipboard, one per line"));
        // own list
        const QString elvish = home.path() + QStringLiteral("/elvish.txt");
        {
            QFile f(elvish);
            f.open(QIODevice::WriteOnly);
            f.write("# my names\nAerin\nBeleth, rare\nCelebrin\nDaeron\nElowen\nFaelar\nGalion\nHalwen\n");
        }
        bool added = false;
        QMetaObject::invokeMethod(ng, "addListFromFile", Q_RETURN_ARG(bool, added), Q_ARG(QString, elvish), Q_ARG(QString, QStringLiteral("Elvish")),
                                  Q_ARG(QString, QStringLiteral("both")));
        bool listed = false;
        for (int i = 0; i < fore->count(); ++i)
            listed = listed || fore->item(i)->text() == QLatin1String("Elvish (own list)");
        same->setChecked(false);
        QMetaObject::invokeMethod(ng, "chooseLists", Q_ARG(QStringList, {QStringLiteral("own-elvish")}), Q_ARG(QStringList, {QStringLiteral("english")}));
        count->setValue(8);
        names = generate();
        const QStringList elves = {"Aerin", "Beleth", "Celebrin", "Daeron", "Elowen", "Faelar", "Galion", "Halwen"};
        bool fromFile = !names.isEmpty();
        for (const QString &n : names)
            if (!elves.contains(n.section(QLatin1Char(' '), 0, 0)))
                fromFile = false;
        check(added && listed && fromFile, QStringLiteral("own list from a text file (comments and second columns ignored) gives the forenames"),
              names.mid(0, 3).join(QLatin1String(", ")));
        for (int i = 0; i < fore->count(); ++i)
            if (fore->item(i)->text() == QLatin1String("Elvish (own list)"))
                fore->setCurrentRow(i);
        QMetaObject::invokeMethod(ng, "removeOwnList");
        bool gone = true;
        for (int i = 0; i < fore->count(); ++i)
            gone = gone && !fore->item(i)->text().startsWith(QLatin1String("Elvish"));
        check(gone, QStringLiteral("Remove Own List"));
        same->setChecked(true);
        count->setValue(12);
        QMetaObject::invokeMethod(ng, "generate");
        ng->close();
        settle(200);
        ngAction->trigger();
        settle(300);
        ng = w.findChild<QWidget *>(QStringLiteral("nameGenerator"));
        check(ng && ng->findChild<QSpinBox *>(QStringLiteral("ngCount"))->value() == 12, QStringLiteral("the name generator remembers its settings"));
        if (ng)
            ng->close();
    }

    // ================================================================ options
    const QString notesFolder = home.path() + QStringLiteral("/My Scratch Notes");
    {
        OptionsDialog dlg(&w, OptionsDialog::General);
        auto *box = dlg.findChild<QGroupBox *>(QStringLiteral("toolOptions:scratch-pad"));
        auto *folderEdit = dlg.findChild<QLineEdit *>(QStringLiteral("scratchFolder"));
        auto *hint = dlg.keyboardPage()->findChild<QLabel *>(QStringLiteral("globalShortcutHint"));
        check(box && folderEdit && folderEdit->text().endsWith(QLatin1String("Documents/Quillbinder Scratch Pad")),
              QStringLiteral("Options > General has a Scratch Pad section; notes go to ~/Documents/Quillbinder Scratch Pad unless chosen"),
              folderEdit ? folderEdit->text() : QString());
        check(hint && hint->text().contains(QLatin1String("--tool scratch-pad.show")),
              QStringLiteral("Options > Keyboard names the command for a shortcut that works in every program"));
        dlg.show();
        settle(200);
        shot(&dlg, QStringLiteral("m10-options-general"));
        if (folderEdit)
            folderEdit->setText(notesFolder);
        dlg.apply();
        settle(100);
        check(host.setting(QStringLiteral("scratch-pad"), QStringLiteral("folder")).toString() == notesFolder,
              QStringLiteral("the chosen folder is kept"));
    }

    // ================================================================ scratch pad
    QWidget *sp = nullptr;
    {
        spAction->trigger();
        settle(400);
        sp = w.findChild<QWidget *>(QStringLiteral("scratchPad"));
        check(sp && sp->isVisible() && QFileInfo(notesFolder).isDir(), QStringLiteral("Window > Scratch Pad opens it; the notes folder is made"));
        if (!sp)
            return 1;
        auto *list = sp->findChild<QListWidget *>(QStringLiteral("scratchNotes"));
        auto *editor = sp->findChild<QTextEdit *>(QStringLiteral("scratchEditor"));
        auto *camera = sp->findChild<QToolButton *>(QStringLiteral("scratchCamera"));
        QMetaObject::invokeMethod(sp, "newNote", Q_ARG(bool, true));
        settle(200);
        const bool created = QFileInfo::exists(notesFolder + QStringLiteral("/Untitled Note.html"));
        QMetaObject::invokeMethod(sp, "setNoteText", Q_ARG(QString, QStringLiteral("<p>Hello <b>harbour</b> world.</p><p>Second line.</p>")));
        bool renamed = false;
        QMetaObject::invokeMethod(sp, "renameNote", Q_RETURN_ARG(bool, renamed), Q_ARG(QString, QStringLiteral("Harbour notes")));
        const QString html = fileText(notesFolder + QStringLiteral("/Harbour notes.html"));
        check(created && renamed && html.contains(QLatin1String("harbour")) && html.contains(QLatin1String("font-weight")),
              QStringLiteral("a formatted note is a file (Untitled Note.html), renamed with its title, bold kept"));
        check(camera && camera->isVisible(), QStringLiteral("the camera button is there for a formatted note"));
        QImage img(120, 80, QImage::Format_RGB32);
        img.fill(QColor(200, 30, 30));
        bool inserted = false;
        QMetaObject::invokeMethod(sp, "insertImage", Q_RETURN_ARG(bool, inserted), Q_ARG(QImage, img));
        const QStringList pics = QDir(notesFolder + QStringLiteral("/.images")).entryList({QStringLiteral("*.png")});
        check(inserted && pics.size() == 1 && fileText(notesFolder + QStringLiteral("/Harbour notes.html")).contains(QLatin1String(".images/") + pics.value(0)),
              QStringLiteral("a picture goes into the note (file in .images/)"));
        shot(sp, QStringLiteral("m10-scratch-pad"));
        // plain text note typed with the keyboard
        QMetaObject::invokeMethod(sp, "newNote", Q_ARG(bool, false));
        settle(200);
        editor->setFocus();
        QTest::keyClicks(editor, QStringLiteral("Call the coast guard about the buoy."));
        settle(1200); // saved after a pause
        check(fileText(notesFolder + QStringLiteral("/Untitled Note.txt")) == QLatin1String("Call the coast guard about the buoy.") && !camera->isVisible(),
              QStringLiteral("a plain text note is saved by itself after typing; no camera for plain text"));
        // another program (or the scratch pad of another window) adds a note: the list follows
        {
            QFile f(notesFolder + QStringLiteral("/From elsewhere.txt"));
            f.open(QIODevice::WriteOnly);
            f.write("Written by another window.");
        }
        check(waitFor([&] { return call<QStringList>(sp, "noteTitles").contains(QStringLiteral("From elsewhere")); }, 4000),
              QStringLiteral("a note added to the folder from outside appears in the list"));
        // reopen: everything is still there, including the picture
        sp->close();
        settle(200);
        spAction->trigger();
        settle(300);
        bool selected = false;
        QMetaObject::invokeMethod(sp, "selectNote", Q_RETURN_ARG(bool, selected), Q_ARG(QString, QStringLiteral("Harbour notes")));
        bool hasImage = false;
        for (QTextBlock b = editor->document()->begin(); b.isValid(); b = b.next())
            for (auto fit = b.begin(); !fit.atEnd(); ++fit)
                if (fit.fragment().charFormat().isImageFormat()) {
                    const QVariant res = editor->document()->resource(QTextDocument::ImageResource,
                                                                      QUrl(fit.fragment().charFormat().toImageFormat().name()));
                    hasImage = hasImage || !qvariant_cast<QImage>(res).isNull() || !qvariant_cast<QPixmap>(res).isNull();
                }
        check(selected && editor->toPlainText().contains(QLatin1String("Hello harbour world.")) && hasImage,
              QStringLiteral("reopened, the note shows its text and picture"));

        // send to this project: new document in the Research folder
        const int researchBefore = int(proj->research()->children.size());
        bool sent = false;
        QMetaObject::invokeMethod(sp, "sendTo", Q_RETURN_ARG(bool, sent), Q_ARG(QString, QStringLiteral("self")), Q_ARG(QString, QStringLiteral("import")),
                                  Q_ARG(QString, QString()));
        settle(300);
        BinderItem *made = proj->research()->children.size() > researchBefore ? proj->research()->children.last() : nullptr;
        bool madeImage = false, madeBold = false;
        if (made) {
            QTextDocument *d = proj->docs()->text(made->id);
            for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
                for (auto fit = b.begin(); !fit.atEnd(); ++fit) {
                    const QTextCharFormat cf = fit.fragment().charFormat();
                    if (cf.isImageFormat())
                        madeImage = QFileInfo::exists(proj->contentDir(made->id) + QLatin1Char('/') + cf.toImageFormat().name())
                            || !d->resource(QTextDocument::ImageResource, QUrl(cf.toImageFormat().name())).isNull();
                    if (fit.fragment().text() == QLatin1String("harbour") && cf.fontWeight() >= QFont::Bold)
                        madeBold = true;
                }
        }
        check(sent && made && made->title == QLatin1String("Harbour notes") && proj->docs()->plainText(made->id).contains(QLatin1String("Second line."))
                  && madeImage && madeBold,
              QStringLiteral("Send to Project > New Document: a document in the Research folder with the text, bold and the picture"),
              made ? proj->docs()->plainText(made->id) : QStringLiteral("no new document"));
        // send: added to the end of a text
        QMetaObject::invokeMethod(sp, "selectNote", Q_RETURN_ARG(bool, selected), Q_ARG(QString, QStringLiteral("Untitled Note")));
        QMetaObject::invokeMethod(sp, "sendTo", Q_RETURN_ARG(bool, sent), Q_ARG(QString, QStringLiteral("self")), Q_ARG(QString, QStringLiteral("append")),
                                  Q_ARG(QString, notesDoc->id));
        settle(300);
        const QString logText = proj->docs()->plainText(notesDoc->id);
        check(sent && logText.startsWith(QLatin1String("Supplies arrive on Tuesdays.")) && logText.trimmed().endsWith(QLatin1String("Call the coast guard about the buoy.")),
              QStringLiteral("Send to Project > Add to the End of: the note follows the text as a new paragraph"), logText);
        // into the Draft folder
        QMetaObject::invokeMethod(sp, "sendTo", Q_RETURN_ARG(bool, sent), Q_ARG(QString, QStringLiteral("self")), Q_ARG(QString, QStringLiteral("import")),
                                  Q_ARG(QString, proj->draft()->id));
        settle(300);
        check(sent && proj->draft()->children.last()->title == QLatin1String("Untitled Note"), QStringLiteral("a note sent into the Draft folder"));
        // the menu
        auto *sendMenu = sp->findChild<QMenu *>(QStringLiteral("scratchSendMenu"));
        emit sendMenu->aboutToShow();
        QMenu *projMenu = nullptr;
        for (QAction *a : sendMenu->actions())
            if (menuText(a->text()) == QLatin1String("The Lighthouse"))
                projMenu = a->menu();
        QStringList entries;
        if (projMenu)
            for (QAction *a : projMenu->actions())
                entries << menuText(a->text());
        check(projMenu && entries.contains(QStringLiteral("New Document in the Research Folder")) && entries.contains(QStringLiteral("New Document in"))
                  && entries.contains(QStringLiteral("Add to the End of")),
              QStringLiteral("the Send to Project menu lists the open project with its binder"), entries.join(QLatin1String(", ")));
        // delete
        QMetaObject::invokeMethod(sp, "selectNote", Q_RETURN_ARG(bool, selected), Q_ARG(QString, QStringLiteral("From elsewhere")));
        QMetaObject::invokeMethod(sp, "deleteNote", Q_ARG(bool, false));
        settle(200);
        check(!QFileInfo::exists(notesFolder + QStringLiteral("/From elsewhere.txt")) && !call<QStringList>(sp, "noteTitles").contains(QStringLiteral("From elsewhere")),
              QStringLiteral("Delete removes the note's file"));
        Q_UNUSED(list);
    }

    // ================================================================ instance link
    {
        const QJsonObject info = threadQuery(host.linkPath(), QJsonObject{{QStringLiteral("cmd"), QStringLiteral("info")}});
        const QJsonObject keeper = findNode(info.value(QStringLiteral("binder")).toArray(), QStringLiteral("The Keeper"));
        check(info.value(QStringLiteral("title")).toString() == QLatin1String("The Lighthouse") && !keeper.isEmpty()
                  && keeper.value(QStringLiteral("text")).toBool() && info.value(QStringLiteral("version")).toString() == QLatin1String(QB_APP_VERSION),
              QStringLiteral("link \"info\": project title, version and binder"));
        // a note sent by another process
        const QString file = home.path() + QStringLiteral("/remote.txt");
        {
            QFile f(file);
            f.open(QIODevice::WriteOnly);
            f.write("A note from another window.");
        }
        const QJsonObject reply = threadQuery(host.linkPath(), QJsonObject{{QStringLiteral("cmd"), QStringLiteral("send")},
                                                                          {QStringLiteral("request"), toolhost::toJson(tools::SendRequest{
                                                                                                          file, QStringLiteral("Remote note"),
                                                                                                          QStringLiteral("import"), QString()})}});
        check(reply.value(QStringLiteral("ok")).toBool() && proj->research()->children.last()->title == QLatin1String("Remote note"),
              QStringLiteral("link \"send\": a note from another process becomes a document"), reply.value(QStringLiteral("error")).toString());
        const QJsonObject bad = threadQuery(host.linkPath(), QJsonObject{{QStringLiteral("cmd"), QStringLiteral("send")},
                                                                        {QStringLiteral("request"), toolhost::toJson(tools::SendRequest{
                                                                                                        file, QString(), QStringLiteral("append"),
                                                                                                        QStringLiteral("no-such-id")})}});
        check(!bad.value(QStringLiteral("ok")).toBool() && !bad.value(QStringLiteral("error")).toString().isEmpty(),
              QStringLiteral("link \"send\" to a document that does not exist is refused with a reason"));
    }

    // a second program window with another project (a separate process)
    QString otherKey;
    QProcess other;
    {
        auto p2 = templates::createProject(novel, projectsTmp.path(), QStringLiteral("Second Book"), &err);
        const QString p2file = p2 ? p2->indexPath() : QString();
        p2.reset();
        other.setProgram(buildDir + QStringLiteral("/quillbinder"));
        other.setArguments({QStringLiteral("--no-restore"), p2file});
        other.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        other.start();
        const bool up = waitFor([&] {
            for (const QString &peer : host.peers()) {
                const QJsonObject o = toolhost::Host::query(peer, QJsonObject{{QStringLiteral("cmd"), QStringLiteral("info")}}, 1000);
                if (o.value(QStringLiteral("title")).toString() == QLatin1String("Second Book")) {
                    otherKey = peer;
                    return true;
                }
            }
            return false;
        }, 20000);
        QStringList titles;
        for (const tools::ProjectInfo &pi : host.openProjects())
            titles << pi.title + (pi.thisWindow ? QStringLiteral("*") : QString());
        check(up && titles.contains(QStringLiteral("The Lighthouse*")) && titles.contains(QStringLiteral("Second Book")),
              QStringLiteral("a second program process: the scratch pad lists both open projects"), titles.join(QLatin1String(", ")));
        bool sent = false;
        bool selected = false;
        QMetaObject::invokeMethod(sp, "selectNote", Q_RETURN_ARG(bool, selected), Q_ARG(QString, QStringLiteral("Harbour notes")));
        if (!otherKey.isEmpty())
            QMetaObject::invokeMethod(sp, "sendTo", Q_RETURN_ARG(bool, sent), Q_ARG(QString, otherKey), Q_ARG(QString, QStringLiteral("import")),
                                      Q_ARG(QString, QString()));
        const QJsonObject after = otherKey.isEmpty() ? QJsonObject()
                                                     : toolhost::Host::query(otherKey, QJsonObject{{QStringLiteral("cmd"), QStringLiteral("info")}});
        check(sent && !findNode(after.value(QStringLiteral("binder")).toArray(), QStringLiteral("Harbour notes")).isEmpty(),
              QStringLiteral("Send to Project into the other process's project: the note is in its binder"),
              call<QString>(sp, "status") + QLatin1String(" / ") + QString::fromUtf8(QJsonDocument(after).toJson(QJsonDocument::Compact)).left(600));
        shot(sp, QStringLiteral("m10-scratch-pad-sent"));
    }

    // ================================================================ standalone scratch pad (--tool)
    {
        QProcess solo;
        solo.setProgram(buildDir + QStringLiteral("/quillbinder"));
        solo.setArguments({QStringLiteral("--scratch-pad")});
        solo.start();
        QString soloKey;
        const bool up = waitFor([&] {
            for (const QString &peer : host.peers()) {
                const QJsonObject o = toolhost::Host::query(peer, QJsonObject{{QStringLiteral("cmd"), QStringLiteral("info")}}, 1000);
                if (o.value(QStringLiteral("standalone")).toString() == QLatin1String("scratch-pad.show")) {
                    soloKey = peer;
                    return true;
                }
            }
            return false;
        }, 20000);
        check(up && solo.state() == QProcess::Running, QStringLiteral("--scratch-pad starts the scratch pad without a main window"));
        QProcess again;
        again.start(buildDir + QStringLiteral("/quillbinder"), {QStringLiteral("--tool"), QStringLiteral("scratch-pad.show")});
        const bool quick = again.waitForFinished(15000);
        check(quick && again.exitCode() == 0 && solo.state() == QProcess::Running,
              QStringLiteral("a second --scratch-pad brings the open one to the front and ends"));
        QProcess wrong;
        wrong.start(buildDir + QStringLiteral("/quillbinder"), {QStringLiteral("--tool"), QStringLiteral("no-such.command")});
        wrong.waitForFinished(15000);
        check(wrong.exitCode() == 2 && QString::fromLocal8Bit(wrong.readAllStandardError()).contains(QLatin1String("No writing tool")),
              QStringLiteral("--tool with an unknown command reports it (exit code 2)"));
        solo.terminate();
        solo.waitForFinished(5000);
        if (solo.state() != QProcess::NotRunning)
            solo.kill();
        solo.waitForFinished(3000);
        settle(200);
        check(!host.peers().contains(soloKey), QStringLiteral("the socket of an ended process is cleaned up"));
    }
    other.terminate();
    other.waitForFinished(5000);
    if (other.state() != QProcess::NotRunning)
        other.kill();

    // ================================================================ corkboard at the edge (found while building M10)
    {
        // In a narrow editor the corkboard could switch its scroll bars on and off
        // for ever (100 % processor; the window then answered nothing). Many window
        // sizes, many cards: the bars must come to rest at every size.
        // Two cards (210 px wide) on a board with room for three in a row: the empty
        // grid was a few pixels wider than the view, both scroll bars came, the grid
        // dropped to two columns, both bars went, and so on.
        BinderItem *board = m->createItem(proj->research(), -1, ItemType::Folder, QStringLiteral("Board"));
        for (int i = 0; i < 2; ++i)
            m->createItem(board, -1, ItemType::Text, QStringLiteral("Card %1").arg(i + 1));
        AppSettings::instance().cardWidth = 210;
        AppSettings::instance().cardsPerRow = 0;
        auto *cork = new Corkboard;
        cork->setFrameShape(QFrame::NoFrame); // as in the editor
        cork->setProject(proj);
        cork->setContent({board}, {});
        cork->show();
        struct Counter : QObject {
            int n = 0;
            bool eventFilter(QObject *, QEvent *e) override
            {
                if (e->type() == QEvent::Show || e->type() == QEvent::Hide)
                    ++n;
                return false;
            }
        } counter;
        cork->verticalScrollBar()->installEventFilter(&counter);
        cork->horizontalScrollBar()->installEventFilter(&counter);
        int restless = 0, sizes = 0;
        QStringList where;
        // with the fault the event loop never comes back: a watchdog ends the run then
        std::atomic<bool> swept{false};
        std::thread watchdog([&swept] {
            for (int i = 0; i < 600 && !swept; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!swept) {
                std::printf("FAIL  the corkboard's scroll bars come to rest at every size (the program got stuck switching them)\n");
                std::fflush(stdout);
                std::_Exit(1);
            }
        });
        // seen in a real window: 721 x 678, two cards, room for three in a row
        for (int width = 700; width <= 760; width += 2) {
            for (int height : {300, 450, 600, 678}) {
                cork->resize(width, height);
                settle(15);
                counter.n = 0;
                settle(35);
                ++sizes;
                if (counter.n > 6) {
                    ++restless;
                    if (where.size() < 3)
                        where << QStringLiteral("%1x%2: %3 switches").arg(width).arg(height).arg(counter.n);
                }
            }
        }
        swept = true;
        watchdog.join();
        delete cork;
        check(restless == 0, QStringLiteral("the corkboard's scroll bars come to rest at every size (%1 sizes; no endless switching)").arg(sizes),
              QStringLiteral("%1 restless sizes: ").arg(restless) + where.join(QLatin1String("; ")));
    }

    // ================================================================ text statistics
    {
        binder->select({ch1}, true);
        settle(300);
        ed()->setFocus();
        ed()->selectAll();
        tsAction->trigger();
        settle(400);
        QWidget *ts = w.findChild<QWidget *>(QStringLiteral("textStatistics"));
        check(ts && ts->isVisible(), QStringLiteral("Project > Text Statistics opens the window"));
        if (!ts)
            return 1;
        auto *scope = ts->findChild<QComboBox *>(QStringLiteral("tsScope"));
        auto value = [&](const QString &key) {
            QString v;
            QMetaObject::invokeMethod(ts, "overviewValue", Q_RETURN_ARG(QString, v), Q_ARG(QString, key));
            return v;
        };
        const QString selText = ed()->textCursor().selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        check(scope->currentData().toInt() == int(tools::Scope::Selection) && value(QStringLiteral("words")) == QString::number(text::countWords(selText))
                  && value(QStringLiteral("sentences")) == QLatin1String("5") && value(QStringLiteral("paragraphs")) == QLatin1String("2"),
              QStringLiteral("with text selected it counts the selection: words as in the footer bar, 5 sentences, 2 paragraphs"),
              value(QStringLiteral("words")) + QLatin1Char('/') + value(QStringLiteral("sentences")));
        QMetaObject::invokeMethod(ts, "setScope", Q_ARG(int, int(tools::Scope::Draft)));
        const int draftWords = text::countWords(proj->docs()->plainText(ch1->id)) + text::countWords(proj->docs()->plainText(ch2->id))
            + text::countWords(proj->docs()->plainText(proj->draft()->children.last()->id));
        check(call<int>(ts, "documentRows") == 3 && value(QStringLiteral("words")) == QLocale().toString(draftWords),
              QStringLiteral("Draft: three documents (the note sent there included), words add up"),
              QStringLiteral("%1 rows, %2 words, expected %3").arg(call<int>(ts, "documentRows")).arg(value(QStringLiteral("words"))).arg(draftWords));
        QMetaObject::invokeMethod(ts, "setScope", Q_ARG(int, int(tools::Scope::Document)));
        int keeper = 0, the = 0;
        QMetaObject::invokeMethod(ts, "wordCountOf", Q_RETURN_ARG(int, keeper), Q_ARG(QString, QStringLiteral("keeper")));
        QMetaObject::invokeMethod(ts, "wordCountOf", Q_RETURN_ARG(int, the), Q_ARG(QString, QStringLiteral("the")));
        QStringList top3;
        QMetaObject::invokeMethod(ts, "topWords", Q_RETURN_ARG(QStringList, top3), Q_ARG(int, 3));
        check(keeper == 3 && the == 0 && top3.value(0) == QLatin1String("keeper=3"),
              QStringLiteral("word frequency: \"keeper\" 3 times at the top; common words like \"the\" hidden"), top3.join(QLatin1String(", ")));
        ts->findChild<QCheckBox *>(QStringLiteral("tsHideCommon"))->setChecked(false);
        QMetaObject::invokeMethod(ts, "wordCountOf", Q_RETURN_ARG(int, the), Q_ARG(QString, QStringLiteral("the")));
        ts->findChild<QCheckBox *>(QStringLiteral("tsHideCommon"))->setChecked(true);
        check(the == 7, QStringLiteral("with common words shown: \"the\" 7 times"), QString::number(the));
        const QStringList phr = call<QStringList>(ts, "phraseList");
        check(phr.contains(QStringLiteral("The keeper=2")) && !phr.join(QLatin1Char('|')).contains(QLatin1String("lit the")),
              QStringLiteral("repeated phrases: \"The keeper\" twice; phrases used once are left out"), phr.join(QLatin1String(", ")));
        // whole project: "The old keeper waited for the morning boat" in two documents
        QMetaObject::invokeMethod(ts, "setScope", Q_ARG(int, int(tools::Scope::Project)));
        const QStringList phr2 = call<QStringList>(ts, "phraseList");
        check(phr2.contains(QStringLiteral("The old keeper waited=2")),
              QStringLiteral("whole project: the sentence repeated in two chapters shows up as a repeated phrase"), phr2.mid(0, 4).join(QLatin1String(", ")));
        check(!value(QStringLiteral("fleschEase")).isEmpty() && !value(QStringLiteral("fleschKincaid")).isEmpty() && !value(QStringLiteral("lix")).isEmpty(),
              QStringLiteral("English text: Flesch Reading Ease, Flesch-Kincaid and LIX"), value(QStringLiteral("fleschEase")));
        shot(ts, QStringLiteral("m10-text-statistics"));
        auto *tabs = ts->findChild<QTabWidget *>(QStringLiteral("tsTabs"));
        tabs->setCurrentIndex(1);
        shot(ts, QStringLiteral("m10-text-statistics-words"));
        tabs->setCurrentIndex(3);
        shot(ts, QStringLiteral("m10-text-statistics-documents"));
        // a folder on the corkboard: "text in the editor" is the documents inside it
        binder->select({proj->draft()}, true);
        settle(300);
        w.findChild<QAction *>(QStringLiteral("viewCork"))->trigger();
        settle(200);
        QMetaObject::invokeMethod(ts, "setScope", Q_ARG(int, int(tools::Scope::Document)));
        check(call<int>(ts, "documentRows") == 3, QStringLiteral("with a folder on the corkboard, \"text in the editor\" counts the documents inside it"),
              QString::number(call<int>(ts, "documentRows")));
        w.findChild<QAction *>(QStringLiteral("viewText"))->trigger();
        settle(200);
        // export
        const QString csv = home.path() + QStringLiteral("/docs.csv");
        bool exported = false;
        QMetaObject::invokeMethod(ts, "exportCsv", Q_RETURN_ARG(bool, exported), Q_ARG(QString, csv), Q_ARG(int, 3));
        const QString csvText = fileText(csv);
        QFile raw(csv);
        raw.open(QIODevice::ReadOnly);
        check(exported && raw.read(3) == QByteArray("\xEF\xBB\xBF") && csvText.contains(QLatin1String("Document,Folder,Words")) && csvText.contains(QLatin1String("The Keeper")),
              QStringLiteral("Export Table writes a CSV file (UTF-8 for spreadsheets)"), csvText.left(120));
        // German text
        binder->select({german}, true);
        settle(300);
        QMetaObject::invokeMethod(ts, "setScope", Q_ARG(int, int(tools::Scope::Document)));
        check(value(QStringLiteral("language")) == QLatin1String("German") && !value(QStringLiteral("amstad")).isEmpty() && !value(QStringLiteral("wstf")).isEmpty(),
              QStringLiteral("German text: language found, Amstad's Flesch index and the Wiener Sachtextformel"), value(QStringLiteral("amstad")));
        // double-click a document row opens it, a word searches the project
        QMetaObject::invokeMethod(ts, "setScope", Q_ARG(int, int(tools::Scope::Draft)));
        auto *docs = ts->findChild<QTableWidget *>(QStringLiteral("tsDocs"));
        int row = -1;
        for (int r = 0; r < docs->rowCount(); ++r)
            if (docs->item(r, 0)->text() == QLatin1String("The Storm"))
                row = r;
        emit docs->cellDoubleClicked(row, 0);
        settle(300);
        check(pane0->items().value(0) == ch2, QStringLiteral("double-click on a document in the table opens it in the editor"));
        auto *words = ts->findChild<QTableWidget *>(QStringLiteral("tsWords"));
        emit words->cellDoubleClicked(0, 0);
        settle(300);
        check(binder->isSearching() && binder->searchField()->text() == words->item(0, 0)->text(),
              QStringLiteral("double-click on a word starts a project search for it"), binder->searchField()->text());
        binder->hideSearch();
        // closing the project closes the window, the command waits for a project
        QPointer<QWidget> tsp(ts);
        sp->close();
        settle(200);
        w.closeProject();
        settle(300);
        check(!tsp && !tsAction->isEnabled() && spAction->isEnabled(),
              QStringLiteral("closing the project closes Text Statistics and disables it; the scratch pad stays available"));
    }

    // ================================================================ desktop entry
    {
        withModal<QMessageBox>(800, [](QMessageBox *mb) { mb->accept(); });
        w.findChild<QAction *>(QStringLiteral("integrate"))->trigger();
        settle(300);
        const QString desktop = fileText(home.path() + QStringLiteral("/data/applications/quillbinder.desktop"));
        check(desktop.contains(QLatin1String("Actions=scratch-pad-show;")) && desktop.contains(QLatin1String("[Desktop Action scratch-pad-show]"))
                  && desktop.contains(QLatin1String("--tool scratch-pad.show")),
              QStringLiteral("Help > Add to Application Menu: the menu entry has the action \"Scratch Pad\" (a desktop shortcut can call it)"));
    }

    std::printf("\n%d failure(s)\n", failures);
    QTimer::singleShot(0, &app, [&] { w.close(); });
    delete wp;
    return failures;
}
