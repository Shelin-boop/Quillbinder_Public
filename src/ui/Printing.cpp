#include "ui/Printing.h"

#include "compile/CompileFormat.h"
#include "compile/Compiler.h"
#include "compile/PdfExporter.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/Corkboard.h"
#include "ui/EditorPane.h"
#include "ui/Outliner.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"

#include "AppInfo.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QImageReader>
#include <QPainter>
#include <QPdfDocument>
#include <QPrinter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QUrl>
#include <cmath>

namespace qb {
namespace printing {

namespace {

// what the printer is asked for
struct Range {
    bool selection = false;
    int from = 0, to = 0;   // 1-based; 0 = no limit
    int copies = 1;
    bool contains(int page) const { return (from <= 0 || page >= from) && (to <= 0 || page <= to); }
};

Range rangeOf(QPrinter *printer)
{
    Range r;
    r.selection = printer->printRange() == QPrinter::Selection;
    if (printer->printRange() == QPrinter::PageRange) {
        r.from = printer->fromPage();
        r.to = printer->toPage();
    }
    r.copies = printer->supportsMultipleCopies() ? 1 : qMax(1, printer->copyCount());
    return r;
}

double mm(double v, double dpi) { return v / 25.4 * dpi; }

QString uiFamily(const QString &family) { return family.isEmpty() ? QApplication::font().family() : family; }

// the paper in device pixels, and the area inside the margins
struct Frame {
    QRectF paper, area;
    double dpi = 1200;
};

Frame frameOf(QPrinter *printer, const PrintSettings &s, bool margins = true)
{
    Frame f;
    f.dpi = printer->resolution();
    f.paper = QRectF(printer->pageLayout().fullRectPixels(int(f.dpi)));
    f.paper.moveTopLeft(QPointF(0, 0));
    if (!margins) {
        f.area = f.paper;
        return f;
    }
    const PageSetup pg = s.page.normalized();
    f.area = f.paper.adjusted(mm(pg.left, f.dpi), mm(pg.top, f.dpi), -mm(pg.right, f.dpi), -mm(pg.bottom, f.dpi));
    if (f.area.width() < mm(20, f.dpi) || f.area.height() < mm(20, f.dpi))
        f.area = f.paper.adjusted(mm(10, f.dpi), mm(10, f.dpi), -mm(10, f.dpi), -mm(10, f.dpi));
    return f;
}

void drawHeaderFooter(QPainter &p, const Frame &f, const PrintSettings &s, const Job &job, int page, int pages,
                      const QString &family)
{
    if (page == 1 && !s.headerOnFirstPage)
        return;
    QFont font(family);
    font.setPointSizeF(9);
    p.save();
    p.setFont(font);
    p.setPen(Qt::black);
    const QFontMetricsF fm(font, p.device());
    const double top = f.area.top() - f.paper.top(), bottom = f.paper.bottom() - f.area.bottom();
    const double hy = qMax(mm(4, f.dpi), top * 0.5 - fm.height() * 0.5);
    const double fy = f.paper.height() - qMax(mm(4, f.dpi) + fm.height(), bottom * 0.5 + fm.height() * 0.5);
    const QRectF hr(f.area.left(), hy, f.area.width(), fm.height()), fr(f.area.left(), fy, f.area.width(), fm.height());
    auto put = [&](const QRectF &r, Qt::Alignment a, const QString &t) {
        const QString text = fillPlaceholders(t, job, page, pages);
        if (!text.isEmpty())
            p.drawText(r, a | Qt::AlignVCenter, fm.elidedText(text, Qt::ElideRight, r.width() / 3 * (a == Qt::AlignHCenter ? 1 : 1.5)));
    };
    put(hr, Qt::AlignLeft, s.headerLeft);
    put(hr, Qt::AlignHCenter, s.headerCenter);
    put(hr, Qt::AlignRight, s.headerRight);
    put(fr, Qt::AlignLeft, s.footerLeft);
    put(fr, Qt::AlignHCenter, s.footerCenter);
    put(fr, Qt::AlignRight, s.footerRight);
    p.restore();
}

// Lines of wrapped text in a rectangle (the last line that fits ends with "…").
// Returns the height used.
double drawWrapped(QPainter *p, const QRectF &r, const QString &text, const QFont &font, const QColor &color,
                   int maxLines = 0, bool draw = true, double lineHeight = 0)
{
    if (text.trimmed().isEmpty() || r.width() <= 0)
        return 0;
    QTextLayout layout(text, font, p->device());
    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(opt);
    const QFontMetricsF fm(font, p->device());
    const double lh = lineHeight > 0 ? lineHeight : fm.lineSpacing();
    layout.beginLayout();
    QList<QTextLine> lines;
    double y = 0;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(r.width());
        if ((r.height() > 0 && y + lh > r.height() + 0.5) || (maxLines > 0 && lines.size() >= maxLines))
            break;
        line.setPosition(QPointF(0, y + (lh - fm.lineSpacing())));
        y += lh;
        lines << line;
    }
    layout.endLayout();
    if (!draw)
        return y;
    p->save();
    p->setFont(font);
    p->setPen(color);
    for (int i = 0; i < lines.size(); ++i) {
        const QTextLine &l = lines[i];
        const bool cut = i == lines.size() - 1 && l.textStart() + l.textLength() < text.size();
        if (cut) {
            const QString s = fm.elidedText(text.mid(l.textStart(), l.textLength()).trimmed() + QChar(0x2026), Qt::ElideRight, r.width());
            p->drawText(QPointF(r.left(), r.top() + l.y() + fm.ascent()), s);
        } else {
            l.draw(p, r.topLeft());
        }
    }
    p->restore();
    return y;
}

void drawFitted(QPainter &p, const QRectF &box, const QImage &img)
{
    if (img.isNull() || box.width() <= 0 || box.height() <= 0)
        return;
    const QSizeF sz = QSizeF(img.size()).scaled(box.size(), Qt::KeepAspectRatio);
    const QRectF r(box.center().x() - sz.width() / 2, box.center().y() - sz.height() / 2, sz.width(), sz.height());
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(r, img);
    p.restore();
}

QImage readImage(const QString &path)
{
    QImageReader r(path);
    r.setAutoTransform(true);
    return r.read();
}

// The picture a card shows: a picture document, or the picture of the index card.
QImage cardPicture(Project *proj, BinderItem *it)
{
    if (it->type == ItemType::Image)
        return readImage(proj->mediaFilePath(it));
    if (!it->synopsisImage.isEmpty())
        return readImage(proj->contentDir(it->id) + QLatin1Char('/') + it->synopsisImage);
    return QImage();
}

// ---------------------------------------------------------------- texts

QPageSize pageSizeFor(const PageSetup &pg)
{
    static const QList<QPair<QString, QPageSize::PageSizeId>> known = {
        {QStringLiteral("A4"), QPageSize::A4},       {QStringLiteral("A5"), QPageSize::A5},
        {QStringLiteral("A6"), QPageSize::A6},       {QStringLiteral("US Letter"), QPageSize::Letter},
        {QStringLiteral("US Legal"), QPageSize::Legal}, {QStringLiteral("B5"), QPageSize::B5}};
    for (const auto &k : known)
        if (k.first == pg.paper)
            return QPageSize(k.second);
    return QPageSize(QSizeF(pg.width, pg.height), QPageSize::Millimeter, pg.paper == QLatin1String("Custom") ? QString() : pg.paper,
                     QPageSize::ExactMatch);
}

// The text of `item` from..to as a document of its own (pictures included).
QTextDocument *selectedPart(QTextDocument *doc, int from, int to)
{
    QTextDocument *part = doc->clone();
    QTextCursor c(part);
    c.setPosition(qMin(to, part->characterCount() - 1));
    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    c.setPosition(0);
    c.setPosition(qMin(from, part->characterCount() - 1), QTextCursor::KeepAnchor);
    c.removeSelectedText();
    // the first paragraph keeps the look it had where the selection began
    QTextBlockFormat bf = doc->findBlock(from).blockFormat();
    bf.setObjectIndex(-1);
    QTextCursor(part).setBlockFormat(bf);
    return part;
}

bool renderText(const Job &job, const PrintSettings &s, QPrinter *printer, const Range &range, QString *error, int *pagesOut)
{
    Project *p = job.project;
    compile::Format f = compile::formatByName(QStringLiteral("Default (Editor Formatting)"));
    f.useEditorFormatting = true;
    f.footnotePlacement = s.endnotes ? 2 : 0;
    f.endnotesTitle = QObject::tr("Notes");
    f.removeHighlights = !s.highlights;
    f.keepComments = s.comments;
    f.keepAnnotationsAsComments = s.comments;
    f.toc = false;
    f.replacements.clear();
    f.styleOverrides.clear();
    f.transform = compile::Transformations();
    f.transform.removeTrailingSpaces = false;
    f.pdfLinks = true;
    compile::Compiler compiler(p, f, p->compile, {AppSettings::instance().forename, AppSettings::instance().surname});
    QList<compile::PrintPart> parts;
    std::unique_ptr<QTextDocument> part;
    if (range.selection && job.selectionItem && job.selectionEnd > job.selectionStart) {
        part.reset(selectedPart(p->docs()->text(job.selectionItem->id), job.selectionStart, job.selectionEnd));
        parts.append({job.selectionItem, 1, part.get()});
    } else {
        for (int i = 0; i < job.items.size(); ++i) {
            BinderItem *it = job.items[i];
            parts.append({it, job.levels.value(i, 1), it->hasText() ? p->docs()->text(it->id) : nullptr});
        }
    }
    compile::PrintOptions o;
    o.titles = s.titles;
    o.synopses = s.synopses;
    o.notes = s.notes;
    compile::Manuscript m = compiler.buildPrint(parts, o);
    if (s.comments)
        compile::commentsAsFootnotes(m, QObject::tr("Comment: "));
    if (!s.editorFont) {
        // one font for the text; sizes set by hand keep their proportion
        const double factor = m.fontSize > 0 ? s.fontSize / m.fontSize : 1.0;
        m.fontFamily = s.fontFamily.isEmpty() ? m.fontFamily : s.fontFamily;
        m.fontSize = s.fontSize;
        auto adapt = [factor](QList<compile::Paragraph> &paras) {
            for (compile::Paragraph &para : paras)
                for (compile::Run &r : para.runs) {
                    r.fontFamily.clear();
                    if (r.fontSize > 0)
                        r.fontSize *= factor;
                }
        };
        adapt(m.paragraphs);
        for (compile::Footnote &fn : m.footnotes)
            adapt(fn.paragraphs);
    }
    if (s.lineSpacing > 0) {
        m.lineSpacing = s.lineSpacing;
        for (compile::Paragraph &para : m.paragraphs)
            if (para.script.isEmpty())
                para.direct &= ~compile::Paragraph::DLine;
    }
    // the page: the printer's paper, the margins and texts of the print settings
    const QRectF paperMm = printer->pageLayout().fullRect(QPageLayout::Millimeter);
    const PageSetup pg = s.page.normalized();
    m.page = compile::PageSettings();
    m.page.widthMm = paperMm.width();
    m.page.heightMm = paperMm.height();
    m.page.marginLeftMm = pg.left;
    m.page.marginRightMm = pg.right;
    m.page.marginTopMm = pg.top;
    m.page.marginBottomMm = pg.bottom;
    m.page.headerLeft = fillPlaceholders(s.headerLeft, job, -1, -1);
    m.page.headerCenter = fillPlaceholders(s.headerCenter, job, -1, -1);
    m.page.headerRight = fillPlaceholders(s.headerRight, job, -1, -1);
    m.page.footerLeft = fillPlaceholders(s.footerLeft, job, -1, -1);
    m.page.footerCenter = fillPlaceholders(s.footerCenter, job, -1, -1);
    m.page.footerRight = fillPlaceholders(s.footerRight, job, -1, -1);
    m.page.headerOnFirstPage = s.headerOnFirstPage;
    m.page.headerOnChapterPages = true;
    m.page.headerFontSize = 9;
    if (m.paragraphs.isEmpty()) {
        compile::Paragraph empty;
        m.paragraphs << empty;
    }
    compile::PaintRange pr;
    pr.fromPage = range.from;
    pr.toPage = range.to;
    pr.copies = range.copies;
    compile::PdfReport rep;
    const bool ok = compile::paintManuscript(m, printer, pr, &rep, error);
    if (pagesOut)
        *pagesOut = rep.pages;
    return ok;
}

// ---------------------------------------------------------------- index cards

void drawCard(QPainter &p, const QRectF &r, BinderItem *it, Project *proj, const PrintSettings &s, double dpi)
{
    auto px = [dpi](double v) { return mm(v, dpi); };
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(r, Qt::white);
    const Label *label = proj->labelById(it->label);
    double top = r.top();
    if (s.cardLabel && label && label->color.isValid()) {
        p.fillRect(QRectF(r.left(), r.top(), r.width(), px(2.5)), label->color);
        top += px(2.5);
    }
    const double pad = px(4);
    const QString family = uiFamily(s.cardFont);
    QFont tf(family), bf(family), sf(family);
    tf.setPointSizeF(s.cardFontSize + 2);
    tf.setBold(true);
    bf.setPointSizeF(s.cardFontSize);
    sf.setPointSizeF(qMax(5.0, s.cardFontSize - 2));
    const QFontMetricsF tfm(tf, p.device()), bfm(bf, p.device()), sfm(sf, p.device());
    const QRectF inner(r.left() + pad, top + px(2), r.width() - 2 * pad, r.bottom() - top - px(2) - px(3));
    double y = inner.top();
    if (s.cardTitle) {
        const QString title = it->title.isEmpty() ? QObject::tr("Untitled") : it->title;
        p.setFont(tf);
        p.setPen(Qt::black);
        p.drawText(QRectF(inner.left(), y, inner.width(), tfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   tfm.elidedText(title, Qt::ElideRight, inner.width()));
        y += tfm.height() + px(1.5);
    }
    // keywords and status at the bottom
    QStringList kws;
    if (s.cardKeywords)
        for (const QString &k : it->keywords)
            if (const Keyword *kw = proj->keywordById(k))
                kws << kw->name;
    const Status *st = s.cardStatus ? proj->statusById(it->status) : nullptr;
    const double footH = (kws.isEmpty() && !st) ? 0 : sfm.height() + px(1);
    const double lh = bfm.lineSpacing() * 1.15;
    if (s.cardRuled) {
        // the lines of an index card: red under the title, blue below
        QPen red(QColor(220, 90, 90));
        red.setWidthF(px(0.3));
        p.setPen(red);
        const double ry = y - px(0.6);
        p.drawLine(QPointF(r.left() + px(2), ry), QPointF(r.right() - px(2), ry));
        QPen blue(QColor(140, 175, 220));
        blue.setWidthF(px(0.2));
        p.setPen(blue);
        for (double ly = y + lh; ly < inner.bottom() - footH; ly += lh)
            p.drawLine(QPointF(r.left() + px(2), ly), QPointF(r.right() - px(2), ly));
    }
    const QRectF body(inner.left(), y, inner.width(), inner.bottom() - footH - y);
    const QImage pic = s.cardPictures ? cardPicture(proj, it) : QImage();
    if (!pic.isNull()) {
        drawFitted(p, body.adjusted(0, px(1), 0, -px(1)), pic);
    } else {
        QString text = s.cardSynopsis ? it->synopsis : QString();
        QColor col(20, 20, 20);
        QFont f = bf;
        if (text.trimmed().isEmpty() && s.cardTextIfEmpty && it->hasText()) {
            text = text::previewText(proj->docs()->plainText(it->id), 600);
            col = QColor(110, 110, 110);
            f.setItalic(true);
        }
        // the text sits on the lines
        drawWrapped(&p, QRectF(body.left(), body.top() + (lh - bfm.lineSpacing()) - px(0.8), body.width(), body.height()), text, f,
                    col, 0, true, lh);
    }
    if (footH > 0) {
        const QRectF fr(inner.left(), inner.bottom() - sfm.height(), inner.width(), sfm.height());
        p.setFont(sf);
        p.setPen(QColor(90, 90, 90));
        const double stW = st ? sfm.horizontalAdvance(st->name.toUpper()) + px(2) : 0;
        if (st)
            p.drawText(fr, Qt::AlignRight | Qt::AlignVCenter, st->name.toUpper());
        if (!kws.isEmpty())
            p.drawText(QRectF(fr.left(), fr.top(), fr.width() - stW, fr.height()), Qt::AlignLeft | Qt::AlignVCenter,
                       sfm.elidedText(kws.join(QStringLiteral(", ")), Qt::ElideRight, fr.width() - stW));
    }
    if (s.cardBorder) {
        QPen pen(QColor(140, 140, 140));
        pen.setWidthF(px(0.25));
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
    }
    p.restore();
}

bool renderCards(const Job &job, const PrintSettings &s, QPrinter *printer, const Range &range, QString *error, int *pagesOut)
{
    Project *proj = job.project;
    QList<BinderItem *> cards = job.items;
    if (range.selection && !job.selected.isEmpty()) {
        cards.clear();
        for (BinderItem *it : job.items)
            if (job.selected.contains(it))
                cards << it;
    }
    const Frame f = frameOf(printer, s, !s.cardPerPage);
    auto px = [&f](double v) { return mm(v, f.dpi); };
    QList<QRectF> places;
    if (s.cardPerPage) {
        places << f.paper;
    } else {
        double cw = px(s.cardWidth), ch = px(s.cardHeight);
        const double gap = px(5);
        if (cw > f.area.width() || ch > f.area.height()) {
            const double k = qMin(f.area.width() / cw, f.area.height() / ch);
            cw *= k;
            ch *= k;
        }
        const int cols = qMax(1, int((f.area.width() + gap) / (cw + gap)));
        const int rows = qMax(1, int((f.area.height() + gap) / (ch + gap)));
        const double x0 = f.area.left() + (f.area.width() - (cols * cw + (cols - 1) * gap)) / 2;
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
                places << QRectF(x0 + c * (cw + gap), f.area.top() + r * (ch + gap), cw, ch);
    }
    const int perPage = int(places.size());
    const int pages = qMax(1, int((cards.size() + perPage - 1) / perPage));
    if (pagesOut)
        *pagesOut = pages;
    QPainter painter;
    if (!painter.begin(printer)) {
        if (error)
            *error = QObject::tr("The printer could not be started.");
        return false;
    }
    bool any = false;
    for (int copy = 0; copy < range.copies; ++copy)
        for (int pg = 0; pg < pages; ++pg) {
            if (!range.contains(pg + 1))
                continue;
            if (any)
                printer->newPage();
            any = true;
            if (!s.cardPerPage)
                drawHeaderFooter(painter, f, s, job, pg + 1, pages, uiFamily(s.cardFont));
            for (int k = 0; k < perPage; ++k) {
                const int index = pg * perPage + k;
                if (index >= cards.size())
                    break;
                drawCard(painter, places[k], cards[index], proj, s, f.dpi);
            }
        }
    painter.end();
    return true;
}

// ---------------------------------------------------------------- outliner

QString cellText(Project *proj, BinderItem *it, int column)
{
    BinderModel *model = proj->model();
    const QModelIndex idx = model->indexFor(it, column);
    if (column == BinderModel::ColCompile) {
        const QVariant cs = idx.data(Qt::CheckStateRole);
        return cs.isValid() ? (cs.toInt() == Qt::Checked ? QStringLiteral("✓") : QStringLiteral("–")) : QString();
    }
    const QVariant cs = idx.data(Qt::CheckStateRole);
    if (cs.isValid() && column >= BinderModel::ColFirstCustom)
        return cs.toInt() == Qt::Checked ? QStringLiteral("✓") : QString();
    return idx.data(Qt::DisplayRole).toString();
}

bool renderOutliner(const Job &job, const PrintSettings &s, QPrinter *printer, const Range &range, QString *error, int *pagesOut)
{
    Project *proj = job.project;
    QList<Row> rows = s.outlinerAllRows ? job.allRows : job.rows;
    if (range.selection && !job.selected.isEmpty()) {
        QList<Row> chosen;
        for (const Row &r : job.allRows)
            if (job.selected.contains(r.item))
                chosen << r;
        rows = chosen;
    }
    const bool synopses = s.outlinerSynopses == 0 ? job.synopses : s.outlinerSynopses == 1;
    const Frame f = frameOf(printer, s);
    auto px = [&f](double v) { return mm(v, f.dpi); };
    const QString family = uiFamily(s.outlinerFont);
    QFont font(family), bold(family), small(family), head(family);
    font.setPointSizeF(s.outlinerFontSize);
    bold.setPointSizeF(s.outlinerFontSize);
    bold.setBold(true);
    small.setPointSizeF(s.outlinerFontSize * 0.92);
    head.setPointSizeF(s.outlinerFontSize);
    head.setBold(true);
    // widths: the shares of the outliner, the title at least a third of the page
    QList<Column> cols = job.columns;
    if (cols.isEmpty())
        cols << Column{BinderModel::ColTitle, 1.0};
    double total = 0;
    for (const Column &c : cols)
        total += c.share;
    QList<double> widths;
    for (const Column &c : cols)
        widths << f.area.width() * c.share / qMax(1e-6, total);
    if (cols.size() > 1 && widths[0] < f.area.width() / 3) {
        const double rest = f.area.width() - f.area.width() / 3;
        double others = 0;
        for (int i = 1; i < widths.size(); ++i)
            others += widths[i];
        widths[0] = f.area.width() / 3;
        for (int i = 1; i < widths.size(); ++i)
            widths[i] = widths[i] * rest / qMax(1e-6, others);
    }
    const double pad = px(1.2);
    const double indentStep = px(5);
    const double iconSize = px(3.6);
    QPainter painter;
    if (!painter.begin(printer)) {
        if (error)
            *error = QObject::tr("The printer could not be started.");
        return false;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    const QFontMetricsF fm(font, painter.device()), hfm(head, painter.device());
    const double headH = hfm.height() + 2 * pad;
    auto titleWidth = [&](const Row &r) { return widths[0] - 2 * pad - r.level * indentStep - iconSize - px(1.5); };
    auto rowHeight = [&](const Row &r) {
        double h = fm.height();
        const double tw = titleWidth(r);
        h = drawWrapped(&painter, QRectF(0, 0, tw, 0), r.item->title.isEmpty() ? QObject::tr("Untitled") : r.item->title,
                        r.item->isContainer() ? bold : font, Qt::black, 3, false);
        h = qMax(h, fm.height());
        if (synopses) {
            const QString syn = proj->model()->indexFor(r.item).data(BinderModel::PreviewRole).toString();
            h += drawWrapped(&painter, QRectF(0, 0, tw, 0), syn.left(600), small, Qt::black, 4, false);
        }
        for (int c = 1; c < cols.size(); ++c) {
            const QString t = cellText(proj, r.item, cols[c].column);
            h = qMax(h, drawWrapped(&painter, QRectF(0, 0, widths[c] - 2 * pad, 0), t, font, Qt::black, 4, false));
        }
        return h + 2 * pad;
    };
    // the pages: rows that fit; the column titles on the first page, and on the others if chosen
    QList<QList<int>> pageRows;
    pageRows.append(QList<int>());
    double y = f.area.top() + headH;
    QList<double> heights;
    for (int i = 0; i < rows.size(); ++i) {
        const double h = rowHeight(rows[i]);
        heights << h;
        if (y + h > f.area.bottom() && !pageRows.last().isEmpty()) {
            pageRows.append(QList<int>());
            y = f.area.top() + (s.outlinerHeader ? headH : 0);
        }
        pageRows.last() << i;
        y += h;
    }
    const int pages = int(pageRows.size());
    if (pagesOut)
        *pagesOut = pages;
    QPen line(QColor(185, 185, 185));
    line.setWidthF(px(0.2));
    bool any = false;
    for (int copy = 0; copy < range.copies; ++copy)
        for (int pg = 0; pg < pages; ++pg) {
            if (!range.contains(pg + 1))
                continue;
            if (any)
                printer->newPage();
            any = true;
            drawHeaderFooter(painter, f, s, job, pg + 1, pages, family);
            double y = f.area.top();
            if (s.outlinerHeader || pg == 0) {
                painter.fillRect(QRectF(f.area.left(), y, f.area.width(), headH), QColor(232, 232, 232));
                double x = f.area.left();
                painter.setFont(head);
                painter.setPen(Qt::black);
                for (int c = 0; c < cols.size(); ++c) {
                    const QString t = proj->model()->headerData(cols[c].column, Qt::Horizontal, Qt::DisplayRole).toString();
                    painter.drawText(QRectF(x + pad, y + pad, widths[c] - 2 * pad, hfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                                     hfm.elidedText(t, Qt::ElideRight, widths[c] - 2 * pad));
                    x += widths[c];
                }
                y += headH;
                painter.setPen(QPen(Qt::black, px(0.3)));
                painter.drawLine(QPointF(f.area.left(), y), QPointF(f.area.right(), y));
            }
            for (int i : pageRows[pg]) {
                const Row &r = rows[i];
                const double h = heights[i];
                double x = f.area.left();
                // title (and synopsis), indented by level, with the document's icon
                const double ix = x + pad + r.level * indentStep;
                const QPixmap icon = icons::forItem(proj, r.item).pixmap(64, 64);
                if (!icon.isNull())
                    painter.drawPixmap(QRectF(ix, y + pad + (fm.height() - iconSize) / 2, iconSize, iconSize), icon, icon.rect());
                const double tx = ix + iconSize + px(1.5);
                const double tw = titleWidth(r);
                const double th = qMax(fm.height(), drawWrapped(&painter, QRectF(tx, y + pad, tw, 0),
                                                                r.item->title.isEmpty() ? QObject::tr("Untitled") : r.item->title,
                                                                r.item->isContainer() ? bold : font, Qt::black, 3));
                if (synopses) {
                    const QString syn = proj->model()->indexFor(r.item).data(BinderModel::PreviewRole).toString();
                    drawWrapped(&painter, QRectF(tx, y + pad + th, tw, 0), syn.left(600), small,
                                r.item->synopsis.isEmpty() ? QColor(120, 120, 120) : QColor(60, 60, 60), 4);
                }
                x += widths[0];
                for (int c = 1; c < cols.size(); ++c) {
                    const int col = cols[c].column;
                    const QRectF cell(x + pad, y + pad, widths[c] - 2 * pad, h - 2 * pad);
                    if (col == BinderModel::ColProgress) {
                        const double v = proj->model()->indexFor(r.item, col).data(BinderModel::ProgressRole).toDouble();
                        if (v >= 0) {
                            const QRectF bar(cell.left(), cell.top() + (fm.height() - px(2)) / 2, cell.width(), px(2));
                            painter.setPen(QPen(QColor(120, 120, 120), px(0.15)));
                            painter.setBrush(Qt::NoBrush);
                            painter.drawRect(bar);
                            painter.fillRect(QRectF(bar.left(), bar.top(), bar.width() * qMin(1.0, v), bar.height()),
                                             v >= 1.0 ? QColor(70, 160, 80) : QColor(90, 130, 200));
                        }
                    } else {
                        QRectF tr = cell;
                        if (col == BinderModel::ColLabel && s.outlinerLabels) {
                            if (const Label *l = proj->labelById(r.item->label)) {
                                const double d = px(2.2);
                                painter.setPen(Qt::NoPen);
                                painter.setBrush(l->color);
                                painter.drawEllipse(QRectF(tr.left(), tr.top() + (fm.height() - d) / 2, d, d));
                                tr.setLeft(tr.left() + d + px(1.2));
                            }
                        }
                        const QVariant align = proj->model()->indexFor(r.item, col).data(Qt::TextAlignmentRole);
                        const QString t = cellText(proj, r.item, col);
                        if (align.isValid() && (align.toInt() & (Qt::AlignRight | Qt::AlignHCenter)) && fm.horizontalAdvance(t) < tr.width()) {
                            painter.setFont(font);
                            painter.setPen(Qt::black);
                            painter.drawText(QRectF(tr.left(), tr.top(), tr.width(), fm.height()),
                                             Qt::Alignment(align.toInt() & Qt::AlignHorizontal_Mask) | Qt::AlignVCenter, t);
                        } else {
                            drawWrapped(&painter, QRectF(tr.left(), tr.top(), tr.width(), 0), t, font, Qt::black, 4);
                        }
                    }
                    x += widths[c];
                }
                y += h;
                if (s.outlinerLines) {
                    painter.setPen(line);
                    painter.drawLine(QPointF(f.area.left(), y), QPointF(f.area.right(), y));
                }
            }
        }
    painter.end();
    return true;
}

// ---------------------------------------------------------------- pictures, PDF files, web pages

bool renderPicture(const Job &job, const PrintSettings &s, QPrinter *printer, const Range &range, QString *error, int *pagesOut)
{
    BinderItem *it = job.items.value(0);
    const QImage img = it ? readImage(job.project->mediaFilePath(it)) : QImage();
    if (img.isNull()) {
        if (error)
            *error = QObject::tr("The picture could not be read.");
        return false;
    }
    if (pagesOut)
        *pagesOut = 1;
    const Frame f = frameOf(printer, s);
    QPainter painter;
    if (!painter.begin(printer)) {
        if (error)
            *error = QObject::tr("The printer could not be started.");
        return false;
    }
    bool any = false;
    for (int copy = 0; copy < range.copies; ++copy) {
        if (!range.contains(1))
            continue;
        if (any)
            printer->newPage();
        any = true;
        drawHeaderFooter(painter, f, s, job, 1, 1, uiFamily(QString()));
        drawFitted(painter, f.area, img);
    }
    painter.end();
    return true;
}

bool renderPdf(const Job &job, const PrintSettings &, QPrinter *printer, const Range &range, QString *error, int *pagesOut)
{
    BinderItem *it = job.items.value(0);
    QPdfDocument pdf;
    if (!it || pdf.load(job.project->mediaFilePath(it)) != QPdfDocument::Error::None || pdf.pageCount() < 1) {
        if (error)
            *error = QObject::tr("The PDF file could not be read.");
        return false;
    }
    const int pages = pdf.pageCount();
    if (pagesOut)
        *pagesOut = pages;
    PrintSettings none;
    const Frame f = frameOf(printer, none, false);
    const QRectF area = f.paper.adjusted(mm(5, f.dpi), mm(5, f.dpi), -mm(5, f.dpi), -mm(5, f.dpi));
    QPainter painter;
    if (!painter.begin(printer)) {
        if (error)
            *error = QObject::tr("The printer could not be started.");
        return false;
    }
    bool any = false;
    for (int copy = 0; copy < range.copies; ++copy)
        for (int i = 0; i < pages; ++i) {
            if (!range.contains(i + 1))
                continue;
            if (any)
                printer->newPage();
            any = true;
            // each page as a picture of 300 dpi, as large as the paper allows
            const QSizeF pt = pdf.pagePointSize(i);
            const QSizeF target = pt.scaled(area.size(), Qt::KeepAspectRatio);
            const double renderDpi = qMin(300.0, f.dpi);
            const QSize size((target * (renderDpi / f.dpi)).toSize());
            QImage img = pdf.render(i, size);
            if (img.isNull())
                continue;
            if (img.hasAlphaChannel()) {
                QImage white(img.size(), QImage::Format_RGB32);
                white.fill(Qt::white);
                QPainter wp(&white);
                wp.drawImage(0, 0, img);
                wp.end();
                img = white;
            }
            drawFitted(painter, area, img);
        }
    painter.end();
    return true;
}

bool renderWebPage(const Job &job, const PrintSettings &s, QPrinter *printer, const Range &range, QString *error, int *pagesOut)
{
    BinderItem *it = job.items.value(0);
    const QString path = it ? job.project->mediaFilePath(it) : QString();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("The web page could not be read.");
        return false;
    }
    const Frame f = frameOf(printer, s);
    QTextDocument doc;
    doc.setMetaInformation(QTextDocument::DocumentUrl, QUrl::fromLocalFile(path).toString());
    doc.setBaseUrl(QUrl::fromLocalFile(path));
    doc.documentLayout()->setPaintDevice(printer);
    QFont font(QStringLiteral("serif"));
    font.setPointSizeF(11);
    doc.setDefaultFont(font);
    doc.setDocumentMargin(0);
    doc.setHtml(QString::fromUtf8(file.readAll()));
    doc.setPageSize(f.area.size());
    const int pages = qMax(1, doc.pageCount());
    if (pagesOut)
        *pagesOut = pages;
    QPainter painter;
    if (!painter.begin(printer)) {
        if (error)
            *error = QObject::tr("The printer could not be started.");
        return false;
    }
    bool any = false;
    for (int copy = 0; copy < range.copies; ++copy)
        for (int i = 0; i < pages; ++i) {
            if (!range.contains(i + 1))
                continue;
            if (any)
                printer->newPage();
            any = true;
            drawHeaderFooter(painter, f, s, job, i + 1, pages, uiFamily(QString()));
            painter.save();
            painter.translate(f.area.left(), f.area.top() - i * f.area.height());
            const QRectF clip(0, i * f.area.height(), f.area.width(), f.area.height());
            painter.setClipRect(clip);
            QAbstractTextDocumentLayout::PaintContext ctx;
            ctx.clip = clip;
            ctx.palette.setColor(QPalette::Text, Qt::black);
            doc.documentLayout()->draw(&painter, ctx);
            painter.restore();
        }
    painter.end();
    return true;
}

} // namespace

QString fillPlaceholders(const QString &text, const Job &job, int page, int pages)
{
    if (!text.contains(QLatin1String("<$")))
        return text;
    QString t = text;
    if (page >= 0) {
        t.replace(QLatin1String("<$p>"), QString::number(page));
        t.replace(QLatin1String("<$pages>"), QString::number(pages));
    }
    compile::Placeholders ph;
    const AppSettings &as = AppSettings::instance();
    ph.projectTitle = job.project ? job.project->title() : QString();
    ph.author = (as.forename + QLatin1Char(' ') + as.surname).trimmed();
    ph.forename = as.forename;
    ph.surname = as.surname;
    ph.sectionTitle = job.title;
    return ph.apply(t);
}

Job currentJob(EditorPane *pane, TextEditor *focus)
{
    Job j;
    if (!pane || !pane->project())
        return j;
    Project *proj = pane->project();
    j.project = proj;
    auto takeSelection = [&j](TextEditor *e) {
        if (!e || !e->item() || !e->textCursor().hasSelection())
            return;
        j.selectionItem = e->item();
        j.selectionStart = e->textCursor().selectionStart();
        j.selectionEnd = e->textCursor().selectionEnd();
    };
    // the keyboard in the copyholder's text: its document
    if (focus && focus == pane->focusedCopyholderEditor() && focus->item() && focus->item()->hasText()) {
        j.kind = Kind::Text;
        j.items = {focus->item()};
        j.levels = {1};
        j.title = focus->item()->title;
        takeSelection(focus);
        return j;
    }
    switch (pane->mode()) {
    case ViewMode::Single:
    case ViewMode::Combined: {
        const QList<BinderItem *> docs = pane->textView()->items();
        if (docs.isEmpty())
            break;
        j.kind = Kind::Text;
        j.items = docs;
        int top = 1000;
        for (BinderItem *it : docs)
            top = qMin(top, it->level());
        for (BinderItem *it : docs)
            j.levels << 1 + it->level() - top;
        const QList<BinderItem *> shown = pane->items();
        j.title = shown.size() == 1 ? shown.first()->title
                                    : (docs.size() == 1 ? docs.first()->title
                                                        : docs.first()->title + QStringLiteral(" – ") + docs.last()->title);
        if (focus && pane->textView()->editors().contains(focus))
            takeSelection(focus);
        break;
    }
    case ViewMode::Corkboard: {
        j.kind = Kind::Cards;
        j.items = pane->corkboard()->shownItems();
        j.selected = pane->corkboard()->selectedItems();
        QStringList titles;
        for (BinderItem *c : pane->corkboard()->containers())
            titles << c->title;
        j.title = titles.isEmpty() ? QObject::tr("Index Cards") : titles.join(QStringLiteral(", "));
        break;
    }
    case ViewMode::Outliner: {
        Outliner *o = pane->outliner();
        j.kind = Kind::Outliner;
        for (const auto &r : o->listedRows(false))
            j.rows << Row{r.first, r.second};
        for (const auto &r : o->listedRows(true))
            j.allRows << Row{r.first, r.second};
        double total = 0;
        const auto cols = o->shownColumns();
        for (const auto &c : cols)
            total += qMax(20, c.second);
        for (const auto &c : cols)
            j.columns << Column{c.first, qMax(20, c.second) / qMax(1.0, total)};
        j.synopses = o->showSynopsis();
        j.selected = o->selectedItems();
        QStringList titles;
        for (BinderItem *it : pane->items())
            titles << it->title;
        j.title = titles.join(QStringLiteral(", "));
        break;
    }
    case ViewMode::Media: {
        BinderItem *it = pane->items().value(0);
        if (!it)
            break;
        j.items = {it};
        j.title = it->title;
        if (it->type == ItemType::Image)
            j.kind = Kind::Picture;
        else if (it->type == ItemType::Pdf)
            j.kind = Kind::Pdf;
        else if (it->type == ItemType::WebPage)
            j.kind = Kind::WebPage;
        break;
    }
    case ViewMode::Empty: break;
    }
    if (j.kind == Kind::None)
        j.items.clear();
    return j;
}

QString describe(const Job &job)
{
    switch (job.kind) {
    case Kind::Text: return job.items.size() > 1 ? QObject::tr("the texts") : QObject::tr("the text");
    case Kind::Cards: return QObject::tr("the index cards");
    case Kind::Outliner: return QObject::tr("the outliner");
    case Kind::Picture: return QObject::tr("the picture");
    case Kind::Pdf: return QObject::tr("the PDF file");
    case Kind::WebPage: return QObject::tr("the web page");
    case Kind::None: break;
    }
    return QString();
}

void preparePrinter(QPrinter *printer, const Job &job, const PrintSettings &s)
{
    printer->setFullPage(true);
    printer->setDocName(job.title.isEmpty() ? QStringLiteral(QB_APP_NAME) : job.title);
    if (job.kind == Kind::Cards && s.cardPerPage) {
        // one card on each sheet: the sheet is as large as the card and lies as the card does
        const double w = s.cardWidth, h = s.cardHeight;
        const QPageSize size(QSizeF(qMin(w, h), qMax(w, h)), QPageSize::Millimeter, QString(), QPageSize::ExactMatch);
        printer->setPageLayout(QPageLayout(size, w > h ? QPageLayout::Landscape : QPageLayout::Portrait, QMarginsF(0, 0, 0, 0)));
        return;
    }
    const PageSetup pg = s.page.normalized();
    printer->setPageLayout(
        QPageLayout(pageSizeFor(pg), pg.landscape ? QPageLayout::Landscape : QPageLayout::Portrait, QMarginsF(0, 0, 0, 0)));
}

bool render(const Job &job, const PrintSettings &settings, QPrinter *printer, QString *error, int *pages)
{
    if (!job.project || job.kind == Kind::None) {
        if (error)
            *error = QObject::tr("There is nothing to print in this editor.");
        return false;
    }
    printer->setFullPage(true);
    const Range range = rangeOf(printer);
    switch (job.kind) {
    case Kind::Text: return renderText(job, settings, printer, range, error, pages);
    case Kind::Cards: return renderCards(job, settings, printer, range, error, pages);
    case Kind::Outliner: return renderOutliner(job, settings, printer, range, error, pages);
    case Kind::Picture: return renderPicture(job, settings, printer, range, error, pages);
    case Kind::Pdf: return renderPdf(job, settings, printer, range, error, pages);
    case Kind::WebPage: return renderWebPage(job, settings, printer, range, error, pages);
    case Kind::None: break;
    }
    return false;
}

} // namespace printing
} // namespace qb
