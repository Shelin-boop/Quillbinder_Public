#pragma once
#include "compile/ExporterApi.h"

namespace qb {
namespace compile {

// Built-in exporter: Office Open XML (.docx), readable by Word, LibreOffice,
// Google Docs, Pages ...
class DocxExporter : public QObject, public ExporterInterface
{
    Q_OBJECT
    Q_INTERFACES(qb::ExporterInterface)
public:
    using QObject::QObject;
    QString id() const override { return QStringLiteral("docx"); }
    QString name() const override { return tr("Word Document (.docx)"); }
    QString fileExtension() const override { return QStringLiteral("docx"); }
    bool exportManuscript(const Manuscript &m, const QString &path, QString *error) override;
};

// Plain Markdown exporter used internally (e.g. as input for pandoc).
bool writeMarkdown(const Manuscript &m, const QString &path, QString *error);

} // namespace compile
} // namespace qb
