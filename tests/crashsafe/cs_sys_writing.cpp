// The system test, stage 2: writing (S-W01 to S-W07), section 6.2.
#include "cs.h"

#include "text/Styles.h"
#include "text/TextProps.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/EditorPane.h"
#include "ui/Inspector.h"
#include "ui/MainWindow.h"
#include "ui/SpellChecker.h"
#include "ui/TextEditor.h"

#include <QAbstractButton>
#include <QClipboard>
#include <QColorDialog>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QInputDialog>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>

#include <random>

namespace cs {

using namespace qb;

namespace {

// an empty scene at the end of the Draft, shown in the editor
TextEditor *emptyScene(MainWindow *w, const QString &title, BinderItem **made = nullptr)
{
    Project *p = w->project();
    BinderItem *it = p->model()->createItem(p->draft(), -1, ItemType::Text, title);
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

void selectRange(TextEditor *e, int from, int to)
{
    QTextCursor c(e->document());
    c.setPosition(qBound(0, from, e->document()->characterCount() - 1));
    c.setPosition(qBound(0, to, e->document()->characterCount() - 1), QTextCursor::KeepAnchor);
    e->setTextCursor(c);
    e->setFocus(Qt::OtherFocusReason);
    settle(30);
}

void undoAll(MainWindow *w, TextEditor *e, int limit = 5000)
{
    for (int i = 0; i < limit && e->document()->isUndoAvailable(); ++i) {
        e->setFocus(Qt::OtherFocusReason);
        e->undo();
    }
    Q_UNUSED(w)
}

void redoAll(TextEditor *e, int limit = 5000)
{
    for (int i = 0; i < limit && e->document()->isRedoAvailable(); ++i)
        e->redo();
}

QString blockFormats(QTextDocument *d)
{
    QString out;
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next()) {
        const QTextBlockFormat f = b.blockFormat();
        // 0.64.2: the paragraph's first letters, so a difference names its paragraph
        out += b.text().left(6) + QLatin1Char(' ');
        out += QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8\n")
                   .arg(int(f.alignment()))
                   .arg(f.lineHeight(), 0, 'f', 1)
                   .arg(f.lineHeightType())
                   .arg(f.topMargin(), 0, 'f', 1)
                   .arg(f.bottomMargin(), 0, 'f', 1)
                   .arg(f.leftMargin(), 0, 'f', 1)
                   .arg(f.textIndent(), 0, 'f', 1)
                   .arg(b.textList() ? int(b.textList()->format().style()) : 0);
    }
    return out;
}

Inspector *inspectorOf(MainWindow *w) { return w->findChild<Inspector *>(); }

QPlainTextEdit *synopsisEditor(MainWindow *w)
{
    if (Inspector *in = inspectorOf(w))
        for (QPlainTextEdit *pe : in->findChildren<QPlainTextEdit *>())
            if (pe->isVisible())
                return pe;
    return nullptr;
}

TextEditor *notesEditor(MainWindow *w)
{
    if (Inspector *in = inspectorOf(w))
        for (TextEditor *te : in->findChildren<TextEditor *>())
            if (te->isVisible())
                return te;
    return nullptr;
}

void setClipboard(const QMap<QString, QByteArray> &formats, const QString &text = QString(), const QImage &image = QImage(), const QList<QUrl> &urls = {})
{
    auto *md = new QMimeData;
    for (auto i = formats.begin(); i != formats.end(); ++i)
        md->setData(i.key(), i.value());
    if (!text.isNull())
        md->setText(text);
    if (!image.isNull())
        md->setImageData(image);
    if (!urls.isEmpty())
        md->setUrls(urls);
    QApplication::clipboard()->setMimeData(md);
    settle(50);
}

} // namespace

// ================================================================ S-W01 a scene typed like an author
void caseSW01()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-scriv")));
    if (!w->project())
        return;
    BinderItem *it = nullptr;
    TextEditor *e = emptyScene(w, QStringLiteral("Typed scene"), &it);
    if (!e)
        return;
    const QStringList paras = atParagraphs(ctx().seed, ctx().smoke ? 150 : ctx().quick ? 800 : 1500, false);
    QString expected;
    int k = 0;
    for (const QString &para : paras) {
        expected = typeLikeWriter(e, (k ? QStringLiteral("\n") : QString()) + para, ctx().seed + quint32(k));
        if (k % 3 == 0) {
            const QStringList specials = {QStringLiteral("Em dash —"), QStringLiteral("En dash –"), QStringLiteral("Ellipsis …")};
            const QString which = specials.at(k / 3 % 3);
            QTextCursor c = e->textCursor();
            c.movePosition(QTextCursor::End);
            e->setTextCursor(c);
            if (menuPath(w, QStringLiteral("Insert > Special Characters > ") + which))
                expected += which.right(1);
        }
        ++k;
    }
    const QString beforeUndo = rawText(e->document());
    check(beforeUndo == expected, QStringLiteral("the typed text equals the model"), firstDifference(expected, beforeUndo));
    for (int i = 0; i < 20; ++i)
        chooseByMenu(w, QStringLiteral("undo"));
    for (int i = 0; i < 20; ++i)
        chooseByMenu(w, QStringLiteral("redo"));
    check(rawText(e->document()) == beforeUndo, QStringLiteral("20 Undo and 20 Redo give the text back"), firstDifference(beforeUndo, rawText(e->document())));
    for (const QString &profile : {QStringLiteral("slow"), QStringLiteral("fast")})
        typingGate(e, profile, 60, QStringLiteral("S-W01"));
    finishProject(w);
    delete w;
}

// ================================================================ S-W02 character formatting
void caseSW02()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    BinderItem *it = nullptr;
    TextEditor *e = emptyScene(w, QStringLiteral("Formatting"), &it);
    if (!e)
        return;
    QTextCursor c(e->document());
    c.insertText(atParagraphs(2200, 300).join(QChar(QChar::ParagraphSeparator)));
    e->document()->clearUndoRedoStacks();
    const QString startText = rawText(e->document());
    const QString startRuns = runsOf(e->document());
    struct Toggle {
        QString action;
        QKeySequence key;
        std::function<bool(const QTextCharFormat &)> on;
    };
    const QList<Toggle> toggles = {
        {QStringLiteral("bold"), QKeySequence(Qt::CTRL | Qt::Key_B), [](const QTextCharFormat &f) { return f.fontWeight() >= QFont::Bold; }},
        {QStringLiteral("italic"), QKeySequence(Qt::CTRL | Qt::Key_I), [](const QTextCharFormat &f) { return f.fontItalic(); }},
        {QStringLiteral("underline"), QKeySequence(Qt::CTRL | Qt::Key_U), [](const QTextCharFormat &f) { return f.fontUnderline(); }},
        {QStringLiteral("strike"), QKeySequence(), [](const QTextCharFormat &f) { return f.fontStrikeOut(); }},
    };
    int start = 10;
    for (const Toggle &t : toggles) {
        // by shortcut, by the menu
        for (int way = 0; way < 2; ++way) {
            selectRange(e, start, start + 20);
            if (way == 0 && !t.key.isEmpty())
                QTest::keySequence(e, t.key);
            else
                chooseByMenu(w, t.action);
            settle(80);
            QTextCursor at(e->document());
            at.setPosition(start + 5);
            check(t.on(at.charFormat()), QStringLiteral("%1 by %2").arg(t.action, way == 0 && !t.key.isEmpty() ? QStringLiteral("shortcut") : QStringLiteral("menu")));
            selectRange(e, start, start + 20);
            w->updateFormatBar();
            if (QAction *a = action(w, t.action); a && a->isCheckable())
                check(a->isChecked(), QStringLiteral("the format bar shows %1 for the selection").arg(t.action));
            start += 30;
        }
    }
    for (const QString &a : {QStringLiteral("superscript"), QStringLiteral("subscript"), QStringLiteral("fontBigger"), QStringLiteral("fontSmaller"), QStringLiteral("highlight"),
                             QStringLiteral("textColorAuto")}) {
        selectRange(e, start, start + 15);
        check(chooseByMenu(w, a), QStringLiteral("Format > %1").arg(a));
        start += 20;
    }
    withModal<QColorDialog>(300, [](QColorDialog *d) {
        d->setCurrentColor(QColor(0xaa, 0x22, 0x33));
        d->accept();
    });
    selectRange(e, start, start + 15);
    chooseByMenu(w, QStringLiteral("textColor"));
    settle(300);
    // copy and paste formatting, clear formatting, a selection over several paragraphs
    selectRange(e, 12, 14);
    chooseByMenu(w, QStringLiteral("copyFormat"));
    selectRange(e, start + 40, start + 60);
    chooseByMenu(w, QStringLiteral("pasteFormat"));
    const int paraEnd = e->document()->findBlockByNumber(2).position() + 5;
    selectRange(e, 3, paraEnd);
    chooseByMenu(w, QStringLiteral("bold"));
    selectRange(e, 3, paraEnd);
    chooseByMenu(w, QStringLiteral("clearFormat"));
    shot(w, QStringLiteral("formatting"));
    for (int i = 0; i < 400 && e->document()->isUndoAvailable(); ++i)
        chooseByMenu(w, QStringLiteral("undo"));
    check(rawText(e->document()) == startText && runsOf(e->document()) == startRuns, QStringLiteral("after undo all, the text and its formatting equal the start"),
          firstDifference(startRuns, runsOf(e->document())));
    finishProject(w);
    delete w;
}

// ================================================================ S-W03 paragraph formatting and lists
void caseSW03()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    BinderItem *it = nullptr;
    TextEditor *e = emptyScene(w, QStringLiteral("Paragraphs"), &it);
    if (!e)
        return;
    QTextCursor c(e->document());
    auto firstPara = [e] {
        const QTextBlockFormat f = e->document()->begin().blockFormat();
        return QStringLiteral("after %1, indent %2, direct %3").arg(f.bottomMargin()).arg(f.textIndent()).arg(f.intProperty(props::ParaDirect));
    };
    fact(QStringLiteral("S-W03 the first paragraph of the new text"), firstPara());
    QString lastFirst;
    auto track = [&](const QString &step) {   // 0.64.2: which step changes the first paragraph's spacing
        const QString now = firstPara();
        if (now != lastFirst)
            fact(QStringLiteral("S-W03 the first paragraph after %1").arg(step), now);
        lastFirst = now;
    };
    c.insertText(atParagraphs(2300, 600).join(QChar(QChar::ParagraphSeparator)));
    // 0.64.2: a paragraph for every step (the steps go up to paragraph 24; the text has fewer, and a step on a
    // paragraph that is not there acted on the first one)
    for (int k = e->document()->blockCount(); k < 30; ++k) {
        c.movePosition(QTextCursor::End);
        c.insertText(QChar(QChar::ParagraphSeparator) + QStringLiteral("Paragraph %1 of the steps, with a few words in it.").arg(k + 1));
    }
    fact(QStringLiteral("S-W03 the first paragraph after the text went in"), firstPara());
    auto inPara = [&](int n) {
        QTextCursor x(e->document()->findBlockByNumber(n));
        x.movePosition(QTextCursor::NextWord);
        e->setTextCursor(x);
        e->setFocus(Qt::OtherFocusReason);
        settle(20);
    };
    int n = 0;
    for (const QString &a : {QStringLiteral("alignLeft"), QStringLiteral("alignCenter"), QStringLiteral("alignRight"), QStringLiteral("alignJustify")}) {
        inPara(n++);
        check(chooseByMenu(w, a), QStringLiteral("Format > Paragraph > %1").arg(a));
        if (n == 1)
            fact(QStringLiteral("S-W03 the first paragraph after Align Left"), firstPara());
    }
    for (const QString &a : {QStringLiteral("line100"), QStringLiteral("line115"), QStringLiteral("line120"), QStringLiteral("line150"), QStringLiteral("line200")}) {
        inPara(n++);
        chooseByMenu(w, a);
    }
    // 0.64.2: "Other…" opens the Paragraph window (not a number question): 1.8 lines there
    withModal<QDialog>(300, [](QDialog *d) {
        if (auto *mode = d->findChild<QComboBox *>(QStringLiteral("paraLineMode")))
            mode->setCurrentIndex(0);
        if (auto *line = d->findChild<QDoubleSpinBox *>(QStringLiteral("paraLine")))
            line->setValue(line->maximum() > 20 ? 180 : 1.8);
        d->accept();
    });
    inPara(n++);
    menuPath(w, QStringLiteral("Format > Paragraph > Line Spacing > Other…"));
    settle(300);
    track(QStringLiteral("the line spacings and Other…"));
    // the four paragraph buttons of the format bar, every value of their menus
    for (const QString &id : {QStringLiteral("spaceBefore"), QStringLiteral("spaceAfter"), QStringLiteral("indentFirst"), QStringLiteral("indentLeft")}) {
        auto *b = w->findChild<QToolButton *>(QStringLiteral("para_") + id);
        QMenu *m = b ? b->menu() : nullptr;
        if (!m) {
            note(QStringLiteral("no paragraph button %1").arg(id));
            continue;
        }
        emit m->aboutToShow();
        for (QAction *a : m->actions()) {
            if (a->isSeparator() || !a->isEnabled())
                continue;
            inPara(n % 20);
            if (a->objectName() == QLatin1String("paraOther"))
                withModal<QInputDialog>(300, [](QInputDialog *d) {
                    d->setDoubleValue(d->doubleMaximum() > 30 ? 30 : d->doubleMaximum());
                    d->accept();
                });
            a->trigger();
            settle(60);
            track(QStringLiteral("%1: %2").arg(id, a->text()));
        }
    }
    chooseByMenu(w, QStringLiteral("showRuler"));
    settle(200);
    track(QStringLiteral("Show Ruler"));
    shot(w, QStringLiteral("ruler"));
    for (const QString &a : {QStringLiteral("indentMore"), QStringLiteral("indentMore"), QStringLiteral("indentLess")}) {
        inPara(21);
        chooseByMenu(w, a);
    }
    track(QStringLiteral("Indent More and Less"));
    inPara(22);
    chooseByMenu(w, QStringLiteral("indentMore"));
    chooseByMenu(w, QStringLiteral("resetParagraph"));
    track(QStringLiteral("Reset Paragraph"));
    // lists
    inPara(23);
    chooseByMenu(w, QStringLiteral("bullets"));
    inPara(24);
    chooseByMenu(w, QStringLiteral("numbers"));
    chooseByMenu(w, QStringLiteral("indent"));
    chooseByMenu(w, QStringLiteral("outdent"));
    QTextCursor endOfList(e->document()->findBlockByNumber(24));
    endOfList.movePosition(QTextCursor::EndOfBlock);
    e->setTextCursor(endOfList);
    QTest::keyClick(e, Qt::Key_Return);
    QTest::keyClick(e, Qt::Key_Return);   // Enter on an empty list item: the list ends
    track(QStringLiteral("the lists"));
    const bool listEnded = !e->textCursor().block().textList();
    check(listEnded, QStringLiteral("Enter on an empty list item ends the list"));
    QTextCursor listStart(e->document()->findBlockByNumber(23));
    e->setTextCursor(listStart);
    QTest::keyClick(e, Qt::Key_Backspace);
    fact(QStringLiteral("S-W03 Backspace at a list start"), e->textCursor().block().textList() ? QStringLiteral("still a list item") : QStringLiteral("the bullet goes"));
    track(QStringLiteral("Backspace at the list start"));
    const QString formats = blockFormats(e->document());
    const QString title = it->title;
    w = reopenWindow(w);
    Project *p = w->project();
    BinderItem *again = p ? itemByTitle(p, title) : nullptr;
    const QString after = again ? blockFormats(p->docs()->text(again->id)) : QString();
    check(after == formats, QStringLiteral("the paragraph values are kept after reopening"), firstDifference(formats, after, 60));
    finishProject(w);
    delete w;
}

// ================================================================ S-W04 styles
void caseSW04()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    BinderItem *it = nullptr;
    TextEditor *e = emptyScene(w, QStringLiteral("Styles"), &it);
    if (!e)
        return;
    QTextCursor c(e->document());
    QStringList paras = atParagraphs(2400, 6000);
    while (paras.size() < 320)
        paras += paras;
    c.insertText(paras.mid(0, 320).join(QChar(QChar::ParagraphSeparator)));
    const QStringList styles = {QStringLiteral("Heading 1"), QStringLiteral("Heading 2"), QStringLiteral("Title"), QStringLiteral("Block Quote"), QStringLiteral("Centered"),
                                QStringLiteral("Caption"), QStringLiteral("Code Block")};
    for (int i = 0; i < styles.size(); ++i) {
        QTextCursor x(e->document()->findBlockByNumber(i));
        e->setTextCursor(x);
        e->setFocus(Qt::OtherFocusReason);
        check(chooseByMenu(w, QStringLiteral("style:") + styles.at(i)), QStringLiteral("Format > Style > %1").arg(styles.at(i)));
    }
    // a style on 300 paragraphs, then redefined from one of them
    QTextCursor all(e->document()->findBlockByNumber(10));
    all.setPosition(e->document()->findBlockByNumber(309).position() + 3, QTextCursor::KeepAnchor);
    e->setTextCursor(all);
    chooseByMenu(w, QStringLiteral("style:Block Quote"));
    QTextCursor one(e->document()->findBlockByNumber(20));
    one.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    e->setTextCursor(one);
    QTextCharFormat big;
    big.setFontPointSize(17);
    one.mergeCharFormat(big);
    withModal<QDialog>(300, [](QDialog *d) { d->accept(); });
    answerBoxLater(600, QMessageBox::Yes);
    chooseByMenu(w, QStringLiteral("styleRedefine"));
    settle(800);
    int reached = 0;
    for (int b = 10; b < 310; ++b) {
        QTextCursor x(e->document()->findBlockByNumber(b));
        x.movePosition(QTextCursor::NextCharacter);
        if (qAbs(x.charFormat().fontPointSize() - 17) < 0.6)
            ++reached;
    }
    check(reached >= 295, QStringLiteral("the redefined style reaches all its paragraphs (%1 of 300)").arg(reached));
    // New Style From Selection
    QTextCursor n(e->document()->findBlockByNumber(315));
    n.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    e->setTextCursor(n);
    withModal<QDialog>(300, [](QDialog *d) {
        if (auto *le = d->findChild<QLineEdit *>())
            le->setText(QStringLiteral("Letter in the Story"));
        d->accept();
    });
    chooseByMenu(w, QStringLiteral("styleNew"));
    settle(500);
    // the Styles Panel open while typing
    chooseByMenu(w, QStringLiteral("stylesPanel"));
    settle(300);
    QTextCursor endC = e->textCursor();
    endC.movePosition(QTextCursor::End);
    e->setTextCursor(endC);
    typeInto(e, QStringLiteral("\nTyped while the Styles Panel is open."));
    chooseByMenu(w, QStringLiteral("style:"));
    // delete a style in use, import styles, use them in new projects
    answerBoxLater(300, QMessageBox::Yes);
    menuPath(w, QStringLiteral("Format > Style > Delete Style > a Code Span"));
    settle(300);
    // 0.64.2: Import Styles asks for a Quillbinder project file (the case gave a .scriv folder)
    {   // a copy of its own: the case's open project is the fixture copy fx1-qb
        const QString src = ctx().fixtures + QStringLiteral("/fx1-qb"), dst = ctx().work + QStringLiteral("/styles-source");
        QDir(dst).removeRecursively();
        copyTree(src, dst);
        fileDialogLater(300, QDir::cleanPath(dst + QLatin1Char('/') + readText(src + QStringLiteral(".open")).trimmed()));
    }
    withModal<QDialog>(1200, [](QDialog *d) { d->accept(); });
    chooseByMenu(w, QStringLiteral("styleImport"));
    settle(800);
    withModal<QDialog>(300, [](QDialog *d) {
        shot(d, QStringLiteral("customize-style-list"));
        d->accept();
    });
    // 0.64.2: by its menu path (the entry is made anew each time the Style menu opens)
    check(menuPath(w, QStringLiteral("Format > Style > Customize Style List…")), QStringLiteral("Format > Style > Customize Style List"));
    settle(400);
    answerBoxLater(300, QMessageBox::Yes);
    chooseByMenu(w, QStringLiteral("styleSaveDefault"));
    settle(300);
    closeDialogsLater(100);
    settle(200);
    const QStringList names = styles::names(p->formatting);
    check(names.contains(QStringLiteral("Letter in the Story")), QStringLiteral("New Style From Selection adds the style"), names.join(QLatin1String(", ")));
    finishProject(w);
    // 0.64.2: through File > New Project (the window gives a new project the saved styles; a project made by the
    // test's own code does not), with the blank template, which has no styles of its own
    templates::Info blank;
    for (const templates::Info &t : templates::all())
        if (t.id == QLatin1String("blank"))
            blank = t;
    bool created = false;
    startDialogLater(300, blank, QStringLiteral("After Styles"), ctx().work, false, &created);
    chooseByMenu(w, QStringLiteral("newProject"));
    settle(1200);
    check(created && w->project() && styles::names(w->project()->formatting).contains(QStringLiteral("Letter in the Story")),
          QStringLiteral("a new project gets the styles (Use These Styles in New Projects)"),
          w->project() ? styles::names(w->project()->formatting).join(QLatin1String(", ")) : QStringLiteral("(no project)"));
    if (w->project())
        w->closeProject();
    delete w;
}

// ================================================================ S-W05 spelling in two languages
void caseSW05()
{
    SpellChecker &sc = SpellChecker::instance();
    if (!sc.availableLanguages().contains(QStringLiteral("de_DE")))
        note(QStringLiteral("[G] no German dictionary: the German part is left out"));
    AppSettings &as = AppSettings::instance();
    as.spellCheck = true;
    as.spellLanguage = QStringLiteral("en_US");
    as.notifyChanged();   // 0.64.2: save() alone does not reach the windows (the spelling language)
    // save() hands the settings to the modules
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    BinderItem *it = nullptr;
    TextEditor *e = emptyScene(w, QStringLiteral("Spelling"), &it);
    if (!e)
        return;
    typeInto(e, QStringLiteral("The wrter met her Mädchen in the Straße. Brackwaterly fields were wet. Teh end of the dya."));
    settle(400);
    const QString text = e->toPlainText();
    QStringList menus;
    // a suggestion
    check(rightClickChoose(e, int(text.indexOf(QLatin1String("wrter"))) + 2, QStringLiteral("writer"), &menus), QStringLiteral("right-click on a misspelled word: a suggestion"),
          menus.join(QLatin1String(" | ")));
    // Learn (Add to Dictionary)
    const int pos = int(e->toPlainText().indexOf(QLatin1String("Brackwaterly")));
    rightClickChoose(e, pos + 3, QStringLiteral("Add \"Brackwaterly\" to Dictionary"), &menus);
    check(sc.isCorrect(QStringLiteral("Brackwaterly")), QStringLiteral("Add to Dictionary: the word is learned"));
    rightClickChoose(e, int(e->toPlainText().indexOf(QLatin1String("dya"))) + 1, QStringLiteral("Ignore \"dya\""), &menus);
    rightClickChoose(e, int(e->toPlainText().indexOf(QLatin1String("Teh"))) + 1, QStringLiteral("More Commands"), &menus);
    fact(QStringLiteral("S-W05 the menu on a misspelled word"), menus.join(QLatin1String(" | ")).left(400));
    // switch the language while marks are shown
    for (const QString &lang : {QStringLiteral("de_DE"), QStringLiteral("en_US"), QStringLiteral("de_DE")}) {
        as.spellLanguage = lang;
        as.notifyChanged();   // 0.64.2: save() alone does not reach the windows (the spelling language)
        // save() hands the settings to the modules
        SpellChecker::rehighlightAll();
        settle(300);
    }
    if (sc.availableLanguages().contains(QStringLiteral("de_DE"))) {
        QStringList marked;
        for (const QString &wd : {QStringLiteral("Mädchen"), QStringLiteral("Straße"), QStringLiteral("Größe"), QStringLiteral("Übermaß")})
            if (!sc.isCorrect(wd))
                marked << wd;
        check(marked.isEmpty(), QStringLiteral("with de_DE, German words with umlauts and ß are not marked"), marked.join(QLatin1String(", ")));
    }
    as.spellLanguage = QStringLiteral("en_US");
    as.smartQuotes = as.smartDashes = as.ellipsis = as.capitalizeSentences = as.capitalizeI = true;
    as.notifyChanged();   // 0.64.2: save() alone does not reach the windows (the spelling language)
    // save() hands the settings to the modules
    QTextCursor endC = e->textCursor();
    endC.movePosition(QTextCursor::End);
    e->setTextCursor(endC);
    typeInto(e, QLatin1Char('\n') + atParagraphs(2500, 300).join(QLatin1Char('\n')));
    checkCleanWindows(w);
    w = reopenWindow(w);
    check(SpellChecker::instance().isCorrect(QStringLiteral("Brackwaterly")), QStringLiteral("the learned word is not marked after reopening"));
    as.smartQuotes = as.smartDashes = as.ellipsis = as.capitalizeSentences = as.capitalizeI = false;
    as.spellCheck = false;
    as.notifyChanged();   // 0.64.2: save() alone does not reach the windows (the spelling language)
    // save() hands the settings to the modules
    finishProject(w);
    delete w;
}

// ================================================================ S-W06 notes in the text, pictures, links
void caseSW06()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    BinderItem *it = nullptr;
    TextEditor *e = emptyScene(w, QStringLiteral("Notes and links"), &it);
    if (!e)
        return;
    QTextCursor c(e->document());
    c.insertText(atParagraphs(2600, 900).join(QChar(QChar::ParagraphSeparator)));
    const QStringList kinds = {QStringLiteral("comment"), QStringLiteral("footnote"), QStringLiteral("annotation"), QStringLiteral("inlineFootnote")};
    int made = 0;
    for (int i = 0; i < 20; ++i) {
        const int at = 40 + i * 180;
        selectRange(e, at, at + 6);
        if (chooseByMenu(w, kinds.at(i % 4))) {
            ++made;
            if (kinds.at(i % 4) != QLatin1String("comment"))
                typeInto(e, QStringLiteral("note %1").arg(i));
        }
        settle(60);
    }
    check(made == 20, QStringLiteral("Insert > Comment, Footnote, Inline Annotation, Inline Footnote at 20 places (%1)").arg(made));
    // Edit > Select > Select Annotation or Note with the cursor inside an inline annotation
    selectRange(e, 40 + 2 * 180 + 3, 40 + 2 * 180 + 3);
    chooseByMenu(w, QStringLiteral("selectAnnotation"));
    fact(QStringLiteral("S-W06 Select Annotation or Note: characters selected"), int(e->textCursor().selectedText().size()));
    // pictures
    for (const QString &f : {QStringLiteral("lens.jpg"), QStringLiteral("lens-small.jpg"), QStringLiteral("pixel.gif"), QStringLiteral("tower.png")}) {
        QTextCursor x = e->textCursor();
        x.movePosition(QTextCursor::End);
        e->setTextCursor(x);
        fileDialogLater(300, ctx().sourceDir + QStringLiteral("/tests/import/") + f);
        check(chooseByMenu(w, QStringLiteral("image")), QStringLiteral("Insert > Image From File: %1").arg(f));
        settle(500);
    }
    // links: to a document (also one moved to the Trash later), to an object, to the web
    BinderItem *target = textItems(p).value(5);
    BinderItem *doomed = textItems(p).value(6);
    for (BinderItem *t : {target, doomed}) {
        selectRange(e, 300, 310);
        withModal<QDialog>(300, [t](QDialog *d) {
            for (QTreeView *tv : d->findChildren<QTreeView *>())
                for (int r = 0; r < 200; ++r) {
                    const QModelIndex ix = tv->model()->index(r, 0);
                    if (ix.isValid() && ix.data().toString() == t->title)
                        tv->setCurrentIndex(ix);
                }
            for (QListWidget *lw : d->findChildren<QListWidget *>())
                for (int r = 0; r < lw->count(); ++r)
                    if (lw->item(r)->text() == t->title)
                        lw->setCurrentRow(r);
            d->accept();
        });
        chooseByMenu(w, QStringLiteral("docLink"));
        settle(300);
    }
    selectRange(e, 400, 410);
    withModal<QDialog>(300, [](QDialog *d) { d->accept(); });
    chooseByMenu(w, QStringLiteral("objectLink"));
    settle(300);
    selectRange(e, 500, 510);
    withModal<QDialog>(300, [](QDialog *d) {
        if (auto *le = d->findChild<QLineEdit *>())
            le->setText(QStringLiteral("https://example.org/page"));
        d->accept();
    });
    chooseByMenu(w, QStringLiteral("hyperlink"));
    settle(300);
    p->model()->moveToTrash({doomed});
    settle(200);
    // click the links (a document link opens that document)
    QTextCursor l(e->document());
    l.setPosition(303);
    const QRect r = e->cursorRect(l);
    QTest::mouseClick(e->viewport(), Qt::LeftButton, Qt::ControlModifier, r.center());
    settle(400);
    fact(QStringLiteral("S-W06 Ctrl+click on a document link"), visiblePane(w) && visiblePane(w)->currentItem() ? visiblePane(w)->currentItem()->title : QStringLiteral("nothing shown"));
    showDoc(w, it);
    e = visiblePane(w)->currentEditor();
    // delete text across a comment's anchor, undo
    if (e) {
        const QString before = rawText(e->document());
        selectRange(e, 30, 60);
        QTest::keyClick(e, Qt::Key_Delete);
        e->undo();
        check(rawText(e->document()) == before, QStringLiteral("deleting across a comment's anchor and undo give the text back"), firstDifference(before, rawText(e->document())));
    }
    finishProject(w);
    delete w;
}

// ================================================================ S-W07 clipboard
void caseSW07()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    const QList<BinderItem *> docs = textItems(p);
    TextEditor *e = showDoc(w, docs.value(3));
    if (!e)
        return;
    // within and between documents
    selectRange(e, 10, 120);
    const QString piece = e->textCursor().selectedText();
    chooseByMenu(w, QStringLiteral("copy"));
    QTextCursor endC = e->textCursor();
    endC.movePosition(QTextCursor::End);
    e->setTextCursor(endC);
    chooseByMenu(w, QStringLiteral("paste"));
    check(e->toPlainText().endsWith(QString(piece).replace(QChar::ParagraphSeparator, QLatin1Char('\n'))), QStringLiteral("Copy and Paste within a document"));
    selectRange(e, 200, 260);
    chooseByMenu(w, QStringLiteral("cut"));
    TextEditor *e2 = showDoc(w, docs.value(4));
    if (e2) {
        QTextCursor x = e2->textCursor();
        x.movePosition(QTextCursor::End);
        e2->setTextCursor(x);
        chooseByMenu(w, QStringLiteral("paste"));
        chooseByMenu(w, QStringLiteral("pastePlain"));
        chooseByMenu(w, QStringLiteral("pasteKeep"));
    }
    // into the notes and the synopsis
    if (TextEditor *n = notesEditor(w)) {
        n->setFocus(Qt::OtherFocusReason);
        chooseByMenu(w, QStringLiteral("paste"));
    } else {
        note(QStringLiteral("the notes editor is not shown"));
    }
    if (QPlainTextEdit *s = synopsisEditor(w)) {
        s->setFocus(Qt::OtherFocusReason);
        s->moveCursor(QTextCursor::End);
        chooseByMenu(w, QStringLiteral("paste"));
    }
    // a title in the binder
    if (BinderPanel *bp = binderOf(w)) {
        selectItems(w, {docs.value(4)});
        QTest::keyClick(bp->tree(), Qt::Key_F2);
        settle(150);
        if (auto *le = qobject_cast<QLineEdit *>(QApplication::focusWidget())) {
            setClipboard({}, QStringLiteral("Pasted title\nwith a second line"));
            le->end(false);
            QTest::keySequence(le, QKeySequence::Paste);
            QTest::keyClick(le, Qt::Key_Return);
        }
        settle(150);
        fact(QStringLiteral("S-W07 a two-line paste into a title"), docs.value(4)->title);
    }
    // prepared clipboard data with Paste with Formatting
    e = showDoc(w, docs.value(5));
    auto pasteAtEnd = [&](const QString &what, const QString &how) {
        QTextCursor x = e->textCursor();
        x.movePosition(QTextCursor::End);
        e->setTextCursor(x);
        e->setFocus(Qt::OtherFocusReason);
        const int before = e->document()->characterCount();
        const bool ok = chooseByMenu(w, how);
        settle(300);
        fact(QStringLiteral("S-W07 %1 with %2").arg(what, how), QStringLiteral("%1 characters added").arg(e->document()->characterCount() - before));
        return ok;
    };
    if (e) {
        setClipboard({{QStringLiteral("text/rtf"), QByteArray("{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Liberation Serif;}}\\pard\\plain\\f0\\fs24 LibreOffice text with \\b bold\\b0  and \\i italic\\i0 .\\par}")}},
                     QStringLiteral("LibreOffice text with bold and italic."));
        pasteAtEnd(QStringLiteral("RTF as LibreOffice writes it"), QStringLiteral("pasteKeep"));
        setClipboard({{QStringLiteral("text/html"), QByteArray("<html><body><!--StartFragment--><p>Firefox <b>bold</b> <a href=\"https://example.org\">link</a></p><!--EndFragment--></body></html>")}},
                     QStringLiteral("Firefox bold link"));
        pasteAtEnd(QStringLiteral("HTML as Firefox writes it"), QStringLiteral("pasteKeep"));
        setClipboard({}, QString(), QImage(), {QUrl::fromLocalFile(ctx().sourceDir + QStringLiteral("/tests/import/tower.png"))});
        pasteAtEnd(QStringLiteral("a file list"), QStringLiteral("paste"));
        setClipboard({}, QString(), testPicture(120, 80));
        pasteAtEnd(QStringLiteral("a PNG picture"), QStringLiteral("paste"));
        QString mb;
        while (mb.size() < 1024 * 1024)
            mb += atParagraphs(2700 + quint32(mb.size() % 7), 2000).join(QLatin1Char('\n')) + QLatin1Char('\n');
        setClipboard({}, mb);
        QElapsedTimer t;
        t.start();
        pasteAtEnd(QStringLiteral("1 MB of plain text"), QStringLiteral("paste"));
        fact(QStringLiteral("S-W07 1 MB paste (ms)"), int(t.elapsed()));
    }
    // copy from a Quick Reference panel and the copyholder
    selectItems(w, {docs.value(8)});
    chooseByMenu(w, QStringLiteral("quickRef"));
    settle(400);
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (tl->isVisible() && tl != w && tl->findChild<TextEditor *>()) {
            TextEditor *q = tl->findChild<TextEditor *>();
            QTextCursor x(q->document());
            x.setPosition(5);
            x.setPosition(40, QTextCursor::KeepAnchor);
            q->setTextCursor(x);
            q->copy();
            fact(QStringLiteral("S-W07 copied from a Quick Reference panel"), QApplication::clipboard()->text().left(40));
        }
    selectItems(w, {docs.value(9)});
    chooseByMenu(w, QStringLiteral("openCopyholder"));
    settle(300);
    finishProject(w);
    delete w;
}

} // namespace cs
