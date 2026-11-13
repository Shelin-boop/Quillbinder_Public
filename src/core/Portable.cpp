// The portable program on Windows (W4b); see Portable.h.
#include "core/Portable.h"
#include "core/Launcher.h"
#include "core/Paths.h"
#include "core/Platform.h"
#include "core/TestSwitches.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace qb {
namespace portable {

namespace {

const char *const kTools[] = {"scratch-pad", "speech-transcription"};

QString say(const char *text) { return QCoreApplication::translate("Portable", text); }

QString subFolder(const QString &toolId, bool own)
{
    if (toolId == QLatin1String("speech-transcription"))
        return own ? QStringLiteral("Quillbinder Transcriptions") : QStringLiteral("Transcriptions");
    return own ? QStringLiteral("Quillbinder Scratch Pad") : QStringLiteral("Scratch Pad");
}

QString placeKey(NotesPlace p)
{
    switch (p) {
    case NotesPlace::ProgramFolder:
        return QStringLiteral("program");
    case NotesPlace::Own:
        return QStringLiteral("own");
    case NotesPlace::Documents:
        break;
    }
    return QStringLiteral("documents");
}

} // namespace

bool active()
{
#ifdef Q_OS_WIN
    return true;
#else
    return QB_TEST_ENV_SET("QB_TEST_PORTABLE");
#endif
}

QString programFolder()
{
#ifdef Q_OS_WIN
    return launcher::appDir();
#else
    const QString forced = QB_TEST_ENV("QB_TEST_PORTABLE_FOLDER");
    return forced.isEmpty() ? QCoreApplication::applicationDirPath() : forced;
#endif
}

void useProgramFolder()
{
#ifdef Q_OS_WIN
    const QString dir = dataDir() + QLatin1String("/settings");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir);
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, dir);
#endif
}

QString folderProblem()
{
#ifdef Q_OS_WIN
    const QString data = dataDir();
    const QString shown = QDir::toNativeSeparators(programFolder());
    if (!QDir().mkpath(data))
        return say(QT_TRANSLATE_NOOP("Portable", "the folder \"data\" cannot be made in %1")).arg(shown);
    QFile probe(data + QStringLiteral("/.write-check-%1").arg(QCoreApplication::applicationPid()));
    const bool ok = probe.open(QIODevice::WriteOnly) && probe.write("check") == 5 && probe.flush();
    const QString why = probe.errorString();
    probe.close();
    probe.remove();
    if (!ok)
        return say(QT_TRANSLATE_NOOP("Portable", "nothing can be written into %1 (%2)")).arg(QDir::toNativeSeparators(data), why);
#endif
    return {};
}

QString syncService(const QString &path)
{
    const QString p = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
    // OneDrive names its folders in the environment (personal and work or school accounts)
    for (const char *var : {"OneDrive", "OneDriveConsumer", "OneDriveCommercial"}) {
        const QString d = QDir::fromNativeSeparators(qEnvironmentVariable(var));
        if (!d.isEmpty() && (p.compare(d, Qt::CaseInsensitive) == 0 || p.startsWith(d + QLatin1Char('/'), Qt::CaseInsensitive)))
            return QStringLiteral("OneDrive");
    }
    const struct
    {
        const char *part;
        const char *name;
    } known[] = {{"/OneDrive", "OneDrive"},
                 {"/Dropbox", "Dropbox"},
                 {"/Google Drive", "Google Drive"},
                 {"/My Drive", "Google Drive"},
                 {"/iCloudDrive", "iCloud Drive"},
                 {"/iCloud Drive", "iCloud Drive"},
                 {"/Nextcloud", "Nextcloud"},
                 {"/pCloud", "pCloud"}};
    for (const auto &k : known) {
        const QString part = QLatin1String(k.part);
        const int at = p.indexOf(part, 0, Qt::CaseInsensitive);
        // a whole folder name, or its start ("OneDrive - Company")
        if (at >= 0) {
            const int end = at + part.size();
            if (end == p.size() || p.at(end) == QLatin1Char('/') || p.at(end) == QLatin1Char(' '))
                return QLatin1String(k.name);
        }
    }
    return {};
}

bool longPath(const QString &path)
{
    // the program's own deepest files are about 110 characters below its folder (data\core\<version>\...,
    // data\tools\speech-transcription\models\...), a project's documents add their own
    return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).size() > 100;
}

Notes notes()
{
    QSettings s;
    Notes n;
    const QString p = s.value(QStringLiteral("app/notesPlace")).toString();
    if (p == QLatin1String("program"))
        n.place = NotesPlace::ProgramFolder;
    else if (p == QLatin1String("own"))
        n.place = NotesPlace::Own;
    n.own = s.value(QStringLiteral("app/notesOwn")).toString();
    if (n.place == NotesPlace::Own && n.own.isEmpty())
        n.place = NotesPlace::Documents;
    return n;
}

QString notesFolder(const QString &toolId, const Notes &n)
{
    switch (n.place) {
    case NotesPlace::ProgramFolder:
        return programFolder() + QLatin1Char('/') + subFolder(toolId, false);
    case NotesPlace::Own:
        if (!n.own.isEmpty())
            return QDir::fromNativeSeparators(n.own) + QLatin1Char('/') + subFolder(toolId, true);
        break;
    case NotesPlace::Documents:
        break;
    }
    return {};
}

void setNotes(const Notes &n)
{
    QSettings s;
    s.setValue(QStringLiteral("app/notesPlace"), placeKey(n.place));
    s.setValue(QStringLiteral("app/notesOwn"), n.place == NotesPlace::Own ? QDir::fromNativeSeparators(n.own) : QString());
    for (const char *t : kTools) {
        const QString id = QLatin1String(t);
        const QString folder = notesFolder(id, n);
        s.setValue(QStringLiteral("tools/%1/folder").arg(id), folder);
        s.setValue(QStringLiteral("app/notesGiven/%1").arg(id), folder);
    }
    s.sync();
}

void followProgramFolder()
{
    const Notes n = notes();
    if (n.place != NotesPlace::ProgramFolder)
        return;
    QSettings s;
    for (const char *t : kTools) {
        const QString id = QLatin1String(t);
        const QString given = s.value(QStringLiteral("app/notesGiven/%1").arg(id)).toString();
        const QString now = s.value(QStringLiteral("tools/%1/folder").arg(id)).toString();
        const QString want = notesFolder(id, n);
        if (now == given && now != want) {
            s.setValue(QStringLiteral("tools/%1/folder").arg(id), want);
            s.setValue(QStringLiteral("app/notesGiven/%1").arg(id), want);
        }
    }
}

namespace {
QString &sessionTempPath()
{
    static QString path;
    return path;
}
void removeSessionTemp()
{
    const QString path = sessionTempPath();
    if (path.isEmpty())
        return;
    QDir(path).removeRecursively();
    QDir().rmdir(QFileInfo(path).absolutePath()); // only when nothing else is in it (the tool link's markers)
    sessionTempPath().clear();
}
} // namespace

QString sessionTemp()
{
    QString &made = sessionTempPath();
    if (!made.isEmpty() && QDir(made).exists())
        return made;
    const QString base = QDir::tempPath() + QStringLiteral("/" QB_APP_ID);
    QDir().mkpath(base);
    const qint64 own = QCoreApplication::applicationPid();
    const QStringList old = QDir(base).entryList({QStringLiteral("run-*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : old) {
        bool ok = false;
        const qint64 pid = name.mid(4).toLongLong(&ok);
        if (ok && pid != own && !platform::processAlive(pid))
            QDir(base + QLatin1Char('/') + name).removeRecursively();
    }
    const bool first = made.isEmpty();
    made = base + QStringLiteral("/run-%1").arg(own);
    QDir(made).removeRecursively(); // a folder of an earlier program with this process number
    QDir().mkpath(made);
    static bool hooked = false;
    if (first && !hooked) {
        hooked = true;
        qAddPostRoutine(removeSessionTemp);
    }
    return made;
}

} // namespace portable
} // namespace qb
