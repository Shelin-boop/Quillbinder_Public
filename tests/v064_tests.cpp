// Scripted run for version 0.64:
//  - the spelling menu: a right-click anywhere on an underlined word gives that word's menu
//    (words with a straight apostrophe, "Kayel's" and "D'Nersun", found another word before);
//  - the autosave while typing goes on: the first short gap after 5 s saves, at the latest 10 s (30 s before 0.81.0);
//  - the corkboard's card size: a change by keys is kept when the settings are read again;
//  - a project in a synced folder: the lock names the computer and is renewed, another computer's
//    lock is shown before opening, a lock taken over while open is noticed, the other version of a
//    file changed elsewhere is kept (a snapshot and a copy; also the project file and in Scrivener
//    format), opening and closing alone change no file, texts whose files have not arrived are
//    read-only and never written.
// Saves screenshots into the folder given as first argument. Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_v064_tests /tmp/shots
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "core/Snapshots.h"
#include "core/SyncSafety.h"
#include "compile/PluginManager.h"
#include "import/ScrivenerFormat.h"
#include "text/DisplayFormat.h"
#include "ui/AppSettings.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/SpellChecker.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include "AppInfo.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QUuid>

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

static void info(const QString &what)
{
    std::printf("      %s\n", qPrintable(what));
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

// answers the next modal QMessageBox
static void answerBox(int delayMs, std::function<void(QMessageBox *)> fn)
{
    QTimer::singleShot(delayMs, qApp, [fn] {
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *d = qobject_cast<QMessageBox *>(tl))
                if (d->isVisible() && d->isModal()) {
                    fn(d);
                    return;
                }
        std::printf("      (no question came)\n");
    });
}

static QMessageBox *visibleBox(const QString &name)
{
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (auto *b = qobject_cast<QMessageBox *>(tl))
            if (b->isVisible() && b->objectName() == name)
                return b;
    return nullptr;
}

static void closePopups()
{
    while (QWidget *p = QApplication::activePopupWidget()) {
        p->close();
        settle(40);
    }
}

// the right-click menu at a position of the text, as the desktop sends the click
static QMenu *contextMenuAt(TextEditor *e, int position)
{
    closePopups();
    QTextCursor c(e->document());
    c.setPosition(position);
    const QRect r = e->cursorRect(c);
    const QPoint pos(r.left() + 1, r.center().y());
    QTest::mousePress(e->viewport(), Qt::RightButton, Qt::NoModifier, pos);
    QContextMenuEvent ce(QContextMenuEvent::Mouse, pos, e->viewport()->mapToGlobal(pos), Qt::NoModifier);
    QApplication::sendEvent(e->viewport(), &ce);
    settle(60);
    QTest::mouseRelease(e->viewport(), Qt::RightButton, Qt::NoModifier, pos);
    for (int i = 0; i < 30 && !QApplication::activePopupWidget(); ++i)
        settle(30);
    return qobject_cast<QMenu *>(QApplication::activePopupWidget());
}

// the word of the spelling menu ("Add "<word>" to Dictionary"), "" for the long menu
static QString spellWordOf(QMenu *m)
{
    if (!m)
        return QStringLiteral("(no menu)");
    for (QAction *a : m->actions()) {
        const QString t = a->text();
        if (t.startsWith(QLatin1String("Add \"")) && t.endsWith(QLatin1String("\" to Dictionary")))
            return t.mid(5, t.size() - 5 - 15);
    }
    return QString();
}

static QByteArray readAll(const QString &f)
{
    QFile file(f);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

static void writeAll(const QString &f, const QByteArray &data)
{
    QFile file(f);
    if (file.open(QIODevice::WriteOnly))
        file.write(data);
}

// the files below a folder with their hash and time (to see what a step changed)
static QMap<QString, QString> filesIn(const QString &dir)
{
    QMap<QString, QString> out;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QFileInfo fi(f);
        out.insert(QDir(dir).relativeFilePath(f),
                   QString::fromLatin1(QCryptographicHash::hash(readAll(f), QCryptographicHash::Sha1).toHex()) + QLatin1Char(' ')
                       + QString::number(fi.lastModified().toMSecsSinceEpoch()));
    }
    return out;
}

static QStringList differences(const QMap<QString, QString> &a, const QMap<QString, QString> &b, const std::function<bool(const QString &)> &ignore)
{
    QStringList out;
    QSet<QString> keys;
    for (auto it = a.cbegin(); it != a.cend(); ++it)
        keys.insert(it.key());
    for (auto it = b.cbegin(); it != b.cend(); ++it)
        keys.insert(it.key());
    for (const QString &k : keys)
        if (!ignore(k) && a.value(k) != b.value(k))
            out << k + (a.contains(k) ? (b.contains(k) ? QStringLiteral(" (changed)") : QStringLiteral(" (removed)")) : QStringLiteral(" (new)"));
    out.sort();
    return out;
}

// a lock of another computer, as this program writes it there
static void foreignLock(const QString &file, const QString &device, const QDateTime &alive)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    writeAll(file, QStringLiteral("%1 4242\ndevice=%1\nuser=ann\ninstance=%2\nprogram=Quillbinder%200.64.0\nopened=%3\nalive=%4\n")
                       .arg(device, QUuid::createUuid().toString(QUuid::WithoutBraces),
                            alive.addSecs(-3600).toUTC().toString(Qt::ISODate), alive.toUTC().toString(Qt::ISODate))
                       .toUtf8());
}

static BinderItem *textDoc(Project *p, BinderItem *parent, const QString &title, const QString &text)
{
    BinderItem *it = p->model()->createItem(parent, -1, ItemType::Text, title);
    QTextCursor c(p->docs()->text(it->id));
    c.insertText(text);
    return it;
}

static EditorPane *firstPane(MainWindow &w)
{
    for (EditorPane *ep : w.findChildren<EditorPane *>())
        if (ep->isVisible())
            return ep;
    return nullptr;
}

// types a letter every intervalMs for totalMs (a pause of gapMs at gapAtMs, if > 0); returns the
// times (ms from the start) at which an autosave happened
static QList<qint64> typeAndWatch(Project *p, QTextDocument *doc, int totalMs, int intervalMs, int gapAtMs = -1, int gapMs = 0)
{
    QList<qint64> saves;
    int count = p->autosaveCount();
    QElapsedTimer t;
    t.start();
    QTextCursor c(doc);
    c.movePosition(QTextCursor::End);
    bool gapDone = false;
    while (t.elapsed() < totalMs) {
        if (gapAtMs >= 0 && !gapDone && t.elapsed() >= gapAtMs) {
            gapDone = true;
            const qint64 until = t.elapsed() + gapMs;
            while (t.elapsed() < until) {
                QTest::qWait(10);
                if (p->autosaveCount() != count) {
                    count = p->autosaveCount();
                    saves << t.elapsed();
                }
            }
        }
        c.insertText(QStringLiteral("a"));
        const qint64 next = t.elapsed() + intervalMs;
        while (t.elapsed() < next) {
            QTest::qWait(5);
            if (p->autosaveCount() != count) {
                count = p->autosaveCount();
                saves << t.elapsed();
            }
        }
    }
    return saves;
}

static QString joinMs(const QList<qint64> &l)
{
    QStringList s;
    for (qint64 v : l)
        s << QString::number(v);
    return s.isEmpty() ? QStringLiteral("none") : s.join(QLatin1String(", "));
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV064Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV064Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);

    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.64.0")) >= 0,
          QStringLiteral("version 0.64.0 or newer"), QStringLiteral(QB_APP_VERSION_SHOWN));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.spellCheck = true;
    as.backupOnClose = false;
    as.backupOnOpen = false;
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();
    templates::Info blank;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("blank"))
            blank = t;

    // ================================================================ the word of the spelling check
    {
        bool all = true;
        QString bad;
        const QString par = QStringLiteral("Luna tried to answer Kayel's question.");
        const int k = int(par.indexOf(QLatin1String("Kayel")));
        for (int pos = k; pos <= k + 7; ++pos) {
            int start = -1;
            const QString w = SpellChecker::checkedWordAt(par, pos, &start);
            if (w != QLatin1String("Kayel's") || start != k) {
                all = false;
                bad += QStringLiteral("%1:%2 ").arg(pos).arg(w);
            }
        }
        check(all, QStringLiteral("the checked word at every place of \"Kayel's\" (also just after its last letter) is Kayel's"), bad);
        const QString d = QStringLiteral("What about D'Nersun? My home!");
        const int at = int(d.indexOf(QLatin1String("D'N")));
        all = true;
        bad.clear();
        for (int pos = at; pos <= at + 8; ++pos)
            if (SpellChecker::checkedWordAt(d, pos, nullptr) != QLatin1String("D'Nersun")) {
                all = false;
                bad += QString::number(pos) + QLatin1Char(' ');
            }
        check(all && SpellChecker::checkedWordAt(QStringLiteral("Route66 north"), 2, nullptr).isEmpty()
                  && SpellChecker::checkedWordAt(QStringLiteral("the dogs' bone"), 8, nullptr) == QLatin1String("dogs"),
              QStringLiteral("\"D'Nersun\" is one word everywhere; words with digits are not checked; an apostrophe at the end is "
                             "not part of the word"),
              bad);
    }

    // ================================================================ a project in the window
    QTemporaryDir work;
    QString err;
    auto proj = templates::createProject(blank, work.path(), QStringLiteral("Aspects"), &err);
    check(proj != nullptr, QStringLiteral("(project made)"), err);
    if (!proj)
        return 1;
    Project *p = proj.get();
    const QString projFolder = p->folderPath();
    const QString projFile = p->indexPath();
    BinderItem *scene = textDoc(p, p->draft(), QStringLiteral("Luna"),
                                QStringLiteral("Luna tried to answer Kayel's question. Kayel’s too. What about D'Nersun? My home!"));
    BinderItem *second = textDoc(p, p->draft(), QStringLiteral("Jinx"), QStringLiteral("Jinx explained the birth of the universe."));
    BinderItem *third = textDoc(p, p->draft(), QStringLiteral("Joey"), QStringLiteral("Joey was thinking out loud."));
    const QString sceneId = scene->id, secondId = second->id; // the items go with the project when it closes
    p->save(&err);

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1400, 950);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(proj)), QStringLiteral("(project open)"));
    settle(800);
    EditorPane *pane = firstPane(w);

    // ================================================================ spelling menu
    SpellChecker &sc = SpellChecker::instance();
    if (!sc.isAvailable() && !sc.setLanguage(QStringLiteral("en_US"))) {
        info(QStringLiteral("(no dictionary installed: the spelling menu is not checked)"));
    } else if (pane) {
        SpellChecker::setEnabled(true);
        pane->showItems({scene}, false, int(ViewMode::Single));
        settle(500);
        TextEditor *e = pane->currentEditor();
        const QString text = e ? e->document()->toPlainText() : QString();
        auto everyPlace = [&](const QString &word, int from) {
            QStringList wrong;
            for (int pos = from; pos <= from + word.size(); ++pos) {
                const QString got = spellWordOf(contextMenuAt(e, pos));
                if (got != word)
                    wrong << QStringLiteral("%1:\"%2\"").arg(pos - from).arg(got);
                closePopups();
            }
            return wrong;
        };
        if (e) {
            const QStringList straight = everyPlace(QStringLiteral("Kayel's"), int(text.indexOf(QLatin1String("Kayel's"))));
            check(straight.isEmpty(),
                  QStringLiteral("right-click anywhere on \"Kayel's\" (straight apostrophe), also on its last letters: its spelling "
                                 "menu (the reported problem)"),
                  straight.join(QLatin1Char(' ')));
            const QStringList curly = everyPlace(QStringLiteral("Kayel’s"), int(text.indexOf(QStringLiteral("Kayel’s"))));
            const QStringList dn = everyPlace(QStringLiteral("D'Nersun"), int(text.indexOf(QLatin1String("D'Nersun"))));
            check(curly.isEmpty() && dn.isEmpty(),
                  QStringLiteral("... the same with a typographic apostrophe, and \"D'Nersun\" is offered as a whole, as it is "
                                 "underlined (before: \"Nersun\", or no menu on the D)"),
                  curly.join(QLatin1Char(' ')) + QStringLiteral(" | ") + dn.join(QLatin1Char(' ')));
            QMenu *ok = contextMenuAt(e, int(text.indexOf(QLatin1String("question"))) + 3);
            check(ok && spellWordOf(ok).isEmpty() && ok->findChild<QMenu *>(QStringLiteral("spellingMore")) == nullptr,
                  QStringLiteral("on a correct word: the long menu as before"));
            closePopups();
            // a suggestion replaces the whole word
            QMenu *m = contextMenuAt(e, int(text.indexOf(QLatin1String("Kayel's"))) + 6);
            shot(m, QStringLiteral("01-spelling-menu"));
            QAction *first = nullptr;
            if (m)
                for (QAction *a : m->actions())
                    if (a->font().bold() && a->isEnabled()) {
                        first = a;
                        break;
                    }
            const QString sug = first ? first->text() : QString();
            if (first)
                first->trigger();
            closePopups();
            settle(100);
            const QString after = e->document()->toPlainText();
            check(first && after.contains(sug + QStringLiteral(" question")) && !after.contains(QLatin1String("Kayel's")),
                  QStringLiteral("a suggestion replaces the whole word"), sug + QStringLiteral(" | ") + after);
        }
        SpellChecker::setEnabled(false);
    }

    // ================================================================ corkboard card size
    if (pane) {
        pane->showItems({p->draft()}, false, int(ViewMode::Corkboard));
        settle(400);
        auto *slider = pane->findChild<QSlider *>(QStringLiteral("cardSizeSlider"));
        if (slider) {
            const int before = slider->value();
            slider->setFocus();
            QTest::keyClick(slider, Qt::Key_PageUp);
            QTest::keyClick(slider, Qt::Key_Right);
            settle(100);
            const int chosen = slider->value();
            // what the settings layer does when a project opens: read the settings again
            AppSettings::instance().load();
            emit AppSettings::instance().changed();
            settle(100);
            check(chosen != before && AppSettings::instance().cardWidth == chosen && slider->value() == chosen,
                  QStringLiteral("the card size changed with the keys is kept when the settings are read again (the reported "
                                 "problem: only a drag with the mouse was saved)"),
                  QStringLiteral("before %1, chosen %2, after reading %3").arg(before).arg(chosen).arg(AppSettings::instance().cardWidth));
            AppSettings::instance().cardWidth = 300;
            AppSettings::instance().notifyChanged();
            settle(100);
            check(slider->value() == 300, QStringLiteral("... and the slider follows a size set elsewhere (View > Card Width)"),
                  QString::number(slider->value()));
            shot(&w, QStringLiteral("02-corkboard"));
        } else {
            check(false, QStringLiteral("(the card size slider is there)"));
        }
        pane->showItems({second}, false, int(ViewMode::Single));
        settle(300);
    }

    // ================================================================ autosave while typing on
    {
        QTextDocument *doc = p->docs()->text(third->id);
        p->save(&err);
        settle(300);
        const QList<qint64> a = typeAndWatch(p, doc, 7600, 150, 6000, 450);
        check(!a.isEmpty() && a.first() >= 6000 && a.first() <= 6700,
              QStringLiteral("typing on for 6 s, then a short pause (0.45 s): saved in that pause, not before"),
              QStringLiteral("saves at %1 ms").arg(joinMs(a)));
        settle(4000); // the pause save of what was typed after (the counts 0.6 s after the last key restart its wait)
        const QList<qint64> b = typeAndWatch(p, doc, 4600, 150, 2000, 450);
        check(b.isEmpty(), QStringLiteral("a short pause before 5 s of typing does not save (the 2 s pause saves as before)"),
              QStringLiteral("saves at %1 ms").arg(joinMs(b)));
        settle(4000);
        // 0.81.0 (audit of 0.80.0, S-04): 10 s at the latest (30 s before)
        const QList<qint64> c = typeAndWatch(p, doc, 11500, 150);
        check(c.size() == 1 && c.first() >= 9700 && c.first() <= 11000,
              QStringLiteral("typing without any pause: saved after 10 s at the latest"), QStringLiteral("saves at %1 ms").arg(joinMs(c)));
        settle(2500);
    }

    // ================================================================ the lock
    const QString lockFile = projFolder + QStringLiteral("/settings/lock");
    {
        const syncsafe::LockInfo mine = syncsafe::readLock(lockFile);
        const QString raw = QString::fromUtf8(readAll(lockFile));
        check(mine.exists && mine.device == syncsafe::deviceName() && mine.instance == syncsafe::instanceId()
                  && mine.pid == QCoreApplication::applicationPid() && mine.alive.secsTo(QDateTime::currentDateTimeUtc()) < 600
                  && raw.split(QLatin1Char(' ')).size() == 2,
              QStringLiteral("the lock in the project names this computer, the user, the program, since when and its last "
                             "renewal; its first line is still \"<computer> <process>\" for older versions"),
              raw);
        check(syncsafe::judge(mine, 900, [](qint64) { return true; }) == syncsafe::LockState::Ours,
              QStringLiteral("... this program sees it as its own"));
        syncsafe::LockInfo other = mine;
        other.device = QStringLiteral("laptop");
        other.instance = QStringLiteral("x");
        other.key = QStringLiteral("0123456789abcdef"); // 0.64.1: another computer writes its own key
        other.alive = QDateTime::currentDateTimeUtc().addSecs(-60);
        const auto fresh = syncsafe::judge(other, 900, nullptr);
        other.alive = QDateTime::currentDateTimeUtc().addSecs(-3600);
        const auto stale = syncsafe::judge(other, 900, nullptr);
        check(fresh == syncsafe::LockState::OtherDevice && stale == syncsafe::LockState::OtherDeviceStale,
              QStringLiteral("another computer's lock renewed a minute ago counts as open there; one not renewed for an hour "
                             "as left over"));
        // another computer opens the project while it is open here
        foreignLock(lockFile, QStringLiteral("laptop"), QDateTime::currentDateTimeUtc());
        p->save(&err); // every save looks at the lock
        settle(300);
        QMessageBox *taken = visibleBox(QStringLiteral("lockTakenNotice"));
        check(taken && taken->text().contains(QLatin1String("laptop")),
              QStringLiteral("the project opened on another computer while open here: a notice names it and offers to close it here"),
              taken ? taken->text() : QString());
        shot(taken, QStringLiteral("03-lock-taken"));
        if (taken)
            taken->reject();
        settle(200);
    }

    // ================================================================ conflicts: the other version is kept
    {
        // a text changed here and, through a sync program, on another computer
        QTextDocument *d2 = p->docs()->text(second->id);
        QTextCursor c(d2);
        c.movePosition(QTextCursor::End);
        c.insertText(QStringLiteral(" Written here."));
        const QString path = p->docs()->textPath(second->id);
        QTest::qWait(1100);
        writeAll(path, "Jinx explained it differently on the laptop.\n");
        p->save(&err);
        settle(400);
        const QList<Snapshot> snaps = snapshots::list(p, second->id);
        QTextDocument other;
        if (!snaps.isEmpty())
            snapshots::load(p, snaps.first(), &other);
        const QStringList kept = QDir(projFolder + QStringLiteral("/settings/conflicts")).entryList(QDir::Files);
        check(!snaps.isEmpty() && snaps.first().title.startsWith(QLatin1String("Other version"))
                  && other.toPlainText().contains(QLatin1String("on the laptop")),
              QStringLiteral("a text changed here and elsewhere: the other version becomes a snapshot of the document"),
              snaps.isEmpty() ? QStringLiteral("no snapshot") : snaps.first().title + QStringLiteral(" | ") + other.toPlainText());
        check(QString::fromUtf8(readAll(path)).contains(QLatin1String("Written here.")) && kept.size() == 1
                  && kept.first().startsWith(QLatin1String("Jinx text (other version")),
              QStringLiteral("... the text of this computer is saved, and a copy of the other one is in settings/conflicts"),
              kept.join(QLatin1String(", ")));
        QMessageBox *notice = visibleBox(QStringLiteral("conflictNotice"));
        check(notice && notice->text().contains(QLatin1String("Jinx")) && notice->text().contains(QLatin1String("snapshot")),
              QStringLiteral("... and the writer is told which document and where the other version is"),
              notice ? notice->text() : QString());
        shot(notice, QStringLiteral("04-conflict"));
        if (notice)
            notice->accept();
        // the same text saved again: no new conflict
        c.insertText(QStringLiteral(" More."));
        p->save(&err);
        settle(200);
        check(snapshots::list(p, second->id).size() == snaps.size() && !visibleBox(QStringLiteral("conflictNotice")),
              QStringLiteral("... the next save of that text is an ordinary one"));
        // the project file changed elsewhere
        QTest::qWait(1100);
        writeAll(projFile, readAll(projFile) + "\n<!-- written by another computer -->\n");
        third->title = QStringLiteral("Joey (renamed)");
        p->notifyItemChanged(third);
        p->save(&err);
        settle(300);
        const QStringList kept2 = QDir(projFolder + QStringLiteral("/settings/conflicts")).entryList({QStringLiteral("project file*")}, QDir::Files);
        check(kept2.size() == 1 && readAll(projFolder + QStringLiteral("/settings/conflicts/") + kept2.value(0)).contains("another computer")
                  && readAll(projFile).contains("Joey (renamed)"),
              QStringLiteral("the project file changed elsewhere: that version is kept in settings/conflicts before this one is saved"),
              kept2.join(QLatin1String(", ")));
        if (QMessageBox *n = visibleBox(QStringLiteral("conflictNotice")))
            n->accept();
    }

    // ================================================================ opening and closing alone change nothing
    {
        check(w.closeProject(), QStringLiteral("(closed)"));
        settle(300);
        check(QFileInfo::exists(lockFile) && syncsafe::readLock(lockFile).device == QLatin1String("laptop"),
              QStringLiteral("closing does not remove a lock another computer took over"));
        QFile::remove(lockFile);
        // once open and closed after the changes above (the spare copy of the project file follows them)
        w.openProject(projFile);
        settle(500);
        w.closeProject();
        settle(300);
        const QMap<QString, QString> before = filesIn(projFolder);
        QTest::qWait(1100);
        check(w.openProject(projFile), QStringLiteral("(opened again)"));
        settle(800);
        check(QFileInfo::exists(lockFile) && syncsafe::readLock(lockFile).instance == syncsafe::instanceId(),
              QStringLiteral("(the lock is ours while it is open)"));
        if (EditorPane *pn = firstPane(w)) {
            pn->showItems({w.project()->item(sceneId)}, false, int(ViewMode::Single));
            settle(300);
        }
        check(w.closeProject(), QStringLiteral("(closed again)"));
        settle(300);
        const QMap<QString, QString> after = filesIn(projFolder);
        const QStringList changed = differences(before, after, [](const QString &k) {
            return k.startsWith(QLatin1String("settings/ui-")); // this computer's own layout file
        });
        check(changed.isEmpty() && !QFileInfo::exists(lockFile),
              QStringLiteral("opening the project and closing it without a change: no file of the project changes (only this "
                             "computer's own layout file settings/ui-<computer>.json), and the lock is gone"),
              changed.join(QLatin1String(", ")));
        bool ownUi = false;
        for (const QString &k : after.keys())
            ownUi = ownUi || (k.startsWith(QLatin1String("settings/ui-")) && k.endsWith(QLatin1String(".json")));
        check(ownUi, QStringLiteral("... the layout is in the computer's own file"));
    }

    // ================================================================ another computer's lock before opening
    {
        foreignLock(lockFile, QStringLiteral("laptop"), QDateTime::currentDateTimeUtc().addSecs(-120));
        QString asked, name;
        answerBox(400, [&](QMessageBox *b) {
            asked = b->text();
            name = b->objectName();
            shot(b, QStringLiteral("05-lock-other-computer"));
            b->button(QMessageBox::Cancel)->click();
        });
        const bool opened = w.openProject(projFile);
        settle(200);
        check(!opened && name == QLatin1String("lockOtherDevice") && asked.contains(QLatin1String("laptop"))
                  && asked.contains(QLatin1String("2 minute")) && asked.contains(QLatin1String("ago")) && !w.project(),
              QStringLiteral("a project open on another computer: before opening, its name, since when and its last sign of "
                             "life; Cancel leaves it closed"),
              asked);
        foreignLock(lockFile, QStringLiteral("laptop"), QDateTime::currentDateTimeUtc().addSecs(-3 * 3600));
        name.clear();
        bool openDefault = false;
        answerBox(400, [&](QMessageBox *b) {
            name = b->objectName();
            openDefault = b->defaultButton() && b->defaultButton()->objectName() == QLatin1String("lockOpen");
            if (auto *o = b->findChild<QPushButton *>(QStringLiteral("lockOpen")))
                o->click();
        });
        const bool opened2 = w.openProject(projFile);
        settle(500);
        check(opened2 && name == QLatin1String("lockLeftOver") && openDefault
                  && syncsafe::readLock(lockFile).instance == syncsafe::instanceId(),
              QStringLiteral("a lock not renewed for hours: \"not closed there\", Open is the default; the lock is ours then"));
        w.closeProject();
        settle(300);
    }

    // ================================================================ texts that have not arrived yet
    {
        const QString jinx = projFolder + QStringLiteral("/content/") + secondId;
        QDir textDir(jinx);
        QStringList files = textDir.entryList({QStringLiteral("text.*")}, QDir::Files);
        for (const QString &f : files)
            QFile::remove(textDir.filePath(f));
        check(w.openProject(projFile), QStringLiteral("(opened with a text file missing)"));
        settle(600);
        QMessageBox *n = visibleBox(QStringLiteral("notArrivedNotice"));
        check(n && n->text().contains(QLatin1String("Jinx")) && n->text().contains(QLatin1String("sync program")),
              QStringLiteral("a document with text whose file is not there (yet): after opening, a notice names it"),
              n ? n->text() : QString());
        shot(n, QStringLiteral("06-not-arrived"));
        if (n)
            n->accept();
        EditorPane *pn = firstPane(w);
        TextEditor *e = nullptr;
        if (pn && w.project()) {
            pn->showItems({w.project()->item(secondId)}, false, int(ViewMode::Single));
            settle(400);
            e = pn->currentEditor();
        }
        if (e) {
            e->setFocus();
            QTest::keyClicks(e, QStringLiteral("typed"));
        }
        if (w.project())
            w.project()->save(&err);
        settle(200);
        check(e && e->isReadOnly() && e->document()->toPlainText().isEmpty() && textDir.entryList({QStringLiteral("text.*")}, QDir::Files).isEmpty(),
              QStringLiteral("... it is shown empty and cannot be edited, and nothing is written in its place"));
        w.closeProject();
        settle(300);
    }

    // ================================================================ Scrivener format
    {
        QTemporaryDir sw;
        auto sp = templates::createProject(blank, sw.path(), QStringLiteral("Synced"), &err);
        const QString alphaId = textDoc(sp.get(), sp->draft(), QStringLiteral("Alpha"), QStringLiteral("Alpha text."))->id;
        textDoc(sp.get(), sp->draft(), QStringLiteral("Beta"), QStringLiteral("Beta text."));
        sp->save(&err);
        const QString scriv = sw.path() + QStringLiteral("/Synced.scriv");
        auto wc = scrivener::convertToScrivener(sp.get(), scriv, &err);
        check(wc != nullptr, QStringLiteral("(saved in Scrivener format)"), err);
        wc.reset();
        sp.reset();
        const QString shared = scriv + QStringLiteral("/quillbinder.lock");
        // opened and closed once, then again: the second time nothing in the .scriv changes
        check(w.openScrivenerProject(scriv), QStringLiteral("(the .scriv opens)"));
        settle(600);
        check(syncsafe::readLock(shared).instance == syncsafe::instanceId(),
              QStringLiteral("a .scriv open here has a lock of this program in it (another computer sees it through the sync program)"));
        w.closeProject();
        settle(300);
        const QMap<QString, QString> before = filesIn(scriv);
        QTest::qWait(1100);
        w.openScrivenerProject(scriv);
        settle(600);
        w.closeProject();
        settle(300);
        const QStringList changed = differences(before, filesIn(scriv), [](const QString &) { return false; });
        check(changed.isEmpty() && !QFileInfo::exists(shared),
              QStringLiteral("Scrivener format: opening and closing without a change leave every file of the .scriv as it was "
                             "(before: the .scrivx and docs.checksum were written each time), the lock is gone"),
              changed.join(QLatin1String(", ")));
        // a text changed here and elsewhere
        w.openScrivenerProject(scriv);
        settle(600);
        Project *wp2 = w.project();
        QString uuid;
        {
            const QByteArray x = readAll(scrivener::scrivxIn(scriv));
            const int t = x.indexOf("<Title>Alpha</Title>");
            const int u = x.lastIndexOf("UUID=\"", t);
            if (t > 0 && u > 0)
                uuid = QString::fromUtf8(x.mid(u + 6, 36));
        }
        const QString rtf = scriv + QStringLiteral("/Files/Data/") + uuid + QStringLiteral("/content.rtf");
        if (wp2 && wp2->item(alphaId)) {
            QTextCursor c(wp2->docs()->text(alphaId));
            c.movePosition(QTextCursor::End);
            c.insertText(QStringLiteral(" Here."));
            wp2->save(&err);
            QTest::qWait(1100);
            writeAll(rtf, "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Times;}}\\f0 Alpha as the laptop wrote it.\\par}");
            QTextCursor c2(wp2->docs()->text(alphaId));
            c2.movePosition(QTextCursor::End);
            c2.insertText(QStringLiteral(" And more."));
            wp2->save(&err);
            settle(400);
            const QList<Snapshot> snaps = snapshots::list(wp2, alphaId);
            QTextDocument other;
            if (!snaps.isEmpty())
                snapshots::load(wp2, snaps.first(), &other);
            check(!snaps.isEmpty() && other.toPlainText().contains(QLatin1String("as the laptop wrote it"))
                      && QString::fromUtf8(readAll(rtf)).contains(QLatin1String("And more")),
                  QStringLiteral("Scrivener format: a text changed in the .scriv and here: the other version becomes a snapshot, "
                                 "this one is written"),
                  QString::fromUtf8(readAll(rtf)).left(300));
            if (QMessageBox *n = visibleBox(QStringLiteral("conflictNotice")))
                n->accept();
        } else {
            check(false, QStringLiteral("(the .scriv opened again)"), uuid);
        }
        w.closeProject();
        settle(300);
    }

    settle(200);
    std::printf("\n%d failure(s)\n", failures);
    delete wp;
    return failures;
}
