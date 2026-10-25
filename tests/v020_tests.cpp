// Version 0.20: the Objects sheet (characters, locations, environments, points of
// interest, weather and categories of the writer's own; templates, colors, folders,
// links, backlinks, links to documents) and the data-safety fixes of the stability
// report (control characters, unreadable files, backups over 4 GiB, autosave while
// typing, Scrivener-format working copies, the spare copy of the project file, the
// mind map number ranges). Saves screenshots into the folder given as first argument.
// Exit code = number of failed checks.
//
//   QT_QPA_PLATFORM=offscreen ./qb_v020_tests /tmp/shots
//
// QB_SKIP_BIG_BACKUP=1 leaves out the backup of a 4.3 GB file (about 30 s).
#include "AppInfo.h"
#include "compile/CompileFormat.h"
#include "compile/Compiler.h"
#include "compile/PluginManager.h"
#include "core/Backup.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/MindMap.h"
#include "core/Objects.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "core/Zip.h"
#include "import/ScrivenerFormat.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/EditorPane.h"
#include "ui/Inspector.h"
#include "ui/MainWindow.h"
#include "ui/ObjectsUi.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QDomDocument>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <csignal>
#include <cstring>
#include <cstdio>
#include <functional>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace qb;
using namespace qb::objects;

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

static void setText(Project *p, BinderItem *it, const QString &text)
{
    QTextDocument *d = p->docs()->text(it->id);
    QTextCursor c(d);
    c.select(QTextCursor::Document);
    c.insertText(text);
}

static QString readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

static void setValues(Store *st, const QString &id, const QVariantMap &values)
{
    Object o = *st->object(id);
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        o.values.insert(it.key(), it.value());
    st->update(o);
}

static QStringList names(const QList<const Object *> &l)
{
    QStringList out;
    for (const Object *o : l)
        out << o->name;
    return out;
}

static QTreeWidgetItem *treeItem(QTreeWidget *t, const QString &text)
{
    for (QTreeWidgetItemIterator it(t); *it; ++it)
        if ((*it)->text(0) == text)
            return *it;
    return nullptr;
}

static QStringList treeTexts(QTreeWidget *t)
{
    QStringList out;
    for (QTreeWidgetItemIterator it(t); *it; ++it)
        out << (*it)->text(0);
    return out;
}

static QStringList chipTexts(ChipList *c)
{
    QStringList out;
    if (c)
        for (const ChipList::Chip &ch : c->chips())
            out << ch.text;
    return out;
}

// a point inside the first link of a text field (viewport coordinates)
static QPoint linkPoint(QTextEdit *e)
{
    for (QTextBlock b = e->document()->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it)
            if (it.fragment().charFormat().isAnchor()) {
                QTextCursor c(e->document());
                c.setPosition(it.fragment().position() + qMin(2, it.fragment().length() / 2));
                return e->cursorRect(c).center() + QPoint(1, 0);
            }
    return QPoint(-1, -1);
}

// the editor pane on the left / top (the window has two; the second is hidden until split)
static EditorPane *mainPane(QWidget *w)
{
    for (EditorPane *p : w->findChildren<EditorPane *>())
        if (p->isVisible())
            return p;
    return nullptr;
}

static QString linkHrefIn(QTextDocument *d)
{
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it)
            if (it.fragment().charFormat().isAnchor())
                return it.fragment().charFormat().anchorHref();
    return QString();
}

// =====================================================================================
// 1. how objects are stored
// =====================================================================================
static void storageChecks()
{
    section(QStringLiteral("Objects: storage in the project folder"));
    QTemporaryDir dir;
    QString err;
    auto proj = novelProject(dir.path(), QStringLiteral("Storage"), &err);
    if (!proj) {
        check(false, QStringLiteral("(project made)"), err);
        return;
    }
    Project *p = proj.get();
    const QString folder = p->folderPath();
    BinderItem *market = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("The Market"));
    setText(p, market, QStringLiteral("Shelin crossed the market."));
    p->save(&err);
    const QString marketId = market->id; // (the item goes with the project when it is opened again)
    check(!QDir(folder + QLatin1String("/objects")).exists(),
          QStringLiteral("a project without objects gets no objects folder (nothing changes for it)"));

    Store *st = p->objects();
    QStringList ids;
    for (const Category &c : st->schema().categories)
        ids << c.id;
    check(ids == QStringList({QStringLiteral("characters"), QStringLiteral("locations"), QStringLiteral("environments"),
                              QStringLiteral("pois"), QStringLiteral("weather")}),
          QStringLiteral("standard categories: Characters, Locations, Environments, Points of Interest, Weather"), ids.join(QLatin1String(", ")));
    const Category *chars = st->category(QStringLiteral("characters"));
    bool fields = chars;
    for (const char *f : {"race", "height", "attributes", "eyes", "traits", "other", "livesIn", "background", "scenes", "picture"})
        fields = fields && chars->field(QLatin1String(f));
    check(fields, QStringLiteral("the character template has race, height, attributes, eye color, traits, other, lives in, "
                                 "background story, appears in, picture"));
    check(chars && chars->field(QStringLiteral("height"))->unit == QLatin1String("cm")
              && chars->field(QStringLiteral("livesIn"))->type == FieldType::Objects
              && chars->field(QStringLiteral("livesIn"))->targets == QStringList{QStringLiteral("locations")},
          QStringLiteral("... height in cm; “Lives in” links to locations"));
    const Category *wx = st->category(QStringLiteral("weather"));
    check(wx && wx->field(QStringLiteral("temperature")) && wx->field(QStringLiteral("temperature"))->unit == QStringLiteral("°C"),
          QStringLiteral("the weather template has a temperature in °C"));

    const QString shelin = st->create(QStringLiteral("characters"), QString(), QStringLiteral("Shelin"));
    const QString ashvale = st->create(QStringLiteral("locations"), QString(), QStringLiteral("Ashvale"));
    const QString fens = st->create(QStringLiteral("environments"), QString(), QStringLiteral("Misty Fens"));
    const QString storm = st->create(QStringLiteral("weather"), QString(), QStringLiteral("Autumn Storms"));
    check(!shelin.isEmpty() && st->count() == 4 && p->hasUnsavedChanges(), QStringLiteral("objects are made; the project has unsaved changes"));
    setValues(st, shelin,
              {{QStringLiteral("race"), QStringLiteral("Elf")},
               {QStringLiteral("height"), QStringLiteral("172")},
               {QStringLiteral("eyes"), QStringLiteral("grey-green")},
               {QStringLiteral("traits"), QStringLiteral("Curious and stubborn. Grew up in [Ashvale](%1).").arg(linkTo(ashvale))},
               {QStringLiteral("livesIn"), QStringList{ashvale}},
               {QStringLiteral("scenes"), QStringList{marketId}}});
    setValues(st, ashvale, {{QStringLiteral("environment"), QStringList{fens}}, {QStringLiteral("kind"), QStringLiteral("Village")}});
    setValues(st, fens, {{QStringLiteral("weather"), QStringList{storm}}});
    check(p->save(&err), QStringLiteral("the project is saved"), err);
    const QString shelinFile = QStringLiteral("%1/objects/characters/%2.json").arg(folder, shelin);
    check(QFile::exists(folder + QLatin1String("/objects/schema.json")) && QFile::exists(shelinFile),
          QStringLiteral("objects/schema.json and one file per object (objects/characters/<id>.json)"));
    check(readAll(shelinFile).contains(QLatin1String("\"Shelin\"")) && readAll(shelinFile).contains(QLatin1String("\"Elf\"")),
          QStringLiteral("... the object file holds the name and the fields"));
    check(!readAll(p->indexPath()).contains(QLatin1String("Shelin")),
          QStringLiteral("the project file (.qbx) is unchanged by objects: older versions read it as before"));

    // reopened
    proj.reset();
    proj = Project::open(folder, &err);
    p = proj.get();
    st = p ? p->objects() : nullptr;
    const Object *s = st ? st->object(shelin) : nullptr;
    check(s && s->text(QStringLiteral("race")) == QLatin1String("Elf") && s->text(QStringLiteral("height")) == QLatin1String("172"),
          QStringLiteral("after reopening: the character with its fields"));
    if (!s)
        return;
    // the click-through of the request: character -> where he lives -> environment -> weather
    const Object *home = st->object(s->ids(QStringLiteral("livesIn")).value(0));
    const Object *env = home ? st->object(home->ids(QStringLiteral("environment")).value(0)) : nullptr;
    const Object *w = env ? st->object(env->ids(QStringLiteral("weather")).value(0)) : nullptr;
    check(home && env && w && home->name == QLatin1String("Ashvale") && env->name == QLatin1String("Misty Fens")
              && w->name == QLatin1String("Autumn Storms"),
          QStringLiteral("links survive: Shelin -> lives in Ashvale -> environment Misty Fens -> weather Autumn Storms"));
    QStringList back;
    for (const Backlink &b : st->backlinks(ashvale))
        back << st->object(b.objectId)->name + QLatin1Char('/') + b.field;
    check(back.size() == 2 && back.join(QLatin1Char(' ')).contains(QLatin1String("Lives in"))
              && back.join(QLatin1Char(' ')).contains(QLatin1String("text of Traits")),
          QStringLiteral("backlinks of Ashvale: Shelin's “Lives in” and the link in Shelin's traits"), back.join(QLatin1String(", ")));
    check(names(st->objectsForDocument(marketId)) == QStringList{QStringLiteral("Shelin")},
          QStringLiteral("objects of a document: “The Market” has Shelin (field “Appears in”)"));

    // deleting an object removes the links to it
    st->remove(ashvale);
    s = st->object(shelin);
    check(s->ids(QStringLiteral("livesIn")).isEmpty() && s->text(QStringLiteral("traits")).contains(QLatin1String("Grew up in Ashvale."))
              && !s->text(QStringLiteral("traits")).contains(QLatin1String("qbobj://")),
          QStringLiteral("deleting Ashvale: the link field is emptied, the link in the text becomes a plain word"),
          s->text(QStringLiteral("traits")));
    p->save(&err);
    check(!QFile::exists(QStringLiteral("%1/objects/locations/%2.json").arg(folder, ashvale)), QStringLiteral("... and its file is gone"));

    // folders
    const QString villages = st->addFolder(QStringLiteral("locations"), QString(), QStringLiteral("Villages"));
    const QString brook = st->create(QStringLiteral("locations"), villages, QStringLiteral("Brookmere"));
    const QString sub = st->addFolder(QStringLiteral("locations"), villages, QStringLiteral("Ruined"));
    st->moveFolder(villages, sub); // into its own sub-folder: refused
    p->save(&err);
    proj.reset();
    proj = Project::open(folder, &err);
    p = proj.get();
    st = p->objects();
    const QList<Folder> fl = st->folders(QStringLiteral("locations"));
    check(fl.size() == 2 && st->object(brook) && st->object(brook)->folder == villages,
          QStringLiteral("sub-folders are kept: Brookmere in “Villages”, “Ruined” inside it"));
    bool cycle = false;
    for (const Folder &f : fl)
        cycle = cycle || (f.id == villages && f.parent == sub);
    check(!cycle, QStringLiteral("a folder cannot be moved into its own sub-folder"));

    // damaged and unknown files: kept, reported
    QDir().mkpath(folder + QLatin1String("/objects/factions"));
    {
        QFile f(folder + QLatin1String("/objects/characters/broken.json"));
        f.open(QIODevice::WriteOnly);
        f.write("{ this is not json");
        QJsonObject o;
        o[QStringLiteral("id")] = QStringLiteral("f00d");
        o[QStringLiteral("category")] = QStringLiteral("factions");
        o[QStringLiteral("name")] = QStringLiteral("The Grey Hand");
        QFile g(folder + QLatin1String("/objects/factions/f00d.json"));
        g.open(QIODevice::WriteOnly);
        g.write(QJsonDocument(o).toJson());
    }
    proj.reset();
    proj = Project::open(folder, &err);
    p = proj.get();
    st = p->objects();
    check(st->warnings().size() == 2 && st->object(QStringLiteral("f00d")) && st->category(QStringLiteral("factions")),
          QStringLiteral("a damaged object file is reported; an object of an unknown category gets that category (nothing lost)"),
          st->warnings().join(QLatin1Char('\n')));
    setValues(st, shelin, {{QStringLiteral("eyes"), QStringLiteral("green")}});
    p->save(&err);
    check(readAll(folder + QLatin1String("/objects/characters/broken.json")) == QLatin1String("{ this is not json"),
          QStringLiteral("... the damaged file is left as it is when the project is saved"));

    // a field removed from a template: its contents stay in the files and come back with the standard template
    {
        Schema less = st->schema();
        Category *c = less.category(QStringLiteral("characters"));
        c->fields.erase(std::remove_if(c->fields.begin(), c->fields.end(), [](const FieldDef &f) { return f.id == QLatin1String("eyes"); }),
                        c->fields.end());
        st->setSchema(less);
        setValues(st, shelin, {{QStringLiteral("race"), QStringLiteral("Half-elf")}});
        p->save(&err);
        const bool inFile = readAll(shelinFile).contains(QLatin1String("\"green\""));
        Schema full = st->schema();
        *full.category(QStringLiteral("characters")) = *builtInSchema().category(QStringLiteral("characters"));
        st->setSchema(full);
        check(inFile && st->object(shelin)->text(QStringLiteral("eyes")) == QLatin1String("green"),
              QStringLiteral("a field removed from a template keeps its contents in the file; the standard template brings it back"));
    }

    // templates for new projects
    Schema mine = st->schema();
    mine.categories.removeAt(4);
    Category spells;
    spells.id = QStringLiteral("spells");
    spells.name = QStringLiteral("Spells");
    spells.color = QColor(QStringLiteral("#aa3366"));
    FieldDef fd;
    fd.id = QStringLiteral("cost");
    fd.label = QStringLiteral("Cost");
    fd.type = FieldType::Number;
    fd.unit = QStringLiteral("mana");
    spells.fields << fd;
    mine.categories << spells;
    check(saveSchemaForNewProjects(mine, &err) && hasOwnSchemaForNewProjects(), QStringLiteral("templates kept for new projects"), err);
    {
        QTemporaryDir d2;
        auto np = novelProject(d2.path(), QStringLiteral("Second"), &err);
        Store *s2 = np ? np->objects() : nullptr;
        check(s2 && s2->category(QStringLiteral("spells")) && !s2->category(QStringLiteral("weather")) && s2->folders(QStringLiteral("locations")).isEmpty(),
              QStringLiteral("... a new project starts with them (without the folders of the other project)"));
    }
    resetSchemaForNewProjects();
    {
        QTemporaryDir d3;
        auto np = novelProject(d3.path(), QStringLiteral("Third"), &err);
        check(np && np->objects()->category(QStringLiteral("weather")) && !np->objects()->category(QStringLiteral("spells")),
              QStringLiteral("... “Standard” brings back the built-in templates for new projects"));
    }

    // autosave includes the objects
    p->setAutosaveDelay(300);
    setValues(st, brook, {{QStringLiteral("population"), QStringLiteral("340")}});
    settle(900);
    check(!p->hasUnsavedChanges() && readAll(QStringLiteral("%1/objects/locations/%2.json").arg(folder, brook)).contains(QLatin1String("340")),
          QStringLiteral("a changed object is saved by the autosave"));

    // compile: links to objects are not links of the manuscript
    QTextDocument *d = p->docs()->text(marketId);
    QTextCursor c(d);
    c.movePosition(QTextCursor::End);
    QTextCharFormat lf;
    lf.setAnchor(true);
    lf.setAnchorHref(linkTo(shelin));
    c.insertText(QStringLiteral(" Shelin"), lf);
    bool linkOut = false, textIn = false;
    for (const auto &fmt : compile::builtinFormats()) {
        compile::Compiler comp(p, fmt, p->compile, compile::AuthorInfo{QStringLiteral("Ada"), QStringLiteral("Keeper")});
        const compile::Manuscript m = comp.build();
        for (const auto &para : m.paragraphs) {
            textIn = textIn || para.plainText().contains(QLatin1String("market. Shelin"));
            for (const auto &r : para.runs)
                linkOut = linkOut || r.link.startsWith(QLatin1String("qbobj"));
        }
    }
    check(textIn && !linkOut, QStringLiteral("compiled: the linked name is in the text, without the link to the object"));
}

// =====================================================================================
// 2. the Objects sheet in the window
// =====================================================================================
static void uiChecks()
{
    section(QStringLiteral("Objects: the sheet in the inspector and in its window"));
    QTemporaryDir dir;
    QString err;
    auto proj = novelProject(dir.path(), QStringLiteral("Ashes"), &err);
    if (!proj) {
        check(false, QStringLiteral("(project made)"), err);
        return;
    }
    Project *p = proj.get();
    BinderItem *market = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("The Market"));
    setText(p, market, QStringLiteral("Shelin crossed the market at dawn. "));
    BinderItem *other = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("The Road"));
    setText(p, other, QStringLiteral("The road was long."));
    p->save(&err);
    const QString folder = p->folderPath();

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1400, 920);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    w.adoptProject(std::move(proj));
    settle(400);
    auto *binder = w.findChild<BinderPanel *>();
    auto *inspector = w.findChild<Inspector *>();
    auto *showInsp = w.findChild<QAction *>(QStringLiteral("showInspector"));
    if (showInsp && !showInsp->isChecked())
        showInsp->trigger();
    settle(200);
    auto *mm = w.findChild<QToolButton *>(QStringLiteral("inspectorMindMapButton"));
    auto *ob = w.findChild<QToolButton *>(QStringLiteral("inspectorObjectsButton"));
    check(mm && ob, QStringLiteral("the inspector has an Objects button"));
    if (!mm || !ob || !inspector || !binder)
        return;
    QLayout *tl = ob->parentWidget()->layout();
    check(tl && tl->indexOf(ob) == tl->indexOf(mm) + 1 && ob->x() > mm->x(),
          QStringLiteral("... right next to the mind map button (to its right)"),
          QStringLiteral("mind map at %1, objects at %2").arg(tl ? tl->indexOf(mm) : -1).arg(tl ? tl->indexOf(ob) : -1));
    check(!ob->icon().isNull(), QStringLiteral("... with its own icon"));
    binder->select({market}, true);
    settle(300);
    QTest::mouseClick(ob, Qt::LeftButton);
    settle(300);
    ObjectsPanel *panel = inspector->objectsPanel();
    check(inspector->currentTab() == Inspector::Objects && panel->isVisible(), QStringLiteral("a click on it shows the Objects sheet"));
    int catButtons = 0;
    for (const char *c : {"characters", "locations", "environments", "pois", "weather"})
        catButtons += panel->categoryButton(QLatin1String(c)) ? 1 : 0;
    check(catButtons == 5 && panel->categoryButton(QStringLiteral("characters"))->isChecked(),
          QStringLiteral("... colored buttons for the five categories, Characters chosen"));
    check(panel->categoryButton(QStringLiteral("locations"))->styleSheet().contains(QLatin1String("#1a9fd9")),
          QStringLiteral("... each in its category's color (Locations blue)"));
    shot(&w, QStringLiteral("v020-01-objects-tab-empty"));

    // a character, made with "+" and filled in
    Store *st = p->objects();
    QTest::mouseClick(w.findChild<QToolButton *>(QStringLiteral("objectsNew")), Qt::LeftButton);
    settle(200);
    ObjectForm *form = panel->form();
    const QString shelin = panel->currentObject();
    check(!shelin.isEmpty() && st->object(shelin) && st->object(shelin)->category == QLatin1String("characters"),
          QStringLiteral("“+” makes a new character and shows it"));
    form->nameField()->setFocus();
    form->nameField()->selectAll();
    QTest::keyClicks(form->nameField(), QStringLiteral("Shelin"));
    auto *race = qobject_cast<QLineEdit *>(form->fieldWidget(QStringLiteral("race")));
    auto *height = qobject_cast<QLineEdit *>(form->fieldWidget(QStringLiteral("height")));
    auto *eyes = qobject_cast<QLineEdit *>(form->fieldWidget(QStringLiteral("eyes")));
    auto *traits = qobject_cast<ObjectTextEdit *>(form->fieldWidget(QStringLiteral("traits")));
    auto *role = qobject_cast<QComboBox *>(form->fieldWidget(QStringLiteral("role")));
    check(race && height && eyes && traits && role, QStringLiteral("the form has the fields of the template (text, number, long text, choice)"));
    if (!race || !height || !eyes || !traits || !role)
        return;
    QTest::keyClicks(race, QStringLiteral("Elf"));
    QTest::keyClicks(height, QStringLiteral("172"));
    QTest::keyClicks(eyes, QStringLiteral("grey-green"));
    traits->setFocus();
    QTest::keyClicks(traits, QStringLiteral("Curious and stubborn. Hates the rain."));
    role->setCurrentIndex(role->findText(QStringLiteral("Protagonist")));
    settle(200);
    const Object *s = st->object(shelin);
    check(s->name == QLatin1String("Shelin") && s->text(QStringLiteral("race")) == QLatin1String("Elf")
              && s->text(QStringLiteral("height")) == QLatin1String("172") && s->text(QStringLiteral("eyes")) == QLatin1String("grey-green")
              && s->text(QStringLiteral("traits")).contains(QLatin1String("Hates the rain")) && s->text(QStringLiteral("role")) == QLatin1String("Protagonist"),
          QStringLiteral("what is typed goes into the object (name, race, height, eye color, traits, role)"));
    check(treeItem(panel->tree(), QStringLiteral("Shelin")), QStringLiteral("... the list shows the new name"));

    // "Lives in": a new location from the link field
    auto *lives = qobject_cast<ChipList *>(form->fieldWidget(QStringLiteral("livesIn")));
    check(lives && lives->addButton(), QStringLiteral("the link field “Lives in” has a “+”"));
    if (!lives)
        return;
    bool newOffered = false;
    withModal<ObjectChooser>(300, [&newOffered](ObjectChooser *c) {
        c->searchField()->setText(QStringLiteral("Ashvale"));
        settle(100);
        for (int i = 0; i < c->list()->count(); ++i)
            if (c->list()->item(i)->data(Qt::UserRole).toString() == QLatin1String("new:locations")) {
                newOffered = true;
                c->list()->setCurrentRow(i);
            }
        shot(c, QStringLiteral("v020-02-chooser-new-location"));
        static_cast<QDialog *>(c)->accept();
    });
    QTest::mouseClick(lives->addButton(), Qt::LeftButton);
    settle(300);
    s = st->object(shelin);
    const QString ashvale = s->ids(QStringLiteral("livesIn")).value(0);
    check(newOffered && st->object(ashvale) && st->object(ashvale)->name == QLatin1String("Ashvale")
              && st->object(ashvale)->category == QLatin1String("locations"),
          QStringLiteral("... typing a new name offers “New in Locations”: Ashvale is made and linked"));
    check(chipTexts(lives) == QStringList{QStringLiteral("Ashvale")} && lives->chipButton(ashvale)
              && lives->chipButton(ashvale)->styleSheet().contains(QLatin1String("#1a9fd9")),
          QStringLiteral("... and shown as a chip in the color of Locations"));
    shot(&w, QStringLiteral("v020-03-character"));

    // click through: Shelin -> Ashvale -> Misty Fens
    QTest::mouseClick(lives->chipButton(ashvale), Qt::LeftButton);
    settle(300);
    check(panel->currentObject() == ashvale && panel->category() == QLatin1String("locations")
              && panel->categoryButton(QStringLiteral("locations"))->isChecked(),
          QStringLiteral("a click on the chip shows Ashvale (the sheet switches to Locations)"));
    check(chipTexts(form->backlinks()).join(QLatin1Char(' ')).contains(QLatin1String("Shelin")),
          QStringLiteral("... “Linked from” shows Shelin"), chipTexts(form->backlinks()).join(QLatin1String(", ")));
    auto *envField = qobject_cast<ChipList *>(form->fieldWidget(QStringLiteral("environment")));
    withModal<ObjectChooser>(300, [](ObjectChooser *c) {
        c->searchField()->setText(QStringLiteral("Misty Fens"));
        settle(100);
        for (int i = 0; i < c->list()->count(); ++i)
            if (c->list()->item(i)->data(Qt::UserRole).toString() == QLatin1String("new:environments"))
                c->list()->setCurrentRow(i);
        static_cast<QDialog *>(c)->accept();
    });
    if (envField)
        QTest::mouseClick(envField->addButton(), Qt::LeftButton);
    settle(300);
    const QString fens = st->object(ashvale)->ids(QStringLiteral("environment")).value(0);
    check(st->object(fens) && st->object(fens)->category == QLatin1String("environments"),
          QStringLiteral("Ashvale's environment: Misty Fens (a new environment)"));
    shot(&w, QStringLiteral("v020-04-location"));
    if (envField && envField->chipButton(fens))
        QTest::mouseClick(envField->chipButton(fens), Qt::LeftButton);
    settle(300);
    check(panel->currentObject() == fens, QStringLiteral("a click on it shows Misty Fens"));
    QTest::mouseClick(panel->backButton(), Qt::LeftButton);
    settle(150);
    const bool back1 = panel->currentObject() == ashvale;
    QTest::mouseClick(panel->backButton(), Qt::LeftButton);
    settle(150);
    const bool back2 = panel->currentObject() == shelin;
    QTest::mouseClick(panel->forwardButton(), Qt::LeftButton);
    settle(150);
    check(back1 && back2 && panel->currentObject() == ashvale && panel->canGoForward(),
          QStringLiteral("Back goes Misty Fens -> Ashvale -> Shelin, Forward to Ashvale again"));

    // looking at objects changes nothing
    p->save(&err);
    const QDateTime seen = st->object(shelin)->modified;
    panel->showObject(fens);
    settle(100);
    panel->showObject(shelin);
    settle(100);
    panel->showObject(ashvale);
    settle(200);
    check(!p->hasUnsavedChanges() && st->object(shelin)->modified == seen
              && st->object(shelin)->text(QStringLiteral("role")) == QLatin1String("Protagonist"),
          QStringLiteral("looking at objects changes nothing (no save, the role is still there)"));

    // a link inside a text field
    panel->showObject(fens);
    settle(200);
    auto *desc = qobject_cast<ObjectTextEdit *>(form->fieldWidget(QStringLiteral("description")));
    check(desc, QStringLiteral("the environment has a long text field “Description”"));
    if (!desc)
        return;
    desc->setFocus();
    QTest::keyClicks(desc, QStringLiteral("Reed beds south of "));
    QString firstOffer;
    withModal<ObjectChooser>(300, [&firstOffer](ObjectChooser *c) {
        c->searchField()->setText(QStringLiteral("Ashv"));
        settle(100);
        firstOffer = c->list()->currentItem() ? c->list()->currentItem()->text() : QString();
        static_cast<QDialog *>(c)->accept(); // the first entry: existing objects come before "New in ..."
    });
    desc->linkToObject();
    QTest::keyClicks(desc, QStringLiteral(", always in mist."));
    settle(300);
    const QString md = st->object(fens)->text(QStringLiteral("description"));
    check(md.contains(QStringLiteral("[Ashvale](%1)").arg(linkTo(ashvale))) && md.contains(QLatin1String("always in mist")),
          QStringLiteral("right-click > Link to Object: the text links to Ashvale (kept as [Ashvale](qbobj://...))"), md);
    check(firstOffer.startsWith(QLatin1String("Ashvale")), QStringLiteral("... the chooser offers the existing Ashvale first"), firstOffer);
    for (QWidget *a = desc->parentWidget(); a; a = a->parentWidget())
        if (auto *sa = qobject_cast<QScrollArea *>(a)) {
            sa->ensureWidgetVisible(desc, 10, 120);
            break;
        }
    shot(&w, QStringLiteral("v020-05-text-link"));
    const QPoint lp = linkPoint(desc);
    QTest::mouseClick(desc->viewport(), Qt::LeftButton, {}, lp);
    settle(300);
    check(panel->currentObject() == ashvale, QStringLiteral("a click on the link in the text shows Ashvale"));
    check(chipTexts(form->backlinks()).join(QLatin1Char(' ')).contains(QLatin1String("Misty Fens")),
          QStringLiteral("... whose “Linked from” now lists Misty Fens as well"), chipTexts(form->backlinks()).join(QLatin1String(", ")));

    // filter and folders
    st->create(QStringLiteral("locations"), QString(), QStringLiteral("Brookmere"));
    st->create(QStringLiteral("locations"), QString(), QStringLiteral("Cinder Keep"));
    panel->setCategory(QStringLiteral("locations"));
    settle(100);
    panel->filterField()->setText(QStringLiteral("ash"));
    settle(150);
    check(treeTexts(panel->tree()) == QStringList{QStringLiteral("Ashvale")}, QStringLiteral("the filter shows only matching objects"),
          treeTexts(panel->tree()).join(QLatin1String(", ")));
    panel->filterField()->clear();
    settle(100);
    const QString villages = panel->newFolder(QStringLiteral("Villages"));
    st->moveObject(ashvale, villages);
    settle(200);
    QTreeWidgetItem *ai = treeItem(panel->tree(), QStringLiteral("Ashvale"));
    check(ai && ai->parent() && ai->parent()->text(0) == QLatin1String("Villages"),
          QStringLiteral("a folder “Villages” in Locations; Ashvale moved into it"));
    // dragging Brookmere onto the folder in the list (a real drag with the mouse; the offscreen
    // platform has no drag and drop, so this runs in the X11 run)
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        info(QStringLiteral("(drag and drop in the list: checked in the X11 run)"));
    } else if (QTreeWidgetItem *bi = treeItem(panel->tree(), QStringLiteral("Brookmere")); bi && ai) {
        QWidget *vp = panel->tree()->viewport();
        const QPoint from = panel->tree()->visualItemRect(bi).center();
        const QPoint to = panel->tree()->visualItemRect(ai->parent()).center();
        QTimer::singleShot(300, qApp, [vp, from, to] {
            for (int i = 1; i <= 6; ++i) {
                QTest::mouseMove(vp, from + (to - from) * i / 6);
                QTest::qWait(40);
            }
            QTest::mouseRelease(vp, Qt::LeftButton, {}, to);
        });
        QTest::mousePress(vp, Qt::LeftButton, {}, from);
        QTest::mouseMove(vp, from + QPoint(0, -12));
        QTest::mouseMove(vp, from + QPoint(0, -24));
        settle(1500);
        QTreeWidgetItem *moved = treeItem(panel->tree(), QStringLiteral("Brookmere"));
        const Object *bo = nullptr;
        for (const Object *o : st->objects(QStringLiteral("locations")))
            if (o->name == QLatin1String("Brookmere"))
                bo = o;
        check(moved && moved->parent() && moved->parent()->text(0) == QLatin1String("Villages") && bo && bo->folder == villages,
              QStringLiteral("dragging Brookmere onto the folder in the list moves it there"));
    }
    shot(&w, QStringLiteral("v020-06-folders"));

    // templates and colors
    withModal<ObjectTemplatesDialog>(300, [](ObjectTemplatesDialog *d) {
        for (int i = 0; i < d->categoryList()->count(); ++i)
            if (d->categoryList()->item(i)->text() == QLatin1String("Locations"))
                d->categoryList()->setCurrentRow(i);
        d->setCategoryColor(QColor(QStringLiteral("#b03a2e")));
        d->addField(QStringLiteral("Founded"), FieldType::Date);
        d->fieldLabel()->setText(QStringLiteral("Founded in"));
        QTest::keyClick(d->fieldLabel(), Qt::Key_End);
        QTest::keyClicks(d->fieldLabel(), QStringLiteral(" year"));
        d->addCategory(QStringLiteral("Factions"));
        d->categoryName()->setText(QStringLiteral("Factions"));
        shot(d, QStringLiteral("v020-07-templates"));
        d->accept();
    });
    w.findChild<QAction *>(QStringLiteral("objectTemplates"))->trigger();
    settle(400);
    const Category *loc = st->category(QStringLiteral("locations"));
    bool founded = false;
    QString factionsId;
    if (loc)
        for (const FieldDef &f : loc->fields)
            founded = founded || (f.label == QLatin1String("Founded in year") && f.type == FieldType::Date);
    for (const Category &c : st->schema().categories)
        if (c.name == QLatin1String("Factions"))
            factionsId = c.id;
    check(loc && loc->color == QColor(QStringLiteral("#b03a2e")) && founded,
          QStringLiteral("Project > Object Templates and Colors: Locations gets a new color and a field “Founded in year”"));
    check(panel->categoryButton(QStringLiteral("locations"))->styleSheet().contains(QLatin1String("#b03a2e")),
          QStringLiteral("... the category button shows the new color at once"));
    check(!factionsId.isEmpty() && panel->categoryButton(factionsId), QStringLiteral("... and a new category “Factions” has its button"));
    panel->showObject(ashvale);
    settle(200);
    QWidget *foundedEditor = nullptr;
    for (const FieldDef &f : st->category(QStringLiteral("locations"))->fields)
        if (f.label == QLatin1String("Founded in year"))
            foundedEditor = form->fieldWidget(f.id);
    check(foundedEditor && foundedEditor->isVisibleTo(form), QStringLiteral("... Ashvale's form has the new field"));
    // an object's own color
    check(form->colorButton() && form->colorButton()->menu() && form->colorButton()->menu()->actions().size() == 2,
          QStringLiteral("the form has a color button (the category's color, or a color of its own)"));
    {
        Object o = *st->object(ashvale);
        o.color = QColor(QStringLiteral("#222266"));
        st->update(o);
    }
    settle(150);
    check(st->colorOf(*st->object(ashvale)) == QColor(QStringLiteral("#222266"))
              && st->colorOf(*st->object(st->objects(QStringLiteral("locations")).value(0)->id)).isValid(),
          QStringLiteral("an object can have a color of its own (the others keep the category's)"));
    shot(&w, QStringLiteral("v020-08-new-color-and-field"));

    // removing a category that has objects asks; "No" changes nothing
    QString question;
    withModal<ObjectTemplatesDialog>(300, [&question](ObjectTemplatesDialog *d) {
        for (int i = 0; i < d->categoryList()->count(); ++i)
            if (d->categoryList()->item(i)->text() == QLatin1String("Environments"))
                d->categoryList()->setCurrentRow(i);
        d->removeCurrentCategory();
        answerBox(300, QString(), QStringLiteral("No"), &question);
        d->accept();
    });
    w.findChild<QAction *>(QStringLiteral("objectTemplates"))->trigger();
    settle(800);
    check(question.contains(QLatin1String("1 object")) && st->category(QStringLiteral("environments")) && st->object(fens),
          QStringLiteral("removing a category with objects asks first; “No” keeps it and its objects"), question);

    // documents: "Appears in", and "In this document"
    panel->showObject(shelin);
    settle(200);
    auto *scenes = qobject_cast<ChipList *>(form->fieldWidget(QStringLiteral("scenes")));
    withModal<ObjectChooser>(300, [](ObjectChooser *c) {
        c->searchField()->setText(QStringLiteral("Market"));
        settle(100);
        c->list()->setCurrentRow(0);
        static_cast<QDialog *>(c)->accept();
    });
    if (scenes)
        QTest::mouseClick(scenes->addButton(), Qt::LeftButton);
    settle(300);
    check(st->object(shelin)->ids(QStringLiteral("scenes")) == QStringList{market->id} && chipTexts(scenes) == QStringList{QStringLiteral("The Market")},
          QStringLiteral("“Appears in” links Shelin to the document “The Market”"));
    binder->select({other}, true);
    settle(300);
    const bool noRow = !panel->documentChips()->isVisible();
    binder->select({market}, true);
    settle(300);
    check(noRow && panel->documentChips()->isVisible() && chipTexts(panel->documentChips()) == QStringList{QStringLiteral("Shelin")},
          QStringLiteral("with “The Market” in the editor the sheet shows “In this document: Shelin” (not for other documents)"));
    check(ob->font().underline(), QStringLiteral("... and the Objects button is marked like tabs with content"));
    panel->showObject(fens);
    settle(150);
    QTest::mouseClick(panel->documentChips()->chipButton(shelin), Qt::LeftButton);
    settle(200);
    check(panel->currentObject() == shelin, QStringLiteral("a click on the chip of the document shows Shelin"));
    shot(&w, QStringLiteral("v020-09-in-this-document"));
    binder->select({other}, true);
    settle(300);
    scenes = qobject_cast<ChipList *>(form->fieldWidget(QStringLiteral("scenes"))); // the form was made again for Shelin
    if (scenes && scenes->chipButton(market->id))
        QTest::mouseClick(scenes->chipButton(market->id), Qt::LeftButton);
    settle(400);
    EditorPane *pane = mainPane(&w);
    check(pane && pane->currentItem() == market, QStringLiteral("a click on “The Market” in Shelin's form opens it in the editor"));

    // Edit > Link to Object... in the text of a document, and a click on that link
    TextEditor *ed = pane ? pane->currentEditor() : nullptr;
    if (ed) {
        QApplication::setActiveWindow(&w);
        ed->setFocus();
        QTextCursor c = ed->textCursor();
        c.movePosition(QTextCursor::End);
        ed->setTextCursor(c);
        settle(100);
        withModal<ObjectChooser>(300, [](ObjectChooser *ch) {
            ch->searchField()->setText(QStringLiteral("Fens"));
            settle(100);
            ch->list()->setCurrentRow(0);
            static_cast<QDialog *>(ch)->accept();
        });
        w.findChild<QAction *>(QStringLiteral("objectLink"))->trigger();
        settle(300);
        check(linkHrefIn(p->docs()->text(market->id)) == linkTo(fens) && ed->toPlainText().contains(QLatin1String("Misty Fens")),
              QStringLiteral("Edit > Link to Object: the document text gets “Misty Fens” linked to the object"));
        inspector->showTab(Inspector::Notes);
        settle(150);
        QTest::mouseClick(ed->viewport(), Qt::LeftButton, {}, linkPoint(ed));
        settle(400);
        check(inspector->currentTab() == Inspector::Objects && panel->currentObject() == fens,
              QStringLiteral("... a click on it shows the object in the inspector"));
        shot(&w, QStringLiteral("v020-10-link-in-document"));
        p->save(&err);
        check(readAll(p->docs()->textPath(market->id)).contains(linkTo(fens)), QStringLiteral("... the link is saved with the text"));
    } else {
        check(false, QStringLiteral("(editor of The Market)"));
    }

    // a picture
    {
        QImage img(2400, 1200, QImage::Format_RGB32);
        img.fill(QColor(QStringLiteral("#8899aa")));
        const QString file = dir.path() + QLatin1String("/portrait.png");
        img.save(file);
        panel->showObject(shelin);
        settle(150);
        form->setPicture(QStringLiteral("picture"), file);
        settle(200);
        const QString name = st->object(shelin)->text(QStringLiteral("picture"));
        const QImage stored(st->picturePath(name));
        auto *pic = qobject_cast<QLabel *>(form->fieldWidget(QStringLiteral("picture")));
        check(!name.isEmpty() && stored.width() == 1600 && pic && !pic->pixmap().isNull(),
              QStringLiteral("a picture is copied into the project (made 1600 px wide) and shown in the form"),
              QStringLiteral("%1, %2 px").arg(name).arg(stored.width()));
    }

    // the window of its own
    w.findChild<QAction *>(QStringLiteral("objectsWindow"))->trigger();
    settle(400);
    ObjectsWindow *ow = ObjectsWindow::existing(p);
    check(ow && ow->isVisible() && ow->panel()->currentObject() == shelin,
          QStringLiteral("Window > Objects opens the large window, at the object of the inspector"));
    if (ow) {
        ow->resize(1200, 760);
        shot(ow, QStringLiteral("v020-11-window"));
        // the dark theme
        AppSettings &as = AppSettings::instance();
        as.theme = QStringLiteral("dark");
        as.save();
        theme::apply();
        settle(300);
        shot(ow, QStringLiteral("v020-11b-window-dark"));
        shot(&w, QStringLiteral("v020-11c-inspector-dark"));
        auto *catLabel = ow->findChild<QLabel *>(QStringLiteral("objectCategory"));
        check(catLabel && catLabel->palette().color(QPalette::WindowText) == QColor(Qt::white),
              QStringLiteral("dark theme: the stripe keeps its color, its text is readable (white on purple)"));
        as.theme = QStringLiteral("light");
        as.save();
        theme::apply();
        settle(200);
        ow->panel()->form()->nameField()->setText(QStringLiteral("Shelin Ardent"));
        emit ow->panel()->form()->nameField()->textEdited(QStringLiteral("Shelin Ardent"));
        settle(200);
        panel->setCategory(QStringLiteral("characters"));
        settle(150);
        check(treeItem(panel->tree(), QStringLiteral("Shelin Ardent")) && form->nameField()->text() == QLatin1String("Shelin Ardent"),
              QStringLiteral("... a change there shows in the inspector at once"));
        // Open in Window from the inspector's button: the same window
        QTest::mouseClick(w.findChild<QToolButton *>(QStringLiteral("objectsOpenWindow")), Qt::LeftButton);
        settle(200);
        int windows = 0;
        for (QWidget *tw : QApplication::topLevelWidgets())
            windows += (qobject_cast<ObjectsWindow *>(tw) && tw->isVisible()) ? 1 : 0;
        check(windows == 1, QStringLiteral("... one window per project"));
    }

    // deleting with the question
    panel->showObject(fens);
    settle(150);
    answerBox(300, QString(), QStringLiteral("Yes"));
    panel->deleteSelected(true);
    settle(300);
    check(!st->object(fens) && st->object(ashvale)->ids(QStringLiteral("environment")).isEmpty()
              && !chipTexts(ow ? ow->panel()->form()->backlinks() : nullptr).join(QLatin1Char(' ')).contains(QLatin1String("Fens")),
          QStringLiteral("deleting Misty Fens (after the question) removes it and the links to it"));
    // the link in the document text stays, and says that the object is gone
    QString gone;
    binder->select({market}, true);
    settle(300);
    if (TextEditor *me = pane ? pane->currentEditor() : nullptr) {
        answerBox(300, QString(), QStringLiteral("OK"), &gone);
        QTest::mouseClick(me->viewport(), Qt::LeftButton, {}, linkPoint(me));
    }
    settle(600);
    check(gone.contains(QLatin1String("no longer exists")), QStringLiteral("... a link to it in a document says the object no longer exists"));

    // the inspector tab is part of layouts
    inspector->showTab(Inspector::Objects);
    const QVariantMap lay = w.captureLayout();
    inspector->showTab(Inspector::Notes);
    w.applyLayout(lay);
    settle(200);
    check(inspector->currentTab() == Inspector::Objects, QStringLiteral("a window layout keeps the Objects tab"));

    // menus
    for (const char *k : {"objectLink", "objectsWindow", "objectTemplates", "inspect6"})
        check(w.findChild<QAction *>(QLatin1String(k)) && w.findChild<QAction *>(QLatin1String(k))->isEnabled(),
              QStringLiteral("menu command %1 is there and enabled with a project").arg(QLatin1String(k)));

    // closed and opened again
    const QString idx = p->indexPath();
    const int count = st->count();
    w.closeProject();
    settle(300);
    check(!ObjectsWindow::existing(p) && QFile::exists(folder + QLatin1String("/objects/schema.json")),
          QStringLiteral("closing the project saves the objects and closes their window"));
    for (const char *k : {"objectLink", "objectsWindow", "objectTemplates", "inspect6"})
        check(!w.findChild<QAction *>(QLatin1String(k))->isEnabled(),
              QStringLiteral("... without a project %1 is off").arg(QLatin1String(k)));
    w.openProject(idx);
    settle(400);
    Project *p2 = w.project();
    check(p2 && p2->objects()->count() == count && p2->objects()->objects(QStringLiteral("locations")).size() == 3,
          QStringLiteral("after reopening: the objects are there"),
          p2 ? QString::number(p2->objects()->count()) : QStringLiteral("no project"));
    if (p2) {
        bool linked = false;
        for (const Object *o : p2->objects()->objects(QStringLiteral("characters")))
            linked = linked || (o->name == QLatin1String("Shelin Ardent") && o->ids(QStringLiteral("livesIn")).size() == 1);
        check(linked, QStringLiteral("... with their links"));
    }
    w.closeProject();
    delete wp;
    settle(200);
}

// =====================================================================================
// 3. data safety
// =====================================================================================

// control characters in titles and notes (pasted from other programs) never make the
// project file unreadable; a file that has them is repaired when it is opened
static void controlCharacterChecks()
{
    section(QStringLiteral("Data safety: control characters"));
    QTemporaryDir dir;
    QString err;
    auto proj = novelProject(dir.path(), QStringLiteral("Controls"), &err);
    Project *p = proj.get();
    BinderItem *it = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Bad\x01Title\x0B" "End"));
    it->synopsis = QStringLiteral("Line\x02 with\x1F codes\x0C" " here");
    p->markIndexDirty();
    check(p->save(&err), QStringLiteral("a title with control characters is saved"), err);
    QDomDocument dom;
    const QByteArray raw = [&] {
        QFile f(p->indexPath());
        f.open(QIODevice::ReadOnly);
        return f.readAll();
    }();
    QString perr;
    check(dom.setContent(raw, &perr), QStringLiteral("... the project file is valid XML (before 0.20 it could no longer be opened)"), perr);
    const QString id = it->id, folder = p->folderPath(), idx = p->indexPath();
    proj.reset();
    proj = Project::open(folder, &err);
    check(proj && proj->item(id) && proj->item(id)->title == QLatin1String("BadTitle End") && proj->openNotice.isEmpty(),
          QStringLiteral("... and opens: the title without the codes (a vertical tab became a space)"),
          proj && proj->item(id) ? proj->item(id)->title : err);
    proj.reset();
    // a file written before 0.20 with such a character in it
    {
        QByteArray bad = raw;
        bad.replace("BadTitle End", "Bad\x01Title End");
        QFile f(idx);
        f.open(QIODevice::WriteOnly);
        f.write(bad);
    }
    proj = Project::open(folder, &err);
    check(proj && proj->item(id) && proj->item(id)->title == QLatin1String("BadTitle End") && !proj->openNotice.isEmpty(),
          QStringLiteral("a project file with a control character (from an older version) opens, repaired, with a notice"),
          proj ? proj->openNotice : err);
    proj.reset();

    // the window shows that notice
    {
        QFile f(idx);
        f.open(QIODevice::ReadWrite);
        QByteArray bad = f.readAll();
        bad.replace("BadTitle End", "Bad\x01Title End");
        f.resize(0);
        f.write(bad);
    }
    auto *w = new MainWindow;
    w->show();
    bool seen = false;
    QString text;
    answerBox(600, QStringLiteral("openNotice"), QStringLiteral("OK"), &text, &seen);
    w->openProject(idx);
    settle(800);
    check(seen && text.contains(QLatin1String("control")), QStringLiteral("... the window tells the writer what was repaired"), text);
    w->closeProject();
    delete w;

    // mind maps
    mindmap::Board b;
    mindmap::Note n;
    n.id = 1;
    n.text = QStringLiteral("Note\x01 with\x08 codes");
    b.notes << n;
    const QString mmFile = dir.path() + QLatin1String("/map.scap");
    b.save(mmFile, &err);
    mindmap::Board b2;
    check(b2.load(mmFile, &err) && b2.notes.size() == 1 && b2.notes.first().text == QLatin1String("Note with codes"),
          QStringLiteral("a mind map note with control characters is saved and opens again"), err);
}

// a text file that is there but cannot be read: a warning, read-only, never saved over
static void unreadableFileChecks()
{
    section(QStringLiteral("Data safety: a text file that cannot be read"));
    QTemporaryDir dir;
    QString err;
    auto proj = novelProject(dir.path(), QStringLiteral("Locked"), &err);
    Project *p = proj.get();
    BinderItem *it = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Locked Scene"));
    setText(p, it, QStringLiteral("The secret words."));
    p->save(&err);
    const QString path = p->docs()->textPath(it->id), idx = p->indexPath(), id = it->id;
    proj.reset();
    // the file is replaced by something that exists but cannot be opened for reading
    // (a local socket: even root cannot read it; a file without read permission does the
    // same for a normal user)
    QFile::remove(path);
    const int sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const QByteArray pb = QFile::encodeName(path);
    bool made = pb.size() < int(sizeof(addr.sun_path));
    if (made) {
        std::memcpy(addr.sun_path, pb.constData(), size_t(pb.size()));
        made = ::bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0;
    }
    check(made && QFileInfo::exists(path) && !QFileInfo(path).isFile(), QStringLiteral("(the text file was replaced by one that cannot be read)"),
          path);

    auto *w = new MainWindow;
    w->resize(1200, 800);
    w->show();
    w->openProject(idx);
    settle(300);
    Project *q = w->project();
    BinderItem *qi = q ? q->item(id) : nullptr;
    auto *binder = w->findChild<BinderPanel *>();
    bool warned = false;
    QString text;
    answerBox(400, QStringLiteral("unreadableDocumentNotice"), QStringLiteral("OK"), &text, &warned);
    if (binder && qi)
        binder->select({qi}, true);
    settle(900);
    check(warned && text.contains(QLatin1String("Locked Scene")), QStringLiteral("opening it shows a warning “Document Cannot Be Read”"), text);
    EditorPane *pane = mainPane(w);
    TextEditor *ed = pane ? pane->currentEditor() : nullptr;
    check(ed && ed->isReadOnly() && ed->item() == qi && q->docs()->isUnreadable(id) && !ed->placeholderText().isEmpty(),
          QStringLiteral("... the editor is read-only for it and says why"),
          QStringLiteral("editor %1, read-only %2, unreadable %3").arg(ed ? 1 : 0).arg(ed && ed->isReadOnly()).arg(q && q->docs()->isUnreadable(id)));
    shot(w, QStringLiteral("v020-12-unreadable"));
    // even a change made by other means is not written over the file
    if (q) {
        QTextCursor c(q->docs()->text(id));
        c.insertText(QStringLiteral("typed over "));
        q->save(&err);
    }
    check(QFileInfo::exists(path) && !QFileInfo(path).isFile(),
          QStringLiteral("... saving the project does not replace the file (before 0.20 the empty text was saved over it)"));
    // another document still saves
    BinderItem *road = q ? q->model()->createItem(q->draft(), -1, ItemType::Text, QStringLiteral("Road")) : nullptr;
    if (road) {
        setText(q, road, QStringLiteral("Another text."));
        q->save(&err);
        check(readAll(q->docs()->textPath(road->id)).contains(QLatin1String("Another text")), QStringLiteral("... other documents are saved as usual"));
    }
    w->closeProject();
    delete w;
    ::close(sock);
    // after the file is back, the next opening shows the text normally
    QFile::remove(path);
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("The secret words.\n");
    }
    auto again = Project::open(idx, &err);
    check(again && again->docs()->text(id)->toPlainText().contains(QLatin1String("secret words")) && !again->docs()->isUnreadable(id),
          QStringLiteral("when the file can be read again, the text is there"));
}

// backups: Zip64 above 4 GiB, a temporary name until checked, the working copy of
// Scrivener-format projects, files that cannot be read
static void backupChecks()
{
    section(QStringLiteral("Data safety: backups"));
    QTemporaryDir dir;
    QString err;
    auto proj = novelProject(dir.path(), QStringLiteral("Big"), &err);
    Project *p = proj.get();
    BinderItem *it = p->model()->createItem(p->research(), -1, ItemType::Text, QStringLiteral("Notes"));
    setText(p, it, QStringLiteral("Small text."));
    p->save(&err);
    const QString bdir = dir.path() + QLatin1String("/backups");
    QString out;
    if (qEnvironmentVariableIsSet("QB_SKIP_BIG_BACKUP")) {
        info(QStringLiteral("(QB_SKIP_BIG_BACKUP: the backup of a 4.3 GB file is left out)"));
    } else {
        // a large file (sparse: no space on the drive) inside the project folder
        const QString big = p->folderPath() + QLatin1String("/research-recording.dat");
        {
            QFile f(big);
            f.open(QIODevice::WriteOnly);
            f.write("start");
            f.resize(qint64(4300) * 1000 * 1000);
            f.seek(f.size() - 3);
            f.write("end");
        }
        QElapsedTimer t;
        t.start();
        const bool ok = backup::backupProject(p, bdir, 0, &out, &err);
        info(QStringLiteral("(backup of 4.3 GB: %1 s, %2 MB)").arg(t.elapsed() / 1000.0, 0, 'f', 1).arg(QFileInfo(out).size() / 1e6, 0, 'f', 1));
        check(ok && QFile::exists(out) && ZipWriter::verify(out, &err) >= 0,
              QStringLiteral("a backup with a file over 4 GiB is written and checked (Zip64)"), err);
        QProcess py;
        py.start(QStringLiteral("python3"),
                 {QStringLiteral("-I"), QStringLiteral("-c"),
                  QStringLiteral("import sys,zipfile\nz=zipfile.ZipFile(sys.argv[1])\n"
                                 "i=[x for x in z.infolist() if x.filename.endswith('research-recording.dat')][0]\n"
                                 "f=z.open(i)\nh=f.read(5)\nf.seek(i.file_size-3)\nt=f.read(3)\n"
                                 "print(i.file_size, h.decode(), t.decode(), z.testzip())"),
                  out});
        py.waitForFinished(600000);
        const QString res = QString::fromUtf8(py.readAllStandardOutput()).trimmed();
        check(res == QLatin1String("4300000000 start end None"),
              QStringLiteral("... Python's zipfile reads it: size 4,300,000,000 bytes, first and last bytes right, no CRC error"),
              res + QLatin1Char(' ') + QString::fromUtf8(py.readAllStandardError()).right(400));
        QFile::remove(big);
        QFile::remove(out);
    }
    check(QDir(bdir).entryList({QStringLiteral("*.part")}, QDir::Files).isEmpty(), QStringLiteral("no unfinished backup (.part) is left behind"));

    // a file that cannot be read: left out, named, the rest is in the backup
    const QString sockPath = p->folderPath() + QLatin1String("/research-sock");
    const QString unreadable = p->folderPath() + QLatin1String("/unreadable.txt");
    {
        QFile f(unreadable);
        f.open(QIODevice::WriteOnly);
        f.write("x");
    }
    QFile::setPermissions(unreadable, QFileDevice::Permissions());
    const bool asRoot = ::geteuid() == 0;
    err.clear();
    const bool ok2 = backup::backupProject(p, bdir, 0, &out, &err);
    if (asRoot) {
        check(ok2 && err.isEmpty(), QStringLiteral("(as root every file can be read: the backup has all files)"), err);
    } else {
        check(ok2 && err.contains(QLatin1String("unreadable.txt")),
              QStringLiteral("a file that cannot be read is left out and named; the backup is still made"), err);
    }
    QFile::setPermissions(unreadable, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    Q_UNUSED(sockPath);
}

// autosave while the writer types without a pause
static void autosaveChecks()
{
    section(QStringLiteral("Data safety: autosave while typing"));
    QTemporaryDir dir;
    QString err;
    auto proj = novelProject(dir.path(), QStringLiteral("Typing"), &err);
    Project *p = proj.get();
    BinderItem *it = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Long Session"));
    setText(p, it, QStringLiteral("Start."));
    p->save(&err);
    p->setAutosaveDelay(1000);
    p->setMaxSaveWait(2000);
    const QString path = p->docs()->textPath(it->id);
    QTextCursor c(p->docs()->text(it->id));
    c.movePosition(QTextCursor::End);
    QElapsedTimer t;
    t.start();
    qint64 savedAt = -1;
    int word = 0;
    while (t.elapsed() < 6000) {
        c.insertText(QStringLiteral(" word%1").arg(++word));
        settle(200); // a change every 0.2 s: never a pause of 1 s
        if (savedAt < 0 && readAll(path).contains(QLatin1String("word1 ")))
            savedAt = t.elapsed();
    }
    check(savedAt > 0 && savedAt < 4000,
          QStringLiteral("typing without a pause is saved at the latest after the longest wait (2 s here, 30 s in the program)"),
          QStringLiteral("first save after %1 ms").arg(savedAt));
    settle(1500);
    check(!p->hasUnsavedChanges() && readAll(path).contains(QStringLiteral("word%1").arg(word)),
          QStringLiteral("... and after the pause everything is saved"));
}

// Scrivener-format projects: the working copy
static void scrivenerChecks()
{
    section(QStringLiteral("Data safety: projects in Scrivener format"));
    QTemporaryDir dir;
    QString err;
    auto src = novelProject(dir.path(), QStringLiteral("Harbor"), &err);
    BinderItem *first = src->model()->createItem(src->draft(), -1, ItemType::Text, QStringLiteral("Arrival"));
    setText(src.get(), first, QStringLiteral("The ship came in."));
    src->save(&err);
    const QString scriv = dir.path() + QLatin1String("/Harbor.scriv");
    scrivener::WriteReport rep;
    check(scrivener::saveCopyAsScrivener(src.get(), scriv, &rep, &err), QStringLiteral("(a Scrivener project to work on)"), err);
    src.reset();
    auto res = scrivener::open(scriv, &err);
    Project *p = res.project.get();
    check(p && !p->scrivenerPath.isEmpty(), QStringLiteral("(opened in Scrivener format: a working copy)"), err);
    if (!p)
        return;
    const QString wc = p->folderPath();
    // objects live in the working copy
    p->objects()->create(QStringLiteral("characters"), QString(), QStringLiteral("Harbor Master"));
    // a new document: saved in the working copy, but the save into the .scriv did not end
    // (a crash between the texts and the .scrivx)
    BinderItem *kept = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Night Watch"));
    setText(p, kept, QStringLiteral("Nobody slept that night."));
    const QString keptId = kept->id;
    const QString scrivPath = p->scrivenerPath;
    p->scrivenerPath.clear();
    p->save(&err);
    p->scrivenerPath = scrivPath;
    {
        // what the cut-off save left in the .scriv: a text file, no entry in the .scrivx
        QDir().mkpath(scriv + QLatin1String("/Files/Data/00000000-0000-0000-0000-00000000C0DE"));
        QFile f(scriv + QLatin1String("/Files/Data/00000000-0000-0000-0000-00000000C0DE/content.rtf"));
        f.open(QIODevice::WriteOnly);
        f.write("{\\rtf1\\ansi Nobody slept that night.}");
    }
    res.project.reset();

    // opened again: the .scriv changed, it is read again; the document is kept
    res = scrivener::open(scriv, &err);
    p = res.project.get();
    BinderItem *back = p ? p->item(keptId) : nullptr;
    check(back && back->title == QLatin1String("Night Watch") && p->docs()->text(keptId)->toPlainText().contains(QLatin1String("Nobody slept")),
          QStringLiteral("a document that had not reached the .scriv is kept when the project is read again (before 0.20 it was lost)"),
          QStringLiteral("read again: %1; %2 %3").arg(res.reread).arg(err, res.warnings.join(QLatin1Char(' '))));
    check(res.warnings.join(QLatin1Char(' ')).contains(QLatin1String("kept")), QStringLiteral("... and the writer is told"),
          res.warnings.join(QLatin1Char(' ')));
    check(p && p->objects()->count() == 1, QStringLiteral("... the objects of the working copy are kept as well"));
    if (!p)
        return;
    // the backup holds the working copy (objects, keywords ...) next to the .scriv
    QString out;
    const bool bok = backup::backupProject(p, dir.path() + QLatin1String("/bak"), 0, &out, &err);
    QProcess py;
    py.start(QStringLiteral("python3"), {QStringLiteral("-I"), QStringLiteral("-c"),
                                         QStringLiteral("import sys,zipfile\nfor n in zipfile.ZipFile(sys.argv[1]).namelist(): print(n)"), out});
    py.waitForFinished(60000);
    const QString listing = QString::fromUtf8(py.readAllStandardOutput());
    check(bok && listing.contains(QLatin1String("Harbor.scriv/")) && listing.contains(QLatin1String(" - working copy of Quillbinder/"))
              && listing.contains(QLatin1String("/objects/characters/")),
          QStringLiteral("a backup of a Scrivener-format project has the .scriv and the working copy (with its objects)"),
          listing.left(600));
    const QString wc2 = p->folderPath();
    res.project.reset();
    // a crash after the old working copy was moved aside and before the new one was in place
    check(QDir().rename(wc2, wc2 + QLatin1String(".old")) && !QDir(wc2).exists(), QStringLiteral("(the working copy moved aside, as by such a crash)"));
    res = scrivener::open(scriv, &err);
    p = res.project.get();
    check(p && p->objects()->count() == 1 && p->item(keptId), QStringLiteral("the next opening finds the moved working copy again"), err);
    check(!QDir(wc2 + QLatin1String(".old")).exists(), QStringLiteral("... and nothing is left aside"));
    Q_UNUSED(wc);
    // File > Project Format > Save a Copy as Quillbinder Project: the objects come along
    if (p) {
        auto copy = scrivener::copyAsQuillbinder(p, dir.path(), QStringLiteral("Harbor Copy"), &err);
        check(copy && copy->objects()->count() == 1 && copy->scrivenerPath.isEmpty(),
              QStringLiteral("a copy in the format of this program has the objects as well"), err);
    }
}

// the spare copy of the project file
static void spareCopyChecks()
{
    section(QStringLiteral("Data safety: the spare copy of the project file"));
    QTemporaryDir dir;
    QString err;
    auto proj = novelProject(dir.path(), QStringLiteral("Spare"), &err);
    BinderItem *first = proj->model()->createItem(proj->draft(), -1, ItemType::Text, QStringLiteral("Chapter One Scene"));
    setText(proj.get(), first, QStringLiteral("It began in spring."));
    proj->save(&err);
    const QString idx = proj->indexPath(), firstId = first->id;
    proj.reset();
    auto *w = new MainWindow;
    w->resize(1200, 800);
    w->show();
    w->openProject(idx);
    settle(300);
    check(QFile::exists(Project::spareCopyOf(idx)), QStringLiteral("opening a project keeps a copy of its project file (settings/spare-copy.qbx)"));
    // written after the copy was made
    Project *p = w->project();
    BinderItem *later = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Written Later"));
    setText(p, later, QStringLiteral("A scene written after the copy.\nMore text."));
    const QString laterId = later->id;
    w->closeProject();
    // the project file is damaged
    {
        QFile f(idx);
        f.open(QIODevice::WriteOnly);
        f.write("<?xml version=\"1.0\"?><QuillbinderProject><Binder><Item ");
    }
    bool asked = false, noticed = false;
    QString notice;
    QTimer::singleShot(500, qApp, [&] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (box && box->objectName() == QLatin1String("spareCopyQuestion")) {
            asked = true;
            shot(box, QStringLiteral("v020-13-spare-copy-question"));
            answerBox(500, QStringLiteral("openNotice"), QStringLiteral("OK"), &notice, &noticed);
            if (auto *b = box->findChild<QPushButton *>(QStringLiteral("useSpareCopy")))
                b->click();
        } else if (QWidget *m = QApplication::activeModalWidget()) {
            info(QStringLiteral("(open: %1)").arg(m->objectName()));
            m->close();
        }
    });
    const bool opened = w->openProject(idx);
    settle(1200);
    check(asked, QStringLiteral("a damaged project file: the spare copy is offered"));
    p = w->project();
    check(opened && p && p->item(firstId), QStringLiteral("... “Use the Copy” opens the project from it"));
    check(noticed && notice.contains(QLatin1String("damaged")), QStringLiteral("... the writer is told where the damaged file is kept"), notice);
    const QStringList damaged = QDir(QFileInfo(idx).absolutePath()).entryList({QStringLiteral("*.qbx.damaged-*")}, QDir::Files);
    check(damaged.size() == 1, QStringLiteral("... the damaged file is kept (%1)").arg(damaged.value(0)));
    BinderItem *rec = p ? p->item(laterId) : nullptr;
    check(rec && rec->parent && rec->parent->title == QLatin1String("Recovered Documents") && rec->parent->parent == p->research()
              && rec->title == QLatin1String("A scene written after the copy.") && notice.contains(QLatin1String("Recovered Documents")),
          QStringLiteral("... a document written after the copy is in Research > Recovered Documents, titled by its first line"),
          rec ? rec->title : QStringLiteral("not found"));
    if (rec)
        w->findChild<BinderPanel *>()->select({rec}, true);
    shot(w, QStringLiteral("v020-14-recovered"));
    w->closeProject();
    delete w;
}

// number ranges in mind map files (from Scapple): a range up to the largest number
// made the program hang before 0.20
static void mindMapRangeChecks()
{
    section(QStringLiteral("Data safety: mind map number ranges"));
    mindmap::Board b;
    mindmap::Note a, c;
    a.id = 1;
    a.text = QStringLiteral("A");
    c.id = 2;
    c.text = QStringLiteral("B");
    b.notes << a << c;
    b.connect(1, 2);
    QByteArray xml = b.toXml();
    const bool replaced = xml.contains("<ConnectedNoteIDs>2</ConnectedNoteIDs>");
    xml.replace("<ConnectedNoteIDs>2</ConnectedNoteIDs>", "<ConnectedNoteIDs>2, 2147483600-2147483647, -2147483648-2147483647</ConnectedNoteIDs>");
    // in a child process with a time limit: a hang is a failed check, not the end of the run
    std::fflush(stdout);
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::alarm(20);
        mindmap::Board r;
        QString e;
        const bool ok = r.fromXml(xml, &e) && r.notes.size() == 2;
        ::_exit(ok ? 0 : 3);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    check(replaced && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          QStringLiteral("a mind map file with the ranges 2147483600-2147483647 and -2147483648-2147483647 opens at once"),
          WIFSIGNALED(status) ? QStringLiteral("ended by signal %1 (a hang: the time limit)").arg(WTERMSIG(status))
                              : QStringLiteral("exit %1").arg(WEXITSTATUS(status)));
}

// For tools/e2e-old-version.sh: a project with objects for an older version to open and save
// (--make-fixture <folder>), and the check that the objects are all there afterwards
// (--check-fixture <project folder>).
static int makeFixture(const QString &dir)
{
    QString err;
    auto proj = novelProject(dir, QStringLiteral("Objects Fixture"), &err);
    if (!proj) {
        std::printf("FAIL  fixture: %s\n", qPrintable(err));
        return 1;
    }
    Project *p = proj.get();
    BinderItem *market = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("The Market"));
    setText(p, market, QStringLiteral("Shelin crossed the market."));
    Store *st = p->objects();
    const QString shelin = st->create(QStringLiteral("characters"), QString(), QStringLiteral("Shelin"));
    const QString ashvale = st->create(QStringLiteral("locations"), QString(), QStringLiteral("Ashvale"));
    const QString fens = st->create(QStringLiteral("environments"), QString(), QStringLiteral("Misty Fens"));
    setValues(st, shelin,
              {{QStringLiteral("race"), QStringLiteral("Elf")},
               {QStringLiteral("livesIn"), QStringList{ashvale}},
               {QStringLiteral("scenes"), QStringList{market->id}},
               {QStringLiteral("background"), QStringLiteral("Grew up in [Ashvale](%1).").arg(linkTo(ashvale))}});
    setValues(st, ashvale, {{QStringLiteral("environment"), QStringList{fens}}});
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    img.save(dir + QLatin1String("/face.png"));
    setValues(st, shelin, {{QStringLiteral("picture"), st->importPicture(shelin, QStringLiteral("picture"), dir + QLatin1String("/face.png"))}});
    if (!p->save(&err)) {
        std::printf("FAIL  fixture: %s\n", qPrintable(err));
        return 1;
    }
    std::printf("%s\n", qPrintable(p->folderPath()));
    return 0;
}

static int checkFixture(const QString &folder)
{
    QString err;
    auto p = Project::open(folder, &err);
    check(p != nullptr, QStringLiteral("the project opens in this version"), err);
    if (!p)
        return failures;
    Store *st = p->objects();
    const Object *s = nullptr;
    for (const Object *o : st->objects(QStringLiteral("characters")))
        if (o->name == QLatin1String("Shelin"))
            s = o;
    const Object *home = s ? st->object(s->ids(QStringLiteral("livesIn")).value(0)) : nullptr;
    const Object *env = home ? st->object(home->ids(QStringLiteral("environment")).value(0)) : nullptr;
    check(st->count() == 3 && s && home && env && env->name == QLatin1String("Misty Fens"),
          QStringLiteral("the objects and their links are all there (Shelin -> Ashvale -> Misty Fens)"));
    check(s && QFile::exists(st->picturePath(s->text(QStringLiteral("picture")))) && !s->ids(QStringLiteral("scenes")).isEmpty()
              && p->item(s->ids(QStringLiteral("scenes")).value(0)),
          QStringLiteral("... with the picture and the link to the document"));
    bool note = false;
    for (BinderItem *it : p->allItems())
        note = note || it->title == QLatin1String("E2E Note");
    check(note, QStringLiteral("the document the older version added is in the binder"));
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qunsetenv("QB_NO_DIALOGS");
    setupApp(QStringLiteral("QuillbinderV020Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 && argv[1][0] != '-' ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    styleApp();
    if (argc > 2 && QByteArray(argv[1]) == "--make-fixture")
        return makeFixture(QString::fromLocal8Bit(argv[2]));
    if (argc > 2 && QByteArray(argv[1]) == "--check-fixture")
        return checkFixture(QString::fromLocal8Bit(argv[2]));

    storageChecks();
    uiChecks();
    controlCharacterChecks();
    unreadableFileChecks();
    autosaveChecks();
    scrivenerChecks();
    spareCopyChecks();
    mindMapRangeChecks();
    backupChecks();

    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
