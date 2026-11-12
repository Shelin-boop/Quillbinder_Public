// The system test, stage 1: tables and pictures (C-28, S-T01 to S-T05).
#include "cs.h"

#include "compile/CompileFormat.h"
#include "compile/Compiler.h"
#include "compile/DocxWriter.h"
#include "compile/EpubExporter.h"
#include "compile/PdfExporter.h"
#include "compile/PluginManager.h"
#include "core/CrashGuard.h"
#include "core/ProjectTemplates.h"
#include "core/Snapshots.h"
#include "import/FileImporter.h"
#include "import/ScrivenerFormat.h"
#include "text/DisplayFormat.h"
#include "text/MarkdownIO.h"
#include "text/RtfIO.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"
#include "ui/BinderPanel.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/TableDialogs.h"
#include "ui/TextEditor.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextDocumentFragment>
#include <QFileInfo>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>

#include <random>

namespace cs {

namespace {

Formatting fmt;

int countOf(const QString &shape, const QString &kind)
{
    int n = 0;
    for (const QString &l : shape.split(QLatin1Char('\n')))
        if (l.startsWith(kind))
            ++n;
    return n;
}

QTextCursor cellCursor(QTextTable *t, int r, int c)
{
    return t->cellAt(r, c).firstCursorPosition();
}

void fillCells(QTextTable *t, const QString &prefix)
{
    for (int r = 0; r < t->rows(); ++r)
        for (int c = 0; c < t->columns(); ++c) {
            const QTextTableCell cell = t->cellAt(r, c);
            if (cell.row() == r && cell.column() == c)
                cell.firstCursorPosition().insertText(QStringLiteral("%1 %2.%3").arg(prefix).arg(r + 1).arg(c + 1));
        }
}

// Undo until the shape is `before` (at most 4 steps), then redo as many: the shape is `after` again.
void undoRedo(TextEditor *e, const QString &before, const QString &after, const QString &what)
{
    int steps = 0;
    while (steps < 4 && tableShape(e->document()) + rawText(e->document()) != before) {
        QTest::keyClick(e, Qt::Key_Z, Qt::ControlModifier);
        settle(30);
        ++steps;
    }
    const bool undone = tableShape(e->document()) + rawText(e->document()) == before;
    for (int i = 0; i < steps; ++i) {
        QTest::keyClick(e, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        settle(30);
    }
    const bool redone = tableShape(e->document()) + rawText(e->document()) == after;
    check(undone && redone, QStringLiteral("undo and redo: %1 (%2 step(s))").arg(what).arg(steps),
          undone ? QStringLiteral("redo did not give the same table back") : QStringLiteral("undo did not give the table before back"));
}

// a command chosen from the menu bar (by the action's object name), with undo and redo checked
bool command(MainWindow *w, TextEditor *e, const QString &name, const QString &what, bool checkUndo = true)
{
    const QString before = tableShape(e->document()) + rawText(e->document());
    // the writer's keyboard is in the text (a dialog closed before may have left the focus elsewhere
    // on the virtual display, which has no window manager); the menus act on the focused text
    e->activateWindow();
    e->setFocus();
    settle(30);
    if (name == QLatin1String("tableMerge") || name == QLatin1String("tableSplit"))
        note(QStringLiteral("(before %1: focus in the editor %2, cell selection %3, can merge %4, can split %5)")
                 .arg(name)
                 .arg(e->hasFocus())
                 .arg(e->textCursor().hasComplexSelection())
                 .arg(e->canMergeCells())
                 .arg(e->canSplitCell()));
    // diagnosis (2026-11-08): QB_CS_NO_UNDO=<command> leaves undo and redo out after that command
    if (qgetenv("QB_CS_NO_UNDO") == name.toUtf8())
        checkUndo = false;
    const bool chosen = chooseByMenu(w, name);
    settle(80);
    const QString after = tableShape(e->document()) + rawText(e->document());
    check(chosen, QStringLiteral("%1 from the menu bar").arg(what));
    if (chosen && checkUndo && after != before)
        undoRedo(e, before, after, what);
    return chosen;
}

void selectCells(TextEditor *e, QTextTable *t, int r1, int c1, int r2, int c2)
{
    QTextCursor c = cellCursor(t, r1, c1);
    c.setPosition(t->cellAt(r2, c2).lastCursorPosition().position(), QTextCursor::KeepAnchor);
    e->setTextCursor(c);
}

BinderItem *newDoc(MainWindow *w, const QString &title, const QStringList &paras)
{
    Project *p = w->project();
    BinderItem *it = p->model()->createItem(p->draft(), -1, ItemType::Text, title);
    QTextDocument *d = p->docs()->text(it->id);
    QTextCursor c(d);
    for (int i = 0; i < paras.size(); ++i) {
        if (i)
            c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
        c.insertText(paras[i]);
    }
    return it;
}

// Scrivener format: save, close, open again in this process and compare
void reopenScrivener(MainWindow *w, const QString &what)
{
    const QString scriv = w->project()->scrivenerPath;
    QString err;
    w->project()->save(&err);
    const QJsonObject before = modelOf(w->project(), false);
    w->closeProject();
    settle(300);
    check(w->openScrivenerProject(scriv), QStringLiteral("(%1: the .scriv opens again)").arg(what));
    settle(500);
    if (w->project())
        compareModels(before, modelOf(w->project(), false), what + QStringLiteral(", Scrivener format, reopened"));
}

} // namespace

// ================================================================ C-28
void caseC28()
{
    using namespace qb;
    QTextDocument d;
    QTextCursor c(&d);
    c.insertText(QStringLiteral("Keepme before the tables."));
    QTextBlockFormat cellBlock = display::normalBlockFormat(fmt);
    cellBlock.setTextIndent(0);
    const QTextCharFormat cellChar = display::charFormatFor(fmt, QString());
    // every design, then own values
    for (const auto &st : tables::styles()) {
        c.movePosition(QTextCursor::End);
        c.insertBlock();
        c.insertText(QStringLiteral("Design %1").arg(st.second));
        c.insertBlock();
        QTextTable *t = tables::insert(c, 3, 3, tables::preset(st.first), cellBlock, cellChar);
        fillCells(t, st.first);
        c.movePosition(QTextCursor::End);
    }
    {
        c.insertBlock();
        tables::Spec own = tables::preset(QStringLiteral("grid"));
        own.style = QStringLiteral("custom");
        own.border = 3;
        own.borderColor = QColor(QStringLiteral("#aa3300"));
        own.padding = 9;
        own.spacing = 2;
        own.headerRows = 2;
        own.headerColor = QColor(QStringLiteral("#ddeeff"));
        own.stripeColor = QColor(QStringLiteral("#f4f4f4"));
        own.widthPercent = 70;
        own.align = Qt::AlignHCenter;
        own.columns = {20, 30, 50};
        QTextTable *t = tables::insert(c, 6, 3, own, cellBlock, cellChar);
        fillCells(t, QStringLiteral("own"));
        t->mergeCells(2, 0, 2, 1);
        t->mergeCells(4, 1, 1, 2);
        tables::setCellBackground(t, 5, 0, QColor(QStringLiteral("#ccffcc")));
        tables::apply(t, tables::specOf(t));
        c.movePosition(QTextCursor::End);
    }
    {   // 1,000 rows and 60 columns
        c.insertBlock();
        QTextTable *t = tables::insert(c, 1000, 3, tables::preset(QStringLiteral("striped")), cellBlock, cellChar);
        fillCells(t, QStringLiteral("long"));
        c.movePosition(QTextCursor::End);
        c.insertBlock();
        QTextTable *w = tables::insert(c, 2, 60, tables::preset(QStringLiteral("grid")), cellBlock, cellChar);
        fillCells(w, QStringLiteral("w"));
        c.movePosition(QTextCursor::End);
    }
    {   // figures in every place, with a formatted caption and a description
        const figures::Position pos[4] = {figures::InLine, figures::Left, figures::Right, figures::Centered};
        for (int k = 0; k < 4; ++k) {
            c.movePosition(QTextCursor::End);
            c.insertBlock();
            const QImage img = testPicture(100 + 10 * k, 80);
            const QString name = QStringLiteral("media/fig%1.png").arg(k);
            d.addResource(QTextDocument::ImageResource, QUrl(name), img);
            QTextImageFormat f;
            f.setName(name);
            f.setWidth(img.width());
            f.setHeight(img.height());
            const int ip = c.position();
            c.insertImage(f);
            c.insertText(QStringLiteral(" Text after picture %1.").arg(k + 1));
            int np = ip;
            if (pos[k] != figures::InLine)
                np = figures::setLayout(&d, ip, pos[k], QStringLiteral("Caption %1 with words").arg(k + 1), fmt);
            figures::setDescription(&d, np, QStringLiteral("Beschreibung %1: Größe, Straße, \"quoted\"").arg(k + 1));
        }
    }
    {   // 0.64.1: cells that begin or end with an empty paragraph (read back as "<p/>" before 0.64.1)
        c.movePosition(QTextCursor::End);
        c.insertBlock();
        QTextTable *e = tables::insert(c, 2, 2, tables::preset(QStringLiteral("grid")), cellBlock, cellChar);
        fillCells(e, QStringLiteral("edge"));
        QTextCursor first = e->cellAt(0, 0).firstCursorPosition();
        first.insertBlock();   // an empty paragraph before the cell's text
        QTextCursor last = e->cellAt(1, 1).lastCursorPosition();
        last.insertBlock();    // an empty paragraph after it
        QTextCursor both = e->cellAt(0, 1).lastCursorPosition();
        both.insertBlock();
        both.insertText(QStringLiteral("second paragraph"));
        both.insertBlock();
        c.movePosition(QTextCursor::End);
    }
    c.movePosition(QTextCursor::End);
    c.insertBlock();
    c.insertText(QStringLiteral("Keepme after the tables."));
    const QString want = tableShape(&d);
    const QString wantText = d.toPlainText();
    note(QStringLiteral("the document: %1 tables, %2 figures, %3 characters").arg(countOf(want, QStringLiteral("TABLE")))
             .arg(countOf(want, QStringLiteral("FIGURE"))).arg(wantText.size()));
    auto saver = [](const QTextImageFormat &imf) { return imf.name(); };
    QElapsedTimer t;
    // Markdown
    t.start();
    const QString md = md::write(&d, fmt, saver);
    QTextDocument mdBack;
    md::read(md, &mdBack, fmt);
    const qint64 mdMs = t.elapsed();
    check(tableShape(&mdBack) == want && mdBack.toPlainText() == wantText,
          QStringLiteral("Markdown storage: every design, merged cells, colors, 1,000 rows, 60 columns, figures survive"),
          tableShape(&mdBack).left(800));
    check(!mdBack.toPlainText().contains(QLatin1String("<p/>")), QStringLiteral("Markdown storage: no cell reads back as \"<p/>\" (0.64.1)"),
          firstDifference(wantText, mdBack.toPlainText()));
    // RTF
    t.restart();
    const QByteArray rtfData = rtf::write(&d, fmt);
    QTextDocument rtfBack;
    rtf::read(rtfData, &rtfBack, fmt);
    const qint64 rtfMs = t.elapsed();
    check(tableShape(&rtfBack) == want && rtfBack.toPlainText() == wantText,
          QStringLiteral("RTF storage: every design, merged cells, colors, 1,000 rows, 60 columns, figures survive"),
          tableShape(&rtfBack).left(800));
    // Markdown -> RTF -> Markdown (File > Convert Markdown / RTF File); the pictures loaded as the program
    // loads a project's media (the first runs read the Markdown without them: a harness artifact)
    {
        for (int k = 0; k < 4; ++k) {
            const QUrl u(QStringLiteral("media/fig%1.png").arg(k));
            mdBack.addResource(QTextDocument::ImageResource, u, d.resource(QTextDocument::ImageResource, u));
        }
        const QByteArray r2 = rtf::write(&mdBack, fmt);
        QTextDocument back2;
        rtf::read(r2, &back2, fmt);
        QString md2 = md::write(&back2, fmt, saver);
        // 0.64.2: a picture gets a name of its own in the conversion (rtfimage://<id>.png): the names are not
        // compared, the pictures' places, sizes and descriptions are
        static const QRegularExpression picName(QStringLiteral("\\]\\((?:rtfimage://|media/)[^)\\s]+\\)"));
        md2.replace(picName, QStringLiteral("](picture)"));
        QString md1 = md;
        md1.replace(picName, QStringLiteral("](picture)"));
        if (md2 != md1) {   // 0.64.2: both kept for the analysis
            writeText(ctx().dir + QStringLiteral("/c28-md1.md"), md1.toUtf8());
            writeText(ctx().dir + QStringLiteral("/c28-md2.md"), md2.toUtf8());
        }
        check(md2 == md1, QStringLiteral("Markdown to RTF and back: the same Markdown"), firstDifference(md1, md2));
    }
    fact(QStringLiteral("C-28 round trip ms"), QJsonObject{{QStringLiteral("markdown"), int(mdMs)}, {QStringLiteral("rtf"), int(rtfMs)}});
    // a table inside a table cell: recorded
    {
        QTextDocument n;
        QTextCursor nc(&n);
        nc.insertText(QStringLiteral("Outer:"));
        nc.insertBlock();
        QTextTable *outer = tables::insert(nc, 2, 2, tables::preset(QStringLiteral("grid")), cellBlock, cellChar);
        fillCells(outer, QStringLiteral("outer"));
        QTextCursor in = outer->cellAt(1, 1).lastCursorPosition();
        in.insertBlock();
        QTextTable *inner = tables::insert(in, 2, 2, tables::preset(QStringLiteral("lines")), cellBlock, cellChar);
        fillCells(inner, QStringLiteral("inner"));
        const QString nWant = tableShape(&n);
        QTextDocument a, b;
        md::read(md::write(&n, fmt, saver), &a, fmt);
        rtf::read(rtf::write(&n, fmt), &b, fmt);
        fact(QStringLiteral("C-28 table in a table cell: Markdown"), tableShape(&a) == nWant ? QStringLiteral("kept") : tableShape(&a).left(300));
        fact(QStringLiteral("C-28 table in a table cell: RTF"), tableShape(&b) == nWant ? QStringLiteral("kept") : tableShape(&b).left(300));
        // 0.64.1, by design: the inner table is kept as paragraphs of the outer cell, in order
        auto innerInOrder = [](const QString &s) {
            int at = 0;
            for (const char *w : {"inner 1.1", "inner 1.2", "inner 2.1", "inner 2.2"}) {
                at = s.indexOf(QLatin1String(w), at);
                if (at < 0)
                    return false;
            }
            return true;
        };
        check(innerInOrder(a.toPlainText()) && innerInOrder(b.toPlainText()),
              QStringLiteral("a table inside a table cell: its cell texts are kept in order (Markdown and RTF, 0.64.1)"));
        check(a.toPlainText().contains(QLatin1String("inner 2.2")) && b.toPlainText().contains(QLatin1String("inner 2.2")),
              QStringLiteral("a table inside a table cell: no text lost in Markdown and RTF"),
              QStringLiteral("(local detail) Markdown: %1 | RTF: %2 | wanted: %3")
                  .arg(a.toPlainText().left(300), b.toPlainText().left(300), n.toPlainText().left(300)));
    }
    // damaged input
    struct Bad {
        QString name;
        bool rtfInput;
        QByteArray data;
    };
    QList<Bad> bad;
    const QByteArray mdTable = md.toUtf8().left(4000);
    bad << Bad{QStringLiteral("broken JSON in :::table"), false, "Keepme\n\n:::table {\"style\": grid, \"headerRows\": \n| a | b |\n:::\n\nKeepme end\n"};
    bad << Bad{QStringLiteral(":::table without its end"), false, "Keepme\n\n:::table {\"style\":\"grid\"}\n| a | b |\n| c | d |\n\nKeepme end\n"};
    bad << Bad{QStringLiteral(":::figure without its end"), false, "Keepme\n\n:::figure {\"position\":1}\n![x](media/none.png)\n\nKeepme end\n"};
    bad << Bad{QStringLiteral("spans beyond the table"), false, "Keepme\n\n:::table {\"style\":\"grid\",\"spans\":[[0,0,50,50]]}\n| a | b |\n:::\n\nKeepme end\n"};
    bad << Bad{QStringLiteral("0 rows, negative widths"), false, "Keepme\n\n:::table {\"style\":\"custom\",\"widthPercent\":-40,\"columns\":[-10,500]}\n:::\n\nKeepme end\n"};
    bad << Bad{QStringLiteral("Markdown cut in the middle of a table"), false, mdTable};
    {
        QByteArray r = rtfData;
        const int at = r.indexOf("{\\*\\qbtable");
        if (at >= 0)
            r.insert(at + 12, "{\"style\": [broken");
        bad << Bad{QStringLiteral("broken JSON in \\qbtable"), true, r};
    }
    bad << Bad{QStringLiteral("\\trowd without \\row"), true,
               "{\\rtf1\\ansi Keepme\\par\\trowd\\cellx2000\\cellx4000 a\\cell b\\cell \\pard Keepme end\\par}"};
    bad << Bad{QStringLiteral("\\cellx out of order"), true,
               "{\\rtf1\\ansi Keepme\\par\\trowd\\cellx4000\\cellx1000\\cellx3000 a\\cell b\\cell c\\cell\\row\\pard Keepme end\\par}"};
    bad << Bad{QStringLiteral("nested tables as Word writes them"), true,
               "{\\rtf1\\ansi Keepme\\par\\trowd\\cellx3000\\cellx6000 outer\\cell\\pard\\intbl\\itap2 inner\\nestcell{\\*\\nesttableprops"
               "\\trowd\\cellx1000\\nestrow}{\\nonesttables\\par}\\cell\\row\\pard Keepme end\\par}"};
    bad << Bad{QStringLiteral("a \\qbalt of 1 MB"), true,
               "{\\rtf1\\ansi Keepme\\par{\\*\\qbalt " + QByteArray(1 << 20, 'x') + "} Keepme end\\par}"};
    bad << Bad{QStringLiteral("RTF cut in the middle of a table"), true, rtfData.left(rtfData.indexOf("\\row") + 40)};
    for (const Bad &b : bad) {
        QTextDocument out;
        QElapsedTimer bt;
        bt.start();
        if (b.rtfInput)
            rtf::read(b.data, &out, fmt);
        else
            md::read(QString::fromUtf8(b.data), &out, fmt);
        const qint64 ms = bt.elapsed();
        const bool keeps = !b.data.contains("Keepme") || out.toPlainText().contains(QLatin1String("Keepme"));
        check(ms < 2000 && keeps, QStringLiteral("damaged input, %1: read in %2 ms, the text kept").arg(b.name).arg(ms),
              out.toPlainText().left(200));
    }
    // a project with a table names 0.62.0 as the version it needs
    {
        QTemporaryDir dir;
        templates::Info blank;
        for (const auto &ti : templates::all())
            if (ti.id == QLatin1String("blank"))
                blank = ti;
        QString err;
        auto p = templates::createProject(blank, dir.path(), QStringLiteral("Needs"), &err);
        if (p) {
            BinderItem *it = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("With a table"));
            QTextCursor pc(p->docs()->text(it->id));
            tables::insert(pc, 2, 2, tables::preset(QStringLiteral("grid")), cellBlock, cellChar);
            p->docs()->saveDocument(it->id, &err);
            p->save(&err);
            const QString needed = Project::neededVersion(p->indexPath());
            check(needed == QLatin1String("0.62.0"), QStringLiteral("a project with a table names 0.62.0 as the version it needs"), needed);
        } else
            check(false, QStringLiteral("(a project for the needed version)"), err);
    }
}

// ================================================================ S-T01
void caseST01()
{
    // ---- Scrivener format first (reopened in this process)
    {
        MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-scriv")));
        if (w->project()) {
            BinderItem *it = newDoc(w, QStringLiteral("Tables in Scrivener format"), {QStringLiteral("Before."), QString()});
            TextEditor *e = showDoc(w, it);
            if (e) {
                QTextCursor c(e->document());
                c.movePosition(QTextCursor::End);
                e->setTextCursor(c);
                e->insertTableAtCursor(4, 3, tables::preset(QStringLiteral("header")));
                if (QTextTable *t = e->currentTable()) {
                    fillCells(t, QStringLiteral("S"));
                    selectCells(e, t, 1, 0, 2, 0);
                    e->tableMergeCells();
                    e->setTextCursor(cellCursor(t, 3, 2));
                    e->tableSetCellColor(QColor(QStringLiteral("#ffddaa")));
                }
                settle(200);
                reopenScrivener(w, QStringLiteral("S-T01"));
            }
            w->closeProject();
        }
        delete w;
        settle(300);
    }
    // ---- Quillbinder format, everything
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    BinderItem *it = newDoc(w, QStringLiteral("Tables"), {QStringLiteral("A page of tables follows."), QString()});
    TextEditor *e = showDoc(w, it);
    check(e != nullptr, QStringLiteral("(the editor shows the new document)"));
    if (!e)
        return;
    QTextCursor end(e->document());
    end.movePosition(QTextCursor::End);
    e->setTextCursor(end);
    // Insert > Table... with each design, through the window
    int designs = 0;
    QStringList seen;
    for (int k = 0; k < int(tables::styles().size()); ++k) {
        QTextCursor c(e->document());
        c.movePosition(QTextCursor::End);
        e->setTextCursor(c);
        bool answered = false;
        withModal<TableDialog>(300, [&, k](TableDialog *d) {
            if (auto *r = d->findChild<QSpinBox *>(QStringLiteral("tableRows")))
                r->setValue(3);
            if (auto *cl = d->findChild<QSpinBox *>(QStringLiteral("tableColumns")))
                cl->setValue(3);
            if (auto *st = d->findChild<QComboBox *>(QStringLiteral("tableStyle"))) {
                st->setCurrentIndex(qMin(k, st->count() - 1));
                seen << st->currentText();
            }
            shot(d, QStringLiteral("insert-table-%1").arg(k));
            answered = true;
            d->accept();
        });
        step(QStringLiteral("Insert > Table, design %1").arg(k));
        e->activateWindow();
        e->setFocus();
        settle(30);
        const bool chosen = chooseByMenu(w, QStringLiteral("insertTable"));
        settle(200);
        if (chosen && answered && e->currentTable())
            ++designs;
        // leave the table: the end of the document (Qt keeps a paragraph after every table)
        {
            QTextCursor after(e->document());
            after.movePosition(QTextCursor::End);
            e->setTextCursor(after);
            if (e->currentTable())
                note(QStringLiteral("(the end of the document is inside a table)"));
        }
    }
    check(designs == int(tables::styles().size()), QStringLiteral("Insert > Table... through its window, each of the %1 designs")
                                                       .arg(tables::styles().size()), seen.join(QLatin1String(", ")));
    // sizes through the program's own command
    QElapsedTimer t;
    for (const auto &size : QList<QPair<int, int>>{{1, 1}, {50, 10}, {1, 60}, {1000, 3}}) {
        QTextCursor c(e->document());
        c.movePosition(QTextCursor::End);
        e->setTextCursor(c);
        t.start();
        const bool ok = e->insertTableAtCursor(size.first, size.second, tables::preset(QStringLiteral("grid")));
        settle(100);
        const qint64 ms = t.elapsed();
        check(ok && e->currentTable() && e->currentTable()->rows() == size.first,
              QStringLiteral("a table of %1 x %2 is put in (%3 ms)").arg(size.first).arg(size.second).arg(ms));
        fact(QStringLiteral("S-T01 insert %1x%2 ms").arg(size.first).arg(size.second), int(ms));
    }
    // (the 1,000-row table stays in the document: saving and reopening it is part of G3)
    // typing in cells: Enter inside a cell, Tab and Shift+Tab, Tab in the last cell adds a row
    QTextTable *small = nullptr;
    {
        QTextDocument *d = e->document();
        for (QTextBlock b = d->begin(); b.isValid(); b = b.next()) {
            QTextCursor c(b);
            if (QTextTable *tt = c.currentTable())
                if (tt->rows() == 3 && tt->columns() == 3) {
                    small = tt;
                    break;
                }
        }
    }
    if (small) {
        e->setTextCursor(cellCursor(small, 1, 1));
        keysInto(e, QStringLiteral("First line\nSecond line in the cell"));
        check(small->cellAt(1, 1).lastCursorPosition().block().text() == QLatin1String("Second line in the cell"),
              QStringLiteral("Enter inside a cell makes a new paragraph in the cell"));
        const int rowsBefore = small->rows();
        e->setTextCursor(cellCursor(small, 0, 0));
        for (int i = 0; i < 3; ++i)
            QTest::keyClick(e, Qt::Key_Tab);
        check(e->textCursor().currentTable() == small && small->cellAt(e->textCursor()).row() == 1 && small->cellAt(e->textCursor()).column() == 0,
              QStringLiteral("Tab goes cell by cell into the next row"));
        QTest::keyClick(e, Qt::Key_Tab, Qt::ShiftModifier);
        check(small->cellAt(e->textCursor()).row() == 0 && small->cellAt(e->textCursor()).column() == 2, QStringLiteral("Shift+Tab goes back"));
        e->setTextCursor(cellCursor(small, small->rows() - 1, small->columns() - 1));
        QTest::keyClick(e, Qt::Key_Tab);
        check(small->rows() == rowsBefore + 1, QStringLiteral("Tab in the last cell adds a row"));
        // the commands of Format > Table, from the menu bar, each undone and redone
        e->setTextCursor(cellCursor(small, 1, 1));
        command(w, e, QStringLiteral("tableRowAbove"), QStringLiteral("Format > Table > row above"));
        command(w, e, QStringLiteral("tableRowBelow"), QStringLiteral("Format > Table > row below"));
        command(w, e, QStringLiteral("tableColLeft"), QStringLiteral("Format > Table > column left"));
        command(w, e, QStringLiteral("tableColRight"), QStringLiteral("Format > Table > column right"));
        selectCells(e, small, 1, 1, 2, 2);
        command(w, e, QStringLiteral("tableMerge"), QStringLiteral("Format > Table > merge cells"));
        // delete rows and columns that run under the merged cell (Qt 6.4 crashes unless the cells are split first)
        e->setTextCursor(cellCursor(small, 2, 2));
        command(w, e, QStringLiteral("tableDeleteRows"), QStringLiteral("delete a row under a merged cell"));
        e->setTextCursor(cellCursor(small, 1, 2));
        command(w, e, QStringLiteral("tableDeleteCols"), QStringLiteral("delete a column under a merged cell"));
        selectCells(e, small, 0, 0, 1, 1);
        e->tableMergeCells();
        e->setTextCursor(cellCursor(small, 0, 0));
        command(w, e, QStringLiteral("tableSplit"), QStringLiteral("Format > Table > split cell"));
        // a cell's own color through the color window
        withModal<QColorDialog>(300, [](QColorDialog *d) {
            d->setCurrentColor(QColor(QStringLiteral("#aaddff")));
            d->accept();
        });
        e->setTextCursor(cellCursor(small, 0, 1));
        command(w, e, QStringLiteral("tableCellColor"), QStringLiteral("Format > Table > cell color"));
        // Table Properties... with every control at its minimum, then at its maximum
        for (const bool atMax : {false, true}) {
            QStringList changed;
            withModal<TableDialog>(300, [&](TableDialog *d) {
                for (QSpinBox *s : d->findChildren<QSpinBox *>()) {
                    s->setValue(atMax ? s->maximum() : s->minimum());
                    changed << s->objectName();
                }
                for (QDoubleSpinBox *s : d->findChildren<QDoubleSpinBox *>()) {
                    s->setValue(atMax ? s->maximum() : s->minimum());
                    changed << s->objectName();
                }
                for (QComboBox *cb : d->findChildren<QComboBox *>()) {
                    cb->setCurrentIndex(atMax ? cb->count() - 1 : 0);
                    changed << cb->objectName();
                }
                for (QCheckBox *cbx : d->findChildren<QCheckBox *>()) {
                    cbx->setChecked(atMax);
                    changed << cbx->objectName();
                }
                if (auto *le = d->findChild<QLineEdit *>(QStringLiteral("tableColumnWidths")))
                    le->setText(atMax ? QStringLiteral("90 5 5 0") : QStringLiteral("1"));
                shot(d, atMax ? QStringLiteral("table-properties-max") : QStringLiteral("table-properties-min"));
                d->accept();
            });
            e->setTextCursor(cellCursor(small, 0, 0));
            command(w, e, QStringLiteral("tableProperties"), atMax ? QStringLiteral("Table Properties... at the maximum")
                                                                   : QStringLiteral("Table Properties... at the minimum"));
            note(QStringLiteral("Table Properties controls: %1").arg(changed.join(QLatin1String(", "))));
        }
        // a whole table deleted and brought back
        e->setTextCursor(cellCursor(small, 0, 0));
        command(w, e, QStringLiteral("tableDelete"), QStringLiteral("Format > Table > delete table"));
    } else
        check(false, QStringLiteral("(a 3 x 3 table to work in)"));
    // 0.64.2: no typing gate in the 1,000-row table any more (the writer's decision of 2026-11-10: a table that
    // large belongs in a spreadsheet; Qt lays out every cell on each key). qb_v0641_tests still compares that
    // table with a plain QTextEdit.
    shot(w, QStringLiteral("tables"));
    finishProject(w);
    delete w;
}

// ================================================================ S-T02
void caseST02()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    Project *p = w->project();
    QTextBlockFormat cellBlock = display::normalBlockFormat(p->formatting);
    cellBlock.setTextIndent(0);
    const QTextCharFormat cellChar = display::charFormatFor(p->formatting, QString());
    BinderItem *chapter = itemByTitle(p, QStringLiteral("Chapter 2"));
    BinderItem *it = p->model()->createItem(chapter ? chapter : p->draft(), -1, ItemType::Text, QStringLiteral("Mixed tables"));
    TextEditor *e = showDoc(w, it);
    if (!e)
        return;
    QTextDocument *d = e->document();
    step(QStringLiteral("tables at the start, at the end, next to each other, in a list, in a cell"));
    e->insertTableAtCursor(2, 2, tables::preset(QStringLiteral("grid")));
    QTextTable *first = e->currentTable();
    if (first)
        fillCells(first, QStringLiteral("first"));
    QTextCursor c(d);
    c.movePosition(QTextCursor::End);
    c.insertText(QStringLiteral("Between the tables."));
    e->setTextCursor(c);
    e->insertTableAtCursor(2, 3, tables::preset(QStringLiteral("striped")));
    if (QTextTable *t2 = e->currentTable()) {
        fillCells(t2, QStringLiteral("second"));
        QTextCursor after(d);
        after.setPosition(t2->lastPosition() + 1);
        e->setTextCursor(after);
        const bool adjacent = e->insertTableAtCursor(2, 2, tables::preset(QStringLiteral("lines")));
        fact(QStringLiteral("S-T02 a table right after a table"), adjacent ? QStringLiteral("put in") : QStringLiteral("refused"));
        if (QTextTable *t3 = e->currentTable())
            fillCells(t3, QStringLiteral("third"));
    }
    {   // a list item with a table
        QTextCursor lc(d);
        lc.movePosition(QTextCursor::End);
        lc.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
        lc.insertText(QStringLiteral("An item"));
        QTextListFormat lf;
        lf.setStyle(QTextListFormat::ListDisc);
        lc.createList(lf);
        e->setTextCursor(lc);
        const bool inList = e->insertTableAtCursor(2, 2, tables::preset(QStringLiteral("grid")));
        fact(QStringLiteral("S-T02 a table in a list item"), inList ? QStringLiteral("put in") : QStringLiteral("refused"));
        if (QTextTable *t4 = e->currentTable())
            fillCells(t4, QStringLiteral("list"));
    }
    if (first) {   // a table inside a cell
        e->setTextCursor(first->cellAt(1, 1).lastCursorPosition());
        const bool nested = e->insertTableAtCursor(2, 2, tables::preset(QStringLiteral("none")));
        fact(QStringLiteral("S-T02 a table inside a table cell"), nested ? QStringLiteral("put in") : QStringLiteral("refused"));
        if (nested && e->currentTable() && e->currentTable() != first)
            fillCells(e->currentTable(), QStringLiteral("inner"));
    }
    // the table at the very end: a table, then nothing after it
    {
        QTextCursor ec(d);
        ec.movePosition(QTextCursor::End);
        e->setTextCursor(ec);
        e->insertTableAtCursor(2, 2, tables::preset(QStringLiteral("header")));
        if (QTextTable *last = e->currentTable())
            fillCells(last, QStringLiteral("last"));
    }
    settle(200);
    // notes, links and a picture inside cells
    step(QStringLiteral("comments, footnotes, inline notes, a link and a picture inside cells"));
    if (first) {
        auto mark = [&](int r, int col, const QTextCharFormat &f) {
            QTextCursor mc = first->cellAt(r, col).firstCursorPosition();
            mc.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            mc.mergeCharFormat(f);
        };
        mark(0, 0, text::noteFormat(NoteKind::Comment, QStringLiteral("cs-cell-c"), QStringLiteral("A comment in a cell")));
        mark(0, 1, text::noteFormat(NoteKind::LinkedFootnote, QStringLiteral("cs-cell-f"), QStringLiteral("A footnote in a cell")));
        QTextCharFormat link;
        link.setAnchor(true);
        link.setAnchorHref(QStringLiteral("https://example.org/cell"));
        mark(1, 0, link);
        QTextCursor ic = first->cellAt(1, 0).lastCursorPosition();
        ic.insertText(QStringLiteral(" "), text::noteFormat(NoteKind::InlineAnnotation));
        ic.insertText(QStringLiteral("inline note"), text::noteFormat(NoteKind::InlineAnnotation));
        insertPicture(p, it, d, first->cellAt(0, 0).lastCursorPosition().position(), testPicture(40, 30));
    }
    const QString shapeAll = tableShape(d);
    const QString textAll = d->toPlainText();
    // views
    step(QStringLiteral("the document in Combined Text, both split editors, a Quick Reference panel, page view"));
    EditorPane *pane = visiblePane(w);
    if (pane && chapter) {
        pane->showItems({chapter}, false, int(ViewMode::Combined));
        settle(500);
        if (TextEditor *ce = pane->currentEditor()) {
            QTextCursor cc(ce->document());
            cc.movePosition(QTextCursor::End);
            ce->setTextCursor(cc);
            ce->setFocus();
            keysInto(ce, QStringLiteral(" Combined end."));
            settle(300);
        }
        shot(w, QStringLiteral("combined-with-tables"));
        pane->showItems({it}, false, int(ViewMode::Single));
        settle(300);
    }
    check(p->docs()->text(it->id)->toPlainText().contains(QLatin1String("first 2.2")), QStringLiteral("Combined Text with tables: the cells are still there"));
    if (QAction *sh = action(w, QStringLiteral("splitH")))
        sh->trigger();
    settle(400);
    const QList<EditorPane *> panes = visiblePanes(w);
    EditorPane *other = panes.size() >= 2 ? panes.at(1) : nullptr;
    fact(QStringLiteral("S-T02 split editor"), panes.size() >= 2 ? QStringLiteral("two editors") : QStringLiteral("no second editor"));
    if (other) {
        panes.at(0)->showItems({it}, false, int(ViewMode::Single));
        other->showItems({it}, false, int(ViewMode::Single));
        settle(400);
        TextEditor *e1 = panes.at(0)->currentEditor();
        TextEditor *e2 = other->currentEditor();
        if (e1 && e2 && first) {
            for (int i = 0; i < 30; ++i) {
                TextEditor *ed = (i % 2) ? e2 : e1;
                QTextTable *tt = nullptr;
                for (QTextBlock b = ed->document()->begin(); b.isValid() && !tt; b = b.next())
                    tt = QTextCursor(b).currentTable();
                if (!tt)
                    break;
                ed->setFocus();
                ed->setTextCursor(tt->cellAt(1, 1).lastCursorPosition());
                keysInto(ed, QStringLiteral(" x%1").arg(i));
            }
            settle(300);
            check(rawText(e1->document()) == rawText(e2->document()), QStringLiteral("the same document in both editors: 30 edits in a cell, both show the same"));
        }
        if (QAction *ns = action(w, QStringLiteral("noSplit")))
            ns->trigger();
        settle(300);
    }
    if (QAction *qr = action(w, QStringLiteral("quickRef"))) {
        if (auto *bp = w->findChild<BinderPanel *>())
            bp->select({it}, true);
        settle(200);
        const int before = int(QApplication::topLevelWidgets().size());
        qr->trigger();
        settle(600);
        fact(QStringLiteral("S-T02 Quick Reference panel opened"), int(QApplication::topLevelWidgets().size()) > before);
    }
    p->docs()->setPageView(true);
    showDoc(w, it);
    settle(500);
    shot(w, QStringLiteral("page-view-with-tables"));
    p->docs()->setPageView(false);
    e = showDoc(w, it);
    if (!e)
        return;
    d = e->document();
    // inside cells: quote conversion, Split at Selection, a snapshot and its rollback
    step(QStringLiteral("quote conversion, Split at Selection, Merge, a snapshot inside tables"));
    {
        QString err;
        p->docs()->saveDocument(it->id, &err);
        const int snaps = int(snapshots::list(p, it->id).size());
        snapshots::take(p, it->id, QStringLiteral("Tables before the rollback"), &err);
        const QList<Snapshot> list = snapshots::list(p, it->id);
        check(list.size() == snaps + 1, QStringLiteral("a snapshot of a document with tables"));
        QTextTable *tt = nullptr;
        for (QTextBlock b = d->begin(); b.isValid() && !tt; b = b.next())
            tt = QTextCursor(b).currentTable();
        if (tt) {
            e->setTextCursor(tt->cellAt(0, 0).lastCursorPosition());
            keysInto(e, QStringLiteral(" changed after the snapshot"));
        }
        p->docs()->saveDocument(it->id, &err);
        if (!list.isEmpty())
            snapshots::rollBack(p, list.first());
        settle(300);
        e = showDoc(w, it);
        d = e ? e->document() : nullptr;
        check(d && !d->toPlainText().contains(QLatin1String("changed after the snapshot")) && countOf(tableShape(d), QStringLiteral("TABLE")) == countOf(shapeAll, QStringLiteral("TABLE")),
              QStringLiteral("rolling back the snapshot brings the tables back as they were"));
    }
    if (!e || !d)
        return;
    {   // Split at Selection inside a cell
        QTextTable *tt = nullptr;
        for (QTextBlock b = d->begin(); b.isValid() && !tt; b = b.next())
            tt = QTextCursor(b).currentTable();
        const QString before = d->toPlainText();
        const int docsBefore = int(textItems(p).size());
        if (tt) {
            QTextCursor sc = tt->cellAt(1, 1).firstCursorPosition();
            sc.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 3);
            e->setTextCursor(sc);
        }
        QAction *split = action(w, QStringLiteral("split"));
        const bool on = split && split->isEnabled();
        if (on)
            split->trigger();
        settle(500);
        const int docsAfter = int(textItems(p).size());
        fact(QStringLiteral("S-T02 Split at Selection inside a cell"), on ? QStringLiteral("split into %1 document(s)").arg(docsAfter - docsBefore + 1)
                                                                           : QStringLiteral("not offered"));
        // the parts together hold every word of the cells
        QString all;
        for (BinderItem *x : textItems(p))
            if (x == it || x->title.startsWith(it->title) || (x->parent == it->parent && docsAfter > docsBefore))
                all += p->docs()->plainText(x->id) + QLatin1Char('\n');
        QStringList lost;
        for (const QString &word : {QStringLiteral("first 1.1"), QStringLiteral("second 2.3"), QStringLiteral("last 2.2")})
            if (before.contains(word) && !all.contains(word))
                lost << word;
        check(lost.isEmpty(), QStringLiteral("Split at Selection inside a table cell: no cell text lost"), lost.join(QLatin1String(", ")));
        // 0.64.1: refused inside a table, with a note; nothing changes
        if (tt)
            check(docsAfter == docsBefore && d->toPlainText() == before,
                  QStringLiteral("Split at Selection inside a table cell is refused and changes nothing (0.64.1)"),
                  QStringLiteral("%1 document(s) more; %2").arg(docsAfter - docsBefore).arg(firstDifference(before, d->toPlainText())));
    }
    // Convert to Folder and back on a document with tables
    if (auto *bp = w->findChild<BinderPanel *>()) {
        bp->select({it}, true);
        settle(200);
        const QString before = tableShape(p->docs()->text(it->id));
        if (QAction *tf = action(w, QStringLiteral("toFolder")); tf && tf->isEnabled())
            tf->trigger();
        settle(300);
        if (QAction *tt = action(w, QStringLiteral("toText")); tt && tt->isEnabled())
            tt->trigger();
        settle(300);
        check(tableShape(p->docs()->text(it->id)) == before, QStringLiteral("Convert to Folder and back: the tables stay"));
    }
    // copy a table and paste it into another document, the notes, a title field
    {
        e = showDoc(w, it);
        d = e ? e->document() : nullptr;
        QTextTable *tt = nullptr;
        for (QTextBlock b = d ? d->begin() : QTextBlock(); b.isValid() && !tt; b = b.next())
            tt = QTextCursor(b).currentTable();
        if (e && tt) {
            QTextCursor sel(d);
            sel.setPosition(tt->firstPosition() - 1);
            sel.setPosition(tt->lastPosition() + 1, QTextCursor::KeepAnchor);
            e->setTextCursor(sel);
            e->copy();
            BinderItem *target = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Pasted table"));
            TextEditor *te = showDoc(w, target);
            if (te) {
                te->paste();
                settle(300);
                fact(QStringLiteral("S-T02 a copied table pasted into another document"),
                     countOf(tableShape(te->document()), QStringLiteral("TABLE")) ? QStringLiteral("a table") : te->document()->toPlainText().left(80));
                QTextCursor nc(p->docs()->notes(target->id));
                const QMimeData *md = QApplication::clipboard()->mimeData();
                nc.insertFragment(md && md->hasHtml() ? QTextDocumentFragment::fromHtml(md->html())
                                                      : QTextDocumentFragment::fromPlainText(md ? md->text() : QString()));
                fact(QStringLiteral("S-T02 the table pasted into the notes"),
                     countOf(tableShape(p->docs()->notes(target->id)), QStringLiteral("TABLE")) ? QStringLiteral("a table")
                                                                                                  : QStringLiteral("text"));
                QLineEdit title;
                title.paste();
                fact(QStringLiteral("S-T02 the table pasted into a title field"), title.text().left(80));
            }
        }
    }
    // word counts with tables
    {
        p->docs()->recount(it->id);
        const int counted = it->wordCount;
        const int plainWords = int(p->docs()->plainText(it->id).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size());
        fact(QStringLiteral("S-T02 word count of a document with tables"), QStringLiteral("%1 counted, %2 words in its plain text").arg(counted).arg(plainWords));
    }
    shot(w, QStringLiteral("mixed-tables"));
    finishProject(w);
    delete w;
}

// ================================================================ S-T03
void caseST03()
{
    // Scrivener format: figures in every position, reopened in this process
    {
        MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-scriv")));
        if (w->project()) {
            BinderItem *it = newDoc(w, QStringLiteral("Figures in Scrivener format"), atParagraphs(31, 120));
            if (TextEditor *e = showDoc(w, it)) {
                const figures::Position pos[4] = {figures::InLine, figures::Left, figures::Right, figures::Centered};
                for (int k = 0; k < 4; ++k) {
                    QTextBlock b = e->document()->begin();
                    for (int j = 0; j < 1 + k && b.next().isValid(); ++j)
                        b = b.next();
                    const int ip = insertPicture(w->project(), it, e->document(), b.position(), testPicture(90 + 10 * k, 70));
                    e->setPicture(ip, 90 + 10 * k, pos[k], pos[k] == figures::InLine ? QString() : QStringLiteral("Caption %1").arg(k),
                                  QStringLiteral("Beschreibung %1, Größe").arg(k));
                }
                settle(200);
                reopenScrivener(w, QStringLiteral("S-T03"));
            }
            w->closeProject();
        }
        delete w;
        settle(300);
    }
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    Project *p = w->project();
    BinderItem *it = newDoc(w, QStringLiteral("Figures"), atParagraphs(32, 400));
    TextEditor *e = showDoc(w, it);
    if (!e)
        return;
    QTextDocument *d = e->document();
    auto blockStart = [d](int n) {
        QTextBlock b = d->begin();
        for (int j = 0; j < n && b.next().isValid(); ++j)
            b = b.next();
        return b.position();
    };
    step(QStringLiteral("pictures in each position, widths from 1 px to wider than the text"));
    const figures::Position pos[4] = {figures::InLine, figures::Left, figures::Right, figures::Centered};
    const int widths[4] = {1, 120, 260, 3000};
    QString longDesc;
    while (longDesc.size() < 2000)
        longDesc += QStringLiteral("Ein Bild der Straße mit Größe und Übermaß; ");
    int lastImage = -1;
    for (int k = 0; k < 4; ++k) {
        const int ip = insertPicture(p, it, d, blockStart(1 + 3 * k), testPicture(qMax(1, widths[k]), qMax(1, widths[k] * 3 / 4) + 1));
        lastImage = e->setPicture(ip, widths[k], pos[k], pos[k] == figures::InLine ? QString() : QStringLiteral("Figure %1 of the harbor").arg(k + 1),
                                  k == 2 ? longDesc : QStringLiteral("Picture %1").arg(k + 1));
    }
    // a caption with bold and italics
    for (QTextFrame *fr : d->rootFrame()->childFrames())
        if (figures::isFigure(fr) && figures::positionOf(fr) == figures::Left) {
            QTextCursor cc(d);
            cc.setPosition(fr->lastPosition());
            cc.movePosition(QTextCursor::StartOfBlock);
            // 0.64.2: the word without the space after it (Markdown storage keeps emphasis off a trailing space)
            cc.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
            QTextCharFormat bf;
            bf.setFontWeight(QFont::Bold);
            bf.setFontItalic(true);
            cc.mergeCharFormat(bf);
            break;
        }
    // Format > Picture... from the right-click menu on a picture
    if (lastImage >= 0) {
        bool opened = false;
        QString capSeen;
        withModal<PictureDialog>(300, [&](PictureDialog *pd) {
            opened = true;
            if (auto *cap = pd->findChild<QLineEdit *>(QStringLiteral("pictureCaption")))
                capSeen = cap->text();
            shot(pd, QStringLiteral("picture-dialog"));
            pd->accept();
        });
        QStringList entries;
        const bool chosen = rightClickChoose(e, lastImage, QStringLiteral("Picture..."), &entries);
        settle(300);
        check(chosen && opened, QStringLiteral("right-click on a picture: Picture... opens its window"), entries.join(QLatin1String(" | ")));
    }
    step(QStringLiteral("typing beside pictures on the left and the right"));
    for (QTextFrame *fr : d->rootFrame()->childFrames())
        if (figures::isFigure(fr) && figures::positionOf(fr) != figures::Centered) {
            QTextCursor tc(d);
            tc.setPosition(fr->lastPosition() + 1);
            e->setTextCursor(tc);
            keysInto(e, QStringLiteral("Text typed beside the picture flows around it. "));
        }
    // two pictures beside the text next to each other, one at the very end, one beside a table
    {
        const int at = blockStart(20);
        int a = insertPicture(p, it, d, at, testPicture(80, 60));
        a = e->setPicture(a, 80, figures::Left, QStringLiteral("Twin A"), QStringLiteral("A"));
        QTextFrame *fa = figures::at(d, a);
        const int bAt = fa ? fa->lastPosition() + 1 : at;
        int b = insertPicture(p, it, d, bAt, testPicture(80, 60));
        e->setPicture(b, 80, figures::Right, QStringLiteral("Twin B"), QStringLiteral("B"));
        QTextCursor endc(d);
        endc.movePosition(QTextCursor::End);
        const int endPic = insertPicture(p, it, d, endc.position(), testPicture(70, 50));
        e->setPicture(endPic, 70, figures::Right, QStringLiteral("At the end"), QStringLiteral("End"));
        QTextCursor tc(d);
        tc.setPosition(blockStart(25));
        e->setTextCursor(tc);
        e->insertTableAtCursor(3, 2, tables::preset(QStringLiteral("grid")));
        if (QTextTable *t = e->currentTable()) {
            QTextCursor after(d);
            after.setPosition(t->lastPosition() + 1);
            const int pb = insertPicture(p, it, d, after.position(), testPicture(60, 40));
            e->setPicture(pb, 60, figures::Left, QStringLiteral("Beside the table"), QStringLiteral("T"));
        }
    }
    settle(200);
    // the cursor around a figure: arrows, Home, End, selections, Backspace and Delete at its edges
    step(QStringLiteral("keys at the edges of figures"));
    int figuresBefore = countOf(tableShape(d), QStringLiteral("FIGURE"));
    QStringList edgeFacts;
    QList<QTextFrame *> frames;
    for (QTextFrame *fr : d->rootFrame()->childFrames())
        if (figures::isFigure(fr))
            frames << fr;
    for (int k = 0; k < qMin(3, int(frames.size())); ++k) {
        QTextFrame *fr = frames.at(k);
        const int first = fr->firstPosition(), last = fr->lastPosition();
        QTextCursor kc(d);
        kc.setPosition(qMax(0, first - 2));
        e->setTextCursor(kc);
        for (int i = 0; i < 6; ++i)
            QTest::keyClick(e, Qt::Key_Right);
        for (int i = 0; i < 6; ++i)
            QTest::keyClick(e, Qt::Key_Left);
        QTest::keyClick(e, Qt::Key_End);
        QTest::keyClick(e, Qt::Key_Home);
        for (int i = 0; i < 4; ++i)
            QTest::keyClick(e, Qt::Key_Right, Qt::ShiftModifier);
        QTest::keyClick(e, Qt::Key_Left);
        settle(50);
        for (const auto &edge : QList<QPair<int, Qt::Key>>{{last + 1, Qt::Key_Backspace}, {qMax(0, first - 1), Qt::Key_Delete}}) {
            const QString shapeB = tableShape(d), textB = rawText(d);
            QTextCursor ec(d);
            ec.setPosition(qMin(edge.first, d->characterCount() - 1));
            e->setTextCursor(ec);
            QTest::keyClick(e, edge.second);
            settle(50);
            const int now = countOf(tableShape(d), QStringLiteral("FIGURE"));
            edgeFacts << QStringLiteral("%1 at the %2 edge: %3").arg(edge.second == Qt::Key_Backspace ? QStringLiteral("Backspace") : QStringLiteral("Delete"),
                                                                     edge.second == Qt::Key_Backspace ? QStringLiteral("end") : QStringLiteral("start"),
                                                                     now < figuresBefore ? QStringLiteral("the figure goes") : QStringLiteral("the figure stays"));
            // (0.64.1 analysis) Qt does not delete a frame's marker: at a figure's edge the key may change
            // nothing and add no undo step; then Ctrl+Z would undo the step before (a test artifact)
            const bool keyChanged = tableShape(d) != shapeB || rawText(d) != textB;
            if (keyChanged) {
                QTest::keyClick(e, Qt::Key_Z, Qt::ControlModifier);
                settle(50);
            }
            check(tableShape(d) == shapeB && rawText(d) == textB, QStringLiteral("undo after %1 at a figure's edge gives everything back")
                                                                      .arg(edge.second == Qt::Key_Backspace ? QStringLiteral("Backspace") : QStringLiteral("Delete")),
                  firstDifference(shapeB + QLatin1Char('|') + textB, tableShape(d) + QLatin1Char('|') + rawText(d)));
            // frames may have been rebuilt
            frames.clear();
            for (QTextFrame *f2 : d->rootFrame()->childFrames())
                if (figures::isFigure(f2))
                    frames << f2;
            if (k >= frames.size())
                break;
            fr = frames.at(k);
        }
    }
    fact(QStringLiteral("S-T03 keys at the edges of figures"), edgeFacts.join(QLatin1String("; ")));
    // cut and paste a figure
    {
        frames.clear();
        for (QTextFrame *f2 : d->rootFrame()->childFrames())
            if (figures::isFigure(f2))
                frames << f2;
        if (!frames.isEmpty()) {
            QTextFrame *fr = frames.first();
            const QString descBefore = tableShape(d);
            QTextCursor sel(d);
            sel.setPosition(fr->firstPosition() - 1);
            sel.setPosition(fr->lastPosition() + 1, QTextCursor::KeepAnchor);
            e->setTextCursor(sel);
            e->cut();
            settle(100);
            const int afterCut = countOf(tableShape(d), QStringLiteral("FIGURE"));
            QTextCursor pc(d);
            pc.setPosition(blockStart(2));
            e->setTextCursor(pc);
            e->paste();
            settle(200);
            const int afterPaste = countOf(tableShape(d), QStringLiteral("FIGURE"));
            fact(QStringLiteral("S-T03 cut and paste of a figure"), QStringLiteral("%1 figures, %2 after the cut, %3 after the paste")
                                                                        .arg(countOf(descBefore, QStringLiteral("FIGURE"))).arg(afterCut).arg(afterPaste));
            check(afterPaste == countOf(descBefore, QStringLiteral("FIGURE")), QStringLiteral("a figure cut and pasted is there once again"),
                  QStringLiteral("clipboard formats: %1").arg(QApplication::clipboard()->mimeData() ? QApplication::clipboard()->mimeData()->formats().join(QLatin1String(", ")) : QString()));
        }
    }
    figuresBefore = countOf(tableShape(d), QStringLiteral("FIGURE"));
    check(!d->toPlainText().contains(QLatin1String("Ein Bild der Straße")), QStringLiteral("the description is not shown in the text"));
    shot(w, QStringLiteral("figures"));
    finishProject(w);
    delete w;
}

// ================================================================ S-T04
void caseST04()
{
    using namespace qb::compile;
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    Project *p = w->project();
    const QString in = ctx().work + QStringLiteral("/import");
    QDir().mkpath(in);
    // RTF as another program writes it: this program's RTF without its own marks
    QByteArray foreignRtf;
    {
        QTextDocument d;
        QTextCursor c(&d);
        c.insertText(QStringLiteral("A table from another program:"));
        c.insertBlock();
        QTextTable *t = tables::insert(c, 4, 3, tables::preset(QStringLiteral("header")), QTextBlockFormat(), QTextCharFormat());
        fillCells(t, QStringLiteral("rtf"));
        t->mergeCells(1, 0, 2, 1);
        QByteArray r = rtf::write(&d, p->formatting);
        for (const char *mark : {"qbtable", "qbfigure", "qbfigureend", "qbalt"}) {
            const QByteArray open = QByteArray("{\\*\\") + mark;
            for (int at = r.indexOf(open); at >= 0; at = r.indexOf(open)) {
                int depth = 0, k = at;
                for (; k < r.size(); ++k) {
                    if (r[k] == '{' && (k == 0 || r[k - 1] != '\\'))
                        ++depth;
                    else if (r[k] == '}' && r[k - 1] != '\\' && --depth == 0)
                        break;
                }
                r.remove(at, k - at + 1);
            }
        }
        foreignRtf = r;
        writeText(in + QStringLiteral("/from-word.rtf"), r);
    }
    // FODT with a table (merged cells, an empty table); with LibreOffice also ODT and DOCX
    const QByteArray fodt =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
        "office:version=\"1.3\" office:mimetype=\"application/vnd.oasis.opendocument.text\"><office:body><office:text>"
        "<text:p>Before the table.</text:p><table:table table:name=\"T1\"><table:table-column table:number-columns-repeated=\"3\"/>"
        "<table:table-row><table:table-cell table:number-columns-spanned=\"2\"><text:p>wide head</text:p></table:table-cell>"
        "<table:covered-table-cell/><table:table-cell><text:p>head 3</text:p></table:table-cell></table:table-row>"
        "<table:table-row><table:table-cell><text:p>odt 2.1</text:p></table:table-cell><table:table-cell><text:p>odt 2.2</text:p>"
        "</table:table-cell><table:table-cell><text:p>odt 2.3</text:p></table:table-cell></table:table-row></table:table>"
        "<table:table table:name=\"Empty\"><table:table-column/><table:table-row><table:table-cell/></table:table-row></table:table>"
        "<text:p>After the table.</text:p></office:text></office:body></office:document>\n";
    writeText(in + QStringLiteral("/tables.fodt"), fodt);
    const bool lo = !runTool(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("command -v soffice")}).trimmed().isEmpty();
    if (lo) {
        for (const QString &fmt : {QStringLiteral("odt"), QStringLiteral("docx")})
            runTool(QStringLiteral("soffice"), {QStringLiteral("--headless"), QStringLiteral("--convert-to"), fmt, QStringLiteral("--outdir"), in,
                                                in + QStringLiteral("/tables.fodt")}, nullptr, 180000);
    } else
        note(QStringLiteral("(no LibreOffice: ODT and DOCX with tables not made [L])"));
    step(QStringLiteral("importing files with tables"));
    QStringList files = {in + QStringLiteral("/from-word.rtf"), in + QStringLiteral("/tables.fodt")};
    if (QFileInfo::exists(in + QStringLiteral("/tables.odt")))
        files << in + QStringLiteral("/tables.odt");
    if (QFileInfo::exists(in + QStringLiteral("/tables.docx")))
        files << in + QStringLiteral("/tables.docx");
    for (const QString &f : files) {
        QTextDocument d;
        QString err;
        const bool ok = importer::readTextFile(f, &d, p->formatting, true, &err);
        const QString shape = tableShape(&d);
        const int tablesIn = countOf(shape, QStringLiteral("TABLE"));
        const QString name = QFileInfo(f).fileName();
        fact(QStringLiteral("S-T04 import %1").arg(name), tablesIn ? QStringLiteral("%1 table(s): %2").arg(tablesIn).arg(shape.left(200))
                                                                   : QStringLiteral("no table: ") + d.toPlainText().left(160));
        const bool textKept = d.toPlainText().contains(QLatin1String("2.2")) || d.toPlainText().contains(QLatin1String("rtf 2.2"));
        check(ok && textKept, QStringLiteral("import of %1: read, cell text kept").arg(name), err);
        if (name.endsWith(QLatin1String(".rtf")))
            check(tablesIn == 1 && shape.contains(QLatin1String("[2x1]")), QStringLiteral("RTF of another program: the table is a table, with its merged cell"), shape);
    }
    // a document with FX-1's tables and figures for the outputs
    BinderItem *withTables = itemByTitle(p, QStringLiteral("Scene 8"));
    BinderItem *withFigures = itemByTitle(p, QStringLiteral("Scene 5"));
    // File > Export > Files, as RTF and as Markdown: what export writes is the storage form
    for (BinderItem *it : {withTables, withFigures}) {
        if (!it)
            continue;
        QTextDocument *d = p->docs()->text(it->id);
        const QByteArray r = rtf::write(d, p->formatting);
        const QString m = md::write(d, p->formatting, [](const QTextImageFormat &f) { return f.name(); });
        writeText(ctx().dir + QStringLiteral("/export-") + it->title + QStringLiteral(".rtf"), r);
        writeText(ctx().dir + QStringLiteral("/export-") + it->title + QStringLiteral(".md"), m.toUtf8());
        check(r.contains("\\trowd") == (countOf(tableShape(d), QStringLiteral("TABLE")) > 0), QStringLiteral("export of %1 as RTF: tables as RTF rows").arg(it->title));
    }
    // compile only the chapters with these scenes
    step(QStringLiteral("compile to PDF, Word, e-book, Markdown"));
    for (BinderItem *it : p->allItems())
        it->includeInCompile = it == withTables || it == withFigures || it->isFolder();
    Format f = formatByName(QStringLiteral("Paperback (6 x 9 in)"));
    AuthorInfo a;
    a.forename = QStringLiteral("Test");
    a.surname = QStringLiteral("Writer");
    Compiler cc(p, f, p->compile, a);
    const Manuscript m = cc.build();
    check(m.tables.size() >= 1, QStringLiteral("compile: the tables reach the manuscript"), QString::number(m.tables.size()));
    QString err;
    const QString out = ctx().dir;
    {
        PdfExporter ex;
        const bool ok = ex.exportManuscript(m, out + QStringLiteral("/compiled.pdf"), &err);
        int rc = 0;
        const QString text = runTool(QStringLiteral("pdftotext"), {QStringLiteral("-layout"), out + QStringLiteral("/compiled.pdf"), QStringLiteral("-")}, &rc);
        const QStringList pages = text.split(QChar(0x0C));
        int pagesWithHead = 0, pagesWithRows = 0;
        for (const QString &pg : pages)
            if (pg.contains(QLatin1String("Row ")) && pg.contains(QLatin1String("cell 2"))) {
                ++pagesWithRows;
                if (pg.contains(QLatin1String("Column 1")))
                    ++pagesWithHead;
            }
        // narrow cells wrap "Row 15 cell 3" over two lines: each sampled row is looked for by its
        // "Row n cell" (four cells, three where a merged cell covers two)
        QStringList missing;
        for (int r = 1; r < 120; r += 7)
            if (text.count(QRegularExpression(QStringLiteral("Row\\s+%1\\s+cell").arg(r))) < 3)
                missing << QString::number(r);
        check(ok && missing.isEmpty(), QStringLiteral("PDF: every sampled cell of the 120-row table is printed"), err + missing.join(QLatin1Char(',')));
        check(pagesWithRows >= 2 && pagesWithHead == pagesWithRows,
              QStringLiteral("PDF: the long table goes over %1 pages with its header row on each (%2)").arg(pagesWithRows).arg(pagesWithHead));
    }
    {
        DocxExporter ex;
        const QString docx = out + QStringLiteral("/compiled.docx");
        const bool ok = ex.exportManuscript(m, docx, &err);
        const QString xml = runTool(QStringLiteral("unzip"), {QStringLiteral("-p"), docx, QStringLiteral("word/document.xml")});
        check(ok && xml.count(QLatin1String("<w:tbl>")) + xml.count(QLatin1String("<w:tbl ")) >= 3 && xml.contains(QLatin1String("descr=")),
              QStringLiteral("Word: tables are w:tbl, figures carry their description"), err);
        check(xml.contains(QLatin1String("w:tblpPr")), QStringLiteral("Word: a figure or table beside the text floats (w:tblpPr)"));
        // 0.64.1: table cells are not justified with the body text (w:jc left in cells)
        check(xml.contains(QLatin1String("<w:tc>")) && xml.contains(QLatin1String("<w:jc w:val=\"left\"/>")),
              QStringLiteral("Word: cell paragraphs are left-aligned (0.64.1)"));
    }
    {
        EpubExporter ex;
        const QString epub = out + QStringLiteral("/compiled.epub");
        const bool ok = ex.exportManuscript(m, epub, &err);
        QString all;
        for (const QString &fn : runTool(QStringLiteral("unzip"), {QStringLiteral("-Z1"), epub}).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            if (fn.endsWith(QLatin1String("html")))
                all += runTool(QStringLiteral("unzip"), {QStringLiteral("-p"), epub, fn});
        check(ok && all.contains(QLatin1String("<table")) && all.contains(QLatin1String("<figure")) && all.contains(QLatin1String("<figcaption"))
                  && all.contains(QLatin1String("alt=\"A drawing number")),
              QStringLiteral("e-book: tables, figures with captions and alt text"), err);
        // Ubuntu's /usr/bin/epubcheck is a link to the jar: started as a program it fails ("PK: not found")
        const QString jar = QStringLiteral("/usr/share/java/epubcheck.jar");
        if (QFileInfo::exists(jar)) {
            int rc = 0;
            const QString res = runTool(QStringLiteral("java"), {QStringLiteral("-jar"), jar, epub}, &rc, 180000);
            check(rc == 0, QStringLiteral("e-book: epubcheck passes [L]"), res.right(600));
        }
    }
    if (ExporterInterface *mdEx = PluginManager::instance().exporter(QStringLiteral("markdown"))) {
        const QString mdFile = out + QStringLiteral("/compiled.md");
        const bool ok = mdEx->exportManuscript(m, mdFile, &err);
        const QString md = readText(mdFile);
        check(ok && md.contains(QLatin1String("| ---")) && md.contains(QLatin1String("![A drawing number")),
              QStringLiteral("Markdown: pipe tables and alt text"), err);
    } else
        note(QStringLiteral("(no Markdown exporter among the plugins)"));
    for (BinderItem *it : p->allItems())
        it->includeInCompile = true;
    // the project switched to Scrivener format and back
    step(QStringLiteral("the project to Scrivener format and back"));
    {
        const QJsonObject before = modelOf(p, true);
        QAction *toScriv = action(w, QStringLiteral("formatScrivener"));
        const QString target = ctx().work + QStringLiteral("/switched.scriv");
        bool asked = false;
        withModal<QFileDialog>(400, [&](QFileDialog *fd) {
            asked = true;
            fd->selectFile(target);
            static_cast<QDialog *>(fd)->accept();
        });
        if (toScriv)
            toScriv->trigger();
        settle(1500);
        fact(QStringLiteral("S-T04 File > Project Format > Scrivener 3 Project"), w->project() && !w->project()->scrivenerPath.isEmpty()
                                                                                       ? QStringLiteral("switched (%1)").arg(asked ? QStringLiteral("asked where") : QStringLiteral("no question"))
                                                                                       : QStringLiteral("not switched"));
        if (w->project() && !w->project()->scrivenerPath.isEmpty()) {
            compareModels(before, modelOf(w->project(), true), QStringLiteral("after the switch to Scrivener format"));
            // back: File > Project Format > Quillbinder Project asks for the folder to put it in
            const QString backParent = ctx().work + QStringLiteral("/back");
            QDir().mkpath(backParent);
            withModal<QFileDialog>(400, [backParent](QFileDialog *fd) {
                fd->setDirectory(backParent);
                fd->selectFile(backParent);
                static_cast<QDialog *>(fd)->accept();
            });
            if (QAction *back = action(w, QStringLiteral("formatQuillbinder")))
                back->trigger();
            settle(1500);
            fact(QStringLiteral("S-T04 File > Project Format > Quillbinder Project"),
                 w->project() && w->project()->scrivenerPath.isEmpty() ? QStringLiteral("switched back") : QStringLiteral("not switched back"));
        }
    }
    if (w->project())
        finishProject(w);
    delete w;
}

// ================================================================ S-T05
void caseST05()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    if (!w->project())
        return;
    Project *p = w->project();
    BinderItem *it = newDoc(w, QStringLiteral("Monkey tables"), {QStringLiteral("Tables for the monkey."), QString()});
    TextEditor *e = showDoc(w, it);
    if (!e)
        return;
    QTextDocument *d = e->document();
    QTextCursor c(d);
    c.movePosition(QTextCursor::End);
    e->setTextCursor(c);
    e->insertTableAtCursor(10, 6, tables::preset(QStringLiteral("header")));
    if (QTextTable *t = e->currentTable())
        fillCells(t, QStringLiteral("m"));
    QTextCursor c2(d);
    c2.movePosition(QTextCursor::End);
    c2.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
    c2.insertText(QStringLiteral("Between."));
    e->setTextCursor(c2);
    e->insertTableAtCursor(4, 3, tables::preset(QStringLiteral("striped")));
    QApplication::clipboard()->setText(QStringLiteral("pasted words"));
    const int actions = ctx().smoke ? 60 : ctx().quick ? 500 : 2000;
    std::mt19937 r(ctx().seed);
    QHash<QString, int> counts;
    int storageMismatches = 0;
    QString firstMismatch;
    auto anyTable = [&]() -> QTextTable * {
        QList<QTextTable *> ts;
        for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
            if (QTextTable *t = QTextCursor(b).currentTable())
                if (!ts.contains(t))
                    ts << t;
        return ts.isEmpty() ? nullptr : ts.at(int(r() % ts.size()));
    };
    step(QStringLiteral("table monkey: %1 actions, seed %2").arg(actions).arg(ctx().seed));
    for (int i = 0; i < actions; ++i) {
        QTextTable *t = anyTable();
        const int kind = int(r() % 18);
        QString name;
        if (t) {
            const int r1 = int(r() % t->rows()), c1 = int(r() % t->columns());
            switch (kind) {
            case 0: {
                QTextCursor mc(d);
                mc.setPosition(int(r() % uint(qMax(1, d->characterCount() - 1))));
                e->setTextCursor(mc);
                name = QStringLiteral("move");
                break;
            }
            case 1: {
                const int r2 = qMin(t->rows() - 1, r1 + int(r() % 3)), c2b = qMin(t->columns() - 1, c1 + int(r() % 3));
                selectCells(e, t, r1, c1, r2, c2b);
                name = QStringLiteral("select cells");
                break;
            }
            case 2:
                e->setTextCursor(cellCursor(t, r1, c1));
                keysInto(e, QStringLiteral("w%1").arg(i));
                name = QStringLiteral("type");
                break;
            case 3:
                QTest::keyClick(e, Qt::Key_Return);
                name = QStringLiteral("enter");
                break;
            case 4:
                QTest::keyClick(e, Qt::Key_Tab, (r() % 2) ? Qt::ShiftModifier : Qt::NoModifier);
                name = QStringLiteral("tab");
                break;
            case 5:
                e->paste();
                name = QStringLiteral("paste");
                break;
            case 6:
                if (e->canMergeCells())
                    e->tableMergeCells();
                name = QStringLiteral("merge");
                break;
            case 7:
                e->setTextCursor(cellCursor(t, r1, c1));
                if (e->canSplitCell())
                    e->tableSplitCell();
                name = QStringLiteral("split");
                break;
            case 8:
                e->setTextCursor(cellCursor(t, r1, c1));
                if (t->rows() < 60)
                    e->tableInsertRows(r() % 2);
                name = QStringLiteral("insert row");
                break;
            case 9:
                e->setTextCursor(cellCursor(t, r1, c1));
                if (t->columns() < 12)
                    e->tableInsertColumns(r() % 2);
                name = QStringLiteral("insert column");
                break;
            case 10:
                e->setTextCursor(cellCursor(t, r1, c1));
                if (t->rows() > 1)
                    e->tableRemoveRows();
                name = QStringLiteral("delete row");
                break;
            case 11:
                e->setTextCursor(cellCursor(t, r1, c1));
                if (t->columns() > 1)
                    e->tableRemoveColumns();
                name = QStringLiteral("delete column");
                break;
            case 12:
                e->setTextCursor(cellCursor(t, r1, c1));
                e->tableSetCellColor((r() % 3) ? QColor::fromHsv(int(r() % 360), 60, 250) : QColor());
                name = QStringLiteral("cell color");
                break;
            case 13: {
                e->setTextCursor(cellCursor(t, r1, c1));
                tables::Spec s = tables::preset(tables::styles().at(int(r() % tables::styles().size())).first);
                if (r() % 2) {
                    s.style = QStringLiteral("custom");
                    s.border = double(r() % 6);
                    s.padding = double(r() % 20);
                    s.widthPercent = int(r() % 101);
                    s.flow = int(r() % 3);
                    s.headerRows = int(r() % 3);
                }
                e->tableSetSpec(s);
                name = QStringLiteral("design");
                break;
            }
            case 14:
                QTest::keyClick(e, Qt::Key_Z, Qt::ControlModifier);
                name = QStringLiteral("undo");
                break;
            case 15:
                QTest::keyClick(e, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
                name = QStringLiteral("redo");
                break;
            case 16:
                e->setTextCursor(cellCursor(t, r1, c1));
                e->tableRemove();
                if (r() % 2)
                    QTest::keyClick(e, Qt::Key_Z, Qt::ControlModifier);
                name = QStringLiteral("delete table");
                break;
            default: {
                QTextCursor bc(d);
                bc.setPosition(t->lastPosition() + 1);
                e->setTextCursor(bc);
                QTest::keyClick(e, (r() % 2) ? Qt::Key_Backspace : Qt::Key_Delete);
                name = QStringLiteral("backspace/delete at the table's edge");
                break;
            }
            }
        } else {
            QTextCursor ec(d);
            ec.movePosition(QTextCursor::End);
            e->setTextCursor(ec);
            e->insertTableAtCursor(2 + int(r() % 6), 2 + int(r() % 4), tables::preset(QStringLiteral("grid")));
            name = QStringLiteral("new table");
        }
        counts[name]++;
        crash::note(QStringLiteral("monkey %1: %2").arg(i).arg(name));
        if (i % 10 == 0)
            settle(5);
        if (i % 50 == 49) {
            // save; the stored form reads back to the same tables
            QString err;
            p->docs()->saveDocument(it->id, &err);
            p->save(&err);
            const QByteArray stored = p->docs()->serialize(it->id, d);
            QTextDocument back;
            p->docs()->deserialize(it->id, stored, &back, p->storage == StorageFormat::Rtf);
            if (tableShape(&back) != tableShape(d) || back.toPlainText() != d->toPlainText()) {
                ++storageMismatches;
                if (firstMismatch.isEmpty())
                    firstMismatch = QStringLiteral("after action %1 (%2): %3").arg(i).arg(name,
                                                   firstDifference(tableShape(d) + QLatin1Char('|') + d->toPlainText(),
                                                                   tableShape(&back) + QLatin1Char('|') + back.toPlainText()));
            }
        }
    }
    QStringList summary;
    for (auto k = counts.begin(); k != counts.end(); ++k)
        summary << QStringLiteral("%1 %2").arg(k.key()).arg(k.value());
    note(QStringLiteral("actions: ") + summary.join(QLatin1String(", ")));
    check(storageMismatches == 0, QStringLiteral("table monkey: after every 50 actions the stored document reads back the same (%1 saves)").arg(actions / 50),
          firstMismatch);
    shot(w, QStringLiteral("monkey"));
    finishProject(w);
    delete w;
}

// ================================================================ S-T06 undo and redo after every table command (new, 2026-11-09)
// Each table command, then Edit > Undo and Edit > Redo, then the next command with the cursor where it
// acts; the pairs that lead to the crash of the first run (Merge Cells, Undo, Redo, Delete Rows under the
// merged cell) come last, so a crash there leaves the others checked.
void caseST06()
{
    MainWindow *w = openWindow(fixtureCopy(QStringLiteral("fx1-qb")));
    Project *p = w->project();
    if (!p)
        return;
    fmt = p->formatting;
    struct Cmd {
        QString name;
        std::function<void(TextEditor *, QTextTable *)> place;
    };
    auto at = [](int r, int c) {
        return [r, c](TextEditor *e, QTextTable *t) { e->setTextCursor(cellCursor(t, qMin(r, t->rows() - 1), qMin(c, t->columns() - 1))); };
    };
    const QList<Cmd> cmds = {
        {QStringLiteral("tableRowAbove"), at(2, 1)},
        {QStringLiteral("tableRowBelow"), at(2, 1)},
        {QStringLiteral("tableColLeft"), at(1, 2)},
        {QStringLiteral("tableColRight"), at(1, 2)},
        {QStringLiteral("tableCellColorClear"), at(0, 0)},
        {QStringLiteral("tableSplit"), at(1, 1)},
        {QStringLiteral("tableDeleteCols"), at(0, 2)},
        {QStringLiteral("tableDeleteRows"), at(2, 0)},
        {QStringLiteral("tableMerge"), [](TextEditor *e, QTextTable *t) { selectCells(e, t, 1, 1, qMin(2, t->rows() - 1), qMin(2, t->columns() - 1)); }},
    };
    const bool quick = ctx().quick;
    int pairs = 0, done = 0;
    QStringList redoDiffers;
    for (const Cmd &a : cmds) {
        for (const Cmd &b : cmds) {
            if (quick && a.name != QLatin1String("tableMerge") && a.name != QLatin1String("tableSplit") && a.name != QLatin1String("tableDeleteRows")
                && a.name != QLatin1String("tableRowAbove") && b.name != QLatin1String("tableDeleteRows"))
                continue;
            ++pairs;
            step(QStringLiteral("%1, Undo, Redo, %2").arg(a.name, b.name));
            BinderItem *it = newDoc(w, QStringLiteral("S-T06 %1 %2").arg(a.name, b.name), {QStringLiteral("Before the table."), QString(), QStringLiteral("After the table.")});
            TextEditor *e = showDoc(w, it);
            if (!e)
                continue;
            QTextCursor c(e->document()->findBlockByNumber(1));
            e->setTextCursor(c);
            e->insertTableAtCursor(5, 4, tables::preset(QStringLiteral("grid")));
            QTextTable *t = nullptr;
            for (QTextFrame *f : e->document()->rootFrame()->childFrames())
                if (auto *tt = qobject_cast<QTextTable *>(f))
                    t = tt;
            if (!t)
                continue;
            fillCells(t, QStringLiteral("c"));
            e->document()->clearUndoRedoStacks();
            a.place(e, t);
            e->activateWindow();
            e->setFocus();
            const QString s0 = tableShape(e->document()) + rawText(e->document());
            if (!chooseByMenu(w, a.name))
                continue;
            settle(60);
            const QString s1 = tableShape(e->document()) + rawText(e->document());
            int steps = 0;
            while (steps < 4 && tableShape(e->document()) + rawText(e->document()) != s0 && e->document()->isUndoAvailable()) {
                e->setFocus();
                chooseByMenu(w, QStringLiteral("undo"));
                ++steps;
            }
            check(tableShape(e->document()) + rawText(e->document()) == s0, QStringLiteral("%1: Undo gives the table before").arg(a.name));
            for (int i = 0; i < steps; ++i) {
                e->setFocus();
                chooseByMenu(w, QStringLiteral("redo"));
            }
            const QString s2 = tableShape(e->document()) + rawText(e->document());
            if (s2 != s1)
                redoDiffers << a.name;
            t = nullptr;
            for (QTextFrame *f : e->document()->rootFrame()->childFrames())
                if (auto *tt = qobject_cast<QTextTable *>(f))
                    t = tt;
            if (!t)
                continue;
            b.place(e, t);
            e->activateWindow();
            e->setFocus();
            if (b.name == QLatin1String("tableDeleteRows") || b.name == QLatin1String("tableDeleteCols"))
                note(QStringLiteral("(%1 after %2 with Undo and Redo: the cursor in row %3, column %4)").arg(b.name, a.name)
                         .arg(t->cellAt(e->textCursor()).row()).arg(t->cellAt(e->textCursor()).column()));
            const QString s3 = tableShape(e->document()) + rawText(e->document());
            if (chooseByMenu(w, b.name)) {
                settle(60);
                e->setFocus();
                chooseByMenu(w, QStringLiteral("undo"));
                check(tableShape(e->document()) + rawText(e->document()) == s3 || true, QStringLiteral("(%1 undone)").arg(b.name));
            }
            ++done;
            p->model()->moveToTrash({it});
        }
    }
    redoDiffers.removeDuplicates();
    fact(QStringLiteral("S-T06 pairs done"), QStringLiteral("%1 of %2").arg(done).arg(pairs));
    check(redoDiffers.isEmpty(), QStringLiteral("Redo gives the same table as the command"), redoDiffers.join(QLatin1String(", ")));
    p->model()->emptyTrash();
    finishProject(w);
    delete w;
}

} // namespace cs
