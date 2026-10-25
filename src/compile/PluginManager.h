#pragma once
#include "compile/ExporterApi.h"
#include "import/ImporterApi.h"
#include "media/MediaApi.h"
#include "tools/ToolApi.h"

#include <QJsonObject>
#include <QList>
#include <QStringList>

namespace qb {
namespace compile {

// Exporter defined by a JSON file that runs an external program on the
// DOCX (or Markdown) produced by the built-in exporter.
class CommandExporter : public ExporterInterface
{
public:
    explicit CommandExporter(const QJsonObject &def, const QString &sourceFile);
    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QString fileExtension() const override { return m_extension; }
    bool exportManuscript(const Manuscript &m, const QString &path, QString *error) override;
    bool isAvailable() const { return !m_program.isEmpty(); }
    QString sourceFile() const { return m_source; }
    QString requirement() const { return m_requires; }

private:
    QString m_id, m_name, m_extension, m_input, m_program, m_output, m_requires, m_source;
    QStringList m_arguments;
    int m_timeout = 180;
};

class PluginManager
{
public:
    static PluginManager &instance();
    void load();
    QList<ExporterInterface *> exporters() const { return m_exporters; }
    // Writing tools (tools/ToolApi.h), in the order they were found; the first
    // plugin with a given id wins.
    QList<tools::ToolInterface *> tools() const { return m_tools; }
    tools::ToolInterface *tool(const QString &id) const;
    // Media engines (media/MediaApi.h, add-on "Media Playback"); the first one plays.
    QList<media::EngineInterface *> mediaEngines() const { return m_media; }
    media::EngineInterface *mediaEngine() const { return m_media.isEmpty() ? nullptr : m_media.first(); }
    ExporterInterface *exporter(const QString &id) const;
    // Importers (import/ImporterApi.h): files by suffix, web pages.
    QList<importers::ImporterInterface *> importers() const { return m_importers; }
    importers::ImporterInterface *importerFor(const QString &suffix) const;  // lower case, no dot
    importers::ImporterInterface *webImporter() const;
    QStringList importerSuffixes() const;
    QStringList searchDirs() const;
    QString userPluginDir() const;
    QStringList messages() const { return m_messages; }
    // plugins that exist but cannot run (missing program)
    QStringList unavailable() const { return m_unavailable; }

private:
    PluginManager() = default;
    QList<ExporterInterface *> m_exporters;
    QList<tools::ToolInterface *> m_tools;
    QList<media::EngineInterface *> m_media;
    QList<importers::ImporterInterface *> m_importers;
    QList<QObject *> m_owned;
    QList<CommandExporter *> m_commands;
    QStringList m_messages;
    QStringList m_unavailable;
    bool m_loaded = false;
};

} // namespace compile
} // namespace qb
