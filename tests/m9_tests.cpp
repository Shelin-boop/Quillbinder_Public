// Scripted run for scriptwriting: script mode, the Tab and Enter keys,
// auto-completion, the footer bar, script formats and Script Settings,
// compile and export (milestone M9). Saves screenshots into the folder given
// as first argument. Exit code = number of failed checks.
//
//   QT_QPA_PLATFORM=offscreen ./qb_m9_tests /tmp/shots
//   xvfb-run -a -s "-screen 0 1920x1080x24" ./qb_m9_tests /tmp/shots
#include "compile/PluginManager.h"
#include "core/Addons.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "text/Script.h"
#include "text/ScriptIO.h"
#include "text/TextProps.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/Commands.h"
#include "ui/CompileDialog.h"
#include "ui/DragDrop.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/QuickRef.h"
#include "ui/ScriptDialogs.h"
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
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontInfo>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
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

static QAction *menuAction(QMenu *menu, const QString &text)
{
    for (QAction *a : menu->actions())
        if (menuText(a->text()) == text)
            return a;
    std::printf("      (menu entry not found: %s)\n", qPrintable(text));
    return nullptr;
}

// Runs fn on the modal window of type T that opens after the call that follows.
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

// "Scene Heading:INT. HOUSE|Action:..." (elements and texts of a document)
static QString elements(const QTextDocument *doc)
{
    QStringList out;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QString style = b.blockFormat().stringProperty(props::Style);
        out << (script::isScriptStyle(style) ? script::elementName(style) : style) + QLatin1Char(':') + b.text();
    }
    return out.join(QLatin1Char('|'));
}

static void type(TextEditor *e, const QString &text)
{
    QTest::keyClicks(e, text);
    settle(60);
}

static void key(TextEditor *e, Qt::Key k, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QTest::keyClick(e, k, mods);
    settle(60);
}

static QString fileText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

static QMenu *openPopupMenu(const QString &objectName)
{
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (auto *m = qobject_cast<QMenu *>(tl))
            if (m->isVisible() && m->objectName() == objectName)
                return m;
    return nullptr;
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderM9Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderM9Test"));
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
    as.smartQuotes = false;
    theme::apply();
    compile::PluginManager::instance().load();

    // ---------------------------------------------------------------- project
    QTemporaryDir projectsTmp;
    const QString projectsPath = argc > 2 ? QString::fromLocal8Bit(argv[2]) : projectsTmp.path();
    QDir().mkpath(projectsPath);
    templates::Info blank;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("novel"))
            blank = t;
    QString err;
    auto p = templates::createProject(blank, projectsPath, QStringLiteral("The Lighthouse Script"), &err);
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
    m->moveToTrash(QList<BinderItem *>(proj->draft()->children));
    settle(200);
    BinderItem *act = m->createItem(proj->draft(), 0, ItemType::Folder, QStringLiteral("Act One"));
    BinderItem *scene1 = m->createItem(act, 0, ItemType::Text, QStringLiteral("The Letter"));
    BinderItem *scene2 = m->createItem(act, 1, ItemType::Text, QStringLiteral("The Quay"));
    BinderItem *prose = m->createItem(proj->research(), 0, ItemType::Text, QStringLiteral("Treatment"));
    templates::setMarkdown(proj, prose, QStringLiteral("Click into this text.\nA keeper finds a letter that was never sent.\n"));
    const QString scene1Id = scene1->id, scene2Id = scene2->id, proseId = prose->id, actId = act->id;
    m->refreshAll();
    proj->save();
    settle(300);

    auto *binder = w.findChild<BinderPanel *>();
    auto *editorSplit = qobject_cast<QSplitter *>(w.findChild<EditorPane *>()->parentWidget());
    auto *pane0 = editorSplit ? qobject_cast<EditorPane *>(editorSplit->widget(0)) : nullptr;
    check(binder && pane0, QStringLiteral("window parts found"));
    if (!binder || !pane0)
        return 1;
    binder->tree()->expandAll();
    auto ed = [&]() -> TextEditor * { return pane0->textView()->editors().value(0); };
    auto *elementButton = pane0->findChild<QToolButton *>(QStringLiteral("scriptElementButton"));
    auto *hint = pane0->findChild<QLabel *>(QStringLiteral("scriptHint"));
    QAction *modeAction = byKey(&w, QStringLiteral("scriptMode"));
    auto elementShown = [&]() { return elementButton ? elementButton->text().section(QStringLiteral(" \u25BE"), 0, 0) : QString(); };
    auto lastBlock = [&]() { return ed()->document()->lastBlock().text(); };
    auto here = [&]() { return ed()->scriptElementAtCursor(); };

    // ================================================================ menus
    {
        emit w.menuBar()->actions().first()->menu()->aboutToShow();
        QStringList missing;
        const QList<QStringList> places = {{"Format", "Scriptwriting", "Script Mode - Screenplay"},
                                           {"Format", "Scriptwriting", "Change Element To", "Scene Heading"},
                                           {"Format", "Scriptwriting", "Change Element To", "Dialogue"},
                                           {"Format", "Scriptwriting", "Change Element To", "General Text"},
                                           {"Format", "Scriptwriting", "Show the List of Elements"},
                                           {"Format", "Scriptwriting", "Script Format", "Screenplay"},
                                           {"Format", "Scriptwriting", "Script Format", "Stage Play"},
                                           {"Format", "Scriptwriting", "Script Format", "Comic Script"},
                                           {"Format", "Scriptwriting", "Script Settings…"}};
        for (const QStringList &pl : places)
            if (!menuPath(&w, pl))
                missing << pl.join(QLatin1String(" > "));
        check(missing.isEmpty(), QStringLiteral("Format > Scriptwriting has Script Mode - Screenplay, the elements, the formats and Script Settings"),
              missing.join(QLatin1String("; ")));
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
        bool listed = false, elementListed = false;
        for (const commands::Command &c : commands::all()) {
            listed = listed || c.key == QLatin1String("scriptMode");
            elementListed = elementListed || c.key == QLatin1String("script:Character");
        }
        check(twice.isEmpty() && modeAction && modeAction->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_8) && listed && elementListed,
              QStringLiteral("Script Mode is Ctrl+8; no shortcut is used twice; the elements are commands that can get shortcuts"),
              twice.join(QLatin1String("; ")));
    }

    // ================================================================ script mode
    binder->select({prose}, true);
    settle(300);
    const QImage plainIcon = icons::forItem(proj, scene1).pixmap(32, 32).toImage();
    const QImage proseIcon = icons::forItem(proj, prose).pixmap(32, 32).toImage();
    // how yellow a picture is: red and green well above blue
    auto yellow = [](const QImage &img) {
        qint64 rg = 0, b = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const QColor c = img.pixelColor(x, y);
                if (c.alpha() < 40)
                    continue;
                rg += (c.red() + c.green()) / 2;
                b += c.blue();
            }
        return b ? double(rg) / double(b) : 0.0;
    };
    check(ed() && !ed()->scriptActive() && elementButton && !elementButton->isVisible() && hint && !hint->isVisible()
              && modeAction->isEnabled() && !modeAction->isChecked(),
          QStringLiteral("a text document is not a script: the footer shows the word count"));

    binder->select({scene1}, true);
    settle(300);
    ed()->setFocus();
    settle(100);
    QTest::keySequence(w.windowHandle(), QKeySequence(Qt::CTRL | Qt::Key_8));
    settle(300);
    const QImage scriptIcon = icons::forItem(proj, scene1).pixmap(32, 32).toImage();
    check(scene1->scriptMode && ed()->scriptActive() && modeAction->isChecked() && elementButton->isVisible() && hint->isVisible()
              && elementShown() == QLatin1String("Scene Heading") && here() == QLatin1String("Scene Heading"),
          QStringLiteral("Ctrl+8 switches script mode on: the footer shows the element, an empty script starts with a scene heading"),
          elementShown() + QLatin1String(" / ") + here());
    check(scriptIcon != plainIcon && yellow(scriptIcon) > yellow(plainIcon) * 1.15
              && icons::forItem(proj, prose).pixmap(32, 32).toImage() == proseIcon,
          QStringLiteral("script documents have a yellow icon, other documents keep theirs"),
          QStringLiteral("yellow %1 against %2").arg(yellow(scriptIcon)).arg(yellow(plainIcon)));
    check(hint->text().contains(QLatin1String("Tab: Action")) && hint->text().contains(QLatin1String("Enter: list of elements")),
          QStringLiteral("the footer says what Tab and Enter will do"), hint->text());

    // ================================================================ typing a scene
    {
        type(ed(), QStringLiteral("in"));
        const QStringList offered = ed()->scriptCompletions();
        const bool popup = ed()->scriptCompletionVisible();
        key(ed(), Qt::Key_Return); // takes "INT. "
        const QString afterPick = lastBlock();
        type(ed(), QStringLiteral("lighthouse"));
        const QString tabHint = ed()->scriptTabHint();
        key(ed(), Qt::Key_Tab); // types " - " and offers the times of day
        const QStringList times = ed()->scriptCompletions(true);
        const bool popup2 = ed()->scriptCompletionVisible();
        type(ed(), QStringLiteral("n"));
        const QStringList nTimes = ed()->scriptCompletions();
        key(ed(), Qt::Key_Return);
        check(offered == QStringList({QStringLiteral("INT. "), QStringLiteral("INT./EXT. ")}) && popup && afterPick == QLatin1String("INT. "),
              QStringLiteral("scene heading: \"in\" offers INT. and INT./EXT.; Enter takes the first"),
              offered.join(QLatin1Char('|')) + QStringLiteral(" popup %1 '%2'").arg(popup).arg(afterPick));
        check(tabHint.contains(QLatin1String(" - ")) && times.contains(QStringLiteral("DAY")) && times.contains(QStringLiteral("NIGHT")) && popup2
                  && nTimes == QStringList({QStringLiteral("NIGHT")}) && lastBlock() == QLatin1String("INT. LIGHTHOUSE - NIGHT"),
              QStringLiteral("scene heading: what is typed becomes capitals; Tab types \" - \" and offers the times of day"),
              tabHint + QLatin1String(" / ") + times.join(QLatin1Char('|')) + QLatin1String(" / ") + lastBlock());
        const QString enterHint = ed()->scriptEnterHint();
        key(ed(), Qt::Key_Return);
        check(enterHint == QLatin1String("Action") && here() == QLatin1String("Action") && ed()->document()->blockCount() == 2
                  && elementShown() == QLatin1String("Action"),
              QStringLiteral("Enter at the end of a scene heading starts an action paragraph; the footer follows"),
              enterHint + QLatin1String(" / ") + here() + QLatin1String(" / ") + elementShown());
        type(ed(), QStringLiteral("Rain against the glass."));
        key(ed(), Qt::Key_Return);
        const QString afterAction = here();
        key(ed(), Qt::Key_Tab); // empty line: Action -> Character
        const QString afterTab = here();
        type(ed(), QStringLiteral("marta"));
        const QString name = lastBlock();
        const QString tabEnd = ed()->scriptTabHint();
        key(ed(), Qt::Key_Tab); // after the name: a parenthetical
        const QString paren0 = lastBlock();
        const int inParen = ed()->textCursor().positionInBlock();
        type(ed(), QStringLiteral("low"));
        const QString paren = lastBlock();
        key(ed(), Qt::Key_Return);
        const QString afterParen = here();
        type(ed(), QStringLiteral("Who left this here?"));
        check(afterAction == QLatin1String("Action") && afterTab == QLatin1String("Character") && name == QLatin1String("MARTA")
                  && tabEnd == QLatin1String("Parenthetical") && paren0 == QLatin1String("()") && inParen == 1 && paren == QLatin1String("(low)")
                  && afterParen == QLatin1String("Dialogue"),
              QStringLiteral("Tab on an empty action line gives a character; Tab after the name a parenthetical with the cursor inside; Enter the dialogue"),
              QStringLiteral("%1 / %2 / %3 / %4 / '%5' %6 / %7 / %8").arg(afterAction, afterTab, name, tabEnd, paren0).arg(inParen).arg(paren, afterParen));
        const QTextBlock dlg = ed()->document()->lastBlock();
        const QTextBlock chr = ed()->document()->findBlockByNumber(2);
        const QFont headFont = ed()->document()->begin().begin().fragment().charFormat().font();
        const QFont dlgFont = dlg.begin().fragment().charFormat().font();
        check(headFont.families() == dlgFont.families() && QFontInfo(headFont).fixedPitch(),
              QStringLiteral("the first paragraph of a new script is in the typewriter font as well"),
              headFont.families().join(QLatin1Char(',')) + QLatin1String(" / ") + dlgFont.families().join(QLatin1Char(',')));
        check(qAbs(dlg.blockFormat().leftMargin() - 96.0) < 2.5
                  && qAbs(chr.blockFormat().leftMargin() - 2.2 * 96.0) < 3.5,
              QStringLiteral("the paragraphs have the indents of the screenplay format"),
              QStringLiteral("dialogue %1 character %2").arg(dlg.blockFormat().leftMargin()).arg(chr.blockFormat().leftMargin()));

        // Tab at the end of dialogue: parenthetical; Tab on the bare "()" goes on to dialogue
        key(ed(), Qt::Key_Tab);
        const QString p1 = here() + QLatin1Char(':') + lastBlock();
        key(ed(), Qt::Key_Tab);
        const QString p2 = here() + QLatin1Char(':') + lastBlock();
        key(ed(), Qt::Key_Backtab);
        const QString p3 = here() + QLatin1Char(':') + lastBlock();
        key(ed(), Qt::Key_Tab);
        type(ed(), QStringLiteral("Tom?"));
        key(ed(), Qt::Key_Return);
        check(p1 == QLatin1String("Parenthetical:()") && p2 == QLatin1String("Dialogue:") && p3 == QLatin1String("Parenthetical:()")
                  && here() == QLatin1String("Action"),
              QStringLiteral("Tab toggles an empty line between elements (the parentheses come and go), Shift+Tab goes back"),
              p1 + QLatin1String(" / ") + p2 + QLatin1String(" / ") + p3 + QLatin1String(" / ") + here());

        // Enter on an empty line: the list of elements; a letter chooses
        key(ed(), Qt::Key_Return);
        settle(200);
        QMenu *list = openPopupMenu(QStringLiteral("scriptElementsMenu"));
        const QStringList entries = list ? menuTexts(list) : QStringList();
        if (list) {
            list->grab().save(outDir + QLatin1String("/m9-02-element-list.png"));
            QTest::keyClick(list, Qt::Key_T);
            settle(200);
        }
        check(list && entries == QStringList({QStringLiteral("Scene Heading"), QStringLiteral("Action"), QStringLiteral("Character"),
                                              QStringLiteral("Parenthetical"), QStringLiteral("Dialogue"), QStringLiteral("Transition"),
                                              QStringLiteral("Shot"), QStringLiteral("General Text")})
                  && here() == QLatin1String("Transition") && ed()->document()->blockCount() == 7,
              QStringLiteral("Enter on an empty line opens the list of elements; the letter T makes the line a transition"),
              entries.join(QLatin1Char('|')) + QLatin1String(" / ") + here() + QStringLiteral(" / %1 blocks").arg(ed()->document()->blockCount()));
        type(ed(), QStringLiteral("cu"));
        const QStringList cuts = ed()->scriptCompletions();
        key(ed(), Qt::Key_Tab); // Tab takes a completion as well
        const QTextBlock trBlock = ed()->document()->lastBlock();
        check(cuts == QStringList({QStringLiteral("CUT TO:")}) && trBlock.text() == QLatin1String("CUT TO:")
                  && (trBlock.blockFormat().alignment() & Qt::AlignRight),
              QStringLiteral("transition: \"cu\" completes to CUT TO: and stands on the right"), cuts.join(QLatin1Char('|')) + QLatin1Char(' ') + trBlock.text());
        key(ed(), Qt::Key_Return);
        type(ed(), QStringLiteral("int. l"));
        const QStringList places = ed()->scriptCompletions();
        key(ed(), Qt::Key_Escape);
        const bool closed = !ed()->scriptCompletionVisible();
        type(ed(), QStringLiteral("amp room"));
        key(ed(), Qt::Key_Return);
        key(ed(), Qt::Key_Tab);
        type(ed(), QStringLiteral("m"));
        const QStringList people = ed()->scriptCompletions();
        key(ed(), Qt::Key_Return);
        check(places.contains(QStringLiteral("INT. LIGHTHOUSE")) && closed && people == QStringList({QStringLiteral("MARTA")})
                  && lastBlock() == QLatin1String("MARTA") && here() == QLatin1String("Character"),
              QStringLiteral("places and characters already in the script are offered; Escape closes the list"),
              places.join(QLatin1Char('|')) + QLatin1String(" / ") + people.join(QLatin1Char('|')) + QLatin1String(" / ") + lastBlock());
        key(ed(), Qt::Key_Return);
        type(ed(), QStringLiteral("It was never sent."));
        const QString all = elements(ed()->document());
        check(all == QLatin1String("Scene Heading:INT. LIGHTHOUSE - NIGHT|Action:Rain against the glass.|Character:MARTA|Parenthetical:(low)|"
                                   "Dialogue:Who left this here?|Dialogue:Tom?|Transition:CUT TO:|Scene Heading:INT. LAMP ROOM|Character:MARTA|"
                                   "Dialogue:It was never sent."),
              QStringLiteral("the scene as typed"), all);
        shot(&w, QStringLiteral("m9-01-screenplay"));

        // Enter in the middle of a paragraph splits it, both halves keep the element
        QTextCursor c = ed()->textCursor();
        c.setPosition(ed()->document()->lastBlock().position() + 6);
        ed()->setTextCursor(c);
        const QString midEnter = ed()->scriptEnterHint();
        const QString midTab = ed()->scriptTabHint();
        key(ed(), Qt::Key_Tab);
        const QString unchanged = lastBlock();
        key(ed(), Qt::Key_Return);
        const QString split = elements(ed()->document()).section(QLatin1Char('|'), -2);
        ed()->undo();
        settle(100);
        check(midEnter == QLatin1String("Dialogue") && midTab.isEmpty() && unchanged == QLatin1String("It was never sent.")
                  && split == QLatin1String("Dialogue:It was|Dialogue: never sent.") && lastBlock() == QLatin1String("It was never sent."),
              QStringLiteral("inside a paragraph Tab does nothing and Enter splits it into two of the same element; Undo joins them"),
              midEnter + QLatin1String(" / '") + midTab + QLatin1String("' / ") + split + QLatin1String(" / ") + lastBlock());

        // Undo takes an element change back
        c = ed()->textCursor();
        c.movePosition(QTextCursor::End);
        ed()->setTextCursor(c);
        key(ed(), Qt::Key_Return); // -> Action
        key(ed(), Qt::Key_Tab);    // -> Character
        const QString before = here();
        ed()->undo();
        settle(100);
        const QString undone = here();
        ed()->undo();
        settle(100);
        check(before == QLatin1String("Character") && undone == QLatin1String("Action") && ed()->document()->blockCount() == 10,
              QStringLiteral("Undo takes back an element change, then the new paragraph"),
              before + QLatin1String(" / ") + undone + QStringLiteral(" / %1").arg(ed()->document()->blockCount()));
    }

    // ================================================================ footer menu, Change Element To
    {
        QTextCursor c = ed()->textCursor();
        c.setPosition(ed()->document()->findBlockByNumber(5).position() + 2); // "Tom?"
        ed()->setTextCursor(c);
        settle(150);
        const QString shown = elementShown();
        emit elementButton->menu()->aboutToShow();
        const QStringList entries = menuTexts(elementButton->menu());
        QAction *checkedOne = nullptr;
        for (QAction *a : elementButton->menu()->actions())
            if (a->isChecked())
                checkedOne = a;
        const QString ticked = checkedOne ? menuText(checkedOne->text()) : QString();
        if (QAction *a = menuAction(elementButton->menu(), QStringLiteral("Parenthetical")))
            a->trigger();
        settle(150);
        const QString asParen = ed()->document()->findBlockByNumber(5).text();
        emit elementButton->menu()->aboutToShow();
        if (QAction *a = menuAction(elementButton->menu(), QStringLiteral("Character")))
            a->trigger();
        settle(150);
        const QString asChar = here() + QLatin1Char(':') + ed()->document()->findBlockByNumber(5).text();
        emit elementButton->menu()->aboutToShow();
        if (QAction *a = menuAction(elementButton->menu(), QStringLiteral("Dialogue")))
            a->trigger();
        settle(150);
        check(shown == QLatin1String("Dialogue") && entries.size() == 8 && entries.last() == QLatin1String("General Text") && ticked == QLatin1String("Dialogue")
                  && asParen == QLatin1String("(Tom?)") && asChar == QLatin1String("Character:Tom?")
                  && ed()->document()->findBlockByNumber(5).text() == QLatin1String("Tom?") && elementShown() == QLatin1String("Dialogue"),
              QStringLiteral("the pop-up menu in the footer shows the element at the cursor and changes it; parentheses are added and taken away"),
              shown + QLatin1String(" / ") + ticked + QLatin1String(" / ") + asParen + QLatin1String(" / ") + asChar);

        // several paragraphs at once through Format > Scriptwriting > Change Element To
        c.setPosition(ed()->document()->findBlockByNumber(4).position() + 1);
        c.setPosition(ed()->document()->findBlockByNumber(5).position() + 2, QTextCursor::KeepAnchor);
        ed()->setTextCursor(c);
        byKey(&w, QStringLiteral("script:Action"))->trigger();
        settle(150);
        const QString two = elements(ed()->document()).section(QLatin1Char('|'), 4, 5);
        ed()->undo();
        settle(150);
        check(two == QLatin1String("Action:Who left this here?|Action:Tom?")
                  && elements(ed()->document()).section(QLatin1Char('|'), 4, 5) == QLatin1String("Dialogue:Who left this here?|Dialogue:Tom?"),
              QStringLiteral("Change Element To changes all selected paragraphs; one Undo takes it back"), two);
        QAction *el = byKey(&w, QStringLiteral("script:Dialogue"));
        emit w.findChild<QMenu *>(QStringLiteral("scriptMenu"))->aboutToShow();
        check(el && el->isChecked() && !byKey(&w, QStringLiteral("script:Action"))->isChecked(),
              QStringLiteral("the menu ticks the element at the cursor"));
    }

    // ================================================================ per document; general text
    {
        proj->save();
        settle(200);
        const QString stored = fileText(proj->docs()->textPath(scene1Id));
        check(stored.contains(QLatin1String("::Script > Scene Heading:: INT. LIGHTHOUSE - NIGHT\n"))
                  && stored.contains(QLatin1String("::Script > Parenthetical:: (low)\n")) && !stored.contains(QLatin1String("font-family")),
              QStringLiteral("the file of the script names the elements and nothing else"), stored);

        binder->select({prose}, true);
        settle(300);
        ed()->setFocus();
        emit w.findChild<QMenu *>(QStringLiteral("scriptMenu"))->aboutToShow();
        const bool proseOff = !modeAction->isChecked() && !elementButton->isVisible();
        QTextCursor c = ed()->textCursor();
        c.movePosition(QTextCursor::End);
        ed()->setTextCursor(c);
        key(ed(), Qt::Key_Tab);
        const bool tabChar = ed()->document()->lastBlock().text().endsWith(QLatin1Char('\t'));
        ed()->undo();
        // the tutorial: switch script mode on in a text and choose elements for a line
        c.setPosition(3);
        ed()->setTextCursor(c);
        modeAction->trigger();
        settle(200);
        const QString general = elementShown();
        const QString generalTab = ed()->scriptTabHint();
        emit elementButton->menu()->aboutToShow();
        if (QAction *a = menuAction(elementButton->menu(), QStringLiteral("Character")))
            a->trigger();
        settle(150);
        const QTextBlock first = ed()->document()->begin();
        const double left = first.blockFormat().leftMargin();
        const QString second = ed()->document()->findBlockByNumber(1).blockFormat().stringProperty(props::Style);
        emit elementButton->menu()->aboutToShow();
        if (QAction *a = menuAction(elementButton->menu(), QStringLiteral("General Text")))
            a->trigger();
        settle(150);
        check(proseOff && tabChar && prose->scriptMode && general == QLatin1String("General Text") && generalTab.isEmpty()
                  && left > 150 && second.isEmpty() && elementShown() == QLatin1String("General Text")
                  && ed()->document()->begin().blockFormat().leftMargin() < 5,
              QStringLiteral("script mode is per document; text that is no element is \"General Text\" and is reformatted by the footer menu"),
              QStringLiteral("off %1 tab %2 '%3' left %4 '%5' -> %6").arg(proseOff).arg(tabChar).arg(general).arg(left).arg(second)
                  .arg(ed()->document()->begin().blockFormat().leftMargin()));
        modeAction->trigger(); // off again
        settle(200);
        check(!prose->scriptMode && !elementButton->isVisible() && icons::forItem(proj, prose).pixmap(32, 32).toImage() == proseIcon,
              QStringLiteral("Script Mode again switches it off"));

        // a command for an element switches script mode on
        binder->select({scene2}, true);
        settle(300);
        ed()->setFocus();
        byKey(&w, QStringLiteral("script:Scene Heading"))->trigger();
        settle(200);
        type(ed(), QStringLiteral("ext. quay"));
        key(ed(), Qt::Key_Tab);
        type(ed(), QStringLiteral("da"));
        key(ed(), Qt::Key_Return);
        key(ed(), Qt::Key_Return);
        type(ed(), QStringLiteral("The boat is in."));
        check(scene2->scriptMode && elements(ed()->document()) == QLatin1String("Scene Heading:EXT. QUAY - DAY|Action:The boat is in."),
              QStringLiteral("choosing an element in a plain document makes it a script"), elements(ed()->document()));
    }

    // ================================================================ Combined text, Quick Reference
    {
        binder->select({act}, true);
        settle(300);
        pane0->setViewMode(ViewMode::Combined);
        settle(400);
        const QList<TextEditor *> eds = pane0->textView()->editors();
        TextEditor *second = nullptr;
        for (TextEditor *e : eds)
            if (e->item() == scene2)
                second = e;
        bool ok = false;
        if (second) {
            w.activateWindow(); // the completion lists were windows of their own
            settle(150);
            second->setFocus();
            QTextCursor c = second->textCursor();
            c.movePosition(QTextCursor::End);
            second->setTextCursor(c);
            settle(150);
            const QString shown = elementShown();
            key(second, Qt::Key_Return);
            key(second, Qt::Key_Tab);
            type(second, QStringLiteral("tom"));
            ok = shown == QLatin1String("Action") && second->scriptElementAtCursor() == QLatin1String("Character")
                 && second->document()->lastBlock().text() == QLatin1String("TOM") && elementShown() == QLatin1String("Character");
        }
        check(eds.size() >= 2 && ok, QStringLiteral("in Combined text each script keeps its elements, and the keys work in the section with the cursor"),
              QStringLiteral("%1 editors, footer '%2'").arg(eds.size()).arg(elementShown()));
        shot(&w, QStringLiteral("m9-03-combined-text"));

        pane0->setViewMode(ViewMode::Single);
        binder->select({scene2}, true);
        settle(300);
        byKey(&w, QStringLiteral("quickRef"))->trigger();
        settle(400);
        QuickRefWindow *qr = nullptr;
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *q = qobject_cast<QuickRefWindow *>(tl))
                if (q->isVisible())
                    qr = q;
        TextEditor *qe = qr ? qr->textView()->editors().value(0) : nullptr;
        bool qok = false;
        if (qe) {
            qe->setFocus();
            QTextCursor c = qe->textCursor();
            c.movePosition(QTextCursor::End);
            qe->setTextCursor(c);
            key(qe, Qt::Key_Return);
            type(qe, QStringLiteral("Marta."));
            qok = qe->scriptActive() && qe->scriptElementAtCursor() == QLatin1String("Dialogue");
        }
        check(qok && elements(proj->docs()->text(scene2Id)) == QLatin1String("Scene Heading:EXT. QUAY - DAY|Action:The boat is in.|Character:TOM|Dialogue:Marta."),
              QStringLiteral("a script in a Quick Reference panel is written with the same keys"), elements(proj->docs()->text(scene2Id)));
        if (qr)
            qr->close();
        settle(200);

        // composition mode
        w.activateWindow();
        settle(150);
        byKey(&w, QStringLiteral("composition"))->trigger();
        settle(700);
        CompositionWindow *cw = nullptr;
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *c = qobject_cast<CompositionWindow *>(tl))
                if (c->isVisible())
                    cw = c;
        TextEditor *ce = cw ? cw->view()->editors().value(0) : nullptr;
        bool cok = false;
        if (ce) {
            ce->setFocus();
            QTextCursor c = ce->textCursor();
            c.movePosition(QTextCursor::End);
            ce->setTextCursor(c);
            key(ce, Qt::Key_Return);
            const QString afterEnter = ce->scriptElementAtCursor();
            type(ce, QStringLiteral("He waits."));
            cok = ce->scriptActive() && afterEnter == QLatin1String("Action");
        }
        if (cw)
            cw->close();
        settle(400);
        check(cok && elements(proj->docs()->text(scene2Id)).endsWith(QLatin1String("|Dialogue:Marta.|Action:He waits.")),
              QStringLiteral("a script in composition mode is written with the same keys"), elements(proj->docs()->text(scene2Id)));
        w.activateWindow();
        settle(200);
        pane0->setViewMode(ViewMode::Single);
        binder->select({scene1}, true);
        settle(300);
    }

    // ================================================================ new, duplicated and split documents
    {
        ed()->setFocus();
        const QList<BinderItem *> before = act->children;
        byKey(&w, QStringLiteral("newText"))->trigger();
        settle(400);
        BinderItem *fresh = nullptr;
        for (BinderItem *c : act->children)
            if (!before.contains(c))
                fresh = c;
        const QString freshStyle = fresh ? proj->docs()->text(fresh->id)->begin().blockFormat().stringProperty(props::Style) : QString();
        binder->select({prose}, true);
        settle(300);
        const QList<BinderItem *> beforeR = proj->research()->children;
        byKey(&w, QStringLiteral("newText"))->trigger();
        settle(400);
        BinderItem *plainNew = nullptr;
        for (BinderItem *c : proj->research()->children)
            if (!beforeR.contains(c))
                plainNew = c;
        check(fresh && fresh->scriptMode && freshStyle == QLatin1String("Script > Scene Heading") && plainNew && !plainNew->scriptMode,
              QStringLiteral("a new document among scripts is a script and starts with a scene heading; elsewhere it is plain text"), freshStyle);

        binder->select({scene1}, true);
        settle(300);
        byKey(&w, QStringLiteral("duplicate"))->trigger();
        settle(400);
        BinderItem *copy = nullptr;
        for (BinderItem *c : act->children)
            if (c->title == QLatin1String("The Letter (copy)"))
                copy = c;
        bool splitOk = false;
        QString halves;
        if (copy) {
            binder->select({copy}, true);
            settle(300);
            ed()->setFocus();
            QTextCursor c = ed()->textCursor();
            c.setPosition(ed()->document()->findBlockByNumber(7).position()); // before "INT. LAMP ROOM"
            ed()->setTextCursor(c);
            const QList<BinderItem *> b2 = act->children;
            byKey(&w, QStringLiteral("split"))->trigger();
            settle(400);
            BinderItem *rest = nullptr;
            for (BinderItem *x : act->children)
                if (!b2.contains(x))
                    rest = x;
            if (rest) {
                halves = elements(proj->docs()->text(copy->id)).section(QLatin1Char('|'), -1) + QLatin1String(" // ")
                         + elements(proj->docs()->text(rest->id));
                splitOk = rest->scriptMode
                          && elements(proj->docs()->text(rest->id)) == QLatin1String("Scene Heading:INT. LAMP ROOM|Character:MARTA|Dialogue:It was never sent.")
                          && proj->docs()->text(copy->id)->blockCount() == 7;
                m->moveToTrash({rest});
            }
        }
        check(copy && copy->scriptMode && splitOk,
              QStringLiteral("a duplicate of a script is a script; Split gives a script whose first line keeps its element"), halves);
        QList<BinderItem *> away;
        for (BinderItem *x : {fresh, plainNew, copy})
            if (x)
                away << x;
        m->moveToTrash(away);
        settle(200);
        binder->select({scene1}, true);
        settle(300);
    }

    // ================================================================ script format
    {
        QMenu *formats = w.findChild<QMenu *>(QStringLiteral("scriptFormatMenu"));
        emit formats->aboutToShow();
        const QStringList names = menuTexts(formats);
        QString ticked;
        for (QAction *a : formats->actions())
            if (a->isChecked())
                ticked = menuText(a->text());
        // Cancel leaves everything
        withModal<QMessageBox>(300, [](QMessageBox *b) { b->button(QMessageBox::Cancel)->click(); });
        menuAction(formats, QStringLiteral("Stage Play"))->trigger();
        settle(300);
        const bool kept = script::format(proj->formatting).name == QLatin1String("Screenplay");
        QString question;
        withModal<QMessageBox>(300, [&](QMessageBox *b) {
            question = b->text();
            b->button(QMessageBox::Ok)->click();
        });
        emit formats->aboutToShow();
        menuAction(formats, QStringLiteral("Stage Play"))->trigger();
        settle(500);
        emit w.findChild<QMenu *>(QStringLiteral("scriptMenu"))->aboutToShow();
        proj->save();
        settle(200);
        const QString all = elements(ed()->document());
        const QTextBlock chr = ed()->document()->findBlockByNumber(2);
        check(names == QStringList({QStringLiteral("Screenplay"), QStringLiteral("Stage Play"), QStringLiteral("Comic Script")})
                  && ticked == QLatin1String("Screenplay") && kept && question.contains(QLatin1String("Stage Play")) && question.contains(QLatin1String("Shot")),
              QStringLiteral("Script Format lists the formats; changing it asks first and names what has no counterpart; Cancel changes nothing"),
              names.join(QLatin1Char('|')) + QLatin1String(" / ") + ticked + QLatin1String(" / ") + question);
        check(script::format(proj->formatting).name == QLatin1String("Stage Play") && menuText(modeAction->text()) == QLatin1String("Script Mode - Stage Play")
                  && all.startsWith(QLatin1String("Scene Heading:INT. LIGHTHOUSE - NIGHT|Stage Direction:Rain against the glass.|Character:MARTA|Parenthetical:(low)|"))
                  && qAbs(chr.blockFormat().leftMargin() - 2.5 * 96.0) < 3.5
                  && (ed()->document()->begin().blockFormat().alignment() & Qt::AlignHCenter)
                  && byKey(&w, QStringLiteral("script:Stage Direction")) && !w.findChild<QAction *>(QStringLiteral("script:Shot"))
                  && fileText(proj->docs()->textPath(scene2Id)).contains(QLatin1String("::Script > Stage Direction:: The boat is in.")),
              QStringLiteral("Stage Play: the scripts get the new layout, the menu and its commands follow, files of closed scripts are rewritten"),
              all + QLatin1String(" / ") + menuText(modeAction->text()) + QStringLiteral(" / %1").arg(chr.blockFormat().leftMargin()));
        // the keys follow the new format: after dialogue comes the next speaker
        QTextCursor c = ed()->textCursor();
        c.movePosition(QTextCursor::End);
        ed()->setTextCursor(c);
        key(ed(), Qt::Key_Return);
        const QString next = here();
        key(ed(), Qt::Key_Tab); // empty character line -> dialogue (stage play)
        const QString toggled = here();
        key(ed(), Qt::Key_Backspace);
        check(next == QLatin1String("Character") && toggled == QLatin1String("Dialogue"),
              QStringLiteral("Stage Play: Enter after dialogue gives the next speaker"), next + QLatin1String(" / ") + toggled);
        shot(&w, QStringLiteral("m9-04-stage-play"));
    }

    // ================================================================ Script Settings
    {
        bool sawDialog = false;
        QStringList listed;
        QString previewText, unitSuffix;
        double leftShown = 0;
        withModal<ScriptSettingsDialog>(400, [&](ScriptSettingsDialog *d) {
            sawDialog = true;
            auto *list = d->findChild<QListWidget *>(QStringLiteral("elementList"));
            for (int i = 0; i < list->count(); ++i)
                listed << list->item(i)->text();
            previewText = d->findChild<QTextEdit *>(QStringLiteral("scriptPreview"))->toPlainText();
            d->selectElement(QStringLiteral("Character"));
            auto *left = d->findChild<QDoubleSpinBox *>(QStringLiteral("elementLeft"));
            leftShown = left->value();
            unitSuffix = left->suffix();
            left->setValue(3.0); // inches
            auto *name = d->findChild<QLineEdit *>(QStringLiteral("elementName"));
            name->setText(QStringLiteral("Speaker"));
            emit name->editingFinished();
            d->findChild<QCheckBox *>(QStringLiteral("elementBold"))->setChecked(true);
            // a new element after the last one
            d->addElement();
            auto *n2 = d->findChild<QLineEdit *>(QStringLiteral("elementName"));
            n2->setText(QStringLiteral("Song"));
            emit n2->editingFinished();
            auto *role = d->findChild<QComboBox *>(QStringLiteral("elementRole"));
            role->setCurrentIndex(role->findData(script::roleKey(script::Role::Lyrics)));
            QTimer::singleShot(0, d, [d] {
                d->grab().save(outDir + QLatin1String("/m9-05-script-settings.png"));
                d->accept();
            });
        });
        byKey(&w, QStringLiteral("scriptSettings"))->trigger();
        settle(600);
        proj->save();
        settle(200);
        const script::Format &f = script::format(proj->formatting);
        const QString all = elements(ed()->document());
        const QTextBlock chr = ed()->document()->findBlockByNumber(2);
        check(sawDialog && listed == QStringList({QStringLiteral("Act Heading"), QStringLiteral("Scene Heading"), QStringLiteral("Stage Direction"),
                                                  QStringLiteral("Character"), QStringLiteral("Parenthetical"), QStringLiteral("Dialogue"),
                                                  QStringLiteral("Transition")})
                  && previewText.contains(QLatin1String("(")) && qAbs(leftShown - 2.5) < 0.01 && unitSuffix.contains(QLatin1String("in")),
              QStringLiteral("Script Settings lists the elements of the project's format with a preview; lengths are in inches"),
              listed.join(QLatin1Char('|')) + QStringLiteral(" / %1%2").arg(leftShown).arg(unitSuffix));
        check(f.find(QStringLiteral("Speaker")) && !f.find(QStringLiteral("Character")) && f.find(QStringLiteral("Song"))
                  && f.find(QStringLiteral("Song"))->role == script::Role::Lyrics && qAbs(f.find(QStringLiteral("Speaker"))->leftMm - 76.2) < 0.2
                  && f.find(QStringLiteral("Speaker"))->bold && all.contains(QLatin1String("|Speaker:MARTA|")) && !all.contains(QLatin1String("Character:"))
                  && qAbs(chr.blockFormat().leftMargin() - 3.0 * 96.0) < 3.5
                  && chr.begin().fragment().charFormat().fontWeight() >= QFont::Bold
                  && f.find(QStringLiteral("Dialogue"))->onEnter == QLatin1String("Speaker")
                  && fileText(proj->docs()->textPath(scene2Id)).contains(QLatin1String("::Script > Speaker:: TOM")),
              QStringLiteral("Script Settings: a renamed element keeps its paragraphs everywhere, its new indent and bold show at once, a new element is there"),
              all + QStringLiteral(" / left %1").arg(chr.blockFormat().leftMargin()));
        QMenu *formats = w.findChild<QMenu *>(QStringLiteral("scriptFormatMenu"));
        emit formats->aboutToShow();
        const QStringList names = menuTexts(formats);
        check(names.size() == 4 && names.last() == QLatin1String("Stage Play (this project)") && byKey(&w, QStringLiteral("script:Song")),
              QStringLiteral("a format changed for this project is listed as such; its new element is a command"), names.join(QLatin1Char('|')));

        // start from another format inside the window, then cancel
        const script::Format beforeCancel = f;
        withModal<ScriptSettingsDialog>(400, [&](ScriptSettingsDialog *d) {
            d->useFormat(script::builtIn().at(2));
            const QString pv = d->findChild<QTextEdit *>(QStringLiteral("scriptPreview"))->toPlainText();
            check(d->format().name == QLatin1String("Comic Script") && pv.contains(QLatin1String("SFX: ")) && pv.contains(QLatin1String("CAPTION: ")),
                  QStringLiteral("Script Settings: Start From loads another format into the window"), pv);
            QTimer::singleShot(0, d, [d] {
                d->grab().save(outDir + QLatin1String("/m9-06-script-settings-comic.png"));
                d->reject();
            });
        });
        byKey(&w, QStringLiteral("scriptSettings"))->trigger();
        settle(600);
        check(script::format(proj->formatting) == beforeCancel, QStringLiteral("Script Settings: Cancel changes nothing"));
    }

    // ================================================================ compile, export
    {
        QStringList outputs;
        withModal<CompileDialog>(500, [&](CompileDialog *d) {
            for (QComboBox *c : d->findChildren<QComboBox *>())
                for (int i = 0; i < c->count(); ++i)
                    if (c->itemData(i).toString() == QLatin1String("fountain") || c->itemData(i).toString() == QLatin1String("fdx"))
                        outputs << c->itemText(i);
            d->reject();
        });
        byKey(&w, QStringLiteral("compile"))->trigger();
        settle(700);
        check(outputs == QStringList({QStringLiteral("Fountain Screenplay (.fountain)"), QStringLiteral("Final Draft (.fdx)")}),
              QStringLiteral("File > Compile offers Fountain and Final Draft as output"), outputs.join(QLatin1Char('|')));

        QTemporaryDir out;
        const QString f1 = dnd::exportItem(proj, scene1, out.path(), QStringLiteral("fountain"), false, false);
        const QString f2 = dnd::exportItem(proj, scene1, out.path(), QStringLiteral("fdx"), false, false);
        const QString ft = fileText(f1);
        script::Screenplay back;
        QFile x(f2);
        x.open(QIODevice::ReadOnly);
        const bool readBack = script::fromFdx(x.readAll(), &back, &err);
        check(f1.endsWith(QLatin1String(".fountain")) && ft.contains(QLatin1String("INT. LIGHTHOUSE - NIGHT\n\nRain against the glass.\n\nMARTA\n(low)\nWho left this here?\n"))
                  && ft.contains(QLatin1String("\nCUT TO:\n")) && readBack && back.paras.size() >= 9
                  && back.paras.at(2).role == script::Role::Character && back.paras.at(2).text() == QLatin1String("MARTA"),
              QStringLiteral("File > Export > Files writes a script as a Fountain or Final Draft file"), ft + err);
    }

    // ================================================================ comic script (screenshot), reopening
    {
        withModal<QMessageBox>(300, [](QMessageBox *b) { b->button(QMessageBox::Ok)->click(); });
        QMenu *formats = w.findChild<QMenu *>(QStringLiteral("scriptFormatMenu"));
        emit formats->aboutToShow();
        menuAction(formats, QStringLiteral("Comic Script"))->trigger();
        settle(500);
        BinderItem *page = m->createItem(act, 2, ItemType::Text, QStringLiteral("Page One"));
        binder->select({page}, true);
        settle(300);
        ed()->setFocus();
        modeAction->trigger();
        settle(200);
        type(ed(), QStringLiteral("page o"));
        key(ed(), Qt::Key_Return);              // PAGE ONE
        key(ed(), Qt::Key_Tab);                 // " (" + list
        type(ed(), QStringLiteral("fi"));
        key(ed(), Qt::Key_Return);              // five panels)
        key(ed(), Qt::Key_Return);              // -> Panel
        type(ed(), QStringLiteral("pan"));
        key(ed(), Qt::Key_Return);              // Panel 1
        key(ed(), Qt::Key_Return);              // -> Description
        type(ed(), QStringLiteral("The lighthouse at night, seen from the sea."));
        key(ed(), Qt::Key_Return);              // -> Character
        type(ed(), QStringLiteral("marta"));
        key(ed(), Qt::Key_Return);              // -> Dialogue
        type(ed(), QStringLiteral("Forty-one."));
        key(ed(), Qt::Key_Return);              // -> Character
        key(ed(), Qt::Key_Tab);                 // -> Caption (types "CAPTION: ")
        const QString cap0 = lastBlock();
        type(ed(), QStringLiteral("Later."));
        key(ed(), Qt::Key_Return);              // -> Character
        key(ed(), Qt::Key_Tab);                 // Caption
        key(ed(), Qt::Key_Tab);                 // Sound Effect
        const QString bareHint = pane0->findChild<QLabel *>(QStringLiteral("scriptHint"))->text();
        type(ed(), QStringLiteral("krak"));
        settle(150);
        const QString typedHint = pane0->findChild<QLabel *>(QStringLiteral("scriptHint"))->text();
        check(bareHint.contains(QLatin1String("Tab: Page")) && bareHint.contains(QLatin1String("list of elements"))
                  && typedHint == QLatin1String("Enter: Character"),
              QStringLiteral("the footer's hint follows the typing"), bareHint + QLatin1String(" -> ") + typedHint);
        const QString all = elements(ed()->document());
        check(cap0 == QLatin1String("CAPTION: ")
                  && all == QLatin1String("Page:PAGE ONE (five panels)|Panel:Panel 1|Description:The lighthouse at night, seen from the sea.|"
                                          "Character:MARTA|Dialogue:Forty-one.|Caption:CAPTION: Later.|Sound Effect:SFX: KRAK"),
              QStringLiteral("Comic Script: pages, panels, descriptions, captions and sound effects with their prefixes"), all);
        shot(&w, QStringLiteral("m9-07-comic-script"));
        const QString pageId = page->id;

        proj->save();
        settle(200);
        check(w.closeProject(), QStringLiteral("project closed"));
        settle(300);
        check(w.openProject(projectFile), QStringLiteral("project reopened"));
        settle(600);
        proj = w.project();
        binder = w.findChild<BinderPanel *>();
        BinderItem *pg = proj->item(pageId);
        binder->select({pg}, true);
        settle(400);
        auto *split2 = qobject_cast<QSplitter *>(w.findChild<EditorPane *>()->parentWidget());
        auto *pane = qobject_cast<EditorPane *>(split2->widget(0));
        TextEditor *e2 = pane->textView()->editors().value(0);
        auto *button2 = pane->findChild<QToolButton *>(QStringLiteral("scriptElementButton"));
        e2->setFocus();
        QTextCursor c = e2->textCursor();
        c.movePosition(QTextCursor::End);
        e2->setTextCursor(c);
        settle(200);
        key(e2, Qt::Key_Return);
        const QString afterSfx = e2->scriptElementAtCursor();
        check(script::format(proj->formatting).name == QLatin1String("Comic Script") && pg->scriptMode && proj->item(scene1Id)->scriptMode
                  && !proj->item(proseId)->scriptMode && e2->scriptActive() && button2->isVisible() && afterSfx == QLatin1String("Character")
                  && elements(proj->docs()->text(scene1Id)).startsWith(QLatin1String("Page:INT. LIGHTHOUSE - NIGHT|Description:Rain against the glass.|Character:MARTA|"))
                  && menuText(byKey(&w, QStringLiteral("scriptMode"))->text()) == QLatin1String("Script Mode - Comic Script"),
              QStringLiteral("after reopening: the format, the scripts and their elements are there, and the keys work"),
              script::format(proj->formatting).name + QLatin1String(" / ") + afterSfx + QLatin1String(" / ") + elements(proj->docs()->text(scene1Id)));
    }

    // ================================================================ a project of a newer version
    {
        QTemporaryDir where;
        templates::Info blankInfo;
        for (const auto &t : templates::all())
            if (t.id == QLatin1String("blank"))
                blankInfo = t;
        auto np = templates::createProject(blankInfo, where.path(), QStringLiteral("From the Future"), &err);
        if (np) {
            np->save();
            const QString file = np->indexPath();
            np.reset();
            QFile f(file);
            f.open(QIODevice::ReadOnly);
            QByteArray data = f.readAll();
            f.close();
            data.replace("<QuillbinderProject ", "<QuillbinderProject needs=\"99.0.0\" "); // newer than any version of the program
            f.open(QIODevice::WriteOnly | QIODevice::Truncate);
            f.write(data);
            f.close();
            QString warned;
            withModal<QMessageBox>(400, [&](QMessageBox *b) {
                warned = b->text();
                b->button(QMessageBox::Cancel)->click();
            });
            const bool opened = w.openProject(file);
            settle(300);
            check(!opened && warned.contains(QLatin1String("99.0.0")) && w.project() && w.project()->indexPath() == projectFile,
                  QStringLiteral("a project that needs a newer version is not opened without asking; Cancel keeps the current project"), warned);
        } else {
            check(false, QStringLiteral("project for the version check"), err);
        }
    }

    // ================================================================ add-on: templates and a format
    {
        const QString pkg = QStringLiteral(QB_TEST_PLUGIN_DIR "/../addons-dist/scriptwriting-pack.qbpkg");
        if (!QFile::exists(pkg)) {
            std::printf("      (add-on packages not built; run packaging/build-addons.sh <build> <build>/addons-dist)\n");
        } else {
            QStringList log;
            QString aerr;
            bool restart = false;
            const bool installed = addons::install(pkg, &log, &aerr, &restart);
            QStringList found;
            templates::Info comicInfo, playInfo;
            for (const templates::Info &t : templates::all())
                if (t.category == QLatin1String("Scriptwriting")) {
                    found << t.name;
                    if (t.name == QLatin1String("Comic Script"))
                        comicInfo = t;
                    if (t.name == QLatin1String("Stage Play"))
                        playInfo = t;
                }
            found.sort();
            script::Kind radioKind = script::Kind::BuiltIn;
            script::Format radio;
            for (const script::Entry &e : script::available())
                if (e.format.name == QLatin1String("Radio Play")) {
                    radioKind = e.kind;
                    radio = e.format;
                }
            QStringList broken;
            for (const script::Element &e : radio.elements)
                for (const QString &t : {e.onEnter, e.onTabEmpty, e.onTabEnd})
                    if (!t.isEmpty() && !radio.find(t))
                        broken << e.name + QLatin1String(" -> ") + t;
            check(installed && !restart && found == QStringList({QStringLiteral("Comic Script"), QStringLiteral("Screenplay"), QStringLiteral("Stage Play")})
                      && radioKind == script::Kind::Addon && radio.elements.size() == 8 && broken.isEmpty() && radio.first(),
                  QStringLiteral("add-on \"Scriptwriting pack\": three project templates under Scriptwriting and the format Radio Play"),
                  aerr + found.join(QLatin1Char('|')) + broken.join(QLatin1Char(';')));
            QMenu *formats = w.findChild<QMenu *>(QStringLiteral("scriptFormatMenu"));
            emit formats->aboutToShow();
            check(menuTexts(formats) == QStringList({QStringLiteral("Screenplay"), QStringLiteral("Stage Play"), QStringLiteral("Comic Script"),
                                                     QStringLiteral("Radio Play")}),
                  QStringLiteral("the format of the add-on is in Format > Scriptwriting > Script Format"), menuTexts(formats).join(QLatin1Char('|')));

            QTemporaryDir where;
            auto cp = templates::createProject(comicInfo, where.path(), QStringLiteral("Harbour Lights"), &err);
            BinderItem *page1 = nullptr, *issue = nullptr, *sheet = nullptr;
            if (cp)
                for (BinderItem *it : cp->allItems()) {
                    if (it->title == QLatin1String("Page 1"))
                        page1 = it;
                    if (it->title == QLatin1String("Issue 1"))
                        issue = it;
                    if (it->title == QLatin1String("Page") && it->type == ItemType::Text)
                        sheet = it;
                }
            check(cp && script::format(cp->formatting).name == QLatin1String("Comic Script") && page1 && page1->scriptMode && issue && sheet
                      && sheet->scriptMode && issue->defaultTemplate == sheet->id
                      && elements(cp->docs()->text(page1->id)).startsWith(QLatin1String("Page:PAGE ONE (four panels)|Panel:Panel 1|Description:"))
                      && elements(cp->docs()->text(page1->id)).endsWith(QLatin1String("|Caption:CAPTION: A line of narration.|Sound Effect:SFX: KRAK")),
                  QStringLiteral("a project from the Comic Script template has the comic format, a sample page and script pages as the folder's default"),
                  cp ? elements(cp->docs()->text(page1 ? page1->id : QString())) : err);
            const QString comicFile = cp ? cp->indexPath() : QString();
            if (cp) {
                cp->save();
                cp.reset();
                w.closeProject();
                settle(300);
                check(w.openProject(comicFile), QStringLiteral("template project opened in the window"));
                settle(600);
                Project *cproj = w.project();
                auto *b3 = w.findChild<BinderPanel *>();
                BinderItem *is = nullptr;
                for (BinderItem *it : cproj->allItems())
                    if (it->title == QLatin1String("Issue 1"))
                        is = it;
                b3->tree()->expandAll();
                b3->select({is->children.first()}, true);
                settle(400);
                shot(&w, QStringLiteral("m9-08-comic-template"));
                const QList<BinderItem *> had = is->children;
                byKey(&w, QStringLiteral("newText"))->trigger();
                settle(400);
                BinderItem *np = nullptr;
                for (BinderItem *c : is->children)
                    if (!had.contains(c))
                        np = c;
                check(np && np->scriptMode && np->title == QLatin1String("Page")
                          && cproj->docs()->text(np->id)->begin().blockFormat().stringProperty(props::Style) == QLatin1String("Script > Page")
                          && menuText(byKey(&w, QStringLiteral("scriptMode"))->text()) == QLatin1String("Script Mode - Comic Script"),
                      QStringLiteral("in the template project a new page is a script page"));
            }
        }
    }

    std::printf("\n%d failure(s)\n", failures);
    w.closeProject();
    delete wp;
    return failures;
}
