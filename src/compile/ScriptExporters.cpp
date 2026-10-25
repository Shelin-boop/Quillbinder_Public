#include "compile/ScriptExporters.h"

#include "text/ScriptIO.h"

#include <QSaveFile>

namespace qb {
namespace compile {

static bool save(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        if (error)
            *error = QObject::tr("The file could not be written: %1").arg(path);
        return false;
    }
    return true;
}

bool FountainExporter::exportManuscript(const Manuscript &m, const QString &path, QString *error)
{
    return save(path, script::toFountain(script::fromManuscript(m)).toUtf8(), error);
}

bool FdxExporter::exportManuscript(const Manuscript &m, const QString &path, QString *error)
{
    return save(path, script::toFdx(script::fromManuscript(m)), error);
}

} // namespace compile
} // namespace qb
