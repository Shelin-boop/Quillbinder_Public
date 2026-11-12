// The system test, cases for what came after 0.64.2 (added for 0.90.0 beta, 2026-11-12): the save journal (0.65.0),
// failed saves while typing (0.70.0), the German interface (0.70.0), a damaged document (0.81.1), a saved web page
// (0.81.3), and everything the Scrivener format exchanges since 0.81.2 (keywords, custom metadata, styles, inline
// notes, bookmarks, targets, the writing history, the corkboard). Specification: the project document
// claude/quillbinder-test-procedure-crashsafe.md, section 9.
#include "cs.h"

#include "core/SaveBatch.h"
#include "core/Snapshots.h"
#include "core/WebArchive.h"
#include "import/FileImporter.h"
#include "import/ImporterApi.h"
#include "import/ScrivenerFormat.h"
#include "import/ScrivenerImporter.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"
#include "ui/BinderPanel.h"
#include "ui/Corkboard.h"
#include "ui/EditorPane.h"
#include "ui/Language.h"
#include "ui/LinkOpen.h"
#include "ui/MainWindow.h"
#include "ui/MediaView.h"
#include "ui/TextEditor.h"

#include <QDir>
#include <QDirIterator>
#include <QDomDocument>
#include <QFile>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>

namespace cs {

namespace {

QLabel *saveStateOf(MainWindow *w)
{
    return w ? w->findChild<QLabel *>(QStringLiteral("saveState")) : nullptr;
}

QWidget *visibleNamed(const QString &name)
{
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (tl->isVisible() && tl->objectName() == name)
            return tl;
    return nullptr;
}

} // namespace

// ------------------------------------------------------------------------------------------------------------------
// C-43: the save journal. A save of several files cut off after its journal: the next opening finishes it, every file
// in place; a journal that cannot be read keeps its staged files aside; a save cut off before the journal leaves
// nothing behind.

void caseC43()
{
    if (runUnderFaults())
        return;
    for (const QString &fx : {QStringLiteral("fx1-qb"), QStringLiteral("fx1-scriv")}) {
        const bool scriv = fx.endsWith(QLatin1String("scriv"));
        const QString what = scriv ? QStringLiteral("Scrivener format") : QStringLiteral("Quillbinder format");
        step(QStringLiteral("C-43 ") + what);
        const QString copy = fixtureCopy(fx);   // the file to open: the .qbx, or the .scriv
        QString err;
        std::unique_ptr<Project> p;
        if (scriv) {
            scrivener::OpenResult r = scrivener::open(scrivener::scrivFolder(copy), &err);
            p = std::move(r.project);
        } else {
            p = Project::open(copy, &err);
        }
        check(bool(p), what + QStringLiteral(": the fixture opens"), err);
        if (!p)
            continue;
        // three texts changed: one save of several files, its renames cut off after the first
        const QList<BinderItem *> texts = textItems(p.get()).mid(0, 3);
        for (int i = 0; i < texts.size(); ++i)
            fillText(p.get(), texts[i], {QStringLiteral("C-43 journal text %1.").arg(i)});
        const QJsonObject want = modelOf(p.get(), false);
        const QString folder = p->folderPath();
        setFaults({QStringLiteral("rename %1/content 5 1 0").arg(QDir(folder).absolutePath())});
        err.clear();
        const bool ok = p->save(&err);
        clearFaults();
        const QStringList fired = firedFaults(true);
        fact(QStringLiteral("C-43 %1: the cut save").arg(what), QStringLiteral("ok=%1 fired=%2 error=%3").arg(ok).arg(fired.size()).arg(err.left(120)));
        check(!fired.isEmpty(), what + QStringLiteral(": the fault hit the save"), fired.join(QLatin1Char(' ')));
        check(!ok, what + QStringLiteral(": the cut save says it failed"), err);
        const bool journalLeft = QFileInfo::exists(folder + QStringLiteral("/settings/journal.json"));
        fact(QStringLiteral("C-43 %1: journal left").arg(what), journalLeft);
        // the program ends here (as after a crash): the next opening finishes the save
        p.reset();
        std::unique_ptr<Project> q = Project::open(projectFileIn(folder), &err);
        check(bool(q), what + QStringLiteral(": opened again after the cut save"), err);
        if (!q)
            continue;
        const QString diff = modelDifference(want, modelOf(q.get(), false));
        check(diff.isEmpty(), what + QStringLiteral(": every changed text is there after the opening (the journal finished)"), diff.left(600));
        check(!QFileInfo::exists(folder + QStringLiteral("/settings/journal.json")), what + QStringLiteral(": no journal is left"));
        int staged = 0;
        QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            staged += savebatch::isStaged(it.next());
        check(staged == 0, what + QStringLiteral(": no staged file is left"), QString::number(staged));
        q.reset();
    }
    // a journal that cannot be read: its staged files are kept aside, nothing is replaced
    {
        const QString qbx = fixtureCopy(QStringLiteral("fx1-qb"));
        const QString copy = QFileInfo(qbx).absolutePath();
        QDir().mkpath(copy + QStringLiteral("/settings"));
        writeText(copy + QStringLiteral("/settings/journal.json"), QByteArray("{ this is not a journal"));
        writeText(copy + QStringLiteral("/content/stray.txt") + QLatin1String(savebatch::kStageSuffix), QByteArray("staged words"));
        QString err;
        std::unique_ptr<Project> q = Project::open(qbx, &err);
        check(bool(q), QStringLiteral("a journal that cannot be read: the project opens"), err);
        bool keptAside = false;
        QDirIterator it(copy + QStringLiteral("/settings/conflicts"), QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext())
            keptAside = it.next().contains(QLatin1String("unreadable save journal")) || keptAside;
        check(keptAside, QStringLiteral("a journal that cannot be read: it and its staged files are kept aside"));
    }
}

// ------------------------------------------------------------------------------------------------------------------
// S-N01: failed saves while writing. The sign in the status bar, the notice that does not take the keys, the retries,
// and not one key lost; the next save that works holds everything.

void caseSN01()
{
    if (runUnderFaults())
        return;
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w ? w->project() : nullptr;
    if (!p)
        return;
    BinderItem *doc = textItems(p).value(2);
    TextEditor *e = showDoc(w, doc);
    check(e != nullptr, QStringLiteral("a text is shown"));
    if (!e)
        return;
    const QString folder = p->folderPath();
    QLabel *sign = saveStateOf(w);
    auto signSays = [&](const QString &start) { return sign && sign->text().startsWith(start); };
    typeInto(e, QStringLiteral(" Before the disk filled up."));
    check(waitFor([&] { return signSays(QStringLiteral("Saved")); }, 20000), QStringLiteral("the automatic save: the sign says Saved"),
          sign ? sign->text() : QString());
    // the disk is full for every file of the project; the automatic save fails while the writer goes on
    setFaults({QStringLiteral("write %1 28").arg(QDir(folder).absolutePath())});
    typeInto(e, QStringLiteral(" Written while nothing could be saved."));
    check(waitFor([&] { return signSays(QStringLiteral("Not saved:")); }, 20000), QStringLiteral("the sign turns red: not saved"),
          sign ? sign->text() : QString());
    check(!firedFaults(false).isEmpty(), QStringLiteral("the full disk hit the automatic save"));
    QWidget *notice = nullptr;
    waitFor([&] { return (notice = visibleNamed(QStringLiteral("saveFailedNotice"))) != nullptr; }, 10000);
    check(notice != nullptr, QStringLiteral("the notice of the failed save is shown"));
    shot(w, QStringLiteral("SN01-failed-save"));
    // writing goes on while the notice is there: the keys go to the text, not to the notice
    const QString more = QStringLiteral(" And more words, every one of them kept, while the program tried again.");
    typeInto(e, more);
    check(docText(p, doc).contains(more), QStringLiteral("the keys typed with the notice open reach the text"), docText(p, doc).left(200));
    check(!notice || notice->isVisible(), QStringLiteral("the notice stays (it does not take the keys)"));
    // the disk has room again: the program's own retry (every 15 s) saves
    clearFaults();
    firedFaults(true);
    const bool saved = waitFor([&] { return signSays(QStringLiteral("Saved")); }, 45000);
    check(saved, QStringLiteral("the retry saves by itself once the disk has room"), sign ? sign->text() : QString());
    {
        // what is on the disk now
        const QString onDisk = readText(p->docs()->textPath(doc->id));
        fact(QStringLiteral("S-N01 on disk after the retry"), QStringLiteral("written-while %1, more %2, dirty %3")
                                                                 .arg(onDisk.contains(QLatin1String("Written while nothing")))
                                                                 .arg(onDisk.contains(QLatin1String("every one of them kept")))
                                                                 .arg(p->hasUnsavedChanges()));
    }
    if (QWidget *n = visibleNamed(QStringLiteral("saveFailedNotice")))
        n->close();
    const QString title = doc->title;   // the item goes with the window
    w = reopenWindow(w);
    p = w ? w->project() : nullptr;
    const QString text = p ? docText(p, itemByTitle(p, title)) : QString();
    check(text.contains(QLatin1String("Written while nothing could be saved.")) && text.contains(more.trimmed()),
          QStringLiteral("after reopening: everything typed is there"),
          QStringLiteral("written-while: %1, more: %2, start: %3").arg(text.contains(QLatin1String("Written while nothing could be saved.")))
              .arg(text.contains(more.trimmed())).arg(text.left(160)));
    if (w)
        finishProject(w);
}

// ------------------------------------------------------------------------------------------------------------------
// S-N02: the German interface. Every menu opens and has German names, File > Options opens and closes, writing and
// saving work, the project's files are the same as with the English interface (nothing of the language goes into them).

void caseSN02()
{
    if (variant().isEmpty()) {
        QString fails;
        const int f = subCase(QStringLiteral("german"), {{QStringLiteral("QB_LANGUAGE"), QStringLiteral("de")}}, &fails);
        check(f == 0, QStringLiteral("the case with the German interface (sub-run)"), f < 0 ? QStringLiteral("did not end") : fails);
        return;
    }
    language::install();   // as main() does: QB_LANGUAGE=de
    check(language::active() == QLatin1String("de"), QStringLiteral("the German interface is active"), language::active());
    const QString copy = scrivener::scrivFolder(fixtureCopy(QStringLiteral("fx1-scriv")));
    MainWindow *w = openWindow(scrivener::scrivxIn(copy));
    Project *p = w ? w->project() : nullptr;
    if (!p)
        return;
    QStringList titles, untranslated;
    for (QAction *a : w->menuBar()->actions()) {
        titles << a->text().remove(QLatin1Char('&'));
        if (QMenu *m = a->menu()) {
            emit m->aboutToShow();
            for (QAction *x : m->actions())
                if (!x->isSeparator() && x->text().isEmpty())
                    untranslated << a->text() + QStringLiteral(": (an entry without a name)");
            emit m->aboutToHide();
        }
    }
    fact(QStringLiteral("S-N02 menu titles"), titles.join(QLatin1String(", ")));
    check(titles.contains(QLatin1String("Datei")) && titles.contains(QLatin1String("Bearbeiten")),
          QStringLiteral("the menus have German names"), titles.join(QLatin1String(", ")));
    check(untranslated.isEmpty(), QStringLiteral("every menu entry has a name"), untranslated.join(QLatin1String("; ")));
    shot(w, QStringLiteral("SN02-german"));
    closeDialogsLater(1500);
    chooseByMenu(w, QStringLiteral("options"));
    settle(300);
    BinderItem *doc = textItems(p).value(1);
    const QString title = doc ? doc->title : QString();
    TextEditor *e = showDoc(w, doc);
    if (e)
        typeInto(e, QStringLiteral(" Ein Satz mit Umlauten: äöü ÄÖÜ ß."));
    const bool typed = p && docText(p, doc).contains(QStringLiteral("äöü ÄÖÜ ß"));
    check(typed, QStringLiteral("German letters typed"), p ? docText(p, doc).right(60) : QString());
    w = reopenWindow(w);
    p = w ? w->project() : nullptr;
    check(p && docText(p, itemByTitle(p, title)).contains(QStringLiteral("äöü ÄÖÜ ß")), QStringLiteral("German letters typed and saved"));
    // nothing of the language in the .scriv: the names of "No Label" and "No Status" stay English
    QDomDocument dom;
    dom.setContent(readText(scrivener::scrivxIn(copy)).toUtf8());
    const QString noLabel = dom.documentElement().firstChildElement(QStringLiteral("LabelSettings")).firstChildElement(QStringLiteral("Labels"))
                                .firstChildElement(QStringLiteral("Label")).text();
    check(!noLabel.startsWith(QLatin1String("Kein")), QStringLiteral("the project file is not written in German"), noLabel);
    if (w)
        finishProject(w);
}

// ------------------------------------------------------------------------------------------------------------------
// S-N03: a damaged document. Shown, never written; the notice; Roll Back to a snapshot makes it editable again with
// the damaged file kept aside.

void caseSN03()
{
    const QString copy = fixtureCopy(QStringLiteral("fx1-qb"));
    QString err;
    QString title, file;
    {
        std::unique_ptr<Project> p = Project::open(copy, &err);
        check(bool(p), QStringLiteral("the fixture opens"), err);
        if (!p)
            return;
        BinderItem *doc = textItems(p.get()).value(3);
        title = doc->title;
        check(snapshots::take(p.get(), doc->id, QStringLiteral("Before the damage"), &err), QStringLiteral("a snapshot is taken"), err);
        p->save(&err);
        file = p->docs()->textPath(doc->id);
    }
    // the file damaged elsewhere: bytes that are no text in its encoding
    QByteArray bytes = readText(file).toUtf8();
    bytes.insert(bytes.size() / 2, QByteArray("\xff\xfe\x00\x00\xc3\x28 damaged", 15));
    writeText(file, bytes);
    MainWindow *w = openWindow(copy);
    Project *p = w ? w->project() : nullptr;
    if (!p)
        return;
    BinderItem *doc = itemByTitle(p, title);
    TextEditor *e = showDoc(w, doc);
    settle(500);
    check(e && e->isReadOnly(), QStringLiteral("the damaged text is shown read-only"));
    check(visibleNamed(QStringLiteral("damagedDocumentNotice")) != nullptr || p->docs()->isUnreadable(doc->id),
          QStringLiteral("the program says the text is damaged"));
    shot(w, QStringLiteral("SN03-damaged"));
    closeDialogsLater(100);
    settle(300);
    const QByteArray onDisk = readText(file).toUtf8();
    if (e)
        typeInto(e, QStringLiteral("x"));
    w->project()->save();
    check(readText(file).toUtf8() == onDisk, QStringLiteral("the damaged file is not written"));
    // Roll Back
    const QList<Snapshot> list = snapshots::list(p, doc->id);
    QString kept;
    check(!list.isEmpty() && snapshots::rollBack(p, list.first(), &err, false, &kept), QStringLiteral("Roll Back to the snapshot"), err);
    settle(300);
    TextEditor *e2 = showDoc(w, doc);
    check(e2 && !e2->isReadOnly(), QStringLiteral("after Roll Back the text can be edited again"));
    check(!kept.isEmpty() && QFileInfo::exists(kept), QStringLiteral("the damaged file is kept aside"), kept);
    if (e2)
        typeInto(e2, QStringLiteral(" Repaired."));
    w = reopenWindow(w);
    p = w ? w->project() : nullptr;
    check(p && docText(p, itemByTitle(p, title)).contains(QLatin1String("Repaired.")), QStringLiteral("the repaired text is saved"));
    if (w)
        finishProject(w);
}

// ------------------------------------------------------------------------------------------------------------------
// S-N04: a saved web page: the page shown with its address, Open Saved Page in Browser (a copy in the Downloads
// folder), Open Original Address; in Scrivener format it goes in as Scrivener's own saved page and reads back.

void caseSN04()
{
    const QString copy = scrivener::scrivFolder(fixtureCopy(QStringLiteral("fx1-scriv")));
    MainWindow *w = openWindow(scrivener::scrivxIn(copy));
    Project *p = w ? w->project() : nullptr;
    if (!p)
        return;
    const QString page = ctx().work + QStringLiteral("/page/content.html");
    writeText(page, QByteArray("<html><head><title>Harbor</title></head><body><h1>Harbor office</h1><p>Open from 08:00 to 16:00.</p>"
                               "<img src=\"pics/flag.png\"></body></html>"));
    QDir().mkpath(ctx().work + QStringLiteral("/page/pics"));
    testPicture(20, 12).save(ctx().work + QStringLiteral("/page/pics/flag.png"));
    importers::WebPage wp;
    wp.title = QStringLiteral("Harbor office");
    wp.asWebPage = true;
    wp.htmlFile = page;
    wp.url = QStringLiteral("https://example.org/harbor");
    BinderItem *web = importer::addWebPage(p, wp, p->research(), -1, nullptr);
    check(web != nullptr, QStringLiteral("a web page is added"));
    if (!web)
        return;
    web->sourceUrl = QStringLiteral("https://example.org/harbor");
    QStringList opened;
    links::setOpenerForTests([&opened](const QUrl &u) { opened << u.toString(); });
    selectItems(w, {web});
    settle(600);
    MediaView *view = nullptr;
    for (MediaView *v : w->findChildren<MediaView *>())
        if (v->isVisible())
            view = v;
    check(view != nullptr, QStringLiteral("the page is shown"));
    if (view) {
        auto *address = view->findChild<QLabel *>(QStringLiteral("webAddress"));
        check(address && address->text().contains(QLatin1String("example.org/harbor")), QStringLiteral("its address is shown"));
        if (auto *b = view->findChild<QPushButton *>(QStringLiteral("webOpenSaved")))
            b->click();
        if (auto *b = view->findChild<QPushButton *>(QStringLiteral("webOpenOriginal")))
            b->click();
        settle(300);
        shot(w, QStringLiteral("SN04-web-page"));
    }
    const bool savedCopy = std::any_of(opened.cbegin(), opened.cend(), [](const QString &u) {
        return u.startsWith(QLatin1String("file:")) && u.contains(QLatin1String("Quillbinder web pages"));
    });
    check(savedCopy, QStringLiteral("Open Saved Page in Browser opens a copy in the Downloads folder"), opened.join(QLatin1String(" | ")));
    check(opened.contains(QStringLiteral("https://example.org/harbor")), QStringLiteral("Open Original Address opens the address"),
          opened.join(QLatin1String(" | ")));
    links::setOpenerForTests(nullptr);
    w->project()->save();
    const QString mht = copy + QStringLiteral("/Files/Data/") + scrivUuidOf(web->id) + QStringLiteral("/content.mht");
    check(QFileInfo::exists(mht), QStringLiteral("in the .scriv: Scrivener's saved web page"), mht);
    ScrivenerImportReport rep;
    QString err;
    auto back = ScrivenerImporter::importProject(copy, ctx().work + QStringLiteral("/back"), QStringLiteral("Back"), StorageFormat::Rtf, &rep, &err);
    BinderItem *again = back ? itemByTitle(back.get(), QStringLiteral("Harbor office")) : nullptr;
    check(again && again->type == ItemType::WebPage && again->sourceUrl == QLatin1String("https://example.org/harbor"),
          QStringLiteral("read back: a web page with its address"), err);
    finishProject(w);
}

// ------------------------------------------------------------------------------------------------------------------
// S-N05: the Scrivener format, everything exchanged since 0.81.2, through the window: keywords, a custom field and its
// value, a collection, a paragraph style, an inline footnote and annotation, bookmarks (to a document and to a web
// address), a document target, the deadline, the writing history, a freely arranged corkboard. Saved, opened again,
// and read afresh from the .scriv (as Scrivener would see it); the verify step compares the .scriv once more.

void caseSN05()
{
    const QString copy = scrivener::scrivFolder(fixtureCopy(QStringLiteral("fx1-scriv")));
    MainWindow *w = openWindow(scrivener::scrivxIn(copy));
    Project *p = w ? w->project() : nullptr;
    if (!p)
        return;
    BinderItem *folder = nullptr;
    for (BinderItem *it : p->draft()->children)
        if (it->isContainer() && it->children.size() >= 2)
            folder = it;
    check(folder != nullptr, QStringLiteral("a folder with documents"));
    if (!folder)
        return;
    BinderItem *a = folder->children.value(0), *b = folder->children.value(1);
    // keywords, a custom field, a collection
    Keyword kw;
    kw.id = QStringLiteral("sn05-keyword");
    kw.name = QStringLiteral("Harbor");
    kw.color = QColor(30, 110, 200);
    p->keywords << kw;
    a->keywords << kw.id;
    CustomField cf;
    cf.id = QStringLiteral("sn05-field");
    cf.title = QStringLiteral("Weather");
    cf.type = CustomFieldType::Text;
    p->customFields << cf;
    a->custom.insert(cf.id, QStringLiteral("Fog at dawn"));
    Collection col;
    col.id = QStringLiteral("sn05-collection");
    col.name = QStringLiteral("Harbor scenes");
    col.items = {a->id, b->id};
    p->collections << col;
    p->model()->customFieldsChanged();
    // bookmarks, a target, the deadline
    a->bookmarks.append({QStringLiteral("Tide tables"), QStringLiteral("https://example.org/tides")});
    a->bookmarks.append({b->title, QStringLiteral("qb://") + b->id});
    p->projectBookmarks.append({QStringLiteral("Harbor office"), QStringLiteral("https://example.org/harbor")});
    a->target = 1200;
    p->targets.deadline = QDate(2027, 6, 30);
    p->markIndexDirty();
    // the text: an inline footnote and annotation typed through the editor's commands
    if (TextEditor *e = showDoc(w, a)) {
        typeInto(e, QStringLiteral(" The bell rang twice."));
        QTextCursor c = e->textCursor();
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral("At low water."), text::noteFormat(NoteKind::InlineFootnote, QStringLiteral("sn05-fn"), QString(), QColor()));
        c.insertText(QStringLiteral(" "), QTextCharFormat());
        c.insertText(QStringLiteral("Check the date."),
                     text::noteFormat(NoteKind::InlineAnnotation, QStringLiteral("sn05-an"), QString(), QColor(200, 30, 30)));
    }
    // the corkboard of the folder: arranged freely, two cards placed
    selectItems(w, {folder});
    settle(400);
    EditorPane *pane = visiblePane(w);
    if (pane) {
        pane->setViewMode(ViewMode::Corkboard);
        settle(200);
        chooseByMenu(w, QStringLiteral("corkFreeform"));
        settle(300);
        shot(w, QStringLiteral("SN05-freeform"));
    }
    a->corkboardPos = QStringLiteral("40,30");
    b->corkboardPos = QStringLiteral("420,260");
    p->markIndexDirty();
    check(folder->corkArrangement == QLatin1String("freeform"), QStringLiteral("the folder keeps its freeform arrangement"),
          folder->corkArrangement);
    const QString aTitle = a->title, bTitle = b->title, folderTitle = folder->title;
    w = reopenWindow(w);
    p = w ? w->project() : nullptr;
    if (!p)
        return;
    BinderItem *a2 = itemByTitle(p, aTitle);
    check(a2 && a2->bookmarks.size() == 2 && a2->target == 1200 && a2->corkboardPos == QLatin1String("40,30"),
          QStringLiteral("after reopening: bookmarks, target and the card's place"));
    // the .scriv read afresh, as Scrivener sees it
    ScrivenerImportReport rep;
    QString err;
    auto back = ScrivenerImporter::importProject(copy, ctx().work + QStringLiteral("/fresh"), QStringLiteral("Fresh"), StorageFormat::Rtf, &rep, &err);
    check(bool(back), QStringLiteral("the .scriv reads afresh"), err);
    if (back) {
        BinderItem *fa = itemByTitle(back.get(), aTitle), *fb = itemByTitle(back.get(), bTitle), *ff = itemByTitle(back.get(), folderTitle);
        QStringList kwNames;
        if (fa)
            for (const QString &k : fa->keywords)
                for (const Keyword &x : back->keywords)
                    if (x.id == k)
                        kwNames << x.name;
        check(kwNames.contains(QStringLiteral("Harbor")), QStringLiteral("afresh: the keyword"), kwNames.join(QLatin1Char(',')));
        QString weather;
        if (fa)
            for (const CustomField &f : back->customFields)
                if (f.title == QLatin1String("Weather"))
                    weather = fa->custom.value(f.id);
        check(weather == QLatin1String("Fog at dawn"), QStringLiteral("afresh: the custom field's value"), weather);
        const bool collection = std::any_of(back->collections.cbegin(), back->collections.cend(), [&](const Collection &c) {
            return c.name == QLatin1String("Harbor scenes") && c.items.size() == 2;
        });
        check(collection, QStringLiteral("afresh: the collection with its two documents"));
        check(fa && fa->bookmarks.size() == 2 && fa->target == 1200, QStringLiteral("afresh: the bookmarks and the target"));
        check(back->targets.deadline == QDate(2027, 6, 30), QStringLiteral("afresh: the deadline"));
        const bool harbor = std::any_of(back->projectBookmarks.cbegin(), back->projectBookmarks.cend(),
                                        [](const Bookmark &x) { return x.target == QLatin1String("https://example.org/harbor"); });
        check(harbor, QStringLiteral("afresh: the project's bookmark to a web address"));
        check(ff && ff->corkArrangement == QLatin1String("freeform") && fb && fb->corkboardPos == QLatin1String("420,260"),
              QStringLiteral("afresh: the arrangement and the places of the cards"), fb ? fb->corkboardPos : QString());
        QTextDocument *d = fa ? back->docs()->text(fa->id) : nullptr;
        int fn = 0, an = 0;
        if (d)
            for (QTextBlock blk = d->begin(); blk.isValid(); blk = blk.next())
                for (auto f = blk.begin(); !f.atEnd(); ++f) {
                    const auto k = NoteKind(f.fragment().charFormat().intProperty(props::NoteKind));
                    fn += k == NoteKind::InlineFootnote;
                    an += k == NoteKind::InlineAnnotation;
                }
        check(fn > 0 && an > 0, QStringLiteral("afresh: the inline footnote and annotation stay inline"), QStringLiteral("%1 %2").arg(fn).arg(an));
    }
    writeModel(p);
    finishProject(w);
}

} // namespace cs
