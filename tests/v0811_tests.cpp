// Scripted run for version 0.81.1 beta, the fixes of the audit of 0.81.0 (2026-11-11, part 1, the four findings at
// function level):
//  - F-01: a document whose file cannot be read (damaged, in another character encoding, not arrived): Roll Back repairs
//    a damaged text (its damaged file is kept first), Merge and Split refuse, Project Replace and style changes leave it
//    and name it, a change a command makes all the same is kept aside and told (before: dropped, and the save counted);
//  - F-02: a picture in notes whose name is not a file is written once, not with every save;
//  - F-03: a save journal that cannot be read is kept with the files of its save (before: they were deleted and the notice
//    said the project was as at the save before); another computer's journal is left alone for 15 minutes, and saves
//    wait meanwhile;
//  - F-04: the RTF reader and writer with the auditor's file (a figure across a table row; the writer froze), in a child
//    process with a time limit; tables and figures beside each other as before.
// Saves screenshots into the folder given as first argument. Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_v0811_tests /tmp/shots
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/Replace.h"
#include "core/SaveBatch.h"
#include "core/Snapshots.h"
#include "core/SyncSafety.h"
#include "text/RtfIO.h"
#include "text/Tables.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/EditorPane.h"
#include "ui/FormattingChange.h"
#include "ui/MainWindow.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"
#include "text/DirectFormat.h"

#include "AppInfo.h"

#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>
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

static void shot(QWidget *w, const QString &name)
{
    if (w)
        w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}

static QByteArray readAll(const QString &f)
{
    QFile file(f);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

static void writeAll(const QString &f, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(f).absolutePath());
    QFile file(f);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(data);
}

static BinderItem *textDoc(Project *p, BinderItem *parent, const QString &title, const QString &text)
{
    BinderItem *it = p->model()->createItem(parent, -1, ItemType::Text, title);
    QTextCursor c(p->docs()->text(it->id));
    c.insertText(text);
    return it;
}

static BinderItem *byTitle(Project *p, const QString &title)
{
    for (BinderItem *x : p->allItems())
        if (x->title == title)
            return x;
    return nullptr;
}

static QAction *byKey(QWidget *w, const QString &key)
{
    QAction *a = w->findChild<QAction *>(key);
    if (!a)
        std::printf("      (action key not found: %s)\n", qPrintable(key));
    return a;
}

static QStringList filesIn(const QString &dir)
{
    QStringList out;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext())
        out << QDir(dir).relativeFilePath(it.next());
    out.sort();
    return out;
}

static QByteArray sha(const QByteArray &d) { return QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex(); }

// a text file in Windows-1252, as another editor saves it (not UTF-8: read-only, never written; 0.70.0)
static QByteArray cp1252(const char *text) { return QByteArray(text); }

// ------------------------------------------------------------------ F-01 in the core

static void unreadableCoreChecks(const QString &W)
{
    QString err;
    auto p = Project::create(W, QStringLiteral("Damaged"), &err);
    BinderItem *ch = textDoc(p.get(), p->draft(), QStringLiteral("Chapter"), QStringLiteral("The good text of the harbor."));
    BinderItem *other = textDoc(p.get(), p->draft(), QStringLiteral("Other"), QStringLiteral("Another harbor."));
    BinderItem *third = textDoc(p.get(), p->draft(), QStringLiteral("Third"), QStringLiteral("The third text."));
    BinderItem *away = textDoc(p.get(), p->draft(), QStringLiteral("Away"), QStringLiteral("A text that will not arrive."));
    p->save(&err);
    snapshots::take(p.get(), ch->id, QStringLiteral("Good"), &err);
    snapshots::take(p.get(), away->id, QStringLiteral("Away good"), &err);
    const QString chId = ch->id, otherId = other->id, thirdId = third->id, awayId = away->id;
    const QString chFile = p->docs()->textPath(chId), thirdFile = p->docs()->textPath(thirdId);
    const QString awayFile = p->docs()->textPath(awayId);
    const QString pf = p->indexPath(), settings = p->settingsDir();
    p.reset();
    const QByteArray damaged = cp1252("The damaged caf\xe9 text of the harbor.");
    const QByteArray damagedThird = cp1252("The third caf\xe9 text.");
    writeAll(chFile, damaged);
    writeAll(thirdFile, damagedThird);
    QFile::remove(awayFile); // a text that has not arrived yet (its counts are in the project file)

    auto o = Project::open(pf, &err);
    check(bool(o), QStringLiteral("(the project with damaged texts opens)"), err);
    if (!o)
        return;
    DocumentStore *docs = o->docs();
    docs->text(chId);
    docs->text(thirdId);
    docs->text(awayId);
    check(docs->isDamaged(chId) && !docs->whyNotEditable(chId).isEmpty() && docs->isMissing(awayId)
              && docs->whyNotEditable(awayId).contains(QLatin1String("not arrived")) && docs->whyNotEditable(otherId).isEmpty(),
          QStringLiteral("F-01: the store names why a text must not be changed (damaged, not arrived) and none for a good one"),
          docs->whyNotEditable(chId) + QStringLiteral(" | ") + docs->whyNotEditable(awayId));

    // Project Replace: the damaged text is left as it is and named; the good one is changed
    {
        ReplaceOptions ro;
        ro.find = QStringLiteral("harbor");
        ro.with = QStringLiteral("port");
        const ReplaceResult r = replace::run(o.get(), ro, false);
        const bool saved = o->save(&err);
        check(saved && r.texts == 1 && r.skipped.contains(QStringLiteral("Chapter")) && readAll(chFile) == damaged
                  && readAll(docs->textPath(otherId)).contains("Another port."),
              QStringLiteral("F-01: Project Replace leaves a damaged text as it is, names it, and changes the others "
                             "(before: changed in memory, counted, never saved)"),
              QStringLiteral("texts %1, skipped %2, saved %3 %4").arg(r.texts).arg(r.skipped.join(QLatin1String(", "))).arg(saved).arg(err));
    }
    // a style change through the texts: the damaged one is left out and named, without a snapshot taken for it
    {
        const int before = int(snapshots::list(o.get(), thirdId).size());
        (void)docs->takeSkippedUnreadable();
        QString why;
        const int n = formatchange::takeStyle(o.get(), QStringLiteral("Heading 1"), false, direct::Family, 0, &why);
        const QStringList left = docs->takeSkippedUnreadable();
        check(n >= 0 && left.contains(QStringLiteral("Third")) && readAll(thirdFile) == damagedThird
                  && snapshots::list(o.get(), thirdId).size() == before,
              QStringLiteral("F-01: a style change leaves a damaged text out and names it"), left.join(QLatin1String(", ")) + QLatin1Char(' ') + why);
    }
    // Roll Back of a text that has not arrived: refused with the reason, nothing written
    {
        QString why, kept;
        const QList<Snapshot> snaps = snapshots::list(o.get(), awayId);
        const bool rolled = !snaps.isEmpty() && snapshots::rollBack(o.get(), snaps.first(), &why, false, &kept);
        o->save(&err);
        check(!snaps.isEmpty() && !rolled && why.contains(QLatin1String("not arrived")) && kept.isEmpty()
                  && !QFile::exists(docs->textPath(awayId)) && docs->isUnreadable(awayId),
              QStringLiteral("F-01: Roll Back of a text that has not arrived yet is refused with the reason"), why);
    }
    // a command that does not ask changes a damaged text in memory: the save keeps it aside and tells, the file stays
    {
        QStringList told;
        QObject::connect(docs, &DocumentStore::changeKeptAside, [&told](const QString &id, bool, const QString &copy) {
            told << id + QLatin1Char('=') + copy;
        });
        QTextCursor c(docs->text(thirdId));
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral(" A sentence added by a command."));
        const bool saved = o->save(&err);
        const QString copy = told.value(0).section(QLatin1Char('='), 1);
        const QByteArray kept = readAll(copy);
        const bool second = o->save(&err);
        check(saved && second && told.size() == 1 && told.value(0).startsWith(thirdId) && kept.startsWith("{\\rtf")
                  && kept.contains("added by a command") && copy.startsWith(settings + QStringLiteral("/conflicts/"))
                  && readAll(thirdFile) == damagedThird && !o->hasUnsavedChanges(),
              QStringLiteral("F-01: a change made to a damaged text all the same is kept as an RTF copy and told once; the "
                             "damaged file stays as it is (before: dropped without a word)"),
              told.join(QLatin1String(" | ")));
    }
    // Roll Back of a damaged text: the damaged file is kept, the text becomes editable and the snapshot's text is saved
    {
        QString why, kept;
        QSignalSpy editable(docs, &DocumentStore::editableAgain);
        const QList<Snapshot> snaps = snapshots::list(o.get(), chId);
        const bool rolled = !snaps.isEmpty() && snapshots::rollBack(o.get(), snaps.first(), &why, false, &kept);
        const bool saved = o->save(&err);
        check(rolled && saved && readAll(kept) == damaged && kept.startsWith(settings + QStringLiteral("/conflicts/"))
                  && readAll(chFile).contains("The good text of the harbor.") && !docs->isUnreadable(chId) && editable.count() == 1,
              QStringLiteral("F-01: Roll Back repairs a damaged text: its damaged file is kept as a copy, and the snapshot's "
                             "text is saved in its place (before: in memory only, back after reopening)"),
              why + QStringLiteral(" | kept ") + kept + QStringLiteral(" | file ") + QString::fromLatin1(readAll(chFile).left(80)));
    }
    o.reset();
    auto again = Project::open(pf, &err);
    check(again && again->docs()->text(chId)->toPlainText() == QLatin1String("The good text of the harbor.")
              && !again->docs()->isDamaged(chId),
          QStringLiteral("F-01: after reopening, the rolled-back text is there and can be edited"),
          again ? again->docs()->text(chId)->toPlainText() : err);
}

// ------------------------------------------------------------------ F-01 in the window

static void unreadableWindowChecks(const QString &W)
{
    QString err;
    auto p = Project::create(W, QStringLiteral("DamagedWindow"), &err);
    BinderItem *ch = textDoc(p.get(), p->draft(), QStringLiteral("Broken"), QStringLiteral("The good words. And more of them."));
    textDoc(p.get(), p->draft(), QStringLiteral("Fine"), QStringLiteral("Fine words."));
    p->save(&err);
    snapshots::take(p.get(), ch->id, QStringLiteral("Good words"), &err);
    const QString chId = ch->id, chFile = p->docs()->textPath(chId), pf = p->indexPath();
    p.reset();
    const QByteArray damaged = cp1252("The broken caf\xe9 words. And more of them.");
    writeAll(chFile, damaged);
    auto o = Project::open(pf, &err);
    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1300, 900);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(o)), QStringLiteral("(project open in the window)"));
    settle(600);
    Project *pr = w.project();
    auto *binder = w.findChild<BinderPanel *>();
    BinderItem *broken = byTitle(pr, QStringLiteral("Broken")), *fine = byTitle(pr, QStringLiteral("Fine"));
    if (!binder || !broken || !fine) {
        check(false, QStringLiteral("(binder and documents found)"));
        return;
    }
    const int items = int(pr->allItems().size());
    // Merge with a damaged document: refused, nothing moved to the Trash
    binder->select({broken, fine}, true);
    settle(400);
    if (QAction *m = byKey(&w, QStringLiteral("merge")))
        m->trigger();
    settle(400);
    const QString mergeMsg = w.statusBar()->currentMessage();
    check(mergeMsg.contains(QLatin1String("not merged")) && mergeMsg.contains(QLatin1String("Broken")) && !pr->isInTrash(fine)
              && !pr->isInTrash(broken) && readAll(chFile) == damaged,
          QStringLiteral("F-01: Merge with a document whose file is damaged is refused with the reason; nothing goes to the "
                         "Trash (before: the merged text existed in memory only)"),
          mergeMsg);
    // Split of a damaged text: refused
    binder->select({broken}, true);
    settle(400);
    auto *editorSplit = qobject_cast<QSplitter *>(w.findChild<EditorPane *>()->parentWidget());
    EditorPane *pane = editorSplit ? qobject_cast<EditorPane *>(editorSplit->widget(0)) : w.findChild<EditorPane *>();
    auto shown = [&]() -> TextEditor * {
        // the editor that shows the damaged text (the pane also holds a hidden one, the copyholder)
        for (TextEditor *e : pane ? pane->findChildren<TextEditor *>() : QList<TextEditor *>())
            if (e->item() && e->item()->id == broken->id && !e->isNotes() && e->isVisible())
                return e;
        return pane ? pane->textView()->editors().value(0) : nullptr;
    };
    TextEditor *ed = shown();
    const bool readOnly = ed && ed->isReadOnly();
    if (ed) {
        QTextCursor c = ed->textCursor();
        c.setPosition(10);
        ed->setTextCursor(c);
    }
    w.statusBar()->clearMessage();
    if (QAction *s = byKey(&w, QStringLiteral("split")))
        s->trigger();
    settle(400);
    const QString splitMsg = w.statusBar()->currentMessage();
    check(readOnly && splitMsg.contains(QLatin1String("cannot be split")) && int(pr->allItems().size()) == items,
          QStringLiteral("F-01: Split of a damaged text is refused with the reason"),
          QStringLiteral("read-only %1, items %2 -> %3: %4").arg(readOnly).arg(items).arg(pr->allItems().size()).arg(splitMsg));
    shot(&w, QStringLiteral("v0811-damaged-read-only"));
    // Roll Back in the inspector: the damaged file is kept, the editor can be edited again, the text is saved
    auto *list = w.findChild<QListWidget *>(QStringLiteral("snapshotList"));
    auto *roll = w.findChild<QToolButton *>(QStringLiteral("snapshotRollBack"));
    if (list && roll && list->count() > 0) {
        list->setCurrentRow(0);
        QTimer::singleShot(300, qApp, [] {
            if (auto *b = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                b->button(QMessageBox::No)->click();
        });
        roll->click();
        settle(600);
        QWidget *insp = roll->parentWidget();
        while (insp && !insp->property("qbLastRollBackNote").isValid() && insp->parentWidget())
            insp = insp->parentWidget();
        const QString note = insp ? insp->property("qbLastRollBackNote").toString() : QString();
        TextEditor *ed2 = shown();
        // the restored text is laid out and painted at once (found while checking: an empty page until something
        // else laid it out again)
        int lines = -1;
        if (ed2) {
            ed2->grab();
            lines = ed2->document()->begin().layout() ? ed2->document()->begin().layout()->lineCount() : -1;
        }
        const bool saved = pr->save(&err);
        check(note.contains(QLatin1String("kept as a copy")) && ed2 && !ed2->isReadOnly() && saved && lines > 0
                  && readAll(chFile).contains("The good words."),
              QStringLiteral("F-01: Roll Back in the inspector repairs a damaged text: told where the damaged file was kept, "
                             "the editor can be edited again, the snapshot's text is saved"),
              QStringLiteral("editor %1, read-only %2, lines %3, saved %4 %5, file %6 | %7")
                  .arg(ed2 != nullptr).arg(ed2 && ed2->isReadOnly()).arg(lines).arg(saved).arg(err, QString::fromUtf8(readAll(chFile).left(60)), note));
        shot(&w, QStringLiteral("v0811-rolled-back"));
        // the same for a document that was never damaged (the empty page was there before 0.81.1 as well)
        BinderItem *f2 = byTitle(pr, QStringLiteral("Fine"));
        snapshots::take(pr, f2->id, QStringLiteral("Fine words"), &err);
        binder->select({f2}, true);
        settle(400);
        QTextCursor fc(pr->docs()->text(f2->id));
        fc.movePosition(QTextCursor::End);
        fc.insertText(QStringLiteral(" Changed."));
        settle(200);
        QString why2;
        const bool rolled = snapshots::rollBack(pr, snapshots::list(pr, f2->id).first(), &why2);
        settle(400);
        TextEditor *fe = nullptr;
        for (TextEditor *e : pane ? pane->findChildren<TextEditor *>() : QList<TextEditor *>())
            if (e->item() && e->item()->id == f2->id && !e->isNotes() && e->isVisible())
                fe = e;
        int fineLines = -1;
        if (fe) {
            fe->grab();
            fineLines = fe->document()->begin().layout() ? fe->document()->begin().layout()->lineCount() : -1;
        }
        check(rolled && fe && fineLines > 0 && fe->toPlainText() == QLatin1String("Fine words."),
              QStringLiteral("Roll Back shows the restored text at once (before: an empty page until it was laid out again)"),
              QStringLiteral("lines %1: %2").arg(fineLines).arg(fe ? fe->toPlainText() : why2));
    } else {
        check(false, QStringLiteral("(the snapshot list and Roll Back are found)"));
    }
    // the notice for a change kept aside
    {
        BinderItem *fineItem = byTitle(pr, QStringLiteral("Fine"));
        const QString fineFile = pr->docs()->textPath(fineItem->id);
        // damaged on disk and read again: a new document of the same project
        BinderItem *late = textDoc(pr, pr->draft(), QStringLiteral("Late"), QStringLiteral("Late words."));
        pr->save(&err);
        const QString lateFile = pr->docs()->textPath(late->id);
        pr->docs()->forgetDocument(late->id);
        writeAll(lateFile, cp1252("Late caf\xe9 words."));
        QTextDocument *ld = pr->docs()->text(late->id);
        settle(300);
        QTextCursor c(ld);
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral(" More."));
        pr->save(&err);
        settle(500);
        const QString msg = w.lastChangeKeptAside();
        check(msg.contains(QLatin1String("Late")) && msg.contains(QLatin1String("kept as a copy")) && readAll(lateFile) == cp1252("Late caf\xe9 words.")
                  && w.findChild<QMessageBox *>(QStringLiteral("changeKeptAsideNotice")) && QFile::exists(fineFile),
              QStringLiteral("F-01: the window tells about a change kept aside, with the copy's place"), msg);
        shot(&w, QStringLiteral("v0811-change-kept-aside"));
    }
    w.close();
    delete wp;
    settle(200);
}

// ------------------------------------------------------------------ F-02

static int picturesIn(const QString &dir) { return int(QDir(dir + QStringLiteral("/images")).entryList(QDir::Files).size()); }

static void addPastedPicture(QTextDocument *doc, const QString &name)
{
    QImage img(24, 16, QImage::Format_RGB32);
    img.fill(QColor(200, 80, 40));
    doc->addResource(QTextDocument::ImageResource, QUrl(name), img);
    QTextCursor c(doc);
    c.movePosition(QTextCursor::End);
    QTextImageFormat f;
    f.setName(name);
    c.insertImage(f);
}

static void pictureChecks(const QString &W)
{
    QString err;
    auto p = Project::create(W, QStringLiteral("NotesPictures"), &err);
    BinderItem *d = textDoc(p.get(), p->draft(), QStringLiteral("Scene"), QStringLiteral("Its text."));
    BinderItem *e = textDoc(p.get(), p->draft(), QStringLiteral("Second"), QStringLiteral("Its text too."));
    p->save(&err);
    const QString dId = d->id, eId = e->id, pf = p->indexPath();
    // the project notes
    QTextDocument *pn = p->docs()->projectNotes();
    addPastedPicture(pn, QStringLiteral("rtfimage://1"));
    for (int i = 0; i < 5; ++i) {
        QTextCursor c(pn);
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral(" more %1").arg(i));
        p->save(&err);
    }
    const int project = picturesIn(p->contentDir(QStringLiteral("PROJECT")));
    // the notes of a document whose text is loaded
    QTextDocument *dn = p->docs()->notes(dId);
    addPastedPicture(dn, QStringLiteral("rtfimage://2"));
    for (int i = 0; i < 3; ++i) {
        QTextCursor c(dn);
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral(" note %1").arg(i));
        p->save(&err);
    }
    const int loaded = picturesIn(p->contentDir(dId));
    p.reset();
    // the notes of a document whose text is not loaded
    auto o = Project::open(pf, &err);
    QTextDocument *en = o->docs()->notes(eId);
    addPastedPicture(en, QStringLiteral("rtfimage://3"));
    for (int i = 0; i < 3; ++i) {
        QTextCursor c(en);
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral(" note %1").arg(i));
        o->save(&err);
    }
    const bool textNotLoaded = !o->docs()->loadedText(eId);
    const int notLoaded = picturesIn(o->contentDir(eId));
    const QString notes = QString::fromUtf8(readAll(o->docs()->notesPath(eId)));
    check(project == 1 && loaded == 1 && notLoaded == 1 && textNotLoaded && notes.contains(QLatin1String("images/")),
          QStringLiteral("F-02: a pasted picture in notes is written once, however often the notes are saved (project notes, "
                         "notes beside a loaded text, notes alone; before: one more file with every save)"),
          QStringLiteral("project notes %1, loaded %2, alone %3").arg(project).arg(loaded).arg(notLoaded));
}

// ------------------------------------------------------------------ F-03

static QJsonObject journalEntry(const QString &folder, const QString &target, const QByteArray &now, const QByteArray &before,
                                const QString &title)
{
    QJsonObject o;
    const QString rel = QDir(folder).relativeFilePath(target);
    o.insert(QStringLiteral("file"), rel);
    o.insert(QStringLiteral("staged"), rel + QLatin1String(savebatch::kStageSuffix));
    o.insert(QStringLiteral("sha256"), QString::fromLatin1(sha(now)));
    o.insert(QStringLiteral("replaces"), QString::fromLatin1(sha(before)));
    o.insert(QStringLiteral("size"), double(now.size()));
    o.insert(QStringLiteral("title"), title);
    return o;
}

static void journalChecks(const QString &W)
{
    QString err;
    // an unreadable journal: A already in place, B still staged
    {
        auto p = Project::create(W, QStringLiteral("Journal"), &err);
        BinderItem *a = textDoc(p.get(), p->draft(), QStringLiteral("Alpha"), QStringLiteral("Alpha old."));
        BinderItem *b = textDoc(p.get(), p->draft(), QStringLiteral("Beta"), QStringLiteral("Beta old."));
        p->save(&err);
        const QString folder = p->folderPath(), aFile = p->docs()->textPath(a->id), bFile = p->docs()->textPath(b->id);
        const QString journal = p->journalFile(), pf = p->indexPath(), settings = p->settingsDir();
        const QByteArray bOld = readAll(bFile);
        p.reset();
        writeAll(aFile, "Alpha new.\n");
        writeAll(bFile + QLatin1String(savebatch::kStageSuffix), "Beta new.\n");
        writeAll(journal, "{\"what\": \"A save of Quillbinder in progress\", \"version\": 1, \"files\": [{\"file\": \"");
        auto o = Project::open(pf, &err);
        const QString notice = o ? o->saveRecoveryText() : err;
        QString keptDir;
        for (const QString &f : filesIn(settings + QStringLiteral("/conflicts")))
            if (f.endsWith(QLatin1String("/journal.json")))
                keptDir = settings + QStringLiteral("/conflicts/") + f.section(QLatin1Char('/'), 0, 0);
        const QString keptB = keptDir + QLatin1Char('/') + QDir(folder).relativeFilePath(bFile);
        check(o && readAll(bFile) == bOld && !QFile::exists(bFile + QLatin1String(savebatch::kStageSuffix)) && readAll(keptB) == "Beta new.\n"
                  && !QFile::exists(journal) && !keptDir.isEmpty() && notice.contains(QLatin1String("cannot be read"))
                  && !notice.contains(QLatin1String("as it was at the save before")),
              QStringLiteral("F-03: a save journal that cannot be read is kept with the file its save had staged, and the notice "
                             "says what was found (before: both deleted, \"the project is as it was at the save before\")"),
              notice + QStringLiteral(" | kept: ") + filesIn(keptDir).join(QLatin1String(", ")));
    }
    // another computer's journal, younger than 15 minutes: left alone, saves wait; older: finished
    {
        auto p = Project::create(W, QStringLiteral("Synced"), &err);
        BinderItem *a = textDoc(p.get(), p->draft(), QStringLiteral("Gamma"), QStringLiteral("Gamma old."));
        BinderItem *b = textDoc(p.get(), p->draft(), QStringLiteral("Delta"), QStringLiteral("Delta old."));
        p->save(&err);
        const QString folder = p->folderPath(), aFile = p->docs()->textPath(a->id), bFile = p->docs()->textPath(b->id);
        const QString journal = p->journalFile(), pf = p->indexPath(), aId = a->id;
        const QByteArray aOld = readAll(aFile), bOld = readAll(bFile);
        p.reset();
        qputenv("QB_DEVICE_NAME", "Laptop");
        const QString laptop = syncsafe::machineKey();
        qunsetenv("QB_DEVICE_NAME");
        const QByteArray bNew = "Delta from the laptop.\n";
        writeAll(bFile + QLatin1String(savebatch::kStageSuffix), bNew);
        auto writeJournal = [&](const QDateTime &when) {
            QJsonObject root;
            root.insert(QStringLiteral("version"), 1);
            root.insert(QStringLiteral("device"), QStringLiteral("Laptop"));
            root.insert(QStringLiteral("key"), laptop);
            root.insert(QStringLiteral("time"), when.toUTC().toString(Qt::ISODate));
            root.insert(QStringLiteral("files"), QJsonArray{journalEntry(folder, bFile, bNew, bOld, QStringLiteral("Delta"))});
            writeAll(journal, QJsonDocument(root).toJson());
        };
        writeJournal(QDateTime::currentDateTimeUtc().addSecs(-60));
        auto o = Project::open(pf, &err);
        const QString notice = o ? o->saveRecoveryText() : err;
        const bool untouched = QFile::exists(journal) && readAll(bFile + QLatin1String(savebatch::kStageSuffix)) == bNew && readAll(bFile) == bOld;
        QString why;
        bool saved = true;
        if (o) {
            QTextCursor c(o->docs()->text(aId));
            c.movePosition(QTextCursor::End);
            c.insertText(QStringLiteral(" Changed here."));
            saved = o->save(&why);
        }
        check(o && untouched && notice.contains(QLatin1String("Laptop")) && !saved && why.contains(QLatin1String("Laptop"))
                  && readAll(aFile) == aOld && QFile::exists(journal) && o->hasUnsavedChanges(),
              QStringLiteral("F-03: the save journal of another computer from a minute ago is left alone with its file, and "
                             "this computer's save waits, with the reason (before: acted on at once)"),
              notice.left(300) + QStringLiteral(" | save: ") + why);
        writeJournal(QDateTime::currentDateTimeUtc().addSecs(-20 * 60));
        const bool later = o && o->save(&why);
        check(later && readAll(bFile) == bNew && !QFile::exists(journal) && readAll(aFile).contains("Changed here.")
                  && !QFile::exists(bFile + QLatin1String(savebatch::kStageSuffix)),
              QStringLiteral("F-03: a journal of another computer 20 minutes old is finished like one of this computer, and "
                             "the save goes on"),
              why);
    }
}

// ------------------------------------------------------------------ F-04

static int tablesIn(QTextFrame *f, int *figures)
{
    int n = 0;
    for (QTextFrame *c : f->childFrames()) {
        if (qobject_cast<QTextTable *>(c))
            ++n;
        else if (figures && figures::isFigure(c))
            ++*figures;
        n += tablesIn(c, figures);
    }
    return n;
}

static int cellWalksNotEnding(QTextFrame *f)
{
    int bad = 0;
    for (QTextFrame *c : f->childFrames()) {
        if (auto *t = qobject_cast<QTextTable *>(c))
            for (int r = 0; r < t->rows(); ++r)
                for (int col = 0; col < t->columns(); ++col) {
                    const QTextTableCell cell = t->cellAt(r, col);
                    if (cell.row() != r || cell.column() != col)
                        continue;
                    int steps = 0;
                    for (QTextFrame::iterator it = cell.begin(); !it.atEnd() && steps < 100000; ++it)
                        ++steps;
                    if (steps >= 100000 || cell.lastPosition() + 1 < cell.firstPosition())
                        ++bad;
                }
        bad += cellWalksNotEnding(c);
    }
    return bad;
}

// child: the file read (both ways the program reads RTF) and written; prints what it found
static int childRtf(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    const QByteArray in = readAll(QString::fromLocal8Bit(argv[2]));
    const Formatting f;
    int bad = 0, written = 0, back = 0;
    for (bool keep : {false, true}) {
        QTextDocument doc;
        rtf::ReadOptions o;
        o.keepFormatting = keep;
        rtf::read(in, &doc, f, o);
        bad += cellWalksNotEnding(doc.rootFrame());
        const QByteArray out = rtf::write(&doc, f);
        written += int(out.size() > 0);
        QTextDocument again;
        rtf::read(out, &again, f, o);
        back += int(rtf::write(&again, f).size() > 0);
    }
    std::printf("walks=%d written=%d back=%d\n", bad, written, back);
    std::fflush(stdout);
    return 0;
}

static void rtfChecks()
{
    const QString input = QStringLiteral(QB_TEST_SOURCE_DIR "/fuzz/inputs/rtf-figure-in-table-row.rtf");
    check(QFile::exists(input), QStringLiteral("(the auditor's input is in tests/fuzz/inputs)"), input);
    QProcess pr;
    pr.setProcessChannelMode(QProcess::MergedChannels);
    QElapsedTimer t;
    t.start();
    pr.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child-rtf"), input});
    const bool ended = pr.waitForFinished(20000);
    const qint64 ms = t.elapsed();
    if (!ended) {
        pr.kill();
        pr.waitForFinished(2000);
    }
    const QString out = QString::fromUtf8(pr.readAll()).trimmed();
    check(ended && pr.exitCode() == 0 && out.contains(QLatin1String("walks=0 written=2 back=2")),
          QStringLiteral("F-04: the auditor's RTF file (a figure across a table row) is read into a sound document and written "
                         "within the time limit, both ways the program reads RTF (before: the writer did not return)"),
          QStringLiteral("%1 ms: %2").arg(ms).arg(out.right(500)));

    // a table, a figure and a table beside each other: written and read back as before
    const Formatting f;
    QTextDocument doc;
    QTextCursor c(&doc);
    c.insertText(QStringLiteral("Before."));
    QTextTable *t1 = c.insertTable(1, 2);
    t1->cellAt(0, 0).firstCursorPosition().insertText(QStringLiteral("a"));
    t1->cellAt(0, 1).firstCursorPosition().insertText(QStringLiteral("b"));
    c.movePosition(QTextCursor::End);
    c.insertText(QStringLiteral("Between."));
    c.insertFrame(figures::frameFormatFor(figures::Centered, 120));
    c.insertText(QStringLiteral("The caption."));
    c.movePosition(QTextCursor::End);
    c.insertText(QStringLiteral("After the figure."));
    QTextTable *t2 = c.insertTable(2, 1);
    t2->cellAt(1, 0).firstCursorPosition().insertText(QStringLiteral("c"));
    c.movePosition(QTextCursor::End);
    c.insertText(QStringLiteral("The end."));
    QTextDocument back;
    rtf::read(rtf::write(&doc, f), &back, f);
    int figs = 0;
    const int tables = tablesIn(back.rootFrame(), &figs);
    check(tables == 2 && figs == 1 && back.toPlainText().contains(QLatin1String("The caption.")) && cellWalksNotEnding(back.rootFrame()) == 0,
          QStringLiteral("F-04: tables and a figure beside each other are written and read back as before"),
          QStringLiteral("tables %1, figures %2: %3").arg(tables).arg(figs).arg(back.toPlainText().left(200)));
}

int main(int argc, char **argv)
{
    if (argc > 2 && qstrcmp(argv[1], "--child-rtf") == 0)
        return childRtf(argc, argv);
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    qputenv("XDG_RUNTIME_DIR", (home.path() + QLatin1String("/run")).toUtf8());
    QDir().mkpath(home.path() + QLatin1String("/run"));
    QFile::setPermissions(home.path() + QLatin1String("/run"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("QB_NO_DIALOGS", "1");
    QCoreApplication::setOrganizationName(QStringLiteral("Quillbinder"));
    QCoreApplication::setApplicationName(QStringLiteral("Quillbinder"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.81.1")) >= 0,
          QStringLiteral("version 0.81.1 or newer"), QStringLiteral(QB_APP_VERSION_SHOWN));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    AppSettings::instance().load();
    AppSettings::instance().backupOnOpen = false;
    AppSettings::instance().backupOnClose = false;
    AppSettings::instance().save();
    QTemporaryDir work;
    const QString W = work.path();
    // a window left open by mistake would stop the run: closed after 10 s
    QTimer watcher;
    QElapsedTimer modalSince;
    QObject::connect(&watcher, &QTimer::timeout, [&modalSince] {
        QWidget *m = QApplication::activeModalWidget();
        if (!m) {
            modalSince.invalidate();
            return;
        }
        if (!modalSince.isValid())
            modalSince.start();
        if (modalSince.elapsed() > 10000) {
            std::printf("      (a window stayed open and was closed: %s)\n", qPrintable(m->objectName()));
            m->close();
            modalSince.invalidate();
        }
    });
    watcher.start(1000);
    unreadableCoreChecks(W);
    unreadableWindowChecks(W);
    pictureChecks(W);
    journalChecks(W);
    rtfChecks();
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
