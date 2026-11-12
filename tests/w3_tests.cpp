// Checks of the Windows port, step W3 (2026-11-08). Runs on Linux and on the Windows runner
// (packaging/windows/win-tests.sh); the Windows-only parts check what the Windows program does differently.
//  - A bundle with a program update of the running version (the "update with all add-ons"):
//    the program update is left out, the add-ons go in.
//  - addons::installBundled (the full version for Windows): installs every package of a folder once
//    per version and writes the marker; an empty folder does nothing.
//  - The license list compiled into the program: every text it names is there; on Windows it is the
//    list of the Windows program (Qt of the build, Hunspell 1.7.5, the dictionaries), on Linux the Linux one.
//  - Spelling: with QB_TEST_DICTIONARIES=<folder> (Windows: also the dictionaries beside the test
//    program, put there by packaging/windows/win-tests.sh) the five dictionaries load and know their words.
// Exit code = number of failed checks. Uses Qt's test folders (QStandardPaths test mode), not the
// user's data.
//
//   ./qb_w3_tests /tmp/shots     (no window is opened; the folder is not used)
#include "core/Addons.h"
#include "core/Paths.h"
#include "core/Platform.h"
#include "ui/SpellChecker.h"

#include "AppInfo.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

using namespace qb;

static int failures = 0;

static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail.left(3000)));
    }
    std::fflush(stdout);
}

static QString python()
{
    for (const char *name : {"python3", "python"}) {
        const QString p = QStandardPaths::findExecutable(QLatin1String(name));
        if (!p.isEmpty())
            return p;
    }
    return QStringLiteral("python3");
}

static QString makePackage(const QStringList &args, QString *log)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(python(), QStringList{QStringLiteral(QB_TEST_SOURCE_DIR "/../tools/make-package.py")} + args);
    p.waitForFinished(120000);
    if (log)
        *log += QString::fromUtf8(p.readAll());
    return args.last();
}

static QString contentAddon(const QString &dir, const QString &id, QString *log)
{
    const QString folder = dir + QLatin1Char('/') + id;
    QDir().mkpath(folder + QLatin1String("/payload/themes"));
    QFile mf(folder + QLatin1String("/manifest.json"));
    mf.open(QIODevice::WriteOnly);
    mf.write(QJsonDocument(QJsonObject{{QStringLiteral("id"), id},
                                       {QStringLiteral("name"), QStringLiteral("Check %1").arg(id)},
                                       {QStringLiteral("version"), QStringLiteral("1.0")}})
                 .toJson());
    mf.close();
    QFile t(folder + QLatin1String("/payload/themes/") + id + QLatin1String(".json"));
    t.open(QIODevice::WriteOnly);
    t.write("{}");
    t.close();
    return makePackage({QStringLiteral("addon"), folder, dir + QLatin1Char('/') + id + QLatin1String(".qbpkg")}, log);
}

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral(QB_APP_NAME));
    QCoreApplication::setApplicationName(QStringLiteral(QB_APP_NAME));
    QDir(dataDir()).removeRecursively();
    QTemporaryDir tmp;
    const QString dir = tmp.path();
    QString log;

    // ---- a bundle with a program update of the running version
    const QString exeDir = dir + QLatin1String("/core");
    QDir().mkpath(exeDir);
    QFile exe(exeDir + QLatin1Char('/') + platform::programFileName());
    exe.open(QIODevice::WriteOnly);
    exe.write("not a program");
    exe.close();
    const QString core = makePackage({QStringLiteral("core"), QStringLiteral("--binary"), exe.fileName(), QStringLiteral("--version"),
                                      QStringLiteral(QB_APP_VERSION), QStringLiteral("--runtime"), QStringLiteral(QB_RUNTIME_ID),
                                      QStringLiteral("--name"), QStringLiteral("Check"), dir + QLatin1String("/core.qbpkg")},
                                     &log);
    const QString a1 = contentAddon(dir, QStringLiteral("check-w3-one"), &log);
    const QString bundle = makePackage({QStringLiteral("bundle"), QStringLiteral("--id"), QStringLiteral("check-update"),
                                        QStringLiteral("--name"), QStringLiteral("Check update"), QStringLiteral("--version"),
                                        QStringLiteral(QB_APP_VERSION), core, a1, dir + QLatin1String("/update.qbpkg")},
                                       &log);
    const addons::PackageInfo bi = addons::inspect(bundle);
    bool coreSkipped = false;
    for (const addons::PackageInfo &n : bi.nested)
        coreSkipped = coreSkipped || (n.manifest.kind == QLatin1String("core") && n.skip);
    check(bi.isValid() && coreSkipped, QStringLiteral("a bundle with a program update of this version is valid; the update is marked to be left out"),
          bi.error + QLatin1Char(' ') + log);
    {
        QStringList ilog;
        QString err;
        bool restart = false;
        const bool ok = addons::install(bundle, &ilog, &err, &restart);
        check(ok && QFileInfo::exists(addons::addonsRoot() + QLatin1String("/check-w3-one"))
                  && !QFileInfo::exists(addons::coreRoot() + QLatin1Char('/') + QLatin1String(QB_APP_VERSION)),
              QStringLiteral("... installing it puts the add-on in and leaves the program update out"),
              err + QLatin1Char(' ') + ilog.join(QLatin1String(" | ")));
        check(ilog.join(QLatin1Char('\n')).contains(QLatin1String("already there")), QStringLiteral("... and says so"),
              ilog.join(QLatin1String(" | ")));
    }
    // the same program update on its own is still refused
    check(!addons::inspect(core).isValid(), QStringLiteral("the program update of this version alone is still refused"));

    // ---- the full version's add-ons
    const QString full = dir + QLatin1String("/full-addons");
    QDir().mkpath(full);
    contentAddon(dir, QStringLiteral("check-w3-two"), &log);
    contentAddon(dir, QStringLiteral("check-w3-three"), &log);
    QFile::copy(dir + QLatin1String("/check-w3-two.qbpkg"), full + QLatin1String("/check-w3-two.qbpkg"));
    QFile::copy(dir + QLatin1String("/check-w3-three.qbpkg"), full + QLatin1String("/check-w3-three.qbpkg"));
    const QString marker = dataDir() + QStringLiteral("/full-addons-installed-" QB_APP_VERSION);
    {
        QStringList ilog;
        QString err;
        check(addons::installBundled(dir + QLatin1String("/empty-none"), &ilog, &err) && !QFile::exists(marker),
              QStringLiteral("installBundled: a folder without packages does nothing"));
        const bool ok = addons::installBundled(full, &ilog, &err);
        check(ok && QFileInfo::exists(addons::addonsRoot() + QLatin1String("/check-w3-two"))
                  && QFileInfo::exists(addons::addonsRoot() + QLatin1String("/check-w3-three")) && QFile::exists(marker),
              QStringLiteral("installBundled installs every package of the folder and writes the marker"), err);
        QDir(addons::addonsRoot() + QLatin1String("/check-w3-two")).removeRecursively();
        addons::installBundled(full, &ilog, &err);
        check(!QFileInfo::exists(addons::addonsRoot() + QLatin1String("/check-w3-two")),
              QStringLiteral("... once per version: with the marker nothing is installed again"));
        QFile::remove(marker);
        QFile bad(full + QLatin1String("/broken.qbpkg"));
        bad.open(QIODevice::WriteOnly);
        bad.write("not a package");
        bad.close();
        err.clear();
        check(!addons::installBundled(full, &ilog, &err) && !QFile::exists(marker) && err.contains(QLatin1String("broken.qbpkg")),
              QStringLiteral("... a package that fails is named, and no marker is written"), err);
    }

    // ---- the license list in the program
    QFile idx(QStringLiteral(":/licenses/index.json"));
    idx.open(QIODevice::ReadOnly);
    const QJsonObject index = QJsonDocument::fromJson(idx.readAll()).object();
    QStringList missing, names;
    for (const QJsonValue &v : index.value(QStringLiteral("components")).toArray()) {
        const QJsonObject c = v.toObject();
        names << c.value(QStringLiteral("name")).toString() + QLatin1Char(' ') + c.value(QStringLiteral("version")).toString();
        if (!QFile::exists(QStringLiteral(":/licenses/texts/") + c.value(QStringLiteral("text")).toString()))
            missing << c.value(QStringLiteral("text")).toString();
    }
    for (const QJsonValue &v : index.value(QStringLiteral("commonLicenses")).toArray())
        if (!QFile::exists(QStringLiteral(":/licenses/texts/") + v.toObject().value(QStringLiteral("text")).toString()))
            missing << v.toObject().value(QStringLiteral("text")).toString();
    check(names.size() > 10 && missing.isEmpty(), QStringLiteral("every license text the list names is in the program (%1 parts)").arg(names.size()),
          missing.join(QLatin1String(", ")));
#ifdef Q_OS_WIN
    const QStringList want = {QStringLiteral("Qt " QT_VERSION_STR), QStringLiteral("Hunspell 1.7.5"), QStringLiteral("Microsoft Visual C++ Runtime"),
                              QStringLiteral("German (Germany, Austria, Switzerland) spelling dictionaries"), QStringLiteral("PDFium (in Qt PDF)")};
    const bool other = names.join(QLatin1Char('\n')).contains(QLatin1String("qt6-base"));
#else
    const QStringList want = {QStringLiteral("Qt " QT_VERSION_STR), QStringLiteral("hunspell")};
    const bool other = names.join(QLatin1Char('\n')).contains(QLatin1String("Microsoft Visual C++"));
#endif
    QStringList absent;
    for (const QString &w : want)
        if (!names.join(QLatin1Char('\n')).contains(w))
            absent << w;
    check(absent.isEmpty() && !other, QStringLiteral("the list is the one of this system's program"), absent.join(QLatin1String(", ")));

    // ---- spelling
    SpellChecker &sp = SpellChecker::instance();
    const QStringList five = {QStringLiteral("en_US"), QStringLiteral("en_GB"), QStringLiteral("de_DE"), QStringLiteral("de_AT"), QStringLiteral("de_CH")};
#ifdef Q_OS_WIN
    {
        const QStringList langs = sp.availableLanguages();
        QStringList lack;
        for (const QString &l : five)
            if (!langs.contains(l))
                lack << l;
        check(lack.isEmpty(), QStringLiteral("Windows: the dictionaries beside the program are found"), QStringLiteral("missing: ") + lack.join(QLatin1Char(' ')));
    }
#endif
    const QString given = qEnvironmentVariable("QB_TEST_DICTIONARIES");
    if (!given.isEmpty()) {
        const QString to = dataDir() + QLatin1String("/dictionaries");
        QDir().mkpath(to);
        for (const QFileInfo &fi : QDir(given).entryInfoList({QStringLiteral("*.aff"), QStringLiteral("*.dic")}, QDir::Files))
            QFile::copy(fi.absoluteFilePath(), to + QLatin1Char('/') + fi.fileName());
    }
    if (!given.isEmpty() || sp.availableLanguages().contains(QStringLiteral("de_CH"))) {
        struct Word {
            const char *lang, *word;
            bool right;
        };
        const Word words[] = {{"en_US", "color", true},   {"en_US", "colour", false}, {"en_GB", "colour", true},
                              {"en_GB", "colr", false},   {"de_DE", "Haus", true},    {"de_DE", "Straße", true},
                              {"de_DE", "Hauss", false},  {"de_AT", "Jänner", true},  {"de_CH", "Strasse", true},
                              {"de_CH", "Wörterbuch", true}};
        QStringList wrong;
        for (const Word &w : words) {
            if (!sp.setLanguage(QString::fromLatin1(w.lang))) {
                wrong << QStringLiteral("%1 does not load").arg(QLatin1String(w.lang));
                continue;
            }
            if (sp.isCorrect(QString::fromUtf8(w.word)) != w.right)
                wrong << QStringLiteral("%1: %2").arg(QLatin1String(w.lang), QString::fromUtf8(w.word));
        }
        check(wrong.isEmpty(), QStringLiteral("the five dictionaries load and know their words"), wrong.join(QLatin1String(", ")));
    } else {
        std::printf("(spelling: no dictionaries given, checks left out)\n");
    }

    QDir(dataDir()).removeRecursively();
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
