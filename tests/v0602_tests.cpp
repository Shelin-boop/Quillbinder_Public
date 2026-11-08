// Scripted run for version 0.60.2: the right-click menu of the text offers the three ways to
// paste one below the other (Paste, Paste and Match Style, Paste with Formatting), with their
// shortcuts; they do what the Edit menu commands do; on a misspelled word they are under More
// Commands; with nothing to paste they are off. Edit > Paste and Match Style (now in the editor)
// still works. Saves screenshots into the folder given as first argument. Exit code = number of
// failed checks.
//
//   xvfb-run -a ./qb_v0602_tests /tmp/shots
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "compile/PluginManager.h"
#include "text/DisplayFormat.h"
#include "ui/AppSettings.h"
#include "ui/Commands.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/SpellChecker.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include "AppInfo.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QMenu>
#include <QMimeData>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <cstdio>

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

static void settle(int ms = 150)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }
}

static QAction *byKey(QWidget *w, const QString &key) { return w->findChild<QAction *>(key); }

static EditorPane *visiblePane(MainWindow &w)
{
    for (EditorPane *ep : w.findChildren<EditorPane *>())
        if (ep->isVisible())
            return ep;
    return nullptr;
}

static BinderItem *textDoc(Project *p, const QString &title, const QStringList &paragraphs)
{
    BinderItem *it = p->model()->createItem(p->draft(), -1, ItemType::Text, title);
    QTextCursor c(p->docs()->text(it->id));
    for (int i = 0; i < paragraphs.size(); ++i) {
        if (i > 0)
            c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
        c.insertText(paragraphs[i]);
    }
    return it;
}

static void closePopups()
{
    for (int i = 0; i < 5; ++i) {
        QWidget *p = QApplication::activePopupWidget();
        if (!p)
            break;
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
    const QPoint pos(r.left() + 2, r.center().y());
    QTest::mousePress(e->viewport(), Qt::RightButton, Qt::NoModifier, pos);
    QContextMenuEvent ce(QContextMenuEvent::Mouse, pos, e->viewport()->mapToGlobal(pos), Qt::NoModifier);
    QApplication::sendEvent(e->viewport(), &ce);
    settle(80);
    QTest::mouseRelease(e->viewport(), Qt::RightButton, Qt::NoModifier, pos);
    for (int i = 0; i < 30 && !QApplication::activePopupWidget(); ++i)
        settle(30);
    return qobject_cast<QMenu *>(QApplication::activePopupWidget());
}

// object names of the menu's entries (separators "|")
static QStringList names(QMenu *m)
{
    QStringList out;
    for (QAction *a : m->actions())
        out << (a->isSeparator() ? QStringLiteral("|") : a->objectName());
    return out;
}

static QAction *named(QMenu *m, const QString &name)
{
    for (QAction *a : m->actions())
        if (a->objectName() == name)
            return a;
    return nullptr;
}

static const char kRtf[] = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 DejaVu Serif;}}{\\colortbl;\\red200\\green0\\blue0;}\n"
                           "\\pard\\qc\\f0\\fs36\\cf1 Red serif words\\par\n}";
// The serif font of the pasted text. It must differ from the project's automatic font, which the
// program treats as "no formatting" (W3: the Windows runner has neither Noto Serif nor Liberation
// Serif, so DejaVu Serif is the automatic font there; FreeSerif is taken instead).
static QString g_serif = QStringLiteral("DejaVu Serif");
static void chooseSerif(const Project *p)
{
    if (display::resolvedFontFamily(p->formatting).compare(g_serif, Qt::CaseInsensitive) == 0)
        g_serif = QStringLiteral("FreeSerif");
}
static QByteArray rtfSample()
{
    return QByteArray(kRtf).replace("DejaVu Serif", g_serif.toLatin1());
}

static void putRtf()
{
    auto *md = new QMimeData;
    md->setData(QStringLiteral("text/rtf"), rtfSample());
    md->setText(QStringLiteral("Red serif words"));
    QApplication::clipboard()->setMimeData(md);
    settle(100);
}

static QString familyAt(QTextDocument *d, const QString &text)
{
    QTextCursor c(d);
    c.setPosition(qMax(0, int(d->toPlainText().indexOf(text))) + 1);
    const QStringList l = c.charFormat().fontFamilies().toStringList();
    return l.isEmpty() ? QString() : l.first();
}

// the cursor into a new, empty paragraph at the end
static int endParagraph(TextEditor *e)
{
    QTextCursor c(e->document());
    c.movePosition(QTextCursor::End);
    e->setTextCursor(c);
    if (!c.block().text().isEmpty())
        QTest::keyClick(e, Qt::Key_Return);
    settle(60);
    return e->textCursor().position();
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV0602Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV0602Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);

    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.60.2")) >= 0
              && QLatin1String(QB_APP_VERSION_SHOWN).endsWith(QLatin1String(" alpha")),
          QStringLiteral("version 0.60.2 or newer, shown with \"alpha\""), QStringLiteral(QB_APP_VERSION_SHOWN));
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
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();

    QTemporaryDir work;
    templates::Info blank;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("blank"))
            blank = t;
    QString err;
    auto proj = templates::createProject(blank, work.path(), QStringLiteral("Paste Menu"), &err);
    check(proj != nullptr, QStringLiteral("(project made)"), err);
    if (!proj)
        return 1;
    Project *p = proj.get();
    chooseSerif(p);
    BinderItem *doc = textDoc(p, QStringLiteral("Paste Here"), {QStringLiteral("The keeper climbed the stairs.")});
    BinderItem *spelled = textDoc(p, QStringLiteral("Spelling"), {QStringLiteral("They could not be boethered to leave the harbor.")});
    p->save(&err);

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1400, 950);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(proj)), QStringLiteral("(project open)"));
    settle(900);

    EditorPane *pane = visiblePane(w);
    if (!pane)
        return 1;
    pane->showItems({doc}, false, int(ViewMode::Single));
    settle(400);
    TextEditor *e = pane->currentEditor();
    if (!e)
        return 1;
    e->setFocus();
    QTextDocument *d = e->document();

    // ---- the entries, their order and shortcuts
    putRtf();
    QMenu *m = contextMenuAt(e, 4);
    check(m != nullptr, QStringLiteral("(right-click menu in the text)"));
    if (!m)
        return 1;
    const QStringList n = names(m);
    const int at = int(n.indexOf(QStringLiteral("edit-paste")));
    check(at >= 0 && n.value(at + 1) == QLatin1String("ctx-pastePlain") && n.value(at + 2) == QLatin1String("ctx-pasteKeep"),
          QStringLiteral("right-click menu: Paste, Paste and Match Style, Paste with Formatting one below the other"), n.join(QLatin1Char(' ')));
    QAction *plain = named(m, QStringLiteral("ctx-pastePlain")), *keep = named(m, QStringLiteral("ctx-pasteKeep"));
    const QString plainKeys = commands::shortcutOf(QStringLiteral("pastePlain")).toString(QKeySequence::NativeText);
    const QString keepKeys = commands::shortcutOf(QStringLiteral("pasteKeep")).toString(QKeySequence::NativeText);
    check(plain && keep && plain->text().startsWith(QLatin1String("Paste and &Match Style\t")) && plain->text().endsWith(plainKeys)
              && keep->text().startsWith(QLatin1String("Paste with &Formatting\t")) && keep->text().endsWith(keepKeys)
              && plainKeys == QLatin1String("Ctrl+Shift+V") && plain->isEnabled() && keep->isEnabled(),
          QStringLiteral("... named as in the Edit menu, with their shortcuts (Ctrl+Shift+V, %1), on while there is something to paste").arg(keepKeys),
          plain && keep ? plain->text() + QLatin1String(" / ") + keep->text() : QString());
    QWidget *popup = m;
    popup->grab().save(outDir + QStringLiteral("/01-right-click-paste.png"));
    w.grab().save(outDir + QStringLiteral("/01-window.png"));

    // ---- each does what its command does
    struct Way {
        QString name;
        bool keepsFont;
    };
    for (const Way &way : {Way{QStringLiteral("edit-paste"), false}, Way{QStringLiteral("ctx-pastePlain"), false},
                           Way{QStringLiteral("ctx-pasteKeep"), true}}) {
        const int pos = endParagraph(e);
        const QString before = d->toPlainText();
        QMenu *menu = contextMenuAt(e, pos);
        QAction *a = menu ? named(menu, way.name) : nullptr;
        const bool found = a != nullptr;
        const QString label = a ? a->text().section(QLatin1Char('\t'), 0, 0).remove(QLatin1Char('&')) : way.name;
        if (a)
            a->trigger();
        closePopups(); // the menu and its entries are deleted when it closes
        settle(150);
        const QTextBlock b = d->findBlock(pos);
        const bool pasted = b.text().startsWith(QLatin1String("Red serif words"));
        const QString fam = familyAt(d, QStringLiteral("Red serif"));
        const bool centered = b.blockFormat().alignment() & Qt::AlignHCenter;
        check(found && pasted && (way.keepsFont ? (fam == g_serif && centered) : (fam.isEmpty() && !centered)),
              way.keepsFont ? QStringLiteral("right-click Paste with Formatting keeps the font and the centered paragraph")
                            : QStringLiteral("right-click %1 pastes in the look of the text").arg(label),
              QStringLiteral("text '%1', font '%2', centered %3").arg(b.text(), fam).arg(centered));
        e->undo();
        settle(80);
        check(d->toPlainText() == before, QStringLiteral("... and one Undo takes it back"));
    }

    // ---- Edit > Paste and Match Style (moved into the editor) still works
    {
        const int pos = endParagraph(e);
        QApplication::clipboard()->setText(QStringLiteral("Line one\nLine two"));
        settle(100);
        e->setFocus();
        settle(60);
        byKey(&w, QStringLiteral("pastePlain"))->trigger();
        settle(120);
        const QTextBlock b = d->findBlock(pos);
        check(b.text() == QLatin1String("Line one") && b.next().text() == QLatin1String("Line two")
                  && familyAt(d, QStringLiteral("Line two")).isEmpty() && e->textCursor().position() == b.next().position() + 8,
              QStringLiteral("Edit > Paste and Match Style: plain lines as paragraphs, the cursor after them"),
              b.text() + QLatin1String(" | ") + b.next().text());
        e->undo();
        settle(80);
    }

    // ---- nothing to paste: the entries are off
    QApplication::clipboard()->clear();
    settle(150);
    m = contextMenuAt(e, 4);
    if (m) {
        QAction *qp = named(m, QStringLiteral("edit-paste"));
        QAction *pp = named(m, QStringLiteral("ctx-pastePlain"));
        QAction *pk = named(m, QStringLiteral("ctx-pasteKeep"));
        check(qp && pp && pk && !qp->isEnabled() && !pp->isEnabled() && !pk->isEnabled(),
              QStringLiteral("with an empty clipboard the three paste entries are off"));
    } else {
        check(false, QStringLiteral("(right-click menu with an empty clipboard)"));
    }
    closePopups();

    // ---- on a misspelled word: under More Commands
    SpellChecker &sc = SpellChecker::instance();
    if (!sc.isAvailable() && !sc.setLanguage(QStringLiteral("en_US"))) {
        info(QStringLiteral("(no dictionary installed: the spelling menu is not checked)"));
    } else {
        SpellChecker::setEnabled(true);
        putRtf();
        pane->showItems({spelled}, false, int(ViewMode::Single));
        settle(400);
        TextEditor *s = pane->currentEditor();
        QMenu *sm = s ? contextMenuAt(s, int(s->document()->toPlainText().indexOf(QLatin1String("boethered"))) + 3) : nullptr;
        QMenu *more = nullptr;
        if (sm)
            for (QAction *a : sm->actions())
                if (a->menu() && a->menu()->objectName() == QLatin1String("spellingMore"))
                    more = a->menu();
        const QStringList mn = more ? names(more) : QStringList();
        const int i = int(mn.indexOf(QStringLiteral("edit-paste")));
        check(more && i >= 0 && mn.value(i + 1) == QLatin1String("ctx-pastePlain") && mn.value(i + 2) == QLatin1String("ctx-pasteKeep")
                  && !names(sm).contains(QStringLiteral("ctx-pasteKeep")),
              QStringLiteral("on a misspelled word the three paste entries are under More Commands"), mn.join(QLatin1Char(' ')));
        closePopups();
        SpellChecker::setEnabled(false);
    }

    settle(200);
    std::printf("\n%d failure(s)\n", failures);
    delete wp;
    return failures;
}
