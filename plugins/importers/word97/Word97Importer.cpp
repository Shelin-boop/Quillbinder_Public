#include "Word97Importer.h"

#include "Cfb.h"
#include "WordReader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

QString Word97Importer::officeProgram()
{
    const QString forced = qEnvironmentVariable("QB_SOFFICE");
    if (!forced.isEmpty())
        return forced == QLatin1String("none") ? QString() : forced;
    for (const char *p : {"soffice", "libreoffice", "lowriter"}) {
        const QString found = QStandardPaths::findExecutable(QString::fromLatin1(p));
        if (!found.isEmpty())
            return found;
    }
    return QString();
}

bool Word97Importer::convertWithOffice(const QString &path, const QString &workDir, QString *result, QString *error)
{
    const QString program = officeProgram();
    if (program.isEmpty())
        return false;
    QProcess proc;
    proc.setWorkingDirectory(workDir);
    // inside the AppImage some variables point into the image; LibreOffice must see the normal ones
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString appDir = env.value(QStringLiteral("APPDIR"));
    if (!appDir.isEmpty())
        for (const QString &k : env.keys())
            if (env.value(k).contains(appDir))
                env.remove(k);
    env.remove(QStringLiteral("QT_QPA_PLATFORMTHEME"));
    env.remove(QStringLiteral("QT_PLUGIN_PATH"));
    proc.setProcessEnvironment(env);
    // a profile of its own: works while LibreOffice is open as well
    const QString profile = QUrl::fromLocalFile(workDir + QStringLiteral("/lo-profile")).toString();
    const QString outDir = workDir + QStringLiteral("/converted");
    QDir().mkpath(outDir);
    // a copy with a plain name (LibreOffice names the result after it)
    const QString input = workDir + QStringLiteral("/document.doc");
    QFile::remove(input);
    if (!QFile::copy(path, input)) {
        if (error)
            *error = tr("Cannot copy the file for LibreOffice.");
        return false;
    }
    proc.start(program, {QStringLiteral("-env:UserInstallation=") + profile, QStringLiteral("--headless"),
                         QStringLiteral("--norestore"), QStringLiteral("--convert-to"), QStringLiteral("docx"),
                         QStringLiteral("--outdir"), outDir, input});
    if (!proc.waitForStarted(15000)) {
        if (error)
            *error = tr("LibreOffice could not be started.");
        return false;
    }
    if (!proc.waitForFinished(180000)) {
        proc.kill();
        proc.waitForFinished(3000);
        if (error)
            *error = tr("LibreOffice did not finish in time.");
        return false;
    }
    const QString out = outDir + QStringLiteral("/document.docx");
    if (!QFileInfo::exists(out)) {
        if (error)
            *error = tr("LibreOffice could not convert the file.");
        return false;
    }
    *result = out;
    return true;
}

bool Word97Importer::readFile(const QString &path, const QString &workDir, qb::importers::Content *out, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Cannot open the file.");
        return false;
    }
    const QByteArray data = f.readAll();
    f.close();
    QString err;
    qbdoc::Failure fail = qbdoc::Failure::NotWord;
    if (qbdoc::CompoundFile::isCompoundFile(data)) {
        fail = qbdoc::readWord97(data, out, &err);
        if (fail == qbdoc::Failure::None)
            return true;
    } else {
        err = tr("The file is not a Word 97–2003 document.");
    }
    if (fail == qbdoc::Failure::Encrypted) {
        if (error)
            *error = err;
        return false;
    }
    // Word 6.0 / 95, Word for DOS, a damaged file …: LibreOffice reads more of them
    *out = qb::importers::Content();
    QString converted, lerr;
    if (convertWithOffice(path, workDir, &converted, &lerr)) {
        out->file = converted;
        out->warnings << tr("read with LibreOffice (%1)").arg(err.trimmed().isEmpty() ? tr("an older Word format") : err);
        return true;
    }
    if (error) {
        *error = err;
        if (officeProgram().isEmpty())
            *error += QLatin1Char(' ')
                      + tr("With LibreOffice installed (sudo apt install libreoffice-writer) it can be read, or save it as "
                           ".docx in a word processor.");
        else if (!lerr.isEmpty())
            *error += QLatin1Char(' ') + lerr;
    }
    return false;
}
