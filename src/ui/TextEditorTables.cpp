// Tables and pictures in the editor (0.62); see TextEditor.h and text/Tables.h.
#include "ui/TextEditor.h"
#include "text/DisplayFormat.h"
#include "ui/TableDialogs.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFrame>
#include <QTextTable>

namespace qb {

QTextTable *TextEditor::currentTable() const { return textCursor().currentTable(); }

QTextBlockFormat TextEditor::cellBlockFormat() const
{
    const Formatting f = m_project ? m_project->formatting : Formatting();
    QTextBlockFormat bf = display::blockFormatFor(f, QString());
    bf.setTextIndent(0);
    bf.setTopMargin(0);
    bf.setBottomMargin(2);
    return bf;
}

bool TextEditor::insertTableAtCursor(int rows, int columns, const tables::Spec &spec)
{
    if (isReadOnly() || currentTable())
        return false; // no tables inside a cell
    QTextCursor c = textCursor();
    c.clearSelection();
    const Formatting f = m_project ? m_project->formatting : Formatting();
    tables::insert(c, rows, columns, spec, cellBlockFormat(), display::charFormatFor(f, QString()));
    setTextCursor(c);
    return true;
}

namespace {
// rows (or columns) the cursor's selection covers in the table
void selectedRange(const QTextCursor &c, QTextTable *t, int *r0, int *rn, int *c0, int *cn)
{
    if (c.hasComplexSelection()) {
        c.selectedTableCells(r0, rn, c0, cn);
        return;
    }
    const QTextTableCell a = t->cellAt(c.selectionStart()), b = t->cellAt(c.selectionEnd());
    // merged cells count with all the rows and columns they cover
    *r0 = qMin(a.row(), b.row());
    *rn = qMax(a.row() + a.rowSpan(), b.row() + b.rowSpan()) - *r0;
    *c0 = qMin(a.column(), b.column());
    *cn = qMax(a.column() + a.columnSpan(), b.column() + b.columnSpan()) - *c0;
}
// Merged cells that reach into rows (columns) about to go are split first: Qt 6.4's layout
// crashed after removing a column a merged cell covered.
void splitMergedIn(QTextTable *t, int r0, int rn, int c0, int cn)
{
    for (int r = 0; r < t->rows(); ++r)
        for (int c = 0; c < t->columns(); ++c) {
            const QTextTableCell cell = t->cellAt(r, c);
            if (cell.row() != r || cell.column() != c || (cell.rowSpan() == 1 && cell.columnSpan() == 1))
                continue;
            const bool rowsMeet = r < r0 + rn && r + cell.rowSpan() > r0;
            const bool colsMeet = c < c0 + cn && c + cell.columnSpan() > c0;
            if (rowsMeet && colsMeet)
                t->splitCell(r, c, 1, 1);
        }
}
// Qt's own test before a merge: no cell may reach over the edge of the rectangle
bool mergeable(QTextTable *t, int r0, int rn, int c0, int cn)
{
    if (r0 < 0 || c0 < 0 || rn < 1 || cn < 1 || r0 + rn > t->rows() || c0 + cn > t->columns() || rn * cn < 2)
        return false;
    for (int r = r0; r < r0 + rn; ++r) {
        if (c0 > 0 && t->cellAt(r, c0) == t->cellAt(r, c0 - 1))
            return false;
        if (c0 + cn < t->columns() && t->cellAt(r, c0 + cn) == t->cellAt(r, c0 + cn - 1))
            return false;
    }
    for (int c = c0; c < c0 + cn; ++c) {
        if (r0 > 0 && t->cellAt(r0, c) == t->cellAt(r0 - 1, c))
            return false;
        if (r0 + rn < t->rows() && t->cellAt(r0 + rn, c) == t->cellAt(r0 + rn - 1, c))
            return false;
    }
    return true;
}

// 0.64.1: the text of the cells a merge covers goes into the first cell first, with ordinary cursor
// steps. Qt 6.4's QTextTable::mergeCells moves it in a way its redo gets wrong (Edit > Redo after a
// merge lost text, and the damaged table crashed later commands; the system test's S-T01); a merge of
// empty cells only removes their marks and sets the span, which undo and redo handle well.
void moveTextIntoFirstCell(QTextTable *t, int r0, int rn, int c0, int cn)
{
    for (int r = r0; r < r0 + rn; ++r)
        for (int c = c0; c < c0 + cn; ++c) {
            const QTextTableCell cell = t->cellAt(r, c);
            if ((r == r0 && c == c0) || cell.row() != r || cell.column() != c)
                continue; // the first cell itself, or a place a merged cell covers
            QTextCursor sel = cell.firstCursorPosition();
            sel.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
            if (!sel.hasSelection())
                continue;
            const QTextDocumentFragment text = sel.selection();
            sel.removeSelectedText();
            const QTextTableCell first = t->cellAt(r0, c0);
            QTextCursor to = first.lastCursorPosition();
            if (first.firstCursorPosition().position() != first.lastCursorPosition().position())
                to.insertBlock(); // after the text already there, in a paragraph of its own
            to.insertFragment(text);
        }
}
} // namespace

void TextEditor::tableInsertRows(bool below)
{
    QTextTable *t = currentTable();
    if (!t)
        return;
    const QTextTableCell cell = t->cellAt(textCursor());
    QTextCursor c = textCursor();
    c.beginEditBlock();
    const int at = below ? cell.row() + cell.rowSpan() : cell.row();
    t->insertRows(at, 1);
    for (int col = 0; col < t->columns(); ++col) {
        const QTextTableCell nc = t->cellAt(at, col);
        if (nc.row() == at)
            QTextCursor(nc.firstCursorPosition()).setBlockFormat(cellBlockFormat());
    }
    tables::apply(t, tables::specOf(t));
    c.endEditBlock();
    setTextCursor(t->cellAt(at, cell.column()).firstCursorPosition());
}

void TextEditor::tableInsertColumns(bool right)
{
    QTextTable *t = currentTable();
    if (!t)
        return;
    const QTextTableCell cell = t->cellAt(textCursor());
    QTextCursor c = textCursor();
    c.beginEditBlock();
    const int at = right ? cell.column() + cell.columnSpan() : cell.column();
    tables::Spec s = tables::specOf(t);
    if (!s.columns.isEmpty() && s.columns.size() == t->columns())
        s.columns.insert(at, s.columns.value(qMin(at, int(s.columns.size()) - 1), 10));
    t->insertColumns(at, 1);
    for (int r = 0; r < t->rows(); ++r) {
        const QTextTableCell nc = t->cellAt(r, at);
        if (nc.column() == at)
            QTextCursor(nc.firstCursorPosition()).setBlockFormat(cellBlockFormat());
    }
    tables::apply(t, s);
    c.endEditBlock();
    setTextCursor(t->cellAt(cell.row(), at).firstCursorPosition());
}

void TextEditor::tableRemoveRows()
{
    QTextTable *t = currentTable();
    if (!t)
        return;
    int r0, rn, c0, cn;
    selectedRange(textCursor(), t, &r0, &rn, &c0, &cn);
    if (rn >= t->rows()) {
        tableRemove();
        return;
    }
    QTextCursor c = textCursor();
    c.beginEditBlock();
    splitMergedIn(t, r0, rn, 0, t->columns());
    t->removeRows(r0, rn);
    tables::apply(t, tables::specOf(t));
    c.endEditBlock();
    setTextCursor(t->cellAt(qMin(r0, t->rows() - 1), qMin(c0, t->columns() - 1)).firstCursorPosition());
}

void TextEditor::tableRemoveColumns()
{
    QTextTable *t = currentTable();
    if (!t)
        return;
    int r0, rn, c0, cn;
    selectedRange(textCursor(), t, &r0, &rn, &c0, &cn);
    if (cn >= t->columns()) {
        tableRemove();
        return;
    }
    QTextCursor c = textCursor();
    c.beginEditBlock();
    tables::Spec s = tables::specOf(t);
    if (s.columns.size() == t->columns())
        s.columns.remove(c0, cn);
    splitMergedIn(t, 0, t->rows(), c0, cn);
    t->removeColumns(c0, cn);
    tables::apply(t, s);
    c.endEditBlock();
    setTextCursor(t->cellAt(qMin(r0, t->rows() - 1), qMin(c0, t->columns() - 1)).firstCursorPosition());
}

void TextEditor::tableRemove()
{
    QTextTable *t = currentTable();
    if (!t)
        return;
    QTextCursor c(document());
    c.setPosition(t->firstPosition() - 1);
    c.setPosition(t->lastPosition() + 1, QTextCursor::KeepAnchor);
    c.beginEditBlock();
    c.removeSelectedText();
    c.endEditBlock();
    setTextCursor(c);
}

bool TextEditor::canMergeCells() const
{
    const QTextCursor c = textCursor();
    if (!c.currentTable() || !c.hasSelection())
        return false;
    int r0, rn, c0, cn;
    selectedRange(c, c.currentTable(), &r0, &rn, &c0, &cn);
    return mergeable(c.currentTable(), r0, rn, c0, cn);
}

bool TextEditor::canSplitCell() const
{
    QTextTable *t = currentTable();
    if (!t)
        return false;
    const QTextTableCell cell = t->cellAt(textCursor());
    return cell.rowSpan() > 1 || cell.columnSpan() > 1;
}

void TextEditor::tableMergeCells()
{
    QTextTable *t = currentTable();
    if (!t || !canMergeCells())
        return;
    QTextCursor c = textCursor();
    int r0, rn, c0, cn;
    selectedRange(c, t, &r0, &rn, &c0, &cn);
    if (!mergeable(t, r0, rn, c0, cn))
        return;
    c.beginEditBlock();
    moveTextIntoFirstCell(t, r0, rn, c0, cn);
    t->mergeCells(r0, c0, rn, cn);
    tables::apply(t, tables::specOf(t));
    c.endEditBlock();
    setTextCursor(t->cellAt(r0, c0).firstCursorPosition());
}

void TextEditor::tableSplitCell()
{
    QTextTable *t = currentTable();
    if (!t || !canSplitCell())
        return;
    const QTextTableCell cell = t->cellAt(textCursor());
    QTextCursor c = textCursor();
    c.beginEditBlock();
    t->splitCell(cell.row(), cell.column(), 1, 1);
    tables::apply(t, tables::specOf(t));
    c.endEditBlock();
}

void TextEditor::tableSetCellColor(const QColor &color)
{
    QTextTable *t = currentTable();
    if (!t)
        return;
    int r0, rn, c0, cn;
    selectedRange(textCursor(), t, &r0, &rn, &c0, &cn);
    QTextCursor c = textCursor();
    c.beginEditBlock();
    for (int r = r0; r < r0 + rn; ++r)
        for (int col = c0; col < c0 + cn; ++col) {
            QTextTableCell cell = t->cellAt(r, col);
            QTextTableCellFormat cf = cell.format().toTableCellFormat();
            if (color.isValid())
                cf.setProperty(props::CellBackground, color);
            else
                cf.clearProperty(props::CellBackground);
            cell.setFormat(cf);
        }
    tables::apply(t, tables::specOf(t));
    c.endEditBlock();
}

void TextEditor::tableSetSpec(const tables::Spec &spec)
{
    QTextTable *t = currentTable();
    if (!t)
        return;
    QTextCursor c = textCursor();
    c.beginEditBlock();
    tables::apply(t, spec);
    c.endEditBlock();
}

void TextEditor::editTableProperties()
{
    QTextTable *t = currentTable();
    if (!t)
        return;
    TableDialog dlg(tables::specOf(t), t->columns(), false, this);
    if (dlg.exec() == QDialog::Accepted)
        tableSetSpec(dlg.spec());
}

void TextEditor::editInsertTable()
{
    if (currentTable() || isReadOnly())
        return;
    TableDialog dlg(tables::preset(QStringLiteral("header")), 3, true, this);
    if (dlg.exec() == QDialog::Accepted)
        insertTableAtCursor(dlg.rows(), dlg.columns(), dlg.spec());
}

int TextEditor::pictureAtCursor() const
{
    const QTextCursor cur = textCursor();
    for (int p : {cur.position(), cur.position() - 1, cur.selectionStart()}) {
        if (p < 0 || p >= document()->characterCount() - 1)
            continue;
        QTextCursor t(document());
        t.setPosition(p);
        t.setPosition(p + 1, QTextCursor::KeepAnchor);
        if (t.charFormat().isImageFormat())
            return p;
    }
    // the cursor in a figure's caption: its picture
    if (QTextFrame *f = figures::at(document(), cur.position()))
        return figures::imagePosition(f);
    return -1;
}

int TextEditor::setPicture(int imagePos, int width, figures::Position pos, const QString &caption, const QString &description)
{
    QTextCursor sel(document());
    sel.setPosition(imagePos);
    sel.setPosition(imagePos + 1, QTextCursor::KeepAnchor);
    if (!sel.charFormat().isImageFormat())
        return imagePos;
    QTextImageFormat fmt = sel.charFormat().toImageFormat();
    const QImage img = document()->resource(QTextDocument::ImageResource, QUrl(fmt.name())).value<QImage>();
    const QSize natural = img.isNull() ? QSize(400, 300) : img.size();
    QTextCursor c(document());
    c.beginEditBlock();
    if (width > 0) {
        fmt.setWidth(width);
        fmt.setHeight(natural.width() > 0 ? double(width) * natural.height() / natural.width() : width);
        sel.setCharFormat(fmt);
    }
    figures::setDescription(document(), imagePos, description);
    const Formatting f = m_project ? m_project->formatting : Formatting();
    const int at = figures::setLayout(document(), imagePos, pos, caption, f);
    c.endEditBlock();
    return at;
}

void TextEditor::editPicture(int imagePos)
{
    if (imagePos < 0)
        return;
    QTextCursor sel(document());
    sel.setPosition(imagePos);
    sel.setPosition(imagePos + 1, QTextCursor::KeepAnchor);
    if (!sel.charFormat().isImageFormat())
        return;
    const QTextImageFormat fmt = sel.charFormat().toImageFormat();
    const QImage img = document()->resource(QTextDocument::ImageResource, QUrl(fmt.name())).value<QImage>();
    const QSize natural = img.isNull() ? QSize(400, 300) : img.size();
    QTextFrame *fig = figures::at(document(), imagePos);
    PictureDialog dlg(natural, fmt.width() > 0 ? qRound(fmt.width()) : natural.width(), fig ? figures::positionOf(fig) : figures::InLine,
                      fig ? figures::caption(fig) : QString(), figures::description(fmt), this);
    if (dlg.exec() != QDialog::Accepted || isReadOnly())
        return;
    const int at = setPicture(imagePos, dlg.pictureWidth(), dlg.position(), dlg.caption(), dlg.description());
    QTextCursor c(document());
    c.setPosition(at);
    setTextCursor(c);
}

} // namespace qb
