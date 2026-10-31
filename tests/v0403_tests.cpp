// Scripted run for version 0.40.3 (private line):
//  1. A document without a name ("Untitled") takes the first line of its text as title and
//     follows it until it is named by hand (ui/MainWindowTitles.cpp).
//  2. "No Style" in the Styles panel: Redefine From Selection makes the formatting at the
//     cursor the project's main text style; all text without a style follows.
//  3. On a misspelled word the right-click menu is short: suggestions, Add, Ignore and the
//     other commands in one submenu.
//  4. The format bar shows what the editor shows: bold from a style lights Bold, and Bold
//     switches what is seen; fonts, sizes, alignment, line spacing and the style of a
//     selection that differs within it leave the field empty.
//  5. Dialogs started from the Styles panel open above it (they belong to the panel).
//  6. Left, centered, right or justified main text (File > Options > Editing > Formatting,
//     Project > Project Settings > Formatting) instead of "Justify text".
//  7. The style list of the format bar and the right-click menus: the entries and their order
//     can be chosen; more than ten scroll; the style commands stay below the list.
// Saves screenshots into the folder given as first argument. Exit code = number of failed
// checks.
//
//   xvfb-run -a ./qb_v0403_tests /tmp/shots
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "core/SettingsFile.h"
#include "compile/PluginManager.h"
#include "text/DirectFormat.h"
#include "text/DisplayFormat.h"
#include "text/Styles.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"
#include "ui/AlignButtons.h"
#include "ui/AppSettings.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/OptionsDialog.h"
#include "ui/ProjectSettingsDialog.h"
#include "ui/SpellChecker.h"
#include "ui/StyleDialogs.h"
#include "ui/StyleList.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include "AppInfo.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontComboBox>
#include <QJsonArray>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QWidgetAction>
#include <QWindow>
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

static QMenu *openPopup()
{
    for (int i = 0; i < 30 && !QApplication::activePopupWidget(); ++i)
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

// a right-click as the desktop sends it
static void rightClick(QWidget *w, const QPoint &pos)
{
    QTest::mousePress(w, Qt::RightButton, Qt::NoModifier, pos);
    QContextMenuEvent ce(QContextMenuEvent::Mouse, pos, w->mapToGlobal(pos), Qt::NoModifier);
    QApplication::sendEvent(w, &ce);
    settle(80);
    QTest::mouseRelease(w, Qt::RightButton, Qt::NoModifier, pos);
    settle(60);
}

static QPoint pointOf(TextEditor *e, int position)
{
    QTextCursor c(e->document());
    c.setPosition(position);
    const QRect r = e->cursorRect(c);
    return QPoint(r.left() + 2, r.center().y());
}

static EditorPane *visiblePane(MainWindow &w)
{
    for (EditorPane *ep : w.findChildren<EditorPane *>())
        if (ep->isVisible())
            return ep;
    return nullptr;
}

// selects `text` (or puts the cursor into it when `select` is false)
static void selectText(TextEditor *e, const QString &text, bool select = true)
{
    const int at = int(e->document()->toPlainText().indexOf(text));
    QTextCursor c(e->document());
    if (at < 0) {
        info(QStringLiteral("(text not found: %1)").arg(text));
        return;
    }
    // the format bar follows the editor that has the keyboard; somewhere else first, so that
    // it is refreshed even for the same selection
    e->window()->activateWindow(); // no window manager here: after a dialog the window is not active by itself
    e->setFocus();
    settle(60);
    c.movePosition(QTextCursor::End);
    e->setTextCursor(c);
    settle(60);
    c.setPosition(select ? at : at + 1);
    if (select)
        c.setPosition(at + int(text.size()), QTextCursor::KeepAnchor);
    e->setTextCursor(c);
    settle(120);
}

static QTextBlock blockWith(QTextDocument *d, const QString &text)
{
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
        if (b.text().contains(text))
            return b;
    return QTextBlock();
}

static QTextCharFormat formatAt(QTextDocument *d, const QString &text)
{
    const int at = int(d->toPlainText().indexOf(text));
    QTextCursor c(d);
    c.setPosition(qMax(0, at) + 1);
    return c.charFormat();
}

static QString titleShown(Project *p, BinderItem *it)
{
    return p->model()->data(p->model()->indexFor(it), Qt::DisplayRole).toString();
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

// runs `f` with the modal dialog that `start` opens; `start` returns when it is closed
static void withModal(const std::function<void()> &start, const std::function<void(QWidget *)> &f, int waitMs = 400)
{
    bool seen = false;
    QTimer::singleShot(waitMs, [&] {
        QWidget *m = QApplication::activeModalWidget();
        seen = m != nullptr;
        f(m);
        if (m && m->isVisible()) {
            if (auto *d = qobject_cast<QDialog *>(m))
                d->reject();
            else
                m->close();
        }
    });
    start();
    settle(100);
    if (!seen)
        info(QStringLiteral("(no dialog opened)"));
}

// ======================================================================= 1: titles from the first line
static void titleChecks(MainWindow &w, Project *p, BinderItem *untitled, BinderItem *empty, BinderItem *named)
{
    check(untitled->title == QLatin1String("Of How to Learn to Hold Back. A Bit.") && untitled->autoTitle
              && titleShown(p, untitled) == untitled->title,
          QStringLiteral("a document named \"Untitled\" with text takes its first line as title when the project opens"),
          untitled->title);
    check(empty->title == QLatin1String("Untitled") && !empty->autoTitle && named->title == QLatin1String("Chapter Notes")
              && !named->autoTitle,
          QStringLiteral("an empty \"Untitled\" document and a document with a name keep their titles"),
          empty->title + QLatin1String(" / ") + named->title);

    EditorPane *pane = visiblePane(w);
    if (!pane)
        return;
    // typing into an empty document
    pane->showItems({empty});
    settle(400);
    TextEditor *e = pane->currentEditor();
    if (!e)
        return;
    e->setFocus();
    QTest::keyClicks(e, QStringLiteral("Harbor at dusk"));
    QTest::keyClick(e, Qt::Key_Return);
    QTest::keyClicks(e, QStringLiteral("The boats came in."));
    settle(700);
    const bool typed = empty->title == QLatin1String("Harbor at dusk") && empty->autoTitle;
    QLineEdit *header = nullptr;
    for (QLineEdit *le : pane->findChildren<QLineEdit *>())
        if (le->placeholderText() == QLatin1String("Untitled") && le->isVisible())
            header = le;
    check(typed && (!header || header->text() == QLatin1String("Harbor at dusk")),
          QStringLiteral("typing into an \"Untitled\" document gives it its first line as title (binder and header)"),
          empty->title + QLatin1String(" / ") + (header ? header->text() : QStringLiteral("(no header field)")));
    shot(&w, QStringLiteral("01-title-from-first-line"));
    // the first line changes: the title follows
    QTextCursor c(e->document());
    c.setPosition(0);
    e->setTextCursor(c);
    QTest::keyClicks(e, QStringLiteral("Old "));
    settle(700);
    check(empty->title == QLatin1String("Old Harbor at dusk"), QStringLiteral("the title follows a change of the first line"),
          empty->title);
    // a long first line is cut at a word
    {
        QTextDocument d;
        d.setPlainText(QStringLiteral("The universe was still quite young and it was a phase of peace and quiet for all of them\nNext"));
        const QString t = text::firstLineTitle(&d);
        const QString line = d.firstBlock().text();
        check(t.size() <= 60 && t.size() >= 30 && line.startsWith(t) && line.at(t.size()) == QLatin1Char(' '),
              QStringLiteral("a long first line is cut to at most 60 characters, at a word"), t);
    }
    // inline annotations and footnotes do not count; a first line without words is skipped
    {
        QTextDocument d;
        QTextCursor dc(&d);
        QTextCharFormat note;
        note.setProperty(props::NoteKind, int(NoteKind::InlineAnnotation));
        dc.insertText(QStringLiteral("check the date"), note);
        dc.insertBlock();
        QTextCharFormat fn;
        fn.setProperty(props::NoteKind, int(NoteKind::InlineFootnote));
        dc.insertText(QStringLiteral("Real start "), QTextCharFormat());
        dc.insertText(QStringLiteral("a footnote"), fn);
        dc.insertText(QStringLiteral("of the text"), QTextCharFormat());
        const QString t = text::firstLineTitle(&d);
        check(t == QLatin1String("Real start of the text"),
              QStringLiteral("inline annotations and footnotes are left out; a line with nothing else is skipped"), t);
    }
    // named by hand: the title stays
    p->model()->setData(p->model()->indexFor(empty), QStringLiteral("My Harbor"), Qt::EditRole);
    c.setPosition(0);
    e->setTextCursor(c);
    QTest::keyClicks(e, QStringLiteral("Very "));
    settle(700);
    check(empty->title == QLatin1String("My Harbor") && !empty->autoTitle,
          QStringLiteral("a title changed by hand no longer follows the text"), empty->title);
    // all text taken away: "Untitled" again
    p->model()->setData(p->model()->indexFor(empty), QStringLiteral("Untitled"), Qt::EditRole);
    e->selectAll();
    QTest::keyClicks(e, QStringLiteral("Lighthouse keeper"));
    settle(700);
    const bool again = empty->title == QLatin1String("Lighthouse keeper") && empty->autoTitle;
    e->selectAll();
    QTest::keyClick(e, Qt::Key_Delete);
    settle(700);
    check(again && empty->title == QLatin1String("Untitled") && !empty->autoTitle,
          QStringLiteral("renamed to \"Untitled\" by hand it follows the text again; without text it is \"Untitled\""),
          empty->title);
    // kept in the project file
    QTest::keyClicks(e, QStringLiteral("Keeper of the light"));
    settle(700);
    QString err;
    p->save(&err);
    QFile f(p->indexPath());
    f.open(QIODevice::ReadOnly);
    const QString xml = QString::fromUtf8(f.readAll());
    auto again2 = Project::open(p->folderPath(), &err);
    BinderItem *re = again2 ? again2->item(empty->id) : nullptr;
    check(xml.contains(QLatin1String("autoTitle=\"1\"")) && re && re->title == QLatin1String("Keeper of the light") && re->autoTitle,
          QStringLiteral("the title and that it follows the text are kept in the project file"), err);
}

// ======================================================================= 4: the format bar
static void formatBarChecks(MainWindow &w, Project *p, BinderItem *doc)
{
    EditorPane *pane = visiblePane(w);
    if (!pane)
        return;
    pane->showItems({doc});
    settle(400);
    TextEditor *e = pane->currentEditor();
    QAction *bold = byKey(&w, QStringLiteral("bold"));
    QAction *italic = byKey(&w, QStringLiteral("italic"));
    QAction *center = byKey(&w, QStringLiteral("alignCenter"));
    QAction *left = byKey(&w, QStringLiteral("alignLeft"));
    auto *combo = w.findChild<QComboBox *>(QStringLiteral("styleCombo"));
    QDoubleSpinBox *size = nullptr;
    for (QDoubleSpinBox *sb : w.findChildren<QDoubleSpinBox *>())
        if (sb->toolTip().contains(QLatin1String("Font size")))
            size = sb;
    QFontComboBox *font = nullptr;
    for (QFontComboBox *fc : w.findChildren<QFontComboBox *>())
        if (fc->toolTip().contains(QLatin1String("Font of the selected")))
            font = fc;
    check(e && bold && italic && center && left && combo && size && font, QStringLiteral("(editor and format bar)"));
    if (!e || !bold || !italic || !center || !left || !combo || !size || !font)
        return;
    e->setFocus();
    selectText(e, QStringLiteral("Pieces"), false);
    e->applyStyle(QStringLiteral("Heading 1"));
    selectText(e, QStringLiteral("Small heading"), false);
    e->applyStyle(QStringLiteral("Heading 5"));
    settle(200);
    QTextDocument *d = e->document();

    // bold from the style lights the button; Bold switches what is seen
    selectText(e, QStringLiteral("Pieces From the Past"));
    const bool seenBold = formatAt(d, QStringLiteral("Pieces")).fontWeight() >= QFont::DemiBold;
    const bool lit = bold->isChecked();
    shot(&w, QStringLiteral("02-bold-from-the-style-shown"));
    check(seenBold && lit && combo->currentData().toString() == QLatin1String("Heading 1"),
          QStringLiteral("a heading made bold by its style: Bold is on (the bar shows what is seen)"),
          QStringLiteral("bold seen %1, button %2").arg(seenBold).arg(lit));
    bold->trigger();
    settle(150);
    const bool offSeen = formatAt(d, QStringLiteral("Pieces")).fontWeight() < QFont::DemiBold
                         && formatAt(d, QStringLiteral("Past")).fontWeight() < QFont::DemiBold;
    const bool offButton = !bold->isChecked();
    check(offSeen && offButton, QStringLiteral("Bold on the bold heading makes it not bold, and the button goes off"),
          QStringLiteral("%1 %2").arg(offSeen).arg(offButton));
    // not bold within a bold style survives saving and reading
    {
        QString err;
        p->save(&err);
        auto again = Project::open(p->folderPath(), &err);
        QTextDocument *rd = again ? again->docs()->text(doc->id) : nullptr;
        const bool kept = rd && formatAt(rd, QStringLiteral("Pieces")).fontWeight() < QFont::DemiBold
                          && blockWith(rd, QStringLiteral("Pieces")).blockFormat().stringProperty(props::Style)
                                 == QLatin1String("Heading 1");
        check(kept, QStringLiteral("a heading made not bold stays not bold after saving and opening the project (%1)")
                        .arg(p->storage == StorageFormat::Rtf ? QStringLiteral("RTF") : QStringLiteral("Markdown")));
    }
    bold->trigger();
    settle(150);
    check(formatAt(d, QStringLiteral("Pieces")).fontWeight() >= QFont::DemiBold && bold->isChecked(),
          QStringLiteral("Bold again makes it bold, and the button is on"));
    // without a selection: the cursor in the heading
    selectText(e, QStringLiteral("From"), false);
    check(bold->isChecked(), QStringLiteral("the cursor in the heading (no selection): Bold is on"));

    // sizes: one paragraph 18 pt by hand
    selectText(e, QStringLiteral("The keeper climbed the stairs."));
    e->setFontSize(18);
    settle(100);
    selectText(e, QStringLiteral("The keeper climbed the stairs."));
    const QString one = size->text();
    const QString keeper = QStringLiteral("The keeper climbed the stairs.");
    const int from = int(d->toPlainText().indexOf(keeper));
    const int to = int(d->toPlainText().indexOf(QStringLiteral("A second paragraph."))) + 19;
    {
        QTextCursor c(d);
        c.setPosition(from);
        c.setPosition(to, QTextCursor::KeepAnchor);
        e->setTextCursor(c);
        settle(150);
    }
    const QString mixed = size->text();
    const QString fontMixedSize = font->currentText();
    shot(&w, QStringLiteral("03-mixed-sizes-leave-the-size-empty"));
    check(one.startsWith(QLatin1String("18")) && mixed.trimmed().isEmpty() && !fontMixedSize.isEmpty(),
          QStringLiteral("a selection with two sizes leaves the size field empty; the font they share is shown"),
          QStringLiteral("one \"%1\", mixed \"%2\", font \"%3\"").arg(one, mixed, fontMixedSize));
    // fonts: the second paragraph in another font
    selectText(e, QStringLiteral("A second paragraph."));
    e->setFontFamily(QStringLiteral("DejaVu Sans"));
    settle(100);
    {
        QTextCursor c(d);
        c.setPosition(from);
        c.setPosition(to, QTextCursor::KeepAnchor);
        e->setTextCursor(c);
        settle(150);
    }
    check(font->currentText().isEmpty(), QStringLiteral("a selection with two fonts leaves the font field empty"),
          font->currentText());
    // alignment: the first body paragraph centered
    selectText(e, QStringLiteral("keeper"), false);
    e->setAlignment(Qt::AlignHCenter);
    settle(100);
    selectText(e, QStringLiteral("keeper"), false);
    const bool oneCentered = center->isChecked() && !left->isChecked();
    {
        QTextCursor c(d);
        c.setPosition(from);
        c.setPosition(to, QTextCursor::KeepAnchor);
        e->setTextCursor(c);
        settle(150);
    }
    const bool noneOn = !center->isChecked() && !left->isChecked();
    check(oneCentered && noneOn,
          QStringLiteral("paragraphs with different alignments: no alignment button is on; one centered paragraph: Center"),
          QStringLiteral("%1 %2").arg(oneCentered).arg(noneOn));
    // the style of a selection over a heading and body text
    {
        QTextCursor c(d);
        c.setPosition(1);
        c.setPosition(from + 5, QTextCursor::KeepAnchor);
        e->setTextCursor(c);
        settle(150);
    }
    check(combo->currentIndex() == -1 && combo->currentText().isEmpty(),
          QStringLiteral("a selection over a heading and body text leaves the style list empty"), combo->currentText());
    // italic from a style: Heading 5 is italic or not, the button says the same
    selectText(e, QStringLiteral("Small heading"));
    const bool italicSeen = formatAt(d, QStringLiteral("Small")).fontItalic();
    check(italic->isChecked() == italicSeen && bold->isChecked() == (formatAt(d, QStringLiteral("Small")).fontWeight() >= QFont::DemiBold),
          QStringLiteral("Italic and Bold show the look of another heading style as well"));
}

// ======================================================================= 3: the spelling menu
static void spellingChecks(MainWindow &w, Project *p)
{
    SpellChecker &sc = SpellChecker::instance();
    if (!sc.isAvailable() && !sc.setLanguage(QStringLiteral("en_US"))) {
        info(QStringLiteral("(no dictionary installed: the spelling menu is not checked)"));
        return;
    }
    BinderItem *doc = textDoc(p, QStringLiteral("Spelling"), {QStringLiteral("They could not be boethered to leave the harbor.")});
    EditorPane *pane = visiblePane(w);
    if (!pane)
        return;
    SpellChecker::setEnabled(true);
    pane->showItems({doc});
    settle(400);
    TextEditor *e = pane->currentEditor();
    if (!e)
        return;
    e->setFocus();
    const int at = int(e->document()->toPlainText().indexOf(QLatin1String("boethered"))) + 3;
    const QPoint pt = pointOf(e, at);
    rightClick(e->viewport(), pt);
    QMenu *m = openPopup();
    check(m != nullptr, QStringLiteral("(right-click menu on a misspelled word)"));
    if (m) {
        int entries = 0;
        QMenu *more = nullptr;
        bool add = false;
        for (QAction *a : m->actions()) {
            if (a->isSeparator())
                continue;
            ++entries;
            if (a->menu() && a->menu()->objectName() == QLatin1String("spellingMore"))
                more = a->menu();
            if (a->text().contains(QLatin1String("to Dictionary")))
                add = true;
        }
        QAction *first = m->actions().value(0);
        const bool suggestion = first && first->font().bold() && !first->menu();
        bool paste = false, style = false, appearance = false;
        if (more)
            for (QAction *a : more->actions()) {
                paste = paste || a->text().contains(QLatin1String("Paste"));
                style = style || (a->menu() && a->text() == QLatin1String("Style"));
                appearance = appearance || (a->menu() && a->text().contains(QLatin1String("Appearance")));
            }
        bool pasteOnTop = false;
        for (QAction *a : m->actions())
            pasteOnTop = pasteOnTop || a->text().contains(QLatin1String("Paste"));
        shot(m, QStringLiteral("04-short-spelling-menu"));
        const QRect firstRect = first ? m->actionGeometry(first) : QRect();
        const QPoint menuTop = m->mapToGlobal(QPoint(0, 0));
        const QPoint mouse = e->viewport()->mapToGlobal(pt);
        info(QStringLiteral("menu %1 px high, %2 entries; first suggestion %3 px from the mouse")
                 .arg(m->height())
                 .arg(entries)
                 .arg(qAbs(menuTop.y() + firstRect.center().y() - mouse.y())));
        check(suggestion && add && more && entries <= 9 && !pasteOnTop,
              QStringLiteral("on a misspelled word the menu holds the suggestions, Add, Ignore and one submenu"),
              QStringLiteral("suggestion %1, add %2, submenu %3, %4 entries").arg(suggestion).arg(add).arg(more != nullptr).arg(entries));
        check(paste && style && appearance,
              QStringLiteral("the submenu holds the other commands (Paste, Style, Appearance ...)"));
        check(m->height() < 360 && qAbs(menuTop.y() + firstRect.center().y() - mouse.y()) < 60,
              QStringLiteral("the menu is short and the first suggestion is next to the mouse"));
    }
    closePopups();
    // a correct word: the whole menu as before
    const int ok = int(e->document()->toPlainText().indexOf(QLatin1String("harbor"))) + 2;
    rightClick(e->viewport(), pointOf(e, ok));
    m = openPopup();
    bool pasteOnTop = false, more = false;
    if (m)
        for (QAction *a : m->actions()) {
            pasteOnTop = pasteOnTop || a->text().contains(QLatin1String("Paste"));
            more = more || (a->menu() && a->menu()->objectName() == QLatin1String("spellingMore"));
        }
    check(m && pasteOnTop && !more, QStringLiteral("on a correct word the menu shows all commands as before"));
    closePopups();
    SpellChecker::setEnabled(false);
}

// ======================================================================= 2 and 5: the Styles panel
static void panelChecks(MainWindow &w, Project *p, BinderItem *source, BinderItem *other, BinderItem *styled)
{
    EditorPane *pane = visiblePane(w);
    if (!pane)
        return;
    pane->showItems({source});
    settle(400);
    TextEditor *e = pane->currentEditor();
    if (!e)
        return;
    e->setFocus();
    // the source paragraph: 15 pt, line spacing 1.5, set by hand
    selectText(e, QStringLiteral("Body text to copy."));
    e->setFontSize(15);
    e->setLineSpacing(1.5);
    selectText(e, QStringLiteral("copy"), false);
    settle(150);
    byKey(&w, QStringLiteral("stylesPanel"))->trigger();
    settle(400);
    auto *panel = w.findChild<StylesPanel *>(QStringLiteral("stylesPanel"));
    auto *list = panel ? panel->findChild<QListWidget *>(QStringLiteral("stylesList")) : nullptr;
    check(panel && list && panel->isVisible(), QStringLiteral("(Styles panel)"));
    if (!panel || !list)
        return;
    auto rowOf = [list](const QString &text) {
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->text().endsWith(text))
                return i;
        return -1;
    };
    // a right-click on a row of the panel, then the entry `entry` of its menu; `f` gets the window it opens
    auto fromPanelMenu = [&](int row, const QString &entry, const std::function<void(QWidget *)> &f) {
        list->setCurrentRow(row);
        const QPoint pos = list->visualItemRect(list->item(row)).center();
        bool found = false;
        QTimer::singleShot(250, [&] {
            QMenu *m = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QAction *act = nullptr;
            if (m)
                for (QAction *a : m->actions())
                    if (a->text().startsWith(entry))
                        act = a;
            found = act != nullptr;
            if (m)
                m->hide();
            if (act) {
                QTimer::singleShot(400, [&f] {
                    QWidget *dlg = QApplication::activeModalWidget();
                    f(dlg);
                    if (dlg && dlg->isVisible()) {
                        if (auto *d = qobject_cast<QDialog *>(dlg))
                            d->reject();
                    }
                });
                act->trigger();
            }
        });
        emit list->customContextMenuRequested(pos);
        settle(200);
        return found;
    };

    // 5: Redefine of Heading 2 from the panel opens above the panel
    bool belongs = false, transient = false;
    QString kind;
    const bool found = fromPanelMenu(rowOf(QStringLiteral("Heading 2")), QStringLiteral("Redefine From Selection"), [&](QWidget *dlg) {
        if (!dlg)
            return;
        kind = QString::fromLatin1(dlg->metaObject()->className());
        belongs = dlg->parentWidget() == panel;
        transient = dlg->windowHandle() && dlg->windowHandle()->transientParent() == panel->windowHandle();
        shot(dlg, QStringLiteral("05-redefine-belongs-to-the-panel"));
    });
    check(found && belongs && transient && kind.contains(QLatin1String("StyleFromSelection")),
          QStringLiteral("Redefine started from the panel opens above the panel (the panel is its parent window)"),
          QStringLiteral("menu entry %1, dialog %2, parent %3, transient %4").arg(found).arg(kind).arg(belongs).arg(transient));
    bool editBelongs = false;
    fromPanelMenu(rowOf(QStringLiteral("Heading 2")), QStringLiteral("Edit"), [&](QWidget *dlg) {
        editBelongs = dlg && dlg->parentWidget() == panel;
    });
    check(editBelongs, QStringLiteral("Edit started from the panel opens above the panel as well"));
    // from the Format menu the dialog still belongs to the main window
    bool mainParent = false;
    e->setFocus();
    withModal([&] { byKey(&w, QStringLiteral("styleRedefine"))->trigger(); },
              [&](QWidget *dlg) { mainParent = dlg && dlg->parentWidget() == &w; });
    check(mainParent, QStringLiteral("Redefine from Format > Style belongs to the main window, as before"));

    // 2: No Style: the panel offers Redefine From Selection
    e->setFocus();
    selectText(e, QStringLiteral("copy"), false);
    const int none = rowOf(QStringLiteral("No Style"));
    list->setCurrentRow(none);
    settle(100);
    auto *redefineButton = panel->findChild<QToolButton *>(QStringLiteral("stylesRedefine"));
    check(redefineButton && redefineButton->isEnabled(), QStringLiteral("the panel's Redefine button works on the No Style row"));
    const QTextCharFormat headBefore = formatAt(p->docs()->text(styled->id), QStringLiteral("Pieces"));
    bool boxBelongs = false;
    QString summary;
    const bool noneEntry = fromPanelMenu(none, QStringLiteral("Redefine From Selection"), [&](QWidget *dlg) {
        auto *box = qobject_cast<QMessageBox *>(dlg);
        if (!box)
            return;
        boxBelongs = box->parentWidget() == panel;
        summary = box->text();
        shot(box, QStringLiteral("06-redefine-no-style"));
        box->button(QMessageBox::Ok)->click();
    });
    settle(400);
    check(noneEntry && boxBelongs && summary.contains(QLatin1String("15")),
          QStringLiteral("No Style: Redefine From Selection in the panel's menu asks above the panel and names the formatting"),
          summary.left(300));
    const QTextBlock otherBlock = p->docs()->text(other->id)->firstBlock();
    const double otherSize = direct::charSpec(formatAt(p->docs()->text(other->id), QStringLiteral("Far"))).size;
    const QTextCharFormat headAfter = formatAt(p->docs()->text(styled->id), QStringLiteral("Pieces"));
    check(qAbs(p->formatting.fontSize - 15) < 0.01 && qAbs(p->formatting.lineSpacing - 1.5) < 0.01,
          QStringLiteral("the formatting at the cursor is the project's main text style now (15 pt, line spacing 1.5)"),
          QStringLiteral("%1 pt, %2").arg(p->formatting.fontSize).arg(p->formatting.lineSpacing));
    check(qAbs(otherBlock.blockFormat().lineHeight() - 150) < 0.5 && (otherSize <= 0 || qAbs(otherSize - 15) < 0.01),
          QStringLiteral("text without a style in another document follows (line spacing 1.5, 15 pt)"),
          QStringLiteral("line %1, size %2").arg(otherBlock.blockFormat().lineHeight()).arg(otherSize));
    check(headAfter.intProperty(QTextFormat::FontSizeAdjustment) == headBefore.intProperty(QTextFormat::FontSizeAdjustment)
              && qAbs(headAfter.fontPointSize() - headBefore.fontPointSize()) < 0.01 && qAbs(headAfter.fontPointSize() - 15) > 0.01,
          QStringLiteral("a heading keeps the size of its style"),
          QStringLiteral("adjust %1 -> %2, points %3 -> %4")
              .arg(headBefore.intProperty(QTextFormat::FontSizeAdjustment))
              .arg(headAfter.intProperty(QTextFormat::FontSizeAdjustment))
              .arg(headBefore.fontPointSize())
              .arg(headAfter.fontPointSize()));
    panel->close();
    settle(100);
}

// ======================================================================= 6: alignment of the main text
static void alignChecks(MainWindow &w, Project *p, BinderItem *styled, const QString &workDir)
{
    // File > Options > Editing > Formatting
    {
        OptionsDialog dlg(&w, OptionsDialog::Editing);
        dlg.showSection(OptionsDialog::SEditing, QStringLiteral("Formatting"));
        auto *buttons = dlg.findChild<AlignButtons *>(QStringLiteral("optNewAlign"));
        bool noJustifyBox = true;
        for (QCheckBox *cb : dlg.findChildren<QCheckBox *>())
            if (cb->text() == QLatin1String("Justify text"))
                noJustifyBox = false;
        const int n = buttons ? int(buttons->findChildren<QToolButton *>().size()) : 0;
        QToolButton *right = buttons ? buttons->findChild<QToolButton *>(QStringLiteral("right")) : nullptr;
        check(buttons && n == 4 && noJustifyBox && buttons->alignment() == Qt::AlignLeft,
              QStringLiteral("Options > Editing > Formatting: four alignment buttons (left chosen) instead of \"Justify text\""));
        if (right) {
            dlg.show();
            settle(300);
            shot(&dlg, QStringLiteral("07-options-alignment-buttons"));
            right->click();
            dlg.apply();
            AppSettings &as = AppSettings::instance();
            const bool stored = as.newAlign == Qt::AlignRight;
            as.newAlign = Qt::AlignLeft;
            as.load();
            check(stored && as.newAlign == Qt::AlignRight,
                  QStringLiteral("right-aligned main text for new projects is stored and read back"));
            as.newAlign = Qt::AlignLeft;
            as.save();
        }
    }
    // Project > Project Settings > Formatting
    QTextDocument *t = p->docs()->text(styled->id);
    {
        ProjectSettingsDialog dlg(p, ProjectSettingsDialog::Formatting, &w);
        auto *buttons = dlg.findChild<AlignButtons *>(QStringLiteral("formattingAlign"));
        QToolButton *center = buttons ? buttons->findChild<QToolButton *>(QStringLiteral("center")) : nullptr;
        check(center != nullptr, QStringLiteral("Project Settings > Formatting: the four alignment buttons"));
        if (center) {
            dlg.show();
            settle(300);
            center->click();
            shot(&dlg, QStringLiteral("08-project-settings-centered"));
            dlg.apply();
            dlg.close();
        }
    }
    settle(200);
    const QTextBlockFormat body = blockWith(t, QStringLiteral("A second paragraph")).blockFormat();
    check(p->formatting.align == Qt::AlignHCenter && (body.alignment() & Qt::AlignHCenter),
          QStringLiteral("centered main text: paragraphs without an alignment of their own are centered"));
    check(display::normalBlockFormat(p->formatting).alignment() == Qt::AlignHCenter,
          QStringLiteral("new paragraphs get the alignment of the main text"));
    {
        const QJsonObject s = settingsfile::capture(p);
        const QJsonObject fo = s.value(settingsfile::key(settingsfile::Formatting)).toObject();
        check(fo.value(QStringLiteral("align")).toString() == QLatin1String("center") && fo.value(QStringLiteral("justify")) == false,
              QStringLiteral("project settings files carry the alignment (and \"justify\" for older versions)"));
    }
    // the project file: "align", and "justify" for older versions; files of older versions read
    {
        QTemporaryDir dir(workDir + QLatin1String("/align-XXXXXX"));
        templates::Info blank;
        for (const auto &ti : templates::all())
            if (ti.id == QLatin1String("blank"))
                blank = ti;
        QString err;
        auto q = templates::createProject(blank, dir.path(), QStringLiteral("Old Style"), &err);
        if (q) {
            q->formatting.align = Qt::AlignRight;
            q->notifySettingsChanged();
            q->save(&err);
            QFile f(q->indexPath());
            f.open(QIODevice::ReadOnly);
            QString xml = QString::fromUtf8(f.readAll());
            f.close();
            const bool both = xml.contains(QLatin1String("align=\"right\"")) && xml.contains(QLatin1String("justify=\"0\""));
            auto r1 = Project::open(q->folderPath(), &err);
            const bool readRight = r1 && r1->formatting.align == Qt::AlignRight;
            r1.reset();
            // as a version before 0.40.3 wrote it: justified, no "align"
            xml.replace(QRegularExpression(QStringLiteral(" align=\"[a-z]*\"")), QString());
            xml.replace(QLatin1String("justify=\"0\""), QLatin1String("justify=\"1\""));
            const QString path = q->indexPath(), folder = q->folderPath();
            q.reset();
            QFile o(path);
            o.open(QIODevice::WriteOnly | QIODevice::Truncate);
            o.write(xml.toUtf8());
            o.close();
            auto r2 = Project::open(folder, &err);
            check(both && readRight && r2 && r2->formatting.align == Qt::AlignJustify,
                  QStringLiteral("the project file keeps the alignment; a file of an older version with \"justify\" reads as justified"),
                  QStringLiteral("written %1, read %2, old file %3 %4 (%5)")
                      .arg(both)
                      .arg(readRight)
                      .arg(r2 != nullptr)
                      .arg(r2 ? int(r2->formatting.align) : -1)
                      .arg(err));
        } else {
            check(false, QStringLiteral("(second project)"), err);
        }
    }
    // back to left
    {
        ProjectSettingsDialog dlg(p, ProjectSettingsDialog::Formatting, &w);
        if (auto *buttons = dlg.findChild<AlignButtons *>(QStringLiteral("formattingAlign")))
            if (QToolButton *l = buttons->findChild<QToolButton *>(QStringLiteral("left")))
                l->click();
        dlg.apply();
    }
    settle(200);
}

// ======================================================================= 7: the style list
static QListWidget *listIn(QMenu *m) { return m ? m->findChild<QListWidget *>(QStringLiteral("styleListRows")) : nullptr; }

static void styleListChecks(MainWindow &w, Project *p, BinderItem *styled)
{
    EditorPane *pane = visiblePane(w);
    if (!pane)
        return;
    pane->showItems({styled});
    settle(400);
    TextEditor *e = pane->currentEditor();
    auto *combo = w.findChild<QComboBox *>(QStringLiteral("styleCombo"));
    if (!e || !combo)
        return;
    e->setFocus();
    selectText(e, QStringLiteral("second"), false);
    combo->showPopup();
    QMenu *m = openPopup();
    QListWidget *rows = listIn(m);
    check(m && m->objectName() == QLatin1String("styleListPopup") && rows, QStringLiteral("(the format bar's style list opens)"));
    if (!m || !rows)
        return;
    settle(200);
    int styleRows = 0, visible = 0;
    for (int i = 0; i < rows->count(); ++i) {
        QListWidgetItem *it = rows->item(i);
        if (!(it->flags() & Qt::ItemIsEnabled))
            continue;
        ++styleRows;
        const QRect r = rows->visualItemRect(it);
        if (rows->viewport()->rect().contains(r))
            ++visible;
    }
    shot(m, QStringLiteral("09-style-list-scrolls"));
    {
        QScrollBar *sb = rows->verticalScrollBar();
        rows->grab().save(outDir + QLatin1String("/09b-list-alone.png"));
        info(QStringLiteral("list %1x%2 at %3,%4 in a menu %5 wide; scroll bar visible %6, %7x%8 at %9")
                 .arg(rows->width()).arg(rows->height()).arg(rows->x()).arg(rows->y()).arg(m->width())
                 .arg(sb->isVisible()).arg(sb->width()).arg(sb->height()).arg(sb->mapTo(m, QPoint(0, 0)).x()));
    }
    check(styleRows == 16 && visible == stylelist::kVisibleRows && rows->verticalScrollBar()->maximum() > 0,
          QStringLiteral("16 entries: the list shows 10 rows and scrolls"),
          QStringLiteral("%1 entries, %2 visible").arg(styleRows).arg(visible));
    QStringList after;
    bool pastList = false;
    for (QAction *a : m->actions()) {
        if (qobject_cast<QWidgetAction *>(a))
            pastList = true;
        else if (pastList && !a->isSeparator())
            after << a->objectName();
    }
    check(after == QStringList({QStringLiteral("styleNew"), QStringLiteral("styleRedefine"), QStringLiteral("stylesPanel"),
                                QStringLiteral("styleListCustomize")}),
          QStringLiteral("below the list, fixed: New Style, Redefine Style, Styles Panel, Customize Style List"),
          after.join(QLatin1String(", ")));
    // a click on a row applies the style
    QListWidgetItem *centered = nullptr;
    for (int i = 0; i < rows->count(); ++i)
        if (rows->item(i)->data(Qt::UserRole).toString() == QLatin1String("Centered"))
            centered = rows->item(i);
    if (centered) {
        rows->scrollToItem(centered);
        settle(100);
        QTest::mouseClick(rows->viewport(), Qt::LeftButton, Qt::NoModifier, rows->visualItemRect(centered).center());
        settle(250);
    }
    check(centered && !QApplication::activePopupWidget() && e->paragraphStyleAtCursor() == QLatin1String("Centered"),
          QStringLiteral("a click on a row (after scrolling) applies the style and closes the list"), e->paragraphStyleAtCursor());
    closePopups();
    e->applyStyle(QString());
    settle(100);

    // Customize Style List: six entries left out, Block Quote first
    QStringList hide = {QStringLiteral("Heading 4"), QStringLiteral("Heading 5"), QStringLiteral("Heading 6"),
                        QStringLiteral("Attribution"), QStringLiteral("Code Block"), QStringLiteral("Code Span")};
    combo->showPopup();
    m = openPopup();
    QAction *custom = nullptr;
    if (m)
        for (QAction *a : m->actions())
            if (a->objectName() == QLatin1String("styleListCustomize"))
                custom = a;
    check(custom && custom->isEnabled(), QStringLiteral("(Customize Style List in the list)"));
    if (m)
        m->hide();
    bool dialogSeen = false;
    if (custom)
        withModal([&] { custom->trigger(); },
                  [&](QWidget *dlg) {
                      auto *d = qobject_cast<StyleListDialog *>(dlg);
                      auto *entries = d ? d->findChild<QListWidget *>(QStringLiteral("styleListEntries")) : nullptr;
                      if (!entries)
                          return;
                      dialogSeen = entries->count() == 16;
                      int quote = -1;
                      for (int i = 0; i < entries->count(); ++i) {
                          QListWidgetItem *it = entries->item(i);
                          if (hide.contains(it->data(Qt::UserRole).toString()))
                              it->setCheckState(Qt::Unchecked);
                          if (it->data(Qt::UserRole).toString() == QLatin1String("Block Quote"))
                              quote = i;
                      }
                      entries->setCurrentRow(quote);
                      auto *up = d->findChild<QPushButton *>(QStringLiteral("styleListUp"));
                      for (int i = 0; i < quote && up; ++i)
                          up->click();
                      shot(d, QStringLiteral("10-customize-style-list"));
                      d->accept();
                  });
    settle(200);
    check(dialogSeen && p->styleListOrder.value(0) == QLatin1String("Block Quote") && p->styleListHidden.size() == 6,
          QStringLiteral("Customize Style List: entries can be left out and moved"),
          p->styleListOrder.join(QLatin1String(", ")) + QLatin1String(" | ") + p->styleListHidden.join(QLatin1String(", ")));
    combo->showPopup();
    m = openPopup();
    QStringList names;
    if (m)
        for (QAction *a : m->actions())
            if (!a->isSeparator() && a->objectName().isEmpty() && !qobject_cast<QWidgetAction *>(a))
                names << a->data().toString();
    shot(m ? static_cast<QWidget *>(m) : static_cast<QWidget *>(&w), QStringLiteral("11-chosen-style-list"));
    check(m && !listIn(m) && names.size() == 10 && names.value(0) == QLatin1String("Block Quote") && !names.contains(QLatin1String("Heading 5")),
          QStringLiteral("the list shows the 10 chosen entries in the chosen order, without a scroll bar"),
          names.join(QLatin1String(", ")));
    closePopups();
    // the text uses a style the list leaves out: the format bar still names it
    selectText(e, QStringLiteral("Small heading"), false);
    check(combo->currentText().contains(QLatin1String("Heading 5")),
          QStringLiteral("a style the list leaves out is still named in the format bar"),
          combo->currentText() + QLatin1String(" / at the cursor: ") + e->paragraphStyleAtCursor()
              + QStringLiteral(" / items: %1, index %2, found %3").arg(combo->count()).arg(combo->currentIndex()).arg(combo->findData(QStringLiteral("Heading 5"))));
    // the right-click menu has the same list
    selectText(e, QStringLiteral("second"), false);
    const int at = int(e->document()->toPlainText().indexOf(QLatin1String("second"))) + 2;
    rightClick(e->viewport(), pointOf(e, at));
    m = openPopup();
    QMenu *sub = m ? m->findChild<QMenu *>(QStringLiteral("contextStyleMenu")) : nullptr;
    QStringList subNames;
    bool subCustom = false;
    if (sub)
        for (QAction *a : sub->actions()) {
            if (a->objectName() == QLatin1String("styleListCustomize"))
                subCustom = true;
            else if (!a->isSeparator() && a->objectName().isEmpty())
                subNames << a->data().toString();
        }
    check(sub && subNames.value(0) == QLatin1String("Block Quote") && subNames.size() == 10 && subCustom,
          QStringLiteral("the right-click menu's Style list is the same, with the commands below it"), subNames.join(QLatin1String(", ")));
    closePopups();
    // Format > Style offers Customize Style List as well
    QMenu *styleMenu = w.findChild<QMenu *>(QStringLiteral("styleMenu"));
    if (styleMenu)
        emit styleMenu->aboutToShow();
    bool inMenu = false;
    if (styleMenu)
        for (QAction *a : styleMenu->actions())
            inMenu = inMenu || a->objectName() == QLatin1String("styleListCustomize");
    check(inMenu, QStringLiteral("Format > Style has Customize Style List"));
    // a renamed style keeps its place
    StyleSheet sheet = styles::sheet(p->formatting);
    const int bq = sheet.indexOf(QStringLiteral("Block Quote"));
    if (bq >= 0) {
        sheet.styles[bq].name = QStringLiteral("Quotation");
        w.commitStyles(sheet, {{QStringLiteral("Block Quote"), QStringLiteral("Quotation")}});
        settle(200);
    }
    check(p->styleListOrder.value(0) == QLatin1String("Quotation"), QStringLiteral("a renamed style keeps its place in the list"),
          p->styleListOrder.value(0));
    // kept in the project file and in project settings files
    QString err;
    p->save(&err);
    auto again = Project::open(p->folderPath(), &err);
    check(again && again->styleListOrder == p->styleListOrder && again->styleListHidden == p->styleListHidden,
          QStringLiteral("the chosen list is kept in the project file"), err);
    const QJsonObject st = settingsfile::capture(p).value(settingsfile::key(settingsfile::Styles)).toObject();
    check(st.value(QStringLiteral("list")).toArray().size() == p->styleListOrder.size()
              && st.value(QStringLiteral("hidden")).toArray().size() == 6,
          QStringLiteral("project settings files carry the chosen list"));
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV0403Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV0403Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);

    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.40.3")) >= 0
              && QLatin1String(QB_APP_VERSION_SHOWN).endsWith(QLatin1String(" alpha")),
          QStringLiteral("version 0.40.3 or newer, shown with \"alpha\""), QStringLiteral(QB_APP_VERSION_SHOWN));
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
    BinderItem *untitled = textDoc(p, QStringLiteral("Untitled"),
                                   {QStringLiteral("Of How to Learn to Hold Back. A Bit."),
                                    QStringLiteral("The universe was still quite young.")});
    untitled->wordCount = 15; // as the project file keeps it
    BinderItem *empty = textDoc(p, QStringLiteral("Untitled"), {});
    BinderItem *named = textDoc(p, QStringLiteral("Chapter Notes"), {QStringLiteral("First line here.")});
    named->wordCount = 3;
    BinderItem *styled = textDoc(p, QStringLiteral("Pieces"),
                                 {QStringLiteral("Pieces From the Past"), QStringLiteral("The keeper climbed the stairs."),
                                  QStringLiteral("A second paragraph."), QStringLiteral("Small heading"),
                                  QStringLiteral("The end of it.")});
    BinderItem *source = textDoc(p, QStringLiteral("Redefine Source"), {QStringLiteral("Body text to copy.")});
    BinderItem *other = textDoc(p, QStringLiteral("Other Shore"), {QStringLiteral("Far away text.")});
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

    titleChecks(w, p, untitled, empty, named);
    formatBarChecks(w, p, styled);
    spellingChecks(w, p);
    panelChecks(w, p, source, other, styled);
    alignChecks(w, p, styled, work.path());
    styleListChecks(w, p, styled);

    settle(200);
    std::printf("\n%d failure(s)\n", failures);
    delete wp;
    return failures;
}
