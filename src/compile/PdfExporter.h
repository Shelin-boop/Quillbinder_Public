#pragma once
// Built-in PDF output (no other program needed): the manuscript is laid out
// with Qt's text engine and placed on pages by a page maker of its own, which
// knows footnotes at the bottom of the page, headers and footers, chapters on
// right-hand pages, keeping headings with their text, no single lines at the
// top or bottom of a page, a table of contents and PDF bookmarks.
#include "compile/ExporterApi.h"

#include <QList>
#include <QMap>
#include <QPair>
#include <QString>

class QPagedPaintDevice;

namespace qb {
namespace compile {

struct PdfReport {
    int pages = 0;
    struct Entry {
        int level;
        QString title;
        int page;        // 1-based, as the reader counts
    };
    QList<Entry> outline; // the bookmarks (and the contents)
    int footnotes = 0;    // placed at page bottoms
    int links = 0;        // internal links (contents)
};

bool writePdf(const Manuscript &m, const QString &path, QString *error, PdfReport *report = nullptr);

// The same page maker for any paged device (File > Print Current Document
// paints onto a printer): the pages of m.page, laid out as for a PDF file and
// painted at the device's resolution. Pages outside fromPage..toPage (1-based,
// 0 = no limit) are laid out but not painted.
struct PaintRange {
    int fromPage = 0, toPage = 0;
    int copies = 1;     // the whole range this often (for devices that cannot make copies themselves)
};
bool paintManuscript(const Manuscript &m, QPagedPaintDevice *device, const PaintRange &range, PdfReport *report,
                     QString *error);

class PdfExporter : public QObject, public ExporterInterface
{
    Q_OBJECT
    Q_INTERFACES(qb::ExporterInterface)
public:
    using QObject::QObject;
    QString id() const override { return QStringLiteral("pdf"); }
    QString name() const override { return tr("PDF (.pdf)"); }
    QString fileExtension() const override { return QStringLiteral("pdf"); }
    bool exportManuscript(const Manuscript &m, const QString &path, QString *error) override
    {
        return writePdf(m, path, error);
    }
};

// Adds bookmarks and internal links to a PDF written by Qt (an incremental
// update at its end; the file stays valid as it was). Positions are in
// points from the top of the page.
struct PdfDest {
    int page;            // 0-based
    double y;
};
struct PdfOutlineItem {
    int level;
    QString title;
    PdfDest dest;
};
struct PdfLink {
    int page;            // where the link is
    double x1, y1, x2, y2; // rectangle, points from the top left
    PdfDest dest;
};
bool addPdfNavigation(const QString &path, double pageHeightPt, const QList<PdfOutlineItem> &outline,
                      const QList<PdfLink> &links, QString *error);
// the same, and entries of the document information (Author, Subject, Keywords)
bool addPdfNavigationEx(const QString &path, double pageHeightPt, const QList<PdfOutlineItem> &outline,
                        const QList<PdfLink> &links, const QMap<QString, QString> &info, QString *error);

} // namespace compile
} // namespace qb
