#include "text/PageLayout.h"

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QtMath>

// The margins of the root frame are the only way to get page margins out of
// Qt's text layout, and the public call (QTextFrame::setFrameFormat) records
// every change in the undo history: Undo would then step through zoom
// changes and take the page margins away. The format index is therefore set
// directly. This needs the private Qt headers of the Qt the program is built
// with; it runs on the same Qt (the one inside the AppImage, APP_RUNTIME).
#include <private/qtextdocument_p.h>

namespace qb {
namespace pagelayout {

namespace {
const char kMetrics[] = "qbPageMetrics";

struct Margins {
    double l, t, r, b;
};

Margins marginsOf(const QTextDocument *doc)
{
    const QTextFrameFormat f = doc->rootFrame()->frameFormat();
    return {f.leftMargin(), f.topMargin(), f.rightMargin(), f.bottomMargin()};
}

bool setMargins(QTextDocument *doc, const Margins &m)
{
    const Margins now = marginsOf(doc);
    if (qFuzzyCompare(now.l + 1, m.l + 1) && qFuzzyCompare(now.t + 1, m.t + 1) && qFuzzyCompare(now.r + 1, m.r + 1)
        && qFuzzyCompare(now.b + 1, m.b + 1))
        return false;
    QTextFrame *root = doc->rootFrame();
    QTextFrameFormat f = root->frameFormat();
    f.setLeftMargin(m.l);
    f.setTopMargin(m.t);
    f.setRightMargin(m.r);
    f.setBottomMargin(m.b);
    QTextFormatCollection *formats = QTextDocumentPrivate::get(doc)->formatCollection();
    formats->setObjectFormatIndex(root->objectIndex(), formats->indexForFormat(f));
    return true;
}
} // namespace

bool apply(QTextDocument *doc, const PageMetrics &m)
{
    if (!doc)
        return false;
    bool changed = setMargins(doc, {double(m.left), double(m.top + m.gapAbove()), double(m.right),
                                    double(m.bottom + m.gapBelow())});
    const QSizeF page(m.paperW, m.step());
    if (changed || doc->pageSize() != page) {
        doc->setProperty(kMetrics, QVariantList{m.paperW, m.paperH, m.left, m.right, m.top, m.bottom, m.gap});
        doc->setPageSize(page); // lays the text out again
        changed = true;
    }
    return changed;
}

bool clear(QTextDocument *doc, int width)
{
    if (!doc)
        return false;
    const double dm = doc->documentMargin();
    const bool margins = setMargins(doc, {dm, dm, dm, dm});
    if (!margins && doc->pageSize().height() <= 0)
        return false;
    doc->setProperty(kMetrics, QVariant());
    doc->setPageSize(QSizeF(width, -1));
    return true;
}

bool isPaged(const QTextDocument *doc) { return doc && doc->pageSize().height() > 0 && doc->property(kMetrics).isValid(); }

bool matches(const QTextDocument *doc, const PageMetrics &m)
{
    if (!isPaged(doc) || doc->pageSize() != QSizeF(m.paperW, m.step()))
        return false;
    const Margins now = marginsOf(doc);
    return qRound(now.l) == m.left && qRound(now.r) == m.right && qRound(now.t) == m.top + m.gapAbove()
           && qRound(now.b) == m.bottom + m.gapBelow();
}

PageMetrics metrics(const QTextDocument *doc)
{
    PageMetrics m;
    const QVariantList v = doc ? doc->property(kMetrics).toList() : QVariantList();
    if (v.size() == 7) {
        m.paperW = v[0].toInt();
        m.paperH = v[1].toInt();
        m.left = v[2].toInt();
        m.right = v[3].toInt();
        m.top = v[4].toInt();
        m.bottom = v[5].toInt();
        m.gap = v[6].toInt();
    }
    return m;
}

int pageCount(const QTextDocument *doc)
{
    if (!isPaged(doc))
        return 0;
    return qMax(1, doc->pageCount());
}

int textBottom(const QTextDocument *doc)
{
    if (!doc)
        return 0;
    const QRectF r = doc->documentLayout()->blockBoundingRect(doc->lastBlock());
    return qCeil(r.bottom());
}

} // namespace pagelayout
} // namespace qb
