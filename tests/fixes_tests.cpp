// Scripted run for the corrections of 0.14.1: fonts in Project Settings and
// the format bar (at once, project-wide), the label color in the synopsis and
// notes, project bookmarks, the new document icons, search results marked in
// the editor. Saves screenshots into the folder given as first argument.
// Exit code = number of failed checks.
//
//   QT_QPA_PLATFORM=offscreen ./qb_fixes_tests /tmp/shots
#include "compile/PluginManager.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "core/Snapshots.h"
#include "text/DirectFormat.h"
#include "text/DisplayFormat.h"
#include "text/Styles.h"
#include "text/TextProps.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/BookmarkList.h"
#include "ui/EditorPane.h"
#include "ui/FormattingChange.h"
#include "ui/Inspector.h"
#include "ui/MainWindow.h"
#include "ui/ProjectSettingsDialog.h"
#include "ui/StyleDialogs.h"
#include "ui/FormatDialogs.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFontComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QPainter>
#include <QListWidget>
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
            std::printf("      %s\n", qPrintable(detail.left(2000)));
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

static void shot(QWidget *w, const QString &name)
{
    settle(300);
    w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}

static QAction *byKey(QWidget *w, const QString &key) { return w->findChild<QAction *>(key); }

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

static TextEditor *editorShowing(MainWindow &w, const QString &needle)
{
    for (TextEditor *e : w.findChildren<TextEditor *>())
        if (e->isVisible() && !e->isReadOnly() && e->item() && e->toPlainText().contains(needle))
            return e;
    return nullptr;
}

// the font a reader sees at the first letter of `word`
static QTextCharFormat formatAt(QTextDocument *doc, const QString &word)
{
    const QTextCursor f = doc->find(word);
    if (f.isNull())
        return QTextCharFormat();
    QTextCursor c(doc);
    c.setPosition(f.selectionStart() + 1);
    return c.charFormat();
}
static QString familyAt(QTextDocument *doc, const QString &word)
{
    const QTextCharFormat cf = formatAt(doc, word);
    const QStringList fams = cf.fontFamilies().toStringList();
    return fams.isEmpty() ? doc->defaultFont().family() : fams.first();
}
static QString ownAt(QTextDocument *doc, const QString &word)
{
    const direct::CharSpec sp = direct::charSpec(formatAt(doc, word));
    return QStringLiteral("own family '%1', own size %2").arg(sp.family).arg(sp.size);
}
static QTextBlockFormat blockOf(QTextDocument *doc, const QString &word)
{
    const QTextCursor f = doc->find(word);
    return f.isNull() ? QTextBlockFormat() : f.block().blockFormat();
}
static bool hasSnapshot(Project *p, BinderItem *it, const QString &title)
{
    for (const Snapshot &s : snapshots::list(p, it->id))
        if (s.title == title)
            return true;
    return false;
}

static QDoubleSpinBox *spinWithSuffix(QWidget *w, const QString &suffix, double max)
{
    for (QDoubleSpinBox *s : w->findChildren<QDoubleSpinBox *>())
        if (s->suffix() == suffix && qFuzzyCompare(s->maximum(), max))
            return s;
    return nullptr;
}
static QDoubleSpinBox *spinWithRange(QWidget *w, double min, double max)
{
    for (QDoubleSpinBox *s : w->findChildren<QDoubleSpinBox *>())
        if (qFuzzyCompare(s->minimum(), min) && qFuzzyCompare(s->maximum(), max))
            return s;
    return nullptr;
}
static QCheckBox *checkBoxNamed(QWidget *w, const QString &text)
{
    for (QCheckBox *c : w->findChildren<QCheckBox *>())
        if (c->text() == text)
            return c;
    return nullptr;
}

// Project Settings ▸ Formatting: the given font, size and line spacing, then Apply
// (inspect runs with the dialog still open), then OK.
static void projectFormatting(MainWindow &w, const QString &family, double size, double line, bool ownToo,
                              const std::function<void(ProjectSettingsDialog *)> &inspect)
{
    withModal<ProjectSettingsDialog>(400, [=](ProjectSettingsDialog *d) {
        checkBoxNamed(d, QStringLiteral("Automatic serif font"))->setChecked(false);
        d->findChild<QFontComboBox *>()->setCurrentFont(QFont(family));
        spinWithSuffix(d, QStringLiteral(" pt"), 48)->setValue(size);
        spinWithRange(d, 0.8, 3.0)->setValue(line);
        d->findChild<QCheckBox *>(QStringLiteral("formattingOwnFollows"))->setChecked(ownToo);
        auto *bb = d->findChild<QDialogButtonBox *>(QStringLiteral("projectSettingsButtons"));
        bb->button(QDialogButtonBox::Apply)->click();
        settle(300);
        if (inspect)
            inspect(d);
        bb->button(QDialogButtonBox::Ok)->click();
    });
    byKey(&w, QStringLiteral("defaultTextFormatting"))->trigger();
    settle(300);
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderFixesTest"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderFixesTest"));
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
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();

    QTemporaryDir work;
    templates::Info novel;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("novel"))
            novel = t;
    QString err;
    auto proj = templates::createProject(novel, work.path(), QStringLiteral("The Lighthouse"), &err);
    check(proj != nullptr, QStringLiteral("(project made)"), err);
    if (!proj)
        return 1;
    Project *p = proj.get();
    // documents with text in a font of its own (as from an import) and without
    BinderItem *ch = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Chapter One"));
    BinderItem *d1 = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("The Storm"));
    BinderItem *d2 = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("The Calm"));
    BinderItem *d3 = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("The Ships"));
    BinderItem *d4 = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("The Bell"));
    auto own = [](const QString &family, double size) {
        QTextCharFormat cf;
        direct::CharSpec sp;
        sp.family = family;
        sp.size = size;
        direct::applyCharSpec(cf, sp);
        return cf;
    };
    {
        QTextCursor c(p->docs()->text(d1->id));
        c.setBlockFormat(display::blockFormatFor(p->formatting, QStringLiteral("Heading 1"), QTextBlockFormat()));
        c.insertText(QStringLiteral("Night Watch"), display::charFormatFor(p->formatting, QStringLiteral("Heading 1")));
        c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
        c.insertText(QStringLiteral("The wind rose in the night and the sea came up over the rocks below the lamp."),
                     own(QStringLiteral("DejaVu Sans"), 15));
    }
    {
        QTextCursor c(p->docs()->text(d2->id));
        c.setBlockFormat(display::blockFormatFor(p->formatting, QStringLiteral("Heading 1"), QTextBlockFormat()));
        c.insertText(QStringLiteral("Morning"), display::charFormatFor(p->formatting, QStringLiteral("Heading 1")));
        c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
        c.insertText(QStringLiteral("By morning the boats were gone and the keeper counted the gulls."),
                     own(QStringLiteral("DejaVu Sans"), 15));
    }
    {
        QTextCursor c(p->docs()->text(d3->id));
        c.insertText(QStringLiteral("Three ships passed the reef at noon; the keeper wrote their names in the book."));
        c.insertText(QStringLiteral(" Signal LOG-7."), own(QStringLiteral("DejaVu Sans Mono"), 0));
        c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
        c.insertText(QStringLiteral("Arrivals"), own(QString(), 20));
    }
    {
        QTextCursor c(p->docs()->text(d4->id));
        c.insertText(QStringLiteral("The fog bell rang twice before the light went on."), own(QStringLiteral("DejaVu Sans"), 0));
    }
    p->save(&err);

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1400, 950);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(proj)), QStringLiteral("(project open)"));
    settle(400);
    auto *binder = w.findChild<BinderPanel *>();
    auto *inspector = w.findChild<Inspector *>();
    auto *formatBar = w.findChild<QToolBar *>(QStringLiteral("formatBar"));
    auto *fontCombo = formatBar ? formatBar->findChild<QFontComboBox *>() : nullptr;
    QDoubleSpinBox *sizeSpin = formatBar ? formatBar->findChild<QDoubleSpinBox *>() : nullptr;
    auto *scope = w.findChild<FontScopeBar *>();
    check(binder && inspector && fontCombo && sizeSpin && scope, QStringLiteral("(window parts found)"));
    if (!binder || !inspector || !fontCombo || !sizeSpin || !scope)
        return 1;

    // ============================================================ Project Settings ▸ Formatting
    {
        binder->select({d1}, true);
        settle(300);
        QTextDocument *t1 = p->docs()->text(d1->id);
        bool openWhileDialog = false, lineWhileDialog = false, dialogStays = false;
        QString detail;
        projectFormatting(w, QStringLiteral("DejaVu Serif"), 14, 1.5, true, [&](ProjectSettingsDialog *d) {
            // Apply: the open editor shows the change at once, the dialog stays
            openWhileDialog = familyAt(t1, QStringLiteral("wind")) == QLatin1String("DejaVu Serif")
                              && direct::charSpec(formatAt(t1, QStringLiteral("wind"))).isEmpty();
            lineWhileDialog = qAbs(blockOf(t1, QStringLiteral("wind")).lineHeight() - 150) < 0.5;
            dialogStays = d->isVisible();
            detail = familyAt(t1, QStringLiteral("wind")) + QLatin1String(", ") + ownAt(t1, QStringLiteral("wind"))
                     + QStringLiteral(", line %1").arg(blockOf(t1, QStringLiteral("wind")).lineHeight());
            d->grab().save(outDir + QLatin1String("/fix-01-project-formatting.png"));
            shot(&w, QStringLiteral("fix-02-applied-behind-the-dialog"));
        });
        check(openWhileDialog && lineWhileDialog && dialogStays,
              QStringLiteral("Project Settings > Formatting: Apply changes the open text at once (font, size, line spacing), "
                             "the dialog stays open"),
              detail);
        QTextDocument *t2 = p->docs()->text(d2->id);
        check(familyAt(t2, QStringLiteral("gulls")) == QLatin1String("DejaVu Serif")
                  && direct::charSpec(formatAt(t2, QStringLiteral("gulls"))).isEmpty()
                  && qAbs(blockOf(t2, QStringLiteral("gulls")).lineHeight() - 150) < 0.5,
              QStringLiteral("... a closed document whose text had a font and size of its own (imported) follows as well"),
              familyAt(t2, QStringLiteral("gulls")) + QLatin1String(", ") + ownAt(t2, QStringLiteral("gulls")));
        QTextDocument *t3 = p->docs()->text(d3->id);
        const direct::CharSpec mono = direct::charSpec(formatAt(t3, QStringLiteral("LOG-7")));
        const direct::CharSpec big = direct::charSpec(formatAt(t3, QStringLiteral("Arrivals")));
        check(mono.family == QLatin1String("DejaVu Sans Mono") && qAbs(big.size - 20) < 0.01
                  && familyAt(t3, QStringLiteral("reef")) == QLatin1String("DejaVu Serif"),
              QStringLiteral("... what differs from the main text of a document stays: a passage in another font, a larger line"),
              ownAt(t3, QStringLiteral("LOG-7")) + QLatin1String(" / ") + ownAt(t3, QStringLiteral("Arrivals")));
        const QString title = QStringLiteral("Before the project formatting changed");
        check(hasSnapshot(p, d1, title) && hasSnapshot(p, d2, title) && !hasSnapshot(p, d3, title),
              QStringLiteral("... every document that changed has a snapshot from before (unchanged ones have none)"),
              QStringLiteral("%1 %2 %3").arg(hasSnapshot(p, d1, title)).arg(hasSnapshot(p, d2, title)).arg(hasSnapshot(p, d3, title)));
        QTextDocument *h = p->docs()->text(d2->id);
        check(familyAt(h, QStringLiteral("Morning")) == QLatin1String("DejaVu Serif"),
              QStringLiteral("... headings take the project font too (their style sets no font)"), familyAt(h, QStringLiteral("Morning")));
    }
    // the check box switched off: text with a font of its own keeps it
    {
        p->docs()->editStored(d4->id, false, [&](QTextDocument *doc) {
            QTextCursor c(doc);
            c.select(QTextCursor::Document);
            QTextCharFormat cf;
            cf.setFontFamilies({QStringLiteral("DejaVu Sans")});
            c.mergeCharFormat(cf);
            return 1;
        });
        projectFormatting(w, QStringLiteral("Caladea"), 14, 1.5, false, nullptr);
        QTextDocument *t4 = p->docs()->text(d4->id);
        QTextDocument *t1 = p->docs()->text(d1->id);
        check(familyAt(t4, QStringLiteral("fog")) == QLatin1String("DejaVu Sans")
                  && familyAt(t1, QStringLiteral("wind")) == QLatin1String("Caladea"),
              QStringLiteral("... without \"Change text with a font ... of its own\" text formatted by hand keeps its font"),
              familyAt(t4, QStringLiteral("fog")) + QLatin1String(" / ") + familyAt(t1, QStringLiteral("wind")));
    }

    // ============================================================ format bar: font list
    {
        binder->select({d3}, true);
        settle(300);
        TextEditor *e = editorShowing(w, QStringLiteral("Three ships"));
        QTextDocument *t3 = e ? e->document() : nullptr;
        if (e) {
            e->setFocus();
            QTextCursor c(t3->find(QStringLiteral("reef")));
            c.setPosition(c.selectionStart() + 2);
            e->setTextCursor(c);
            settle(150);
            fontCombo->setCurrentFont(QFont(QStringLiteral("Carlito")));
            settle(300);
        }
        bool whole = t3 != nullptr;
        for (const QString &word : {QStringLiteral("Three"), QStringLiteral("reef"), QStringLiteral("book"), QStringLiteral("LOG-7")})
            whole = whole && direct::charSpec(formatAt(t3, word)).family == QLatin1String("Carlito");
        const bool otherParagraph = t3 && direct::charSpec(formatAt(t3, QStringLiteral("Arrivals"))).family.isEmpty();
        check(whole && otherParagraph,
              QStringLiteral("format bar: a font picked without a selection changes the whole paragraph at once"),
              t3 ? ownAt(t3, QStringLiteral("Three")) + QLatin1String(" / ") + ownAt(t3, QStringLiteral("Arrivals")) : QString());
        check(scope->isVisible() && scope->text().contains(QLatin1String("all text of the project"))
                  && scope->button()->text() == QLatin1String("Use for All Text (Project Font)"),
              QStringLiteral("... the bar above the editor offers the font for all text of the project"),
              scope->text() + QLatin1String(" [") + scope->button()->text() + QLatin1Char(']'));
        shot(&w, QStringLiteral("fix-03-format-bar-offer"));
        scope->button()->click();
        settle(400);
        QTextDocument *t1 = p->docs()->text(d1->id);
        QTextDocument *t4 = p->docs()->text(d4->id);
        check(p->formatting.fontFamily == QLatin1String("Carlito") && familyAt(t1, QStringLiteral("wind")) == QLatin1String("Carlito")
                  && familyAt(t4, QStringLiteral("fog")) == QLatin1String("Carlito")
                  && direct::charSpec(formatAt(t3, QStringLiteral("reef"))).family.isEmpty() && !scope->isVisible(),
              QStringLiteral("... \"Use for All Text\" makes it the project font: every document follows, also text that had a "
                             "font of its own"),
              QStringLiteral("project '%1'; storm %2; bell %3; ships %4")
                  .arg(p->formatting.fontFamily, familyAt(t1, QStringLiteral("wind")), familyAt(t4, QStringLiteral("fog")),
                       ownAt(t3, QStringLiteral("reef"))));
    }
    // a styled paragraph: the offer changes the style in the whole project
    {
        // the heading of the closed document got the font of the time by hand (as imported text has)
        p->docs()->editStored(d2->id, false, [&](QTextDocument *doc) {
            QTextCursor c(doc->find(QStringLiteral("Morning")));
            QTextCharFormat cf;
            cf.setFontFamilies({QStringLiteral("Carlito")});
            c.mergeCharFormat(cf);
            return 1;
        });
        binder->select({d1}, true);
        settle(300);
        TextEditor *e = editorShowing(w, QStringLiteral("Night Watch"));
        if (e) {
            e->setFocus();
            QTextCursor c(e->document()->find(QStringLiteral("Watch")));
            c.clearSelection();
            e->setTextCursor(c);
            settle(150);
            fontCombo->setCurrentFont(QFont(QStringLiteral("FreeSerif")));
            settle(300);
        }
        check(scope->isVisible() && scope->button()->text() == QStringLiteral("Change the Style “Heading 1”"),
              QStringLiteral("format bar: in a styled paragraph the bar offers to change the style"),
              scope->text() + QLatin1String(" [") + scope->button()->text() + QLatin1Char(']'));
        shot(&w, QStringLiteral("fix-04-style-offer"));
        scope->button()->click();
        settle(500);
        const Style *h1 = styles::find(p->formatting, QStringLiteral("Heading 1"));
        QTextDocument *t1 = p->docs()->text(d1->id);
        QTextDocument *t2 = p->docs()->text(d2->id);
        check(h1 && h1->chars.family == QLatin1String("FreeSerif") && familyAt(t1, QStringLiteral("Night")) == QLatin1String("FreeSerif")
                  && familyAt(t2, QStringLiteral("Morning")) == QLatin1String("FreeSerif")
                  && familyAt(t2, QStringLiteral("gulls")) == QLatin1String("Carlito"),
              QStringLiteral("... the style \"Heading 1\" gets the font; all its headings in the project show it, also one that had "
                             "the old font set by hand; body text stays"),
              QStringLiteral("style '%1'; storm %2; calm %3 / %4")
                  .arg(h1 ? h1->chars.family : QString(), familyAt(t1, QStringLiteral("Night")),
                       familyAt(t2, QStringLiteral("Morning")), familyAt(t2, QStringLiteral("gulls"))));
    }
    // the size field: the arrows (and Enter) change the text at once
    {
        binder->select({d3}, true);
        settle(300);
        TextEditor *e = editorShowing(w, QStringLiteral("Three ships"));
        if (e) {
            e->setFocus();
            e->setTextCursor(e->document()->find(QStringLiteral("noon")));
            settle(150);
            sizeSpin->stepBy(4); // as with the arrow keys or the arrow buttons
            settle(300);
        }
        QTextDocument *t3 = e ? e->document() : nullptr;
        const double want = p->formatting.fontSize + 4;
        const bool sized = t3 && qAbs(direct::charSpec(formatAt(t3, QStringLiteral("noon"))).size - want) < 0.01
                           && direct::charSpec(formatAt(t3, QStringLiteral("reef"))).size == 0;
        check(sized && scope->isVisible() && scope->text().contains(QStringLiteral("%1 pt").arg(want)),
              QStringLiteral("format bar: the size field changes the selection at once (no Enter needed) and offers the size for "
                             "all text"),
              (t3 ? ownAt(t3, QStringLiteral("noon")) : QString()) + QLatin1String(" / ") + scope->text());
        if (e) {
            w.activateWindow();
            (void)QTest::qWaitForWindowActive(&w, 2000);
            e->setFocus();
            settle(100);
            QTextCursor c(e->document()->find(QStringLiteral("Arrivals")));
            c.clearSelection();
            e->setTextCursor(c);
            settle(200);
        }
        check(!scope->isVisible(), QStringLiteral("... the offer goes away when the cursor leaves the paragraph"));
        if (e) {
            e->setTextCursor(e->document()->find(QStringLiteral("noon")));
            sizeSpin->setValue(p->formatting.fontSize);
            settle(200);
        }
        scope->hide();
    }

    // Format > Style > Edit Style: Apply changes the style in the whole project, the window stays
    {
        p->docs()->editStored(d4->id, false, [&](QTextDocument *doc) {
            QTextCursor c(doc);
            c.insertBlock();
            c.movePosition(QTextCursor::Start);
            c.setBlockFormat(display::blockFormatFor(p->formatting, QStringLiteral("Heading 2"), QTextBlockFormat()));
            QTextCharFormat cf = display::charFormatFor(p->formatting, QStringLiteral("Heading 2"));
            cf.setFontFamilies({display::resolvedFontFamily(p->formatting)}); // as imported text has it
            c.insertText(QStringLiteral("Fog Signals"), cf);
            return 1;
        });
        QMenu *styleMenu = w.findChild<QMenu *>(QStringLiteral("styleMenu"));
        QAction *editH2 = nullptr;
        if (styleMenu) {
            emit styleMenu->aboutToShow();
            for (QAction *ac : styleMenu->actions())
                if (ac->menu() && ac->text() == QLatin1String("&Edit Style"))
                    for (QAction *e : ac->menu()->actions())
                        if (e->text().contains(QLatin1String("Heading 2")))
                            editH2 = e;
        }
        bool appliedWhileOpen = false, stays = false;
        QString detail;
        withModal<StyleEditDialog>(400, [&](StyleEditDialog *d) {
            withModal<FontDialog>(400, [](FontDialog *fd) {
                if (auto *fams = fd->findChild<QListWidget *>(QStringLiteral("fontFamilies"))) {
                    const auto hits = fams->findItems(QStringLiteral("DejaVu Serif"), Qt::MatchExactly);
                    if (!hits.isEmpty())
                        fams->setCurrentItem(hits.first());
                }
                fd->accept();
            });
            if (auto *fontButton = d->findChild<QPushButton *>(QStringLiteral("styleFont")))
                fontButton->click();
            settle(300);
            d->findChild<QDialogButtonBox *>(QStringLiteral("styleEditButtons"))->button(QDialogButtonBox::Apply)->click();
            settle(400);
            QTextDocument *t4 = p->docs()->text(d4->id);
            appliedWhileOpen = familyAt(t4, QStringLiteral("Signals")) == QLatin1String("DejaVu Serif");
            stays = d->isVisible();
            detail = familyAt(t4, QStringLiteral("Signals")) + QLatin1String(" / ") + ownAt(t4, QStringLiteral("Signals"));
            d->grab().save(outDir + QLatin1String("/fix-12-edit-style-apply.png"));
            d->findChild<QDialogButtonBox *>(QStringLiteral("styleEditButtons"))->button(QDialogButtonBox::Ok)->click();
        });
        if (editH2)
            editH2->trigger();
        settle(400);
        const Style *h2 = styles::find(p->formatting, QStringLiteral("Heading 2"));
        QTextDocument *t4 = p->docs()->text(d4->id);
        check(editH2 && appliedWhileOpen && stays && h2 && h2->chars.family == QLatin1String("DejaVu Serif")
                  && familyAt(t4, QStringLiteral("bell")) == display::resolvedFontFamily(p->formatting),
              QStringLiteral("Format > Style > Edit Style: Apply gives all text of the style the new font at once (also a "
                             "heading that had the old font set by hand); the window stays open; body text stays"),
              detail);
    }

    // ============================================================ RTF projects (Scrivener-like text)
    {
        check(p->docs()->convertStorage(StorageFormat::Rtf, &err), QStringLiteral("(project converted to RTF)"), err);
        BinderItem *d5 = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("The Fog"));
        // a file as Scrivener writes it: every run names its font and size, the paragraph its spacing
        const QString fam = display::resolvedFontFamily(p->formatting);
        const QByteArray rtf = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fnil " + fam.toUtf8()
                               + ";}}\\pard\\sl" + QByteArray::number(qRound(240 * p->formatting.lineSpacing))
                               + "\\slmult1\\fi850\\sa60 {\\f0\\fs" + QByteArray::number(qRound(p->formatting.fontSize * 2))
                               + " The fog came in from the sea and stayed for three days.}\\par\n}";
        QDir().mkpath(QFileInfo(p->docs()->textPath(d5->id)).absolutePath());
        QFile f(p->docs()->textPath(d5->id));
        f.open(QIODevice::WriteOnly);
        f.write(rtf);
        f.close();
        {
            QTextDocument probe;
            p->docs()->deserialize(d5->id, rtf, &probe, true);
            info(QStringLiteral("(the file before: %1)").arg(ownAt(&probe, QStringLiteral("stayed"))));
        }
        projectFormatting(w, QStringLiteral("FreeSerif"), 12, 2.0, false, nullptr);
        QTextDocument *t5 = p->docs()->text(d5->id);
        const QTextBlockFormat bf = blockOf(t5, QStringLiteral("fog"));
        check(familyAt(t5, QStringLiteral("stayed")) == QLatin1String("FreeSerif")
                  && direct::charSpec(formatAt(t5, QStringLiteral("stayed"))).isEmpty() && qAbs(bf.lineHeight() - 200) < 0.5
                  && qAbs(bf.textIndent() - 850 / 15.0) < 0.5,
              QStringLiteral("RTF project: text that names the old project font and size in the file follows the new ones; "
                             "a first line indent of its own stays"),
              familyAt(t5, QStringLiteral("stayed")) + QLatin1String(", ") + ownAt(t5, QStringLiteral("stayed"))
                  + QStringLiteral(", line %1, indent %2").arg(bf.lineHeight()).arg(bf.textIndent()));
    }

    // ============================================================ project bookmarks
    {
        inspector->showTab(Inspector::Notes);
        binder->select({d2}, true);
        settle(300);
        TextEditor *main = editorShowing(w, QStringLiteral("counted the gulls"));
        const int widthBefore = main ? main->lineWrapColumnOrWidth() : -1;
        byKey(&w, QStringLiteral("bookmark"))->trigger();
        settle(300);
        const bool stored = !p->projectBookmarks.isEmpty() && p->projectBookmarks.last().target == QLatin1String("qb://") + d2->id;
        auto *button = w.findChild<QToolButton *>(QStringLiteral("bookmarksButton"));
        QStringList menuTitles;
        if (button && button->menu()) {
            emit button->menu()->aboutToShow();
            for (QAction *ac : button->menu()->actions())
                menuTitles << ac->text();
        }
        BookmarkListWidget *list = inspector->projectBookmarkList();
        QStringList listed;
        for (int i = 0; list && i < list->count(); ++i)
            listed << list->item(i)->text();
        auto *title = inspector->findChild<QLabel *>(QStringLiteral("projectBookmarksTitle"));
        check(stored && menuTitles.contains(QStringLiteral("The Calm")) && listed.contains(QStringLiteral("The Calm"))
                  && list->isVisible() && title && title->isVisible() && inspector->currentTab() == Inspector::Bookmarks
                  && list->currentItem() && list->currentItem()->text() == QLatin1String("The Calm"),
              QStringLiteral("Documents > Add to Project Bookmarks: the bookmark appears under Inspector > Bookmarks (section "
                             "\"Project Bookmarks\", the inspector shows it) and in the toolbar's bookmarks menu"),
              QStringLiteral("stored %1, toolbar: %2, inspector: %3, tab %4")
                  .arg(stored)
                  .arg(menuTitles.join(QLatin1String(" | ")), listed.join(QLatin1String(" | ")))
                  .arg(inspector->currentTab()));
        TextEditor *preview = nullptr;
        for (TextEditor *te : inspector->findChildren<TextEditor *>())
            if (te->isVisible() && te->toPlainText().contains(QLatin1String("counted the gulls")))
                preview = te;
        // (since 0.21.1 the copy can be edited: it is kept in step with the document, see qb_v0211_tests)
        check(main && main->lineWrapColumnOrWidth() == widthBefore && widthBefore > 400 && preview && preview->document() != main->document(),
              QStringLiteral("... the preview of the document that the editor shows is a copy: the editor keeps its "
                             "line width"),
              QStringLiteral("width %1 -> %2, preview %3").arg(widthBefore).arg(main ? main->lineWrapColumnOrWidth() : -1).arg(preview != nullptr));
        // the document's own bookmarks are still there, in their section
        BookmarkListWidget *docList = inspector->bookmarkList();
        check(docList && docList->isVisible() && inspector->findChild<QLabel *>(QStringLiteral("documentBookmarksTitle")),
              QStringLiteral("... the document's own bookmarks have their section above"));
        shot(&w, QStringLiteral("fix-05-project-bookmarks"));
    }

    // ============================================================ label color in synopsis and notes
    {
        // a strong color far from the cream of the cards (shows best in the screenshots)
        const Label *red = nullptr;
        auto score = [](const QColor &c) {
            const QColor h = c.toHsv();
            const double d = qAbs(h.hsvHueF() * 360 - 50);
            return h.hsvSaturationF() > 0.4 ? h.hsvSaturationF() * qMin(d, 360 - d) : -1.0;
        };
        for (const Label &l : p->labels)
            if (l.color.isValid() && score(l.color) > 0 && (!red || score(l.color) > score(red->color)))
                red = &l;
        check(red != nullptr, QStringLiteral("(a label with a strong color)"));
        if (red) {
            d2->label = red->id;
            ch->label = red->id;
            p->notifyItemChanged(d2, false);
            p->notifyItemChanged(ch, false);
        }
        auto *card = inspector->findChildren<QPlainTextEdit *>().value(0);
        for (QPlainTextEdit *pe : inspector->findChildren<QPlainTextEdit *>())
            if (pe->property("qbTint").isValid() || pe->placeholderText().startsWith(QLatin1String("Synopsis")))
                card = pe;
        TextEditor *notes = nullptr;
        for (TextEditor *te : inspector->findChildren<TextEditor *>())
            if (te->isReadOnly() == false && te->objectName() != QLatin1String("bookmarkPreview"))
                notes = notes ? notes : te;
        auto expected = [&](theme::Role role) {
            const QColor c = red->color.toHsv();
            return QColor::fromHsvF(c.hsvHueF(), c.hsvSaturationF() * 0.15f, theme::color(role).valueF());
        };
        auto near = [](const QColor &a, const QColor &b) {
            return qAbs(a.red() - b.red()) <= 3 && qAbs(a.green() - b.green()) <= 3 && qAbs(a.blue() - b.blue()) <= 3;
        };
        auto pixel = [](QWidget *view) {
            const QImage img = view->grab().toImage();
            return img.isNull() ? QColor() : QColor(img.pixel(img.width() - 6, 3));
        };
        for (BinderItem *it : {d2, ch}) {
            binder->select({it}, true);
            inspector->showTab(Inspector::Notes);
            settle(400);
            const QColor want = red ? expected(theme::Role::SynopsisPaper) : QColor();
            const QColor seen = card ? pixel(card->viewport()) : QColor();
            const QColor wantNotes = red ? expected(theme::Role::NotesPaper) : QColor();
            const QColor seenNotes = notes ? pixel(notes->viewport()) : QColor();
            const QColor lc = red ? red->color.toHsv() : QColor();
            const QColor sc = seen.toHsv();
            check(red && near(seen, want) && near(seenNotes, wantNotes) && qAbs(sc.hsvHueF() - lc.hsvHueF()) < 0.03
                      && qAbs(sc.hsvSaturationF() - lc.hsvSaturationF() * 0.15) < 0.03,
                  QStringLiteral("label color in \"Synopsis and notes\" (%1): the card and the notes show the label's color "
                                 "with 15 %% of its saturation")
                      .arg(it == ch ? QStringLiteral("folder") : QStringLiteral("document")),
                  QStringLiteral("label %1 (s %2); card %3 (s %4, want %5); notes %6 (want %7)")
                      .arg(red ? red->color.name() : QString())
                      .arg(lc.hsvSaturationF(), 0, 'f', 2)
                      .arg(seen.name())
                      .arg(sc.hsvSaturationF(), 0, 'f', 3)
                      .arg(want.name(), seenNotes.name(), wantNotes.name()));
            shot(&w, it == ch ? QStringLiteral("fix-07-label-tint-folder") : QStringLiteral("fix-06-label-tint-document"));
        }
        binder->select({d3}, true);
        inspector->showTab(Inspector::Notes);
        settle(300);
        const QColor plain = card ? pixel(card->viewport()) : QColor();
        check(near(plain, theme::color(theme::Role::SynopsisPaper)),
              QStringLiteral("... a document without label keeps the normal card color"),
              plain.name() + QLatin1String(" / ") + theme::color(theme::Role::SynopsisPaper).name());
    }

    // ============================================================ new icons
    {
        QMenu *iconMenu = nullptr;
        for (QMenu *m : w.menuBar()->findChildren<QMenu *>())
            if (m->title() == QLatin1String("Change &Icon"))
                iconMenu = m;
        QStringList missing;
        QList<QAction *> found;
        if (iconMenu) {
            // since 0.17 the menu is built like Scrivener's: the check marks are Ticked / Unticked, Pass,
            // Fail and Unclear in the submenu To Do; Proofread and Question Mark among the single icons
            emit iconMenu->aboutToShow();
            QList<QAction *> all;
            std::function<void(QMenu *)> collect = [&](QMenu *m) {
                emit m->aboutToShow();
                for (QAction *ac : m->actions()) {
                    if (ac->menu())
                        collect(ac->menu());
                    else
                        all << ac;
                }
            };
            collect(iconMenu);
            for (const char *want : {"qb-checked", "qb-unchecked", "qb-proofread", "qb-question", "qb-fail", "qb-pass", "qb-unclear"}) {
                QAction *hit = nullptr;
                for (QAction *ac : all)
                    if (ac->data().toString() == QLatin1String(want))
                        hit = ac;
                if (!hit || hit->icon().pixmap(32).isNull())
                    missing << QLatin1String(want);
                else
                    found << hit;
            }
        }
        check(iconMenu && missing.isEmpty(),
              QStringLiteral("Documents > Change Icon has Checked, Unchecked, Proofread (PR), Question, Fail, Pass, Unclear"),
              missing.join(QLatin1String(", ")));
        // the icons side by side, light theme
        QImage strip(found.size() * 72 + 8, 80, QImage::Format_ARGB32);
        strip.fill(Qt::white);
        {
            QPainter pt(&strip);
            int x = 8;
            for (QAction *ac : found) {
                pt.drawPixmap(x, 8, ac->icon().pixmap(64));
                x += 72;
            }
        }
        strip.save(outDir + QLatin1String("/fix-08-icons.png"));
        // pick one for a document
        binder->select({d3}, true);
        settle(200);
        for (QAction *ac : found)
            if (ac->data().toString() == QLatin1String("qb-proofread"))
                ac->trigger();
        settle(200);
        check(d3->icon == QLatin1String("qb-proofread") && !icons::forItem(p, d3).isNull(),
              QStringLiteral("... choosing \"Proofread (PR)\" gives the document that icon in the binder"), d3->icon);
        shot(&w, QStringLiteral("fix-09-icon-in-binder"));
    }

    // ============================================================ search: matches marked in the editor
    {
        auto *quick = w.findChild<QLineEdit *>(QStringLiteral("quickSearch"));
        quick->setFocus();
        QTest::keyClicks(quick, QStringLiteral("gulls"));
        settle(700);
        QCompleter *completer = quick->completer();
        QModelIndex target;
        for (int i = 0; completer && i < completer->model()->rowCount(); ++i) {
            const QModelIndex idx = completer->model()->index(i, 0);
            if (idx.data().toString() == QLatin1String("The Calm") && (completer->model()->flags(idx) & Qt::ItemIsSelectable))
                target = idx;
        }
        if (target.isValid())
            emit completer->activated(target);
        if (completer && completer->popup())
            completer->popup()->hide();
        settle(500);
        w.activateWindow();
        settle(200);
        TextEditor *e = editorShowing(w, QStringLiteral("counted the gulls"));
        const QString sel = e ? e->textCursor().selectedText() : QString();
        check(target.isValid() && e && e->searchMatchCount() == 1 && sel == QLatin1String("gulls"),
              QStringLiteral("quick search: the chosen result opens with the searched word marked and selected"),
              QStringLiteral("found %1, matches %2, selected '%3'").arg(target.isValid()).arg(e ? e->searchMatchCount() : -1).arg(sel));
        shot(&w, QStringLiteral("fix-10-quick-search-marked"));
        // another document from the binder: no marks left over
        binder->select({d1}, true);
        settle(300);
        binder->select({d2}, true);
        settle(300);
        TextEditor *again = editorShowing(w, QStringLiteral("counted the gulls"));
        check(again && again->searchMatchCount() == 0, QStringLiteral("... the marks go when another document is chosen in the binder"),
              QStringLiteral("matches %1").arg(again ? again->searchMatchCount() : -1));

        // project search: the result list
        byKey(&w, QStringLiteral("projectSearch"))->trigger();
        settle(300);
        QLineEdit *field = binder->searchField();
        field->setText(QStringLiteral("keeper"));
        emit field->returnPressed();
        settle(400);
        ItemListWidget *results = binder->resultsList();
        bool picked = false;
        for (int i = 0; results && i < results->count(); ++i)
            if (results->item(i)->data(Qt::UserRole).toString() == d3->id) {
                results->setCurrentRow(i);
                picked = true;
            }
        settle(500);
        TextEditor *e3 = editorShowing(w, QStringLiteral("Three ships"));
        const QString sel3 = e3 ? e3->textCursor().selectedText() : QString();
        check(picked && e3 && e3->searchMatchCount() == 1 && sel3 == QLatin1String("keeper"),
              QStringLiteral("project search: the chosen result opens with the matches marked and the first one selected"),
              QStringLiteral("picked %1, matches %2, selected '%3'").arg(picked).arg(e3 ? e3->searchMatchCount() : -1).arg(sel3));
        shot(&w, QStringLiteral("fix-11-project-search-marked"));
        binder->hideSearch();
        settle(200);
    }

    // ============================================================ the same in the dark theme
    {
        as.theme = QStringLiteral("dark");
        as.save();
        theme::apply();
        settle(300);
        binder->select({d2}, true);
        inspector->showTab(Inspector::Notes);
        settle(400);
        QPlainTextEdit *card = nullptr;
        for (QPlainTextEdit *pe : inspector->findChildren<QPlainTextEdit *>())
            if (pe->placeholderText().startsWith(QLatin1String("Synopsis")))
                card = pe;
        TextEditor *e = editorShowing(w, QStringLiteral("counted the gulls"));
        if (e) {
            w.activateWindow();
            e->setFocus();
            QTextCursor c(e->document()->find(QStringLiteral("gulls")));
            c.clearSelection();
            e->setTextCursor(c);
            fontCombo->setCurrentFont(QFont(QStringLiteral("DejaVu Sans")));
            settle(300);
        }
        const QImage img = card ? card->viewport()->grab().toImage() : QImage();
        const QColor tint = img.isNull() ? QColor() : QColor(img.pixel(img.width() - 6, 3));
        const QColor bar = scope->isVisible() ? QColor(scope->grab().toImage().pixel(3, scope->height() / 2)) : QColor();
        check(tint.isValid() && tint.valueF() < 0.5 && tint.hsvSaturationF() > 0.02 && bar.isValid() && bar.valueF() < 0.5,
              QStringLiteral("dark theme: the label tint and the offer bar are dark as well"),
              QStringLiteral("card %1, bar %2").arg(tint.name(), bar.name()));
        shot(&w, QStringLiteral("fix-13-dark-theme"));
        scope->hide();
        if (e)
            e->undo();
    }

    w.closeProject();
    delete wp;
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
