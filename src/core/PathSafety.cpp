#include "core/PathSafety.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace qb {
namespace pathsafety {

bool isSafeId(const QString &id)
{
    if (id.isEmpty() || id.size() > 80)
        return false;
    for (const QChar c : id) {
        const ushort u = c.unicode();
        const bool ok = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '-' || u == '_';
        if (!ok)
            return false;
    }
    return true;
}

bool isSafeExtension(const QString &ext)
{
    if (ext.isEmpty() || ext.size() > 12)
        return false;
    for (const QChar c : ext) {
        const ushort u = c.unicode();
        if (!((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9')))
            return false;
    }
    return true;
}

bool isSafeRelative(const QString &rel)
{
    if (rel.isEmpty() || rel.startsWith(QLatin1Char('/')) || rel.contains(QLatin1Char('\\')) || rel.contains(QLatin1Char(':')))
        return false;
    for (const QChar c : rel)
        if (c.unicode() < 0x20)
            return false;
    const QStringList parts = rel.split(QLatin1Char('/'));
    for (int i = 0; i < parts.size(); ++i) {
        const QString &part = parts.at(i);
        // a folder entry of an archive ends with '/': its last part is empty
        if (part.isEmpty() && i == parts.size() - 1 && i > 0)
            continue;
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String(".."))
            return false;
    }
    return true;
}

namespace {
// the absolute, clean path with the longest existing part's links resolved
QString resolved(const QString &path)
{
    QString abs = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QString head = abs, tail;
    while (!head.isEmpty() && !QFileInfo::exists(head)) {
        const int slash = int(head.lastIndexOf(QLatin1Char('/')));
        if (slash <= 0)
            return abs;
        tail = head.mid(slash) + tail;
        head = head.left(slash);
    }
    const QString canon = QFileInfo(head).canonicalFilePath();
    return QDir::cleanPath((canon.isEmpty() ? head : canon) + tail);
}
} // namespace

bool isInside(const QString &path, const QString &folder)
{
    if (path.isEmpty() || folder.isEmpty())
        return false;
    const QString p = resolved(path), f = resolved(folder);
    return p == f || p.startsWith(f.endsWith(QLatin1Char('/')) ? f : f + QLatin1Char('/'));
}

bool removeTreeInside(const QString &dir, const QString &root)
{
    const QFileInfo fi(dir);
    if (fi.isSymLink())
        return QFile::remove(dir); // the link only
    if (!fi.exists())
        return true;
    if (!isInside(dir, root) || QDir::cleanPath(fi.absoluteFilePath()) == QDir::cleanPath(QFileInfo(root).absoluteFilePath()))
        return false;
    return QDir(dir).removeRecursively();
}

} // namespace pathsafety
} // namespace qb
