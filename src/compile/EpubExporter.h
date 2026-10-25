#pragma once
// Built-in e-book output: EPUB 3 (with an EPUB 2 table of contents for older
// readers), no other program needed. A file per section that starts on a new
// page, headings in the reader's table of contents, footnotes as pop-up notes
// or as notes at the end of the chapter or the book, pictures, a cover.
#include "compile/ExporterApi.h"

namespace qb {
namespace compile {

struct EpubReport {
    int files = 0;      // text files (chapters)
    int images = 0;
    int tocEntries = 0;
    int notes = 0;
};

bool writeEpub(const Manuscript &m, const QString &path, QString *error, EpubReport *report = nullptr);

class EpubExporter : public QObject, public ExporterInterface
{
    Q_OBJECT
    Q_INTERFACES(qb::ExporterInterface)
public:
    using QObject::QObject;
    QString id() const override { return QStringLiteral("epub"); }
    QString name() const override { return tr("E-book (.epub)"); }
    QString fileExtension() const override { return QStringLiteral("epub"); }
    bool exportManuscript(const Manuscript &m, const QString &path, QString *error) override { return writeEpub(m, path, error); }
};

} // namespace compile
} // namespace qb
