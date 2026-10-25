#include "ui/FontInstall.h"
#include "core/Zip.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

namespace qb {
namespace fontinstall {

namespace {

// the files this process registered (by path), so reload() does not add them twice
QSet<QString> &registered()
{
    static QSet<QString> s;
    return s;
}

QStringList addToQt(const QString &path)
{
    const QString canon = QFileInfo(path).canonicalFilePath();
    if (registered().contains(canon))
        return {};
    const int id = QFontDatabase::addApplicationFont(path);
    if (id < 0)
        return {};
    registered().insert(canon);
    return QFontDatabase::applicationFontFamilies(id);
}

// Inside the AppImage some variables point into the image; programs of the
// desktop must see the normal ones.
QProcessEnvironment cleanEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString appDir = env.value(QStringLiteral("APPDIR"));
    if (!appDir.isEmpty())
        for (const QString &k : env.keys())
            if (env.value(k).contains(appDir))
                env.remove(k);
    env.remove(QStringLiteral("QT_QPA_PLATFORMTHEME"));
    env.remove(QStringLiteral("QT_PLUGIN_PATH"));
    return env;
}

bool startClean(const QString &program, const QStringList &args)
{
    QProcess proc;
    proc.setProgram(program);
    proc.setArguments(args);
    proc.setProcessEnvironment(cleanEnvironment());
    proc.setStandardOutputFile(QProcess::nullDevice());
    proc.setStandardErrorFile(QProcess::nullDevice());
    return proc.startDetached();
}

QByteArray sha(const QByteArray &data) { return QCryptographicHash::hash(data, QCryptographicHash::Sha256); }

// Writes one font into the folder; returns the file name used or an empty string.
QString place(const QString &dir, const QString &name, const QByteArray &data, QString *why, bool *fresh)
{
    *fresh = false;
    if (data.size() < 12) {
        *why = QObject::tr("the file is empty or cut off");
        return QString();
    }
    const QFileInfo fi(name);
    QString target = dir + QLatin1Char('/') + fi.fileName();
    for (int n = 2; QFile::exists(target); ++n) {
        QFile old(target);
        if (old.open(QIODevice::ReadOnly) && sha(old.readAll()) == sha(data))
            return QFileInfo(target).fileName(); // the same font is there already
        target = dir + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral("-%1.").arg(n) + fi.suffix();
    }
    QFile out(target);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size()) {
        *why = QObject::tr("cannot write into %1").arg(dir);
        return QString();
    }
    out.close();
    *fresh = true;
    return QFileInfo(target).fileName();
}

} // namespace

QString userFontDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    const QString dir = base + QStringLiteral("/fonts");
    QDir().mkpath(dir);
    return dir;
}

QStringList fontSuffixes()
{
    return {QStringLiteral("ttf"), QStringLiteral("otf"), QStringLiteral("ttc"), QStringLiteral("otc"),
            QStringLiteral("pfb"), QStringLiteral("pfa"), QStringLiteral("woff"), QStringLiteral("woff2")};
}

bool isFontFile(const QString &path) { return fontSuffixes().contains(QFileInfo(path).suffix().toLower()); }

Result install(const QStringList &files, const QString &targetDir)
{
    Result r;
    const QString dir = targetDir.isEmpty() ? userFontDir() : targetDir;
    QDir().mkpath(dir);
    auto one = [&](const QString &name, const QByteArray &data) {
        QString why;
        bool fresh = false;
        const QString placed = place(dir, name, data, &why, &fresh);
        if (placed.isEmpty()) {
            r.skipped << QStringLiteral("%1: %2").arg(QFileInfo(name).fileName(), why);
            return;
        }
        const QStringList fams = addToQt(dir + QLatin1Char('/') + placed);
        if (fams.isEmpty()) {
            // Qt could not read it: not a font after all (a file that was there before stays)
            const QString canon = QFileInfo(dir + QLatin1Char('/') + placed).canonicalFilePath();
            if (!registered().contains(canon) && fresh) {
                QFile::remove(dir + QLatin1Char('/') + placed);
                r.skipped << QStringLiteral("%1: %2").arg(QFileInfo(name).fileName(), QObject::tr("not a font the program can read"));
                return;
            }
        }
        r.installed << placed;
        for (const QString &f : fams)
            if (!r.families.contains(f))
                r.families << f;
    };
    for (const QString &f : files) {
        const QFileInfo fi(f);
        if (fi.suffix().compare(QLatin1String("zip"), Qt::CaseInsensitive) == 0) {
            ZipReader zip(f);
            if (!zip.isValid()) {
                r.skipped << QStringLiteral("%1: %2").arg(fi.fileName(), QObject::tr("not a ZIP archive that can be read"));
                continue;
            }
            int found = 0;
            for (const QString &n : zip.fileNames()) {
                if (n.endsWith(QLatin1Char('/')) || n.contains(QLatin1String("__MACOSX")) || !isFontFile(n))
                    continue;
                ++found;
                one(n, zip.fileData(n));
            }
            if (found == 0)
                r.skipped << QStringLiteral("%1: %2").arg(fi.fileName(), QObject::tr("no font files inside"));
            continue;
        }
        if (!isFontFile(f)) {
            r.skipped << QStringLiteral("%1: %2").arg(fi.fileName(), QObject::tr("not a font file"));
            continue;
        }
        QFile in(f);
        if (!in.open(QIODevice::ReadOnly)) {
            r.skipped << QStringLiteral("%1: %2").arg(fi.fileName(), QObject::tr("cannot be read"));
            continue;
        }
        one(f, in.readAll());
    }
    if (!r.installed.isEmpty())
        refreshCache(dir);
    return r;
}

QStringList reload()
{
    QStringList fresh;
    const QStringList dirs = {userFontDir(), QDir::homePath() + QStringLiteral("/.fonts")};
    for (const QString &d : dirs) {
        if (!QDir(d).exists())
            continue;
        QDirIterator it(d, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            if (!isFontFile(f))
                continue;
            const QStringList before = QFontDatabase::families();
            for (const QString &fam : addToQt(f))
                if (!before.contains(fam) && !fresh.contains(fam))
                    fresh << fam;
        }
    }
    return fresh;
}

QStringList fontManagerCommand()
{
    // KDE Plasma 6 / 5 first (the user's desktop), then others
    const QList<QStringList> candidates = {
        {QStringLiteral("kcmshell6"), QStringLiteral("kcm_fontinst")},
        {QStringLiteral("systemsettings"), QStringLiteral("kcm_fontinst")},
        {QStringLiteral("kcmshell5"), QStringLiteral("fontinst")},
        {QStringLiteral("font-manager")},
        {QStringLiteral("gnome-font-viewer")},
    };
    const QString forced = qEnvironmentVariable("QB_FONT_MANAGER"); // tests
    if (!forced.isEmpty())
        return forced == QLatin1String("none") ? QStringList() : forced.split(QLatin1Char(' '));
    for (const QStringList &c : candidates) {
        const QString exe = QStandardPaths::findExecutable(c.first());
        if (!exe.isEmpty()) {
            QStringList cmd = c;
            cmd[0] = exe;
            return cmd;
        }
    }
    return {};
}

bool openFontManager(QString *which)
{
    const QStringList cmd = fontManagerCommand();
    if (cmd.isEmpty())
        return false;
    if (which)
        *which = QFileInfo(cmd.first()).fileName();
    return startClean(cmd.first(), cmd.mid(1));
}

void refreshCache(const QString &dir)
{
    const QString exe = QStandardPaths::findExecutable(QStringLiteral("fc-cache"));
    if (!exe.isEmpty())
        startClean(exe, {QStringLiteral("-f"), dir});
}

} // namespace fontinstall
} // namespace qb
