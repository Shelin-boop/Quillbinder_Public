// Scripted run for version 0.60.1:
//  1. Edit > Paste with Formatting: text from another program keeps fonts, sizes, colors and the
//     formatting of its paragraphs (RTF and HTML); Paste stays as it was.
//  2. View > Text Editing > Show Invisibles and its toolbar button: spaces, tabs, line breaks and
//     paragraph ends drawn on the screen only (single and combined text), the choice kept.
//  3. The total of a folder: the footer (text, corkboard, outliner) and the inspector.
//  4. A paragraph style chosen for a selection over several paragraphs styles all of them in full;
//     fonts, sizes and colors set by hand give way, bold, italic, underline, strikethrough and
//     highlights stay; a selection that ends at the start of a paragraph leaves it alone.
// Saves screenshots into the folder given as first argument. Exit code = number of failed
// checks.
//
//   xvfb-run -a ./qb_v0601_tests /tmp/shots
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "compile/PluginManager.h"
#include "text/DirectFormat.h"
#include "text/DisplayFormat.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/Commands.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"
#include "ui/ToolbarConfig.h"

#include "AppInfo.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTextBlock>
#include <QTextDocument>
#include <QToolBar>
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

static void shot(QWidget *w, const QString &name)
{
    settle(300);
    w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}

static QAction *byKey(QWidget *w, const QString &key) { return w->findChild<QAction *>(key); }

static EditorPane *visiblePane(MainWindow &w)
{
    for (EditorPane *ep : w.findChildren<EditorPane *>())
        if (ep->isVisible())
            return ep;
    return nullptr;
}

static BinderItem *textDoc(Project *p, BinderItem *parent, const QString &title, const QStringList &paragraphs,
                           ItemType type = ItemType::Text)
{
    BinderItem *it = p->model()->createItem(parent, -1, type, title);
    QTextCursor c(p->docs()->text(it->id));
    for (int i = 0; i < paragraphs.size(); ++i) {
        if (i > 0)
            c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
        c.insertText(paragraphs[i]);
    }
    p->docs()->saveDocument(it->id);
    p->docs()->recount(it->id);
    return it;
}

static QTextBlock blockWith(QTextDocument *d, const QString &text)
{
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
        if (b.text().contains(text))
            return b;
    return QTextBlock();
}

static int posOf(QTextDocument *d, const QString &text) { return int(d->toPlainText().indexOf(text)); }

static QTextCharFormat formatAt(QTextDocument *d, const QString &text)
{
    QTextCursor c(d);
    c.setPosition(qMax(0, posOf(d, text)) + 1);
    return c.charFormat();
}

static QString familyOf(const QTextCharFormat &cf)
{
    const QStringList l = cf.fontFamilies().toStringList();
    return l.isEmpty() ? QString() : l.first();
}

// the editor shows `it` alone, with the keyboard
static TextEditor *openAlone(MainWindow &w, BinderItem *it)
{
    EditorPane *pane = visiblePane(w);
    if (!pane)
        return nullptr;
    pane->showItems({it}, false, int(ViewMode::Single));
    settle(400);
    TextEditor *e = pane->currentEditor();
    if (e) {
        w.activateWindow();
        e->setFocus();
        settle(100);
    }
    return e;
}

static void select(TextEditor *e, int from, int to)
{
    QTextCursor c(e->document());
    c.setPosition(from);
    c.setPosition(to, QTextCursor::KeepAnchor);
    e->setTextCursor(c);
    settle(60);
}

static QString menuNeighbor(QAction *a, int delta)
{
    for (QObject *o : a->associatedObjects())
        if (auto *m = qobject_cast<QMenu *>(o)) {
            const QList<QAction *> l = m->actions();
            const int i = int(l.indexOf(a));
            if (i + delta >= 0 && i + delta < l.size())
                return l.at(i + delta)->objectName();
        }
    return QString();
}

// ======================================================================= 1: Paste with Formatting
static const char kRtf[] =
    "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 DejaVu Serif;}{\\f1 DejaVu Sans Mono;}}{\\colortbl;\\red200\\green0\\blue0;}\n"
    "\\pard\\qc\\f0\\fs36\\cf1 Red serif words \\b bold\\b0\\par\n"
    "\\pard\\li720\\f1\\fs28\\cf0 Mono indented line\\par\n}";
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
    md->setText(QStringLiteral("Red serif words bold\nMono indented line"));
    QApplication::clipboard()->setMimeData(md);
    settle(100);
}

static void putHtml()
{
    auto *md = new QMimeData;
    md->setHtml(QStringLiteral("<p align=\"right\"><span style=\"font-family:'DejaVu Sans Mono'; font-size:14pt; "
                               "color:#0000ff;\">Blue mono words</span></p>"));
    md->setText(QStringLiteral("Blue mono words"));
    QApplication::clipboard()->setMimeData(md);
    settle(100);
}

// the cursor into a new, empty paragraph at the end
static void toNewParagraph(TextEditor *e)
{
    QTextCursor c(e->document());
    c.movePosition(QTextCursor::End);
    e->setTextCursor(c);
    if (!c.block().text().isEmpty())
        QTest::keyClick(e, Qt::Key_Return);
    settle(60);
}

static void pasteChecks(MainWindow &w, Project *p, BinderItem *doc)
{
    QAction *keep = byKey(&w, QStringLiteral("pasteKeep"));
    const QKeySequence seq(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_V);
    check(keep && keep->text().remove(QLatin1Char('&')) == QLatin1String("Paste with Formatting") && keep->shortcut() == seq
              && menuNeighbor(keep, -1) == QLatin1String("pastePlain")
              && commands::conflict(QStringLiteral("pasteKeep"), seq, commands::custom()).isEmpty(),
          QStringLiteral("Edit > Paste with Formatting after Paste and Match Style, Ctrl+Alt+Shift+V, no other command has the keys"),
          keep ? keep->shortcut().toString() + QLatin1String(" / ") + menuNeighbor(keep, -1) : QStringLiteral("no command"));

    TextEditor *e = openAlone(w, doc);
    if (!e || !keep)
        return;
    check(keep->isEnabled(), QStringLiteral("Paste with Formatting is on while an editor has the text"));
    QTextDocument *d = e->document();

    // Paste: a Markdown project gives text from another program the look of its own text
    putRtf();
    toNewParagraph(e);
    e->paste();
    settle(100);
    QTextCharFormat cf = formatAt(d, QStringLiteral("Red serif"));
    QTextBlock b = blockWith(d, QStringLiteral("Mono indented"));
    check(familyOf(cf).isEmpty() && !cf.hasProperty(QTextFormat::ForegroundBrush)
              && !(blockWith(d, QStringLiteral("Red serif")).blockFormat().alignment() & Qt::AlignHCenter)
              && b.isValid() && b.blockFormat().leftMargin() < 1 && formatAt(d, QStringLiteral("bold")).fontWeight() == QFont::Bold,
          QStringLiteral("Paste (as before): no fonts, colors or paragraph formatting from another program, bold stays"),
          familyOf(cf) + QStringLiteral(" / left %1").arg(b.blockFormat().leftMargin()));
    e->undo();
    settle(60);

    // Paste with Formatting into an empty paragraph
    toNewParagraph(e);
    const QString before = d->toPlainText();
    const int undoBefore = d->availableUndoSteps();
    keep->trigger();
    settle(150);
    cf = formatAt(d, QStringLiteral("Red serif"));
    const QTextBlock red = blockWith(d, QStringLiteral("Red serif"));
    b = blockWith(d, QStringLiteral("Mono indented"));
    check(familyOf(cf) == g_serif && qAbs(cf.fontPointSize() - 18) < 0.01
              && cf.foreground().color() == QColor(200, 0, 0),
          QStringLiteral("Paste with Formatting: font, size and color of RTF text from another program kept"),
          familyOf(cf) + QStringLiteral(" %1pt ").arg(cf.fontPointSize()) + cf.foreground().color().name());
    check(formatAt(d, QStringLiteral("bold")).fontWeight() == QFont::Bold
              && familyOf(formatAt(d, QStringLiteral("Mono indented"))) == QLatin1String("DejaVu Sans Mono")
              && qAbs(formatAt(d, QStringLiteral("Mono indented")).fontPointSize() - 14) < 0.01,
          QStringLiteral("... bold kept, the second paragraph has its own font and size"));
    check((red.blockFormat().alignment() & Qt::AlignHCenter) && (red.blockFormat().intProperty(props::ParaDirect) & direct::Align),
          QStringLiteral("... the first paragraph, pasted into an empty one, is centered as in the source (set by hand)"),
          QString::number(int(red.blockFormat().alignment())));
    check(b.isValid() && qAbs(b.blockFormat().leftMargin() - 48) < 1.5 && (b.blockFormat().intProperty(props::ParaDirect) & direct::Left),
          QStringLiteral("... the second paragraph keeps its left indent of 12.7 mm (48 px)"),
          QString::number(b.blockFormat().leftMargin()));
    shot(&w, QStringLiteral("01-paste-with-formatting"));

    // saved into the Markdown file
    QString err;
    p->docs()->saveDocument(doc->id, &err);
    QFile f(p->docs()->textPath(doc->id));
    const QString md = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    check(md.contains(g_serif) && md.contains(QLatin1String("->")) && md.contains(QLatin1String("<-")),
          QStringLiteral("... and saved with the document (font span, centered line)"), md.left(600));

    // one Undo takes the whole paste back
    e->undo();
    settle(60);
    check(d->toPlainText() == before && d->availableUndoSteps() == undoBefore, QStringLiteral("one Undo takes the whole paste back"),
          d->toPlainText().right(200));

    // into the middle of a paragraph: it keeps its own alignment
    const QTextBlock host = blockWith(d, QStringLiteral("Host paragraph"));
    QTextCursor c(d);
    c.setPosition(host.position() + 4);
    e->setTextCursor(c);
    keep->trigger();
    settle(150);
    const QTextBlock host2 = blockWith(d, QStringLiteral("Host"));
    check(host2.text().startsWith(QLatin1String("HostRed serif")) && !(host2.blockFormat().alignment() & Qt::AlignHCenter),
          QStringLiteral("pasted into the middle of a paragraph, the paragraph keeps its own alignment"), host2.text());
    e->undo();
    settle(60);

    // HTML only (a web page): Paste ignores the look, Paste with Formatting keeps it
    putHtml();
    toNewParagraph(e);
    e->paste();
    settle(100);
    const bool plainHtml = familyOf(formatAt(d, QStringLiteral("Blue mono"))).isEmpty()
                           && !(blockWith(d, QStringLiteral("Blue mono")).blockFormat().alignment() & Qt::AlignRight);
    e->undo();
    settle(60);
    keep->trigger();
    settle(150);
    cf = formatAt(d, QStringLiteral("Blue mono"));
    check(plainHtml && familyOf(cf) == QLatin1String("DejaVu Sans Mono") && qAbs(cf.fontPointSize() - 14) < 0.01
              && cf.foreground().color() == QColor(0, 0, 255)
              && (blockWith(d, QStringLiteral("Blue mono")).blockFormat().alignment() & Qt::AlignRight),
          QStringLiteral("HTML from a web page: Paste takes the look of the text, Paste with Formatting keeps font, size, color, right alignment"),
          familyOf(cf) + QStringLiteral(" %1pt ").arg(cf.fontPointSize()) + cf.foreground().color().name());
    e->undo();
    settle(60);
}

// ======================================================================= 2: Show Invisibles
// pixels that differ between two grabs inside r
static int changed(const QImage &a, const QImage &b, const QRect &r)
{
    int n = 0;
    const QRect rr = r.intersected(a.rect()).intersected(b.rect());
    for (int y = rr.top(); y <= rr.bottom(); ++y)
        for (int x = rr.left(); x <= rr.right(); ++x)
            if (a.pixel(x, y) != b.pixel(x, y))
                ++n;
    return n;
}

static QRect gapAt(TextEditor *e, int pos)
{
    QTextCursor c(e->document());
    c.setPosition(pos);
    const QRect r1 = e->cursorRect(c);
    c.setPosition(pos + 1);
    const QRect r2 = e->cursorRect(c);
    return QRect(QPoint(r1.right() + 1, r1.top()), QPoint(qMax(r1.right() + 2, r2.left() - 1), r1.bottom()));
}

static QRect afterEnd(TextEditor *e, const QTextBlock &b)
{
    QTextCursor c(b);
    c.movePosition(QTextCursor::EndOfBlock);
    const QRect r = e->cursorRect(c);
    return QRect(r.right() + 1, r.top(), r.height(), r.height());
}

static QImage grabOf(TextEditor *e)
{
    e->viewport()->update();
    settle(120);
    return e->viewport()->grab().toImage().convertToFormat(QImage::Format_RGB32);
}

static void invisiblesChecks(MainWindow &w, Project *p, BinderItem *doc, BinderItem *folder)
{
    QAction *inv = byKey(&w, QStringLiteral("showInvisibles"));
    check(inv && inv->isCheckable() && !inv->isChecked() && menuNeighbor(inv, -1) == QLatin1String("showRuler")
              && !inv->icon().isNull(),
          QStringLiteral("View > Text Editing > Show Invisibles after Show Ruler, a switch, off at first, with an icon"),
          inv ? menuNeighbor(inv, -1) : QStringLiteral("no command"));
    if (!inv)
        return;
    auto *bar = w.findChild<QToolBar *>(QStringLiteral("mainToolbar"));
    check(toolbars::standard(toolbars::Bar::Main).contains(QStringLiteral("showInvisibles")) && bar && bar->widgetForAction(inv),
          QStringLiteral("the toolbar has a Show Invisibles button (part of the standard toolbar)"));
    // the icon: a pilcrow in the colors of the light and the dark icon set
    auto inkOf = [](const QIcon &ic) {
        const QImage img = ic.pixmap(QSize(22, 22)).toImage().convertToFormat(QImage::Format_ARGB32);
        int n = 0, sum = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qAlpha(img.pixel(x, y)) > 128) {
                    ++n;
                    sum += qGray(img.pixel(x, y));
                }
        return QPair<int, int>(n, n ? sum / n : 0);
    };
    const auto light = inkOf(inv->icon());
    icons::setDark(true);
    const auto dark = inkOf(inv->icon());
    icons::setDark(false);
    check(light.first > 40 && light.first < 300 && light.second < 90 && dark.first > 40 && dark.second > 200,
          QStringLiteral("the button's pilcrow is drawn dark on light icons and light on dark icons"),
          QStringLiteral("light %1 px gray %2, dark %3 px gray %4").arg(light.first).arg(light.second).arg(dark.first).arg(dark.second));

    TextEditor *e = openAlone(w, doc);
    if (!e)
        return;
    QTextDocument *d = e->document();
    QTextCursor c(d);
    e->setTextCursor(c);
    w.setFocus(); // no blinking cursor in the pictures
    e->clearFocus();
    settle(100);
    const QString text = d->toPlainText();
    const bool modified = d->isModified();
    const int undo = d->availableUndoSteps();
    const QTextBlock first = d->begin(), last = d->lastBlock();
    const int space = posOf(d, QStringLiteral(" two")), tab = posOf(d, QStringLiteral("\tthree"));
    const int lineBreak = first.position() + int(first.text().indexOf(QChar::LineSeparator)); // toPlainText() turns it into \n
    const QImage off = grabOf(e);
    const QImage off2 = grabOf(e);
    inv->trigger();
    settle(100);
    const QImage on = grabOf(e);
    const QRect gs = gapAt(e, space), gt = gapAt(e, tab);
    QTextCursor lb(d);
    lb.setPosition(lineBreak);
    const QRect lr = e->cursorRect(lb), gl(lr.right() + 1, lr.top(), lr.height(), lr.height());
    const int nSpace = changed(off, on, gs), nTab = changed(off, on, gt), nBreak = changed(off, on, gl);
    const int nEnd1 = changed(off, on, afterEnd(e, first)), nEnd2 = changed(off, on, afterEnd(e, last));
    check(changed(off, off2, off.rect()) == 0 && nSpace > 0 && nTab > nSpace && nBreak > 0 && nEnd1 > 8 && nEnd2 > 8,
          QStringLiteral("on: a mark in the space, an arrow in the tab, a bent arrow at the line break, a pilcrow after each paragraph"),
          QStringLiteral("space %1, tab %2, break %3, ends %4 / %5").arg(nSpace).arg(nTab).arg(nBreak).arg(nEnd1).arg(nEnd2));
    check(changed(off, on, gapAt(e, posOf(d, QStringLiteral("ne two")))) == 0,
          QStringLiteral("... and nothing over the letters"));
    check(AppSettings::instance().showInvisibles && TextEditor::showInvisibles() && inv->isChecked(),
          QStringLiteral("the choice is the setting \"Show Invisibles\""));
    check(d->toPlainText() == text && d->isModified() == modified && d->availableUndoSteps() == undo
              && !(d->defaultTextOption().flags() & (QTextOption::ShowTabsAndSpaces | QTextOption::ShowLineAndParagraphSeparators)),
          QStringLiteral("the text is not touched: same text, not modified, no undo step, no marks in the document itself"));
    shot(&w, QStringLiteral("02-invisibles-on"));

    // kept: read back from the settings file
    AppSettings::instance().showInvisibles = false;
    AppSettings::instance().load();
    check(AppSettings::instance().showInvisibles, QStringLiteral("... saved in the settings (read back as on)"));

    // the combined text of a folder: every part shows them
    EditorPane *pane = visiblePane(w);
    pane->showItems({folder}, false, int(ViewMode::Combined));
    settle(500);
    const QList<TextEditor *> eds = pane->textView()->editors();
    if (eds.size() >= 2) {
        TextEditor *e2 = eds.at(1);
        e2->clearFocus();
        const QImage on2 = grabOf(e2);
        inv->trigger();
        settle(100);
        const QImage off3 = grabOf(e2);
        check(changed(off3, on2, afterEnd(e2, e2->document()->begin())) > 8,
              QStringLiteral("in the combined text of a folder, the second document shows them too"));
        shot(&w, QStringLiteral("03-invisibles-combined-off"));
        inv->trigger();
        settle(100);
        shot(&w, QStringLiteral("03-invisibles-combined-on"));
    } else {
        check(false, QStringLiteral("(combined text with two documents)"), QString::number(eds.size()));
    }
    // off again: the editor looks as before
    e = openAlone(w, doc);
    e->setTextCursor(QTextCursor(e->document()));
    w.setFocus();
    e->clearFocus();
    settle(100);
    const QImage onAgain = grabOf(e);
    inv->trigger();
    settle(100);
    const QImage again = grabOf(e);
    const bool sameSize = again.size() == off.size();
    check(!inv->isChecked() && !TextEditor::showInvisibles() && changed(onAgain, again, afterEnd(e, e->document()->begin())) > 8
              && (!sameSize || changed(off, again, off.rect()) == 0),
          QStringLiteral("off again: the marks are gone and the text looks exactly as before"),
          QStringLiteral("%1 pixels differ, same size %2").arg(sameSize ? changed(off, again, off.rect()) : -1).arg(sameSize));
    Q_UNUSED(p);
}

// ======================================================================= 3: the total of a folder
static void folderChecks(MainWindow &w, Project *p, BinderItem *folder, BinderItem *alpha, BinderItem *beta, BinderItem *loose,
                         BinderItem *withSubs)
{
    EditorPane *pane = visiblePane(w);
    if (!pane)
        return;
    const int total = folder->totalWordCount();
    check(total == 26, QStringLiteral("(the folder holds 26 words: 3 of its own, 5, 7 and 11 in a subfolder)"), QString::number(total));
    // buttons since 0.60.3 (a click shows the details)
    auto label = [&](const char *name) { return pane->findChild<QToolButton *>(QLatin1String(name)); };
    QToolButton *single = label("folderCount"), *cork = label("folderCountCorkboard"), *outl = label("folderCountOutliner");
    if (!single || !cork || !outl) {
        check(false, QStringLiteral("(the count labels are there)"));
        return;
    }

    pane->showItems({folder}, false, int(ViewMode::Single));
    settle(300);
    check(single->isVisible() && single->text() == QLatin1String("Folder: 26 words"),
          QStringLiteral("a folder shown alone: the footer shows \"Folder: 26 words\" beside its own count"), single->text());
    shot(&w, QStringLiteral("04-folder-total-text"));
    pane->showItems({alpha}, false, int(ViewMode::Single));
    settle(300);
    check(!single->isVisible() && pane->folderCountText().isEmpty(), QStringLiteral("a document without subdocuments: no total"));
    pane->showItems({withSubs}, false, int(ViewMode::Single));
    settle(300);
    check(single->isVisible() && single->text() == QLatin1String("With subdocuments: 5 words"),
          QStringLiteral("a document with subdocuments: \"With subdocuments: 5 words\""), single->text());

    pane->showItems({folder}, false, int(ViewMode::Corkboard));
    settle(400);
    check(cork->isVisible() && cork->text() == QLatin1String("Folder: 26 words"),
          QStringLiteral("corkboard of the folder: the total in its footer"), cork->text());
    shot(&w, QStringLiteral("05-folder-total-corkboard"));

    // counted again when a document of the folder changes
    QTextCursor c(p->docs()->text(beta->id));
    c.movePosition(QTextCursor::End);
    c.insertText(QStringLiteral(" two more"));
    settle(1200); // the counts follow the text 0.6 s after typing stops
    check(cork->text() == QLatin1String("Folder: 28 words"), QStringLiteral("... follows the text of its documents (28 after two words more)"),
          cork->text());
    // and when a document is moved into it
    p->model()->moveItems({loose}, folder, -1);
    settle(400);
    check(cork->text() == QLatin1String("Folder: 32 words"), QStringLiteral("... and documents moved into it (32)"), cork->text());

    pane->showItems({folder}, false, int(ViewMode::Outliner));
    settle(400);
    check(outl->isVisible() && outl->text() == QLatin1String("Folder: 32 words"),
          QStringLiteral("outliner of the folder: the total in its footer"), outl->text());
    pane->showItems({folder, withSubs}, false, int(ViewMode::Corkboard));
    settle(400);
    check(cork->text() == QLatin1String("Total: 37 words"), QStringLiteral("several items: \"Total: 37 words\" for all of them"), cork->text());
    pane->showItems({folder, alpha}, false, int(ViewMode::Corkboard));
    settle(400);
    check(cork->text() == QLatin1String("Folder: 32 words"),
          QStringLiteral("a folder and a document inside it: the document is not counted twice"), cork->text());

    AppSettings::instance().countCharacters = true;
    AppSettings::instance().notifyChanged();
    pane->showItems({folder}, false, int(ViewMode::Corkboard));
    settle(300);
    check(cork->text() == QStringLiteral("Folder: %1 characters").arg(QLocale().toString(folder->totalCharCount())),
          QStringLiteral("characters when the footer counts characters"), cork->text());
    AppSettings::instance().countCharacters = false;
    AppSettings::instance().notifyChanged();
    settle(200);

    // the inspector
    auto *binder = w.findChild<BinderPanel *>();
    auto *insp = w.findChild<QLabel *>(QStringLiteral("inspectorFolderCount"));
    if (!binder || !insp) {
        check(false, QStringLiteral("(binder and inspector there)"));
        return;
    }
    binder->select({folder}, true);
    settle(500);
    check(!insp->isHidden() && insp->text() == QLatin1String("32 words"), QStringLiteral("inspector of the folder: Total 32 words"),
          insp->text() + (insp->isHidden() ? QStringLiteral(" (hidden)") : QString()));
    binder->select({alpha}, true);
    settle(500);
    check(insp->isHidden(), QStringLiteral("inspector of a document without subdocuments: no total"));
}

// ======================================================================= 4: styles over paragraphs
static void styleChecks(MainWindow &w, Project *p, BinderItem *doc)
{
    TextEditor *e = openAlone(w, doc);
    if (!e)
        return;
    QTextDocument *d = e->document();
    auto mark = [&](const QString &text, const std::function<void(QTextCharFormat &)> &f) {
        QTextCursor c(d);
        const int at = posOf(d, text);
        c.setPosition(at);
        c.setPosition(at + int(text.size()), QTextCursor::KeepAnchor);
        QTextCharFormat cf;
        f(cf);
        c.mergeCharFormat(cf);
    };
    mark(QStringLiteral("red serif"), [](QTextCharFormat &cf) {
        cf.setFontFamilies({QStringLiteral("DejaVu Serif")});
        cf.setFontPointSize(16);
        cf.setForeground(QColor(200, 0, 0));
    });
    mark(QStringLiteral("bold"), [](QTextCharFormat &cf) { cf.setFontWeight(QFont::Bold); });
    mark(QStringLiteral("italic"), [](QTextCharFormat &cf) { cf.setFontItalic(true); });
    {
        const QTextBlock b = blockWith(d, QStringLiteral("Second paragraph"));
        QTextCursor c(d);
        c.setPosition(b.position());
        c.setPosition(b.position() + b.length() - 1, QTextCursor::KeepAnchor);
        QTextCharFormat cf;
        cf.setFontFamilies({QStringLiteral("DejaVu Sans Mono")});
        cf.setFontPointSize(13);
        c.mergeCharFormat(cf);
    }
    mark(QStringLiteral("underlined"), [](QTextCharFormat &cf) { cf.setFontUnderline(true); });
    mark(QStringLiteral("struck"), [](QTextCharFormat &cf) { cf.setFontStrikeOut(true); });
    mark(QStringLiteral("highlighted"), [](QTextCharFormat &cf) { cf.merge(text::highlightFormat(QColor(255, 230, 0))); });
    check(familyOf(formatAt(d, QStringLiteral("red serif"))) == QLatin1String("DejaVu Serif"), QStringLiteral("(formatting set by hand)"));
    const int undoBefore = d->availableUndoSteps();

    // from the middle of the first to the middle of the third paragraph, through the style command
    select(e, posOf(d, QStringLiteral("with red")), posOf(d, QStringLiteral("Third")) + 5);
    QAction *code = byKey(&w, QStringLiteral("style:Code Block"));
    check(code != nullptr, QStringLiteral("(Format > Style > Code Block)"));
    if (!code)
        return;
    code->trigger();
    settle(200);
    auto styleOf = [&](const QString &t) { return blockWith(d, t).blockFormat().stringProperty(props::Style); };
    check(styleOf(QStringLiteral("First")) == QLatin1String("Code Block") && styleOf(QStringLiteral("Second")) == QLatin1String("Code Block")
              && styleOf(QStringLiteral("Third")) == QLatin1String("Code Block") && styleOf(QStringLiteral("Fourth")).isEmpty(),
          QStringLiteral("a style chosen for a selection over three paragraphs styles all three, not the fourth"));
    const QTextCharFormat plain = formatAt(d, QStringLiteral("First")), red = formatAt(d, QStringLiteral("red serif"));
    check(familyOf(red) == familyOf(plain) && qAbs(red.fontPointSize() - plain.fontPointSize()) < 0.01
              && red.foreground().color() != QColor(200, 0, 0) && familyOf(plain) != QLatin1String("DejaVu Serif"),
          QStringLiteral("the font, size and color set by hand give way to the style's (also before the selection)"),
          familyOf(red) + QLatin1String(" / ") + familyOf(plain) + QStringLiteral(" %1 / %2 ").arg(red.fontPointSize()).arg(plain.fontPointSize())
              + red.foreground().color().name());
    check(familyOf(formatAt(d, QStringLiteral("Second paragraph"))) == familyOf(plain)
              && familyOf(formatAt(d, QStringLiteral("all in a hand"))) != QLatin1String("DejaVu Sans Mono"),
          QStringLiteral("... the hand-set font of the whole second paragraph too"), familyOf(formatAt(d, QStringLiteral("Second paragraph"))));
    check(formatAt(d, QStringLiteral("bold")).fontWeight() == QFont::Bold && formatAt(d, QStringLiteral("italic")).fontItalic()
              && formatAt(d, QStringLiteral("underlined")).fontUnderline() && formatAt(d, QStringLiteral("struck")).fontStrikeOut()
              && formatAt(d, QStringLiteral("highlighted")).boolProperty(props::Highlight),
          QStringLiteral("bold, italic, underline, strikethrough and the highlight stay"));
    bool noDirect = true;
    for (const QString &t : {QStringLiteral("First"), QStringLiteral("Second"), QStringLiteral("Third")})
        noDirect = noDirect && !(blockWith(d, t).blockFormat().intProperty(props::ParaDirect));
    check(noDirect, QStringLiteral("the paragraph formatting comes from the style"));
    shot(&w, QStringLiteral("06-style-over-paragraphs"));
    p->docs()->saveDocument(doc->id);
    QFile f(p->docs()->textPath(doc->id));
    const QString md = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    check(!md.contains(QLatin1String("DejaVu")) && md.contains(QLatin1String("Code Block")),
          QStringLiteral("saved: the style named, the fonts set by hand gone"), md.left(600));

    // one Undo
    e->undo();
    settle(100);
    check(familyOf(formatAt(d, QStringLiteral("red serif"))) == QLatin1String("DejaVu Serif") && styleOf(QStringLiteral("First")).isEmpty()
              && d->availableUndoSteps() == undoBefore,
          QStringLiteral("one Undo brings the fonts set by hand and the old paragraphs back"));

    // a selection down to the start of a paragraph (as Shift+Down from the start of the second makes)
    QTextCursor c(d);
    select(e, blockWith(d, QStringLiteral("Second")).position(), blockWith(d, QStringLiteral("Fourth")).position());
    const bool atStart = e->textCursor().selectionEnd() == blockWith(d, QStringLiteral("Fourth")).position();
    byKey(&w, QStringLiteral("style:Block Quote"))->trigger();
    settle(150);
    check(atStart && styleOf(QStringLiteral("Second")) == QLatin1String("Block Quote") && styleOf(QStringLiteral("Third")) == QLatin1String("Block Quote")
              && styleOf(QStringLiteral("Fourth")).isEmpty() && styleOf(QStringLiteral("First")).isEmpty(),
          QStringLiteral("a selection that ends at the start of a paragraph leaves that paragraph alone"),
          QStringLiteral("selection ends at the fourth: %1").arg(atStart));

    // a character style still goes to the selected text only
    select(e, posOf(d, QStringLiteral("hand set")), posOf(d, QStringLiteral("paragraph with struck")));
    byKey(&w, QStringLiteral("style:Emphasis"))->trigger();
    settle(150);
    check(formatAt(d, QStringLiteral("hand set")).stringProperty(props::CharStyle) == QLatin1String("Emphasis")
              && formatAt(d, QStringLiteral("Third")).stringProperty(props::CharStyle) == QLatin1String("Emphasis")
              && formatAt(d, QStringLiteral("Second")).stringProperty(props::CharStyle).isEmpty()
              && formatAt(d, QStringLiteral("struck")).stringProperty(props::CharStyle).isEmpty()
              && styleOf(QStringLiteral("Third")) == QLatin1String("Block Quote"),
          QStringLiteral("a character style still goes to the selected text only, the paragraphs keep their style"));

    // no selection: the paragraph at the cursor
    c.setPosition(blockWith(d, QStringLiteral("Fourth")).position() + 3);
    e->setTextCursor(c);
    byKey(&w, QStringLiteral("style:Block Quote"))->trigger();
    settle(150);
    check(styleOf(QStringLiteral("Fourth")) == QLatin1String("Block Quote") && styleOf(QStringLiteral("First")).isEmpty(),
          QStringLiteral("without a selection the style goes to the paragraph at the cursor"));
}

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QLatin1String("/data")).toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderV0601Test"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderV0601Test"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);

    check(Project::compareVersionText(QLatin1String(QB_APP_VERSION), QStringLiteral("0.60.1")) >= 0
              && QLatin1String(QB_APP_VERSION_SHOWN).endsWith(QLatin1String(" " QB_APP_VERSION_LABEL)),
          QStringLiteral("version 0.60.1 or newer, shown with its label"), QStringLiteral(QB_APP_VERSION_SHOWN));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.showInvisibles = false;
    as.countCharacters = false;
    as.toolbarMain.clear();
    as.save();
    theme::apply();
    compile::PluginManager::instance().load();

    QTemporaryDir work;
    templates::Info blank;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("blank"))
            blank = t;
    QString err;
    auto proj = templates::createProject(blank, work.path(), QStringLiteral("Folder Sums"), &err);
    check(proj != nullptr, QStringLiteral("(project made)"), err);
    if (!proj)
        return 1;
    Project *p = proj.get();
    chooseSerif(p);
    BinderItem *draft = p->draft();
    BinderItem *pasteDoc = textDoc(p, draft, QStringLiteral("Paste Here"), {QStringLiteral("Host paragraph of the paste.")});
    BinderItem *gaps = textDoc(p, draft, QStringLiteral("Gaps"),
                               {QStringLiteral("one two\tthree") + QChar(QChar::LineSeparator) + QStringLiteral("after a break"),
                                QStringLiteral("second line")});
    BinderItem *folder = textDoc(p, draft, QStringLiteral("Part One"), {QStringLiteral("Opening words here")}, ItemType::Folder);
    BinderItem *alpha = textDoc(p, folder, QStringLiteral("Alpha"), {QStringLiteral("one two three four five")});
    BinderItem *beta = textDoc(p, folder, QStringLiteral("Beta"), {QStringLiteral("one two three four five six seven")});
    BinderItem *sub = p->model()->createItem(folder, -1, ItemType::Folder, QStringLiteral("Inner"));
    textDoc(p, sub, QStringLiteral("Gamma"), {QStringLiteral("one two three four five six seven eight nine ten eleven")});
    BinderItem *loose = textDoc(p, draft, QStringLiteral("Loose"), {QStringLiteral("four words stand here")});
    BinderItem *withSubs = textDoc(p, draft, QStringLiteral("Long Scene"), {QStringLiteral("two words")});
    textDoc(p, withSubs, QStringLiteral("Its Part"), {QStringLiteral("three more words")});
    BinderItem *styled = textDoc(p, draft, QStringLiteral("Styled"),
                                 {QStringLiteral("First paragraph with red serif and bold and italic words."),
                                  QStringLiteral("Second paragraph all in a hand set font with an underlined word."),
                                  QStringLiteral("Third paragraph with struck and highlighted words."),
                                  QStringLiteral("Fourth paragraph stays.")});
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

    pasteChecks(w, p, pasteDoc);
    invisiblesChecks(w, p, gaps, folder);
    folderChecks(w, p, folder, alpha, beta, loose, withSubs);
    styleChecks(w, p, styled);

    settle(200);
    std::printf("\n%d failure(s)\n", failures);
    delete wp;
    return failures;
}
