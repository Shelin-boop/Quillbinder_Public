// The system test, stage 2: usability (U-01 to U-19), section 6.3. These cases collect facts for the
// review (ISO 9241-110, Nielsen's heuristics); they fail only when the program crashes, freezes or a
// step cannot be done at all. Reports go into the case folder as tab-separated tables.
#include "cs.h"

#include "core/SettingsLayer.h"
#include "core/Snapshots.h"
#include "core/SyncSafety.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/EditorPane.h"
#include "ui/FirstStartDialog.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include <QAbstractButton>
#include <QAccessible>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>

#include <cmath>

namespace cs {

using namespace qb;

namespace {

// Keystroke-Level Model (Card, Moran, Newell): K a key (0.28 s, an average typist), P pointing (1.1 s),
// B a click (0.1 s), H the hand between keyboard and mouse (0.4 s), M a mental step (1.35 s)
double klm(int keys, int points, int clicks, int homes, int mental)
{
    return keys * 0.28 + points * 1.1 + clicks * 0.1 + homes * 0.4 + mental * 1.35;
}

struct Entry {
    QString path;
    QString name;
    QString shortcut;
    bool enabled = false;
    int depth = 0;
};

QList<Entry> menuEntries(MainWindow *w)
{
    QList<Entry> out;
    std::function<void(QMenu *, const QString &, int)> walk = [&](QMenu *m, const QString &path, int depth) {
        emit m->aboutToShow();
        for (QAction *a : m->actions()) {
            if (a->isSeparator())
                continue;
            const QString here = path + QStringLiteral(" > ") + a->text().remove(QLatin1Char('&'));
            if (a->menu()) {
                walk(a->menu(), here, depth + 1);
                continue;
            }
            QObject *target = a->property("qbAliasOf").value<QObject *>();
            auto *ta = qobject_cast<QAction *>(target);
            Entry e;
            e.path = here.mid(3);
            e.name = target ? target->objectName() : a->objectName();
            e.shortcut = (ta ? ta->shortcut() : a->shortcut()).toString();
            e.enabled = a->isEnabled();
            e.depth = depth;
            out << e;
        }
    };
    for (QAction *top : w->menuBar()->actions())
        if (top->menu())
            walk(top->menu(), QStringLiteral(" > ") + top->text().remove(QLatin1Char('&')), 1);
    return out;
}

// the entries of the right-click menu at a place (the menu is read, then closed)
QStringList contextEntries(QWidget *target, const QPoint &pos)
{
    QStringList out;
    QTimer::singleShot(300, qApp, [&out] {
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *m = qobject_cast<QMenu *>(tl); m && m->isVisible()) {
                for (QAction *a : m->actions())
                    out << (a->isSeparator() ? QStringLiteral("|") : a->text().remove(QLatin1Char('&')) + (a->menu() ? QStringLiteral(" >") : QString()));
                m->close();
            }
    });
    QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, target->mapToGlobal(pos));
    QApplication::sendEvent(target, &ev);
    settle(500);
    return out;
}

double luminance(const QColor &c)
{
    auto ch = [](double v) {
        v /= 255.0;
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * ch(c.red()) + 0.7152 * ch(c.green()) + 0.0722 * ch(c.blue());
}

double contrast(const QColor &a, const QColor &b)
{
    const double la = luminance(a), lb = luminance(b);
    return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
}

void writeTable(const QString &name, const QStringList &rows)
{
    writeText(ctx().dir + QLatin1Char('/') + name, rows.join(QLatin1Char('\n')).toUtf8());
}

QMap<QString, QByteArray> fileHashes(const QStringList &files)
{
    QMap<QString, QByteArray> out;
    for (const QString &f : files)
        out.insert(f, QCryptographicHash::hash(readText(f).toUtf8(), QCryptographicHash::Sha1));
    return out;
}

} // namespace

// ================================================================ U-01 common workflows: steps and time
void caseU01()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    const QList<Entry> entries = menuEntries(w);
    QHash<QString, Entry> byName;
    for (const Entry &e : entries)
        if (!e.name.isEmpty() && !byName.contains(e.name))
            byName.insert(e.name, e);
    QSet<QString> onToolbar;
    for (QToolBar *tb : w->findChildren<QToolBar *>())
        for (QAction *a : tb->actions())
            onToolbar.insert(a->objectName());
    struct Flow {
        QString what, action;
        int extraKeys;   // typing that belongs to the step (a title, a word)
    };
    const QList<Flow> flows = {
        {QStringLiteral("new scene after the current one"), QStringLiteral("newText"), 0}, {QStringLiteral("new chapter folder"), QStringLiteral("newFolder"), 0},
        {QStringLiteral("rename"), QStringLiteral("rename"), 10}, {QStringLiteral("move a scene to another chapter"), QStringLiteral("moveDown"), 0},
        {QStringLiteral("open a document by name"), QStringLiteral("projectSearch"), 8}, {QStringLiteral("find a word in the project"), QStringLiteral("projectSearch"), 6},
        {QStringLiteral("italics"), QStringLiteral("italic"), 0}, {QStringLiteral("a paragraph style"), QStringLiteral("stylesPanel"), 0},
        {QStringLiteral("a comment"), QStringLiteral("comment"), 10}, {QStringLiteral("a footnote"), QStringLiteral("footnote"), 10},
        {QStringLiteral("a snapshot"), QStringLiteral("snapshot"), 0}, {QStringLiteral("compare with a snapshot"), QStringLiteral("showSnapshots"), 0},
        {QStringLiteral("corkboard and back"), QStringLiteral("viewCork"), 0}, {QStringLiteral("label and status"), QString(), 0},
        {QStringLiteral("a synopsis"), QStringLiteral("inspect0"), 20}, {QStringLiteral("document notes"), QStringLiteral("inspect0"), 20},
        {QStringLiteral("split a scene"), QStringLiteral("split"), 0}, {QStringLiteral("merge two scenes"), QStringLiteral("merge"), 0},
        {QStringLiteral("compile to Word and PDF"), QStringLiteral("compile"), 0}, {QStringLiteral("back up"), QStringLiteral("backupNow"), 0},
        {QStringLiteral("open a recent project"), QString(), 0}, {QStringLiteral("see the word count and target"), QStringLiteral("targets"), 0},
        {QStringLiteral("look at a character sheet while writing"), QStringLiteral("quickRef"), 0}, {QStringLiteral("undo"), QStringLiteral("undo"), 0},
        {QStringLiteral("composition mode"), QStringLiteral("composition"), 0},
    };
    QStringList rows = {QStringLiteral("workflow\tshortest path\tsteps\tKLM seconds\tshortcut\ttoolbar\tmenu path")};
    int over6 = 0;
    for (const Flow &f : flows) {
        const Entry e = byName.value(f.action);
        const bool shortcut = !e.shortcut.isEmpty();
        const bool toolbar = onToolbar.contains(f.action);
        int steps;
        double secs;
        QString path;
        if (shortcut) {
            steps = 1;
            secs = klm(2 + f.extraKeys, 0, 0, 0, 1);
            path = e.shortcut;
        } else if (toolbar) {
            steps = 1;
            secs = klm(f.extraKeys, 1, 1, 1, 1);
            path = QStringLiteral("toolbar");
        } else {
            steps = qMax(1, e.depth) + 1;
            secs = klm(f.extraKeys, steps, steps, 1, 1);
            path = e.path.isEmpty() ? QStringLiteral("(not in the menu bar)") : e.path;
        }
        if (f.action.isEmpty()) {
            steps = 3;
            secs = klm(0, 3, 3, 1, 2);
            path = f.what == QLatin1String("open a recent project") ? QStringLiteral("File > Open Recent > (name)") : QStringLiteral("Documents > Label / Status > (value)");
        }
        if (steps > 6)
            ++over6;
        rows << QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6\t%7").arg(f.what, path).arg(steps).arg(secs, 0, 'f', 1).arg(shortcut ? e.shortcut : QStringLiteral("none"),
                                                                                                                     toolbar ? QStringLiteral("yes") : QStringLiteral("no"), e.path);
    }
    writeTable(QStringLiteral("u01-workflows.tsv"), rows);
    fact(QStringLiteral("U-01 workflows over 6 steps"), over6);
    finishProject(w);
    delete w;
}

// ================================================================ U-02 menu bar structure
void caseU02()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    QStringList rows = {QStringLiteral("menu\tentries\tseparators\tdepth\tdialogs without \"...\"")};
    for (QAction *top : w->menuBar()->actions()) {
        QMenu *m = top->menu();
        if (!m)
            continue;
        int entries = 0, separators = 0, depth = 1;
        QStringList noDots;
        std::function<void(QMenu *, int)> walk = [&](QMenu *mm, int d) {
            emit mm->aboutToShow();
            depth = qMax(depth, d);
            for (QAction *a : mm->actions()) {
                if (a->isSeparator()) {
                    ++separators;
                    continue;
                }
                ++entries;
                if (a->menu())
                    walk(a->menu(), d + 1);
            }
        };
        walk(m, 1);
        Q_UNUSED(noDots)
        rows << QStringLiteral("%1\t%2\t%3\t%4\t").arg(top->text().remove(QLatin1Char('&'))).arg(entries).arg(separators).arg(depth);
    }
    // standard shortcuts of Linux desktop programs
    const QList<QPair<QString, QString>> standard = {{QStringLiteral("save"), QStringLiteral("Ctrl+S")}, {QStringLiteral("openProject"), QStringLiteral("Ctrl+O")},
                                                     {QStringLiteral("undo"), QStringLiteral("Ctrl+Z")}, {QStringLiteral("find"), QStringLiteral("Ctrl+F")},
                                                     {QStringLiteral("print"), QStringLiteral("Ctrl+P")}, {QStringLiteral("quit"), QStringLiteral("Ctrl+Q")},
                                                     {QStringLiteral("newProject"), QStringLiteral("Ctrl+Shift+N")}, {QStringLiteral("copy"), QStringLiteral("Ctrl+C")},
                                                     {QStringLiteral("paste"), QStringLiteral("Ctrl+V")}, {QStringLiteral("cut"), QStringLiteral("Ctrl+X")}};
    QStringList differ;
    for (const auto &s : standard)
        if (QAction *a = action(w, s.first); a && a->shortcut().toString() != s.second)
            differ << QStringLiteral("%1: %2 (common: %3)").arg(s.first, a->shortcut().toString(), s.second);
    // the same command under different names
    QMultiHash<QString, QString> names;
    for (const Entry &e : menuEntries(w))
        if (!e.name.isEmpty())
            names.insert(e.name, e.path.section(QStringLiteral(" > "), -1));
    QStringList twoNames;
    for (const QString &k : names.uniqueKeys()) {
        QStringList ns = names.values(k);
        ns.removeDuplicates();
        if (ns.size() > 1)
            twoNames << k + QStringLiteral(": ") + ns.join(QLatin1String(" / "));
    }
    writeTable(QStringLiteral("u02-menus.tsv"), rows);
    fact(QStringLiteral("U-02 standard shortcuts that differ"), differ.join(QLatin1String("; ")));
    fact(QStringLiteral("U-02 one command, several names"), twoNames.join(QLatin1String("; ")));
    finishProject(w);
    delete w;
}

// ================================================================ U-03 right-click menus fit their place
void caseU03()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    QStringList rows = {QStringLiteral("place\tentries")};
    BinderPanel *bp = binderOf(w);
    if (bp) {
        rows << QStringLiteral("binder item\t") + contextEntries(bp->tree()->viewport(), binderPoint(w, textItems(p).value(2))).join(QLatin1String(", "));
        rows << QStringLiteral("binder background\t") + contextEntries(bp->tree()->viewport(), QPoint(10, bp->tree()->viewport()->height() - 5)).join(QLatin1String(", "));
    }
    if (TextEditor *e = showDoc(w, textItems(p).value(2))) {
        QTextCursor c(e->document());
        c.setPosition(5);
        rows << QStringLiteral("text\t") + contextEntries(e->viewport(), e->cursorRect(c).center()).join(QLatin1String(", "));
        c.setPosition(20, QTextCursor::KeepAnchor);
        e->setTextCursor(c);
        rows << QStringLiteral("a selection\t") + contextEntries(e->viewport(), e->cursorRect(c).center()).join(QLatin1String(", "));
    }
    selectItems(w, {p->draft()->children.value(0)});
    chooseByMenu(w, QStringLiteral("viewCork"));
    for (QWidget *x : w->findChildren<QWidget *>())
        if (x->inherits("qb::Corkboard") && x->isVisible())
            rows << QStringLiteral("corkboard\t") + contextEntries(qobject_cast<QAbstractScrollArea *>(x)->viewport(), QPoint(80, 60)).join(QLatin1String(", "));
    chooseByMenu(w, QStringLiteral("viewOutline"));
    for (QWidget *x : w->findChildren<QWidget *>())
        if (x->inherits("qb::Outliner") && x->isVisible())
            rows << QStringLiteral("outliner row\t") + contextEntries(qobject_cast<QAbstractScrollArea *>(x)->viewport(), QPoint(80, 12)).join(QLatin1String(", "));
    for (QToolBar *tb : w->findChildren<QToolBar *>())
        if (tb->isVisible()) {
            rows << QStringLiteral("toolbar %1\t").arg(tb->objectName()) + contextEntries(tb, QPoint(5, 5)).join(QLatin1String(", "));
            break;
        }
    writeTable(QStringLiteral("u03-context-menus.tsv"), rows);
    fact(QStringLiteral("U-03 places read"), int(rows.size() - 1));
    finishProject(w);
    delete w;
}

// ================================================================ U-04 working with the keyboard only
void caseU04()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    QStringList findings;
    TextEditor *e = showDoc(w, textItems(p).value(4));
    if (e) {
        QTextCursor c = e->textCursor();
        c.movePosition(QTextCursor::End);
        e->setTextCursor(c);
        keysInto(e, QStringLiteral(" Typed with the keyboard only."));
    }
    // dialogs opened by their shortcuts: Tab through, then Esc; is the keyboard back in the editor?
    for (const QString &a : {QStringLiteral("options"), QStringLiteral("projectSettings"), QStringLiteral("compile"), QStringLiteral("snapshotTitle"), QStringLiteral("targets")}) {
        QAction *act = action(w, a);
        const int posBefore = e ? e->textCursor().position() : -1;
        QStringList order;
        withModal<QDialog>(400, [&order](QDialog *d) {
            for (int i = 0; i < 12; ++i) {
                QTest::keyClick(QApplication::focusWidget() ? QApplication::focusWidget() : d, Qt::Key_Tab);
                if (QWidget *f = QApplication::focusWidget())
                    order << (f->objectName().isEmpty() ? QString::fromLatin1(f->metaObject()->className()) : f->objectName());
            }
            QTest::keyClick(QApplication::focusWidget() ? QApplication::focusWidget() : d, Qt::Key_Escape);
            settle(200);
            if (d->isVisible()) {
                order << QStringLiteral("(Esc did not close it)");
                d->reject();
            }
        });
        if (act && !act->shortcut().isEmpty() && e)
            QTest::keySequence(e, act->shortcut());
        else if (act)
            act->trigger();
        settle(1500);
        QWidget *f = QApplication::focusWidget();
        const bool back = e && f == e && e->textCursor().position() == posBefore;
        if (!back)
            findings << QStringLiteral("%1: the keyboard is not back in the editor (%2)").arg(a, f ? QString::fromLatin1(f->metaObject()->className()) : QStringLiteral("none"));
        writeTable(QStringLiteral("u04-tab-order-%1.txt").arg(a), order);
        if (!act || act->shortcut().isEmpty())
            findings << a + QStringLiteral(": no shortcut");
    }
    fact(QStringLiteral("U-04 findings"), findings.join(QLatin1String("; ")));
    finishProject(w);
    delete w;
}

// ================================================================ U-05 dialogs and windows on the writer's screen
void caseU05()
{
    const QString v = variant();
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")), v == QLatin1String("small") ? 1366 : 1400, v == QLatin1String("small") ? 768 : 950);
    if (!w->project())
        return;
    const QRect screen = v == QLatin1String("small") ? QRect(0, 0, 1366, 768) : QGuiApplication::primaryScreen()->availableGeometry();
    QStringList rows = {QStringLiteral("dialog\tsize\tfits\tdefault button\tcancel or close")};
    for (const Entry &e : menuEntries(w)) {
        if (!e.path.endsWith(QStringLiteral("…")) || !e.enabled || e.name.isEmpty())
            continue;
        if (e.name == QLatin1String("newProject") || e.name == QLatin1String("openProject") || e.name == QLatin1String("installPackage") || e.name.startsWith(QLatin1String("import")))
            continue;
        QString row;
        withModal<QDialog>(500, [&row, screen, e](QDialog *d) {
            const QRect g = d->frameGeometry();
            bool hasDefault = false, hasCancel = false;
            for (QPushButton *b : d->findChildren<QPushButton *>()) {
                hasDefault = hasDefault || b->isDefault();
                const QString t = b->text().remove(QLatin1Char('&'));
                hasCancel = hasCancel || t == QLatin1String("Cancel") || t == QLatin1String("Close");
            }
            row = QStringLiteral("%1\t%2x%3\t%4\t%5\t%6").arg(e.path).arg(g.width()).arg(g.height()).arg(screen.contains(g) || (g.width() <= screen.width() && g.height() <= screen.height()) ? QStringLiteral("yes") : QStringLiteral("NO"))
                      .arg(hasDefault ? QStringLiteral("yes") : QStringLiteral("no"), hasCancel ? QStringLiteral("yes") : QStringLiteral("no"));
            shot(d, QStringLiteral("dialog-") + e.name);
            d->reject();
        }, 10);
        if (QAction *a = action(w, e.name))
            a->trigger();
        settle(800);
        closeDialogsLater(10);
        settle(80);
        if (!row.isEmpty())
            rows << row;
    }
    writeTable(QStringLiteral("u05-dialogs-%1.tsv").arg(v.isEmpty() ? QStringLiteral("1920") : v), rows);
    fact(QStringLiteral("U-05 dialogs read (%1)").arg(v.isEmpty() ? QStringLiteral("1920 x 1080") : v), int(rows.size() - 1));
    finishProject(w);
    delete w;
    if (v.isEmpty()) {
        QString fails;
        check(subCase(QStringLiteral("scale2"), {{QStringLiteral("QT_SCALE_FACTOR"), QStringLiteral("2")}}, &fails) == 0, QStringLiteral("the dialogs at scale 2"), fails);
        check(subCase(QStringLiteral("small"), {}, &fails) == 0, QStringLiteral("the dialogs at 1366 x 768"), fails);
    }
}

// ================================================================ U-06 messages
void caseU06()
{
    const QDir run(QFileInfo(ctx().dir).absolutePath());
    QStringList rows = {QStringLiteral("case\tmessage")};
    QStringList internal;
    static const QRegularExpression box(QStringLiteral("^\\s*(?:NOTE\\s*)?\\(([A-Za-z]+[^)]*): ([^)]*)\\) (.*)$"));
    static const QRegularExpression tech(QStringLiteral("(::|\\bQ[A-Z][a-z]+[A-Z]|errno|error code|0x[0-9a-f]{4,}|nullptr|SIG[A-Z]+)"));
    for (const QString &c : run.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        for (const QString &line : readText(run.filePath(c) + QStringLiteral("/case.log")).split(QLatin1Char('\n'))) {
            const auto m = box.match(line);
            if (!m.hasMatch())
                continue;
            const QString text = m.captured(3).trimmed();
            if (text.isEmpty())
                continue;
            rows << c + QLatin1Char('\t') + text;
            if (tech.match(text).hasMatch())
                internal << c + QStringLiteral(": ") + text.left(120);
        }
        const QJsonObject facts = QJsonDocument::fromJson(readText(run.filePath(c) + QStringLiteral("/facts.json")).toUtf8()).object();
        for (auto i = facts.begin(); i != facts.end(); ++i)
            if (i.key().contains(QLatin1String("message")) || i.key().contains(QLatin1String("question")))
                rows << c + QLatin1Char('\t') + i.value().toVariant().toString().left(300);
    }
    rows.removeDuplicates();
    writeTable(QStringLiteral("u06-messages.tsv"), rows);
    fact(QStringLiteral("U-06 messages collected"), int(rows.size() - 1));
    fact(QStringLiteral("U-06 messages with program-internal words"), internal.join(QLatin1String("; ")));
}

// ================================================================ U-07 where settings live
void caseU07()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    const QString program = settingslayer::programFile();
    const QStringList files = {program, p->settingsDir() + QStringLiteral("/look.json"), p->settingsDir() + QStringLiteral("/options.ini"), p->indexPath(), p->uiStateFile()};
    struct Change {
        QString what;
        std::function<void()> act;
    };
    const QList<Change> changes = {
        {QStringLiteral("View > Show Ruler"), [w] { chooseByMenu(w, QStringLiteral("showRuler")); }},
        {QStringLiteral("View > Page View"), [w] { chooseByMenu(w, QStringLiteral("pageView")); }},
        {QStringLiteral("Window > Themes > Sepia"), [w] { menuPath(w, QStringLiteral("Window > Themes > Sepia")); }},
        {QStringLiteral("View > Zoom In"), [w] { chooseByMenu(w, QStringLiteral("zoomIn")); }},
        {QStringLiteral("spelling language"), [] { AppSettings::instance().spellLanguage = QStringLiteral("de_DE"); AppSettings::instance().save(); }},
        {QStringLiteral("quote style"), [] { AppSettings::instance().quoteStyle = QStringLiteral("de"); AppSettings::instance().save(); }},
        {QStringLiteral("project targets"), [p] { p->targets.draftTarget = 12345; p->markIndexDirty(); p->save(); }},
        {QStringLiteral("View > Split Vertically"), [w] { chooseByMenu(w, QStringLiteral("splitV")); }},
    };
    QStringList rows = {QStringLiteral("setting\tprogram file\tlook.json\toptions.ini\tproject file\tview state")};
    for (const Change &c : changes) {
        const auto before = fileHashes(files);
        c.act();
        settle(400);
        if (w->project())
            w->project()->save();
        AppSettings::instance().save();
        const auto after = fileHashes(files);
        QString row = c.what;
        for (const QString &f : files)
            row += QLatin1Char('\t') + QString(before.value(f) != after.value(f) ? QStringLiteral("changed") : QString());
        rows << row;
    }
    writeTable(QStringLiteral("u07-settings.tsv"), rows);
    menuPath(w, QStringLiteral("Window > Themes > Light"));
    finishProject(w);
    delete w;
}

// ================================================================ U-08 same names everywhere; the manual's menu places exist
void caseU08()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    QSet<QString> paths;
    for (const Entry &e : menuEntries(w)) {
        QString p = e.path;
        while (!p.isEmpty()) {
            paths.insert(QString(p).remove(QStringLiteral("…")).trimmed());
            p = p.section(QStringLiteral(" > "), 0, -2);
        }
    }
    QStringList missing;
    int named = 0;
    static const QRegularExpression bold(QStringLiteral("\\*\\*([^*]+ > [^*]+)\\*\\*"));
    for (const QString &doc : {QStringLiteral("/docs/manual.md"), QStringLiteral("/docs/quick-start.md")}) {
        auto it = bold.globalMatch(readText(ctx().sourceDir + doc));
        while (it.hasNext()) {
            QString p = it.next().captured(1).remove(QStringLiteral("…")).remove(QStringLiteral("...")).trimmed();
            if (!p.contains(QStringLiteral(" > ")))
                continue;
            const QString first = p.section(QStringLiteral(" > "), 0, 0);
            if (!QStringList{QStringLiteral("File"), QStringLiteral("Edit"), QStringLiteral("View"), QStringLiteral("Insert"), QStringLiteral("Format"), QStringLiteral("Documents"),
                             QStringLiteral("Project"), QStringLiteral("Navigate"), QStringLiteral("Window"), QStringLiteral("Help")}.contains(first))
                continue;
            ++named;
            if (!paths.contains(p))
                missing << QFileInfo(doc).fileName() + QStringLiteral(": ") + p;
        }
    }
    missing.removeDuplicates();
    writeTable(QStringLiteral("u08-missing-menu-places.txt"), missing);
    fact(QStringLiteral("U-08 menu places named in the manuals / not found"), QStringLiteral("%1 / %2").arg(named).arg(missing.size()));
    finishProject(w);
    delete w;
}

// ================================================================ U-09 the first hour of a new writer
void caseU09()
{
    auto *w = new MainWindow;
    w->resize(1400, 950);
    w->show();
    settle(300);
    w->openWelcomeOnFirstStart();
    settle(800);
    QStringList noTip;
    for (QToolBar *tb : w->findChildren<QToolBar *>())
        for (QAction *a : tb->actions())
            if (!a->isSeparator() && a->toolTip().isEmpty() && a->text().isEmpty())
                noTip << tb->objectName() + QLatin1Char('/') + a->objectName();
    for (QToolButton *b : w->findChildren<QToolButton *>())
        if (b->isVisible() && b->text().isEmpty() && b->toolTip().isEmpty() && (!b->defaultAction() || b->defaultAction()->toolTip().isEmpty()))
            noTip << QStringLiteral("button ") + b->objectName();
    int noStatus = 0, total = 0;
    for (const Entry &e : menuEntries(w)) {
        ++total;
        if (QAction *a = action(w, e.name); a && a->statusTip().isEmpty())
            ++noStatus;
    }
    fact(QStringLiteral("U-09 toolbar buttons without a tool tip"), noTip.join(QLatin1String(", ")));
    fact(QStringLiteral("U-09 menu entries without a status tip"), QStringLiteral("%1 of %2").arg(noStatus).arg(total));
    shot(w, QStringLiteral("first-hour-welcome"));
    if (w->project())
        w->closeProject();
    delete w;
}

// ================================================================ U-10 feedback during long actions
void caseU10()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx2-scriv")));
    if (!w->project())
        return;
    QStringList rows = {QStringLiteral("action\tseconds\ttimer ticks while it ran\tprogress window\tbusy cursor")};
    for (const QString &a : {QStringLiteral("backupNow"), QStringLiteral("statistics"), QStringLiteral("projectSearch"), QStringLiteral("deleteSnapshots")}) {
        int ticks = 0;
        bool progress = false, busy = false;
        QTimer tick;
        tick.setInterval(250);
        QObject::connect(&tick, &QTimer::timeout, [&] {
            ++ticks;
            for (QWidget *tl : QApplication::topLevelWidgets())
                if (qobject_cast<QProgressDialog *>(tl) && tl->isVisible())
                    progress = true;
            if (QApplication::overrideCursor())
                busy = true;
        });
        closeDialogsLater(2500);
        QElapsedTimer t;
        t.start();
        tick.start();
        chooseByMenu(w, a);
        const qint64 ms = t.elapsed();
        tick.stop();
        settle(300);
        rows << QStringLiteral("%1\t%2\t%3\t%4\t%5").arg(a).arg(ms / 1000.0, 0, 'f', 1).arg(ticks).arg(progress ? QStringLiteral("yes") : QStringLiteral("no"), busy ? QStringLiteral("yes") : QStringLiteral("no"));
    }
    writeTable(QStringLiteral("u10-feedback.tsv"), rows);
    finishProject(w);
    delete w;
}

// ================================================================ U-11 destructive commands
void caseU11()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    QStringList rows = {QStringLiteral("command\tasks first\tundo works\tsnapshot taken")};
    struct Cmd {
        QString action;
        std::function<void()> prepare;
    };
    const QList<Cmd> cmds = {
        {QStringLiteral("trash"), [&] { selectItems(w, {textItems(p).value(3)}); }},
        {QStringLiteral("merge"), [&] { selectItems(w, textItems(p).mid(5, 2)); }},
        {QStringLiteral("split"), [&] { if (TextEditor *e = showDoc(w, textItems(p).value(8))) { QTextCursor c(e->document()); c.setPosition(30); e->setTextCursor(c); } }},
        {QStringLiteral("toFolder"), [&] { selectItems(w, {textItems(p).value(9)}); }},
        {QStringLiteral("defaultFormatting"), [&] { selectItems(w, {textItems(p).value(10)}); }},
        {QStringLiteral("tableDelete"), [&] { if (TextEditor *e = showDoc(w, textItems(p).value(11))) { e->insertTableAtCursor(2, 2, tables::preset(QStringLiteral("grid"))); } }},
        {QStringLiteral("emptyTrash"), [] {}},
        {QStringLiteral("deleteSnapshots"), [] {}},
    };
    for (const Cmd &c : cmds) {
        c.prepare();
        settle(150);
        QString asked;
        withModal<QMessageBox>(300, [&asked](QMessageBox *b) {
            asked = b->text().left(80);
            if (QAbstractButton *y = b->button(QMessageBox::Yes))
                y->click();
            else if (QAbstractButton *o = b->button(QMessageBox::Ok))
                o->click();
            else
                b->accept();
        }, 6);
        withModal<QDialog>(300, [](QDialog *d) {
            if (!qobject_cast<QMessageBox *>(d))
                d->accept();
        }, 6);
        int snapsBefore = 0;
        for (BinderItem *it : textItems(p))
            snapsBefore += int(snapshots::list(p, it->id).size());
        chooseByMenu(w, c.action);
        settle(600);
        int snapsAfter = 0;
        for (BinderItem *it : textItems(p))
            snapsAfter += int(snapshots::list(p, it->id).size());
        QAction *undo = action(w, QStringLiteral("undo"));
        w->updateFormatBar();
        rows << QStringLiteral("%1\t%2\t%3\t%4").arg(c.action, asked.isEmpty() ? QStringLiteral("no") : QStringLiteral("yes: ") + asked,
                                                     undo && undo->isEnabled() ? QStringLiteral("enabled") : QStringLiteral("no"), snapsAfter > snapsBefore ? QStringLiteral("yes") : QStringLiteral("no"));
    }
    writeTable(QStringLiteral("u11-destructive.tsv"), rows);
    finishProject(w);
    delete w;
}

// ================================================================ U-12 legibility of the themes
void caseU12()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    const QList<QPair<theme::Role, theme::Role>> pairs = {{theme::Role::Base, theme::Role::Text},
                                                          {theme::Role::Window, theme::Role::WindowText},
                                                          {theme::Role::BinderBackground, theme::Role::BinderText},
                                                          {theme::Role::EditorPaper, theme::Role::EditorText},
                                                          {theme::Role::CardPaper, theme::Role::CardText},
                                                          {theme::Role::OutlinerBackground, theme::Role::OutlinerText},
                                                          {theme::Role::InspectorBackground, theme::Role::InspectorText},
                                                          {theme::Role::SynopsisPaper, theme::Role::SynopsisText},
                                                          {theme::Role::CompositionPaper, theme::Role::CompositionText},
                                                          {theme::Role::HeaderBar, theme::Role::HeaderBarText}};
    QStringList rows = {QStringLiteral("theme\tarea\tcontrast")};
    int low = 0;
    for (const QString &t : {QStringLiteral("Light"), QStringLiteral("Dark Mode"), QStringLiteral("Sepia"), QStringLiteral("Slate"), QStringLiteral("Midnight"), QStringLiteral("Ocean"),
                             QStringLiteral("High Contrast")}) {
        menuPath(w, QStringLiteral("Window > Themes > ") + t);
        settle(300);
        shot(w, QStringLiteral("legibility-") + QString(t).replace(QLatin1Char(' '), QLatin1Char('-')));
        for (const auto &pr : pairs) {
            const double c = contrast(theme::color(pr.first), theme::color(pr.second));
            QString area;
            for (const theme::RoleInfo &ri : theme::roles())
                if (ri.role == pr.second)
                    area = ri.label;
            rows << QStringLiteral("%1\t%2\t%3%4").arg(t, area).arg(c, 0, 'f', 2).arg(c < 4.5 ? QStringLiteral(" (below 4.5)") : QString());
            if (c < 4.5)
                ++low;
        }
    }
    writeTable(QStringLiteral("u12-contrast.tsv"), rows);
    fact(QStringLiteral("U-12 areas below 4.5 : 1"), low);
    menuPath(w, QStringLiteral("Window > Themes > Light"));
    finishProject(w);
    delete w;
}

// ================================================================ U-13 dictation: from the first wish to text
void caseU13()
{
    // 0.64.1: the local test server is not the official source; its models are read after a yes (test switch)
    qputenv("QB_SPEECH_ACCEPT_CHANGED", "1");
    QObject *tool = speechToolObject();
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    QStringList places;
    for (const Entry &e : menuEntries(w))
        if (e.path.contains(QLatin1String("Speech"), Qt::CaseInsensitive) || e.path.contains(QLatin1String("Dictat"), Qt::CaseInsensitive) || e.path.contains(QLatin1String("Record"), Qt::CaseInsensitive))
            places << e.path;
    for (QToolButton *b : w->findChildren<QToolButton *>())
        if (b->isVisible() && (b->objectName().contains(QLatin1String("speech"), Qt::CaseInsensitive) || b->toolTip().contains(QLatin1String("Speech"))))
            places << QStringLiteral("toolbar: ") + b->toolTip();
    fact(QStringLiteral("U-13 where dictation is found"), places.join(QLatin1String("; ")));
    if (tool) {
        bool started = false;
        QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, started), Q_ARG(QString, QString()));
        QString err;
        QMetaObject::invokeMethod(tool, "lastError", Qt::DirectConnection, Q_RETURN_ARG(QString, err));
        fact(QStringLiteral("U-13 the button with no model"), started ? QStringLiteral("started") : err);
    } else {
        note(QStringLiteral("[S] no speech tool: only the places are recorded"));
    }
    finishProject(w);
    delete w;
}

// ================================================================ U-14 which settings am I changing?
void caseU14()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    QStringList rows = {QStringLiteral("scope\tpage\tscope visible")};
    for (int scope = 0; scope < 2; ++scope) {
        withModal<OptionsDialog>(400, [&rows, scope](OptionsDialog *d) {
            auto *sc = d->findChild<QComboBox *>(QStringLiteral("optScope"));
            if (sc && sc->count() > scope)
                sc->setCurrentIndex(scope);
            for (QWidget *pages : d->findChildren<QWidget *>(QStringLiteral("optionPages")))
                if (auto *lv = qobject_cast<QAbstractItemView *>(pages)) {
                    for (int r = 0; r < lv->model()->rowCount(); ++r) {
                        lv->setCurrentIndex(lv->model()->index(r, 0));
                        settle(60);
                        const bool visible = sc && sc->isVisible() && sc->visibleRegion().boundingRect().height() > 0;
                        rows << QStringLiteral("%1\t%2\t%3").arg(sc ? sc->currentText() : QStringLiteral("?"), lv->model()->index(r, 0).data().toString(), visible ? QStringLiteral("yes") : QStringLiteral("no"));
                        shot(d, QStringLiteral("options-scope%1-page%2").arg(scope).arg(r));
                    }
                }
            d->reject();
        });
        chooseByMenu(w, QStringLiteral("options"));
        settle(2000);
    }
    writeTable(QStringLiteral("u14-scope.tsv"), rows);
    finishProject(w);
    delete w;
}

// ================================================================ U-15 tables and pictures: steps and places
void caseU15()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    QStringList rows = {QStringLiteral("command\tshortcut\tenabled outside a table\tenabled inside")};
    TextEditor *e = showDoc(w, textItems(p).value(2));
    const QStringList cmds = {QStringLiteral("insertTable"), QStringLiteral("tableRowAbove"), QStringLiteral("tableRowBelow"), QStringLiteral("tableColLeft"), QStringLiteral("tableColRight"),
                              QStringLiteral("tableDeleteRows"), QStringLiteral("tableDeleteCols"), QStringLiteral("tableDelete"), QStringLiteral("tableMerge"), QStringLiteral("tableSplit"),
                              QStringLiteral("tableCellColor"), QStringLiteral("tableProperties"), QStringLiteral("picture")};
    QMap<QString, bool> outside;
    w->updateFormatBar();
    for (const QString &c : cmds)
        outside.insert(c, action(w, c) && action(w, c)->isEnabled());
    QStringList inside;
    if (e) {
        e->insertTableAtCursor(4, 3, tables::preset(QStringLiteral("header")));
        settle(200);
        w->updateFormatBar();
        inside = contextEntries(e->viewport(), e->cursorRect().center());
    }
    for (const QString &c : cmds) {
        QAction *a = action(w, c);
        rows << QStringLiteral("%1\t%2\t%3\t%4").arg(c, a ? a->shortcut().toString() : QStringLiteral("?"), outside.value(c) ? QStringLiteral("yes") : QStringLiteral("no"),
                                                     a && a->isEnabled() ? QStringLiteral("yes") : QStringLiteral("no"));
    }
    writeTable(QStringLiteral("u15-tables.tsv"), rows);
    fact(QStringLiteral("U-15 the right-click menu inside a table"), inside.join(QLatin1String(", ")));
    finishProject(w);
    delete w;
}

// ================================================================ U-16 names for screen readers and the focus
void caseU16()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    QStringList unnamed;
    int buttons = 0;
    for (QAbstractButton *b : w->findChildren<QAbstractButton *>()) {
        if (!b->isVisible())
            continue;
        ++buttons;
        QAccessibleInterface *ai = QAccessible::queryAccessibleInterface(b);
        const QString name = ai ? ai->text(QAccessible::Name) : QString();
        if (name.trimmed().isEmpty() && b->toolTip().isEmpty())
            unnamed << (b->objectName().isEmpty() ? QString::fromLatin1(b->metaObject()->className()) : b->objectName());
    }
    writeTable(QStringLiteral("u16-unnamed.txt"), unnamed);
    fact(QStringLiteral("U-16 visible buttons / without a name or tool tip"), QStringLiteral("%1 / %2").arg(buttons).arg(unnamed.size()));
    for (const QString &a : {QStringLiteral("options"), QStringLiteral("projectSettings"), QStringLiteral("compile")}) {
        withModal<QDialog>(400, [a](QDialog *d) {
            for (int i = 0; i < 4; ++i) {
                QTest::keyClick(QApplication::focusWidget() ? QApplication::focusWidget() : d, Qt::Key_Tab);
                shot(d, QStringLiteral("focus-%1-%2").arg(a).arg(i));
            }
            d->reject();
        });
        chooseByMenu(w, a);
        settle(1500);
    }
    finishProject(w);
    delete w;
}

// ================================================================ U-17 undo and redo follow the writer
void caseU17()
{
    const QString sw14 = QFileInfo(ctx().dir).absolutePath() + QStringLiteral("/S-W14/facts.json");
    const QJsonObject facts = QJsonDocument::fromJson(readText(sw14).toUtf8()).object();
    fact(QStringLiteral("U-17 from S-W14"), facts.value(QStringLiteral("S-W14 the toolbar's Undo and Redo per place (U-17)")).toString(QStringLiteral("(S-W14 did not run)")));
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    QStringList rows;
    QAction *undo = action(w, QStringLiteral("undo"));
    selectItems(w, {textItems(p).value(4)});
    chooseByMenu(w, QStringLiteral("moveDown"));
    rows << QStringLiteral("a binder move: Undo %1 (%2)").arg(undo && undo->isEnabled() ? QStringLiteral("enabled") : QStringLiteral("disabled"), undo ? undo->text() : QString());
    selectItems(w, {textItems(p).value(5)});
    chooseByMenu(w, QStringLiteral("trash"));
    rows << QStringLiteral("Move to Trash: Undo %1 (%2)").arg(undo && undo->isEnabled() ? QStringLiteral("enabled") : QStringLiteral("disabled"), undo ? undo->text() : QString());
    fact(QStringLiteral("U-17 undo of binder commands"), rows.join(QLatin1String("; ")));
    finishProject(w);
    delete w;
}

// ================================================================ U-18 the messages of a synced folder
void caseU18()
{
    const QString path = fixtureCopy(QStringLiteral("fx1-qb"));
    const QString lock = QFileInfo(path).absolutePath() + QStringLiteral("/settings/lock");
    const QDateTime now = QDateTime::currentDateTimeUtc();
    writeText(lock, QStringLiteral("laptop 1234\ndevice=laptop\nuser=anna\ninstance=other\nprogram=Quillbinder\nopened=%1\nalive=%1\n").arg(now.toString(Qt::ISODate)).toUtf8());
    QStringList rows;
    auto *w = new MainWindow;
    w->resize(1366, 768);
    w->show();
    settle(300);
    withModal<QMessageBox>(500, [&rows](QMessageBox *b) {
        QStringList buttons;
        for (QAbstractButton *x : b->buttons())
            buttons << x->text().remove(QLatin1Char('&')) + (b->defaultButton() == x ? QStringLiteral(" (default)") : QString());
        rows << QStringLiteral("%1\t%2\t%3\t%4x%5").arg(b->objectName(), b->text().simplified(), buttons.join(QLatin1String(", "))).arg(b->width()).arg(b->height());
        shot(b, QStringLiteral("lock-question"));
        b->reject();
    });
    w->openProject(path);
    settle(1200);
    writeTable(QStringLiteral("u18-messages.tsv"), rows);
    fact(QStringLiteral("U-18 messages read"), int(rows.size()));
    if (w->project())
        w->closeProject();
    delete w;
}

// ================================================================ U-19 Record only: from the walk to text in the scene
void caseU19()
{
    QObject *tool = speechToolObject();
    if (!tool) {
        note(QStringLiteral("[S] no speech tool"));
        return;
    }
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    QStringList steps;
    steps << QStringLiteral("File > Options > Speech: Record only");
    steps << QStringLiteral("the microphone button: start, stop");
    steps << QStringLiteral("the inspector's Transcriptions tab: the waiting recordings");
    steps << QStringLiteral("Transcribe All");
    steps << QStringLiteral("the text into the scene (copy, paste)");
    w->toolShowInspectorTab(QStringLiteral("speech-transcription"));
    settle(400);
    shot(w, QStringLiteral("transcriptions-tab"));
    fact(QStringLiteral("U-19 steps"), steps.join(QLatin1String(" -> ")));
    finishProject(w);
    delete w;
}

} // namespace cs
