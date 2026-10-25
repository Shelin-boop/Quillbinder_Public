#include "compile/PluginManager.h"

#include "compile/ScriptExporters.h"
#include "compile/EpubExporter.h"
#include "compile/PdfExporter.h"
#include "core/Paths.h"
#include "compile/DocxWriter.h"
#include "core/Addons.h"
#include "core/Launcher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPluginLoader>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace qb {
namespace compile {

CommandExporter::CommandExporter(const QJsonObject &def, const QString &sourceFile) : m_source(sourceFile)
{
    m_id = def.value(QStringLiteral("id")).toString();
    m_name = def.value(QStringLiteral("name")).toString(m_id);
    m_extension = def.value(QStringLiteral("extension")).toString();
    m_input = def.value(QStringLiteral("input")).toString(QStringLiteral("docx"));
    m_output = def.value(QStringLiteral("output")).toString();
    m_timeout = def.value(QStringLiteral("timeout")).toInt(180);
    for (const QJsonValue &v : def.value(QStringLiteral("arguments")).toArray())
        m_arguments << v.toString();
    QStringList programs;
    if (def.contains(QStringLiteral("programs")))
        for (const QJsonValue &v : def.value(QStringLiteral("programs")).toArray())
            programs << v.toString();
    else
        programs << def.value(QStringLiteral("program")).toString();
    m_requires = programs.join(QStringLiteral(" / "));
    for (const QString &p : programs) {
        if (p.isEmpty())
            continue;
        const QString found = QStandardPaths::findExecutable(p);
        if (!found.isEmpty()) {
            m_program = found;
            break;
        }
    }
}

bool CommandExporter::exportManuscript(const Manuscript &m, const QString &path, QString *error)
{
    if (m_program.isEmpty()) {
        if (error)
            *error = QObject::tr("This format needs the program \"%1\", which is not installed.").arg(m_requires);
        return false;
    }
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        if (error)
            *error = QObject::tr("Cannot create a temporary folder.");
        return false;
    }
    const QString base = QStringLiteral("manuscript");
    const QString input = tmp.filePath(base + QLatin1Char('.') + m_input);
    if (m_input == QLatin1String("md")) {
        if (!writeMarkdown(m, input, error))
            return false;
    } else {
        DocxExporter docx;
        if (!docx.exportManuscript(m, input, error))
            return false;
    }
    const QString outDir = tmp.filePath(QStringLiteral("out"));
    QDir().mkpath(outDir);
    const QString directOut = tmp.filePath(QStringLiteral("out/") + base + QLatin1Char('.') + m_extension);
    auto subst = [&](QString s) {
        s.replace(QLatin1String("{input}"), input);
        s.replace(QLatin1String("{inputbase}"), base);
        s.replace(QLatin1String("{outdir}"), outDir);
        s.replace(QLatin1String("{output}"), directOut);
        s.replace(QLatin1String("{tmpdir}"), tmp.path());
        s.replace(QLatin1String("{title}"), m.title);
        s.replace(QLatin1String("{author}"), m.author);
        return s;
    };
    QStringList args;
    for (const QString &a : m_arguments)
        args << subst(a);
    QProcess proc;
    proc.setWorkingDirectory(tmp.path());
    // Inside the AppImage some variables point into the image (Qt plugin
    // paths etc.); the external program must see the normal system values.
    const QString appDir = qEnvironmentVariable("APPDIR");
    if (!appDir.isEmpty()) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const QString &k : env.keys())
            if (env.value(k).contains(appDir))
                env.remove(k);
        env.remove(QStringLiteral("QT_QPA_PLATFORMTHEME"));
        proc.setProcessEnvironment(env);
    }
    proc.start(m_program, args);
    if (!proc.waitForStarted(10000)) {
        if (error)
            *error = QObject::tr("Could not start %1.").arg(m_program);
        return false;
    }
    if (!proc.waitForFinished(m_timeout * 1000)) {
        proc.kill();
        if (error)
            *error = QObject::tr("%1 did not finish in time.").arg(QFileInfo(m_program).fileName());
        return false;
    }
    const QString produced = m_output.isEmpty() ? directOut : subst(m_output);
    if (!QFile::exists(produced)) {
        if (error)
            *error = QObject::tr("%1 did not produce a file.\n%2")
                         .arg(QFileInfo(m_program).fileName(),
                              QString::fromLocal8Bit(proc.readAllStandardError()).left(800));
        return false;
    }
    QFile::remove(path);
    if (!QFile::copy(produced, path)) {
        if (error)
            *error = QObject::tr("Cannot write %1").arg(path);
        return false;
    }
    return true;
}

PluginManager &PluginManager::instance()
{
    static PluginManager pm;
    return pm;
}

QString PluginManager::userPluginDir() const
{
    return dataDir() + QLatin1String("/plugins");
}

QStringList PluginManager::searchDirs() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    // Order matters: the first plugin with a given id wins. A program update
    // may carry newer plugins next to its binary; the AppImage's own come next.
    QStringList dirs = {QDir::cleanPath(appDir + QLatin1String("/plugins")),
                        QDir::cleanPath(appDir + QLatin1String("/../lib/" QB_APP_ID "/plugins")),
                        QDir::cleanPath(appDir + QLatin1String("/../share/" QB_APP_ID "/plugins"))};
    const QString image = launcher::appDir();
    if (!image.isEmpty())
        dirs << QDir::cleanPath(image + QLatin1String("/usr/lib/" QB_APP_ID "/plugins"))
             << QDir::cleanPath(image + QLatin1String("/usr/share/" QB_APP_ID "/plugins"));
    dirs << addons::contentDirs(QStringLiteral("plugins")) << userPluginDir();
    // extra folders for developers and packagers (colon separated)
    for (const QString &d : qEnvironmentVariable("QB_PLUGIN_PATH").split(QLatin1Char(':'), Qt::SkipEmptyParts))
        dirs.prepend(QDir::cleanPath(d));
    dirs.removeDuplicates();
    return dirs;
}

tools::ToolInterface *PluginManager::tool(const QString &id) const
{
    for (tools::ToolInterface *t : m_tools)
        if (t->id() == id)
            return t;
    return nullptr;
}

importers::ImporterInterface *PluginManager::importerFor(const QString &suffix) const
{
    const QString ext = suffix.toLower();
    for (importers::ImporterInterface *i : m_importers)
        if (i->suffixes().contains(ext))
            return i;
    return nullptr;
}

importers::ImporterInterface *PluginManager::webImporter() const
{
    for (importers::ImporterInterface *i : m_importers)
        if (i->importsWebPages())
            return i;
    return nullptr;
}

QStringList PluginManager::importerSuffixes() const
{
    QStringList out;
    for (importers::ImporterInterface *i : m_importers)
        out << i->suffixes();
    out.removeDuplicates();
    return out;
}

ExporterInterface *PluginManager::exporter(const QString &id) const
{
    for (ExporterInterface *e : m_exporters)
        if (e->id() == id)
            return e;
    return nullptr;
}

void PluginManager::load()
{
    if (m_loaded)
        return;
    m_loaded = true;
    auto *docx = new DocxExporter;
    m_owned << docx;
    m_exporters << docx;

    QStringList seenIds = {docx->id()};
    // scripts for other screenwriting programs
    // PDF and e-books without other programs (0.14), scripts for other screenwriting programs
    for (ExporterInterface *e : {static_cast<ExporterInterface *>(new PdfExporter), static_cast<ExporterInterface *>(new EpubExporter),
                                 static_cast<ExporterInterface *>(new FountainExporter),
                                 static_cast<ExporterInterface *>(new FdxExporter)}) {
        m_owned << dynamic_cast<QObject *>(e);
        m_exporters << e;
        seenIds << e->id();
    }
    for (const QString &dir : searchDirs()) {
        QDir d(dir);
        if (!d.exists())
            continue;
        // C++ plugins
        for (const QFileInfo &fi : d.entryInfoList({QStringLiteral("*.so")}, QDir::Files)) {
            QPluginLoader loader(fi.absoluteFilePath());
            QObject *obj = loader.instance();
            if (!obj) {
                m_messages << QObject::tr("%1: %2").arg(fi.fileName(), loader.errorString());
                continue;
            }
            if (auto *tool = qobject_cast<tools::ToolInterface *>(obj)) {
                if (this->tool(tool->id()))
                    continue;
                m_tools << tool;
                m_messages << QObject::tr("Loaded writing tool %1 %2 (%3)").arg(tool->name(), tool->version(), fi.fileName());
                continue;
            }
            if (auto *imp = qobject_cast<importers::ImporterInterface *>(obj)) {
                bool seen = false;
                for (importers::ImporterInterface *i : m_importers)
                    seen = seen || i->id() == imp->id();
                if (seen)
                    continue;
                m_importers << imp;
                m_messages << QObject::tr("Loaded importer %1 %2 (%3)").arg(imp->name(), imp->version(), fi.fileName());
                continue;
            }
            if (auto *engine = qobject_cast<media::EngineInterface *>(obj)) {
                bool seen = false;
                for (media::EngineInterface *e : m_media)
                    seen = seen || e->id() == engine->id();
                if (seen)
                    continue;
                m_media << engine;
                m_messages << QObject::tr("Loaded media engine %1 %2 (%3)").arg(engine->name(), engine->version(), fi.fileName());
                continue;
            }
            auto *exp = qobject_cast<ExporterInterface *>(obj);
            if (!exp) {
                m_messages << QObject::tr("%1 is neither an exporter, an importer, a writing tool nor a media engine (or built for another version)").arg(fi.fileName());
                continue;
            }
            if (seenIds.contains(exp->id()))
                continue;
            seenIds << exp->id();
            m_exporters << exp;
            m_messages << QObject::tr("Loaded %1 (%2)").arg(exp->name(), fi.fileName());
        }
        // command plugins
        for (const QFileInfo &fi : d.entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly))
                continue;
            QJsonParseError pe;
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
            if (pe.error != QJsonParseError::NoError) {
                m_messages << QObject::tr("%1: %2").arg(fi.fileName(), pe.errorString());
                continue;
            }
            auto *ce = new CommandExporter(doc.object(), fi.absoluteFilePath());
            if (ce->id().isEmpty() || seenIds.contains(ce->id())) {
                delete ce;
                continue;
            }
            m_commands << ce;
            if (ce->isAvailable()) {
                seenIds << ce->id();
                m_exporters << ce;
                m_messages << QObject::tr("Loaded %1 (%2)").arg(ce->name(), fi.fileName());
            } else {
                m_unavailable << QObject::tr("%1 (needs %2)").arg(ce->name(), ce->requirement());
            }
        }
    }
}

} // namespace compile
} // namespace qb
