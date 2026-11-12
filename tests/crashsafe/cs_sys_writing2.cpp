// The system test, stage 2: writing (S-W08 to S-W14, S-W16, S-W17, S-W19) and kinds of writing (S-K01 to
// S-K05), section 6.2. S-W15 and S-W18 are in stage 1 (cs_cases.cpp).
#include "cs.h"

#include "compile/CompileFormat.h"
#include "compile/Compiler.h"
#include "compile/DocxWriter.h"
#include "compile/EpubExporter.h"
#include "compile/PdfExporter.h"
#include "core/Zip.h"
#include "import/FileImporter.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "text/TextProps.h"
#include "text/DirectFormat.h"
#include "ui/SpellChecker.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"
#include "ui/Windows.h"

#include <QAbstractButton>
#include <QBuffer>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>
#include <QUrl>

#include <random>

namespace cs {

using namespace qb;
using namespace qb::compile;

namespace {

TextEditor *sceneWith(MainWindow *w, const QString &title, const QStringList &paras, BinderItem **made = nullptr)
{
    Project *p = w->project();
    BinderItem *it = p->model()->createItem(p->draft(), -1, ItemType::Text, title);
    if (!paras.isEmpty())
        fillText(p, it, paras);
    p->notifyItemChanged(it);
    if (made)
        *made = it;
    TextEditor *e = showDoc(w, it);
    if (e) {
        w->activateWindow();
        e->setFocus(Qt::OtherFocusReason);
    }
    return e;
}

void cursorTo(TextEditor *e, int pos, int anchor = -1)
{
    QTextCursor c(e->document());
    c.setPosition(qBound(0, anchor < 0 ? pos : anchor, e->document()->characterCount() - 1));
    if (anchor >= 0)
        c.setPosition(qBound(0, pos, e->document()->characterCount() - 1), QTextCursor::KeepAnchor);
    e->setTextCursor(c);
    // 0.64.2: the window first (after a dialog it is not active, and Edit > Cut / Paste / Undo act on the focused
    // widget: S-K04's cut, paste and undo went nowhere)
    e->window()->activateWindow();
    e->setFocus(Qt::OtherFocusReason);
    settle(20);
}

void toEnd(TextEditor *e)
{
    QTextCursor c = e->textCursor();
    c.movePosition(QTextCursor::End);
    e->setTextCursor(c);
    e->window()->activateWindow();
    e->setFocus(Qt::OtherFocusReason);
    settle(20);
}

QString compileDraft(Project *p, const QString &kind, const QString &file)
{
    Format f = formatByName(QStringLiteral("Paperback (6 x 9 in)"));
    AuthorInfo a;
    a.forename = QStringLiteral("Test");
    a.surname = QStringLiteral("Writer");
    Compiler cc(p, f, p->compile, a);
    const Manuscript m = cc.build();
    QString err;
    bool ok = false;
    if (kind == QLatin1String("pdf")) {
        PdfExporter ex;
        ok = ex.exportManuscript(m, file, &err);
    } else if (kind == QLatin1String("epub")) {
        EpubExporter ex;
        ok = ex.exportManuscript(m, file, &err);
    } else {
        DocxExporter ex;
        ok = ex.exportManuscript(m, file, &err);
    }
    if (!ok)
        note(QStringLiteral("compile to %1: %2").arg(kind, err));
    return ok ? file : QString();
}

int countTables(QTextDocument *d)
{
    int n = 0;
    std::function<void(QTextFrame *)> walk = [&](QTextFrame *f) {
        for (QTextFrame *c : f->childFrames()) {
            if (qobject_cast<QTextTable *>(c))
                ++n;
            walk(c);
        }
    };
    if (d)
        walk(d->rootFrame());
    return n;
}

// only the given documents in the Draft are compiled (the others left out)
void compileOnly(Project *p, const QList<BinderItem *> &keep)
{
    QSet<BinderItem *> k(keep.begin(), keep.end());
    for (BinderItem *it : p->allItems()) {
        if (it->type != ItemType::Text)
            continue;
        BinderItem *x = it;
        bool in = false;
        while (x) {
            if (k.contains(x))
                in = true;
            x = x->parent;
        }
        it->includeInCompile = in;
    }
}

QString epubText(const QString &epub)
{
    ZipReader z(epub);
    QString out;
    for (const QString &n : z.fileNames())
        if (n.endsWith(QLatin1String(".xhtml")) || n.endsWith(QLatin1String(".html")))
            out += QString::fromUtf8(z.fileData(n));
    return out;
}

void setClip(const QMap<QString, QByteArray> &formats, const QString &text = QString(), const QImage &img = QImage())
{
    auto *md = new QMimeData;
    for (auto i = formats.begin(); i != formats.end(); ++i)
        md->setData(i.key(), i.value());
    if (!text.isNull())
        md->setText(text);
    if (!img.isNull())
        md->setImageData(img);
    QApplication::clipboard()->setMimeData(md);
    settle(50);
}

// FX-7, made by hand (no LibreOffice here): what other programs put on the clipboard
struct ClipSource {
    QString what;
    QMap<QString, QByteArray> formats;
    QString text;
    QImage image;
};
QList<ClipSource> fx7()
{
    QList<ClipSource> out;
    const QByteArray writerTable =
        "<html><body><table border=\"1\"><tr><td colspan=\"2\">Merged head</td><td>C</td></tr><tr><td>a1</td><td>b1</td><td>c1</td></tr>"
        "<tr><td>a2</td><td>b2</td><td>c2</td></tr></table></body></html>";
    const QByteArray rtfTable =
        "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Liberation Serif;}}"
        "\\trowd\\cellx2000\\clmgf\\cellx4000\\clmrg\\cellx6000\\pard\\intbl A1\\cell Merged B1\\cell \\cell\\row"
        "\\trowd\\cellx2000\\cellx4000\\cellx6000\\pard\\intbl A2\\cell B2\\cell C2\\cell\\row\\pard Text after.\\par}";
    out << ClipSource{QStringLiteral("a PNG picture"), {}, QString(), testPicture(200, 120)};
    {
        QByteArray jpg;
        QBuffer b(&jpg);
        b.open(QIODevice::WriteOnly);
        testPicture(160, 90, 0xffaa4422).save(&b, "JPEG");
        out << ClipSource{QStringLiteral("a JPEG picture"), {{QStringLiteral("image/jpeg"), jpg}}, QString(), QImage()};
    }
    {
        QByteArray png;
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        testPicture(40, 40).save(&b, "PNG");
        out << ClipSource{QStringLiteral("HTML with an embedded picture"),
                          {{QStringLiteral("text/html"), "<p>Picture: <img src=\"data:image/png;base64," + png.toBase64() + "\"></p>"}}, QStringLiteral("Picture:"), QImage()};
    }
    out << ClipSource{QStringLiteral("HTML with a picture on the web"), {{QStringLiteral("text/html"), "<p>Web picture <img src=\"https://example.org/x.png\" alt=\"x\"></p>"}},
                      QStringLiteral("Web picture"), QImage()};
    out << ClipSource{QStringLiteral("a LibreOffice Writer table (HTML, merged cells)"), {{QStringLiteral("text/html"), writerTable}}, QStringLiteral("Merged head\tC\na1\tb1\tc1"), QImage()};
    out << ClipSource{QStringLiteral("a Word table (RTF, merged cells)"), {{QStringLiteral("text/rtf"), rtfTable}}, QStringLiteral("A1\tMerged B1"), QImage()};
    out << ClipSource{QStringLiteral("a web page table (HTML)"),
                      {{QStringLiteral("text/html"), "<table><thead><tr><th>Name</th><th rowspan=\"2\">Merged</th></tr></thead><tr><td>x</td></tr></table>"}},
                      QStringLiteral("Name\tMerged\nx"), QImage()};
    out << ClipSource{QStringLiteral("LibreOffice Calc cells (HTML and tabs, merged)"),
                      {{QStringLiteral("text/html"), "<table><tr><td colspan=2>Sum</td></tr><tr><td>1</td><td>2</td></tr></table>"}}, QStringLiteral("Sum\t\n1\t2\n"), QImage()};
    out << ClipSource{QStringLiteral("a bulleted list"), {{QStringLiteral("text/html"), "<ul><li>one</li><li>two</li></ul>"}}, QStringLiteral("one\ntwo"), QImage()};
    out << ClipSource{QStringLiteral("a numbered list"), {{QStringLiteral("text/html"), "<ol><li>first</li><li>second</li></ol>"}}, QStringLiteral("first\nsecond"), QImage()};
    out << ClipSource{QStringLiteral("text with footnotes from LibreOffice (RTF)"),
                      {{QStringLiteral("text/rtf"), "{\\rtf1\\ansi Text with a note{\\super\\chftn}{\\footnote\\pard\\plain{\\super\\chftn} The note.}.\\par}"}},
                      QStringLiteral("Text with a note1."), QImage()};
    return out;
}

} // namespace

// ================================================================ S-W08 find and replace in a document
void caseSW08()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    TextEditor *e = sceneWith(w, QStringLiteral("Find"), {QStringLiteral("The cat sat on the mat. The Cat ran."), QStringLiteral("A category of cats."),
                                                           QStringLiteral("The end of the cat story.")});
    if (!e)
        return;
    cursorTo(e, 6, 0);
    QTest::keyClick(e, Qt::Key_E, Qt::ControlModifier);   // Use Selection for Find
    chooseByMenu(w, QStringLiteral("find"));
    settle(200);
    // 0.64.2: each editor pane has a find bar of its own: the one of the pane shown
    QWidget *pane = visiblePane(w) ? static_cast<QWidget *>(visiblePane(w)) : static_cast<QWidget *>(w);
    auto *field = pane->findChild<QLineEdit *>(QStringLiteral("findField"));
    auto *count = pane->findChild<QLabel *>(QStringLiteral("findCount"));
    check(field && field->isVisible(), QStringLiteral("Edit > Find shows the find bar"));
    if (!field)
        return;
    field->setText(QStringLiteral("cat"));
    settle(200);
    fact(QStringLiteral("S-W08 count for \"cat\""), count ? count->text() : QString());
    for (int i = 0; i < 4; ++i)
        chooseByMenu(w, QStringLiteral("findNext"));
    chooseByMenu(w, QStringLiteral("findPrev"));
    for (QCheckBox *cb : w->findChildren<QCheckBox *>())
        if (cb->isVisible() && (cb->text().contains(QLatin1String("case")) || cb->text().contains(QLatin1String("Whole"))))
            cb->setChecked(true);
    settle(200);
    fact(QStringLiteral("S-W08 count with case and whole words"), count ? count->text() : QString());
    const QString before = rawText(e->document());
    chooseByMenu(w, QStringLiteral("replace"));
    auto *with = pane->findChild<QLineEdit *>(QStringLiteral("replaceField"));
    auto *all = pane->findChild<QPushButton *>(QStringLiteral("replaceAll"));
    if (with && all) {
        with->setText(QStringLiteral("dog"));
        all->click();
        settle(200);
        check(!e->toPlainText().contains(QLatin1String(" cat ")) && e->toPlainText().contains(QLatin1String("dog")), QStringLiteral("Replace All with case and whole words"));
        // a Replace All that empties the document
        for (QCheckBox *cb : w->findChildren<QCheckBox *>())
            if (cb->isVisible())
                cb->setChecked(false);
        field->setText(QStringLiteral("."));
        with->setText(QString());
        all->click();
        settle(200);
        fact(QStringLiteral("S-W08 the find bar has regular expressions"), w->findChild<QCheckBox *>(QStringLiteral("findRegex")) ? QStringLiteral("yes") : QStringLiteral("no (Project Replace has them)"));
        // 0.64.2: the focus is in the find field after Replace All, where Edit > Undo undoes the field: back to the text
        e->setFocus(Qt::OtherFocusReason);
        settle(100);
        for (int i = 0; i < 10 && rawText(e->document()) != before; ++i) {
            e->setFocus(Qt::OtherFocusReason);
            chooseByMenu(w, QStringLiteral("undo"));
        }
        check(rawText(e->document()) == before, QStringLiteral("undo brings the text back"), firstDifference(before, rawText(e->document())));
    }
    finishProject(w);
    delete w;
}

// ================================================================ S-W09 the Insert menu
void caseSW09()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    TextEditor *e = sceneWith(w, QStringLiteral("Insert menu"), {QStringLiteral("Start.")});
    if (!e)
        return;
    const QStringList entries = {
        QStringLiteral("Auto Number > 1, 2, 3 …   <$n>"), QStringLiteral("Auto Number > one, two, three …   <$w>"), QStringLiteral("Auto Number > One, Two, Three …   <$W>"),
        QStringLiteral("Auto Number > I, II, III …   <$R>"), QStringLiteral("Auto Number > i, ii, iii …   <$r>"), QStringLiteral("Auto Number > a, b, c …   <$l>"),
        QStringLiteral("Auto Number > A, B, C …   <$L>"), QStringLiteral("Placeholders > Project title   <$projecttitle>"), QStringLiteral("Placeholders > Author   <$author>"),
        QStringLiteral("Placeholders > Author's last name   <$surname>"), QStringLiteral("Placeholders > Word count (rounded to 100)   <$wc100>"),
        QStringLiteral("Placeholders > Compile date   <$date>"), QStringLiteral("Special Characters > Em dash —"), QStringLiteral("Special Characters > En dash –"),
        QStringLiteral("Special Characters > Ellipsis …"), QStringLiteral("Special Characters > Non-breaking space"), QStringLiteral("Special Characters > Section sign §"),
        QStringLiteral("Special Characters > Bullet •")};
    QStringList missing;
    for (const QString &en : entries) {
        toEnd(e);
        if (!menuPath(w, QStringLiteral("Insert > ") + en))
            missing << en;
        const QRegularExpression tag(QStringLiteral("<\\$[a-zA-Z0-9]+>"));
        const auto m = tag.match(en);
        if (m.hasMatch() && !e->toPlainText().contains(m.captured(0)))
            missing << QStringLiteral("not in the text: ") + m.captured(0);
    }
    for (const QString &a : {QStringLiteral("insertDate"), QStringLiteral("insertTime")}) {
        toEnd(e);
        check(chooseByMenu(w, a), QStringLiteral("Insert > %1").arg(a));
    }
    check(missing.isEmpty(), QStringLiteral("every Auto Number, Placeholder and Special Character entry puts its tag into the text"), missing.join(QLatin1String("; ")));
    QAction *ts = action(w, QStringLiteral("mediaTimestamp"));
    check(ts && !ts->isEnabled(), QStringLiteral("Media Timestamp is disabled without media"));
    withModal<QDialog>(400, [](QDialog *d) {
        shot(d, QStringLiteral("placeholders"));
        d->accept();
    });
    chooseByMenu(w, QStringLiteral("placeholders"));
    settle(600);
    finishProject(w);
    delete w;
}

// ================================================================ S-W10 revision mode
void caseSW10()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    BinderItem *it = nullptr;
    TextEditor *e = sceneWith(w, QStringLiteral("Revisions"), atParagraphs(2800, 200), &it);
    if (!e)
        return;
    QStringList colors;
    for (int level = 1; level <= 5; ++level) {
        chooseByMenu(w, QStringLiteral("rev%1").arg(level));
        toEnd(e);
        typeInto(e, QStringLiteral(" Revision %1 text.").arg(level));
        cursorTo(e, 20 * level + 5, 20 * level);
        QTest::keyClick(e, Qt::Key_Delete);
        QTextCursor c(e->document());
        c.setPosition(e->document()->characterCount() - 4);
        colors << c.charFormat().foreground().color().name();
    }
    chooseByMenu(w, QStringLiteral("comment"));
    chooseByMenu(w, QStringLiteral("rev0"));
    toEnd(e);
    typeInto(e, QStringLiteral(" Without revision."));
    fact(QStringLiteral("S-W10 colors of the levels"), colors.join(QLatin1String(", ")));
    const QString runs = runsOf(e->document());
    const QString title = it->title;
    w = reopenWindow(w);
    Project *p = w->project();
    BinderItem *again = p ? itemByTitle(p, title) : nullptr;
    check(again && runsOf(p->docs()->text(again->id)) == runs, QStringLiteral("the revision colors are kept after reopening"),
          again ? firstDifference(runs, runsOf(p->docs()->text(again->id)), 60) : QString());
    finishProject(w);
    delete w;
}

// ================================================================ S-W11 a very long document
void caseSW11()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    TextEditor *e = sceneWith(w, QStringLiteral("Long"), {});
    if (!e)
        return;
    QString big;
    for (quint32 s = 0; big.size() < 360000; ++s)
        big += atParagraphs(2900 + s, 3000).join(QLatin1Char('\n')) + QLatin1Char('\n');
    auto *md = new QMimeData;
    md->setText(big);
    QApplication::clipboard()->setMimeData(md);
    QElapsedTimer t;
    t.start();
    chooseByMenu(w, QStringLiteral("paste"));
    fact(QStringLiteral("S-W11 paste of about 60,000 words (ms)"), int(t.elapsed()));
    fact(QStringLiteral("S-W11 words"), text::countWords(e->toPlainText()));
    QTest::keyClick(e, Qt::Key_End, Qt::ControlModifier);
    cursorTo(e, e->document()->characterCount() / 2);
    t.restart();
    typeInto(e, atParagraphs(2950, 200).join(QLatin1Char(' ')));
    fact(QStringLiteral("S-W11 200 words typed in the middle (ms per key)"), double(t.elapsed()) / 1200.0);
    chooseByMenu(w, QStringLiteral("selectAll"));
    t.restart();
    chooseByMenu(w, QStringLiteral("style:Block Quote"));
    fact(QStringLiteral("S-W11 a style on 60,000 words (ms)"), int(t.elapsed()));
    chooseByMenu(w, QStringLiteral("undo"));
    for (const QString &a : {QStringLiteral("pageView"), QStringLiteral("pageView"), QStringLiteral("typewriter"), QStringLiteral("typewriter"), QStringLiteral("fixedWidth"),
                             QStringLiteral("fixedWidth"), QStringLiteral("showInvisibles")}) {
        t.restart();
        chooseByMenu(w, a);
        settle(300);
        fact(QStringLiteral("S-W11 %1 (ms)").arg(a), int(t.elapsed()));
    }
    for (int i = 0; i < 12; ++i)
        chooseByMenu(w, QStringLiteral("zoomOut"));
    for (int i = 0; i < 24; ++i)
        chooseByMenu(w, QStringLiteral("zoomIn"));
    chooseByMenu(w, QStringLiteral("zoomReset"));
    chooseByMenu(w, QStringLiteral("showInvisibles"));
    checkCleanWindows(w);
    toEnd(e);
    for (const QString &profile : {QStringLiteral("slow"), QStringLiteral("fast")})
        typingGate(e, profile, 60, QStringLiteral("S-W11"));
    finishProject(w);
    delete w;
}

// ================================================================ S-W12 titles from the first line
void caseSW12()
{
    for (const QString &fx : {QStringLiteral("fx1-qb"), QStringLiteral("fx1-scriv")}) {
        MainWindow *w = openWindow(fixtureCopy(fx));
        Project *p = w->project();
        if (!p)
            continue;
        struct First {
            QString kind;
            QString text;
        };
        const QList<First> firsts = {{QStringLiteral("plain"), QStringLiteral("A plain first line")},
                                     {QStringLiteral("80 characters"), QString(80, QLatin1Char('w'))},
                                     {QStringLiteral("only spaces"), QStringLiteral("     ")},
                                     {QStringLiteral("an inline footnote first"), QString()},
                                     {QStringLiteral("a picture first"), QString()}};
        QList<BinderItem *> made;
        for (int i = 0; i < 10; ++i) {
            selectItems(w, {p->draft()});
            chooseByMenu(w, QStringLiteral("newText"));
            settle(200);
            BinderItem *it = binderOf(w)->currentItem();
            TextEditor *e = it ? showDoc(w, it) : nullptr;
            if (!e)
                continue;
            const First &f = firsts.at(i % firsts.size());
            if (f.kind == QLatin1String("an inline footnote first")) {
                chooseByMenu(w, QStringLiteral("inlineFootnote"));
                typeInto(e, QStringLiteral("the note"));
                QTest::keyClick(e, Qt::Key_End);
                typeInto(e, QStringLiteral(" Then the words"));
            } else if (f.kind == QLatin1String("a picture first")) {
                insertPicture(p, it, e->document(), 0, testPicture(30, 20));
                toEnd(e);
                typeInto(e, QStringLiteral(" After the picture"));
            } else {
                typeInto(e, f.text);
            }
            typeInto(e, QStringLiteral("\nSecond line."));
            settle(700);
            made << it;
            fact(QStringLiteral("S-W12 %1, %2").arg(fx, f.kind), it->title);
            check(it->title.size() <= 60, QStringLiteral("%1, %2: the title has at most 60 characters").arg(fx, f.kind), it->title);
        }
        if (!made.isEmpty()) {
            BinderItem *it = made.first();
            check(it->title == QLatin1String("A plain first line"), QStringLiteral("%1: the title follows the first line").arg(fx), it->title);
            p->model()->setData(p->model()->indexFor(it), QStringLiteral("Named by hand"), Qt::EditRole);
            if (TextEditor *e = showDoc(w, it)) {
                cursorTo(e, 0);
                typeInto(e, QStringLiteral("Changed "));
                settle(700);
            }
            check(it->title == QLatin1String("Named by hand"), QStringLiteral("%1: renamed by hand, the title no longer follows").arg(fx), it->title);
        }
        finishProject(w);
        delete w;
    }
}

// ================================================================ S-W13 composition mode and full screen
void caseSW13()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    const QRect geometry = w->geometry();
    BinderItem *it = nullptr;
    TextEditor *e = sceneWith(w, QStringLiteral("Composed"), {QStringLiteral("Start.")}, &it);
    if (!e)
        return;
    chooseByMenu(w, QStringLiteral("composition"));
    settle(800);
    CompositionWindow *cw = nullptr;
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (auto *c = qobject_cast<CompositionWindow *>(tl); c && c->isVisible())
            cw = c;
    check(cw != nullptr, QStringLiteral("View > Composition Mode opens"));
    QString want;
    if (cw) {
        TextEditor *ce = cw->findChild<TextEditor *>();
        if (ce) {
            cw->activateWindow();
            ce->setFocus(Qt::OtherFocusReason);
            QTextCursor c = ce->textCursor();
            c.movePosition(QTextCursor::End);
            ce->setTextCursor(c);
            want = typeLikeWriter(ce, QLatin1Char('\n') + atParagraphs(3000, ctx().smoke ? 60 : ctx().quick ? 250 : 500).join(QLatin1Char('\n')), 3000);
            QTest::keyClick(ce, Qt::Key_Plus, Qt::ControlModifier);
            QTest::keyClick(ce, Qt::Key_Minus, Qt::ControlModifier);
            shot(cw, QStringLiteral("composition"));
        }
        QTest::keyClick(cw, Qt::Key_Escape);
        settle(600);
    }
    Project *p = w->project();
    check(rawText(p->docs()->text(it->id)) == want, QStringLiteral("the text typed in composition mode equals the model"), firstDifference(want, rawText(p->docs()->text(it->id))));
    chooseByMenu(w, QStringLiteral("splitV"));
    chooseByMenu(w, QStringLiteral("fullScreen"));
    settle(600);
    chooseByMenu(w, QStringLiteral("fullScreen"));
    settle(600);
    chooseByMenu(w, QStringLiteral("noSplit"));
    fact(QStringLiteral("S-W13 the window after full screen"), QStringLiteral("%1 x %2 (before %3 x %4)").arg(w->width()).arg(w->height()).arg(geometry.width()).arg(geometry.height()));
    check(w->size() == geometry.size(), QStringLiteral("the window comes back to its size"));
    // composition mode with a 60,000-word document
    QString big;
    for (quint32 s = 0; big.size() < 360000; ++s)
        big += atParagraphs(3100 + s, 3000).join(QLatin1Char('\n')) + QLatin1Char('\n');
    TextEditor *l = sceneWith(w, QStringLiteral("Long composed"), {});
    if (l) {
        QTextCursor c(l->document());
        c.insertText(big);
        QElapsedTimer t;
        t.start();
        chooseByMenu(w, QStringLiteral("composition"));
        settle(1500);
        fact(QStringLiteral("S-W13 composition mode with 60,000 words opens in (ms)"), int(t.elapsed()));
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *c2 = qobject_cast<CompositionWindow *>(tl); c2 && c2->isVisible())
                QTest::keyClick(c2, Qt::Key_Escape);
        settle(600);
    }
    finishProject(w);
    delete w;
}

// ================================================================ S-W14 undo marathon
void caseSW14()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    BinderItem *it = nullptr;
    TextEditor *e = sceneWith(w, QStringLiteral("Marathon"), atParagraphs(3200, 400), &it);
    if (!e)
        return;
    e->document()->clearUndoRedoStacks();
    const QString start = rawText(e->document()) + runsOf(e->document());
    std::mt19937 r(ctx().seed);
    const int edits = ctx().smoke ? 80 : ctx().quick ? 600 : 2000;
    for (int i = 0; i < edits; ++i) {
        const int len = e->document()->characterCount();
        const int at = int(r() % quint32(qMax(2, len - 2)));
        switch (r() % 8) {
        case 0: case 1: case 2:
            cursorTo(e, at);
            QTest::keyClicks(e, QStringLiteral("word "));
            break;
        case 3:
            cursorTo(e, qMin(len - 1, at + 8), at);
            QTest::keyClick(e, Qt::Key_B, Qt::ControlModifier);
            break;
        case 4:
            cursorTo(e, at);
            chooseByMenu(w, i % 2 ? QStringLiteral("style:Block Quote") : QStringLiteral("style:"));
            break;
        case 5:
            cursorTo(e, qMin(len - 1, at + 4), at);
            chooseByMenu(w, i % 3 ? QStringLiteral("annotation") : QStringLiteral("comment"));
            break;
        case 6:
            cursorTo(e, qMin(len - 1, at + 3), at);
            QTest::keyClick(e, Qt::Key_Delete);
            break;
        default:
            if (i % 50 == 7) {
                showDoc(w, textItems(p).value(int(r() % 30)));
                settle(100);
                e = showDoc(w, it);
            } else {
                cursorTo(e, at);
                QTest::keyClick(e, Qt::Key_Return);
            }
        }
    }
    const QString end = rawText(e->document()) + runsOf(e->document());
    QElapsedTimer t;
    t.start();
    int undone = 0;
    while (e->document()->isUndoAvailable() && undone < 20000) {
        e->undo();
        ++undone;
    }
    fact(QStringLiteral("S-W14 undo steps (and ms)"), QStringLiteral("%1 (%2)").arg(undone).arg(t.elapsed()));
    check(rawText(e->document()) + runsOf(e->document()) == start, QStringLiteral("after undo all, the text equals the start"), firstDifference(start, rawText(e->document()) + runsOf(e->document()), 60));
    while (e->document()->isRedoAvailable())
        e->redo();
    check(rawText(e->document()) + runsOf(e->document()) == end, QStringLiteral("after redo all, the end state"), firstDifference(end, rawText(e->document()) + runsOf(e->document()), 60));
    // the toolbar buttons at each place of the focus
    QAction *undo = action(w, QStringLiteral("undo"));
    QAction *redo = action(w, QStringLiteral("redo"));
    struct Place {
        QString what;
        std::function<QWidget *()> focus;
    };
    const QList<Place> places = {
        {QStringLiteral("the editor"), [&]() -> QWidget * { return e; }},
        {QStringLiteral("the notes"), [&]() -> QWidget * { for (TextEditor *t2 : w->findChildren<TextEditor *>()) if (t2 != e && t2->isVisible() && !t2->window()->inherits("QDialog")) return t2; return nullptr; }},
        {QStringLiteral("the synopsis"), [&]() -> QWidget * { for (QPlainTextEdit *s : w->findChildren<QPlainTextEdit *>()) if (s->isVisible()) return s; return nullptr; }},
    };
    QStringList seen;
    for (const Place &pl : places) {
        QWidget *f = pl.focus();
        if (!f) {
            seen << pl.what + QStringLiteral(": not shown");
            continue;
        }
        f->setFocus(Qt::OtherFocusReason);
        QTest::keyClicks(f, QStringLiteral("xyz"));
        settle(80);
        w->updateFormatBar();
        const bool canUndo = undo && undo->isEnabled();
        if (undo)
            undo->trigger();
        settle(50);
        const bool canRedo = redo && redo->isEnabled();
        if (redo)
            redo->trigger();
        seen << QStringLiteral("%1: undo %2, redo %3").arg(pl.what).arg(canUndo ? QStringLiteral("enabled") : QStringLiteral("disabled")).arg(canRedo ? QStringLiteral("enabled") : QStringLiteral("disabled"));
    }
    fact(QStringLiteral("S-W14 the toolbar's Undo and Redo per place (U-17)"), seen.join(QLatin1String("; ")));
    finishProject(w);
    delete w;
}

// ================================================================ S-W16 clipboard with pictures and tables
void caseSW16()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    TextEditor *e = sceneWith(w, QStringLiteral("Pasted things"), {QStringLiteral("Start.")});
    if (!e)
        return;
    QStringList baseline;
    for (const ClipSource &src : fx7()) {
        for (const QString &how : {QStringLiteral("paste"), QStringLiteral("pastePlain"), QStringLiteral("pasteKeep")}) {
            setClip(src.formats, src.text, src.image);
            toEnd(e);
            QTest::keyClick(e, Qt::Key_Return);
            const QString beforeShape = tableShape(e->document());
            const int frames = int(e->document()->rootFrame()->childFrames().size());
            QElapsedTimer t;
            t.start();
            chooseByMenu(w, how);
            settle(200);
            const QString afterShape = tableShape(e->document());
            const int framesAfter = int(e->document()->rootFrame()->childFrames().size());
            QString got = framesAfter > frames ? QStringLiteral("a table or figure") : QStringLiteral("text");
            if (e->toPlainText().contains(QChar(QChar::ObjectReplacementCharacter)))
                got += QStringLiteral(" (pictures in the text)");
            baseline << QStringLiteral("%1, %2: %3 (%4 ms)").arg(src.what, how, got).arg(t.elapsed());
            check(t.elapsed() < 10000, QStringLiteral("%1 with %2: no freeze").arg(src.what, how));
            if (src.what.startsWith(QLatin1String("a Word table")) && how == QLatin1String("paste"))
                check(afterShape != beforeShape, QStringLiteral("an RTF table arrives as a table in the main text"), afterShape.right(200));
        }
    }
    writeText(ctx().dir + QStringLiteral("/paste-baseline.txt"), baseline.join(QLatin1Char('\n')).toUtf8());
    fact(QStringLiteral("S-W16 what each source gives"), baseline.join(QLatin1String("; ")).left(3000));
    // a table where tables are not offered: synopsis, a comment, a title
    const ClipSource table = fx7().at(5);
    for (QPlainTextEdit *s : w->findChildren<QPlainTextEdit *>())
        if (s->isVisible()) {
            setClip(table.formats, table.text);
            s->setFocus(Qt::OtherFocusReason);
            chooseByMenu(w, QStringLiteral("paste"));
            fact(QStringLiteral("S-W16 a table into the synopsis"), s->toPlainText().right(60));
            break;
        }
    finishProject(w);
    delete w;
}

// ================================================================ S-W17 every key on the German and the US keyboard
namespace {
QString runSender(const QStringList &args, int ms)
{
    QProcess pr;
    pr.setProcessChannelMode(QProcess::MergedChannels);
    pr.start(QStringLiteral("python3"), QStringList{ctx().sourceDir + QStringLiteral("/tests/crashsafe/keys.py")} + args);
    if (!pr.waitForStarted(5000))
        return QStringLiteral("(the sender did not start)");
    waitFor([&pr] { return pr.state() == QProcess::NotRunning; }, ms);
    if (pr.state() != QProcess::NotRunning) {
        pr.terminate();
        pr.waitForFinished(2000);
    }
    return QString::fromUtf8(pr.readAll());
}
} // namespace

void caseSW17()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    QStringList findings;
    for (const QString &layout : {QStringLiteral("de"), QStringLiteral("us")}) {
        TextEditor *e = sceneWith(w, QStringLiteral("Keys %1").arg(layout), {});
        if (!e)
            continue;
        int rc = -1;
        runTool(QStringLiteral("setxkbmap"), {layout}, &rc, 5000);
        // the layout's own key table of the virtual display
        const QString table = runTool(QStringLiteral("xmodmap"), {QStringLiteral("-pke")}, &rc, 5000);
        writeText(ctx().dir + QStringLiteral("/keytable-") + layout + QStringLiteral(".txt"), table.toUtf8());
        w->activateWindow();
        e->setFocus(Qt::OtherFocusReason);
        settle(200);
        const QString active = runTool(QStringLiteral("setxkbmap"), {QStringLiteral("-query")}, &rc, 5000);
        fact(QStringLiteral("S-W17 the layout asked for / active"), layout + QStringLiteral(" / ") + active.simplified());
        // 0.64.1: on a display that keeps the US layout the German half would only repeat the US one:
        // it is reported as not run, never as passed (the key table shows no ä then)
        if (layout == QLatin1String("de") && !table.contains(QLatin1String("adiaeresis"))) {
            note(QStringLiteral("S-W17: the virtual display does not take the German layout (setxkbmap -query: %1): the German half is not run").arg(active.simplified()));
            fact(QStringLiteral("S-W17 German half"), QStringLiteral("not run: the display keeps the US layout"));
            findings << QStringLiteral("de: not run (the display keeps the US layout)");
            continue;
        }
        const QString out = runSender({QStringLiteral("--log"), ctx().dir + QStringLiteral("/keys-") + layout + QStringLiteral(".log")}, 180000);
        settle(500);
        const QString expected = readText(ctx().dir + QStringLiteral("/keys-") + layout + QStringLiteral(".log.expected"));
        const QString got = e->toPlainText();
        writeText(ctx().dir + QStringLiteral("/keys-") + layout + QStringLiteral(".got"), got.toUtf8());
        if (expected.isEmpty()) {
            note(QStringLiteral("the sender gave no expected text for %1 (%2)").arg(layout, out.left(200)));
            findings << layout + QStringLiteral(": keys.py gave no expected text");
        } else {
            check(got == expected, QStringLiteral("%1: every key gives the character of the layout's key table").arg(layout), firstDifference(expected, got, 40));
        }
        // the shortcuts of the menus on this layout
        QStringList untypeable;
        for (QAction *a : w->findChildren<QAction *>()) {
            const QKeySequence k = a->shortcut();
            if (k.isEmpty())
                continue;
            if (k.toString().contains(QLatin1String("Alt+")) && layout == QLatin1String("de") && k.toString().contains(QRegularExpression(QStringLiteral("[\\[\\]{}\\\\|@~]"))))
                untypeable << QStringLiteral("%1 (%2)").arg(k.toString(), a->text().remove(QLatin1Char('&')));
        }
        if (!untypeable.isEmpty())
            findings << QStringLiteral("%1: shortcuts that need AltGr: %2").arg(layout, untypeable.join(QLatin1String(", ")));
    }
    int rc = -1;
    runTool(QStringLiteral("setxkbmap"), {QStringLiteral("de")}, &rc, 5000);
    fact(QStringLiteral("S-W17 findings for U-02"), findings.join(QLatin1String("; ")));
    finishProject(w);
    delete w;
}

// ================================================================ S-W19 the spelling menu at every place of a word
void caseSW19()
{
    SpellChecker &sc = SpellChecker::instance();
    AppSettings &as = AppSettings::instance();
    as.spellCheck = true;
    as.spellLanguage = QStringLiteral("en_US");
    as.notifyChanged();   // 0.64.2: save() alone does not reach the windows (the spelling language)
    // save() hands the settings to the modules
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    TextEditor *e = sceneWith(w, QStringLiteral("Apostrophes"), {QStringLiteral("dont stop at the start"), QStringLiteral("the writers's pen and Annaz’ book"),
                                                                 QStringLiteral("at oclokc it ended o'clok")});
    if (!e)
        return;
    settle(500);
    const QStringList words = {QStringLiteral("dont"), QStringLiteral("writers's"), QStringLiteral("Annaz’"), QStringLiteral("o'clok"), QStringLiteral("oclokc")};
    QList<qint64> times;
    QStringList partial;
    for (const QString &wd : words) {
        const int at = int(e->toPlainText().indexOf(wd));
        if (at < 0)
            continue;
        for (int k = 0; k < wd.size(); ++k) {
            QStringList entries;
            QElapsedTimer t;
            t.start();
            rightClickChoose(e, at + k, QStringLiteral("(no entry: the menu is read)"), &entries);
            times << t.elapsed();
            // 0.64.2: an apostrophe at a word's end is not checked with it (it is also the closing single
            // quotation mark), so "Annaz’" is offered as "Annaz"
            QString checked = wd;
            while (checked.endsWith(QLatin1Char('\'')) || checked.endsWith(QChar(0x2019)))
                checked.chop(1);
            bool whole = false;
            for (const QString &en : entries)
                if (en.contains(QStringLiteral("\"%1\"").arg(checked)))
                    whole = true;
            if (!whole)
                partial << QStringLiteral("%1 at %2").arg(wd).arg(k);
        }
    }
    std::sort(times.begin(), times.end());
    fact(QStringLiteral("S-W19 the menu's time, median and largest (ms)"), QStringLiteral("%1 / %2").arg(times.value(times.size() / 2)).arg(times.isEmpty() ? 0 : times.last()));
    check(partial.isEmpty(), QStringLiteral("the menu offers the whole word at every place"), partial.mid(0, 10).join(QLatin1String(", ")));
    const int at = int(e->toPlainText().indexOf(QLatin1String("writers's")));
    rightClickChoose(e, at + 8, QStringLiteral("Add \"writers's\" to Dictionary"));
    check(sc.isCorrect(QStringLiteral("writers's")), QStringLiteral("Add to Dictionary takes the whole word"));
    as.spellCheck = false;
    as.notifyChanged();   // 0.64.2: save() alone does not reach the windows (the spelling language)
    // save() hands the settings to the modules
    finishProject(w);
    delete w;
}

// ================================================================ S-K01 a novel chapter
void caseSK01()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    BinderItem *chapter = p->model()->createItem(p->draft(), -1, ItemType::Folder, QStringLiteral("Chapter K"));
    QList<BinderItem *> scenes;
    for (int s = 0; s < 3; ++s) {
        BinderItem *it = p->model()->createItem(chapter, -1, ItemType::Text, QStringLiteral("K scene %1").arg(s + 1));
        scenes << it;
        TextEditor *e = showDoc(w, it);
        if (!e)
            continue;
        if (s == 0) {
            typeInto(e, QStringLiteral("Life is long.\n"));
            cursorTo(e, 0);
            chooseByMenu(w, QStringLiteral("style:Block Quote"));
            toEnd(e);
        }
        typeLikeWriter(e, atParagraphs(3300 + quint32(s), 400).join(QLatin1Char('\n')), 3300 + quint32(s));
        toEnd(e);
        typeInto(e, QStringLiteral("\nShe thought: "));
        chooseByMenu(w, QStringLiteral("italic"));
        typeInto(e, QStringLiteral("this cannot be."));
        chooseByMenu(w, QStringLiteral("italic"));
        typeInto(e, QStringLiteral("\n#\n"));
        QTextCursor sep = e->textCursor();
        sep.movePosition(QTextCursor::PreviousBlock);
        e->setTextCursor(sep);
        chooseByMenu(w, QStringLiteral("alignCenter"));
        toEnd(e);
        if (s == 1) {
            typeInto(e, QStringLiteral("Dear Anna, I write to you from the coast."));
            QTextCursor l = e->textCursor();
            l.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
            QTextCharFormat f;
            f.setFontFamilies({QStringLiteral("DejaVu Serif")});
            l.mergeCharFormat(f);
            typeInto(e, QStringLiteral("\nmsg: where r u"));
            QTextCursor m = e->textCursor();
            m.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
            // 0.64.2: the style Code Span, as a writer keeps a font through compile (a compile format's own
            // font replaces fonts set by hand; a style's font stays: the program's design)
            e->setTextCursor(m);
            chooseByMenu(w, QStringLiteral("style:Code Span"));
            toEnd(e);
        }
    }
    selectItems(w, {chapter});
    chooseByMenu(w, QStringLiteral("viewText"));
    settle(500);
    shot(w, QStringLiteral("combined-chapter"));
    compileOnly(p, {chapter});
    const QString pdf = compileDraft(p, QStringLiteral("pdf"), ctx().dir + QStringLiteral("/chapter.pdf"));
    const QString epub = compileDraft(p, QStringLiteral("epub"), ctx().dir + QStringLiteral("/chapter.epub"));
    const QString docx = compileDraft(p, QStringLiteral("docx"), ctx().dir + QStringLiteral("/chapter.docx"));
    const QString pt = pdfToText(pdf);
    check(pt.contains(QLatin1String("#")) && pt.contains(QLatin1String("this cannot be")), QStringLiteral("the PDF holds the separator and the thoughts"));
    const QString xml = QString::fromUtf8(zipFile(docx, QStringLiteral("word/document.xml")));
    check(xml.contains(QLatin1String("<w:i/>")) || xml.contains(QLatin1String("<w:i ")), QStringLiteral("the Word file has the italics"));
    check(xml.contains(QLatin1String("DejaVu Sans Mono")) || QString::fromUtf8(zipFile(docx, QStringLiteral("word/styles.xml"))).contains(QLatin1String("Mono")),
          QStringLiteral("the Word file keeps the monospace font of the text message"));
    check(epubText(epub).contains(QLatin1String("this cannot be")), QStringLiteral("the e-book holds the chapter"));
    for (BinderItem *it : p->allItems())
        it->includeInCompile = true;
    finishProject(w);
    delete w;
}

// ================================================================ S-K02 a scientific paper
void caseSK02()
{
    auto p0 = projectFromTemplate(ctx().work, QStringLiteral("Paper"), QStringLiteral("essay"));
    if (!p0) {
        note(QStringLiteral("no template \"essay\": the Blank template stands in"));
        p0 = projectFromTemplate(ctx().work, QStringLiteral("Paper"), QStringLiteral("blank"));
    }
    if (!p0)
        return;
    const QString path = p0->indexPath();
    p0->save();
    p0.reset();
    MainWindow *w = openWindow(path);
    Project *p = w->project();
    if (!p)
        return;
    TextEditor *e = sceneWith(w, QStringLiteral("The paper"), {});
    if (!e)
        return;
    auto para = [&](const QString &style, const QString &text) {
        toEnd(e);
        if (!e->toPlainText().isEmpty())
            QTest::keyClick(e, Qt::Key_Return);
        chooseByMenu(w, QStringLiteral("style:") + style);
        typeInto(e, text);
    };
    para(QStringLiteral("Title"), QStringLiteral("On the Drying of Fields"));
    para(QString(), QStringLiteral("Abstract: we measure how fields dry."));
    for (int h = 1; h <= 3; ++h)
        para(QStringLiteral("Heading %1").arg(h), QStringLiteral("Heading level %1").arg(h));
    para(QString(), QStringLiteral("Water is H"));
    chooseByMenu(w, QStringLiteral("subscript"));
    typeInto(e, QStringLiteral("2"));
    chooseByMenu(w, QStringLiteral("subscript"));
    typeInto(e, QStringLiteral("O; the area is 4 m"));
    chooseByMenu(w, QStringLiteral("superscript"));
    typeInto(e, QStringLiteral("2"));
    chooseByMenu(w, QStringLiteral("superscript"));
    typeInto(e, QStringLiteral(" and the rate 10"));
    chooseByMenu(w, QStringLiteral("superscript"));
    typeInto(e, QStringLiteral("-3"));
    chooseByMenu(w, QStringLiteral("superscript"));
    for (int i = 0; i < 50; ++i) {
        toEnd(e);
        typeInto(e, QStringLiteral(" Claim %1.").arg(i));
        {   // 0.64.2: a linked footnote is attached to a selection ("Claim n"); without one the program asks for it
            const QString claim = QStringLiteral("Claim %1").arg(i);
            QTextCursor c = e->textCursor();
            c.movePosition(QTextCursor::End);
            c.movePosition(QTextCursor::Left);
            c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, int(claim.size()));
            e->setTextCursor(c);
        }
        chooseByMenu(w, QStringLiteral("footnote"));
        typeInto(e, QStringLiteral("Source %1.").arg(i));
        e->setFocus(Qt::OtherFocusReason);
    }
    para(QString(), QStringLiteral("First point"));
    chooseByMenu(w, QStringLiteral("bullets"));
    para(QString(), QStringLiteral("Step one"));
    chooseByMenu(w, QStringLiteral("numbers"));
    para(QStringLiteral("Block Quote"), QStringLiteral("A quoted passage."));
    // a table with Insert > Table, a figure with Format > Picture
    toEnd(e);
    QTest::keyClick(e, Qt::Key_Return);
    withModal<QDialog>(300, [](QDialog *d) {
        const QList<QSpinBox *> spins = d->findChildren<QSpinBox *>();
        if (spins.size() >= 2) {
            spins.at(0)->setValue(8);
            spins.at(1)->setValue(3);
        }
        d->accept();
    });
    check(chooseByMenu(w, QStringLiteral("insertTable")), QStringLiteral("Insert > Table"));
    settle(300);
    toEnd(e);
    const int picAt = e->textCursor().position();   // 0.64.2: the cursor right at the picture, wherever the editor's cursor went
    insertPicture(p, binderOf(w)->currentItem() ? binderOf(w)->currentItem() : itemByTitle(p, QStringLiteral("The paper")), e->document(), picAt, testPicture(300, 200));
    cursorTo(e, picAt + 1, picAt);   // the picture selected: Format > Picture acts on a selected picture (as in S-K04)
    withModal<QDialog>(300, [](QDialog *d) {
        // 0.64.2: a figure (centered): a picture in the line has no caption (the dialog disables the field)
        if (auto *pos = d->findChild<QComboBox *>())
            pos->setCurrentIndex(pos->count() - 1);
        for (QLineEdit *le : d->findChildren<QLineEdit *>())
            le->setText(QStringLiteral("Figure 1: the field"));
        for (QPlainTextEdit *pe : d->findChildren<QPlainTextEdit *>())
            pe->setPlainText(QStringLiteral("A drawing of a wet field."));
        d->accept();
    });
    chooseByMenu(w, QStringLiteral("picture"));
    settle(300);
    para(QString(), QStringLiteral("Name\tValue\tUnit"));
    para(QString(), QStringLiteral("Width\t4\tm"));
    para(QString(), QStringLiteral("Smith, A. (2020). Fields. Journal of Soil 3."));
    QTextBlockFormat hang;
    hang.setLeftMargin(36);
    hang.setTextIndent(-36);
    e->textCursor().mergeBlockFormat(hang);
    const QString pdf = compileDraft(p, QStringLiteral("pdf"), ctx().dir + QStringLiteral("/paper.pdf"));
    const QString docx = compileDraft(p, QStringLiteral("docx"), ctx().dir + QStringLiteral("/paper.docx"));
    const QString epub = compileDraft(p, QStringLiteral("epub"), ctx().dir + QStringLiteral("/paper.epub"));
    const QString xml = QString::fromUtf8(zipFile(docx, QStringLiteral("word/document.xml")));
    int rc = -1;
    const QString outline = runTool(QStringLiteral("pdfinfo"), {QStringLiteral("-dests"), pdf}, &rc, 20000);
    fact(QStringLiteral("S-K02 PDF destinations"), outline.left(400));
    check(xml.contains(QLatin1String("Heading1")) || xml.contains(QLatin1String("Heading 1")), QStringLiteral("the headings are Heading 1 to 3 in the Word file"));
    check(!zipFile(docx, QStringLiteral("word/footnotes.xml")).isEmpty(), QStringLiteral("the footnotes are footnotes in the Word file"));
    check(xml.contains(QLatin1String("<w:tbl>")), QStringLiteral("the table is a table in Word (w:tbl)"));
    check(xml.contains(QLatin1String("descr=\"A drawing of a wet field.\"")), QStringLiteral("the figure's description is the alt text in Word"));
    check(epubText(epub).contains(QLatin1String("<table")) && epubText(epub).contains(QLatin1String("alt=\"A drawing of a wet field.\"")),
          QStringLiteral("the e-book has the table and the alt text"));
    check(xml.contains(QLatin1String("vertAlign w:val=\"subscript\"")) && xml.contains(QLatin1String("vertAlign w:val=\"superscript\"")),
          QStringLiteral("superscript and subscript are kept in Word"));
    const QString pt = pdfToText(pdf, true);
    check(pt.contains(QLatin1String("Figure 1: the field")), QStringLiteral("the caption is in the PDF"));
    finishProject(w);
    delete w;
}

// ================================================================ S-K03 a poem
void caseSK03()
{
    for (const QString &fx : {QStringLiteral("fx1-qb"), QStringLiteral("fx1-scriv")}) {
        MainWindow *w = openWindow(fixtureCopy(fx));
        Project *p = w->project();
        if (!p)
            continue;
        BinderItem *it = nullptr;
        TextEditor *e = sceneWith(w, QStringLiteral("Poem"), {}, &it);
        if (!e)
            continue;
        const QStringList lines = {QStringLiteral("The field lies wet,"), QStringLiteral("\tthe sky lies grey,"), QStringLiteral("    the crows have gone"),
                                   QStringLiteral("and so the day.")};
        for (int i = 0; i < lines.size(); ++i) {
            keysInto(e, lines.at(i));   // any character (QTest::keyClicks: ASCII only)
            if (i < lines.size() - 1)
                QTest::keyClick(e, Qt::Key_Return, Qt::ShiftModifier);
        }
        QTest::keyClick(e, Qt::Key_Return);
        QTest::keyClicks(e, QStringLiteral("Second stanza, one line."));
        QTest::keyClick(e, Qt::Key_Return);
        QTest::keyClicks(e, QStringLiteral("For Anna"));
        chooseByMenu(w, QStringLiteral("alignRight"));
        cursorTo(e, 0);
        chooseByMenu(w, QStringLiteral("alignCenter"));
        chooseByMenu(w, QStringLiteral("showInvisibles"));
        shot(w, QStringLiteral("poem-") + fx);
        chooseByMenu(w, QStringLiteral("showInvisibles"));
        const QString want = rawText(e->document());
        const QString title = it->title;
        w = reopenWindow(w);
        p = w->project();
        BinderItem *again = p ? itemByTitle(p, title) : nullptr;
        check(again && rawText(p->docs()->text(again->id)) == want, QStringLiteral("%1: line breaks, tabs and leading spaces survive saving").arg(fx),
              again ? firstDifference(want, rawText(p->docs()->text(again->id))) : QString());
        if (again) {
            compileOnly(p, {again});
            const QString pdf = compileDraft(p, QStringLiteral("pdf"), ctx().dir + QStringLiteral("/poem-%1.pdf").arg(fx));
            const QString docx = compileDraft(p, QStringLiteral("docx"), ctx().dir + QStringLiteral("/poem-%1.docx").arg(fx));
            const QString epub = compileDraft(p, QStringLiteral("epub"), ctx().dir + QStringLiteral("/poem-%1.epub").arg(fx));
            const QString pt = pdfToText(pdf, true);
            int found = 0;
            for (const QString &l : lines)
                if (pt.contains(l.trimmed()))
                    ++found;
            check(found == lines.size(), QStringLiteral("%1: every line in the PDF on its own").arg(fx), pt.left(300));
            const QString xml = QString::fromUtf8(zipFile(docx, QStringLiteral("word/document.xml")));
            check(xml.contains(QLatin1String("<w:br/>")) && xml.contains(QLatin1String("<w:tab/>")), QStringLiteral("%1: Word has the line breaks and the tab").arg(fx));
            check(xml.contains(QLatin1String("xml:space=\"preserve\">    the crows")), QStringLiteral("%1: Word keeps the leading spaces").arg(fx));
            check(epubText(epub).contains(QLatin1String("<br")), QStringLiteral("%1: the e-book has the line breaks").arg(fx));
            for (BinderItem *x : p->allItems())
                x->includeInCompile = true;
        }
        finishProject(w);
        delete w;
    }
}

// ================================================================ S-K04 pictures in the text
void caseSK04()
{
    for (const QString &fx : {QStringLiteral("fx1-qb"), QStringLiteral("fx1-scriv")}) {
        MainWindow *w = openWindow(fixtureCopy(fx));
        Project *p = w->project();
        if (!p)
            continue;
        BinderItem *it = nullptr;
        TextEditor *e = sceneWith(w, QStringLiteral("Pictures"), atParagraphs(3400, 300), &it);
        if (!e)
            continue;
        int pictures = 0;
        for (int width : {1, 64, 1000, 6000}) {
            const int len = e->document()->characterCount();
            for (int at : {0, len / 2, len - 1}) {
                insertPicture(p, it, e->document(), qBound(0, at, e->document()->characterCount() - 1), testPicture(width, qMax(1, width / 2)));
                ++pictures;
            }
        }
        // in a list, in the notes, as a synopsis picture
        toEnd(e);
        QTest::keyClick(e, Qt::Key_Return);
        chooseByMenu(w, QStringLiteral("bullets"));
        insertPicture(p, it, e->document(), e->textCursor().position(), testPicture(50, 50));
        ++pictures;
        insertPicture(p, it, p->docs()->notes(it->id), 0, testPicture(40, 40));
        // Format > Picture: each position with a caption and a description
        const QStringList positions = {QStringLiteral("in the line"), QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("centered")};
        for (int k = 0; k < positions.size(); ++k) {
            toEnd(e);
            QTest::keyClick(e, Qt::Key_Return);
            const int at = e->textCursor().position();
            insertPicture(p, it, e->document(), at, testPicture(200 + k * 50, 120));
            ++pictures;
            cursorTo(e, at + 1, at);
            const int choice = k;
            withModal<QDialog>(300, [choice](QDialog *d) {
                if (auto *cb = d->findChild<QComboBox *>())
                    cb->setCurrentIndex(qMin(choice, cb->count() - 1));
                for (QLineEdit *le : d->findChildren<QLineEdit *>())
                    le->setText(QStringLiteral("Caption %1").arg(choice + 1));
                for (QPlainTextEdit *pe : d->findChildren<QPlainTextEdit *>())
                    pe->setPlainText(QStringLiteral("Description %1 with Umlaute äöü").arg(choice + 1));
                d->accept();
            });
            check(chooseByMenu(w, QStringLiteral("picture")), QStringLiteral("%1: Format > Picture, %2").arg(fx, positions.at(k)));
            settle(300);
            toEnd(e);
            typeInto(e, QStringLiteral(" Text beside the picture %1.").arg(k + 1));
        }
        // cut and paste, delete and undo
        cursorTo(e, 1, 0);
        chooseByMenu(w, QStringLiteral("cut"));
        toEnd(e);
        chooseByMenu(w, QStringLiteral("paste"));
        cursorTo(e, 1, 0);
        QTest::keyClick(e, Qt::Key_Delete);
        chooseByMenu(w, QStringLiteral("undo"));
        chooseByMenu(w, QStringLiteral("pageView"));
        shot(w, QStringLiteral("pictures-page-view-") + fx);
        chooseByMenu(w, QStringLiteral("pageView"));
        const QString shape = tableShape(e->document());
        const QString title = it->title;
        w = reopenWindow(w);
        p = w->project();
        BinderItem *again = p ? itemByTitle(p, title) : nullptr;
        check(again && tableShape(p->docs()->text(again->id)) == shape, QStringLiteral("%1: the pictures and figures are kept after reopening").arg(fx),
              again ? firstDifference(shape, tableShape(p->docs()->text(again->id)), 60) : QString());
        if (again) {
            compileOnly(p, {again});
            const QString pdf = compileDraft(p, QStringLiteral("pdf"), ctx().dir + QStringLiteral("/pictures-%1.pdf").arg(fx));
            const QString docx = compileDraft(p, QStringLiteral("docx"), ctx().dir + QStringLiteral("/pictures-%1.docx").arg(fx));
            const QString epub = compileDraft(p, QStringLiteral("epub"), ctx().dir + QStringLiteral("/pictures-%1.epub").arg(fx));
            int rc = -1;
            const QString list = runTool(QStringLiteral("pdfimages"), {QStringLiteral("-list"), pdf}, &rc, 30000);
            const int inPdf = qMax(0, int(list.split(QLatin1Char('\n'), Qt::SkipEmptyParts).size()) - 2);
            ZipReader z(docx);
            int inDocx = 0;
            for (const QString &n : z.fileNames())
                if (n.startsWith(QLatin1String("word/media/")))
                    ++inDocx;
            ZipReader ze(epub);
            int inEpub = 0;
            for (const QString &n : ze.fileNames())
                if (n.contains(QLatin1String("image")) || n.endsWith(QLatin1String(".png")) || n.endsWith(QLatin1String(".jpg")))
                    ++inEpub;
            fact(QStringLiteral("S-K04 %1 pictures: in the text, PDF, Word, e-book").arg(fx), QStringLiteral("%1, %2, %3, %4").arg(pictures).arg(inPdf).arg(inDocx).arg(inEpub));
            check(inPdf >= pictures && inDocx >= 1 && inEpub >= 1, QStringLiteral("%1: each output holds the pictures").arg(fx));
            const QString xml = QString::fromUtf8(zipFile(docx, QStringLiteral("word/document.xml")));
            check(xml.contains(QLatin1String("tblpPr")), QStringLiteral("%1: a picture beside the text is a floating table in Word").arg(fx));
            check(xml.contains(QLatin1String("Description 2 with Umlaute")), QStringLiteral("%1: descriptions are the alt text in Word").arg(fx));
            for (BinderItem *x : p->allItems())
                x->includeInCompile = true;
        }
        finishProject(w);
        delete w;
    }
}

// ================================================================ S-K05 tables
void caseSK05()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    QStringList baseline;
    // import: FODT, the RTF of LibreOffice and of Word (made by hand); ODT and DOCX when LibreOffice made them
    const QString dir = ctx().work + QStringLiteral("/tables-in");
    QDir().mkpath(dir);
    writeText(dir + QStringLiteral("/word.rtf"), "{\\rtf1\\ansi\\trowd\\cellx2000\\cellx4000\\pard\\intbl a\\cell b\\cell\\row\\trowd\\clmgf\\cellx2000\\clmrg\\cellx4000\\pard\\intbl merged\\cell\\cell\\row\\pard After.\\par}");
    writeText(dir + QStringLiteral("/libreoffice.rtf"), "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\froman Liberation Serif;}}\\trowd\\trgaph55\\cellx3000\\cellx6000\\pard\\intbl\\f0 one\\cell two\\cell\\row\\pard\\par}");
    QStringList files = {dir + QStringLiteral("/word.rtf"), dir + QStringLiteral("/libreoffice.rtf"), ctx().sourceDir + QStringLiteral("/tests/import/features.fodt")};
    if (QFileInfo::exists(QStringLiteral("/usr/bin/soffice"))) {
        int rc = -1;
        runTool(QStringLiteral("soffice"), {QStringLiteral("--headless"), QStringLiteral("--convert-to"), QStringLiteral("odt"), QStringLiteral("--outdir"), dir, files.last()}, &rc, 120000);
        runTool(QStringLiteral("soffice"), {QStringLiteral("--headless"), QStringLiteral("--convert-to"), QStringLiteral("docx"), QStringLiteral("--outdir"), dir, files.last()}, &rc, 120000);
        files << dir + QStringLiteral("/features.odt") << dir + QStringLiteral("/features.docx");
    } else {
        note(QStringLiteral("[L] no LibreOffice: FODT only"));
    }
    for (const QString &f : files) {
        QStringList warn;
        const QList<BinderItem *> got = importer::importFiles(p, {f}, p->draft(), -1, &warn);
        int tables = 0;
        for (BinderItem *it : got)
            tables += countTables(p->docs()->text(it->id));
        baseline << QStringLiteral("%1: %2 table(s)").arg(QFileInfo(f).fileName()).arg(tables);
        if (f.endsWith(QLatin1String(".rtf")))
            check(tables >= 1, QStringLiteral("tables from RTF (%1) arrive as tables").arg(QFileInfo(f).fileName()));
    }
    // a table by Insert > Table, one by hand with tabs and ruler tab stops
    TextEditor *e = sceneWith(w, QStringLiteral("Tables by hand"), {QStringLiteral("Before.")});
    if (e) {
        toEnd(e);
        QTest::keyClick(e, Qt::Key_Return);
        withModal<QDialog>(300, [](QDialog *d) { d->accept(); });
        chooseByMenu(w, QStringLiteral("insertTable"));
        settle(300);
        QTest::keyClicks(e, QStringLiteral("cell text"));
        toEnd(e);
        QTest::keyClick(e, Qt::Key_Return);
        typeInto(e, QStringLiteral("Name\tValue\nWidth\t4"));
        QTextBlockFormat tabs;
        tabs.setTabPositions({QTextOption::Tab(144, QTextOption::LeftTab)});
        QTextCursor c = e->textCursor();
        c.movePosition(QTextCursor::PreviousBlock, QTextCursor::KeepAnchor);
        // 0.64.2: marked as set by hand, as the ruler does (without the mark compile takes the style's tabs)
        tabs.setProperty(props::ParaDirect, c.blockFormat().intProperty(props::ParaDirect) | direct::Tabs);
        c.mergeBlockFormat(tabs);
        compileOnly(p, {binderOf(w)->currentItem() ? binderOf(w)->currentItem() : itemByTitle(p, QStringLiteral("Tables by hand"))});
        const QString docx = compileDraft(p, QStringLiteral("docx"), ctx().dir + QStringLiteral("/tables.docx"));
        const QString pdf = compileDraft(p, QStringLiteral("pdf"), ctx().dir + QStringLiteral("/tables.pdf"));
        const QString xml = QString::fromUtf8(zipFile(docx, QStringLiteral("word/document.xml")));
        check(xml.contains(QLatin1String("<w:tbl>")), QStringLiteral("the Insert > Table table is a table in Word"));
        check(xml.contains(QLatin1String("<w:tabs>")), QStringLiteral("the hand-made table keeps its tab stops in Word"));
        check(pdfToText(pdf).contains(QLatin1String("cell text")), QStringLiteral("the table's text is in the PDF"));
        for (BinderItem *x : p->allItems())
            x->includeInCompile = true;
    }
    writeText(ctx().dir + QStringLiteral("/tables-baseline.txt"), baseline.join(QLatin1Char('\n')).toUtf8());
    fact(QStringLiteral("S-K05 what each source gives"), baseline.join(QLatin1String("; ")));
    finishProject(w);
    delete w;
}

} // namespace cs
