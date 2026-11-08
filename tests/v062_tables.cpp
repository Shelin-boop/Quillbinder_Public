// qb_v062_tests, part B: tables and pictures.
#include "v062_common.h"
#include "text/MarkdownIO.h"
#include "text/RtfIO.h"
#include "text/Tables.h"

#include <QProcess>
#include <QTextFrame>
#include <QXmlStreamReader>
#include "compile/CompileFormat.h"
#include "compile/Compiler.h"
#include "compile/DocxWriter.h"
#include "compile/EpubExporter.h"
#include "compile/PdfExporter.h"
#include "import/ScrivenerFormat.h"

namespace {

// A description of the structure: paragraphs, tables (rows x columns, spans, cell texts, design),
// figures (position, caption, picture with its description).
QString shape(QTextDocument *d)
{
    QStringList out;
    for (QTextBlock b = d->begin(); b.isValid();) {
        QTextCursor c(b);
        if (QTextTable *t = c.currentTable()) {
            const tables::Spec s = tables::specOf(t);
            QStringList cells;
            for (int r = 0; r < t->rows(); ++r)
                for (int col = 0; col < t->columns(); ++col) {
                    const QTextTableCell cell = t->cellAt(r, col);
                    if (cell.row() != r || cell.column() != col)
                        continue;
                    QStringList texts;
                    for (QTextFrame::iterator it = cell.begin(); !it.atEnd(); ++it)
                        if (it.currentBlock().isValid())
                            texts << it.currentBlock().text();
                    QString one = texts.join(QLatin1Char('/'));
                    if (cell.rowSpan() > 1 || cell.columnSpan() > 1)
                        one += QStringLiteral("[%1x%2]").arg(cell.rowSpan()).arg(cell.columnSpan());
                    const QColor bg = cell.format().property(props::CellBackground).value<QColor>();
                    if (bg.isValid())
                        one += QLatin1Char('{') + bg.name() + QLatin1Char('}');
                    cells << one;
                }
            out << QStringLiteral("TABLE %1x%2 %3 h%4 b%5 w%6 f%7: %8")
                       .arg(t->rows())
                       .arg(t->columns())
                       .arg(s.style)
                       .arg(s.headerRows)
                       .arg(s.border)
                       .arg(s.widthPercent)
                       .arg(s.flow)
                       .arg(cells.join(QLatin1String(" | ")));
            b = d->findBlock(t->lastPosition() + 1);
            continue;
        }
        if (QTextFrame *fr = figures::at(d, b.position())) {
            const int ip = figures::imagePosition(fr);
            QString desc;
            if (ip >= 0) {
                QTextCursor ic(d);
                ic.setPosition(ip);
                ic.setPosition(ip + 1, QTextCursor::KeepAnchor);
                desc = figures::description(ic.charFormat());
            }
            out << QStringLiteral("FIGURE pos%1 caption='%2' desc='%3'").arg(int(figures::positionOf(fr))).arg(figures::caption(fr), desc);
            b = d->findBlock(fr->lastPosition() + 1);
            continue;
        }
        out << QStringLiteral("P '%1'").arg(b.text());
        b = b.next();
    }
    return out.join(QLatin1Char('\n'));
}

QImage picture(int w, int h)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(QColor(0x40, 0x80, 0xc0));
    return img;
}

void build(QTextDocument *d, const Formatting &f, const QString &imageName = QStringLiteral("media/lamp.png"))
{
    d->addResource(QTextDocument::ImageResource, QUrl(imageName), picture(160, 120));
    QTextCursor c(d);
    c.insertText(QStringLiteral("Before the table."));
    c.insertBlock(display::normalBlockFormat(f), QTextCharFormat());
    QTextBlockFormat cell = display::blockFormatFor(f, QString());
    cell.setTextIndent(0);
    QTextTable *t = tables::insert(c, 3, 3, tables::preset(QStringLiteral("header")), cell, display::charFormatFor(f, QString()));
    const QStringList texts = {QStringLiteral("Name"), QStringLiteral("Place"), QStringLiteral("Age"),
                               QStringLiteral("Ada"),  QStringLiteral("Harbor | Pier"), QStringLiteral("31"),
                               QStringLiteral("Ben"),  QStringLiteral("Hill"), QStringLiteral("44")};
    for (int i = 0; i < 9; ++i) {
        QTextCursor cc = t->cellAt(i / 3, i % 3).firstCursorPosition();
        cc.insertText(texts[i]);
    }
    {
        QTextCursor cc = t->cellAt(2, 0).lastCursorPosition();
        cc.insertBlock();
        cc.insertText(QStringLiteral("second line"));
    }
    t->mergeCells(1, 2, 2, 1);
    tables::setCellBackground(t, 2, 1, QColor(QStringLiteral("#ffeeaa")));
    QTextCursor after(d);
    after.setPosition(t->lastPosition() + 1);
    after.insertText(QStringLiteral("After the table, a picture follows."));
    after.insertBlock(display::normalBlockFormat(f), QTextCharFormat());
    after.insertText(QStringLiteral("The lamp stands here: "));
    const int imgPos = after.position();
    QTextImageFormat imf;
    imf.setName(imageName);
    imf.setWidth(160);
    imf.setHeight(120);
    after.insertImage(imf);
    after.insertText(QStringLiteral(" and the text goes on beside it for a while, long enough to flow around."));
    figures::setDescription(d, imgPos, QStringLiteral("A brass lamp [lit] on a table"));
    figures::setLayout(d, imgPos, figures::Left, QStringLiteral("The old lamp"), f);
    QTextCursor end(d);
    end.movePosition(QTextCursor::End);
    end.insertBlock(display::normalBlockFormat(f), QTextCharFormat());
    end.insertText(QStringLiteral("The end."));
}

} // namespace

static void editorChecks();
static void compileChecks();

void tableChecks()
{
    Formatting f;
    // ================================================================ the design
    {
        QTextDocument d;
        QTextCursor c(&d);
        QTextTable *t = tables::insert(c, 4, 2, tables::preset(QStringLiteral("striped")), QTextBlockFormat(), QTextCharFormat());
        const QBrush h = t->cellAt(0, 0).format().background();
        const QBrush odd = t->cellAt(2, 0).format().background();
        const QBrush even = t->cellAt(1, 0).format().background();
        check(t->format().headerRowCount() == 1 && h.style() != Qt::NoBrush && odd.style() != Qt::NoBrush && even.style() == Qt::NoBrush,
              QStringLiteral("table styles: striped rows with a header row"));
        t->insertRows(1, 1);
        tables::apply(t, tables::specOf(t));
        check(t->cellAt(2, 0).format().background().style() != Qt::NoBrush && t->cellAt(3, 0).format().background().style() == Qt::NoBrush,
              QStringLiteral("table styles: a row put in keeps the stripes right"));
        tables::Spec lines = tables::preset(QStringLiteral("lines"));
        tables::apply(t, lines);
        const QTextTableCellFormat cf = t->cellAt(1, 1).format().toTableCellFormat();
        check(cf.leftBorderStyle() == QTextFrameFormat::BorderStyle_None && cf.topBorder() > 0,
              QStringLiteral("table styles: lines between the rows only"));
        lines.flow = 2;
        lines.widthPercent = 40;
        tables::apply(t, lines);
        check(t->format().position() == QTextFrameFormat::FloatRight && qRound(t->format().width().rawValue()) == 40,
              QStringLiteral("table: text can flow beside a narrower table"));
    }

    // ================================================================ storage round trips
    QTextDocument d;
    build(&d, f);
    const QString want = shape(&d);
    note(want);
    check(want.contains(QLatin1String("TABLE 3x3 header h1")) && want.contains(QLatin1String("[2x1]"))
              && want.contains(QLatin1String("{#ffeeaa}")) && want.contains(QLatin1String("FIGURE pos1 caption='The old lamp'")),
          QStringLiteral("(a document with a table and a figure)"));
    {
        const QString md = md::write(&d, f, [](const QTextImageFormat &imf) { return imf.name(); });
        QTextDocument back;
        back.addResource(QTextDocument::ImageResource, QUrl(QStringLiteral("media/lamp.png")), picture(160, 120));
        md::read(md, &back, f);
        const QString got = shape(&back);
        check(got == want, QStringLiteral("Markdown storage: table, merged cells, cell color, figure, caption and description survive"),
              got + QLatin1String("\n---\n") + md);
        check(md.contains(QLatin1String(":::table")) && md.contains(QLatin1String(":::figure")) && md.contains(QLatin1String("![A brass lamp")),
              QStringLiteral("Markdown storage: the table and the figure are sections, the description is the alt text"));
        const QString md2 = md::write(&back, f, [](const QTextImageFormat &imf) { return imf.name(); });
        check(md2 == md, QStringLiteral("Markdown storage: written again, the same file"), md + QLatin1String("\n===\n") + md2);
    }
    {
        const QByteArray rtf = rtf::write(&d, f);
        QTextDocument back;
        rtf::ReadOptions o;
        rtf::read(rtf, &back, f, o);
        QString got = shape(&back);
        // pictures read from RTF get new names; the shape does not name them
        check(got == want, QStringLiteral("RTF storage: table, merged cells, cell color, figure, caption and description survive"),
              got);
        check(rtf.contains("\\trowd") && rtf.contains("\\cellx") && rtf.contains("\\clvmgf") && rtf.contains("\\row"),
              QStringLiteral("RTF storage: the table is written as RTF rows other programs read"));
        // the same RTF without this program's marks: what Word or Scrivener give back
        QByteArray foreign = rtf;
        for (const char *mark : {"qbtable", "qbfigure", "qbfigureend", "qbalt"}) {
            const QByteArray open = QByteArray("{\\*\\") + mark;
            for (int at = foreign.indexOf(open); at >= 0; at = foreign.indexOf(open)) {
                int depth = 0, k = at;
                for (; k < foreign.size(); ++k) {
                    if (foreign[k] == '{' && (k == 0 || foreign[k - 1] != '\\'))
                        ++depth;
                    else if (foreign[k] == '}' && foreign[k - 1] != '\\' && --depth == 0)
                        break;
                }
                foreign.remove(at, k - at + 1);
            }
        }
        QTextDocument plain;
        rtf::read(foreign, &plain, f, o);
        const QString got2 = shape(&plain);
        check(got2.contains(QLatin1String("TABLE 3x3 custom h1")) && got2.contains(QLatin1String("[2x1]"))
                  && got2.contains(QLatin1String("P 'The old lamp'")),
              QStringLiteral("RTF of other programs: rows, header row and merged cells become a table; a figure's caption stays text"),
              got2);
    }
    editorChecks();
    compileChecks();
}

// ================================================================ in the editor and in projects
static void editorChecks()
{
    QTemporaryDir work;
    for (const bool rtfStorage : {false, true}) {
        auto proj = blankProject(work.path(), rtfStorage ? QStringLiteral("Tables RTF") : QStringLiteral("Tables MD"));
        if (!proj)
            return;
        if (rtfStorage)
            proj->storage = StorageFormat::Rtf;
        BinderItem *doc = textDoc(proj.get(), proj->draft(), QStringLiteral("With a Table"), {QStringLiteral("First paragraph."), QString()});
        const QString docId = doc->id;
        QString err;
        proj->save(&err);
        const QString file = proj->indexPath();
        auto *wp = new MainWindow;
        MainWindow &w = *wp;
        w.resize(1300, 900);
        w.show();
        w.activateWindow();
        (void)QTest::qWaitForWindowActive(&w, 2000);
        w.adoptProject(std::move(proj));
        settle(500);
        EditorPane *pane = visiblePane(w);
        if (pane) {
            pane->showItems({doc}, false, int(ViewMode::Single));
            settle(300);
        }
        TextEditor *e = pane ? pane->currentEditor() : nullptr;
        if (!e) {
            check(false, QStringLiteral("(an editor)"));
            delete wp;
            return;
        }
        e->setFocus();
        QTextCursor c(e->document());
        c.movePosition(QTextCursor::End);
        e->setTextCursor(c);
        const QString tag = rtfStorage ? QStringLiteral("RTF project: ") : QStringLiteral("Markdown project: ");
        QAction *ins = w.findChild<QAction *>(QStringLiteral("insertTable"));
        w.updateFormatBar();
        check(ins && ins->isEnabled(), tag + QStringLiteral("Insert > Table is on in the text"));
        check(e->insertTableAtCursor(2, 3, tables::preset(QStringLiteral("striped"))) && e->currentTable(),
              tag + QStringLiteral("a table of 2 rows and 3 columns is put in"));
        w.updateFormatBar();
        check(!ins->isEnabled() && w.findChild<QAction *>(QStringLiteral("tableRowBelow"))->isEnabled(),
              tag + QStringLiteral("in the table: the table commands are on, Insert > Table is off"));
        QTest::keyClicks(e, QStringLiteral("Name"));
        QTest::keyClick(e, Qt::Key_Tab);
        QTest::keyClicks(e, QStringLiteral("Age"));
        QTextTable *t = e->currentTable();
        check(t && t->cellAt(0, 0).firstCursorPosition().block().text() == QLatin1String("Name")
                  && t->cellAt(0, 1).firstCursorPosition().block().text() == QLatin1String("Age"),
              tag + QStringLiteral("Tab goes to the next cell"));
        // Tab in the last cell adds a row
        e->setTextCursor(t->cellAt(1, 2).firstCursorPosition());
        QTest::keyClick(e, Qt::Key_Tab);
        check(t->rows() == 3, tag + QStringLiteral("Tab in the last cell adds a row"), QString::number(t->rows()));
        e->setTextCursor(t->cellAt(0, 0).firstCursorPosition());
        e->tableInsertColumns(true);
        check(t->columns() == 4, tag + QStringLiteral("Insert Column Right"));
        QTextCursor sel = t->cellAt(1, 0).firstCursorPosition();
        sel.setPosition(t->cellAt(1, 1).lastCursorPosition().position(), QTextCursor::KeepAnchor);
        e->setTextCursor(sel);
        check(e->canMergeCells(), tag + QStringLiteral("two cells selected: Merge Cells is on"));
        e->tableMergeCells();
        check(t->cellAt(1, 0).columnSpan() == 2 && e->canSplitCell(), tag + QStringLiteral("Merge Cells joins them; Split Cell is on"));
        e->tableSetCellColor(QColor(QStringLiteral("#ccffcc")));
        e->tableRemoveColumns();
        check(t->columns() == 2, tag + QStringLiteral("Delete Columns takes the columns of the selection"), QString::number(t->columns()));
        tables::Spec sp = tables::specOf(t);
        sp.border = 2;
        sp.flow = 1;
        sp.widthPercent = 50;
        e->tableSetSpec(sp);
        // a picture beside the text
        QTextCursor pc(e->document());
        pc.setPosition(t->lastPosition() + 1);
        pc.insertText(QStringLiteral("Lamp: "));
        e->document()->addResource(QTextDocument::ImageResource, QUrl(QStringLiteral("test-lamp.png")), picture(120, 90));
        const QString rel = w.project()->docs()->importImage(doc->id, picture(120, 90));
        e->document()->addResource(QTextDocument::ImageResource, QUrl(rel), picture(120, 90));
        QTextImageFormat imf;
        imf.setName(rel);
        const int ip = pc.position();
        pc.insertImage(imf);
        pc.insertText(QStringLiteral(" text that flows beside the picture."));
        const int at = e->setPicture(ip, 100, figures::Right, QStringLiteral("A lamp"), QStringLiteral("A small blue lamp"));
        QTextFrame *fig = figures::at(e->document(), at);
        check(fig && figures::positionOf(fig) == figures::Right && figures::caption(fig) == QLatin1String("A lamp"),
              tag + QStringLiteral("Picture: right, with a caption"));
        e->setTextCursor(QTextCursor(e->document()->findBlock(at)));
        w.updateFormatBar();
        check(w.findChild<QAction *>(QStringLiteral("picture"))->isEnabled(), tag + QStringLiteral("Format > Picture is on at the picture"));
        shot(&w, rtfStorage ? QStringLiteral("v062-editor-rtf") : QStringLiteral("v062-editor-md"));
        const QString before = shape(e->document());
        w.project()->save(&err);
        check(Project::neededVersion(file) == QLatin1String("0.62.0"),
              tag + QStringLiteral("a project with tables names 0.62.0 as the version it needs (older versions ask first)"),
              Project::neededVersion(file));
        w.closeProject();
        settle(200);
        auto again = Project::open(file, &err);
        QString after;
        if (again) {
            after = shape(again->docs()->text(docId));
            // in Scrivener format as well
            if (!rtfStorage) {
                const QString scriv = work.path() + QStringLiteral("/Tables.scriv");
                auto wc = scrivener::convertToScrivener(again.get(), scriv, &err);
                const QString uuid = [&] {
                    QFile f(scrivener::scrivxIn(scriv));
                    const QString x = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
                    const int ti = int(x.indexOf(QStringLiteral("<Title>With a Table</Title>")));
                    const int b = ti < 0 ? -1 : int(x.lastIndexOf(QStringLiteral("UUID=\""), ti));
                    return b < 0 ? QString() : x.mid(b + 6, 36);
                }();
                QFile content(scriv + QStringLiteral("/Files/Data/") + uuid + QStringLiteral("/content.rtf"));
                const QByteArray rtf = content.open(QIODevice::ReadOnly) ? content.readAll() : QByteArray();
                check(wc && rtf.contains("\\trowd") && rtf.contains("\\cellx"), QStringLiteral("Scrivener format: the table is in the .scriv as RTF rows"),
                      err);
                wc.reset();
                QDir(scrivener::workingCopyFor(scriv)).removeRecursively();
                scrivener::OpenResult opened = scrivener::open(scriv, &err);
                QString fromScriv;
                if (opened.project)
                    for (BinderItem *it : opened.project->allItems())
                        if (it->title == QLatin1String("With a Table"))
                            fromScriv = shape(opened.project->docs()->text(it->id));
                check(fromScriv == before, QStringLiteral("Scrivener format: read anew, the table and the figure are back"),
                      fromScriv + QLatin1String("\n---\n") + before);
            }
        }
        check(again && after == before, tag + QStringLiteral("saved and opened again: table and figure as they were"),
              after + QLatin1String("\n---\n") + before);
        note(before);
        w.close();
        delete wp;
        settle(100);
    }
}

// ================================================================ compile: PDF, Word, e-book, Markdown
namespace {
QString runTool(const QString &program, const QStringList &args, int *rc = nullptr, int ms = 120000)
{
    QProcess pr;
    pr.start(program, args);
    if (!pr.waitForFinished(ms)) {
        pr.kill();
        pr.waitForFinished(2000);
    }
    if (rc)
        *rc = pr.exitStatus() == QProcess::NormalExit ? pr.exitCode() : -1;
    return QString::fromUtf8(pr.readAllStandardOutput());
}
bool wellFormed(const QString &xml, QString *why)
{
    QXmlStreamReader r(xml);
    while (!r.atEnd())
        r.readNext();
    if (r.hasError() && why)
        *why = r.errorString() + QStringLiteral(" at line %1").arg(r.lineNumber());
    return !r.hasError();
}
} // namespace

static void compileChecks()
{
    using namespace qb::compile;
    QTemporaryDir work;
    auto p = blankProject(work.path(), QStringLiteral("Compiled Tables"));
    if (!p) {
        check(false, QStringLiteral("compile: project made"));
        return;
    }
    QString err;
    BinderItem *it = p->model()->createItem(p->draft(), -1, ItemType::Text, QStringLiteral("Tables and Pictures"));
    QTextDocument *d = p->docs()->text(it->id);
    const QString rel = p->docs()->importImage(it->id, picture(160, 120));
    build(d, p->formatting, rel);
    // a long table: the header row comes again on the next page
    {
        QTextCursor c(d);
        c.movePosition(QTextCursor::End);
        c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
        c.insertText(QStringLiteral("A long table follows."));
        c.insertBlock(display::normalBlockFormat(p->formatting), QTextCharFormat());
        QTextBlockFormat cell = display::blockFormatFor(p->formatting, QString());
        cell.setTextIndent(0);
        tables::Spec sp = tables::preset(QStringLiteral("striped"));
        QTextTable *t = tables::insert(c, 61, 3, sp, cell, display::charFormatFor(p->formatting, QString()));
        for (int r = 0; r < 61; ++r)
            for (int col = 0; col < 3; ++col)
                t->cellAt(r, col).firstCursorPosition().insertText(
                    r == 0 ? QStringLiteral("Col %1").arg(QChar(u'A' + col)) : QStringLiteral("Row %1 cell %2").arg(r).arg(col + 1));
        QTextCursor end(d);
        end.movePosition(QTextCursor::End);
        end.insertText(QStringLiteral("Closing words."));
    }
    p->docs()->saveDocument(it->id, &err);
    p->save(&err);

    Format f = formatByName(QStringLiteral("Paperback (6 x 9 in)"));
    AuthorInfo a;
    a.forename = QStringLiteral("Ann");
    a.surname = QStringLiteral("Keeper");
    Compiler cc(p.get(), f, p->compile, a);
    const Manuscript m = cc.build();
    int cellParas = 0, figureAt = -1;
    QString desc;
    for (int i = 0; i < m.paragraphs.size(); ++i) {
        const Paragraph &q = m.paragraphs.at(i);
        if (q.table == 0)
            ++cellParas;
        if (q.figure == 1 && figureAt < 0) {
            figureAt = i;
            for (const Run &r : q.runs)
                if (r.kind == Run::Image)
                    desc = r.imageDescription;
        }
    }
    QString cellBg;
    if (!m.tables.isEmpty())
        for (const TableCell &tc : m.tables.first().cells)
            if (tc.background.isValid())
                cellBg += tc.background.name() + QLatin1Char(' ');
    check(m.tables.size() == 2 && m.tables.first().rows == 3 && m.tables.first().cells.size() == 8 && m.tables.first().headerRows == 1
              && cellParas == 10,
          QStringLiteral("compile: two tables, the first 3 x 3 with a merged cell (8 cells, 10 paragraphs) and a header row"),
          QStringLiteral("tables %1, cells %2, paragraphs %3").arg(m.tables.size()).arg(m.tables.value(0).cells.size()).arg(cellParas));
    check(cellBg.contains(QLatin1String("#ffeeaa")), QStringLiteral("compile: a cell's own color and the design's colors come along"), cellBg);
    check(figureAt >= 0 && figureAt + 1 < m.paragraphs.size() && m.paragraphs.at(figureAt + 1).figureCaption
              && m.paragraphs.at(figureAt + 1).plainText() == QLatin1String("The old lamp"),
          QStringLiteral("compile: the picture beside the text with its caption"));
    check(desc == QLatin1String("A brass lamp [lit] on a table"), QStringLiteral("compile: the picture's description comes along"), desc);

    // PDF
    {
        const QString pdf = work.path() + QStringLiteral("/out.pdf");
        PdfExporter ex;
        const bool ok = ex.exportManuscript(m, pdf, &err);
        int rc = 0;
        const QString text = runTool(QStringLiteral("pdftotext"), {QStringLiteral("-layout"), pdf, QStringLiteral("-")}, &rc);
        const QStringList pages = text.split(QLatin1Char('\f'));
        int withHeader = 0, firstLong = -1, lastLong = -1;
        for (int i = 0; i < pages.size(); ++i) {
            if (pages[i].contains(QLatin1String("Col A")) && pages[i].contains(QLatin1String("Row ")))
                ++withHeader;
            if (pages[i].contains(QLatin1String("Row 1 cell 1")) && firstLong < 0)
                firstLong = i;
            if (pages[i].contains(QLatin1String("Row 60 cell 3")))
                lastLong = i;
        }
        check(ok && text.contains(QLatin1String("Harbor | Pier")) && text.contains(QLatin1String("second line"))
                  && text.contains(QLatin1String("The old lamp")) && text.contains(QLatin1String("Closing words.")),
              QStringLiteral("PDF: the cells, the caption and the text after the tables are printed"), err + text.left(1500));
        check(lastLong > firstLong && withHeader >= 2, QStringLiteral("PDF: a long table goes on over pages, its header row on each"),
              QStringLiteral("first %1 last %2 pages with header %3").arg(firstLong).arg(lastLong).arg(withHeader));
        const QString imgs = runTool(QStringLiteral("pdfimages"), {QStringLiteral("-list"), pdf});
        check(imgs.split(QLatin1Char('\n'), Qt::SkipEmptyParts).size() >= 3, QStringLiteral("PDF: the picture is in it"), imgs);
        // the text beside the picture: on the same lines as the picture's area
        bool beside = false;
        for (const QString &line : text.split(QLatin1Char('\n')))
            if (line.contains(QLatin1String("flow around")) || line.contains(QLatin1String("beside it")))
                beside = true;
        check(beside, QStringLiteral("PDF: the paragraph with the picture is printed"));
        runTool(QStringLiteral("pdftoppm"), {QStringLiteral("-png"), QStringLiteral("-r"), QStringLiteral("60"), QStringLiteral("-f"),
                                             QStringLiteral("1"), QStringLiteral("-l"), QStringLiteral("3"), pdf, outDir + QStringLiteral("/v062-pdf")});
    }
    // Word
    QString docxFile = work.path() + QStringLiteral("/out.docx");
    {
        DocxExporter ex;
        const bool ok = ex.exportManuscript(m, docxFile, &err);
        const QString xml = runTool(QStringLiteral("unzip"), {QStringLiteral("-p"), docxFile, QStringLiteral("word/document.xml")});
        QString why;
        check(ok && wellFormed(xml, &why), QStringLiteral("Word: the document is well-formed XML"), err + why);
        check(xml.count(QLatin1String("<w:tbl>")) >= 3 && xml.contains(QLatin1String("<w:tblHeader/>"))
                  && xml.contains(QLatin1String("<w:vMerge w:val=\"restart\"/>")) && xml.contains(QLatin1String("w:fill=\"FFEEAA\"")),
              QStringLiteral("Word: tables with header rows, merged cells and cell colors"));
        check(xml.contains(QLatin1String("w:tblpXSpec=\"left\"")) && xml.contains(QLatin1String("descr=\"A brass lamp [lit] on a table\"")),
              QStringLiteral("Word: the picture floats left with its caption, its description is the alt text"));
    }
    // e-book
    {
        const QString epub = work.path() + QStringLiteral("/out.epub");
        EpubExporter ex;
        const bool ok = ex.exportManuscript(m, epub, &err);
        const QStringList files = runTool(QStringLiteral("unzip"), {QStringLiteral("-Z1"), epub}).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QString all, why;
        bool formed = true;
        for (const QString &fn : files)
            if (fn.endsWith(QLatin1String(".xhtml"))) {
                const QString x = runTool(QStringLiteral("unzip"), {QStringLiteral("-p"), epub, fn});
                formed = formed && wellFormed(x, &why);
                all += x;
            }
        check(ok && formed && all.contains(QLatin1String("<table class=\"qbtable\"")) && all.contains(QLatin1String("<thead>"))
                  && all.contains(QLatin1String("rowspan=\"2\"")) && all.contains(QLatin1String("Harbor | Pier")),
              QStringLiteral("e-book: tables with header rows and merged cells, well-formed"), err + why);
        check(all.contains(QLatin1String("figure class=\"qbfigure left\"")) && all.contains(QLatin1String("<figcaption>"))
                  && all.contains(QLatin1String("alt=\"A brass lamp [lit] on a table\"")),
              QStringLiteral("e-book: the picture is a figure on the left with its caption and alt text"));
    }
    // Markdown (the plugin)
    {
        ExporterInterface *md = compile::PluginManager::instance().exporter(QStringLiteral("markdown"));
        const QString mdFile = work.path() + QStringLiteral("/out.md");
        if (!md)
            err = QStringLiteral("no Markdown exporter among the plugins");
        const bool ok = md && md->exportManuscript(m, mdFile, &err);
        QFile mf(mdFile);
        const QString text = ok && mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll()) : QString();
        check(text.contains(QLatin1String("| Name | Place | Age |")) && text.contains(QLatin1String("Harbor \\| Pier"))
                  && text.contains(QLatin1String("| --- | --- | --- |")),
              QStringLiteral("Markdown: tables as pipe tables"), err + text.left(800));
        check(text.contains(QLatin1String("![A brass lamp \\[lit\\] on a table](")) && text.contains(QLatin1String("*The old lamp*")),
              QStringLiteral("Markdown: the picture with its description and caption"));
    }
    // LibreOffice reads the Word file (when installed)
    if (QFile::exists(QStringLiteral("/usr/bin/soffice")) && qEnvironmentVariableIsEmpty("QB_SKIP_SOFFICE")) {
        QTemporaryDir lo;
        int rc = 0;
        runTool(QStringLiteral("soffice"),
                {QStringLiteral("-env:UserInstallation=file://") + lo.path() + QStringLiteral("/profile"), QStringLiteral("--headless"),
                 QStringLiteral("--convert-to"), QStringLiteral("pdf"), QStringLiteral("--outdir"), lo.path(), docxFile},
                &rc, 180000);
        const QString lopdf = lo.path() + QStringLiteral("/out.pdf");
        const QString text = runTool(QStringLiteral("pdftotext"), {QStringLiteral("-layout"), lopdf, QStringLiteral("-")});
        check(text.contains(QLatin1String("Harbor | Pier")) && text.contains(QLatin1String("The old lamp"))
                  && text.contains(QLatin1String("Row 60 cell 3")),
              QStringLiteral("Word: LibreOffice opens the file and shows the tables and the caption"), text.left(600));
        runTool(QStringLiteral("pdftoppm"), {QStringLiteral("-png"), QStringLiteral("-r"), QStringLiteral("60"), QStringLiteral("-f"),
                                             QStringLiteral("1"), QStringLiteral("-l"), QStringLiteral("2"), lopdf, outDir + QStringLiteral("/v062-docx")});
    }
}
