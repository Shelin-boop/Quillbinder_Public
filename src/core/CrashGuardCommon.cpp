#include "core/CrashGuard.h"
// The parts of the crash guard that every system shares (moved here unchanged from
// CrashGuard.cpp in the port, W2): the list of reports and what their first lines say.
// Linux writes the reports in CrashGuard.cpp, Windows in CrashGuardWin.cpp, in the same format.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace qb {
namespace crash {

Report readHeader(const QString &file)
{
    Report r;
    r.file = file;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return r;
    const QString head = QString::fromUtf8(f.read(4096));
    if (head.startsWith(QLatin1String("Quillbinder freeze report"))) {
        r.kind = QStringLiteral("freeze");
        r.signal = QStringLiteral("freeze");
        r.meaning = QStringLiteral("the program did not respond");
        f.seek(0);
        r.recovered = f.readAll().contains("\n== Responded again after ");
    }
    for (const QString &line : head.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("== ")))
            break;
        const int colon = line.indexOf(QLatin1String(": "));
        if (colon < 0)
            continue;
        const QString key = line.left(colon), value = line.mid(colon + 2).trimmed();
        if (key == QLatin1String("Version"))
            r.version = value;
        else if (key == QLatin1String("Unix time"))
            r.when = QDateTime::fromSecsSinceEpoch(value.toLongLong());
        else if (key == QLatin1String("Process"))
            r.pid = value.toLongLong();
        else if (key == QLatin1String("Signal")) {
            static const QRegularExpression re(QStringLiteral("^\\d+ (\\S+) \\((.*)\\)$"));
            const auto m = re.match(value);
            r.signal = m.hasMatch() ? m.captured(1) : value;
            r.meaning = m.hasMatch() ? m.captured(2) : QString();
        }
    }
    if (!r.when.isValid()) {
        // from the name: crash-<unix time>-<pid>.txt (or freeze-...)
        const QStringList parts = QFileInfo(file).completeBaseName().split(QLatin1Char('-'));
        if (parts.size() == 3) {
            r.when = QDateTime::fromSecsSinceEpoch(parts.at(1).toLongLong());
            r.pid = parts.at(2).toLongLong();
        }
    }
    return r;
}

QList<Report> reports()
{
    QList<Report> out;
    for (const QFileInfo &fi : QDir(reportDir()).entryInfoList({QStringLiteral("crash-*.txt"), QStringLiteral("freeze-*.txt")}, QDir::Files))
        out << readHeader(fi.absoluteFilePath());
    std::sort(out.begin(), out.end(), [](const Report &a, const Report &b) { return a.when > b.when; });
    return out;
}

void prune(int keep)
{
    const QList<Report> all = reports();
    for (int i = keep; i < all.size(); ++i) {
        QFile::remove(all.at(i).file);
        // Windows writes a memory dump (.dmp) beside the report
        const QString dump = all.at(i).file.left(all.at(i).file.size() - 4) + QLatin1String(".dmp");
        if (QFile::exists(dump))
            QFile::remove(dump);
    }
}

} // namespace crash
} // namespace qb
