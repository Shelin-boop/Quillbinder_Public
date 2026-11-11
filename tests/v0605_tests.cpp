// Scripted run for version 0.60.5: speed with a large project in Scrivener format and the
// right-click menu of the text.
//
// A project of 500,000 words (150,000 in the draft, 350,000 in research, 1,000 documents with
// synopses and some notes) is made, saved in Scrivener format and worked on as a working copy:
// - an autosave after an edit stays short, also with the whole draft open (combined text), and
//   writes only what changed into the .scriv; the fingerprint stays right, so the next opening
//   does not read the .scriv again;
// - a full save (switching to another program, closing) still writes the search index and
//   everything else; a window of the program taking over (a dialog) saves quickly;
// - a file that "Scrivener" changed in the meantime is still left alone.
// The right-click menu of the text: Qt's own menu and tooltip effects are off (also when a
// desktop theme turns them on), the menu shows in a short time with its entries, keeps its size
// after it shows, and a second right-click on the same misspelled word does not search the
// dictionary again.
//
// Times are printed; the limits are generous (a slow test machine has two cores) and catch a
// return of the old behavior, which took far longer. Exit code = number of failed checks.
//
//   xvfb-run -a ./qb_v0605_tests /tmp/shots

#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "import/ScrivenerFormat.h"
#include "ui/AppSettings.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/SpellChecker.h"
#include "ui/TextEditor.h"
#include "AppInfo.h"
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <algorithm>
#include <cstdio>

using namespace qb;

static int failures = 0;
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

static QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static void writeAll(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(data);
}

static qint64 median(QList<qint64> v)
{
    std::sort(v.begin(), v.end());
    return v.isEmpty() ? -1 : v.at(v.size() / 2);
}

// words that read like text, the same every run
static QString words(quint32 seed, int count)
{
    static const char *vocab[] = {"the", "harbor", "light", "she", "walked", "along", "quiet", "stones", "and", "rain",
                                  "fell", "on", "old", "roofs", "while", "ships", "waited", "for", "morning", "tide",
                                  "keeper", "counted", "lanterns", "in", "a", "small", "room", "above", "water", "wind",
                                  "carried", "voices", "from", "market", "where", "fishermen", "sold", "silver", "herring",
                                  "her", "brother", "never", "returned", "after", "storm", "that", "winter", "letters",
                                  "arrived", "late", "with", "salt", "stains", "across", "every", "page", "he", "wrote"};
    const int n = int(sizeof(vocab) / sizeof(vocab[0]));
    QString out;
    out.reserve(count * 7);
    quint32 x = seed * 2654435761u + 12345u;
    for (int i = 0; i < count; ++i) {
        x = x * 1664525u + 1013904223u;
        if (i > 0)
            out += (i % 100 == 0) ? QLatin1Char('\n') : QLatin1Char(' ');
        out += QLatin1String(vocab[(x >> 8) % n]);
        if (i % 12 == 11)
            out += QLatin1Char('.');
    }
    return out + QLatin1Char('.');
}

static void setText(Project *p, BinderItem *it, const QString &text)
{
    QTextCursor c(p->docs()->text(it->id));
    c.insertText(text);
}

static void appendWords(Project *p, const QString &id, const QString &text)
{
    QTextCursor c(p->docs()->text(id));
    c.movePosition(QTextCursor::End);
    c.insertText(text);
}

static QString uuidOf(const QString &scriv, const QString &title)
{
    QFile f(scrivener::scrivxIn(scriv));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const QString x = QString::fromUtf8(f.readAll());
    const int t = int(x.indexOf(QStringLiteral("<Title>") + title + QStringLiteral("</Title>")));
    if (t < 0)
        return QString();
    const int b = int(x.lastIndexOf(QStringLiteral("UUID=\""), t));
    return b < 0 ? QString() : x.mid(b + 6, 36);
}

static QString stateFingerprint(Project *p)
{
    const QJsonObject o = QJsonDocument::fromJson(readAll(scrivener::stateFile(p))).object();
    return o.value(QStringLiteral("fingerprint")).toString();
}

static QMenu *openPopup()
{
    return qobject_cast<QMenu *>(QApplication::activePopupWidget());
}

// "opens" a submenu as the writer does: its aboutToShow fills it
static QMenu *opened(QMenu *m)
{
    if (m)
        emit m->aboutToShow();
    return m;
}

static QMenu *subMenu(QMenu *m, const QString &name) { return m ? opened(m->findChild<QMenu *>(name)) : nullptr; }

struct MenuRun {
    qint64 ms = -1;
    QSize first, later;
    QStringList topEntries;
    QMenu *menu = nullptr;
};

static MenuRun rightClick(TextEditor *e, int position, bool keepOpen = false)
{
    MenuRun r;
    QTextCursor c(e->document());
    c.setPosition(position);
    e->setTextCursor(c);
    const QRect cr = e->cursorRect(c);
    const QPoint pos(cr.left() + 2, cr.center().y());
    QContextMenuEvent ce(QContextMenuEvent::Mouse, pos, e->viewport()->mapToGlobal(pos), Qt::NoModifier);
    QElapsedTimer t;
    t.start();
    QApplication::sendEvent(e->viewport(), &ce);
    QMenu *m = openPopup();
    while (!(m && m->isVisible()) && t.elapsed() < 1000) {
        QApplication::processEvents(QEventLoop::AllEvents, 5);
        m = openPopup();
    }
    if (m)
        QApplication::processEvents(); // the first paint belongs to what the writer waits for
    r.ms = t.elapsed();
    if (!m)
        return r;
    r.first = m->size();
    for (QAction *a : m->actions())
        r.topEntries << (a->isSeparator() ? QStringLiteral("|") : (a->menu() ? a->menu()->objectName() : a->text()));
    settle(250);
    r.later = m->size();
    r.menu = m;
    if (!keepOpen) {
        m->close();
        settle(60);
        r.menu = nullptr;
    }
    return r;
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV0605Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV0605Test"));
    QApplication app(argc, argv);
    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);

    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.60.5")) >= 0
              && QLatin1String(QB_APP_VERSION_SHOWN).endsWith(QLatin1String(" " QB_APP_VERSION_LABEL)),
          QStringLiteral("version 0.60.5 or newer, shown with its label"), QStringLiteral(QB_APP_VERSION_SHOWN));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    AppSettings::instance().load();

    // ================================================================ a project of 500,000 words
    QTemporaryDir work;
    QString err;
    const QString scriv = work.filePath(QStringLiteral("Harbor Book.scriv"));
    QStringList draftIds, researchIds;
    {
        QElapsedTimer t;
        t.start();
        auto p = Project::create(work.path(), QStringLiteral("Harbor Book"), &err);
        check(p != nullptr, QStringLiteral("(project made)"), err);
        if (!p)
            return failures;
        quint32 seed = 1;
        // the draft: 30 chapters of 10 scenes, 500 words each = 150,000 words
        for (int ch = 0; ch < 30; ++ch) {
            BinderItem *folder = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Chapter %1").arg(ch + 1));
            for (int s = 0; s < 10; ++s) {
                BinderItem *it = p->model()->createItem(folder, -1, ItemType::Text, QStringLiteral("Scene %1.%2").arg(ch + 1).arg(s + 1));
                setText(p.get(), it, words(seed++, 500));
                it->synopsis = words(seed++, 14);
                if (s % 5 == 0) {
                    QTextCursor n(p->docs()->notes(it->id));
                    n.insertText(words(seed++, 40));
                }
                draftIds << it->id;
            }
        }
        // research: 10 folders of 70 documents, 500 words each = 350,000 words
        for (int f = 0; f < 10; ++f) {
            BinderItem *folder = p->model()->createItem(p->research(), -1, ItemType::Folder, QStringLiteral("Notes %1").arg(f + 1));
            for (int d = 0; d < 70; ++d) {
                BinderItem *it = p->model()->createItem(folder, -1, ItemType::Text, QStringLiteral("Note %1.%2").arg(f + 1).arg(d + 1));
                setText(p.get(), it, words(seed++, 500));
                researchIds << it->id;
            }
        }
        p->docs()->recountAll();
        p->markIndexDirty();
        check(p->save(&err), QStringLiteral("(the large project is saved)"), err);
        int draftWords = 0;
        for (const QString &id : draftIds)
            draftWords += p->item(id)->wordCount;
        const int total = p->totalWordCount();
        std::printf("      %d words in all, %d in the draft, %d documents; made in %lld ms\n", total, draftWords,
                    int(draftIds.size() + researchIds.size()), t.elapsed());
        check(total >= 495000 && total <= 505000 && draftWords >= 148000 && draftWords <= 152000,
              QStringLiteral("(the project has about 500,000 words, 150,000 of them in the draft)"),
              QStringLiteral("%1 / %2").arg(total).arg(draftWords));
        t.restart();
        auto wc = scrivener::convertToScrivener(p.get(), scriv, &err);
        check(wc != nullptr, QStringLiteral("(saved in Scrivener format)"), err);
        std::printf("      saved in Scrivener format in %lld ms\n", t.elapsed());
    }

    // the working copy, opened as the program opens a .scriv
    scrivener::OpenResult opened1 = scrivener::open(scriv, &err);
    std::unique_ptr<Project> wc = std::move(opened1.project);
    check(wc != nullptr && !wc->scrivenerPath.isEmpty(), QStringLiteral("(the .scriv opens through its working copy)"), err);
    if (!wc)
        return failures;
    Project *p = wc.get();
    const QString edited = draftIds.at(41);
    const QString other = draftIds.at(42);
    const QString editedUuid = uuidOf(scriv, p->item(edited)->title);
    const QString otherUuid = uuidOf(scriv, p->item(other)->title);
    const QString dataDir = scriv + QLatin1String("/Files/Data/");
    check(!editedUuid.isEmpty() && QFile::exists(dataDir + editedUuid + QLatin1String("/content.rtf")),
          QStringLiteral("(the scene's file in the .scriv is found)"), editedUuid);

    // ================================================================ autosave after an edit
    const QByteArray otherBefore = readAll(dataDir + otherUuid + QLatin1String("/content.rtf"));
    const QByteArray indexBefore = readAll(scriv + QLatin1String("/Files/search.indexes"));
    QList<qint64> one;
    bool savedOk = true;
    for (int i = 0; i < 5; ++i) {
        appendWords(p, edited, QStringLiteral(" Gullwing%1 rose over the pier.").arg(i));
        QElapsedTimer t;
        t.start();
        savedOk = p->autosave(&err) && savedOk;
        one << t.elapsed();
    }
    const qint64 oneMs = median(one);
    std::printf("      autosave after an edit, one scene open: %lld ms (median of 5)\n", oneMs);
    check(savedOk, QStringLiteral("autosave with a .scriv: works"), err);
    const QByteArray editedNow = readAll(dataDir + editedUuid + QLatin1String("/content.rtf"));
    check(editedNow.contains("Gullwing4"), QStringLiteral("autosave: the edited scene reaches the .scriv at once"));
    check(readAll(dataDir + otherUuid + QLatin1String("/content.rtf")) == otherBefore,
          QStringLiteral("autosave: a scene nobody touched keeps its file byte for byte"));
    {
        const QByteArray cs = readAll(dataDir + QLatin1String("docs.checksum"));
        const QByteArray want = editedUuid.toLower().toUtf8() + "/content.rtf="
                                + QCryptographicHash::hash(editedNow, QCryptographicHash::Sha1).toHex();
        check(cs.contains(want), QStringLiteral("autosave: docs.checksum has the edited scene's new checksum"));
    }
    check(stateFingerprint(p) == scrivener::fingerprint(scriv),
          QStringLiteral("autosave: the fingerprint of the .scriv is kept right (the next opening does not read it again)"));
    check(oneMs >= 0 && oneMs < 250, QStringLiteral("autosave after an edit takes less than 250 ms"),
          QStringLiteral("%1 ms").arg(oneMs));

    // the whole draft open (combined text of the draft)
    for (const QString &id : draftIds)
        p->docs()->text(id);
    QList<qint64> all;
    for (int i = 0; i < 5; ++i) {
        appendWords(p, edited, QStringLiteral(" Herons%1 waited.").arg(i));
        QElapsedTimer t;
        t.start();
        p->autosave(&err);
        all << t.elapsed();
    }
    const qint64 allMs = median(all);
    std::printf("      autosave after an edit, whole draft open: %lld ms (median of 5)\n", allMs);
    check(allMs >= 0 && allMs < 300, QStringLiteral("autosave with the whole draft open takes less than 300 ms"),
          QStringLiteral("%1 ms").arg(allMs));

    // a full save (switching to another program, closing): the search index and the rest
    {
        QElapsedTimer t;
        t.start();
        check(p->save(&err), QStringLiteral("full save: works"), err);
        std::printf("      full save: %lld ms\n", t.elapsed());
        const QByteArray idx = readAll(scriv + QLatin1String("/Files/search.indexes"));
        check(idx != indexBefore && idx.contains("Herons4") && idx.contains("Gullwing0"),
              QStringLiteral("full save: the search index has the new words"));
        check(stateFingerprint(p) == scrivener::fingerprint(scriv), QStringLiteral("full save: the fingerprint is right"));
    }

    // "Scrivener" changed another scene meanwhile: an autosave leaves it alone
    {
        const QString theirPath = dataDir + otherUuid + QLatin1String("/content.rtf");
        QByteArray theirs = readAll(theirPath);
        const int end = int(theirs.lastIndexOf('}'));
        theirs.insert(end, QByteArray("\\par Changed in Scrivener."));
        writeAll(theirPath, theirs);
        appendWords(p, edited, QStringLiteral(" Terns."));
        p->autosave(&err);
        check(readAll(theirPath) == theirs, QStringLiteral("autosave: a file changed in Scrivener meanwhile stays as it is"));
        p->save(&err);
        check(readAll(theirPath) == theirs, QStringLiteral("full save: a file changed in Scrivener meanwhile stays as it is"));
    }

    // the next opening finds the .scriv as it left it (nothing read again), with the new words
    {
        p->save(&err);
        wc.reset();
        p = nullptr;
        QElapsedTimer t;
        t.start();
        scrivener::OpenResult r = scrivener::open(scriv, &err);
        std::printf("      opening the .scriv again: %lld ms (read again: %s)\n", t.elapsed(), r.reread ? "yes" : "no");
        check(r.project != nullptr, QStringLiteral("reopening: works"), err);
        if (r.project)
            check(r.project->docs()->plainText(edited).contains(QLatin1String("Terns")),
                  QStringLiteral("reopening: the last edit is there"));
        wc = std::move(r.project);
        p = wc.get();
    }

    // ================================================================ the right-click menu
    // a desktop theme that turns Qt's menu animation on
    for (Qt::UIEffect e : {Qt::UI_General, Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateCombo, Qt::UI_AnimateTooltip,
                           Qt::UI_FadeTooltip, Qt::UI_AnimateToolBox})
        QApplication::setEffectEnabled(e, true);
    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    auto effectsOff = [] {
        return !QApplication::isEffectEnabled(Qt::UI_AnimateMenu) && !QApplication::isEffectEnabled(Qt::UI_FadeMenu)
               && !QApplication::isEffectEnabled(Qt::UI_AnimateCombo) && !QApplication::isEffectEnabled(Qt::UI_AnimateTooltip)
               && !QApplication::isEffectEnabled(Qt::UI_FadeTooltip) && !QApplication::isEffectEnabled(Qt::UI_AnimateToolBox);
    };
    check(effectsOff(), QStringLiteral("Qt's menu, list and tooltip animations are off, even when the desktop turns them on"));
    w.move(0, 0);
    w.resize(1500, 900);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(wc)), QStringLiteral("(the large project is open in the window)"));
    p = w.project();
    settle(600);
    {
        for (Qt::UIEffect e : {Qt::UI_General, Qt::UI_AnimateMenu, Qt::UI_FadeMenu})
            QApplication::setEffectEnabled(e, true);
        QEvent tc(QEvent::ThemeChange);
        QApplication::sendEvent(&w, &tc);
        check(effectsOff(), QStringLiteral("the animations stay off when the desktop theme changes"));
    }

    // a dialog of the program takes over: the quick save; another program: the full save
    {
        const QString content = dataDir + editedUuid + QLatin1String("/content.rtf");
        const QString index = scriv + QLatin1String("/Files/search.indexes");
        auto stale = [p] { return QJsonDocument::fromJson(readAll(scrivener::stateFile(p))).object().value(QStringLiteral("indexStale")).toBool(); };
        appendWords(p, edited, QStringLiteral(" Cormorant."));
        QDialog d(&w);
        d.resize(300, 200);
        d.show();
        d.activateWindow();
        (void)QTest::qWaitForWindowActive(&d, 2000);
        settle(300);
        check(readAll(content).contains("Cormorant") && !readAll(index).contains("Cormorant") && stale(),
              QStringLiteral("a dialog of the program takes over: the edit reaches the .scriv quickly, the search index waits"));
        d.close();
        w.activateWindow();
        (void)QTest::qWaitForWindowActive(&w, 2000);
        settle(300);
        QApplication::setActiveWindow(nullptr); // as when another program takes over
        settle(300);
        check(readAll(index).contains("Cormorant") && !stale(),
              QStringLiteral("switching to another program: the full save writes the search index"));
        w.activateWindow();
        (void)QTest::qWaitForWindowActive(&w, 2000);
        settle(200);
    }

    EditorPane *pane = nullptr;
    for (EditorPane *ep : w.findChildren<EditorPane *>())
        if (ep->isVisible())
            pane = ep;
    BinderItem *scene = p ? p->item(edited) : nullptr;
    if (pane && scene)
        pane->showItems({scene}, false, int(ViewMode::Single));
    settle(400);
    TextEditor *e = pane ? pane->currentEditor() : nullptr;
    check(e != nullptr, QStringLiteral("(the scene is in the editor)"));
    if (!e) {
        delete wp;
        return failures;
    }
    SpellChecker &sc = SpellChecker::instance();
    if (!sc.isAvailable())
        sc.setLanguage(QStringLiteral("en_US"));
    SpellChecker::setEnabled(false);

    // the menu on a correct word, five times
    QList<qint64> menuTimes;
    MenuRun last;
    for (int i = 0; i < 5; ++i) {
        last = rightClick(e, 6);
        menuTimes << last.ms;
    }
    const qint64 menuMs = median(menuTimes);
    std::printf("      right-click menu: %lld ms until shown (median of 5)\n", menuMs);
    check(last.first.isValid() && last.first == last.later,
          QStringLiteral("the right-click menu keeps its size after it shows"),
          QStringLiteral("%1x%2 -> %3x%4").arg(last.first.width()).arg(last.first.height()).arg(last.later.width()).arg(last.later.height()));
    check(last.topEntries.contains(QStringLiteral("lookMenu")) && last.topEntries.contains(QStringLiteral("contextStyleMenu")),
          QStringLiteral("the right-click menu still has Style and Appearance"), last.topEntries.join(QLatin1Char(',')));
    check(menuMs >= 0 && menuMs < 60, QStringLiteral("the right-click menu shows in less than 60 ms"),
          QStringLiteral("%1 ms").arg(menuMs));

    // its submenus, with the entries they had
    {
        MenuRun r = rightClick(e, 6, true);
        QMenu *m = r.menu;
        QMenu *look = m ? m->findChild<QMenu *>(QStringLiteral("lookMenu")) : nullptr;
        QMenu *style = m ? m->findChild<QMenu *>(QStringLiteral("contextStyleMenu")) : nullptr;
        QMenu *paper = subMenu(opened(look), QStringLiteral("lookColor_editorPaper"));
        int colors = 0;
        if (paper)
            for (QAction *a : paper->actions())
                colors += a->data().value<QColor>().isValid() ? 1 : 0;
        check(paper && colors >= 10 && subMenu(look, QStringLiteral("lookColor_editorText"))
                  && look->findChild<QMenu *>(QStringLiteral("lookEditorWidth")),
              QStringLiteral("Appearance, opened: the color menus with their colors, the width"),
              QStringLiteral("%1 colors").arg(colors));
        opened(style);
        int styles = 0;
        for (QAction *a : style ? style->actions() : QList<QAction *>())
            styles += a->isSeparator() ? 0 : 1;
        check(styles >= 3, QStringLiteral("Style, opened: the style list"), QStringLiteral("%1 entries").arg(styles));
        if (m) {
            m->close();
            settle(60);
        }
    }

    // on a misspelled word: the short menu with suggestions
    if (sc.isAvailable()) {
        SpellChecker::setEnabled(true);
        QTextCursor c(e->document());
        c.insertText(QStringLiteral("Harbourr "));
        settle(200);
        QList<qint64> spellTimes;
        MenuRun r;
        for (int i = 0; i < 5; ++i) {
            r = rightClick(e, 3);
            spellTimes << r.ms;
        }
        const qint64 spellMs = median(spellTimes);
        std::printf("      right-click menu on a misspelled word: %lld ms the first time, %lld ms after (median of 5)\n",
                    spellTimes.first(), spellMs);
        check(r.topEntries.contains(QStringLiteral("spellingMore")), QStringLiteral("misspelled word: the short menu as before"),
              r.topEntries.join(QLatin1Char(',')));
        check(spellMs >= 0 && spellMs < 120, QStringLiteral("the menu on a misspelled word shows in less than 120 ms"),
              QStringLiteral("%1 ms").arg(spellMs));
        SpellChecker::setEnabled(false);
    } else {
        std::printf("      (no English dictionary: the menu on a misspelled word was not timed)\n");
    }

    w.grab().save(outDir + QLatin1String("/v0605-large-project.png"));
    delete wp;
    settle(100);
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
