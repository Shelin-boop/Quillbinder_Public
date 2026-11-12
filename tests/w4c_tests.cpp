// Windows port, step W4c (2026-11-12): the add-on "Speech Transcription" on the system it was built for. Runs on
// Linux (the AppImage's add-on, <build>/addons-dist/speech-transcription.qbpkg) and on the Windows runner (the Windows
// add-on, QB_TEST_SPEECH_PACKAGE, built by packaging/windows/win-addons.sh with whisper.cpp for the processor only).
//   - the package installs; its files are there (the engine, whisper.cpp with a compute module per processor
//     generation, Qt Multimedia, the test recording, the licenses); on Windows no graphics-card module
//   - the tool is loaded from the installed add-on
//   - with a real model (QB_SPEECH_MODEL_FILE, whisper.cpp's ggml-tiny.bin is enough): the model check passes (the
//     engine and whisper.cpp load from the add-on, the test recording is heard), and a recording from a file is
//     written as text. The program's data folder, the model and the recording sit in folders whose names have
//     letters such as "ü" (a writer's name in C:\Users\...): on Linux this test sets them so itself; on the runner
//     packaging/windows/win-tests.sh gives the data folder such a name (QB_DATA_DIR).
// Without QB_SPEECH_MODEL_FILE the model checks are reported as not run. Exit code = number of failed checks.
//
//   QB_SPEECH_MODEL_FILE=ggml-tiny.bin xvfb-run -a ./qb_w4c_tests /tmp/shots
#include "AppInfo.h"
#include "compile/PluginManager.h"
#include "core/Addons.h"
#include "core/Paths.h"
#include "tools/ToolApi.h"
#include "ui/ToolHost.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>
#include <functional>

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

static void note(const QString &text)
{
    std::printf("      (%s)\n", qPrintable(text));
    std::fflush(stdout);
}

static bool waitFor(const std::function<bool()> &done, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > ms)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QTest::qWait(20);
    }
    return true;
}

static QObject *speechTool()
{
    for (tools::ToolInterface *t : compile::PluginManager::instance().tools())
        if (t->id() == QLatin1String("speech-transcription"))
            return dynamic_cast<QObject *>(t);
    return nullptr;
}

static bool callBool(QObject *o, const char *slot)
{
    bool r = false;
    QMetaObject::invokeMethod(o, slot, Qt::DirectConnection, Q_RETURN_ARG(bool, r));
    return r;
}

static QString callString(QObject *o, const char *slot)
{
    QString r;
    QMetaObject::invokeMethod(o, slot, Qt::DirectConnection, Q_RETURN_ARG(QString, r));
    return r;
}

static QString readText(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

static bool hasNonAscii(const QString &s)
{
    for (const QChar c : s)
        if (c.unicode() > 127)
            return true;
    return false;
}

int main(int argc, char **argv)
{
    // a folder whose name has letters outside ASCII, as in C:\Users\Jürgen
    QTemporaryDir tmp;
    const QString odd = tmp.path() + QStringLiteral("/Pr\u00fcfung \u00c4 \u00df");
    QDir().mkpath(odd);
#ifndef Q_OS_WIN
    qputenv("HOME", odd.toUtf8());
    qputenv("XDG_CONFIG_HOME", (odd + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (odd + QLatin1String("/data")).toUtf8());
#endif
    qputenv("QB_NO_DIALOGS", "1");
    qputenv("QB_SPEECH_FAST", "1");             // a recording from a file runs faster than real time
    qputenv("QB_SPEECH_ACCEPT_CHANGED", "1");   // a model that is not one of the catalog's (tiny) is checked without asking
    const QString buildDir = QStringLiteral(QB_TEST_BUILD_DIR);
    QCoreApplication::setOrganizationName(QStringLiteral("QuillbinderW4cTest"));
    QCoreApplication::setApplicationName(QStringLiteral("QuillbinderW4cTest"));
    QApplication app(argc, argv);

    const QString data = qb::dataDir();
    note(QStringLiteral("data folder: %1").arg(data));
    check(hasNonAscii(data), QStringLiteral("(the program's data folder has letters outside ASCII in its name)"), data);

    // ================================================================ the package
#ifdef Q_OS_WIN
    const QString pkg = qEnvironmentVariable("QB_TEST_SPEECH_PACKAGE",
                                             buildDir + QStringLiteral("/addons-dist/speech-transcription-windows.qbpkg"));
#else
    const QString pkg = qEnvironmentVariable("QB_TEST_SPEECH_PACKAGE",
                                             buildDir + QStringLiteral("/addons-dist/speech-transcription.qbpkg"));
#endif
    const addons::PackageInfo info = addons::inspect(pkg);
    QStringList log;
    QString err;
    bool restart = false;
    const bool installed = info.isValid() && addons::install(pkg, &log, &err, &restart);
#ifdef Q_OS_WIN
    check(installed && info.manifest.platform == QLatin1String("windows-x64"),
          QStringLiteral("the Windows add-on \"Speech Transcription\" installs (built for windows-x64)"),
          pkg + QLatin1String(": ") + info.error + err + QLatin1String(" platform ") + info.manifest.platform);
#else
    check(installed, QStringLiteral("the add-on \"Speech Transcription\" installs"), pkg + QLatin1String(": ") + info.error + err);
#endif
    const QString dir = addons::contentDirs(QStringLiteral("speech")).value(0);
    QStringList missing;
#ifdef Q_OS_WIN
    const char *const files[] = {"../plugins/qb-speech.dll", "qb-speech-impl.dll", "Qt6Multimedia.dll", "lib/whisper.dll",
                                 "lib/ggml.dll", "lib/ggml-base.dll", "lib/ggml-cpu-x64.dll", "lib/ggml-cpu-sse42.dll",
                                 "lib/ggml-cpu-haswell.dll", "lib/ggml-cpu-alderlake.dll", "samples/jfk.wav",
                                 "licenses/README.txt", "licenses/whisper.cpp-MIT.txt", "licenses/Qt-LGPL-3.0.txt",
                                 "licenses/GPL-3.0.txt"};
#else
    const char *const files[] = {"../plugins/libqb-speech.so", "libqb-speech-impl.so", "lib/libwhisper.so.1", "lib/libggml.so.0",
                                 "lib/libggml-base.so.0", "lib/libggml-cpu-x64.so", "lib/libggml-cpu-haswell.so",
                                 "lib/libQt6Multimedia.so.6", "samples/jfk.wav", "licenses/README.txt",
                                 "licenses/whisper.cpp-MIT.txt", "licenses/Qt-LGPL-3.0.txt"};
#endif
    for (const char *f : files)
        if (!QFileInfo::exists(dir + QLatin1Char('/') + QLatin1String(f)))
            missing << QLatin1String(f);
    check(!dir.isEmpty() && missing.isEmpty(),
          QStringLiteral("its files: the tool, the engine, whisper.cpp with a compute module per processor generation, Qt "
                         "Multimedia, the test recording, the licenses"),
          dir + QLatin1String(" missing: ") + missing.join(QLatin1String(", ")));
    const QString readme = readText(dir + QStringLiteral("/licenses/README.txt"));
    const QStringList modules = QDir(dir + QStringLiteral("/lib")).entryList({QStringLiteral("*ggml-cpu-*")}, QDir::Files);
#ifdef Q_OS_WIN
    check(readme.contains(QLatin1String("whisper.cpp v1.9.5")) && readme.contains(QLatin1String("MIT"))
              && readme.contains(QLatin1String("Qt6Multimedia.dll")) && !readme.contains(QLatin1String("Vulkan"))
              && !QFileInfo::exists(dir + QStringLiteral("/lib/ggml-vulkan.dll")) && modules.size() >= 5,
          QStringLiteral("Windows: the processor only (no graphics-card module), and the license notes name whisper.cpp and "
                         "Qt Multimedia"),
          modules.join(QLatin1Char(' ')));
    check(info.manifest.description.contains(QLatin1String("processor")) && !info.manifest.description.contains(QLatin1String("6.4.2"))
              && !info.manifest.description.contains(QLatin1String("Vulkan")),
          QStringLiteral("Windows: the add-on's description says the processor and names no graphics cards"),
          info.manifest.description);
#else
    check(readme.contains(QLatin1String("whisper.cpp")) && readme.contains(QLatin1String("MIT")) && modules.size() >= 5,
          QStringLiteral("the license notes name whisper.cpp; one compute module per processor generation"),
          modules.join(QLatin1Char(' ')));
#endif
    note(QStringLiteral("compute modules: %1").arg(modules.join(QLatin1Char(' '))));

    // ================================================================ the tool
    compile::PluginManager::instance().load();
    toolhost::host().attachTools(); // as the window does at its start (no window here)
    QObject *tool = speechTool();
    check(tool != nullptr, QStringLiteral("the writing tool \"Speech Transcription\" is loaded from the installed add-on"),
          compile::PluginManager::instance().messages().join(QLatin1String("; ")));

    // ================================================================ a real model
    const QString model = qEnvironmentVariable("QB_SPEECH_MODEL_FILE");
    if (tool && QFileInfo::exists(model)) {
        const QString models = odd + QStringLiteral("/Modelle \u00fc");
        const QString copy = models + QStringLiteral("/ggml-tiny \u00fc.bin");
        QDir().mkpath(models);
        QFile::remove(copy);
        QFile::copy(model, copy);
        const QString texts = odd + QStringLiteral("/Transkripte \u00fc");
        toolhost::host().setSetting(QStringLiteral("speech-transcription"), QStringLiteral("folder"), texts);
        toolhost::host().setSetting(QStringLiteral("speech-transcription"), QStringLiteral("mode"), QStringLiteral("live"));
        toolhost::host().setSetting(QStringLiteral("speech-transcription"), QStringLiteral("gpu"), -1);
        toolhost::host().setSetting(QStringLiteral("speech-transcription"), QStringLiteral("main"), QStringLiteral("en"));
        QElapsedTimer t;
        t.start();
        QMetaObject::invokeMethod(tool, "useLocalModel", Qt::DirectConnection, Q_ARG(QString, copy));
        const bool done = waitFor([tool] { return !callBool(tool, "modelJobRunning"); }, 600000);
        const QString message = callString(tool, "modelMessage");
        const QString installedFile =
            toolhost::host().setting(QStringLiteral("speech-transcription"), QStringLiteral("modelFile")).toString();
        check(done && message.startsWith(QLatin1String("Installed")) && installedFile.startsWith(data)
                  && QFileInfo::exists(installedFile),
              QStringLiteral("a model in a folder named with \"\u00fc\" passes the check: the engine and whisper.cpp load from "
                             "the add-on (in the data folder named with \"\u00fc\"), and the test recording is heard"),
              message + QLatin1String(" / ") + installedFile);
        note(QStringLiteral("model check (%1 s): %2").arg(t.elapsed() / 1000).arg(message));

        // a recording from a file, as the microphone would deliver it
        const QString wav = odd + QStringLiteral("/Aufnahme \u00e4.wav");
        QFile::remove(wav);
        QFile::copy(dir + QStringLiteral("/samples/jfk.wav"), wav);
        bool started = false;
        t.restart();
        QMetaObject::invokeMethod(tool, "startRecording", Qt::DirectConnection, Q_RETURN_ARG(bool, started), Q_ARG(QString, wav));
        const QString file = callString(tool, "recordingFile");
        waitFor([tool] { return !callBool(tool, "isBusy"); }, 600000);
        const QString text = readText(file);
        check(started && file.startsWith(texts) && text.contains(QLatin1String("country"), Qt::CaseInsensitive),
              QStringLiteral("a recording is written as text into the transcription folder named with \"\u00fc\""),
              QStringLiteral("started %1, file %2, last error %3: %4")
                  .arg(started).arg(file, callString(tool, "lastError"), text));
        note(QStringLiteral("recording (%1 s): %2").arg(t.elapsed() / 1000).arg(text.simplified()));
        check(callBool(tool, "removeModel") && !QFileInfo::exists(installedFile),
              QStringLiteral("Remove deletes the model again"));
    } else {
        note(QStringLiteral("model checks not run (QB_SPEECH_MODEL_FILE not set or not there)"));
    }

    std::printf("%d failure(s)\n", failures);
    return failures;
}
