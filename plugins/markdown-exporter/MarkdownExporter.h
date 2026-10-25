#pragma once
// Example compile plugin. Copy this folder to start a new exporter:
// change the class name, id(), name(), fileExtension() and write the file in
// exportManuscript().
#include "compile/ExporterApi.h"

#include <QObject>

class MarkdownExporter : public QObject, public qb::ExporterInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QB_EXPORTER_IID)
    Q_INTERFACES(qb::ExporterInterface)

public:
    QString id() const override { return QStringLiteral("markdown"); }
    QString name() const override { return QStringLiteral("Markdown (.md)"); }
    QString fileExtension() const override { return QStringLiteral("md"); }
    bool exportManuscript(const qb::compile::Manuscript &m, const QString &path, QString *error) override;
};
