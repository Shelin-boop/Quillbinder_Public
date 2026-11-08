// Tables and figures in the text (0.62); see Tables.h.
#include "text/Tables.h"
#include "text/DisplayFormat.h"
#include "text/Styles.h"
#include "text/TextProps.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

namespace qb {
namespace tables {

QJsonObject Spec::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("style")] = style;
    o[QStringLiteral("border")] = border;
    o[QStringLiteral("borderColor")] = borderColor.name();
    if (linesOnly)
        o[QStringLiteral("linesOnly")] = true;
    o[QStringLiteral("padding")] = padding;
    o[QStringLiteral("spacing")] = spacing;
    o[QStringLiteral("headerRows")] = headerRows;
    if (headerColor.isValid())
        o[QStringLiteral("headerColor")] = headerColor.name();
    if (stripeColor.isValid())
        o[QStringLiteral("stripeColor")] = stripeColor.name();
    o[QStringLiteral("width")] = widthPercent;
    o[QStringLiteral("align")] = align & Qt::AlignHCenter ? QStringLiteral("center")
                                 : align & Qt::AlignRight ? QStringLiteral("right")
                                                          : QStringLiteral("left");
    o[QStringLiteral("flow")] = flow == 1 ? QStringLiteral("left") : flow == 2 ? QStringLiteral("right") : QStringLiteral("none");
    if (!columns.isEmpty()) {
        QJsonArray a;
        for (double c : columns)
            a.append(c);
        o[QStringLiteral("columns")] = a;
    }
    return o;
}

Spec Spec::fromJson(const QJsonObject &o)
{
    Spec s;
    s.style = o.value(QStringLiteral("style")).toString(s.style);
    s.border = qBound(0.0, o.value(QStringLiteral("border")).toDouble(s.border), 20.0);
    if (const QColor c(o.value(QStringLiteral("borderColor")).toString()); c.isValid())
        s.borderColor = c;
    s.linesOnly = o.value(QStringLiteral("linesOnly")).toBool();
    s.padding = qBound(0.0, o.value(QStringLiteral("padding")).toDouble(s.padding), 50.0);
    s.spacing = qBound(0.0, o.value(QStringLiteral("spacing")).toDouble(s.spacing), 50.0);
    s.headerRows = qBound(0, o.value(QStringLiteral("headerRows")).toInt(s.headerRows), 20);
    s.headerColor = QColor(o.value(QStringLiteral("headerColor")).toString());
    s.stripeColor = QColor(o.value(QStringLiteral("stripeColor")).toString());
    s.widthPercent = qBound(0, o.value(QStringLiteral("width")).toInt(s.widthPercent), 100);
    const QString a = o.value(QStringLiteral("align")).toString();
    s.align = a == QLatin1String("center") ? Qt::AlignHCenter : a == QLatin1String("right") ? Qt::AlignRight : Qt::AlignLeft;
    const QString f = o.value(QStringLiteral("flow")).toString();
    s.flow = f == QLatin1String("left") ? 1 : f == QLatin1String("right") ? 2 : 0;
    for (const QJsonValue &v : o.value(QStringLiteral("columns")).toArray())
        s.columns << qBound(1.0, v.toDouble(), 100.0);
    return s;
}

QList<std::pair<QString, QString>> styles()
{
    return {{QStringLiteral("grid"), QObject::tr("Grid")},
            {QStringLiteral("header"), QObject::tr("Grid with a header row")},
            {QStringLiteral("striped"), QObject::tr("Striped rows")},
            {QStringLiteral("lines"), QObject::tr("Lines between the rows")},
            {QStringLiteral("none"), QObject::tr("No lines")}};
}

Spec preset(const QString &id)
{
    Spec s;
    s.style = id;
    if (id == QLatin1String("header")) {
        s.headerColor = QColor(0xdd, 0xe3, 0xea);
    } else if (id == QLatin1String("striped")) {
        s.borderColor = QColor(0xb0, 0xb0, 0xb0);
        s.headerColor = QColor(0xc9, 0xd3, 0xdf);
        s.stripeColor = QColor(0xf0, 0xf3, 0xf6);
    } else if (id == QLatin1String("lines")) {
        s.linesOnly = true;
        s.borderColor = QColor(0x60, 0x60, 0x60);
    } else if (id == QLatin1String("none")) {
        s.border = 0;
        s.headerRows = 0;
    } else {
        s.style = QStringLiteral("grid");
    }
    return s;
}

Spec specOf(const QTextTable *t)
{
    if (!t)
        return preset(QStringLiteral("grid"));
    const QString json = t->format().stringProperty(props::TableSpec);
    if (json.isEmpty()) {
        // a table read from elsewhere: what Qt's format says
        Spec s;
        const QTextTableFormat tf = t->format();
        s.style = QStringLiteral("custom");
        s.border = tf.border();
        if (tf.borderBrush().style() != Qt::NoBrush)
            s.borderColor = tf.borderBrush().color();
        s.padding = tf.cellPadding();
        s.spacing = tf.cellSpacing() > 0 && !tf.borderCollapse() ? tf.cellSpacing() : 0;
        s.headerRows = tf.headerRowCount();
        s.widthPercent = tf.width().type() == QTextLength::PercentageLength ? qRound(tf.width().rawValue()) : 0;
        s.align = tf.alignment() & Qt::AlignHorizontal_Mask;
        return s;
    }
    return Spec::fromJson(QJsonDocument::fromJson(json.toUtf8()).object());
}

void apply(QTextTable *t, const Spec &s)
{
    if (!t)
        return;
    QTextTableFormat tf = t->format();
    tf.setBorder(s.border);
    tf.setBorderBrush(s.borderColor);
    tf.setBorderStyle(s.border > 0 ? QTextFrameFormat::BorderStyle_Solid : QTextFrameFormat::BorderStyle_None);
    tf.setBorderCollapse(s.spacing <= 0);
    tf.setCellSpacing(s.spacing);
    tf.setCellPadding(s.padding);
    tf.setHeaderRowCount(qMin(s.headerRows, t->rows()));
    tf.setWidth(s.widthPercent > 0 ? QTextLength(QTextLength::PercentageLength, s.widthPercent) : QTextLength());
    tf.setAlignment(s.align);
    tf.setPosition(s.flow == 1 ? QTextFrameFormat::FloatLeft : s.flow == 2 ? QTextFrameFormat::FloatRight : QTextFrameFormat::InFlow);
    tf.setMargin(0);
    if (s.flow == 1)
        tf.setRightMargin(12);
    else if (s.flow == 2)
        tf.setLeftMargin(12);
    tf.setTopMargin(4);
    tf.setBottomMargin(4);
    QList<QTextLength> cols;
    if (s.columns.size() == t->columns()) {
        double sum = 0;
        for (double c : s.columns)
            sum += c;
        for (double c : s.columns)
            cols << QTextLength(QTextLength::PercentageLength, sum > 0 ? c * 100.0 / sum : 100.0 / t->columns());
    }
    tf.setColumnWidthConstraints(cols);
    tf.setProperty(props::TableSpec, QString::fromUtf8(QJsonDocument(s.toJson()).toJson(QJsonDocument::Compact)));
    t->setFormat(tf);
    for (int r = 0; r < t->rows(); ++r) {
        for (int c = 0; c < t->columns(); ++c) {
            QTextTableCell cell = t->cellAt(r, c);
            if (cell.row() != r || cell.column() != c)
                continue; // a merged cell is formatted once, at its first row and column
            QTextTableCellFormat cf = cell.format().toTableCellFormat();
            const QColor own = cf.property(props::CellBackground).value<QColor>();
            QColor bg;
            if (own.isValid())
                bg = own;
            else if (r < s.headerRows && s.headerColor.isValid())
                bg = s.headerColor;
            else if (s.stripeColor.isValid() && (r - s.headerRows) % 2 == 1)
                bg = s.stripeColor;
            if (bg.isValid())
                cf.setBackground(bg);
            else
                cf.clearBackground();
            const double side = s.linesOnly ? 0 : s.border;
            cf.setLeftBorder(side);
            cf.setRightBorder(side);
            cf.setTopBorder(s.border);
            cf.setBottomBorder(s.border);
            cf.setBorderBrush(s.borderColor);
            cf.setBorderStyle(s.border > 0 ? QTextFrameFormat::BorderStyle_Solid : QTextFrameFormat::BorderStyle_None);
            if (s.linesOnly) {
                cf.setLeftBorderStyle(QTextFrameFormat::BorderStyle_None);
                cf.setRightBorderStyle(QTextFrameFormat::BorderStyle_None);
            }
            cell.setFormat(cf);
        }
    }
}

QTextTable *insert(QTextCursor &c, int rows, int columns, const Spec &s, const QTextBlockFormat &cellBlock,
                   const QTextCharFormat &cellChar)
{
    c.beginEditBlock();
    QTextTableFormat tf;
    QTextTable *t = c.insertTable(qMax(1, rows), qMax(1, columns), tf);
    for (int r = 0; r < t->rows(); ++r)
        for (int col = 0; col < t->columns(); ++col) {
            QTextCursor cc = t->cellAt(r, col).firstCursorPosition();
            cc.setBlockFormat(cellBlock);
            cc.setBlockCharFormat(cellChar);
        }
    apply(t, s);
    c.endEditBlock();
    c = t->cellAt(0, 0).firstCursorPosition();
    return t;
}

void setCellBackground(QTextTable *t, int row, int column, const QColor &c)
{
    if (!t)
        return;
    QTextTableCell cell = t->cellAt(row, column);
    if (!cell.isValid())
        return;
    QTextTableCellFormat cf = cell.format().toTableCellFormat();
    if (c.isValid())
        cf.setProperty(props::CellBackground, c);
    else
        cf.clearProperty(props::CellBackground);
    cell.setFormat(cf);
    apply(t, specOf(t));
}

} // namespace tables

namespace figures {

bool isFigure(const QTextFrame *f) { return f && !qobject_cast<const QTextTable *>(f) && f->frameFormat().hasProperty(props::Figure); }

QTextFrame *at(QTextDocument *doc, int position)
{
    if (!doc)
        return nullptr;
    QTextCursor c(doc);
    c.setPosition(qBound(0, position, doc->characterCount() - 1));
    for (QTextFrame *f = c.currentFrame(); f && f != doc->rootFrame(); f = f->parentFrame())
        if (isFigure(f))
            return f;
    return nullptr;
}

Position positionOf(const QTextFrame *f)
{
    return f ? Position(qBound(1, f->frameFormat().intProperty(props::Figure), 3)) : InLine;
}

int imagePosition(const QTextFrame *f)
{
    if (!f)
        return -1;
    for (QTextFrame::iterator it = f->begin(); !it.atEnd(); ++it) {
        const QTextBlock b = it.currentBlock();
        if (!b.isValid())
            continue;
        for (auto fr = b.begin(); !fr.atEnd(); ++fr)
            if (fr.fragment().charFormat().isImageFormat())
                return fr.fragment().position();
    }
    return -1;
}

QString caption(const QTextFrame *f)
{
    if (!f)
        return QString();
    QStringList lines;
    bool first = true;
    for (QTextFrame::iterator it = f->begin(); !it.atEnd(); ++it) {
        const QTextBlock b = it.currentBlock();
        if (!b.isValid())
            continue;
        if (first) {   // the picture's paragraph
            first = false;
            continue;
        }
        lines << b.text();
    }
    return lines.join(QLatin1Char('\n')).trimmed();
}

double frameWidth(double imageWidth) { return imageWidth + 8; }

QTextFrameFormat frameFormatFor(Position p, double imageWidth)
{
    QTextFrameFormat ff;
    ff.setProperty(props::Figure, int(p));
    ff.setBorder(0);
    ff.setPadding(2);
    ff.setMargin(0);
    ff.setTopMargin(4);
    ff.setBottomMargin(6);
    if (p == Left) {
        ff.setPosition(QTextFrameFormat::FloatLeft);
        ff.setRightMargin(14);
        ff.setWidth(QTextLength(QTextLength::FixedLength, frameWidth(imageWidth)));
    } else if (p == Right) {
        ff.setPosition(QTextFrameFormat::FloatRight);
        ff.setLeftMargin(14);
        ff.setWidth(QTextLength(QTextLength::FixedLength, frameWidth(imageWidth)));
    } else {
        ff.setPosition(QTextFrameFormat::InFlow);
    }
    return ff;
}

static double widthOf(QTextDocument *doc, const QTextImageFormat &imf)
{
    if (imf.width() > 0)
        return imf.width();
    const QImage img = doc->resource(QTextDocument::ImageResource, QUrl(imf.name())).value<QImage>();
    return img.isNull() ? 200 : img.width();
}

static void setCaptionBlocks(QTextFrame *fr, const QString &text, const Formatting &f)
{
    QTextDocument *doc = fr->document();
    // drop what follows the picture's paragraph
    QTextBlock firstBlock = doc->findBlock(fr->firstPosition());
    QTextCursor c(doc);
    c.setPosition(firstBlock.position() + firstBlock.length() - 1);
    c.setPosition(fr->lastPosition(), QTextCursor::KeepAnchor);
    c.removeSelectedText();
    if (text.trimmed().isEmpty())
        return;
    QTextBlockFormat bf = display::blockFormatFor(f, QStringLiteral("Caption"));
    bf.setAlignment(Qt::AlignHCenter);
    bf.setTextIndent(0);
    QTextCharFormat cf = display::charFormatFor(f, QStringLiteral("Caption"));
    c.setPosition(firstBlock.position() + firstBlock.length() - 1);
    const QStringList lines = text.trimmed().split(QLatin1Char('\n'));
    for (const QString &l : lines) {
        c.insertBlock(bf, cf);
        c.insertText(l, cf);
    }
}

int setLayout(QTextDocument *doc, int imagePos, Position p, const QString &captionText, const Formatting &f)
{
    if (!doc)
        return imagePos;
    QTextCursor sel(doc);
    sel.setPosition(imagePos);
    sel.setPosition(imagePos + 1, QTextCursor::KeepAnchor);
    if (!sel.charFormat().isImageFormat())
        return imagePos;
    const QTextImageFormat imf = sel.charFormat().toImageFormat();
    QTextFrame *existing = at(doc, imagePos);
    QTextCursor c(doc);
    c.beginEditBlock();
    int result = imagePos;
    if (p == InLine) {
        if (existing) {
            // the figure goes; the picture stays in a paragraph of its own
            const int from = existing->firstPosition() - 1;
            QTextCursor r(doc);
            r.setPosition(from);
            r.setPosition(existing->lastPosition() + 1, QTextCursor::KeepAnchor);
            r.removeSelectedText();
            r.insertBlock(display::blockFormatFor(f, QString()), QTextCharFormat());
            result = r.position();
            r.insertImage(imf);
            r.insertBlock(display::blockFormatFor(f, QString()), QTextCharFormat());
        }
    } else if (existing) {
        existing->setFrameFormat(frameFormatFor(p, widthOf(doc, imf)));
        setCaptionBlocks(existing, captionText, f);
        result = imagePosition(existing);
    } else {
        // the picture leaves its paragraph; the figure goes before it (left, right: the paragraph
        // flows around it) or where it was (centered: the paragraph is split there)
        const QTextBlock block = doc->findBlock(imagePos);
        const bool atStart = imagePos == block.position();
        sel.removeSelectedText();
        int at = p == Centered ? imagePos : block.position();
        QTextCursor k(doc);
        k.setPosition(at);
        QTextFrame *fr = k.insertFrame(frameFormatFor(p, widthOf(doc, imf)));
        QTextBlockFormat bf = display::blockFormatFor(f, QString());
        bf.setAlignment(Qt::AlignHCenter);
        bf.setTextIndent(0);
        bf.setTopMargin(0);
        bf.setBottomMargin(0);
        k.setBlockFormat(bf);
        result = k.position();
        k.insertImage(imf);
        setCaptionBlocks(fr, captionText, f);
        // the empty paragraph Qt leaves before a frame at a paragraph's start goes, unless the
        // figure is the first thing of the text
        if (p != Centered || atStart) {
            const QTextBlock before = doc->findBlock(fr->firstPosition() - 1);
            if (before.isValid() && before.text().isEmpty() && before.previous().isValid()
                && !QTextCursor(before.previous()).currentTable()) {
                QTextCursor d(doc);
                d.setPosition(before.previous().position() + before.previous().length() - 1);
                d.deleteChar();
                result = imagePosition(fr);
            }
        }
        // an empty paragraph after a figure that took the whole paragraph
        const QTextBlock after = doc->findBlock(fr->lastPosition() + 1);
        Q_UNUSED(after);
    }
    c.endEditBlock();
    return result;
}

QString description(const QTextFormat &imageFormat)
{
    const QString d = imageFormat.stringProperty(props::ImageDescription);
    return d.isEmpty() ? imageFormat.toCharFormat().toolTip() : d;
}

void setDescription(QTextDocument *doc, int imagePos, const QString &text)
{
    QTextCursor c(doc);
    c.setPosition(imagePos);
    c.setPosition(imagePos + 1, QTextCursor::KeepAnchor);
    if (!c.charFormat().isImageFormat())
        return;
    QTextImageFormat imf = c.charFormat().toImageFormat();
    if (text.trimmed().isEmpty()) {
        imf.clearProperty(props::ImageDescription);
        imf.setToolTip(QString());
    } else {
        imf.setProperty(props::ImageDescription, text.trimmed());
        imf.setToolTip(text.trimmed());
    }
    c.setCharFormat(imf);
}

} // namespace figures
} // namespace qb
