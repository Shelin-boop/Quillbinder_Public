// Scripted run for version 0.50: the release on the project page. The version and the
// project page; Help > Third-Party Licenses names, for every library, the file of the
// release "Third-party source code" that holds its source (tools/collect-licenses.py,
// tools/collect-sources.py); the media add-ons split in two: "Media Playback" 1.1 with an
// FFmpeg for free formats only, and "Extra Video and Audio Formats" with the full FFmpeg,
// used instead while it is installed and switched on; a file in a format the free set
// cannot decode says which formats and which add-on; the release text, the AppStream
// description, the release workflow. Saves screenshots into the folder given as first
// argument. Exit code = number of failed checks.
//
//   tools/get-ffmpeg.sh; packaging/build-addons.sh build build/addons-dist
//   pulseaudio -D --exit-idle-time=-1     (any sound server; a null sink is enough)
//   QT_QPA_PLATFORM=offscreen ./qb_v050_tests /tmp/shots
//
// The run starts itself four more times (child runs, one FFmpeg per process): with Media
// Playback alone, with the extra formats installed, with them switched off, and removed.
#include "compile/PluginManager.h"
#include "core/Addons.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "import/FileImporter.h"
#include "media/MediaApi.h"
#include "ui/AppSettings.h"
#include "ui/EditorPane.h"
#include "ui/LicensesDialog.h"
#include "ui/MainWindow.h"
#include "ui/MediaPlayback.h"
#include "ui/MediaView.h"
#include "ui/Theme.h"

#include "AppInfo.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProcess>
#include <QRegularExpression>
#include <QSplitter>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <cstdio>
#include <functional>
#include <memory>

using namespace qb;

static int failures = 0;
static QString outDir;
static QString prefix; // child runs mark their lines
static const QString kSource = QStringLiteral(QB_TEST_SOURCE_DIR "/..");
static const QString kBuild = QStringLiteral(QB_TEST_BUILD_DIR);

static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s%s  %s\n", qPrintable(prefix), ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail.left(3000)));
    }
    std::fflush(stdout);
}

static void settle(int ms = 150)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }
}

static bool waitFor(const std::function<bool()> &cond, int ms = 6000)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        if (cond())
            return true;
        settle(40);
    }
    return cond();
}

static void shot(QWidget *w, const QString &name)
{
    settle(300);
    w->grab().save(outDir + QLatin1Char('/') + name + QLatin1String(".png"));
}

static QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static QRgb centerPixel(QWidget *w)
{
    const QImage img = w ? w->grab().toImage() : QImage();
    return img.isNull() ? 0 : img.pixel(img.width() / 2, img.height() / 2);
}

static bool near(QRgb c, int r, int g, int b, int tol = 60)
{
    return qAbs(qRed(c) - r) <= tol && qAbs(qGreen(c) - g) <= tol && qAbs(qBlue(c) - b) <= tol;
}

static QString rgbText(QRgb c) { return QStringLiteral("rgb(%1,%2,%3)").arg(qRed(c)).arg(qGreen(c)).arg(qBlue(c)); }

static QWidget *videoWidget(MediaView *v) { return v ? v->findChild<QWidget *>(QStringLiteral("qbVideoView")) : nullptr; }

static bool mapsContain(const QString &text)
{
    QFile f(QStringLiteral("/proc/self/maps"));
    return f.open(QIODevice::ReadOnly) && QString::fromLocal8Bit(f.readAll()).contains(text);
}

// runs a command, returns its output (stdout and stderr) and puts its exit code in *rc
static QString run(const QString &program, const QStringList &args, int *rc = nullptr, const QString &dir = QString())
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    if (!dir.isEmpty())
        p.setWorkingDirectory(dir);
    p.start(program, args);
    if (!p.waitForFinished(300000)) {
        p.kill();
        if (rc)
            *rc = -1;
        return QStringLiteral("(did not finish)");
    }
    if (rc)
        *rc = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    return QString::fromUtf8(p.readAll());
}

// the files of a package (a ZIP file): via python, as the program's own reader is internal
static QString zipEntry(const QString &zip, const QString &entry)
{
    int rc = 0;
    const QString out = run(QStringLiteral("python3"), {QStringLiteral("-I"), QStringLiteral("-c"),
                                                         QStringLiteral("import sys,zipfile; sys.stdout.write(zipfile.ZipFile(sys.argv[1])"
                                                                        ".read(sys.argv[2]).decode('utf-8','replace'))"),
                                                         zip, entry},
                            &rc);
    return rc == 0 ? out : QString();
}

// ==================================================================== media fixture
struct Fixture {
    std::unique_ptr<QTemporaryDir> projects;
    MainWindow *w = nullptr;
    Project *proj = nullptr;
    EditorPane *pane0 = nullptr;
    QMap<QString, BinderItem *> media;
};

static bool setUp(Fixture &f, const QStringList &files)
{
    f.projects = std::make_unique<QTemporaryDir>();
    templates::Info blank;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("novel"))
            blank = t;
    QString err;
    auto p = templates::createProject(blank, f.projects->path(), QStringLiteral("Recordings"), &err);
    if (!p) {
        check(false, QStringLiteral("create project"), err);
        return false;
    }
    f.w = new MainWindow;
    f.w->move(0, 0);
    f.w->resize(1300, 900);
    f.w->show();
    f.w->activateWindow();
    (void)QTest::qWaitForWindowActive(f.w, 2000);
    if (!f.w->adoptProject(std::move(p))) {
        check(false, QStringLiteral("open project"));
        return false;
    }
    f.proj = f.w->project();
    settle(300);
    QStringList paths, warnings;
    for (const QString &n : files)
        paths << QStringLiteral(QB_TEST_SOURCE_DIR "/media/") + n;
    const QList<BinderItem *> imported = importer::importFiles(f.proj, paths, f.proj->research(), -1, &warnings);
    for (BinderItem *it : imported)
        for (const QString &n : files)
            if (QFileInfo(n).completeBaseName() == it->title && QFileInfo(n).suffix() == it->fileExt)
                f.media.insert(n, it);
    f.proj->model()->refreshAll();
    f.proj->save();
    auto *editorSplit = qobject_cast<QSplitter *>(f.w->findChild<EditorPane *>()->parentWidget());
    f.pane0 = editorSplit ? qobject_cast<EditorPane *>(editorSplit->widget(0)) : nullptr;
    const bool ok = f.pane0 && f.media.size() == files.size();
    if (!ok)
        check(false, QStringLiteral("media files imported"), warnings.join(QLatin1Char(' ')));
    return ok;
}

static bool install(const QString &pkg, const QString &what)
{
    QStringList log;
    QString err;
    bool restart = false;
    const bool ok = addons::install(pkg, &log, &err, &restart);
    check(ok, QStringLiteral("the add-on \"%1\" installs").arg(what), err);
    return ok;
}

// shows a file in the editor; true when it plays and its picture (if any) has the colour
static bool plays(Fixture &f, const QString &file, int r, int g, int b, bool video, QString *detail)
{
    MediaView *v = f.pane0->media();
    f.pane0->showItems({f.media.value(file)});
    if (!waitFor([&] { return v->isPlayable() || !v->playerError().isEmpty(); }, 8000)) {
        *detail = QStringLiteral("not loaded: %1").arg(v->playerError());
        return false;
    }
    v->play();
    QRgb c = 0;
    const bool ok = waitFor([&] {
        if (v->position() < 500)
            return false;
        if (!video)
            return true;
        c = centerPixel(videoWidget(v));
        return near(c, r, g, b);
    }, 8000);
    *detail = QStringLiteral("position %1, pixel %2, error \"%3\"").arg(v->position()).arg(rgbText(c), v->playerError());
    v->pause(false);
    settle(200);
    return ok;
}

// shows a file in the editor; returns the message under the player (empty: none shown)
static QString message(Fixture &f, const QString &file)
{
    MediaView *v = f.pane0->media();
    f.pane0->showItems({f.media.value(file)});
    waitFor([&] { return !v->playerError().isEmpty() || v->isPlayable(); }, 8000);
    settle(300);
    auto *msg = v->findChild<QLabel *>(QStringLiteral("mediaError"));
    return msg && msg->isVisible() ? msg->text() : QString();
}

static void initApp()
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setThemeName(QStringLiteral("qbicons"));
    QIcon::setFallbackThemeName(QStringLiteral("qbicons"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
    AppSettings &as = AppSettings::instance();
    as.load();
    as.theme = QStringLiteral("light");
    as.spellCheck = false;
    as.backupOnClose = false;
    as.smartQuotes = false;
    as.welcomeShown = true;
    as.mediaBackend = QStringLiteral("ffmpeg");
    as.save();
    theme::apply();
}

static const QString kExtraId = QStringLiteral("media-extra-formats");

// ==================================================================== child: Media Playback alone
static int runBase()
{
    prefix = QStringLiteral("[Media Playback alone] ");
    if (!install(kBuild + QStringLiteral("/addons-dist/media-playback.qbpkg"), QStringLiteral("Media Playback")))
        return failures;
    compile::PluginManager::instance().load();
    media::EngineInterface *e = playback::engine();
    check(e && e->version() == QLatin1String("1.1"), QStringLiteral("the media engine of Media Playback 1.1 is loaded"),
          e ? e->version() : QStringLiteral("none"));
    if (!e)
        return failures;
    {
        const QList<media::Backend> b = e->backends();
        check(!b.isEmpty() && b[0].id == QLatin1String("ffmpeg") && b[0].available
                  && b[0].note.contains(QLatin1String("free formats")) && b[0].note.contains(QLatin1String("Extra Video and Audio Formats")),
              QStringLiteral("the built-in decoding says it plays free formats and names the add-on for the others"),
              b.isEmpty() ? QString() : b[0].note);
    }
    Fixture f;
    if (!setUp(f, {QStringLiteral("tone.mp3"), QStringLiteral("tone.ogg"), QStringLiteral("blue.webm"), QStringLiteral("red.mp4"),
                   QStringLiteral("green.mkv")}))
        return failures + 1;
    QString d;
    check(plays(f, QStringLiteral("tone.mp3"), 0, 0, 0, false, &d), QStringLiteral("an MP3 file plays"), d);
    check(e->isStarted() && mapsContain(QStringLiteral("/media/lib/libavcodec.so.60"))
              && !mapsContain(QStringLiteral("/media-extra/lib/")) && e->description().contains(QLatin1String("free formats")),
          QStringLiteral("the add-on's own FFmpeg (free formats) is the one loaded"), e->description());
    check(plays(f, QStringLiteral("tone.ogg"), 0, 0, 0, false, &d), QStringLiteral("an Ogg Vorbis file plays"), d);
    check(plays(f, QStringLiteral("blue.webm"), 0, 0, 255, true, &d), QStringLiteral("a WebM video (VP9, Opus) plays: the picture is blue"), d);
    const QString mp4 = message(f, QStringLiteral("red.mp4"));
    shot(f.w, QStringLiteral("v050_mp4_without_extra_formats"));
    check(mp4.contains(QLatin1String("H.264")) && mp4.contains(QLatin1String("AAC"))
              && mp4.contains(QLatin1String("\"Extra Video and Audio Formats\"")) && mp4.contains(QLatin1String("GStreamer"))
              && mp4.contains(QLatin1String("File > Options > Behaviors > Playback")),
          QStringLiteral("an MP4 video (H.264, AAC) says which formats it uses, the add-on that plays them, and the GStreamer way"), mp4);
    {
        MediaView *v = f.pane0->media();
        v->play();
        settle(800);
        check(v->position() == 0 && !v->isPlaying() && v->playerError() == mp4.section(QLatin1String(": "), 1),
              QStringLiteral("Play does nothing then, and the message stays"), QStringLiteral("position %1").arg(v->position()));
    }
    const QString mkv = message(f, QStringLiteral("green.mkv"));
    check(mp4.contains(QLatin1String("H.264 and AAC, formats licensed under patents")),
          QStringLiteral("two formats are named as formats (plural)"), mp4);
    check(mkv.contains(QLatin1String("MPEG-4 video, a format licensed under patents")) && !mkv.contains(QLatin1String("AAC")),
          QStringLiteral("a Matroska video in MPEG-4 Part 2 names that format only"), mkv);
    check(plays(f, QStringLiteral("tone.mp3"), 0, 0, 0, false, &d) && message(f, QStringLiteral("tone.mp3")).isEmpty(),
          QStringLiteral("after that, an MP3 file plays again and shows no message"), d);
    f.w->close();
    delete f.w;
    return failures;
}

// ==================================================================== child: with the extra formats
static int runExtra()
{
    prefix = QStringLiteral("[with the extra formats] ");
    const QString pkg = kBuild + QStringLiteral("/addons-dist/media-extra-formats.qbpkg");
    const addons::PackageInfo info = addons::inspect(pkg);
    check(info.isValid() && info.provides.join(QLatin1Char(' ')).contains(QLatin1String("licensed under patents")),
          QStringLiteral("the Add-on Manager says what the add-on brings"), info.provides.join(QLatin1String(", ")) + info.error);
    if (!install(pkg, QStringLiteral("Extra Video and Audio Formats")))
        return failures;
    compile::PluginManager::instance().load();
    media::EngineInterface *e = playback::engine();
    if (!e) {
        check(false, QStringLiteral("the media engine is loaded"));
        return failures;
    }
    const QList<media::Backend> b = e->backends();
    check(!b.isEmpty() && b[0].note.contains(QLatin1String("H.264")) && !b[0].note.contains(QLatin1String("free formats")),
          QStringLiteral("the built-in decoding now names H.264, H.265 and AAC"), b.isEmpty() ? QString() : b[0].note);
    Fixture f;
    if (!setUp(f, {QStringLiteral("red.mp4"), QStringLiteral("green.mkv"), QStringLiteral("tone.mp3")}))
        return failures + 1;
    QString d;
    check(plays(f, QStringLiteral("red.mp4"), 255, 0, 0, true, &d), QStringLiteral("the MP4 video (H.264, AAC) plays: the picture is red"), d);
    check(mapsContain(QStringLiteral("/media-extra/lib/libavcodec.so.60")) && !mapsContain(QStringLiteral("/media/lib/libavcodec.so.60"))
              && e->description().contains(QLatin1String("extra formats")),
          QStringLiteral("the full FFmpeg of \"Extra Video and Audio Formats\" is the one loaded"), e->description());
    check(plays(f, QStringLiteral("green.mkv"), 0, 128, 0, true, &d), QStringLiteral("the Matroska video (MPEG-4 Part 2) plays: the picture is green"), d);
    check(plays(f, QStringLiteral("tone.mp3"), 0, 0, 0, false, &d), QStringLiteral("an MP3 file plays as well"), d);
    shot(f.w, QStringLiteral("v050_mp3_with_extra_formats"));
    f.w->close();
    delete f.w;
    return failures;
}

// ==================================================================== child: switched off, then removed
static int runExtraGone(bool removed)
{
    prefix = removed ? QStringLiteral("[extra formats removed] ") : QStringLiteral("[extra formats switched off] ");
    compile::PluginManager::instance().load();
    media::EngineInterface *e = playback::engine();
    if (!e) {
        check(false, QStringLiteral("the media engine is loaded"));
        return failures;
    }
    Fixture f;
    if (!setUp(f, {QStringLiteral("red.mp4"), QStringLiteral("tone.mp3")}))
        return failures + 1;
    QString d;
    check(plays(f, QStringLiteral("tone.mp3"), 0, 0, 0, false, &d), QStringLiteral("an MP3 file plays"), d);
    check(mapsContain(QStringLiteral("/media/lib/libavcodec.so.60")) && !mapsContain(QStringLiteral("/media-extra/lib/")),
          QStringLiteral("Media Playback's own FFmpeg is loaded again"), e->description());
    const QString mp4 = message(f, QStringLiteral("red.mp4"));
    check(mp4.contains(QLatin1String("H.264")) && mp4.contains(QLatin1String("Extra Video and Audio Formats")),
          QStringLiteral("the MP4 video names its formats and the add-on again"), mp4);
    f.w->close();
    delete f.w;
    return failures;
}

static int runChild(const QString &mode, const QProcessEnvironment &env)
{
    QProcess p;
    p.setProcessEnvironment(env);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(QCoreApplication::applicationFilePath(), {outDir, mode});
    if (!p.waitForFinished(240000)) {
        p.kill();
        check(false, QStringLiteral("child run %1 finished").arg(mode));
        return 1;
    }
    std::printf("%s", p.readAll().constData());
    std::fflush(stdout);
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : 1;
}

static void setHome(const QString &home)
{
    qputenv("HOME", home.toUtf8());
    qputenv("XDG_CONFIG_HOME", (home + QLatin1String("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (home + QLatin1String("/data")).toUtf8());
    qputenv("XDG_RUNTIME_DIR", (home + QLatin1String("/run")).toUtf8());
    QDir().mkpath(home + QLatin1String("/run"));
    QFile::setPermissions(home + QLatin1String("/run"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

static QJsonObject licenseIndex()
{
    return QJsonDocument::fromJson(readAll(QStringLiteral(":/licenses/index.json"))).object();
}

static QString safeName(QString s) { return s.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_")); }

int main(int argc, char **argv)
{
    const QString mode = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    std::unique_ptr<QTemporaryDir> home;
    if (mode.isEmpty()) {
        home = std::make_unique<QTemporaryDir>();
        setHome(home->path());
    }
    qputenv("QB_NO_DIALOGS", "1");
    qputenv("QB_NO_WELCOME", "1");
    QCoreApplication::setOrganizationName(QStringLiteral("Quillbinder"));
    QCoreApplication::setApplicationName(QStringLiteral("Quillbinder"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    initApp();
    if (mode == QLatin1String("--base"))
        return runBase();
    if (mode == QLatin1String("--extra"))
        return runExtra();
    if (mode == QLatin1String("--extra-off"))
        return runExtraGone(false);
    if (mode == QLatin1String("--extra-removed"))
        return runExtraGone(true);

    // ================================================================ version and project page
    check(QLatin1String(QB_APP_VERSION) == QLatin1String("0.50.0") && QLatin1String(QB_APP_VERSION_SHOWN) == QLatin1String("0.50.0 alpha"),
          QStringLiteral("the version is 0.50.0, shown as \"0.50.0 alpha\""), QStringLiteral(QB_APP_VERSION_SHOWN));
    check(QLatin1String(QB_APP_HOMEPAGE) == QLatin1String("https://github.com/Shelin-boop/Quillbinder_Public")
              && QLatin1String(QB_SOURCES_URL) == QLatin1String("https://github.com/Shelin-boop/Quillbinder_Public/releases/tag/third-party-sources"),
          QStringLiteral("the project page and its release with the third-party source code"), QStringLiteral(QB_SOURCES_URL));

    // ================================================================ the license list names the source files
    const QJsonObject index = licenseIndex();
    const QJsonArray comps = index.value(QStringLiteral("components")).toArray();
    {
        int ubuntu = 0, runtimeParts = 0;
        QStringList bad;
        const QString runtimeFile = index.value(QStringLiteral("runtime")).toObject().value(QStringLiteral("sourceFile")).toString();
        for (const QJsonValue &v : comps) {
            const QJsonObject c = v.toObject();
            const QString src = c.value(QStringLiteral("source")).toString();
            if (c.contains(QStringLiteral("package"))) {
                ++ubuntu;
                const QString want = safeName(c.value(QStringLiteral("package")).toString()) + QLatin1Char('_')
                                     + safeName(c.value(QStringLiteral("version")).toString()) + QStringLiteral(".src.tar");
                if (c.value(QStringLiteral("sourceFile")).toString() != want || !src.startsWith(QLatin1String(QB_SOURCES_URL))
                    || !src.contains(want) || c.value(QStringLiteral("files")).toArray().isEmpty())
                    bad << c.value(QStringLiteral("name")).toString();
            } else if (c.value(QStringLiteral("runtimePart")).toBool()) {
                ++runtimeParts;
                if (runtimeFile.isEmpty() || c.value(QStringLiteral("sourceFile")).toString() != runtimeFile || !src.contains(runtimeFile))
                    bad << c.value(QStringLiteral("name")).toString();
            }
        }
        check(ubuntu >= 25 && runtimeParts == 6 && bad.isEmpty() && runtimeFile.startsWith(QLatin1String("appimage-type2-runtime-")),
              QStringLiteral("every Ubuntu library and every part of the AppImage runtime names its file in the release of the "
                             "third-party source code"),
              QStringLiteral("%1 Ubuntu, %2 runtime parts, wrong: %3").arg(ubuntu).arg(runtimeParts).arg(bad.join(QLatin1String(", "))));
    }
    {
        showThirdPartyLicenses(nullptr);
        settle(300);
        QDialog *dlg = nullptr;
        for (QWidget *w : QApplication::topLevelWidgets())
            if (w->objectName() == QLatin1String("licensesWindow") && w->isVisible())
                dlg = qobject_cast<QDialog *>(w);
        QLabel *intro = nullptr;
        if (dlg)
            for (QLabel *l : dlg->findChildren<QLabel *>())
                if (l->text().contains(QLatin1String("Zero-Clause")))
                    intro = l;
        auto *list = dlg ? dlg->findChild<QListWidget *>(QStringLiteral("licensesList")) : nullptr;
        auto *text = dlg ? dlg->findChild<QPlainTextEdit *>(QStringLiteral("licensesText")) : nullptr;
        QString qtText;
        if (list && text)
            for (int i = 0; i < list->count(); ++i)
                if (list->item(i)->text().startsWith(QLatin1String("qt6-base "))) {
                    list->setCurrentRow(i);
                    settle(100);
                    qtText = text->toPlainText();
                }
        if (dlg)
            dlg->resize(1100, 720);
        if (dlg)
            shot(dlg, QStringLiteral("v050_licenses_qt6_base"));
        check(intro && intro->openExternalLinks() && intro->text().contains(QLatin1String(QB_SOURCES_URL))
                  && intro->text().contains(QLatin1String("Third-party source code")),
              QStringLiteral("the licenses window links to the release \"Third-party source code\""), intro ? intro->text() : QString());
        check(qtText.contains(QLatin1String("Source: " QB_SOURCES_URL ", file qt6-base_"))
                  && qtText.contains(QLatin1String("apt-get source qt6-base=")),
              QStringLiteral("Qt's entry names its source file in that release and the Ubuntu package"), qtText.left(600));
        if (dlg)
            dlg->close();
        settle(100);
    }

    // ================================================================ the scripts behind it
    const QString tools = kSource + QStringLiteral("/tools/");
    {
        int rc = 0;
        const QString list = run(QStringLiteral("python3"), {QStringLiteral("-I"), tools + QStringLiteral("collect-sources.py"), QStringLiteral("--list")}, &rc);
        const QStringList names = list.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QStringList missing;
        for (const QJsonValue &v : comps) {
            const QString sf = v.toObject().value(QStringLiteral("sourceFile")).toString();
            if (!sf.isEmpty() && !names.contains(sf))
                missing << sf;
        }
        const QString ff = run(QStringLiteral("python3"), {QStringLiteral("-I"), tools + QStringLiteral("collect-sources.py"),
                                                           QStringLiteral("--name"), QStringLiteral("ffmpeg")}).trimmed();
        check(rc == 0 && missing.isEmpty() && names.contains(ff) && ff.startsWith(QLatin1String("ffmpeg-6.1.1-quillbinder-")),
              QStringLiteral("collect-sources.py lists a source file for every entry of the license list and for FFmpeg"),
              QStringLiteral("rc %1, missing %2, ffmpeg %3").arg(rc).arg(missing.join(QLatin1Char(' ')), ff));
        bool multimedia = false;
        for (const QString &n : names)
            multimedia = multimedia || n.startsWith(QLatin1String("qt6-multimedia_"));
        check(multimedia, QStringLiteral("... and for the libraries of the add-ons (Qt Multimedia)"), list.left(600));
    }
    {
        // --refresh on a copy of the list: this system's versions are those the list names
        QTemporaryDir tmp;
        run(QStringLiteral("cp"), {QStringLiteral("-r"), kSource + QStringLiteral("/resources/licenses"), tmp.path() + QStringLiteral("/licenses")});
        const QString rt = kSource + QStringLiteral("/packaging/tools/runtime-x86_64");
        QStringList args = {QStringLiteral("-I"), tools + QStringLiteral("collect-licenses.py"), QStringLiteral("--refresh"),
                            tmp.path() + QStringLiteral("/licenses")};
        if (QFileInfo::exists(rt))
            args << rt;
        int rc = 0;
        const QString out = run(QStringLiteral("python3"), args, &rc);
        check(rc == 0 && out.contains(QLatin1String("no changes"))
                  && readAll(tmp.path() + QStringLiteral("/licenses/index.json")) == readAll(kSource + QStringLiteral("/resources/licenses/index.json")),
              QStringLiteral("collect-licenses.py --refresh finds the versions of this system in the list (nothing to change)"), out);
    }

    // ================================================================ the two media add-ons
    const QString base = kBuild + QStringLiteral("/addons-dist/media-playback.qbpkg");
    const QString extra = kBuild + QStringLiteral("/addons-dist/media-extra-formats.qbpkg");
    {
        const addons::PackageInfo b = addons::inspect(base);
        const addons::PackageInfo x = addons::inspect(extra);
        check(b.isValid() && b.manifest.version == QLatin1String("1.1") && x.isValid() && x.manifest.id == kExtraId
                  && x.manifest.name == QLatin1String("Extra Video and Audio Formats") && x.manifest.version == QLatin1String("1.0")
                  && QFileInfo(base).size() < 30LL * 1024 * 1024 && QFileInfo(extra).size() < 30LL * 1024 * 1024,
              QStringLiteral("Media Playback 1.1 and Extra Video and Audio Formats 1.0 are valid packages below the 30 MiB delivery limit"),
              QStringLiteral("%1 %2 / %3 %4, %5 and %6 bytes").arg(b.manifest.version, b.error, x.manifest.version, x.error)
                  .arg(QFileInfo(base).size()).arg(QFileInfo(extra).size()));
        QStringList xf;
        for (const addons::FileEntry &fe : x.manifest.files)
            xf << fe.path;
        bool noCode = true;
        for (const QString &p : xf)
            noCode = noCode && (p.startsWith(QLatin1String("media-extra/lib/")) || p.startsWith(QLatin1String("media-extra/licenses/")));
        check(xf.contains(QStringLiteral("media-extra/lib/libavcodec.so.60")) && xf.contains(QStringLiteral("media-extra/lib/libavformat.so.60"))
                  && xf.contains(QStringLiteral("media-extra/lib/libavutil.so.58")) && xf.contains(QStringLiteral("media-extra/lib/libswresample.so.4"))
                  && xf.contains(QStringLiteral("media-extra/lib/libswscale.so.7")) && noCode,
              QStringLiteral("the extra formats are the five FFmpeg libraries and their licence notes, nothing else"), xf.join(QLatin1String(", ")));
        const QString baseDec = zipEntry(base, QStringLiteral("payload/media/licenses/ffmpeg-decoders.txt"));
        const QString extraDec = zipEntry(extra, QStringLiteral("payload/media-extra/licenses/ffmpeg-decoders.txt"));
        const QStringList bd = baseDec.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QStringList xd = extraDec.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QStringList patented;
        for (const QString &n : {QStringLiteral("h264"), QStringLiteral("hevc"), QStringLiteral("aac"), QStringLiteral("aac_latm"),
                                 QStringLiteral("ac3"), QStringLiteral("eac3"), QStringLiteral("dca"), QStringLiteral("mpeg4"),
                                 QStringLiteral("mpeg2video"), QStringLiteral("mpeg1video"), QStringLiteral("vc1"), QStringLiteral("wmav2"),
                                 QStringLiteral("amrnb"), QStringLiteral("h263"), QStringLiteral("prores")})
            if (bd.contains(n))
                patented << n;
        bool freeOnes = true;
        for (const QString &n : {QStringLiteral("mp3"), QStringLiteral("vorbis"), QStringLiteral("opus"), QStringLiteral("flac"),
                                 QStringLiteral("pcm_s16le"), QStringLiteral("vp8"), QStringLiteral("vp9"), QStringLiteral("theora"),
                                 QStringLiteral("mjpeg"), QStringLiteral("png")})
            freeOnes = freeOnes && bd.contains(n);
        check(bd.size() > 50 && bd.size() < 120 && patented.isEmpty() && freeOnes,
              QStringLiteral("Media Playback's FFmpeg has the decoders of free formats and none of the formats licensed under patents"),
              QStringLiteral("%1 decoders, patented: %2").arg(bd.size()).arg(patented.join(QLatin1Char(' '))));
        check(xd.size() > 400 && xd.contains(QStringLiteral("h264")) && xd.contains(QStringLiteral("hevc")) && xd.contains(QStringLiteral("aac"))
                  && !xd.contains(QStringLiteral("wmapro")) && !xd.contains(QStringLiteral("wmv3")),
              QStringLiteral("the extra formats' FFmpeg has every decoder but the two that crash Qt Multimedia"),
              QStringLiteral("%1 decoders").arg(xd.size()));
        const QString ff = run(QStringLiteral("python3"), {QStringLiteral("-I"), tools + QStringLiteral("collect-sources.py"),
                                                           QStringLiteral("--name"), QStringLiteral("ffmpeg")}).trimmed();
        const QString bReadme = zipEntry(base, QStringLiteral("payload/media/licenses/README.txt"));
        const QString xReadme = zipEntry(extra, QStringLiteral("payload/media-extra/licenses/README.txt"));
        check(bReadme.contains(QLatin1String(QB_SOURCES_URL)) && bReadme.contains(ff) && bReadme.contains(QLatin1String("qt6-multimedia_"))
                  && bReadme.contains(QLatin1String("libva_")) && bReadme.contains(QLatin1String("\"Media Playback\" 1.1"))
                  && xReadme.contains(QLatin1String(QB_SOURCES_URL)) && xReadme.contains(ff) && xReadme.contains(QLatin1String("patents")),
              QStringLiteral("both add-ons' licence notes name the source files of their libraries"), bReadme.left(400) + QLatin1String(" | ") + xReadme.left(400));
        const QString web = kBuild + QStringLiteral("/addons-dist/import-web.qbpkg");
        const QString wReadme = zipEntry(web, QStringLiteral("payload/licenses/README.txt"));
        check(wReadme.contains(QLatin1String(QB_SOURCES_URL)) && wReadme.contains(QLatin1String("qt6-base_")),
              QStringLiteral("the web import add-on's licence note names the source file of Qt's TLS plugin"), wReadme.left(500));
    }

    // ================================================================ playing: four child runs in one home
    {
        QTemporaryDir mediaHome;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("HOME"), mediaHome.path());
        env.insert(QStringLiteral("XDG_CONFIG_HOME"), mediaHome.path() + QStringLiteral("/config"));
        env.insert(QStringLiteral("XDG_DATA_HOME"), mediaHome.path() + QStringLiteral("/data"));
        env.insert(QStringLiteral("XDG_RUNTIME_DIR"), mediaHome.path() + QStringLiteral("/run"));
        QDir().mkpath(mediaHome.path() + QStringLiteral("/run"));
        QFile::setPermissions(mediaHome.path() + QStringLiteral("/run"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        failures += runChild(QStringLiteral("--base"), env);
        failures += runChild(QStringLiteral("--extra"), env);
        // switched off in the Add-on Manager (state.json of the add-ons folder)
        {
            const QByteArray old = qgetenv("XDG_DATA_HOME");
            qputenv("XDG_DATA_HOME", (mediaHome.path() + QStringLiteral("/data")).toUtf8());
            addons::setEnabled(kExtraId, false);
            QString err;
            failures += runChild(QStringLiteral("--extra-off"), env);
            addons::setEnabled(kExtraId, true);
            const bool removed = addons::removeAddon(kExtraId, &err);
            check(removed && !QFileInfo::exists(addons::addonsRoot() + QLatin1Char('/') + kExtraId),
                  QStringLiteral("the extra formats can be removed in the Add-on Manager"), err);
            failures += runChild(QStringLiteral("--extra-removed"), env);
            qputenv("XDG_DATA_HOME", old);
        }
    }

    // ================================================================ release text, workflow, descriptions
    {
        QTemporaryDir dist;
        for (const QString &n : {QStringLiteral("Quillbinder-0.50.0-alpha-x86_64.AppImage"), QStringLiteral("quillbinder-0.50.0-update.qbpkg"),
                                 QStringLiteral("quillbinder-0.50.0-symbols.xz"), QStringLiteral("SHA256SUMS.txt"),
                                 QStringLiteral("THIRD-PARTY-SOURCES.txt")}) {
            QFile f(dist.path() + QLatin1Char('/') + n);
            f.open(QIODevice::WriteOnly);
        }
        QFile::copy(base, dist.path() + QStringLiteral("/media-playback.qbpkg"));
        QFile::copy(extra, dist.path() + QStringLiteral("/media-extra-formats.qbpkg"));
        int rc = 0;
        const QString notes = run(QStringLiteral("python3"), {QStringLiteral("-I"), tools + QStringLiteral("release-notes.py"), dist.path()}, &rc);
        QFile out(outDir + QStringLiteral("/v050_release_notes.md"));
        if (out.open(QIODevice::WriteOnly))
            out.write(notes.toUtf8());
        check(rc == 0 && notes.contains(QLatin1String("**This is an alpha version.**")) && notes.contains(QLatin1String("Back Up Now"))
                  && notes.contains(QLatin1String("Quillbinder-0.50.0-alpha-x86_64.AppImage"))
                  && notes.contains(QLatin1String("Add-on \"Extra Video and Audio Formats\" 1.0"))
                  && notes.contains(QLatin1String("Add-on \"Media Playback\" 1.1"))
                  && notes.contains(QLatin1String("not made by or connected with Literature & Latte"))
                  && notes.contains(QLatin1String("releases/tag/third-party-sources")) && notes.contains(QLatin1String("What is new in 0.50.0 alpha")),
              QStringLiteral("the release text: alpha note with backups, every file with what it is, non-affiliation, source code, what is new"),
              notes.left(1500));
        const QString news = run(QStringLiteral("python3"), {QStringLiteral("-I"), tools + QStringLiteral("release-notes.py"), QStringLiteral("--whats-new")}, &rc);
        check(rc == 0 && news.split(QLatin1Char('\n'), Qt::SkipEmptyParts).size() >= 3 && news.contains(QLatin1String("Media Playback 1.1")),
              QStringLiteral("the short what's-new lines of the program update come from the list of changes"), news);
        const QString wf = QString::fromUtf8(readAll(kSource + QStringLiteral("/.github/workflows/release.yml")));
        check(wf.contains(QLatin1String("tags: ['v*']")) && wf.contains(QLatin1String("container: ubuntu:24.04"))
                  && wf.contains(QLatin1String("tools/get-ffmpeg.sh")) && wf.contains(QLatin1String("packaging/build-appimage.sh"))
                  && wf.contains(QLatin1String("tools/e2e-appimage.sh")) && wf.contains(QLatin1String("tools/collect-sources.py"))
                  && wf.contains(QLatin1String("tools/publish-release.sh")) && wf.contains(QLatin1String("contents: write")),
              QStringLiteral("the release workflow builds, checks, collects the sources and publishes on a version tag"));
        const QString meta = QString::fromUtf8(readAll(kSource + QStringLiteral("/packaging/quillbinder.metainfo.xml")));
        const QString desktop = QString::fromUtf8(readAll(kSource + QStringLiteral("/packaging/quillbinder.desktop")));
        check(meta.contains(QLatin1String("Scrivener 3 format")) && meta.contains(QLatin1String("not made by or connected"))
                  && meta.contains(QLatin1String("alpha version: keep backups")) && !desktop.contains(QLatin1String("Scrivener")),
              QStringLiteral("the AppStream description names Scrivener only as a file format, with the non-affiliation and alpha notes; "
                             "the desktop file does not name it"));
        if (!run(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("command -v appstreamcli")}).trimmed().isEmpty()) {
            const QString v = run(QStringLiteral("appstreamcli"), {QStringLiteral("validate"), QStringLiteral("--no-net"),
                                                                    kSource + QStringLiteral("/packaging/quillbinder.metainfo.xml")}, &rc);
            check(rc == 0, QStringLiteral("appstreamcli accepts the AppStream description"), v);
        }
        const QString readme = QString::fromUtf8(readAll(kSource + QStringLiteral("/README.md")));
        const QString third = QString::fromUtf8(readAll(kSource + QStringLiteral("/THIRD-PARTY.md")));
        check(readme.contains(QLatin1String("## Download")) && readme.contains(QLatin1String("alpha software"))
                  && readme.contains(QLatin1String("releases/tag/third-party-sources")) && third.contains(QLatin1String("releases/tag/third-party-sources"))
                  && third.contains(QLatin1String("Extra Video and Audio Formats")),
              QStringLiteral("README and THIRD-PARTY.md say where to download, that it is alpha software, and where the sources are"));
    }
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
