#pragma once
// Built-in exporters for scripts: Fountain (.fountain) and Final Draft (.fdx).
// Paragraphs of script elements keep their role; headings become sections
// (Fountain) or act headings (Final Draft), other text becomes action.
#include "compile/ExporterApi.h"

namespace qb {
namespace compile {

class FountainExporter : public QObject, public ExporterInterface
{
    Q_OBJECT
    Q_INTERFACES(qb::ExporterInterface)
public:
    using QObject::QObject;
    QString id() const override { return QStringLiteral("fountain"); }
    QString name() const override { return tr("Fountain Screenplay (.fountain)"); }
    QString fileExtension() const override { return QStringLiteral("fountain"); }
    bool exportManuscript(const Manuscript &m, const QString &path, QString *error) override;
};

class FdxExporter : public QObject, public ExporterInterface
{
    Q_OBJECT
    Q_INTERFACES(qb::ExporterInterface)
public:
    using QObject::QObject;
    QString id() const override { return QStringLiteral("fdx"); }
    QString name() const override { return tr("Final Draft (.fdx)"); }
    QString fileExtension() const override { return QStringLiteral("fdx"); }
    bool exportManuscript(const Manuscript &m, const QString &path, QString *error) override;
};

} // namespace compile
} // namespace qb
