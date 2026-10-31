// Scripted run for version 0.40.2 (private line):
//  * A right-click (or a click) in or beside the text no longer moves the view.
//  * Every project keeps its own look (theme, colors and fonts of the areas, text column
//    width, border beside the text); a project opened first by 0.40.2 keeps the look it
//    had, a new project gets the look set for new projects (ui/ProjectLook.*).
//  * A border in the paper color left and right of the text (10 px for new projects).
//  * The right-click menus offer the colors, fonts and sizes of what is under the mouse.
//  * Project settings saved to a file and read back, with a choice per section
//    (core/SettingsFile.*, ui/SettingsTransfer.*).
//  * A changed style reaches all text of the style, also text with formatting of its own.
// Saves screenshots into the folder given as first argument. Exit code = number of failed
// checks.
//
//   QT_QPA_PLATFORM=offscreen ./qb_v0402_tests /tmp/shots
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "core/Snapshots.h"
#include "core/SettingsFile.h"
#include "core/Objects.h"
#include "compile/PluginManager.h"
#include "text/DirectFormat.h"
#include "text/DisplayFormat.h"
#include "text/Styles.h"
#include "text/TextProps.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/Corkboard.h"
#include "ui/Inspector.h"
#include "ui/Outliner.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/ProjectLook.h"
#include "ui/ThemeDialogs.h"
#include "ui/SettingsTransfer.h"
#include "ui/Windows.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include "AppInfo.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QFile>
#include <QMenu>
#include <QComboBox>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QScrollBar>
#include <QSpinBox>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>
#include <cstdio>
#include <functional>

using namespace qb;

// see builtInThemesUnchanged()
static const char *const kThemesDigest = "43feb46fb0a97a89d14f7cb150f69ece3bafb2d62c3953c6eb8d26374bf0181d";

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

static void shot(QWidget *w, const QString &name)
{
    settle(300);
    w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}

static QAction *byKey(QWidget *w, const QString &key) { return w->findChild<QAction *>(key); }

// the menu a right-click opened (closed by the caller with closePopup)
static QMenu *openPopup()
{
    for (int i = 0; i < 20 && !QApplication::activePopupWidget(); ++i)
        settle(30);
    return qobject_cast<QMenu *>(QApplication::activePopupWidget());
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

// A right-click as the desktop sends it: the button goes down (on Linux the menu is asked
// for at this moment), the menu event, the button comes up.
static void rightClick(QWidget *w, const QPoint &pos)
{
    QTest::mousePress(w, Qt::RightButton, Qt::NoModifier, pos);
    QContextMenuEvent ce(QContextMenuEvent::Mouse, pos, w->mapToGlobal(pos), Qt::NoModifier);
    QApplication::sendEvent(w, &ce);
    settle(80);
    QTest::mouseRelease(w, Qt::RightButton, Qt::NoModifier, pos);
    settle(60);
}

// the place in the editor's viewport of a position in its text
static QPoint pointOf(TextEditor *e, int position)
{
    QTextCursor c(e->document());
    c.setPosition(position);
    const QRect r = e->cursorRect(c);
    return QPoint(r.left() + 2, r.center().y());
}

// a point of the viewport at y (viewport of the column view) in the editor's viewport
static QPoint editorPointAtViewY(TextColumnView *v, TextEditor *e, int viewY, int x = 40)
{
    const QPoint inView(e->viewport()->mapTo(v->viewport(), QPoint(x, 0)).x(), viewY);
    return e->viewport()->mapFrom(v->viewport(), inView);
}

// ======================================================================= 1: right-click jump
static void rightClickChecks(MainWindow &w, EditorPane *pane, BinderItem *longDoc)
{
    pane->showItems({longDoc});
    settle(400);
    TextColumnView *v = pane->textView();
    TextEditor *e = pane->currentEditor();
    check(v && e, QStringLiteral("(long document in the editor)"));
    if (!v || !e)
        return;
    e->setFocus();
    settle(100);
    v->setScrollValue(v->verticalScrollBar()->maximum() / 2);
    settle(200);
    const int start = v->scrollValue();
    const int vh = v->viewport()->height();
    info(QStringLiteral("view %1 px high, scrolled to %2 of %3").arg(vh).arg(start).arg(v->verticalScrollBar()->maximum()));

    // a left click on a line close to the top edge of the view
    {
        const QPoint p = editorPointAtViewY(v, e, 22);
        QTest::mouseClick(e->viewport(), Qt::LeftButton, Qt::NoModifier, p);
        settle(150);
        check(v->scrollValue() == start, QStringLiteral("a click on a line at the top edge leaves the view where it is"),
              QStringLiteral("scroll %1 -> %2").arg(start).arg(v->scrollValue()));
    }
    v->setScrollValue(start);
    settle(100);
    // a right-click on a line close to the bottom edge (the cursor goes there, the view stays)
    {
        const QPoint p = editorPointAtViewY(v, e, vh - 20);
        const int expected = e->cursorForPosition(p).position();
        rightClick(e->viewport(), p);
        QMenu *m = openPopup();
        check(m != nullptr, QStringLiteral("the right-click menu of the text opens"));
        check(v->scrollValue() == start, QStringLiteral("a right-click on a line at the bottom edge leaves the view where it is"),
              QStringLiteral("scroll %1 -> %2").arg(start).arg(v->scrollValue()));
        check(e->textCursor().position() == expected, QStringLiteral("... and puts the cursor where the mouse is"),
              QStringLiteral("%1, expected %2").arg(e->textCursor().position()).arg(expected));
        closePopups();
        check(v->scrollValue() == start, QStringLiteral("... also after the menu closed"),
              QStringLiteral("scroll %1 -> %2").arg(start).arg(v->scrollValue()));
    }
    // a selection in the middle, then a right-click inside it
    v->setScrollValue(start);
    settle(100);
    const QPoint mid = editorPointAtViewY(v, e, vh / 2);
    const int at = e->cursorForPosition(mid).position();
    {
        QTextCursor c = e->textCursor();
        c.setPosition(at);
        c.movePosition(QTextCursor::StartOfWord);
        c.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
        c.movePosition(QTextCursor::NextWord, QTextCursor::KeepAnchor);
        c.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
        e->setTextCursor(c);
        settle(100);
        v->setScrollValue(start);
        settle(100);
    }
    const QString selected = e->textCursor().selectedText();
    check(!selected.isEmpty(), QStringLiteral("(two words selected: \"%1\")").arg(selected));
    {
        const QTextCursor c = e->textCursor();
        rightClick(e->viewport(), pointOf(e, (c.selectionStart() + c.selectionEnd()) / 2));
        check(openPopup() != nullptr, QStringLiteral("right-click inside the selection: the menu opens"));
        check(v->scrollValue() == start && e->textCursor().selectedText() == selected,
              QStringLiteral("... the view stays and the selection is kept"),
              QStringLiteral("scroll %1 -> %2, selection \"%3\"").arg(start).arg(v->scrollValue()).arg(e->textCursor().selectedText()));
        closePopups();
    }
    // the same selection, a right-click beside the text column (the area around the paper)
    {
        QWidget *container = v->widget();
        const QPoint beside = container->mapFrom(v->viewport(), QPoint(6, vh / 2));
        rightClick(container, beside);
        shot(&w, QStringLiteral("01-right-click-beside-the-text"));
        check(v->scrollValue() == start, QStringLiteral("a right-click beside the text leaves the view where it is"),
              QStringLiteral("scroll %1 -> %2 (of %3)").arg(start).arg(v->scrollValue()).arg(v->verticalScrollBar()->maximum()));
        check(e->textCursor().selectedText() == selected, QStringLiteral("... and keeps the selection"),
              QStringLiteral("now \"%1\"").arg(e->textCursor().selectedText()));
        closePopups();
    }
    // a left click beside the text puts the cursor on that line, not at the end of the document
    {
        v->setScrollValue(start);
        settle(100);
        QWidget *container = v->widget();
        const QPoint beside = container->mapFrom(v->viewport(), QPoint(6, vh / 2));
        const int expectedLine = e->cursorForPosition(editorPointAtViewY(v, e, vh / 2, 0)).block().blockNumber();
        QTest::mouseClick(container, Qt::LeftButton, Qt::NoModifier, beside);
        settle(150);
        check(v->scrollValue() == start, QStringLiteral("a click beside the text leaves the view where it is"),
              QStringLiteral("scroll %1 -> %2").arg(start).arg(v->scrollValue()));
        check(e->textCursor().block().blockNumber() == expectedLine,
              QStringLiteral("... and puts the cursor on the paragraph beside the click"),
              QStringLiteral("paragraph %1, expected %2").arg(e->textCursor().block().blockNumber()).arg(expectedLine));
    }
    // dragging a selection past the bottom edge still scrolls the view along
    {
        v->setScrollValue(start);
        settle(100);
        const QPoint from = editorPointAtViewY(v, e, vh / 2);
        const QPoint below = editorPointAtViewY(v, e, vh + 60);
        QTest::mousePress(e->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QMouseEvent move(QEvent::MouseMove, QPointF(below), QPointF(e->viewport()->mapToGlobal(below)), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(e->viewport(), &move);
        settle(100);
        QTest::mouseRelease(e->viewport(), Qt::LeftButton, Qt::NoModifier, below);
        settle(100);
        check(v->scrollValue() > start && v->scrollValue() < start + 200 && e->textCursor().hasSelection(),
              QStringLiteral("dragging a selection below the view still scrolls along, as far as needed"),
              QStringLiteral("scroll %1 -> %2, selection %3").arg(start).arg(v->scrollValue()).arg(e->textCursor().hasSelection()));
    }
}

// a click below the end of a short text puts the cursor at its end (as before)
static void clickBelowShortText(EditorPane *pane, BinderItem *shortDoc)
{
    pane->showItems({shortDoc});
    settle(400);
    TextColumnView *v = pane->textView();
    TextEditor *e = pane->currentEditor();
    if (!v || !e)
        return;
    QTextCursor c = e->textCursor();
    c.setPosition(0);
    e->setTextCursor(c);
    QWidget *container = v->widget();
    const QPoint below = container->mapFrom(v->viewport(), QPoint(v->viewport()->width() / 2, v->viewport()->height() - 10));
    QTest::mouseClick(container, Qt::LeftButton, Qt::NoModifier, below);
    settle(150);
    check(e->textCursor().atEnd(), QStringLiteral("a click below a short text puts the cursor at its end, as before"),
          QStringLiteral("position %1 of %2").arg(e->textCursor().position()).arg(e->document()->characterCount() - 1));
}

// ======================================================================= 2 + 3: look per project, border
static QWidget *columnOf(TextColumnView *v) { return v ? v->findChild<QWidget *>(QStringLiteral("textColumn")) : nullptr; }

// the color of a pixel of a widget as it is drawn
static QColor pixel(QWidget *w, const QPoint &p)
{
    settle(150);
    const QImage img = w->grab().toImage();
    return p.x() >= 0 && p.y() >= 0 && p.x() < img.width() && p.y() < img.height() ? QColor(img.pixel(p)) : QColor();
}

static look::Values lookFile(Project *p)
{
    bool ok = false;
    const look::Values v = look::read(p, &ok);
    return ok ? v : look::Values();
}

static std::unique_ptr<Project> makeProject(const QString &dir, const QString &name, BinderItem **doc)
{
    templates::Info blank;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("blank"))
            blank = t;
    QString err;
    auto proj = templates::createProject(blank, dir, name, &err);
    if (!proj)
        return proj;
    BinderItem *d = proj->model()->createItem(proj->draft(), -1, ItemType::Text, QStringLiteral("Morning"));
    QTextCursor c(proj->docs()->text(d->id));
    for (int i = 1; i <= 12; ++i) {
        if (i > 1)
            c.insertBlock(display::normalBlockFormat(proj->formatting), QTextCharFormat());
        c.insertText(QStringLiteral("Line %1 of a text that runs over more than one line in a column of five hundred pixels.").arg(i));
    }
    proj->save(&err);
    if (doc)
        *doc = d;
    return proj;
}

static EditorPane *visiblePane(MainWindow &w)
{
    for (EditorPane *ep : w.findChildren<EditorPane *>())
        if (ep->isVisible())
            return ep;
    return nullptr;
}

static void lookChecks(MainWindow &w, const QString &dir)
{
    AppSettings &as = AppSettings::instance();
    // the program as the writer has it (the standard of 0.21.2: paper #d1d495, beside #999999, 500 px)
    const QColor paperBefore = as.ownColors.value(QStringLiteral("editorPaper"));
    check(paperBefore.isValid() && paperBefore.name() == QLatin1String("#d1d495") && as.editorWidth == 500 && as.textBorder == 10,
          QStringLiteral("(the program's look: paper #d1d495, column 500 px; new projects get a border of 10 px)"),
          QStringLiteral("%1, %2 px, border %3").arg(paperBefore.name()).arg(as.editorWidth).arg(as.textBorder));

    // a project made by an older version (no look of its own yet), opened by 0.40.2
    BinderItem *docA = nullptr;
    auto a = makeProject(dir, QStringLiteral("Old Lighthouse"), &docA);
    check(a && !look::isStored(a.get()), QStringLiteral("(a project without a look of its own, as older versions made them)"));
    if (!a)
        return;
    Project *pa = a.get();
    const QString fileA = pa->folderPath();
    const QString docAId = docA->id;
    check(w.adoptProject(std::move(a)), QStringLiteral("(the older project open)"));
    settle(300);
    EditorPane *pane = visiblePane(w);
    pane->showItems({docA});
    settle(400);
    look::Values la = lookFile(pa);
    check(look::isStored(pa), QStringLiteral("a project opened first by 0.40.2 gets a look of its own (settings/look.json)"));
    check(la.theme == as.theme && la.colors.value(QStringLiteral("editorPaper")).name() == QLatin1String("#d1d495")
              && la.editorWidth == 500 && la.border == 0,
          QStringLiteral("... the look it had: same theme, paper and width, and no border (it had none)"),
          QString::fromUtf8(QJsonDocument(la.toJson()).toJson(QJsonDocument::Compact)));
    TextColumnView *v = pane->textView();
    QWidget *col = columnOf(v);
    TextEditor *e = pane->currentEditor();
    check(col && e && col->width() == e->width(), QStringLiteral("... no border: the paper is as wide as the text"),
          QStringLiteral("paper %1 px, text %2 px").arg(col ? col->width() : -1).arg(e ? e->width() : -1));
    const int textWidthOld = e ? e->width() : 0;
    shot(&w, QStringLiteral("02-older-project-keeps-its-look"));

    // the look for new projects changes (as an update or File > Options would): the open project stays
    as.ownColors.insert(QStringLiteral("editorPaper"), QColor(QStringLiteral("#ffffff")));
    as.editorWidth = 620;
    as.notifyChanged();
    settle(300);
    check(e && e->palette().color(QPalette::Base).name() == QLatin1String("#d1d495") && col->width() == textWidthOld,
          QStringLiteral("a new paper color and width for new projects leave the open project as it was"),
          QStringLiteral("paper %1, width %2").arg(e ? e->palette().color(QPalette::Base).name() : QString()).arg(col->width()));

    // a new project gets the look for new projects, with the border
    BinderItem *docB = nullptr;
    auto b = makeProject(dir, QStringLiteral("New Harbor"), &docB);
    Project *pb = b.get();
    check(w.adoptProject(std::move(b), true), QStringLiteral("(a new project open)"));
    settle(300);
    pane = visiblePane(w);
    pane->showItems({docB});
    settle(400);
    const look::Values lb = lookFile(pb);
    check(lb.colors.value(QStringLiteral("editorPaper")).name() == QLatin1String("#ffffff") && lb.editorWidth == 620 && lb.border == 10,
          QStringLiteral("a new project gets the look for new projects: white paper, 620 px, a border of 10 px"),
          QString::fromUtf8(QJsonDocument(lb.toJson()).toJson(QJsonDocument::Compact)));
    v = pane->textView();
    col = columnOf(v);
    e = pane->currentEditor();
    const double zoom = pb->docs()->zoom();
    const int bpx = int(10 * zoom + 0.5);
    check(col && e && col->width() == e->width() + 2 * bpx && e->width() == int(620 * zoom),
          QStringLiteral("the border lies outside the text: the text keeps its width, the paper is 2 x 10 px wider"),
          QStringLiteral("paper %1 px, text %2 px, zoom %3").arg(col ? col->width() : -1).arg(e ? e->width() : -1).arg(zoom));
    if (col && e) {
        const QPoint inBorder(bpx / 2, e->mapTo(col, QPoint(0, e->height() / 3)).y());
        const QColor c = pixel(col, inBorder);
        check(c.name() == QLatin1String("#ffffff"), QStringLiteral("... and has the paper's color"), c.name());
        // a click in the border puts the cursor at the start of the line beside it
        QTextCursor cur = e->textCursor();
        cur.movePosition(QTextCursor::End);
        e->setTextCursor(cur);
        QTest::mouseClick(col, Qt::LeftButton, Qt::NoModifier, inBorder);
        settle(150);
        check(e->textCursor().atBlockStart() || e->textCursor().columnNumber() == 0,
              QStringLiteral("a click in the border puts the cursor at the start of the line beside it"),
              QStringLiteral("column %1").arg(e->textCursor().columnNumber()));
    }
    shot(&w, QStringLiteral("03-new-project-border"));
    // zoom: the border grows with the text
    pb->docs()->setZoom(2.0);
    settle(400);
    check(col && e && col->width() - e->width() == 40, QStringLiteral("at 200 % zoom the border is 2 x 20 px"),
          QStringLiteral("paper %1 px, text %2 px").arg(col ? col->width() : -1).arg(e ? e->width() : -1));
    pb->docs()->setZoom(1.0);
    settle(300);

    // File > Options > Appearance: the open project, or new projects
    {
        OptionsDialog dlg(&w);
        dlg.show();
        dlg.showSection(OptionsDialog::SAppearance, QStringLiteral("Main Editor"));
        settle(300);
        auto *scope = dlg.findChild<QComboBox *>(QStringLiteral("optLookScope"));
        auto *border = dlg.findChild<QSpinBox *>(QStringLiteral("optTextBorder"));
        auto *width = dlg.findChild<QSpinBox *>(QStringLiteral("optEditorWidth"));
        check(scope && border && width && dlg.lookScope() == OptionsDialog::ScopeProject && border->value() == 10 && width->value() == 620,
              QStringLiteral("Options > Appearance shows the open project's look first (width, border)"),
              QStringLiteral("scope %1").arg(dlg.lookScope()));
        shot(&dlg, QStringLiteral("04-options-look-of-this-project"));
        if (border && width && scope) {
            border->setValue(24);
            dlg.setOwnColor(QStringLiteral("editorPaper"), QColor(QStringLiteral("#f4ecd8")));
            dlg.setLookScope(OptionsDialog::ScopeNew);
            settle(100);
            check(border->value() == 10 && width->value() == 620 && dlg.shownColor(QStringLiteral("editorPaper")).name() == QLatin1String("#ffffff"),
                  QStringLiteral("... \"New projects\" shows the look for new projects"),
                  QStringLiteral("border %1, paper %2").arg(border->value()).arg(dlg.shownColor(QStringLiteral("editorPaper")).name()));
            width->setValue(560);
            dlg.setLookScope(OptionsDialog::ScopeProject);
            settle(100);
            check(border->value() == 24 && dlg.shownColor(QStringLiteral("editorPaper")).name() == QLatin1String("#f4ecd8"),
                  QStringLiteral("... switching back keeps what was changed for the project"));
            dlg.apply();
            settle(300);
            const look::Values now = lookFile(pb);
            check(now.border == 24 && now.colors.value(QStringLiteral("editorPaper")).name() == QLatin1String("#f4ecd8") && now.editorWidth == 620,
                  QStringLiteral("Apply: the project's border and paper go into the project"),
                  QString::fromUtf8(QJsonDocument(now.toJson()).toJson(QJsonDocument::Compact)));
            check(as.editorWidth == 560 && as.textBorder == 10 && as.ownColors.value(QStringLiteral("editorPaper")).name() == QLatin1String("#ffffff"),
                  QStringLiteral("... and the width for new projects into the settings, the rest of them unchanged"),
                  QStringLiteral("%1 px, border %2, paper %3").arg(as.editorWidth).arg(as.textBorder).arg(as.ownColors.value(QStringLiteral("editorPaper")).name()));
            check(e && e->palette().color(QPalette::Base).name() == QLatin1String("#f4ecd8") && col->width() == e->width() + 48,
                  QStringLiteral("... and the editor shows the project's new paper and border at once"),
                  QStringLiteral("paper %1, %2 vs %3").arg(e ? e->palette().color(QPalette::Base).name() : QString()).arg(col->width()).arg(e ? e->width() : -1));
        }
        dlg.close();
    }

    // Window > Themes: the theme for the program and this project; other projects keep theirs
    themeui::choose(&w, QStringLiteral("dark"), false);
    settle(300);
    check(lookFile(pb).theme == QLatin1String("dark") && as.theme == QLatin1String("dark"),
          QStringLiteral("Window > Themes sets the theme of the open project and of the program"));
    shot(&w, QStringLiteral("05-new-project-dark-theme"));
    themeui::choose(&w, QStringLiteral("light"), false);
    settle(200);

    // the older project again: still as it was
    QString err;
    auto again = Project::open(fileA, &err);
    check(again && w.adoptProject(std::move(again)), QStringLiteral("(the older project open again)"), err);
    settle(300);
    pane = visiblePane(w);
    Project *pa2 = look::project();
    BinderItem *doc = pa2 ? pa2->item(docAId) : nullptr;
    if (doc)
        pane->showItems({doc});
    settle(400);
    e = pane->currentEditor();
    col = columnOf(pane->textView());
    const look::Values la2 = pa2 ? lookFile(pa2) : look::Values();
    check(la2.border == 0 && la2.editorWidth == 500 && la2.colors.value(QStringLiteral("editorPaper")).name() == QLatin1String("#d1d495")
              && e && e->palette().color(QPalette::Base).name() == QLatin1String("#d1d495") && col && col->width() == e->width()
              && e->width() == textWidthOld,
          QStringLiteral("the older project opened again looks as before: its paper, its width, no border"),
          QString::fromUtf8(QJsonDocument(la2.toJson()).toJson(QJsonDocument::Compact)));
}

// ======================================================================= 4: right-click menus
static QMenu *subMenu(QMenu *m, const QString &name) { return m ? m->findChild<QMenu *>(name) : nullptr; }

static QAction *colorAction(QMenu *m, const QColor &c)
{
    if (!m)
        return nullptr;
    for (QAction *a : m->actions())
        if (a->data().value<QColor>().isValid() && a->data().value<QColor>().rgb() == c.rgb())
            return a;
    return nullptr;
}

static QAction *textAction(QMenu *m, const QString &text)
{
    if (!m)
        return nullptr;
    for (QAction *a : m->actions())
        if (a->text() == text)
            return a;
    return nullptr;
}

// Opens a menu that runs its own loop (exec), looks at it while it is open, then closes it.
static bool withMenu(const std::function<void()> &open, const std::function<void(QMenu *)> &inspect)
{
    bool seen = false;
    QTimer::singleShot(400, qApp, [&] {
        QMenu *m = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (m) {
            seen = true;
            inspect(m);
        }
        closePopups();
    });
    open();
    settle(200);
    closePopups();
    return seen;
}

static const QColor kGreen(44, 160, 44), kYellow(255, 221, 0), kBlue(31, 100, 200);

static void menuChecks(MainWindow &w)
{
    Project *p = look::project();
    EditorPane *pane = visiblePane(w);
    if (!p || !pane)
        return;
    BinderItem *doc = nullptr;
    for (BinderItem *it : p->allItems())
        if (it->type == ItemType::Text && !doc && p->isInDraft(it))
            doc = it;
    pane->showItems({doc});
    settle(400);
    TextEditor *e = pane->currentEditor();
    TextColumnView *v = pane->textView();
    QWidget *col = columnOf(v);
    if (!e || !col)
        return;
    e->setFocus();
    settle(100);

    // the text: Appearance with the editor's colors, width and border
    rightClick(e->viewport(), pointOf(e, 5));
    QMenu *m = openPopup();
    QMenu *lookM = subMenu(m, QStringLiteral("lookMenu"));
    check(lookM && subMenu(lookM, QStringLiteral("lookColor_editorPaper")) && subMenu(lookM, QStringLiteral("lookColor_editorText"))
              && subMenu(lookM, QStringLiteral("lookColor_editorMargin")) && subMenu(lookM, QStringLiteral("lookEditorWidth"))
              && subMenu(lookM, QStringLiteral("lookBorder")) && lookM->findChild<QAction *>(QStringLiteral("lookOptions")),
          QStringLiteral("right-click on the text: Appearance with paper, text and column colors, column width, border, options"));
    if (lookM) {
        QAction *whose = lookM->findChild<QAction *>(QStringLiteral("lookWhose"));
        check(whose && whose->text() == QLatin1String("Look of this project"), QStringLiteral("... it says the changes are for this project"),
              whose ? whose->text() : QString());
        lookM->popup(m->geometry().topRight());
        settle(200);
        shot(lookM, QStringLiteral("06-right-click-appearance"));
        QAction *yellow = colorAction(subMenu(lookM, QStringLiteral("lookColor_editorPaper")), kYellow);
        check(yellow != nullptr, QStringLiteral("... the paper's color offers the shared palette"));
        if (yellow)
            yellow->trigger();
    }
    closePopups();
    settle(300);
    check(lookFile(p).colors.value(QStringLiteral("editorPaper")).rgb() == kYellow.rgb()
              && e->palette().color(QPalette::Base).rgb() == kYellow.rgb(),
          QStringLiteral("choosing yellow paper there changes the project's paper at once"),
          e->palette().color(QPalette::Base).name());
    // the border from the menu
    rightClick(e->viewport(), pointOf(e, 5));
    m = openPopup();
    if (QAction *a20 = textAction(subMenu(subMenu(m, QStringLiteral("lookMenu")), QStringLiteral("lookBorder")), QStringLiteral("20 px")))
        a20->trigger();
    closePopups();
    settle(300);
    check(lookFile(p).border == 20 && col->width() == e->width() + int(40 * p->docs()->zoom() + 0.5),
          QStringLiteral("Border Beside the Text > 20 px in the menu: the border is 20 px"),
          QStringLiteral("border %1, paper %2, text %3").arg(lookFile(p).border).arg(col->width()).arg(e->width()));

    // beside the text: a menu of its own with the same entries
    {
        QWidget *container = v->widget();
        const QPoint beside = container->mapFrom(v->viewport(), QPoint(6, v->viewport()->height() / 3));
        rightClick(container, beside);
        QMenu *pm = openPopup();
        check(pm && pm->objectName() == QLatin1String("lookPopup") && subMenu(pm, QStringLiteral("lookColor_editorMargin")),
              QStringLiteral("right-click beside the text: the editor's look (also the color beside the column)"));
        if (QAction *a700 = textAction(subMenu(pm, QStringLiteral("lookEditorWidth")), QStringLiteral("700 px")))
            a700->trigger();
        closePopups();
        settle(300);
        check(lookFile(p).editorWidth == 700 && e->width() == qMin(int(700 * p->docs()->zoom()), v->viewport()->width() - 48),
              QStringLiteral("Text Column Width > 700 px there: the column is 700 px"),
              QStringLiteral("%1 px, text %2").arg(lookFile(p).editorWidth).arg(e->width()));
        // back to the theme's paper
        rightClick(container, beside);
        pm = openPopup();
        if (QAction *reset = pm ? pm->findChild<QAction *>(QStringLiteral("lookColorReset_editorPaper")) : nullptr)
            reset->trigger();
        closePopups();
        settle(300);
        check(!lookFile(p).colors.contains(QStringLiteral("editorPaper")), QStringLiteral("\"The Theme's Color\" takes the project's own paper color away"));
    }

    // the header bar
    if (QWidget *header = pane->findChild<QWidget *>(QStringLiteral("editorHeader"))) {
        QStringList seen;
        const bool opened = withMenu(
            [&] {
                QContextMenuEvent ce(QContextMenuEvent::Mouse, QPoint(header->width() - 60, 8),
                                     header->mapToGlobal(QPoint(header->width() - 60, 8)));
                QApplication::sendEvent(header, &ce);
            },
            [&](QMenu *hm) {
                QMenu *lm = subMenu(hm, QStringLiteral("lookMenu"));
                if (subMenu(lm, QStringLiteral("lookColor_headerBar")))
                    seen << QStringLiteral("headerBar");
                if (subMenu(lm, QStringLiteral("lookFont_headerTitle")))
                    seen << QStringLiteral("font");
                if (QAction *blue = colorAction(subMenu(lm, QStringLiteral("lookColor_headerBar")), kBlue))
                    blue->trigger();
            });
        settle(300);
        check(opened && seen.size() == 2, QStringLiteral("right-click on the header bar: its colors and its font"), seen.join(QLatin1Char(',')));
        check(lookFile(p).colors.value(QStringLiteral("headerBar")).rgb() == kBlue.rgb()
                  && theme::color(theme::Role::HeaderBar).rgb() == kBlue.rgb(),
              QStringLiteral("... a blue header bar for the project"));
    }

    // the binder
    {
        auto *binder = w.findChild<BinderPanel *>();
        bool found = false;
        const bool opened = withMenu([&] { emit binder->contextMenuRequested(binder->mapToGlobal(QPoint(40, 60))); },
                                     [&](QMenu *bm) {
                                         QMenu *lm = subMenu(bm, QStringLiteral("lookMenu"));
                                         found = subMenu(lm, QStringLiteral("lookColor_binderBackground"))
                                                 && subMenu(lm, QStringLiteral("lookFont_binder"))
                                                 && subMenu(lm, QStringLiteral("lookBinderIndent"));
                                         if (QAction *green = colorAction(subMenu(lm, QStringLiteral("lookColor_binderBackground")), kGreen))
                                             green->trigger();
                                     });
        settle(300);
        check(opened && found, QStringLiteral("right-click in the binder: its background, titles, font, indent and label colors"));
        check(theme::color(theme::Role::BinderBackground).rgb() == kGreen.rgb()
                  && lookFile(p).colors.value(QStringLiteral("binderBackground")).rgb() == kGreen.rgb(),
              QStringLiteral("... a green binder for the project"));
        shot(&w, QStringLiteral("07-binder-and-header-from-the-menus"));
    }

    // corkboard and outliner (the folder's documents)
    BinderItem *folder = doc ? doc->parent : nullptr;
    for (ViewMode mode : {ViewMode::Corkboard, ViewMode::Outliner}) {
        pane->showItems({folder});
        settle(300);
        pane->setViewMode(mode);
        settle(400);
        const QString key = mode == ViewMode::Corkboard ? QStringLiteral("corkboardBackground") : QStringLiteral("outlinerBackground");
        bool found = false;
        const bool opened = withMenu(
            [&] {
                if (mode == ViewMode::Corkboard)
                    emit pane->corkboard()->contextMenuRequested(pane->corkboard()->mapToGlobal(QPoint(20, 20)));
                else
                    emit pane->outliner()->contextMenuRequested(pane->outliner()->mapToGlobal(QPoint(20, 40)));
            },
            [&](QMenu *im) {
                QMenu *lm = subMenu(im, QStringLiteral("lookMenu"));
                found = subMenu(lm, QStringLiteral("lookColor_") + key) != nullptr;
                if (QAction *yellow = colorAction(subMenu(lm, QStringLiteral("lookColor_") + key), kYellow))
                    yellow->trigger();
            });
        settle(300);
        check(opened && found && lookFile(p).colors.value(key).rgb() == kYellow.rgb(),
              QStringLiteral("right-click on the %1: its colors; a yellow background for the project")
                  .arg(mode == ViewMode::Corkboard ? QStringLiteral("corkboard") : QStringLiteral("outliner")));
        shot(&w, mode == ViewMode::Corkboard ? QStringLiteral("08-corkboard-from-the-menu") : QStringLiteral("09-outliner-from-the-menu"));
    }
    pane->showItems({doc});
    settle(300);

    // the inspector: its background, and the notes
    if (auto *insp = w.findChild<Inspector *>()) {
        QContextMenuEvent ce(QContextMenuEvent::Mouse, QPoint(insp->width() / 2, insp->height() - 5),
                             insp->mapToGlobal(QPoint(insp->width() / 2, insp->height() - 5)));
        QApplication::sendEvent(insp, &ce);
        QMenu *im = openPopup();
        check(im && subMenu(im, QStringLiteral("lookColor_inspectorBackground")) && subMenu(im, QStringLiteral("lookColor_notesPaper"))
                  && subMenu(im, QStringLiteral("lookColor_synopsisPaper")),
              QStringLiteral("right-click on the inspector: its background, synopsis card and notes colors"));
        closePopups();
        TextEditor *notes = nullptr;
        for (TextEditor *t : insp->findChildren<TextEditor *>())
            if (t->isNotes() && t->isVisible())
                notes = t;
        if (notes) {
            rightClick(notes->viewport(), QPoint(20, 10));
            QMenu *nm = openPopup();
            check(subMenu(subMenu(nm, QStringLiteral("lookMenu")), QStringLiteral("lookColor_notesPaper")) != nullptr
                      && !subMenu(subMenu(nm, QStringLiteral("lookMenu")), QStringLiteral("lookColor_editorPaper")),
                  QStringLiteral("right-click in the notes: the inspector's colors, not the editor's"));
            closePopups();
        }
    }
    // composition mode: its own colors, in the text and beside it
    {
        auto *binder = w.findChild<BinderPanel *>();
        binder->select({doc}, true);
        settle(200);
        byKey(&w, QStringLiteral("composition"))->trigger();
        settle(500);
        QPointer<CompositionWindow> comp;
        for (QWidget *t : QApplication::topLevelWidgets())
            if (auto *cw = qobject_cast<CompositionWindow *>(t))
                if (cw->isVisible())
                    comp = cw;
        TextEditor *ce = nullptr;
        if (comp)
            for (TextEditor *t : comp->findChildren<TextEditor *>())
                if (t->isVisible() && !ce)
                    ce = t;
        bool text = false, beside = false;
        if (ce) {
            rightClick(ce->viewport(), QPoint(20, 10));
            QMenu *cm = openPopup();
            QMenu *lm = subMenu(cm, QStringLiteral("lookMenu"));
            text = subMenu(lm, QStringLiteral("lookColor_compositionPaper")) && !subMenu(lm, QStringLiteral("lookColor_editorPaper"));
            closePopups();
            if (auto *cv = comp->findChild<TextColumnView *>()) {
                QWidget *container = cv->widget();
                rightClick(container, container->mapFrom(cv->viewport(), QPoint(5, cv->viewport()->height() / 2)));
                QMenu *pm = openPopup();
                beside = subMenu(pm, QStringLiteral("lookColor_compositionBackground")) != nullptr;
                closePopups();
            }
        }
        check(text && beside, QStringLiteral("composition mode: the right-click menus offer its own colors (text and background)"),
              QStringLiteral("window %1, in the text %2, beside %3").arg(comp != nullptr).arg(text).arg(beside));
        if (comp) {
            QTest::keyClick(comp, Qt::Key_F11);
            settle(300);
        }
    }
    // the look of the project, reopened: still there (covered by the file checks above)
    look::Values back = lookFile(p);
    back.colors.remove(QStringLiteral("binderBackground"));
    back.colors.remove(QStringLiteral("headerBar"));
    back.colors.remove(QStringLiteral("corkboardBackground"));
    back.colors.remove(QStringLiteral("outlinerBackground"));
    look::change(back);
    settle(200);
}

// ======================================================================= 6: style changes reach all text of the style
static QTextCharFormat formatAt(QTextDocument *doc, const QString &word)
{
    const int at = int(doc->toPlainText().indexOf(word));
    QTextCursor c(doc);
    c.setPosition(qMax(0, at + 1));
    return c.charFormat();
}
static QTextBlockFormat blockAt(QTextDocument *doc, const QString &word)
{
    const int at = int(doc->toPlainText().indexOf(word));
    return doc->findBlock(qMax(0, at)).blockFormat();
}

// A heading as an import brings it: the style's name, but a font, size, color, alignment and
// spacing of its own in every piece and paragraph.
static BinderItem *styledDocument(Project *p, const QString &title, const QString &heading)
{
    const Formatting &f = p->formatting;
    BinderItem *d = p->model()->createItem(p->draft(), -1, ItemType::Text, title);
    QTextCursor c(p->docs()->text(d->id));
    direct::ParaSpec ps;
    ps.bits = direct::After | direct::Align;
    ps.after = 40;
    ps.align = Qt::AlignRight;
    QTextBlockFormat hb = display::blockFormatFor(f, QStringLiteral("Heading 1"));
    direct::applyParaSpec(hb, ps);
    c.setBlockFormat(hb);
    QTextCharFormat hc = display::charFormatFor(f, QStringLiteral("Heading 1"));
    direct::CharSpec cs;
    cs.family = QStringLiteral("DejaVu Sans");
    cs.size = 15;
    cs.color = QColor(QStringLiteral("#7a1010"));
    direct::mergeCharSpec(hc, cs);
    c.insertText(heading, hc);
    c.insertBlock(display::blockFormatFor(f, QStringLiteral("Heading 1")), display::charFormatFor(f, QStringLiteral("Heading 1")));
    c.insertText(QStringLiteral("Calm Water"));
    c.insertBlock(display::normalBlockFormat(f), display::charFormatFor(f, QString()));
    c.insertText(QStringLiteral("Body text with a "));
    QTextCharFormat red = display::charFormatFor(f, QString());
    direct::CharSpec rs;
    rs.color = QColor(QStringLiteral("#cc0000"));
    rs.family = QStringLiteral("DejaVu Sans Mono");
    direct::mergeCharSpec(red, rs);
    c.insertText(QStringLiteral("crimson"), red);
    c.insertText(QStringLiteral(" word."), display::charFormatFor(f, QString()));
    return d;
}

static void styleChecks(MainWindow &w)
{
    Project *p = look::project();
    EditorPane *pane = visiblePane(w);
    if (!p || !pane)
        return;
    BinderItem *shown = styledDocument(p, QStringLiteral("Storm (shown)"), QStringLiteral("North Gale"));
    BinderItem *other = styledDocument(p, QStringLiteral("Storm (not shown)"), QStringLiteral("South Gale"));
    QString err;
    p->save(&err);
    pane->showItems({shown});
    settle(400);
    QTextDocument *t1 = p->docs()->text(shown->id);
    const direct::CharSpec before = direct::charSpec(formatAt(t1, QStringLiteral("North")));
    check(before.family == QLatin1String("DejaVu Sans") && qAbs(before.size - 15) < 0.01
              && (direct::paraSpec(blockAt(t1, QStringLiteral("North"))).bits & direct::After),
          QStringLiteral("(a heading with a font, size, color, alignment and spacing of its own, as imported)"));
    const int snapsBefore = int(snapshots::list(p, other->id).size());

    // Heading 1 gets another font, size, color, spacing after and alignment
    StyleSheet sheet = styles::sheet(p->formatting);
    const int i = sheet.indexOf(QStringLiteral("Heading 1"));
    check(i >= 0, QStringLiteral("(the style Heading 1)"));
    if (i < 0)
        return;
    Style &h = sheet.styles[i];
    h.chars.family = QStringLiteral("Liberation Sans");
    h.chars.size = 22;
    h.chars.color = QColor(QStringLiteral("#1f64c8"));
    h.para.bits |= direct::After | direct::Align;
    h.para.after = 18;
    h.para.align = Qt::AlignHCenter;
    check(w.commitStyles(sheet), QStringLiteral("(Heading 1 changed)"));
    settle(400);
    shot(&w, QStringLiteral("10-style-change-reaches-every-heading"));

    for (const auto &pr : {qMakePair(t1, QStringLiteral("North")), qMakePair(p->docs()->text(other->id), QStringLiteral("South"))}) {
        QTextDocument *t = pr.first;
        const QTextCharFormat cf = formatAt(t, pr.second);
        const QTextBlockFormat bf = blockAt(t, pr.second);
        const QString where = t == t1 ? QStringLiteral("shown") : QStringLiteral("not shown");
        // what is left once the style's own look is taken away: set by hand
        const direct::CharSpec own = direct::charSpec(
            styles::withoutStyles(cf, styles::find(p->formatting, QStringLiteral("Heading 1")), p->formatting));
        check(cf.fontFamilies().toStringList().value(0) == QLatin1String("Liberation Sans") && qAbs(cf.fontPointSize() - 22) < 0.01
                  && cf.foreground().color().name() == QLatin1String("#1f64c8") && own.isEmpty(),
              QStringLiteral("a heading with its own font, size and color takes the style's new ones (%1 document)").arg(where),
              QStringLiteral("%1 %2 pt %3, own: %4").arg(cf.fontFamilies().toStringList().value(0)).arg(cf.fontPointSize())
                  .arg(cf.foreground().color().name()).arg(direct::toCss(own)));
        check(qAbs(bf.bottomMargin() - 18) < 0.5 && (bf.alignment() & Qt::AlignHCenter)
                  && !(direct::paraSpec(bf).bits & (direct::After | direct::Align)),
              QStringLiteral("... and its spacing after and alignment (%1 document)").arg(where),
              QStringLiteral("after %1, align %2, own bits %3").arg(bf.bottomMargin()).arg(int(bf.alignment())).arg(direct::paraSpec(bf).bits));
        const QTextCharFormat calm = formatAt(t, QStringLiteral("Calm"));
        check(calm.fontFamilies().toStringList().value(0) == QLatin1String("Liberation Sans") && qAbs(calm.fontPointSize() - 22) < 0.01,
              QStringLiteral("... the heading without own formatting as well (%1 document)").arg(where));
        const direct::CharSpec crimson = direct::charSpec(formatAt(t, QStringLiteral("crimson")));
        check(crimson.color.name() == QLatin1String("#cc0000") && crimson.family == QLatin1String("DejaVu Sans Mono"),
              QStringLiteral("... body text keeps the color and font set by hand (%1 document)").arg(where));
    }
    check(int(snapshots::list(p, other->id).size()) == snapsBefore + 1,
          QStringLiteral("a document that changes gets a snapshot first (\"Before the style ... changed\")"));
    // in the file as well
    p->save(&err);
    QFile md(p->docs()->textPath(other->id));
    const QString stored = md.open(QIODevice::ReadOnly) ? QString::fromUtf8(md.readAll()) : QString();
    check(stored.contains(QLatin1String("South Gale")) && !stored.contains(QLatin1String("DejaVu Sans;"))
              && !stored.contains(QLatin1String("DejaVu Sans'")) && !stored.contains(QLatin1String("#7a1010")),
          QStringLiteral("... and the saved file no longer names the heading's own font and color"), stored.left(400));

    // bold is taken away from the style: the headings are no longer bold
    sheet = styles::sheet(p->formatting);
    Style &h2 = sheet.styles[sheet.indexOf(QStringLiteral("Heading 1"))];
    const bool wasBold = h2.bold;
    h2.bold = false;
    w.commitStyles(sheet);
    settle(300);
    check(wasBold && formatAt(t1, QStringLiteral("North")).fontWeight() < QFont::DemiBold
              && formatAt(t1, QStringLiteral("Calm")).fontWeight() < QFont::DemiBold,
          QStringLiteral("bold taken away from the style: no heading of the style is bold any more"),
          QStringLiteral("was bold %1, weight %2").arg(wasBold).arg(formatAt(t1, QStringLiteral("North")).fontWeight()));
}

// ======================================================================= 5: project settings to a file and back
static int labelId(Project *p, const QString &name)
{
    for (const Label &l : p->labels)
        if (l.name == name)
            return l.id;
    return -1;
}

static QStringList labelNames(Project *p)
{
    QStringList out;
    for (const Label &l : p->labels)
        out << l.name;
    return out;
}

// Loads a settings file through the window's command; answer(dialog) makes the choices
// (null: the dialog is not expected). Returns whether the dialog was shown.
static bool loadSettings(MainWindow &w, const QString &file, const std::function<void(SettingsImportDialog *)> &answer,
                         QString *sameNotice = nullptr)
{
    bool shown = false;
    QTimer::singleShot(400, qApp, [&] {
        QWidget *m = QApplication::activeModalWidget();
        if (auto *d = qobject_cast<SettingsImportDialog *>(m)) {
            shown = true;
            d->grab().save(outDir + QLatin1String("/11-load-project-settings.png"));
            if (answer)
                answer(d);
            else
                d->reject();
        } else if (auto *box = qobject_cast<QMessageBox *>(m)) {
            if (sameNotice)
                *sameNotice = box->objectName();
            box->accept();
        }
    });
    qputenv("QB_NO_DIALOGS", "1");
    w.importProjectSettings(file);
    qunsetenv("QB_NO_DIALOGS");
    settle(300);
    return shown;
}

static void settingsChecks(MainWindow &w, const QString &dir)
{
    Project *p = look::project();
    if (!p)
        return;
    QString err;
    // settings of the project: labels, a custom field, a keyword, formatting, a compile replacement, the look
    p->labels.append({p->nextLabelId(), QStringLiteral("Idea"), QColor(QStringLiteral("#d62728"))});
    p->labels.append({p->nextLabelId(), QStringLiteral("Done"), QColor(QStringLiteral("#2ca02c"))});
    CustomField pov;
    pov.id = newId();
    pov.title = QStringLiteral("Point of View");
    pov.type = CustomFieldType::List;
    pov.options = {QStringLiteral("Mara"), QStringLiteral("Tom")};
    p->customFields.append(pov);
    p->keywords.append({newId(), QStringLiteral("Storm"), QColor(QStringLiteral("#1f64c8")), QString()});
    CompileReplacement r;
    r.find = QStringLiteral("--");
    r.replace = QStringLiteral("\u2014");
    p->compile.replacements << r;
    p->notifySettingsChanged();
    look::Values lv = look::active();
    lv.colors.insert(QStringLiteral("editorPaper"), QColor(QStringLiteral("#f4ecd8")));
    lv.border = 12;
    look::change(lv);
    settle(200);
    const double sizeBefore = p->formatting.fontSize;

    const QString file = dir + QLatin1String("/Old Lighthouse settings.qbsettings");
    check(w.exportProjectSettings(file), QStringLiteral("Project > Save Project Settings to File writes the file"));
    QJsonObject doc;
    check(settingsfile::load(file, &doc, &err), QStringLiteral("... a settings file that can be read again"), err);
    const QJsonObject sec = doc.value(QStringLiteral("sections")).toObject();
    QStringList inFile;
    for (const QJsonValue &l : sec.value(QStringLiteral("labels")).toObject().value(QStringLiteral("items")).toArray())
        inFile << l.toObject().value(QStringLiteral("name")).toString();
    check(sec.size() == settingsfile::SectionCount && inFile.contains(QStringLiteral("Idea")) && inFile.contains(QStringLiteral("Done"))
              && sec.value(QStringLiteral("look")).toObject().value(QStringLiteral("border")).toInt() == 12
              && sec.value(QStringLiteral("customMetadata")).toArray().size() >= 1,
          QStringLiteral("... with every section: labels by name, custom metadata, the look (border 12 px) ..."),
          QString::fromUtf8(QJsonDocument(sec.value(QStringLiteral("labels")).toObject()).toJson(QJsonDocument::Compact)));

    // the same file into the unchanged project: nothing to do
    QString notice;
    const bool shownSame = loadSettings(w, file, nullptr, &notice);
    check(!shownSame && notice == QLatin1String("settingsSameNotice"),
          QStringLiteral("loading it into the unchanged project: \"the same as in the file\", nothing changes"), notice);

    // the project changes meanwhile: another label used by a document, Idea gone, a larger font, no border
    p->labels.removeAt(labelNames(p).indexOf(QStringLiteral("Idea")));
    p->labels.append({p->nextLabelId(), QStringLiteral("Revise"), QColor(QStringLiteral("#ffdd00"))});
    BinderItem *doc1 = nullptr;
    for (BinderItem *it : p->allItems())
        if (it->type == ItemType::Text && p->isInDraft(it) && !doc1)
            doc1 = it;
    doc1->label = labelId(p, QStringLiteral("Revise"));
    p->notifyItemChanged(doc1);
    p->statuses.append({p->nextStatusId(), QStringLiteral("Polished")});
    p->notifySettingsChanged();
    {
        qb::Formatting f = p->formatting;
        f.fontSize = sizeBefore + 3;
        p->formatting = f;
    }
    lv = look::active();
    lv.border = 0;
    look::change(lv);
    settle(200);

    QStringList table;
    QString labelDetails;
    const bool shown = loadSettings(w, file, [&](SettingsImportDialog *d) {
        for (int i = 0; i < settingsfile::SectionCount; ++i)
            if (d->differs(settingsfile::Section(i)))
                table << settingsfile::key(settingsfile::Section(i));
        labelDetails = d->details(settingsfile::Labels);
        d->setChoice(settingsfile::Labels, SettingsImportDialog::TakeFile);
        d->setChoice(settingsfile::Look, SettingsImportDialog::TakeFile);
        d->setChoice(settingsfile::Formatting, SettingsImportDialog::KeepProject);
        d->setChoice(settingsfile::Status, SettingsImportDialog::UseDefault);
        d->accept();
    });
    check(shown && table.contains(QStringLiteral("labels")) && table.contains(QStringLiteral("look"))
              && table.contains(QStringLiteral("formatting")) && table.contains(QStringLiteral("status"))
              && !table.contains(QStringLiteral("keywords")),
          QStringLiteral("the project changed since: the window lists the sections that differ (labels, status, formatting, look), not the others"),
          table.join(QLatin1Char(',')));
    check(labelDetails.contains(QLatin1String("only in the file: Idea")) && labelDetails.contains(QLatin1String("only in the project: Revise")),
          QStringLiteral("... and says what differs (\"only in the file: Idea; only in the project: Revise\")"), labelDetails);
    const QStringList names = labelNames(p);
    check(names.contains(QStringLiteral("Idea")) && names.contains(QStringLiteral("Revise")) && doc1->label == labelId(p, QStringLiteral("Revise")),
          QStringLiteral("Labels taken from the file: Idea is back, Revise is kept because a document has it"), names.join(QLatin1Char(',')));
    check(w.lastSettingsNotes().join(QLatin1Char(' ')).contains(QLatin1String("Revise")),
          QStringLiteral("... and the message names the label that was kept"), w.lastSettingsNotes().join(QLatin1Char(' ')));
    check(qAbs(p->formatting.fontSize - (sizeBefore + 3)) < 0.01, QStringLiteral("Formatting kept as the project has it (the choice \"Keep the Project's\")"));
    check(look::active().border == 12 && look::active().colors.value(QStringLiteral("editorPaper")).name() == QLatin1String("#f4ecd8"),
          QStringLiteral("the look taken from the file: border 12 px and the paper color again"));
    QStringList statusNames;
    for (const qb::Status &st : p->statuses)
        statusNames << st.name;
    check(!statusNames.contains(QStringLiteral("Polished")) && statusNames.size() >= 3,
          QStringLiteral("Status \"Quillbinder default\": the statuses a new project has"), statusNames.join(QLatin1Char(',')));
    const QStringList before = QDir(p->settingsDir()).entryList({QStringLiteral("settings-before-loading-*.qbsettings")}, QDir::Files);
    check(before.size() == 1, QStringLiteral("the settings as they were are kept in the project's settings folder (to go back)"),
          before.join(QLatin1Char(',')));

    // the same file into another project: labels by name, styles the text uses are kept
    BinderItem *docB = nullptr;
    auto other = makeProject(dir, QStringLiteral("Far Shore"), &docB);
    Project *pb = other.get();
    pb->labels = {{1, QStringLiteral("Concept"), QColor(QStringLiteral("#888888"))}};
    {
        // a paragraph style of its own that the text uses, which the file does not know
        StyleSheet sheet = styles::sheet(pb->formatting);
        Style letter;
        letter.name = QStringLiteral("Letter");
        letter.italic = true;
        sheet.styles << letter;
        pb->formatting = styles::withSheet(pb->formatting, sheet);
        QTextCursor c(pb->docs()->text(docB->id));
        QTextBlockFormat bf = display::blockFormatFor(pb->formatting, QStringLiteral("Letter"));
        c.setBlockFormat(bf);
    }
    pb->save(&err);
    check(w.adoptProject(std::move(other), true), QStringLiteral("(another project open)"));
    settle(300);
    loadSettings(w, file, [&](SettingsImportDialog *d) {
        d->setAll(SettingsImportDialog::TakeFile);
        d->accept();
    });
    Project *now = look::project();
    const QStringList nb = now ? labelNames(now) : QStringList();
    bool customOk = false;
    for (const CustomField &f : now ? now->customFields : QList<CustomField>())
        customOk = customOk || (f.title == QLatin1String("Point of View") && f.options.size() == 2);
    check(now && nb.contains(QStringLiteral("Idea")) && nb.contains(QStringLiteral("Done")) && !nb.contains(QStringLiteral("Concept")) && customOk,
          QStringLiteral("into another project: the file's labels (an unused label of its own goes) and custom metadata"),
          nb.join(QLatin1Char(',')));
    check(now && styles::find(now->formatting, QStringLiteral("Letter")) != nullptr,
          QStringLiteral("... a style its text uses stays although the file does not have it"));
    check(now && now->compile.replacements.size() == 1 && now->compile.replacements.first().find == QLatin1String("--"),
          QStringLiteral("... the compile replacements of the file"));
    shot(&w, QStringLiteral("12-settings-loaded-into-another-project"));
}

// The built-in themes must not change in an update (a project that uses one would change its
// look): their colors, worked out as the program shows them, are recorded here. A new look
// goes into a new theme or into the look for new projects.
static void builtInThemesUnchanged()
{
    QStringList lines;
    for (const theme::Entry &te : theme::available()) {
        if (te.kind != theme::Kind::BuiltIn || te.id == QLatin1String("system"))
            continue;
        theme::Theme t;
        if (!theme::load(te.id, &t))
            continue;
        const QMap<theme::Role, QColor> c = theme::resolvedColors(t, {});
        QStringList parts;
        for (const theme::RoleInfo &ri : theme::roles())
            parts << QLatin1String(ri.key) + QLatin1Char('=') + c.value(ri.role).name();
        lines << te.id + QLatin1Char(':') + parts.join(QLatin1Char(','));
    }
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(lines.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha256).toHex());
    info(QStringLiteral("built-in themes: %1, digest %2").arg(lines.size()).arg(digest));
    check(digest == QLatin1String(kThemesDigest), QStringLiteral("the built-in themes look as they did in 0.40.1 (an update must not change them)"),
          digest);
}

// ======================================================================= main
int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV0402Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV0402Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);

    // holds for 0.40.2 or newer (0.40.3)
    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.40.2")) >= 0
              && QLatin1String(QB_APP_VERSION_SHOWN).endsWith(QLatin1String(" alpha")),
          QStringLiteral("version 0.40.2 or newer, shown with \"alpha\""), QStringLiteral(QB_APP_VERSION_SHOWN));
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
    auto proj = templates::createProject(blank, work.path(), QStringLiteral("Harbor Light"), &err);
    check(proj != nullptr, QStringLiteral("(project made)"), err);
    if (!proj)
        return 1;
    Project *p = proj.get();
    BinderItem *ch = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Chapter One"));
    BinderItem *longDoc = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("The Long Night"));
    {
        QTextCursor c(p->docs()->text(longDoc->id));
        for (int i = 1; i <= 140; ++i) {
            if (i > 1)
                c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
            c.insertText(QStringLiteral("Paragraph %1: the keeper climbed the stairs at dusk, trimmed the wick and lit the "
                                        "lamp while the storm came in from the west.")
                             .arg(i));
        }
    }
    BinderItem *shortDoc = p->model()->createItem(ch, -1, ItemType::Text, QStringLiteral("Dawn"));
    {
        QTextCursor c(p->docs()->text(shortDoc->id));
        c.insertText(QStringLiteral("The lamp went out at dawn."));
    }
    p->save(&err);
    // QB_TEST_KEEP=<folder>: a copy of this project for a run with the real AppImage (end to end)
    if (qEnvironmentVariableIsSet("QB_TEST_KEEP")) {
        const QString keep = qEnvironmentVariable("QB_TEST_KEEP");
        QDir().mkpath(keep);
        const QString src = p->folderPath();
        const QString dst = QDir(keep).filePath(QFileInfo(src).fileName());
        QDir(dst).removeRecursively();
        QDirIterator it(src, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            const QString to = dst + f.mid(src.size());
            QDir().mkpath(QFileInfo(to).absolutePath());
            QFile::copy(f, to);
        }
        info(QStringLiteral("project kept in %1").arg(dst));
    }

    auto *wp = new MainWindow;
    MainWindow &w = *wp;
    w.move(0, 0);
    w.resize(1400, 950);
    w.show();
    w.activateWindow();
    (void)QTest::qWaitForWindowActive(&w, 2000);
    check(w.adoptProject(std::move(proj)), QStringLiteral("(project open)"));
    settle(400);
    EditorPane *pane = nullptr;
    for (EditorPane *ep : w.findChildren<EditorPane *>())
        if (ep->isVisible() && !pane)
            pane = ep;
    check(pane != nullptr, QStringLiteral("(editor pane)"));
    if (pane) {
        rightClickChecks(w, pane, longDoc);
        clickBelowShortText(pane, shortDoc);
    }
    builtInThemesUnchanged();
    lookChecks(w, work.path());
    menuChecks(w);
    styleChecks(w);
    settingsChecks(w, work.path());

    settle(200);
    std::printf("\n%d failure(s)\n", failures);
    delete wp;
    return failures;
}
