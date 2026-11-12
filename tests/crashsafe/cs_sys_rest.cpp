// The system test, stage 2: script writing, tools, add-ons, help (S-A01 to S-A05), data safety (S-D02 to
// S-D05), endurance (S-E01 to S-E04), section 6.2. S-A06 and S-R02 to S-R04 are in cs_sync.cpp, S-D01 in
// stage 1 (cs_cases.cpp).
#include "cs.h"

#include "compile/CompileFormat.h"
#include "compile/Compiler.h"
#include "compile/ScriptExporters.h"
#include "core/Addons.h"
#include "core/Backup.h"
#include "core/CrashGuard.h"
#include "core/Snapshots.h"
#include "core/Zip.h"
#include "import/ScrivenerFormat.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/TextEditor.h"

#include <QAbstractButton>
#include <QClipboard>
#include <QDialog>
#include <QPointer>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QTreeWidget>

#include <random>
#include <unistd.h>

namespace cs {

using namespace qb;
using namespace qb::compile;

namespace {

void endOf(TextEditor *e)
{
    QTextCursor c = e->textCursor();
    c.movePosition(QTextCursor::End);
    e->setTextCursor(c);
    e->setFocus(Qt::OtherFocusReason);
}

QStringList leafActions(QMenu *m)
{
    QStringList out;
    if (!m)
        return out;
    emit m->aboutToShow();
    for (QAction *a : m->actions()) {
        if (a->isSeparator())
            continue;
        if (a->menu())
            out += leafActions(a->menu());
        else
            out << a->text().remove(QLatin1Char('&'));
    }
    return out;
}

QMenu *topMenu(MainWindow *w, const QString &title)
{
    for (QAction *a : w->menuBar()->actions())
        if (a->text().remove(QLatin1Char('&')) == title)
            return a->menu();
    return nullptr;
}

QStringList zipsIn(const QString &dir)
{
    QStringList out;
    QDirIterator it(dir, {QStringLiteral("*.zip")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        out << it.next();
    out.sort();
    return out;
}

} // namespace

// ================================================================ S-A01 script mode
void caseSA01()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    BinderItem *it = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Script scene"));
    TextEditor *e = showDoc(w, it);
    if (!e)
        return;
    check(chooseByMenu(w, QStringLiteral("scriptMode")), QStringLiteral("Format > Scriptwriting > Script Mode"));
    for (int s = 0; s < 6; ++s) {
        typeInto(e, QStringLiteral("INT. KITCHEN %1 - NIGHT\n").arg(s + 1));
        typeInto(e, QStringLiteral("Rain on the window.\n"));
        QTest::keyClick(e, Qt::Key_Tab);
        typeInto(e, QStringLiteral("ANNA\n"));
        typeInto(e, QStringLiteral("Where were you?\n"));
        QTest::keyClick(e, Qt::Key_Tab);
        typeInto(e, QStringLiteral("BEN\nOut.\n"));
    }
    chooseByMenu(w, QStringLiteral("scriptElements"));
    settle(300);
    withModal<QDialog>(300, [](QDialog *d) {
        shot(d, QStringLiteral("script-settings"));
        d->accept();
    });
    chooseByMenu(w, QStringLiteral("scriptSettings"));
    settle(400);
    const QString runs = runsOf(e->document());
    const QString text = rawText(e->document());
    // compile to Fountain and Final Draft
    for (BinderItem *x : p->allItems())
        x->includeInCompile = x == it;
    Format f = formatByName(QStringLiteral("Paperback (6 x 9 in)"));
    AuthorInfo a;
    Compiler cc(p, f, p->compile, a);
    const Manuscript m = cc.build();
    QString err;
    FountainExporter fe;
    FdxExporter fd;
    const QString fountain = ctx().dir + QStringLiteral("/scene.fountain"), fdx = ctx().dir + QStringLiteral("/scene.fdx");
    fe.exportManuscript(m, fountain, &err);
    fd.exportManuscript(m, fdx, &err);
    check(readText(fountain).contains(QLatin1String("INT. KITCHEN 3")) && readText(fdx).contains(QLatin1String("INT. KITCHEN 3")), QStringLiteral("Fountain and Final Draft hold the scene"), err);
    for (BinderItem *x : p->allItems())
        x->includeInCompile = true;
    chooseByMenu(w, QStringLiteral("scriptMode"));
    check(rawText(e->document()) == text, QStringLiteral("script mode off with the text in place"));
    const QString title = it->title;
    w = reopenWindow(w);
    p = w->project();
    BinderItem *again = p ? itemByTitle(p, title) : nullptr;
    check(again && runsOf(p->docs()->text(again->id)) == runs, QStringLiteral("the script elements are kept after reopening"));
    finishProject(w);
    delete w;
}

// ================================================================ S-A02 writing tools
void caseSA02()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    QMenu *tools = nullptr;
    if (QMenu *edit = topMenu(w, QStringLiteral("Edit"))) {
        emit edit->aboutToShow();
        for (QAction *a : edit->actions())
            if (a->menu() && a->text().remove(QLatin1Char('&')).contains(QLatin1String("Writing Tools")))
                tools = a->menu();
    }
    const QStringList entries = leafActions(tools);
    fact(QStringLiteral("S-A02 Edit > Writing Tools"), entries.join(QLatin1String(", ")));
    if (entries.size() <= 2)
        note(QStringLiteral("[A] no writing tools installed"));
    for (const QString &en : entries) {
        if (en.contains(QLatin1String("No writing tools")) || en.contains(QLatin1String("Add-on Manager")))
            continue;
        menuPath(w, QStringLiteral("Edit > Writing Tools > ") + en);
        settle(600);
        shot(w, QStringLiteral("tool-") + QString(en).replace(QLatin1Char(' '), QLatin1Char('-')).left(30));
    }
    checkCleanWindows(w);
    w->closeProject();
    settle(400);
    checkCleanWindows(w);
    delete w;
}

// ================================================================ S-A03 installing and managing add-ons
void caseSA03()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    const QString dist = ctx().buildDir + QStringLiteral("/addons-dist");
    const QStringList pkgs = QDir(dist).entryList({QStringLiteral("*.qbpkg")}, QDir::Files);
    if (pkgs.isEmpty())
        note(QStringLiteral("[A] no add-on packages built"));
    QStringList messages;
    auto install = [&](const QString &file) {
        QString text;
        withModal<QMessageBox>(500, [&text](QMessageBox *b) {
            text = b->text();
            if (QAbstractButton *later = b->button(QMessageBox::No))
                later->click();
            else
                b->accept();
        });
        clickLater(500, QStringLiteral("Install"));
        clickLater(1500, QStringLiteral("Later"));
        w->installPackage(file);
        settle(2000);
        closeDialogsLater(50);
        settle(100);
        messages << QFileInfo(file).fileName() + QStringLiteral(": ") + text.left(100);
    };
    for (const QString &pk : pkgs)
        if (!pk.startsWith(QLatin1String("speech")))
            install(dist + QLatin1Char('/') + pk);
    const QString broken = ctx().work + QStringLiteral("/broken.qbpkg");
    writeText(broken, QByteArray("PK\x03\x04 broken"));
    install(broken);
    fact(QStringLiteral("S-A03 the answers of the installs"), messages.join(QLatin1String("; ")));
    check(messages.last().contains(QLatin1String(":")) && messages.last().size() > 15, QStringLiteral("a damaged package gives a message"), messages.last());
    // the Add-on Manager: disable, enable, remove while in use
    withModal<QDialog>(500, [](QDialog *d) {
        shot(d, QStringLiteral("addon-manager"));
        for (QTreeWidget *t : d->findChildren<QTreeWidget *>())
            fact(QStringLiteral("S-A03 add-ons listed"), t->topLevelItemCount());
        d->reject();
    });
    chooseByMenu(w, QStringLiteral("addonManager"));
    settle(800);
    for (const addons::Installed &a : addons::installedAddons()) {
        addons::setEnabled(a.manifest.id, false);
        addons::setEnabled(a.manifest.id, true);
    }
    checkCleanWindows(w);
    finishProject(w);
    delete w;
}

// ================================================================ S-A04 bibliography and citations
void caseSA04()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    TextEditor *e = showDoc(w, textItems(p).value(2));
    AppSettings &as = AppSettings::instance();
    const QString script = ctx().work + QStringLiteral("/cite.sh");
    writeText(script, "#!/bin/sh\nprintf '(Smith 2020)'\n");
    QFile::setPermissions(script, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QString failing = ctx().work + QStringLiteral("/fail.sh");
    writeText(failing, "#!/bin/sh\necho broken >&2\nexit 3\n");
    QFile::setPermissions(failing, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    struct Try {
        QString what, app;
    };
    for (const Try &t : {Try{QStringLiteral("no program set"), QString()}, Try{QStringLiteral("the test script"), script},
                         Try{QStringLiteral("a missing program"), ctx().work + QStringLiteral("/missing.sh")}, Try{QStringLiteral("a program that fails"), failing}}) {
        as.citationsApp = t.app;
        as.save();
        if (e)
            endOf(e);
        const QString before = e ? e->toPlainText() : QString();
        BoxText msg;
        answerBoxLater(1500, QMessageBox::Ok, &msg);
        chooseByMenu(w, QStringLiteral("citations"));
        settle(2500);
        closeDialogsLater(50);
        settle(100);
        fact(QStringLiteral("S-A04 ") + t.what, msg.isEmpty() ? (e && e->toPlainText() != before ? e->toPlainText().right(20) : QStringLiteral("nothing")) : msg.left(160));
    }
    as.citationsApp.clear();
    as.save();
    finishProject(w);
    delete w;
}

// ================================================================ S-A05 the Help menu
void caseSA05()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    // a planted crash report for Help > Crash Reports
    QDir().mkpath(crash::reportDir());
    writeText(crash::reportDir() + QStringLiteral("/crash-2026-11-09-10-00-00-1234.txt"), "Quillbinder crash report (planted by the system test)\nsignal 11\n");
    QStringList seen;
    for (const QString &a : {QStringLiteral("manual"), QStringLiteral("quickStart"), QStringLiteral("placeholders"), QStringLiteral("keyboardShortcuts"), QStringLiteral("addonManager"),
                             QStringLiteral("mindMapHelp"), QStringLiteral("developerGuide"), QStringLiteral("systemInfo"), QStringLiteral("crashReports"), QStringLiteral("licenses"),
                             QStringLiteral("about")}) {
        withModal<QDialog>(500, [a, &seen](QDialog *d) {
            seen << a + QStringLiteral(": ") + d->windowTitle();
            shot(d, QStringLiteral("help-") + a);
            for (QPushButton *b : d->findChildren<QPushButton *>())
                if (b->text().remove(QLatin1Char('&')).contains(QLatin1String("Copy")))
                    b->click();
            d->reject();
        });
        check(chooseByMenu(w, a), QStringLiteral("Help > %1").arg(a));
        settle(900);
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (tl != w && tl->isVisible() && !qobject_cast<QMenu *>(tl))
                tl->close();
        settle(100);
    }
    fact(QStringLiteral("S-A05 windows"), seen.join(QLatin1String("; ")));
    fact(QStringLiteral("S-A05 Open Plugin Folder"), QStringLiteral("not chosen (it opens the desktop's file manager)"));
    checkCleanWindows(w);
    // the planted report is no crash of this case: it goes before the verify step looks (shakedown 2026-11-09)
    QFile::remove(crash::reportDir() + QStringLiteral("/crash-2026-11-09-10-00-00-1234.txt"));
    finishProject(w);
    delete w;
}

// ================================================================ S-D02 files changed outside the program
void caseSD02()
{
    QString err;
    const QString scriv = fixtureCopy(QStringLiteral("fx1-scriv"));
    MainWindow *w = openWindow(scriv);
    Project *p = w->project();
    if (!p)
        return;
    const QList<BinderItem *> docs = textItems(p);
    const QString title = docs.value(4)->title;
    w->closeProject();
    settle(300);
    // (a) closed: a content.rtf changed as Scrivener would
    QStringList changed;
    QDirIterator it(scriv + QStringLiteral("/Files/Data"), {QStringLiteral("content.rtf")}, QDir::Files, QDirIterator::Subdirectories);
    int n = 0;
    while (it.hasNext() && n < 2) {
        const QString f = it.next();
        QByteArray b = readText(f).toUtf8();
        if (b.size() < 50)
            continue;
        b.insert(b.lastIndexOf('}'), QByteArray("\\par Changed in Scrivener (a)."));
        writeText(f, b);
        changed << f;
        ++n;
    }
    BoxText msg;
    answerBoxLater(800, QMessageBox::Ok, &msg);
    w->openScrivenerProject(scriv);
    settle(1500);
    fact(QStringLiteral("S-D02 (a) the message after opening"), msg.left(200));
    int shown = 0;
    if (w->project())
        for (BinderItem *x : textItems(w->project()))
            if (docText(w->project(), x).contains(QLatin1String("Changed in Scrivener (a).")))
                ++shown;
    check(shown == changed.size(), QStringLiteral("(a) the documents changed in Scrivener show their new text (%1 of %2)").arg(shown).arg(changed.size()));
    // (b) while open, then a full save
    if (!changed.isEmpty()) {
        QByteArray b = readText(changed.first()).toUtf8();
        b.insert(b.lastIndexOf('}'), QByteArray("\\par Changed in Scrivener (b)."));
        writeText(changed.first(), b);
    }
    if (w->project()) {
        if (TextEditor *e = showDoc(w, itemByTitle(w->project(), title))) {
            endOf(e);
            typeInto(e, QStringLiteral(" Typed here (b)."));
        }
        w->project()->save(&err);
        settle(500);
        fact(QStringLiteral("S-D02 (b) the change outside while open"), readText(changed.value(0)).contains(QLatin1String("(b)")) ? QStringLiteral("kept in the .scriv") : QStringLiteral("written over"));
        // 0.64.2: the program keeps the outside change in the .scriv and reads it at the next opening; the
        // model the verify step compares with is taken from what it reads then
        w = reopenWindow(w);
    }
    finishProject(w);
    delete w;
}

// ================================================================ S-D03 folder and disk problems through the user interface
void caseSD03()
{
    for (const QString &fx : {QStringLiteral("fx1-qb"), QStringLiteral("fx1-scriv")}) {
        const QString path = fixtureCopy(fx);
        MainWindow *w = openWindow(path);
        Project *p = w->project();
        if (!p)
            continue;
        BinderItem *it = textItems(p).value(3);
        const QString title = it->title;
        TextEditor *e = showDoc(w, it);
        if (!e)
            continue;
        // the folder renamed while typing
        const QString folder = fx.endsWith(QLatin1String("scriv")) ? path : QFileInfo(path).absolutePath();
        const QString moved = folder + QStringLiteral("-moved");
        endOf(e);
        typeInto(e, QStringLiteral(" Before the rename."));
        QDir().rename(folder, moved);
        typeInto(e, QStringLiteral(" While renamed."));
        BoxText msg;
        answerBoxLater(500, QMessageBox::Ok, &msg);
        chooseByMenu(w, QStringLiteral("save"));
        settle(1500);
        fact(QStringLiteral("S-D03 %1: File > Save while the folder is renamed").arg(fx), msg.isEmpty() ? QStringLiteral("no message") : msg.left(160));
        QDir().rename(moved, folder);
        typeInto(e, QStringLiteral(" After the fix."));
        chooseByMenu(w, QStringLiteral("save"));
        settle(800);
        w->closeProject();
        settle(300);
        w->openProject(path) || w->openScrivenerProject(path);
        settle(800);
        BinderItem *again = w->project() ? itemByTitle(w->project(), title) : nullptr;
        check(again && docText(w->project(), again).contains(QLatin1String("After the fix.")), QStringLiteral("%1: the typed text is saved after the fix").arg(fx));
        if (::geteuid() == 0)
            note(QStringLiteral("[A] root: the read-only folder is left out"));
        if (w->project())
            finishProject(w);
        delete w;
    }
}

// ================================================================ S-D04 backups through the user interface
void caseSD04()
{
    AppSettings &as = AppSettings::instance();
    as.backupDir = ctx().work + QStringLiteral("/backups");
    as.backupKeep = 3;
    as.backupOnOpen = true;
    as.backupOnClose = true;
    as.save();
    const QString path = fixtureCopy(QStringLiteral("fx1-qb"));
    MainWindow *w = openWindow(path);
    if (!w->project())
        return;
    for (int i = 0; i < 6; ++i) {
        answerBoxLater(700, QMessageBox::Ok); // "Backup saved: ..."
        chooseByMenu(w, QStringLiteral("backupNow"));
        settle(1200);   // backups carry the time in their names
    }
    const QStringList zips = zipsIn(as.backupDir);
    fact(QStringLiteral("S-D04 backups"), zips.join(QLatin1String(", ")));
    check(zips.size() == 3, QStringLiteral("3 backups remain of 6 (keep 3)"), QString::number(zips.size()));
    // 0.64.2: Back Up To asks for a folder (the backup's name is made as for every backup), then says where it went
    const QString elsewhere = ctx().work + QStringLiteral("/elsewhere");
    QDir().mkpath(elsewhere);
    fileDialogLater(300, elsewhere);
    answerBoxLater(2500, QMessageBox::Ok);
    chooseByMenu(w, QStringLiteral("backupTo"));
    settle(3000);
    check(zipsIn(elsewhere).size() == 1, QStringLiteral("Back Up To writes one backup into the folder chosen"), zipsIn(elsewhere).join(QLatin1String(", ")));
    const QJsonObject want = modelOf(w->project(), false);
    w->closeProject();
    settle(500);
    // unzip the newest and open it
    const QStringList all = zipsIn(as.backupDir);
    if (!all.isEmpty()) {
        const QString out = ctx().work + QStringLiteral("/restored");
        ZipReader z(all.last());
        z.extractAll(out);
        QString qbx;
        qbx = projectFileIn(out);   // 0.64.2: not the spare copy in settings
        check(!qbx.isEmpty() && w->openProject(qbx), QStringLiteral("a backup opens as a project"));
        if (w->project())
            check(compareModels(want, modelOf(w->project(), false), QStringLiteral("the backup")) == 0, QStringLiteral("the opened backup equals the project"));
        if (w->project())
            w->closeProject();
    }
    as.backupOnOpen = as.backupOnClose = false;
    as.save();
    delete w;
}

// ================================================================ S-D05 how fast can a writer type?
void caseSD05()
{
    struct Situation {
        QString what;
        QString fixture;
        std::function<TextEditor *(MainWindow *)> setUp;
    };
    QList<Situation> situations = {
        {QStringLiteral("(a) a scene of FX-1"), QStringLiteral("fx1-scriv"), [](MainWindow *w) { return showDoc(w, textItems(w->project()).value(3)); }},
        {QStringLiteral("(b) FX-2 while autosaves run"), QStringLiteral("fx2-scriv"), [](MainWindow *w) { return showDoc(w, textItems(w->project()).value(100)); }},
        {QStringLiteral("(c) the 60,000-word document"), QStringLiteral("fx1-qb"), [](MainWindow *w) {
             Project *p = w->project();
             BinderItem *it = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Long ramp"));
             QString big;
             for (quint32 s = 0; big.size() < 360000; ++s)
                 big += atParagraphs(4100 + s, 3000).join(QLatin1Char('\n')) + QLatin1Char('\n');
             QTextCursor(p->docs()->text(it->id)).insertText(big);
             return showDoc(w, it);
         }},
    };
    if (!ctx().quick) {
        situations << Situation{QStringLiteral("(d) Combined Text of a chapter"), QStringLiteral("fx1-qb"), [](MainWindow *w) -> TextEditor * {
            Project *p = w->project();
            BinderItem *ch = p->draft()->children.value(0) ? p->draft()->children.value(0)->children.value(0) : nullptr;
            if (!ch)
                return nullptr;
            visiblePane(w)->showItems({ch}, false, int(ViewMode::Combined));
            settle(500);
            TextEditor *last = nullptr;
            for (TextEditor *x : visiblePane(w)->findChildren<TextEditor *>())
                if (x->isVisible())
                    last = x;
            return last;
        }};
        situations << Situation{QStringLiteral("(e) one document in both split editors"), QStringLiteral("fx1-qb"), [](MainWindow *w) -> TextEditor * {
            BinderItem *it = textItems(w->project()).value(5);
            chooseByMenu(w, QStringLiteral("splitV"));
            for (EditorPane *pn : visiblePanes(w))
                pn->showItems({it}, false, int(ViewMode::Single));
            return visiblePanes(w).value(0) ? visiblePanes(w).value(0)->currentEditor() : nullptr;
        }};
        situations << Situation{QStringLiteral("(f) English with German words, spelling on"), QStringLiteral("fx1-qb"), [](MainWindow *w) {
            AppSettings::instance().spellCheck = true;
            AppSettings::instance().save();
            return showDoc(w, textItems(w->project()).value(6));
        }};
    }
    const QList<int> steps = {20, 40, 60, 90, 120, 150, 200, 250, 300};
    QStringList summary;
    for (const Situation &s : situations) {
        MainWindow *w = openWindow(fixtureCopy(s.fixture));
        if (!w->project()) {
            delete w;
            continue;
        }
        TextEditor *e = s.setUp(w);
        if (!e) {
            finishProject(w);
            delete w;
            continue;
        }
        endOf(e);
        int keeps = 0;
        for (int wpm : steps) {
            const int before = ctx().failures;
            const GateResult r = typingGate(e, QStringLiteral("wpm%1").arg(wpm), 20, QStringLiteral("S-D05 ") + s.what);
            if (r.result == QLatin1String("Fail")) {
                if (!r.textOk && keeps > 0)
                    note(QStringLiteral("lost keys at %1 WPM, above the last speed that held (%2 WPM)").arg(wpm).arg(keeps));
                ctx().failures = before + (r.textOk ? 0 : (keeps == 0 ? 1 : 0));   // the breaking step ends the ramp; lost keys below it fail
                break;
            }
            keeps = wpm;
        }
        summary << QStringLiteral("%1: keeps pace up to %2 WPM").arg(s.what).arg(keeps);
        finishProject(w);
        delete w;
    }
    AppSettings::instance().spellCheck = false;
    AppSettings::instance().save();
    fact(QStringLiteral("S-D05 speed per situation"), summary.join(QLatin1String("; ")));
}

// ================================================================ S-E01, S-E02 a writing day
namespace {
void writingDay(const QString &fixture, int minutes, quint32 seed)
{
    MainWindow *w = openWindow(fixtureCopy(fixture));
    Project *p = w->project();
    if (!p)
        return;
    std::mt19937 r(seed);
    QElapsedTimer t;
    t.start();
    qint64 nextCheck = 600000;
    int actions = 0;
    QMap<QString, int> counts;
    // the editor is replaced by many of the actions (views, binder changes): a guarded pointer (shakedown 2026-11-09)
    QPointer<TextEditor> e = showDoc(w, textItems(p).value(1));
    while (t.elapsed() < qint64(minutes) * 60000) {
        const int roll = int(r() % 100);
        QString kind;
        const QList<BinderItem *> docs = textItems(p);
        if (roll < 38) {
            kind = QStringLiteral("typing");
            if (!e)
                e = showDoc(w, docs.value(int(r() % quint32(docs.size()))));
            if (e) {
                endOf(e);
                // keysInto: QTest::keyClicks knows ASCII only and aborts on ä, ö, ü, ß (shakedown 2026-11-09)
                keysInto(e, atParagraphs(seed + quint32(actions), 12).join(QLatin1Char(' ')) + QLatin1Char(' '));
            }
        } else if (roll < 52) {
            kind = QStringLiteral("navigation");
            e = showDoc(w, docs.value(int(r() % quint32(docs.size()))));
        } else if (roll < 62) {
            kind = QStringLiteral("formatting");
            if (e) {
                QTextCursor c(e->document());
                c.setPosition(int(r() % quint32(qMax(1, e->document()->characterCount() - 10))));
                c.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
                e->setTextCursor(c);
                chooseByMenu(w, QStringList{QStringLiteral("bold"), QStringLiteral("italic"), QStringLiteral("style:Block Quote"), QStringLiteral("style:")}.at(int(r() % 4)));
            }
        } else if (roll < 71) {
            kind = QStringLiteral("binder");
            BinderItem *x = docs.value(int(r() % quint32(docs.size())));
            if (r() % 2)
                p->model()->moveItems({x}, x->parent, 0);
            else {
                BinderItem *n = p->model()->createItem(x->parent, -1, ItemType::Text, QStringLiteral("Day note %1").arg(actions));
                fillText(p, n, {QStringLiteral("A note of the day.")});
            }
            e = nullptr;
        } else if (roll < 80) {
            kind = QStringLiteral("views");
            selectItems(w, {p->draft()->children.value(int(r() % quint32(qMax(1, int(p->draft()->children.size())))))});
            chooseByMenu(w, QStringList{QStringLiteral("viewCork"), QStringLiteral("viewOutline"), QStringLiteral("viewText")}.at(int(r() % 3)));
            e = nullptr;
        } else if (roll < 89) {
            kind = QStringLiteral("inspector");
            BinderItem *x = docs.value(int(r() % quint32(docs.size())));
            x->synopsis += QStringLiteral(" more");
            p->notifyItemChanged(x);
        } else if (roll < 94) {
            kind = QStringLiteral("search");
            if (BinderPanel *bp = binderOf(w)) {
                SearchQuery q;
                q.text = QStringLiteral("the");
                bp->runSearch(q);
                bp->hideSearch();
            }
        } else if (roll < 99) {
            kind = QStringLiteral("tables and pictures");
            if (e) {
                endOf(e);
                if (r() % 2)
                    e->insertTableAtCursor(3, 3, tables::preset(QStringLiteral("grid")));
                else
                    insertPicture(p, visiblePane(w)->currentItem(), e->document(), e->textCursor().position(), testPicture(80, 60));
            }
        } else {
            kind = QStringLiteral("recording (fake engine)");
        }
        counts[kind] += 1;
        ++actions;
        settle(20);
        if (t.elapsed() > nextCheck) {
            nextCheck += 600000;
            QString err;
            p->save(&err);
            fact(QStringLiteral("memory after %1 min (KiB)").arg(t.elapsed() / 60000), readText(QStringLiteral("/proc/self/status")).section(QStringLiteral("VmRSS:"), 1).section(QLatin1Char('\n'), 0, 0).trimmed());
        }
    }
    QStringList c;
    for (auto i = counts.begin(); i != counts.end(); ++i)
        c << QStringLiteral("%1 %2").arg(i.key()).arg(i.value());
    fact(QStringLiteral("actions"), c.join(QLatin1String(", ")));
    checkCleanWindows(w);
    finishProject(w);
    delete w;
}
} // namespace

void caseSE01() { writingDay(QStringLiteral("fx1-scriv"), ctx().smoke ? 1 : ctx().quick ? 10 : 60, ctx().seed); }
void caseSE02() { writingDay(QStringLiteral("fx1-qb"), ctx().smoke ? 1 : ctx().quick ? 5 : 20, ctx().seed + 17); }

// ================================================================ S-E03 twenty sessions
void caseSE03()
{
    AppSettings &as = AppSettings::instance();
    as.backupOnClose = true;
    as.backupKeep = 5;
    as.backupDir = ctx().work + QStringLiteral("/backups");
    as.save();
    const QString path = fixtureCopy(QStringLiteral("fx1-scriv"));
    const int sessions = ctx().smoke ? 2 : ctx().quick ? 5 : 20;
    QStringList marks;
    for (int s = 0; s < sessions; ++s) {
        MainWindow *w = openWindow(path);
        Project *p = w->project();
        if (!p) {
            delete w;
            continue;
        }
        BinderItem *it = textItems(p).value(s % 40);
        if (TextEditor *e = showDoc(w, it)) {
            endOf(e);
            const QString mark = QStringLiteral(" Session %1 ends here.").arg(s);
            typeInto(e, QLatin1Char(' ') + atParagraphs(4200 + quint32(s), 300).join(QLatin1Char(' ')) + mark);
            marks << mark.trimmed();
            snapshots::take(p, it->id, QStringLiteral("Session %1").arg(s));
        }
        w->closeProject();
        settle(500);
        delete w;
    }
    MainWindow *w = openWindow(path);
    int found = 0;
    if (Project *p = w->project())
        for (const QString &m : marks)
            for (BinderItem *it : textItems(p))
                if (docText(p, it).contains(m)) {
                    ++found;
                    break;
                }
    check(found == marks.size(), QStringLiteral("the words of all %1 sessions are there (%2)").arg(marks.size()).arg(found));
    check(zipsIn(as.backupDir).size() <= 5, QStringLiteral("backups kept to the keep count"), QString::number(zipsIn(as.backupDir).size()));
    withModal<QDialog>(400, [](QDialog *d) {
        shot(d, QStringLiteral("writing-history"));
        d->accept();
    });
    chooseByMenu(w, QStringLiteral("history"));
    settle(600);
    as.backupOnClose = false;
    as.save();
    finishProject(w);
    delete w;
}

// ================================================================ S-E04 menu monkey
void caseSE04()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    const QSet<QString> never = {QStringLiteral("quit"), QStringLiteral("closeProject"), QStringLiteral("emptyTrash"), QStringLiteral("deleteSnapshots"),
                                 QStringLiteral("showProjectFolder"), QStringLiteral("showBackups"), QStringLiteral("pluginFolder"), QStringLiteral("fontFolder"),
                                 QStringLiteral("fontManager"), QStringLiteral("integrate"), QStringLiteral("formatScrivener"), QStringLiteral("formatQuillbinder"),
                                 QStringLiteral("installPackage"), QStringLiteral("newProject"), QStringLiteral("openProject"), QStringLiteral("print")};
    std::mt19937 r(ctx().seed);
    const int n = qEnvironmentVariableIsSet("QB_CS_MONKEY_N") ? qEnvironmentVariableIntValue("QB_CS_MONKEY_N")   // to find a step
                  : ctx().smoke ? 60 : ctx().quick ? 500 : 2000;
    int done = 0;
    // every action of the menu bar (its leaves)
    // 0.64.2: guarded pointers: menus filled when they open (Go To, Open Recent ...) delete their old entries
    QList<QPointer<QAction>> pool;
    std::function<void(QMenu *)> collect = [&](QMenu *m) {
        emit m->aboutToShow();
        for (QAction *a : m->actions()) {
            if (a->isSeparator())
                continue;
            if (a->menu())
                collect(a->menu());
            else
                pool << a;
        }
    };
    for (QAction *top : w->menuBar()->actions())
        if (top->menu())
            collect(top->menu());
    // the answers: every window that opens is cancelled after a moment, file dialogs too
    auto *closer = new QTimer(w);
    closer->setInterval(300);
    QObject::connect(closer, &QTimer::timeout, [] {
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *d = qobject_cast<QDialog *>(tl); d && d->isVisible())
                d->reject();
    });
    closer->start();
    for (int i = 0; i < n && !pool.isEmpty(); ++i) {
        QAction *a = pool.at(int(r() % quint32(pool.size())));
        if (!a)
            continue;
        QObject *target = a->property("qbAliasOf").value<QObject *>();
        const QString name = target ? target->objectName() : a->objectName();
        if (never.contains(name) || !a->isEnabled())
            continue;
        static const bool every = qEnvironmentVariableIsSet("QB_CS_MONKEY_ALL");   // every entry in the log (to find a step)
        if (every || i % 25 == 0)
            step(QStringLiteral("monkey %1: %2 (%3)").arg(i).arg(a->text().remove(QLatin1Char('&')), name));
        a->trigger();
        settle(40);
        ++done;
        if (!w->project())
            break;
    }
    closer->stop();
    fact(QStringLiteral("S-E04 menu entries chosen"), done);
    checkCleanWindows(w);
    if (w->project()) {
        const QString path = projectPathOf(w->project());
        w->project()->save();
        writeModel(w->project());
        w->closeProject();
        settle(300);
        check(path.endsWith(QLatin1String(".scriv")) ? w->openScrivenerProject(path) : w->openProject(path), QStringLiteral("the project opens again after the monkey"));
        if (w->project())
            w->closeProject();
    }
    delete w;
}

} // namespace cs
