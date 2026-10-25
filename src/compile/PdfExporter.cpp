#include "compile/PdfExporter.h"

#include "compile/Look.h"

#include "AppInfo.h"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QFile>
#include <QFontMetricsF>
#include <QHash>
#include <QMap>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSet>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QUrl>
#include <algorithm>
#include <cmath>

namespace qb {
namespace compile {

namespace {

constexpr double kDpi = 1200.0;
constexpr double kScreenDpi = 96.0;
inline double mmPx(double mm) { return mm / 25.4 * kDpi; }
inline double ptPx(double pt) { return pt / 72.0 * kDpi; }
inline double pxPt(double px) { return px * 72.0 / kDpi; }
// Qt scales tab stops and fixed line heights to the paint device itself:
// they are given in pixels of a 96 dpi screen (left/right margins and the
// first line indent are not scaled and are given in device pixels).
inline double mmScreen(double mm) { return mm / 25.4 * kScreenDpi; }
inline double ptScreen(double pt) { return pt / 72.0 * kScreenDpi; }

const int kFootnoteProp = QTextFormat::UserProperty + 300;

struct BlockMeta {
    int para = -1;
    bool pageBreak = false;
    bool keepWithNext = false;
    bool chapterStart = false;
    bool tocSlot = false;          // the contents go before this block
    double before = 0, after = 0;  // device pixels
    int outlineLevel = 0;
    QString outlineText;
};

struct Placed {
    const QTextLayout *tl;
    int line;
    double y;                      // top of the line (page coordinates; notes: from the top of the note area)
};

struct PageOut {
    QList<Placed> lines;
    QList<Placed> notes;
    double notesHeight = 0;
    bool blank = false;
    bool chapterStart = false;
    bool toc = false;
    bool any = false;
};

struct TocEntry {
    int level;
    QString text;
    int block;      // block of the main document
    int page = 0;   // 0-based, filled by the page maker
    double y = 0;   // device pixels on that page
};

QTextBlockFormat blockFormat(const look::Para &a)
{
    QTextBlockFormat bf;
    bf.setAlignment(a.align);
    bf.setLeftMargin(mmPx(a.leftMm));
    bf.setRightMargin(mmPx(a.rightMm));
    bf.setTextIndent(mmPx(a.firstMm));
    bf.setTopMargin(0);
    bf.setBottomMargin(0);
    if (a.lineSpacing > 0)
        bf.setLineHeight(a.lineSpacing * 100.0, QTextBlockFormat::ProportionalHeight);
    else if (a.lineHeightPt > 0)
        bf.setLineHeight(ptScreen(a.lineHeightPt), a.lineAtLeast ? QTextBlockFormat::MinimumHeight : QTextBlockFormat::FixedHeight);
    if (!a.tabs.isEmpty()) {
        QList<QTextOption::Tab> tabs;
        const double start = a.leftMm + a.firstMm;
        for (const TabStop &t : a.tabs) {
            QTextOption::Tab tab;
            tab.position = mmScreen(qMax(0.0, t.positionMm - start));
            tab.type = t.type == TabStop::Right    ? QTextOption::RightTab
                       : t.type == TabStop::Center ? QTextOption::CenterTab
                       : t.type == TabStop::Decimal ? QTextOption::DelimiterTab
                                                    : QTextOption::LeftTab;
            if (t.type == TabStop::Decimal)
                tab.delimiter = QLatin1Char('.');
            tabs << tab;
        }
        bf.setTabPositions(tabs);
    }
    return bf;
}

struct Builder {
    const Manuscript &m;
    QPaintDevice *dev;
    double textW, textH;
    QTextDocument doc;
    QTextDocument notes;           // footnotes at the bottom of the page
    QList<BlockMeta> meta;
    QHash<int, int> noteNumber;    // footnote index -> number shown
    QHash<int, QPair<int, int>> noteBlocks; // footnote index -> first, last block in "notes"
    QList<int> pendingEndnotes;    // footnotes waiting for the next notes section
    QHash<QPair<int, int>, int> listCounters;
    int noteCounter = 0;
    int images = 0;
    bool firstBlock = true;
    bool firstNoteBlock = true;
    QList<TocEntry> toc;

    Builder(const Manuscript &m, QPaintDevice *d, double w, double h) : m(m), dev(d), textW(w), textH(h)
    {
        for (QTextDocument *x : {&doc, &notes}) {
            x->documentLayout()->setPaintDevice(dev);
            x->setDocumentMargin(0);
            x->setTextWidth(textW);
            x->setUndoRedoEnabled(false);
            QFont f(m.fontFamily);
            f.setPointSizeF(m.fontSize);
            x->setDefaultFont(f);
        }
    }

    QTextCharFormat charFormat(const look::Para &pl, const Run &r)
    {
        QTextCharFormat cf;
        cf.setFont(look::runFont(pl, r));
        if (r.color.isValid())
            cf.setForeground(r.color);
        if (r.highlight.isValid())
            cf.setBackground(r.highlight);
        if (r.superscript)
            cf.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
        else if (r.subscript)
            cf.setVerticalAlignment(QTextCharFormat::AlignSubScript);
        if (!r.link.isEmpty() && m.options.pdfLinks) {
            cf.setAnchor(true);
            cf.setAnchorHref(r.link);
            if (!r.color.isValid())
                cf.setForeground(QColor(0x05, 0x63, 0xC1));
            cf.setFontUnderline(true);
        }
        return cf;
    }

    void insertRuns(QTextDocument *d, QTextCursor &c, const look::Para &pl, const QList<Run> &runs, bool refs)
    {
        for (const Run &r : runs) {
            switch (r.kind) {
            case Run::Text:
                if (!r.text.isEmpty())
                    c.insertText(r.text, charFormat(pl, r));
                break;
            case Run::Tab: c.insertText(QStringLiteral("\t"), charFormat(pl, r)); break;
            case Run::LineBreak: c.insertText(QString(QChar::LineSeparator), charFormat(pl, r)); break;
            case Run::FootnoteRef: {
                if (!refs || r.footnote < 0 || r.footnote >= m.footnotes.size())
                    break;
                const int n = ++noteCounter;
                noteNumber.insert(r.footnote, n);
                QTextCharFormat cf = charFormat(pl, Run());
                cf.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
                cf.setProperty(kFootnoteProp, r.footnote);
                c.insertText(QString::number(n), cf);
                if (m.options.footnotePlacement == 0)
                    addFootnote(r.footnote, n);
                else
                    pendingEndnotes << r.footnote;
                break;
            }
            case Run::Image: {
                if (r.image.isNull())
                    break;
                double w = ptPx(r.imageSize.width() > 0 ? r.imageSize.width() : r.image.width() * 0.75);
                double h = ptPx(r.imageSize.height() > 0 ? r.imageSize.height() : r.image.height() * 0.75);
                const double maxW = textW - mmPx(pl.leftMm + pl.rightMm), maxH = textH * 0.85;
                if (w > maxW) {
                    h *= maxW / w;
                    w = maxW;
                }
                if (h > maxH) {
                    w *= maxH / h;
                    h = maxH;
                }
                const QUrl url(QStringLiteral("pdfimg:%1").arg(++images));
                d->addResource(QTextDocument::ImageResource, url, r.image);
                QTextImageFormat imf;
                imf.setName(url.toString());
                // image sizes are scaled by Qt like tab stops (screen pixels)
                imf.setWidth(w * kScreenDpi / kDpi);
                imf.setHeight(h * kScreenDpi / kDpi);
                c.insertImage(imf);
                break;
            }
            }
        }
    }

    void addFootnote(int index, int number)
    {
        const Footnote &fn = m.footnotes[index];
        Manuscript small = m;
        small.fontSize = look::footnoteSize(m);
        small.lineSpacing = 1.0;
        QTextCursor c(&notes);
        c.movePosition(QTextCursor::End);
        int first = -1;
        for (int i = 0; i < fn.paragraphs.size(); ++i) {
            Paragraph p = fn.paragraphs[i];
            p.style = Paragraph::Normal;
            look::Para pl = look::paragraph(small, p);
            pl.firstMm = 0;
            pl.afterPt = 0;
            pl.beforePt = 0;
            pl.align = Qt::AlignLeft;
            if (firstNoteBlock) {
                c.setBlockFormat(blockFormat(pl));
                firstNoteBlock = false;
            } else {
                c.insertBlock(blockFormat(pl));
            }
            if (first < 0)
                first = c.block().blockNumber();
            if (i == 0) {
                QTextCharFormat cf = charFormat(pl, Run());
                cf.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
                c.insertText(QString::number(number), cf);
                c.insertText(QStringLiteral(" "), charFormat(pl, Run()));
            }
            insertRuns(&notes, c, pl, p.runs, false);
        }
        if (first < 0) {
            // an empty footnote still shows its number
            if (firstNoteBlock)
                firstNoteBlock = false;
            else
                c.insertBlock();
            first = c.block().blockNumber();
            c.insertText(QString::number(number));
        }
        noteBlocks.insert(index, {first, c.block().blockNumber()});
    }

    void newBlock(QTextCursor &c, const look::Para &pl, BlockMeta bm)
    {
        bm.before = ptPx(pl.beforePt);
        bm.after = ptPx(pl.afterPt);
        bm.keepWithNext = bm.keepWithNext || pl.keepWithNext;
        if (firstBlock) {
            c.setBlockFormat(blockFormat(pl));
            firstBlock = false;
        } else {
            c.insertBlock(blockFormat(pl));
        }
        meta << bm;
    }

    void flushEndnotes(QTextCursor &c, bool pageBreak)
    {
        if (pendingEndnotes.isEmpty())
            return;
        Paragraph hp;
        hp.style = Paragraph::Heading2;
        look::Para hl = look::paragraph(m, hp);
        BlockMeta bm;
        bm.pageBreak = pageBreak;
        bm.keepWithNext = true;
        newBlock(c, hl, bm);
        Run t;
        t.text = m.options.endnotesTitle;
        insertRuns(&doc, c, hl, {t}, false);
        for (int index : std::as_const(pendingEndnotes)) {
            const Footnote &fn = m.footnotes[index];
            for (int i = 0; i < qMax(1, int(fn.paragraphs.size())); ++i) {
                Paragraph p = i < fn.paragraphs.size() ? fn.paragraphs[i] : Paragraph();
                p.style = Paragraph::Normal;
                Manuscript small = m;
                small.fontSize = look::footnoteSize(m);
                look::Para pl = look::paragraph(small, p);
                pl.firstMm = 0;
                newBlock(c, pl, BlockMeta());
                if (i == 0)
                    c.insertText(QString::number(noteNumber.value(index)) + QStringLiteral(". "), charFormat(pl, Run()));
                insertRuns(&doc, c, pl, p.runs, false);
            }
        }
        pendingEndnotes.clear();
    }

    // the paragraph that begins the section of paragraph i: the empty lines
    // before a heading belong to it
    int sectionStart(int i) const
    {
        while (i > 0 && !m.paragraphs[i].pageBreakBefore) {
            const Paragraph &prev = m.paragraphs[i - 1];
            bool empty = prev.plainText().trimmed().isEmpty();
            for (const Run &r : prev.runs)
                empty = empty && r.kind != Run::Image;
            if (!empty)
                break;
            --i;
        }
        return i;
    }

    void build()
    {
        QTextCursor c(&doc);
        // where the contents go, and which page breaks start chapters
        int tocAt = -1;
        QSet<int> chapterStarts;
        for (int i = 0; i < m.paragraphs.size(); ++i) {
            if (m.paragraphs[i].outlineLevel != 1)
                continue;
            const int start = sectionStart(i);
            if (tocAt < 0 && m.options.toc)
                tocAt = start;
            if (m.paragraphs[start].pageBreakBefore || start == 0)
                chapterStarts.insert(start);
        }
        bool tocPlaced = false;
        for (int i = 0; i < m.paragraphs.size(); ++i) {
            const Paragraph &p = m.paragraphs[i];
            const look::Para pl = look::paragraph(m, p);
            BlockMeta bm;
            bm.para = i;
            bm.pageBreak = p.pageBreakBefore;
            bm.chapterStart = chapterStarts.contains(i);
            bm.outlineLevel = p.outlineLevel;
            bm.outlineText = p.tocText.isEmpty() ? p.plainText().simplified() : p.tocText.simplified();
            if (bm.chapterStart && m.options.footnotePlacement == 1)
                flushEndnotes(c, true);
            if (i == tocAt) {
                bm.tocSlot = true;
                tocPlaced = true;
            }
            newBlock(c, pl, bm);
            if (p.outlineLevel > 0 && p.outlineLevel <= m.options.tocLevels && !bm.outlineText.isEmpty())
                toc.append({p.outlineLevel, bm.outlineText, c.block().blockNumber()});
            // list markers are text: "•" or "3." and a tab to the text
            if (p.style == Paragraph::ListBullet || p.style == Paragraph::ListNumber) {
                QTextBlockFormat bf = c.blockFormat();
                QTextOption::Tab tab;
                tab.position = mmScreen(-pl.firstMm);
                bf.setTabPositions({tab});
                c.setBlockFormat(bf);
                QString marker = QString(QChar(0x2022));
                if (p.style == Paragraph::ListNumber)
                    marker = QString::number(++listCounters[{p.listId, p.listLevel}]) + QLatin1Char('.');
                c.insertText(marker + QLatin1Char('\t'), charFormat(pl, Run()));
            }
            insertRuns(&doc, c, pl, p.runs, true);
        }
        if (m.options.toc && !tocPlaced && !toc.isEmpty()) {
            // headings without a level-1 entry: the contents at the first entry
            for (TocEntry &e : toc) {
                meta[e.block].tocSlot = true;
                break;
            }
        }
        if (!pendingEndnotes.isEmpty())
            flushEndnotes(c, m.options.footnotePlacement == 2 || m.options.footnotePlacement == 1);
        doc.documentLayout()->documentSize(); // lays out every block
        notes.documentLayout()->documentSize();
    }
};

double lineAdvance(const QTextBlock &b, int i)
{
    const QTextLayout *tl = b.layout();
    const QTextLine line = tl->lineAt(i);
    if (i + 1 < tl->lineCount())
        return tl->lineAt(i + 1).y() - line.y();
    return b.blockFormat().lineHeight(line.height(), kDpi / kScreenDpi);
}

double blockHeight(const QTextBlock &b)
{
    double h = 0;
    for (int i = 0; i < b.layout()->lineCount(); ++i)
        h += lineAdvance(b, i);
    return h;
}

// ---------------------------------------------------------------- PDF objects

QByteArray pdfText(const QString &s)
{
    // UTF-16BE with byte order mark, as a hex string
    QByteArray o = "<FEFF";
    for (const QChar ch : s)
        o += QByteArray::number(ch.unicode(), 16).rightJustified(4, '0').toUpper();
    return o + '>';
}

} // namespace

bool addPdfNavigation(const QString &path, double pageHeightPt, const QList<PdfOutlineItem> &outline, const QList<PdfLink> &links,
                      QString *error)
{
    return addPdfNavigationEx(path, pageHeightPt, outline, links, {}, error);
}

bool addPdfNavigationEx(const QString &path, double pageHeightPt, const QList<PdfOutlineItem> &outline,
                        const QList<PdfLink> &links, const QMap<QString, QString> &info, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const QByteArray d = f.readAll();
    f.close();
    auto fail = [&](const QString &why) {
        if (error)
            *error = QObject::tr("The PDF could not be completed (%1).").arg(why);
        return false;
    };
    const int sx = d.lastIndexOf("startxref");
    if (sx < 0)
        return fail(QStringLiteral("startxref"));
    const qint64 prevXref = d.mid(sx + 9, 30).trimmed().split('\n').first().trimmed().toLongLong();
    const int tr = d.lastIndexOf("trailer");
    if (tr < 0)
        return fail(QStringLiteral("trailer"));
    const QByteArray trailer = d.mid(tr, sx - tr);
    static const QRegularExpression sizeRe(QStringLiteral("/Size\\s+(\\d+)"));
    static const QRegularExpression rootRe(QStringLiteral("/Root\\s+(\\d+)\\s+0\\s+R"));
    static const QRegularExpression infoRe(QStringLiteral("/Info\\s+(\\d+)\\s+0\\s+R"));
    const QString trs = QString::fromLatin1(trailer);
    const auto sm = sizeRe.match(trs), rm = rootRe.match(trs), im = infoRe.match(trs);
    if (!sm.hasMatch() || !rm.hasMatch())
        return fail(QStringLiteral("trailer"));
    int nextId = sm.captured(1).toInt();
    const int rootId = rm.captured(1).toInt();
    const int infoId = im.hasMatch() ? im.captured(1).toInt() : 0;

    // the objects of the file (Qt writes them uncompressed, one after another)
    QHash<int, QByteArray> objs;
    {
        static const QRegularExpression objRe(QStringLiteral("(?:^|\\n)(\\d+) 0 obj"));
        const QString ds = QString::fromLatin1(d);
        auto it = objRe.globalMatch(ds);
        while (it.hasNext()) {
            const auto mm = it.next();
            const int start = mm.capturedEnd();
            const int end = d.indexOf("endobj", start);
            if (end > start)
                objs.insert(mm.captured(1).toInt(), d.mid(start, end - start).trimmed());
        }
    }
    const QByteArray root = objs.value(rootId);
    static const QRegularExpression pagesRe(QStringLiteral("/Pages\\s+(\\d+)\\s+0\\s+R"));
    const auto pm = pagesRe.match(QString::fromLatin1(root));
    if (!pm.hasMatch())
        return fail(QStringLiteral("pages"));
    const QByteArray pagesObj = objs.value(pm.captured(1).toInt());
    QList<int> pageIds;
    {
        const int k = pagesObj.indexOf("/Kids");
        const int a = pagesObj.indexOf('[', k), b = pagesObj.indexOf(']', a);
        if (k < 0 || a < 0 || b < 0)
            return fail(QStringLiteral("kids"));
        static const QRegularExpression refRe(QStringLiteral("(\\d+)\\s+0\\s+R"));
        auto it = refRe.globalMatch(QString::fromLatin1(pagesObj.mid(a, b - a)));
        while (it.hasNext())
            pageIds << it.next().captured(1).toInt();
    }
    auto dest = [&](const PdfDest &x) -> QByteArray {
        const int pid = pageIds.value(qBound(0, x.page, int(pageIds.size()) - 1));
        return "[" + QByteArray::number(pid) + " 0 R /XYZ 0 " + QByteArray::number(pageHeightPt - x.y, 'f', 2) + " 0]";
    };

    QMap<int, QByteArray> out; // id -> object body
    // bookmarks: a tree from the levels
    if (!outline.isEmpty()) {
        const int rootOutline = nextId++;
        struct Node {
            int id;
            int parent;       // index, -1 = root
            QList<int> kids;  // indexes
        };
        QList<Node> nodes;
        QList<int> stack;     // indexes of the open ancestors
        QList<int> top;
        for (int i = 0; i < outline.size(); ++i) {
            const int level = qMax(1, outline[i].level);
            while (!stack.isEmpty() && qMax(1, outline[stack.last()].level) >= level)
                stack.removeLast();
            Node n{nextId++, stack.isEmpty() ? -1 : stack.last(), {}};
            nodes << n;
            if (n.parent < 0)
                top << i;
            else
                nodes[n.parent].kids << i;
            stack << i;
        }
        std::function<int(int)> count = [&](int i) {
            int c = 0;
            for (int k : nodes[i].kids)
                c += 1 + count(k);
            return c;
        };
        auto siblings = [&](int i) -> const QList<int> & { return nodes[i].parent < 0 ? top : nodes[nodes[i].parent].kids; };
        for (int i = 0; i < nodes.size(); ++i) {
            const QList<int> &sib = siblings(i);
            const int pos = sib.indexOf(i);
            QByteArray o = "<< /Title " + pdfText(outline[i].title) + " /Parent "
                           + QByteArray::number(nodes[i].parent < 0 ? rootOutline : nodes[nodes[i].parent].id) + " 0 R";
            if (pos > 0)
                o += " /Prev " + QByteArray::number(nodes[sib[pos - 1]].id) + " 0 R";
            if (pos + 1 < sib.size())
                o += " /Next " + QByteArray::number(nodes[sib[pos + 1]].id) + " 0 R";
            if (!nodes[i].kids.isEmpty())
                o += " /First " + QByteArray::number(nodes[nodes[i].kids.first()].id) + " 0 R /Last "
                     + QByteArray::number(nodes[nodes[i].kids.last()].id) + " 0 R /Count " + QByteArray::number(count(i));
            o += " /Dest " + dest(outline[i].dest) + " >>";
            out.insert(nodes[i].id, o);
        }
        out.insert(rootOutline, "<< /Type /Outlines /First " + QByteArray::number(nodes[top.first()].id) + " 0 R /Last "
                                    + QByteArray::number(nodes[top.last()].id) + " 0 R /Count "
                                    + QByteArray::number(nodes.size()) + " >>");
        QByteArray cat = root;
        cat.chop(cat.endsWith(">>") ? 2 : 0);
        cat += " /Outlines " + QByteArray::number(rootOutline) + " 0 R /PageMode /UseOutlines >>";
        out.insert(rootId, cat);
    }
    // links: annotations added to the pages' /Annots arrays
    QMap<int, QList<int>> annotsOfPage;
    for (const PdfLink &l : links) {
        const int id = nextId++;
        const double y1 = pageHeightPt - l.y2, y2 = pageHeightPt - l.y1;
        out.insert(id, "<< /Type /Annot /Subtype /Link /Rect [" + QByteArray::number(l.x1, 'f', 2) + ' '
                           + QByteArray::number(y1, 'f', 2) + ' ' + QByteArray::number(l.x2, 'f', 2) + ' '
                           + QByteArray::number(y2, 'f', 2) + "] /Border [0 0 0] /Dest " + dest(l.dest) + " >>");
        annotsOfPage[l.page] << id;
    }
    static const QRegularExpression annotsRe(QStringLiteral("/Annots\\s+(\\d+)\\s+0\\s+R"));
    for (auto it = annotsOfPage.cbegin(); it != annotsOfPage.cend(); ++it) {
        const int pid = pageIds.value(it.key());
        const QByteArray page = objs.value(pid);
        QByteArray add;
        for (int id : it.value())
            add += ' ' + QByteArray::number(id) + " 0 R";
        const auto am = annotsRe.match(QString::fromLatin1(page));
        if (am.hasMatch()) {
            const int aid = am.captured(1).toInt();
            QByteArray arr = out.contains(aid) ? out.value(aid) : objs.value(aid);
            const int close = arr.lastIndexOf(']');
            if (close < 0)
                continue;
            arr.insert(close, add + ' ');
            out.insert(aid, arr);
        } else {
            QByteArray pg = out.contains(pid) ? out.value(pid) : page;
            const int ai = pg.indexOf("/Annots");
            if (ai >= 0) {
                const int close = pg.indexOf(']', ai);
                pg.insert(close, add + ' ');
            } else {
                pg.chop(pg.endsWith(">>") ? 2 : 0);
                pg += " /Annots [" + add + " ] >>";
            }
            out.insert(pid, pg);
        }
    }
    // document information: author, subject, keywords
    if (!info.isEmpty() && infoId > 0) {
        QByteArray inf = objs.value(infoId);
        inf.chop(inf.endsWith(">>") ? 2 : 0);
        for (auto it = info.cbegin(); it != info.cend(); ++it)
            if (!it.value().isEmpty() && !inf.contains("/" + it.key().toLatin1() + " "))
                inf += "\n/" + it.key().toLatin1() + ' ' + pdfText(it.value());
        inf += "\n>>";
        out.insert(infoId, inf);
    }
    if (out.isEmpty())
        return true;

    QByteArray app = "\n";
    QMap<int, qint64> offsets;
    for (auto it = out.cbegin(); it != out.cend(); ++it) {
        offsets.insert(it.key(), d.size() + app.size());
        app += QByteArray::number(it.key()) + " 0 obj\n" + it.value() + "\nendobj\n";
    }
    const qint64 xrefPos = d.size() + app.size();
    app += "xref\n";
    QList<int> ids = offsets.keys();
    for (int i = 0; i < ids.size();) {
        int j = i;
        while (j + 1 < ids.size() && ids[j + 1] == ids[j] + 1)
            ++j;
        app += QByteArray::number(ids[i]) + ' ' + QByteArray::number(j - i + 1) + '\n';
        for (int k = i; k <= j; ++k)
            app += QByteArray::number(offsets.value(ids[k])).rightJustified(10, '0') + " 00000 n \n";
        i = j + 1;
    }
    app += "trailer\n<< /Size " + QByteArray::number(qMax(nextId, ids.last() + 1)) + " /Root " + QByteArray::number(rootId) + " 0 R";
    if (infoId > 0)
        app += " /Info " + QByteArray::number(infoId) + " 0 R";
    app += " /Prev " + QByteArray::number(prevXref) + " >>\nstartxref\n" + QByteArray::number(xrefPos) + "\n%%EOF\n";
    if (!f.open(QIODevice::Append)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(app);
    f.close();
    return true;
}

namespace {

// Lays the manuscript out on pages and paints them onto `out`. The text is laid
// out for `layoutDev` (1200 dpi) and painted with the painter scaled to the
// resolution of `out` (the same device for a PDF file; a printer may differ).
// Pages outside the range are laid out but not painted.
bool makePages(const Manuscript &m, QPaintDevice *layoutDev, QPagedPaintDevice *out, const PaintRange &range,
               QList<PdfOutlineItem> *outlineOut, QList<PdfLink> *linksOut, PdfReport *report, QString *error)
{
    const PageSettings &pg = m.page;
    const double pageW = mmPx(pg.widthMm), pageH = mmPx(pg.heightMm);
    const double top = mmPx(pg.marginTopMm), bottom = mmPx(pg.marginBottomMm);
    const double textW = pageW - mmPx(pg.marginLeftMm) - mmPx(pg.marginRightMm);
    const double textH = pageH - top - bottom;
    if (textW < mmPx(20) || textH < mmPx(20)) {
        if (error)
            *error = QObject::tr("The page margins leave no room for the text.");
        return false;
    }

    Builder b(m, layoutDev, textW, textH);
    b.build();

    // ------------------------------------------------ contents: how many pages
    QFont tocFont(m.fontFamily);
    tocFont.setPointSizeF(m.fontSize);
    QFont tocTitleFont(m.headingFontFamily.isEmpty() ? m.fontFamily : m.headingFontFamily);
    tocTitleFont.setPointSizeF(m.fontSize * qMax(1.2, m.headingScale));
    tocTitleFont.setBold(m.headingBold);
    const QFontMetricsF tocFm(tocFont, layoutDev), tocTitleFm(tocTitleFont, layoutDev);
    const double tocLine = tocFm.lineSpacing() * 1.5;
    const double tocTitleH = tocTitleFm.lineSpacing() * 3;
    int tocPages = 0;
    if (m.options.toc && !b.toc.isEmpty()) {
        const int first = int((textH - tocTitleH) / tocLine), per = int(textH / tocLine);
        const int rest = qMax(0, int(b.toc.size()) - first);
        tocPages = 1 + (rest + per - 1) / qMax(1, per);
    }

    // ------------------------------------------------ the page maker
    QList<PageOut> pages;
    pages.append(PageOut());
    double cur = 0;              // used height of the text area
    QList<Placed> carryNotes;    // note lines that did not fit on the page before
    double sepH = ptPx(look::footnoteSize(m)) * 1.2;
    QHash<int, int> tocPageOfBlock;
    QHash<int, double> tocYOfBlock;

    // note lines are kept as (block, line) of the notes document
    struct NoteLine {
        QTextBlock block;
        int line;
        double h;
    };
    QList<NoteLine> carry;
    auto notesTotal = [&](const PageOut &p) { return p.notesHeight > 0 ? p.notesHeight + sepH : 0.0; };
    auto newPage = [&]() {
        pages.append(PageOut());
        cur = 0;
        // notes that went over: first on the new page
        PageOut &p = pages.last();
        QList<NoteLine> still;
        for (const NoteLine &n : std::as_const(carry)) {
            if (still.isEmpty() && cur + p.notesHeight + n.h + sepH <= textH * 0.6) {
                p.notes.append({n.block.layout(), n.line, p.notesHeight});
                p.notesHeight += n.h;
            } else {
                still << n;
            }
        }
        carry = still;
    };
    auto pageEmpty = [&]() { return !pages.last().any; };
    // notes of a line: their lines and height
    auto notesOfLine = [&](const QTextBlock &blk, const QTextLine &line, QList<NoteLine> *lines) {
        double h = 0;
        const int from = blk.position() + line.textStart(), to = from + line.textLength();
        for (auto it = blk.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            if (fr.position() < from || fr.position() >= to || !fr.charFormat().hasProperty(kFootnoteProp))
                continue;
            const int index = fr.charFormat().intProperty(kFootnoteProp);
            if (!b.noteBlocks.contains(index))
                continue;
            const auto range = b.noteBlocks.value(index);
            for (int bn = range.first; bn <= range.second; ++bn) {
                const QTextBlock nb = b.notes.findBlockByNumber(bn);
                for (int li = 0; li < nb.layout()->lineCount(); ++li) {
                    const double lh = lineAdvance(nb, li);
                    h += lh;
                    if (lines)
                        lines->append({nb, li, lh});
                }
            }
        }
        return h;
    };

    const QTextBlock lastBlock = b.doc.lastBlock();
    int bi = 0;
    for (QTextBlock blk = b.doc.begin(); blk.isValid(); blk = blk.next(), ++bi) {
        const BlockMeta &bm = b.meta.value(bi);
        if (bm.tocSlot && tocPages > 0) {
            if (!pageEmpty())
                newPage();
            // a page of contents is a page of its own
            for (int t = 0; t < tocPages; ++t) {
                if (t > 0)
                    newPage();
                pages.last().toc = true;
                pages.last().any = true;
            }
            newPage();
        }
        if (bm.pageBreak && !pageEmpty())
            newPage();
        if (bm.chapterStart && pg.chaptersOnRightPage && pageEmpty() && (pages.size() - 1) % 2 == 1) {
            // a chapter starts on a right-hand (odd) page: this one stays empty
            pages.last().blank = true;
            pages.last().any = true;
            newPage();
        }
        if (bm.chapterStart && pageEmpty())
            pages.last().chapterStart = true;
        QTextLayout *tl = blk.layout();
        const int n = tl->lineCount();
        double before = pageEmpty() ? 0 : bm.before;
        // a heading stays with the start of what follows
        if (bm.keepWithNext && !pageEmpty() && blk != lastBlock) {
            const QTextBlock next = blk.next();
            double need = before + blockHeight(blk);
            const BlockMeta &nm = b.meta.value(bi + 1);
            need += nm.before;
            for (int i = 0; i < qMin(2, next.layout()->lineCount()); ++i)
                need += lineAdvance(next, i);
            if (cur + need + notesTotal(pages.last()) > textH) {
                newPage();
                before = 0;
            }
        }
        int k = 0;
        while (k < n) {
            const QTextLine line = tl->lineAt(k);
            const double h = lineAdvance(blk, k);
            QList<NoteLine> nl;
            const double nh = notesOfLine(blk, line, &nl);
            const double sepNeed = (nh > 0 && pages.last().notesHeight <= 0) ? sepH : 0;
            const double extra = k == 0 ? before : 0;
            if (cur + extra + h + notesTotal(pages.last()) + nh + sepNeed > textH && !pageEmpty()) {
                // no single last line at the top of the next page, no single first line at the bottom
                PageOut &p = pages.last();
                const bool prevNotes = k > 0 && notesOfLine(blk, tl->lineAt(k - 1), nullptr) > 0;
                if (k == 1 && n >= 2 && !prevNotes && p.lines.size() > 1 && p.lines.last().tl == tl) {
                    p.lines.removeLast();
                    k = 0;
                } else if (k == n - 1 && k >= 2 && !prevNotes && !p.lines.isEmpty() && p.lines.last().tl == tl) {
                    p.lines.removeLast();
                    --k;
                }
                newPage();
                before = 0;
                continue;
            }
            cur += extra;
            PageOut &p = pages.last();
            p.lines.append({tl, k, top + cur});
            p.any = true;
            if (k == 0 && bm.outlineLevel > 0) {
                tocPageOfBlock.insert(bi, pages.size() - 1);
                tocYOfBlock.insert(bi, top + cur);
            }
            cur += h;
            // its notes: as far as they fit, the rest on the next page
            for (const NoteLine &x : std::as_const(nl)) {
                if (carry.isEmpty() && cur + notesTotal(p) + x.h + (p.notesHeight <= 0 ? sepH : 0) <= textH) {
                    p.notes.append({x.block.layout(), x.line, p.notesHeight});
                    p.notesHeight += x.h;
                } else {
                    carry << x;
                }
            }
            ++k;
        }
        cur += bm.after;
    }
    while (!carry.isEmpty()) {
        newPage();
        pages.last().any = true;
        if (pages.last().notes.isEmpty() && !carry.isEmpty()) {
            // a note longer than a page: take its lines anyway
            const NoteLine x = carry.takeFirst();
            pages.last().notes.append({x.block.layout(), x.line, pages.last().notesHeight});
            pages.last().notesHeight += x.h;
        }
    }
    while (pages.size() > 1 && !pages.last().any)
        pages.removeLast();

    // ------------------------------------------------ painting
    QPainter painter;
    if (!painter.begin(out)) {
        if (error)
            *error = QObject::tr("The pages could not be painted.");
        return false;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    const double scale = out->logicalDpiX() / kDpi;
    if (qAbs(scale - 1.0) > 1e-6)
        painter.scale(scale, out->logicalDpiY() / kDpi);
    QFont hf(m.fontFamily);
    hf.setPointSizeF(look::headerSize(m));
    const QFontMetricsF hfm(hf, layoutDev);
    QList<PdfOutlineItem> outline;
    QList<PdfLink> links;
    int tocDrawn = 0;
    int tocPageCount = 0;
    bool anyPainted = false;
    for (int copy = 0; copy < qMax(1, range.copies); ++copy) {
    tocDrawn = 0;
    tocPageCount = 0;
    for (int i = 0; i < pages.size(); ++i) {
        const bool inRange = (range.fromPage <= 0 || i + 1 >= range.fromPage) && (range.toPage <= 0 || i + 1 <= range.toPage);
        const PageOut &p = pages[i];
        if (!inRange) {
            // a page of contents that is left out still takes its entries
            if (p.toc) {
                double y = top + (tocPageCount == 0 ? tocTitleH : 0);
                ++tocPageCount;
                while (tocDrawn < b.toc.size() && y + tocLine <= top + textH + 1) {
                    const TocEntry &e = b.toc[tocDrawn++];
                    if (tocPageOfBlock.value(e.block, -1) >= 0)
                        y += tocLine;
                }
            }
            continue;
        }
        if (anyPainted)
            out->newPage();
        anyPainted = true;
        const bool verso = pg.mirrorMargins && (i % 2 == 1);
        const double left = mmPx(verso ? pg.marginRightMm : pg.marginLeftMm);
        if (p.blank)
            continue;
        for (const Placed &pl : p.lines) {
            const QTextLine line = pl.tl->lineAt(pl.line);
            const QPointF origin(left + pl.tl->position().x(), pl.y - line.y());
            line.draw(&painter, origin); // pictures in the line included
        }
        if (!p.notes.isEmpty()) {
            const double noteTop = top + textH - p.notesHeight;
            painter.save();
            QPen pen(Qt::black);
            pen.setWidthF(ptPx(0.5));
            painter.setPen(pen);
            const double ly = noteTop - sepH * 0.5;
            painter.drawLine(QPointF(left, ly), QPointF(left + textW * 0.3, ly));
            painter.restore();
            for (const Placed &pl : p.notes) {
                const QTextLine line = pl.tl->lineAt(pl.line);
                line.draw(&painter, QPointF(left + pl.tl->position().x(), noteTop + pl.y - line.y()));
            }
        }
        if (p.toc) {
            // the contents: title, entries with dots and page numbers
            painter.save();
            double y = top;
            if (tocPageCount == 0) {
                painter.setFont(tocTitleFont);
                painter.drawText(QRectF(left, y, textW, tocTitleFm.lineSpacing() * 1.5), Qt::AlignHCenter | Qt::AlignTop,
                                 m.options.tocTitle);
                y += tocTitleH;
            }
            ++tocPageCount;
            painter.setFont(tocFont);
            while (tocDrawn < b.toc.size() && y + tocLine <= top + textH + 1) {
                TocEntry &e = b.toc[tocDrawn++];
                const int pageIndex = tocPageOfBlock.value(e.block, -1);
                if (pageIndex < 0)
                    continue;
                const QString num = look::pageNumberText(pg.firstPageNumber + pageIndex, pg.pageNumberStyle);
                const double indent = mmPx(6) * (e.level - 1);
                const double numW = tocFm.horizontalAdvance(num);
                const double gap = tocFm.horizontalAdvance(QStringLiteral("   "));
                const QString title = tocFm.elidedText(e.text, Qt::ElideRight, textW - indent - numW - gap * 2);
                const double tw = tocFm.horizontalAdvance(title);
                const double base = y + tocFm.ascent();
                painter.drawText(QPointF(left + indent, base), title);
                painter.drawText(QPointF(left + textW - numW, base), num);
                // dot leaders
                const double dotW = tocFm.horizontalAdvance(QStringLiteral(". "));
                QString dots;
                const int count = int((textW - indent - tw - numW - gap * 2) / qMax(1.0, dotW));
                for (int d = 0; d < count; ++d)
                    dots += QStringLiteral(". ");
                painter.drawText(QPointF(left + textW - numW - gap - tocFm.horizontalAdvance(dots), base), dots);
                links.append({i, pxPt(left + indent), pxPt(y), pxPt(left + textW), pxPt(y + tocLine),
                              {pageIndex, pxPt(tocYOfBlock.value(e.block))}});
                y += tocLine;
            }
            painter.restore();
        }
        // header and footer
        const bool showHf = !(i == 0 && !pg.headerOnFirstPage) && !(p.chapterStart && !pg.headerOnChapterPages) && !p.toc;
        if (showHf) {
            const QString number = look::pageNumberText(pg.firstPageNumber + i, pg.pageNumberStyle);
            auto txt = [&](QString s) {
                s.replace(QLatin1String("<$p>"), number);
                s.replace(QLatin1String("<$pages>"), QString::number(pages.size()));
                return s;
            };
            QString hl = txt(pg.headerLeft), hc = txt(pg.headerCenter), hr = txt(pg.headerRight);
            QString fl = txt(pg.footerLeft), fc = txt(pg.footerCenter), fr = txt(pg.footerRight);
            if (pg.mirrorHeaders) {
                // facing pages: the left texts on left-hand pages, the right texts on right-hand pages
                if (i % 2 == 1) {
                    hr.clear();
                    fr.clear();
                } else {
                    hl.clear();
                    fl.clear();
                }
            }
            painter.save();
            painter.setFont(hf);
            const double hy = qMax(mmPx(4), top * 0.5 - hfm.height() * 0.5);
            const double fy = pageH - qMax(mmPx(4) + hfm.height(), bottom * 0.5 + hfm.height() * 0.5);
            const QRectF hr1(left, hy, textW, hfm.height()), fr1(left, fy, textW, hfm.height());
            painter.drawText(hr1, Qt::AlignLeft | Qt::AlignVCenter, hl);
            painter.drawText(hr1, Qt::AlignHCenter | Qt::AlignVCenter, hc);
            painter.drawText(hr1, Qt::AlignRight | Qt::AlignVCenter, hr);
            painter.drawText(fr1, Qt::AlignLeft | Qt::AlignVCenter, fl);
            painter.drawText(fr1, Qt::AlignHCenter | Qt::AlignVCenter, fc);
            painter.drawText(fr1, Qt::AlignRight | Qt::AlignVCenter, fr);
            painter.restore();
        }
    }
    }
    painter.end();

    // bookmarks of every heading
    int bi2 = 0;
    for (QTextBlock blk = b.doc.begin(); blk.isValid(); blk = blk.next(), ++bi2) {
        const BlockMeta &bm = b.meta.value(bi2);
        if (bm.outlineLevel <= 0 || bm.outlineText.isEmpty() || !tocPageOfBlock.contains(bi2))
            continue;
        outline.append({bm.outlineLevel, bm.outlineText, {tocPageOfBlock.value(bi2), pxPt(tocYOfBlock.value(bi2))}});
    }
    if (report) {
        report->pages = pages.size();
        for (const PdfOutlineItem &o : std::as_const(outline))
            report->outline.append({o.level, o.title, o.dest.page + 1});
        for (const PageOut &p : std::as_const(pages))
            for (const Placed &n : p.notes)
                if (n.line == 0)
                    ++report->footnotes;
        report->links = links.size();
    }
    if (outlineOut)
        *outlineOut = outline;
    if (linksOut)
        *linksOut = links;
    return true;
}

} // namespace

bool paintManuscript(const Manuscript &m, QPagedPaintDevice *device, const PaintRange &range, PdfReport *report, QString *error)
{
    // the text is laid out for a PDF device of 1200 dpi that is never written:
    // the same measures as a compiled PDF, whatever the printer's resolution
    QBuffer sink;
    sink.open(QIODevice::WriteOnly);
    QPdfWriter layout(&sink);
    layout.setResolution(int(kDpi));
    layout.setPageSize(QPageSize(QSizeF(m.page.widthMm, m.page.heightMm), QPageSize::Millimeter, QString(), QPageSize::ExactMatch));
    layout.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    return makePages(m, &layout, device, range, nullptr, nullptr, report, error);
}

bool writePdf(const Manuscript &m, const QString &path, QString *error, PdfReport *report)
{
    const PageSettings &pg = m.page;
    QPdfWriter writer(path);
    writer.setResolution(int(kDpi));
    writer.setPageSize(QPageSize(QSizeF(pg.widthMm, pg.heightMm), QPageSize::Millimeter, QString(), QPageSize::ExactMatch));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    writer.setTitle(m.title);
    writer.setCreator(QStringLiteral(QB_APP_NAME " " QB_APP_VERSION));
    QList<PdfOutlineItem> outline;
    QList<PdfLink> links;
    QString perr;
    if (!makePages(m, &writer, &writer, PaintRange(), &outline, &links, report, &perr)) {
        if (error)
            *error = perr.startsWith(QLatin1String("The pages could not")) ? QObject::tr("The PDF file could not be written:\n%1").arg(path)
                                                                          : perr;
        return false;
    }
    QMap<QString, QString> info;
    info.insert(QStringLiteral("Author"), m.author);
    info.insert(QStringLiteral("Subject"), m.metadata.value(QStringLiteral("subject")));
    info.insert(QStringLiteral("Keywords"), m.metadata.value(QStringLiteral("keywords")));
    QString err;
    if (!addPdfNavigationEx(path, pg.heightMm / 25.4 * 72.0, m.options.pdfBookmarks ? outline : QList<PdfOutlineItem>(), links,
                            info, &err)) {
        if (error)
            *error = err;
        return false;
    }
    return true;
}

} // namespace compile
} // namespace qb
