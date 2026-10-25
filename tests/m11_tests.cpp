// Scripted run for audio and video (milestone M11): the add-on "Media
// Playback", the player in the editor, copyholder and Quick Reference panels,
// the play controls, Navigate > Media, timestamps, the options. Saves
// screenshots into the folder given as first argument. Exit code = number of
// failed checks.
//
//   tools/build-ffmpeg.sh <ffmpeg-6.1.1 source> build/ffmpeg
//   packaging/build-addons.sh build build/addons-dist
//   pulseaudio -D --exit-idle-time=-1     (any sound server; a null sink is enough)
//   QT_QPA_PLATFORM=offscreen ./qb_m11_tests /tmp/shots
//
// The run starts itself three more times (child runs, own output): without
// the add-on, with GStreamer instead of FFmpeg, without a sound server.
#include "compile/PluginManager.h"
#include "core/Addons.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "core/ProjectTemplates.h"
#include "import/FileImporter.h"
#include "media/MediaApi.h"
#include "ui/AppSettings.h"
#include "ui/BinderPanel.h"
#include "ui/Commands.h"
#include "ui/Copyholder.h"
#include "ui/EditorPane.h"
#include "ui/MainWindow.h"
#include "ui/MediaPlayback.h"
#include "ui/MediaView.h"
#include "ui/OptionsDialog.h"
#include "ui/QuickRef.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"
#include "ui/Windows.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QProcess>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QToolButton>

#include <cstdio>
#include <functional>

using namespace qb;

static int failures = 0;
static QString outDir;
static QString prefix; // child runs mark their lines
static void check(bool ok, const QString &what, const QString &detail = QString())
{
    std::printf("%s%s  %s\n", qPrintable(prefix), ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) {
        ++failures;
        if (!detail.isEmpty())
            std::printf("      %s\n", qPrintable(detail));
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

static bool waitFor(const std::function<bool()> &cond, int ms = 5000)
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

static QString menuText(QString t)
{
    const int tab = int(t.indexOf(QLatin1Char('\t')));
    if (tab >= 0)
        t.truncate(tab);
    t.remove(QLatin1Char('&'));
    return t;
}

static QAction *menuPath(QMainWindow *w, const QStringList &path)
{
    QList<QAction *> level = w->menuBar()->actions();
    QAction *found = nullptr;
    for (int i = 0; i < path.size(); ++i) {
        found = nullptr;
        for (QAction *a : level)
            if (menuText(a->text()) == path[i]) {
                found = a;
                break;
            }
        if (!found)
            return nullptr;
        if (i + 1 < path.size()) {
            if (!found->menu())
                return nullptr;
            emit found->menu()->aboutToShow();
            level = found->menu()->actions();
        }
    }
    return found;
}

static QAction *byKey(QWidget *w, const QString &key)
{
    QAction *a = w->findChild<QAction *>(key);
    if (!a)
        std::printf("      (action key not found: %s)\n", qPrintable(key));
    return a;
}

static void keys(QWidget *window, const QKeySequence &ks)
{
    QTest::keySequence(window->windowHandle(), ks);
    settle(120);
}

static QRgb centerPixel(QWidget *w)
{
    const QImage img = w->grab().toImage();
    return img.isNull() ? 0 : img.pixel(img.width() / 2, img.height() / 2);
}

static bool near(QRgb c, int r, int g, int b, int tol = 60)
{
    return qAbs(qRed(c) - r) <= tol && qAbs(qGreen(c) - g) <= tol && qAbs(qBlue(c) - b) <= tol;
}

static QString rgbText(QRgb c)
{
    return QStringLiteral("rgb(%1,%2,%3)").arg(qRed(c)).arg(qGreen(c)).arg(qBlue(c));
}

static QWidget *videoWidget(MediaView *v)
{
    return v ? v->findChild<QWidget *>(QStringLiteral("qbVideoView")) : nullptr;
}

static bool mapsContain(const QString &text)
{
    QFile f(QStringLiteral("/proc/self/maps"));
    return f.open(QIODevice::ReadOnly) && QString::fromLocal8Bit(f.readAll()).contains(text);
}

struct Fixture {
    std::unique_ptr<QTemporaryDir> projects;
    MainWindow *w = nullptr;
    Project *proj = nullptr;
    BinderPanel *binder = nullptr;
    EditorPane *pane0 = nullptr;
    QMap<QString, BinderItem *> media; // by file name
    BinderItem *transcript = nullptr;
    BinderItem *other = nullptr;
};

static bool setUp(Fixture &f, const QStringList &files)
{
    f.projects = std::make_unique<QTemporaryDir>();
    templates::Info novel;
    for (const auto &t : templates::all())
        if (t.id == QLatin1String("novel"))
            novel = t;
    QString err;
    auto p = templates::createProject(novel, f.projects->path(), QStringLiteral("The Lighthouse"), &err);
    check(p != nullptr, QStringLiteral("create project"), err);
    if (!p)
        return false;
    f.w = new MainWindow;
    f.w->move(0, 0);
    f.w->resize(1300, 900);
    f.w->show();
    f.w->activateWindow();
    (void)QTest::qWaitForWindowActive(f.w, 2000);
    check(f.w->adoptProject(std::move(p)), QStringLiteral("open project in main window"));
    f.proj = f.w->project();
    settle(300);
    BinderModel *m = f.proj->model();
    m->moveToTrash(QList<BinderItem *>(f.proj->draft()->children));
    f.transcript = m->createItem(f.proj->draft(), 0, ItemType::Text, QStringLiteral("Transcript"));
    templates::setMarkdown(f.proj, f.transcript, QStringLiteral("Interview with the keeper.\n[00:07] The boat came at seven.\n"));
    QStringList paths, warnings;
    for (const QString &n : files)
        paths << QStringLiteral(QB_TEST_MEDIA_DIR "/") + n;
    const QList<BinderItem *> imported = importer::importFiles(f.proj, paths, f.proj->research(), -1, &warnings);
    for (BinderItem *it : imported)
        for (const QString &n : files)
            if (QFileInfo(n).completeBaseName() == it->title && QFileInfo(n).suffix() == it->fileExt)
                f.media.insert(n, it);
    m->refreshAll();
    f.proj->save();
    f.binder = f.w->findChild<BinderPanel *>();
    auto *editorSplit = qobject_cast<QSplitter *>(f.w->findChild<EditorPane *>()->parentWidget());
    f.pane0 = editorSplit ? qobject_cast<EditorPane *>(editorSplit->widget(0)) : nullptr;
    check(f.binder && f.pane0 && f.media.size() == files.size(),
          QStringLiteral("window parts found, %1 media files imported").arg(files.size()), warnings.join(QLatin1Char(' ')));
    return f.binder && f.pane0 && f.media.size() == files.size();
}

// ==================================================================== child: without the add-on
static int runWithoutAddon()
{
    prefix = QStringLiteral("[without add-on] ");
    Fixture f;
    if (!setUp(f, {QStringLiteral("tone.mp3")}))
        return failures + 1;
    MainWindow &w = *f.w;
    check(!playback::engine(), QStringLiteral("no media engine is loaded"));
    QAction *menu = menuPath(&w, {QStringLiteral("Navigate"), QStringLiteral("Media")});
    QAction *play = byKey(&w, QStringLiteral("mediaPlayPause"));
    bool missingShown = false;
    if (menu && menu->menu()) {
        emit menu->menu()->aboutToShow();
        for (QAction *a : menu->menu()->actions())
            missingShown = missingShown || (a->isVisible() && a->text().contains(QLatin1String("Media Playback")));
    }
    check(menu && play && !play->isEnabled() && missingShown,
          QStringLiteral("Navigate > Media is there; its commands are disabled and it names the add-on that is missing"));
    f.pane0->showItems({f.media.value(QStringLiteral("tone.mp3"))});
    settle(300);
    MediaView *v = f.pane0->media();
    auto *hint = v->findChild<QLabel *>(QStringLiteral("mediaHint"));
    QPushButton *open = nullptr;
    for (QPushButton *b : v->findChildren<QPushButton *>())
        if (b->isVisible() && b->text().contains(QLatin1String("Default Application")))
            open = b;
    check(!v->isPlayer() && hint && hint->isVisible() && hint->text().contains(QLatin1String("Media Playback")) && open,
          QStringLiteral("an audio file shows \"Open with Default Application\" and the hint where to get playback"),
          hint ? hint->text() : QString());
    shot(&w, QStringLiteral("m11-without-addon"));
    {
        OptionsDialog dlg(&w, OptionsDialog::Behaviors);
        auto *backend = dlg.findChild<QComboBox *>(QStringLiteral("mediaBackend"));
        check(backend && !backend->isEnabled(), QStringLiteral("Options > Behaviors > Playback: decoding is not available without the add-on"));
    }
    w.close();
    delete f.w;
    return failures;
}

// ==================================================================== child: GStreamer
static int runGStreamer()
{
    prefix = QStringLiteral("[GStreamer] ");
    AppSettings::instance().load();
    AppSettings::instance().mediaBackend = QStringLiteral("gstreamer");
    AppSettings::instance().save();
    Fixture f;
    if (!setUp(f, {QStringLiteral("red.mp4"), QStringLiteral("tone.mp3")}))
        return failures + 1;
    MediaView *v = f.pane0->media();
    f.pane0->showItems({f.media.value(QStringLiteral("red.mp4"))});
    media::EngineInterface *e = playback::engine();
    check(e && e->isStarted() && e->startedBackend() == QLatin1String("gstreamer") && e->description().contains(QLatin1String("GStreamer")),
          QStringLiteral("the engine runs with GStreamer when the options say so"), e ? e->description() : QString());
    check(!mapsContain(QStringLiteral("media/lib/libavcodec.so.60")),
          QStringLiteral("with GStreamer the add-on's FFmpeg is not loaded (the system's gst-libav keeps its own)"));
    v->play();
    const bool moving = waitFor([&] { return v->position() > 700; }, 6000);
    QRgb c = 0;
    waitFor([&] {
        c = centerPixel(videoWidget(v));
        return near(c, 255, 0, 0);
    });
    check(moving && v->hasVideo() && near(c, 255, 0, 0), QStringLiteral("GStreamer plays an MP4 video (H.264): position moves, the picture is red"),
          QStringLiteral("position %1, pixel %2").arg(v->position()).arg(rgbText(c)));
    v->pause(false);
    f.w->close();
    delete f.w;
    return failures;
}

// ==================================================================== child: no sound server
static int runNoSound()
{
    prefix = QStringLiteral("[no sound server] ");
    AppSettings::instance().load();
    AppSettings::instance().mediaBackend = QStringLiteral("ffmpeg");
    AppSettings::instance().save();
    Fixture f;
    if (!setUp(f, {QStringLiteral("red.mp4"), QStringLiteral("tone.mp3")}))
        return failures + 1;
    MediaView *v = f.pane0->media();
    f.pane0->showItems({f.media.value(QStringLiteral("red.mp4"))});
    v->play();
    QRgb c = 0;
    const bool red = waitFor([&] {
        c = centerPixel(videoWidget(v));
        return near(c, 255, 0, 0) && v->position() > 500;
    }, 6000);
    check(red, QStringLiteral("without a sound server a video plays its picture (no crash in Qt's FFmpeg backend)"),
          QStringLiteral("position %1, pixel %2").arg(v->position()).arg(rgbText(c)));
    f.pane0->showItems({f.media.value(QStringLiteral("tone.mp3"))});
    const bool said = waitFor([&] { return v->playerError().contains(QLatin1String("sound output")); }, 4000);
    v->play();
    settle(300);
    auto *msg = v->findChild<QLabel *>(QStringLiteral("mediaError"));
    check(said && !v->isPlaying() && msg && msg->isVisible(),
          QStringLiteral("an audio file says that no sound output was found"), v->playerError());
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
    if (!p.waitForFinished(180000)) {
        p.kill();
        check(false, QStringLiteral("child run %1 finished").arg(mode));
        return 1;
    }
    std::printf("%s", p.readAll().constData());
    std::fflush(stdout);
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : 1;
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
    theme::apply();
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

int main(int argc, char **argv)
{
    const QString mode = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    std::unique_ptr<QTemporaryDir> home;
    if (mode.isEmpty() || mode == QLatin1String("--without-addon")) {
        home = std::make_unique<QTemporaryDir>();
        setHome(home->path());
    }
    qputenv("QB_NO_DIALOGS", "1");
    QCoreApplication::setOrganizationName(QStringLiteral("Quillbinder"));
    QCoreApplication::setApplicationName(QStringLiteral("Quillbinder"));
    QApplication app(argc, argv);
    outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + QLatin1String("/shots");
    QDir().mkpath(outDir);
    initApp();
    if (mode == QLatin1String("--without-addon"))
        return runWithoutAddon();
    if (mode == QLatin1String("--gstreamer"))
        return runGStreamer();
    if (mode == QLatin1String("--no-sound"))
        return runNoSound();

    const QString buildDir = QStringLiteral(QB_TEST_BUILD_DIR);
    const QString pkg = buildDir + QStringLiteral("/addons-dist/media-playback.qbpkg");

    // ================================================================ child run without the add-on
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("HOME")); // the child makes its own
        failures += runChild(QStringLiteral("--without-addon"), env);
    }

    // ================================================================ the add-on
    {
        const addons::PackageInfo info = addons::inspect(pkg);
        QStringList log;
        QString err;
        bool restart = false;
        check(info.isValid() && info.manifest.minApp == QLatin1String("0.12.0") && QFileInfo(pkg).size() < 30LL * 1024 * 1024,
              QStringLiteral("the add-on package is valid, needs program version 0.12.0 and is below the 30 MiB delivery limit"),
              QStringLiteral("%1, %2 bytes").arg(info.error).arg(QFileInfo(pkg).size()));
        check(addons::install(pkg, &log, &err, &restart), QStringLiteral("the add-on installs"), err);
        QStringList files;
        for (const addons::FileEntry &fe : info.manifest.files)
            files << fe.path;
        check(files.contains(QStringLiteral("plugins/libqb-media.so")) && files.contains(QStringLiteral("media/libqb-media-impl.so"))
                  && files.contains(QStringLiteral("media/lib/libavcodec.so.60")) && files.contains(QStringLiteral("media/lib/libQt6Multimedia.so.6"))
                  && files.contains(QStringLiteral("media/qtplugins/multimedia/libffmpegmediaplugin.so"))
                  && files.contains(QStringLiteral("media/lib/fallback/libva.so.2")) && files.contains(QStringLiteral("media/licenses/README.txt")),
              QStringLiteral("the add-on brings its plugin, the player, Qt Multimedia, FFmpeg, the fallback libva and the licences"));
    }
    compile::PluginManager::instance().load();
    media::EngineInterface *engine = playback::engine();
    check(engine && engine->id() == QLatin1String("media-playback"), QStringLiteral("the plugin manager loads the media engine from the add-on"),
          compile::PluginManager::instance().messages().join(QLatin1String("; ")));
    if (!engine)
        return failures + 1;
    {
        const QList<media::Backend> b = engine->backends();
        check(b.size() == 2 && b[0].id == QLatin1String("ffmpeg") && b[0].available && b[1].id == QLatin1String("gstreamer"),
              QStringLiteral("two ways of decoding: the built-in FFmpeg (available) and GStreamer of the system"));
        check(!engine->isStarted() && !mapsContain(QStringLiteral("libQt6Multimedia")),
              QStringLiteral("Qt Multimedia is not loaded before the first audio or video file"));
    }

    Fixture f;
    const QStringList files = {QStringLiteral("tone.mp3"), QStringLiteral("tone.ogg"), QStringLiteral("red.mp4"), QStringLiteral("blue.webm"),
                               QStringLiteral("green.mkv")};
    if (!setUp(f, files))
        return failures + 1;
    MainWindow &w = *f.w;
    Project *proj = f.proj;
    EditorPane *pane0 = f.pane0;
    BinderItem *mp3 = f.media.value(QStringLiteral("tone.mp3"));
    BinderItem *ogg = f.media.value(QStringLiteral("tone.ogg"));
    BinderItem *mp4 = f.media.value(QStringLiteral("red.mp4"));
    BinderItem *webm = f.media.value(QStringLiteral("blue.webm"));
    BinderItem *mkv = f.media.value(QStringLiteral("green.mkv"));
    AppSettings &as = AppSettings::instance();

    // ================================================================ menus and commands
    {
        const QStringList keysWanted = {QStringLiteral("mediaPlayPause"), QStringLiteral("mediaStop"), QStringLiteral("mediaStart"),
                                        QStringLiteral("mediaBack"), QStringLiteral("mediaForward"), QStringLiteral("mediaPauseRewind"),
                                        QStringLiteral("mediaFaster"), QStringLiteral("mediaSlower"), QStringLiteral("mediaNormalSpeed"),
                                        QStringLiteral("mediaVolumeUp"), QStringLiteral("mediaVolumeDown"), QStringLiteral("mediaMute"),
                                        QStringLiteral("mediaTimestamp"), QStringLiteral("mediaGoToTimestamp")};
        QAction *menu = menuPath(&w, {QStringLiteral("Navigate"), QStringLiteral("Media")});
        QStringList missing;
        for (const QString &k : keysWanted) {
            const commands::Command *c = commands::find(k);
            bool inMenu = false;
            if (menu && menu->menu())
                for (QAction *a : menu->menu()->actions())
                    inMenu = inMenu || a->objectName() == k;
            // Insert > Media Timestamp comes first in the menu bar, so that command is listed under Insert
            const QString cat = k == QLatin1String("mediaTimestamp") ? QStringLiteral("Insert") : QStringLiteral("Navigate");
            if (!c || c->category != cat || !inMenu)
                missing << k;
        }
        check(menu && missing.isEmpty(), QStringLiteral("Navigate > Media holds the 14 media commands, which are commands of the keyboard options"),
              missing.join(QLatin1Char(',')));
        check(byKey(&w, QStringLiteral("mediaPlayPause"))->shortcut() == QKeySequence(QStringLiteral("Ctrl+Return")),
              QStringLiteral("Play / Pause has the shortcut of the tutorial: Ctrl+Return"));
        check(byKey(&w, QStringLiteral("mediaBack"))->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+,"))
                  && byKey(&w, QStringLiteral("mediaForward"))->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+."))
                  && byKey(&w, QStringLiteral("mediaTimestamp"))->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+Return")),
              QStringLiteral("Skip Back Ctrl+Alt+, / Skip Forward Ctrl+Alt+. / Insert Timestamp Ctrl+Shift+Return"));
        QAction *ins = menuPath(&w, {QStringLiteral("Insert"), QStringLiteral("Media Timestamp")});
        check(ins && qvariant_cast<QObject *>(ins->property("qbAliasOf")) == byKey(&w, QStringLiteral("mediaTimestamp")),
              QStringLiteral("Insert > Media Timestamp is the same command"));
        QHash<QString, QString> used;
        QStringList twice;
        for (QAction *a : w.findChildren<QAction *>()) {
            if (a->shortcut().isEmpty() || a->objectName().isEmpty() || a->property("qbAliasOf").isValid())
                continue;
            const QString ks = a->shortcut().toString();
            if (used.contains(ks) && used.value(ks) != a->objectName())
                twice << ks + QLatin1String(": ") + used.value(ks) + QLatin1String(" / ") + a->objectName();
            used.insert(ks, a->objectName());
        }
        check(twice.isEmpty(), QStringLiteral("no shortcut is used twice"), twice.join(QLatin1String("; ")));
        check(!byKey(&w, QStringLiteral("mediaPlayPause"))->isEnabled(), QStringLiteral("the media commands are disabled while no media file is shown"));
        check(QFile::exists(QStringLiteral(":/icons/qbicons/actions/16/media-playback-start.svg"))
                  && QFile::exists(QStringLiteral(":/icons/qbicons-dark/actions/22/chronometer-reset.svg")),
              QStringLiteral("the play icons are bundled (light and dark)"));
    }

    // ================================================================ audio in the editor
    MediaView *v = pane0->media();
    playback::MediaControls *controls = nullptr; // the footer's (the copyholder has its own)
    for (auto *mc : pane0->findChildren<playback::MediaControls *>())
        if (mc->view() == v)
            controls = mc;
    auto ctl = [&](const char *name) { return controls ? controls->findChild<QToolButton *>(QString::fromLatin1(name)) : nullptr; };
    auto *timeLabel = controls ? controls->findChild<QLabel *>(QStringLiteral("mediaTime")) : nullptr;
    auto *slider = controls ? controls->findChild<QSlider *>(QStringLiteral("mediaPosition")) : nullptr;
    pane0->showItems({mp3});
    const bool loaded = waitFor([&] { return v->isPlayable() && v->duration() > 9000; });
    check(loaded && engine->isStarted() && engine->startedBackend() == QLatin1String("ffmpeg"),
          QStringLiteral("selecting an MP3 starts the engine (FFmpeg) and loads the file into the editor's player"),
          engine->description() + QLatin1String(" / ") + v->playerError());
    check(engine->description().contains(QLatin1String("FFmpeg 6.1.1")) && mapsContain(QStringLiteral("/media/lib/libavcodec.so.60"))
              && mapsContain(QStringLiteral("/media/lib/libQt6Multimedia.so.6")),
          QStringLiteral("the add-on's own Qt Multimedia and FFmpeg 6.1.1 are the ones loaded"), engine->description());
    const bool sound = !engine->description().contains(QLatin1String("no sound output"));
    if (!sound)
        std::printf("      (no sound server: start one, e.g. pulseaudio -D --exit-idle-time=-1)\n");
    auto *title = v->findChild<QLabel *>(QStringLiteral("mediaTitle"));
    auto *info = v->findChild<QLabel *>(QStringLiteral("mediaInfo"));
    waitFor([&] { return title && title->text() == QLatin1String("Harbour Interview"); }, 3000);
    check(title && title->text() == QLatin1String("Harbour Interview") && info && info->text().contains(QLatin1String("Old Keeper"))
              && info->text().contains(QLatin1String("0:10")),
          QStringLiteral("an audio file shows its title, artist and length (from the file's tags)"),
          (title ? title->text() : QString()) + QLatin1String(" / ") + (info ? info->text() : QString()));
    check(controls && controls->isVisible() && ctl("mediaPauseRewind") && ctl("mediaPlayButton") && slider && timeLabel
              && timeLabel->text() == QLatin1String("0:00 / 0:10") && slider->maximum() > 9000,
          QStringLiteral("the footer bar shows the play controls: pause and rewind, play, position, \"0:00 / 0:10\""),
          timeLabel ? timeLabel->text() : QString());
    check(byKey(&w, QStringLiteral("mediaPlayPause"))->isEnabled(), QStringLiteral("the media commands are enabled now"));
    shot(&w, QStringLiteral("m11-audio-editor"));

    // ---------------------------------------------------------------- transcribing: text in the other editor
    byKey(&w, QStringLiteral("splitV"))->trigger();
    settle(300);
    auto *editorSplit = qobject_cast<QSplitter *>(pane0->parentWidget());
    EditorPane *pane1 = editorSplit ? qobject_cast<EditorPane *>(editorSplit->widget(1)) : nullptr;
    check(pane1 && pane1->isVisible(), QStringLiteral("split editor"));
    if (!pane1)
        return failures + 1;
    pane1->showItems({f.transcript});
    settle(300);
    TextEditor *te = pane1->textView()->editors().value(0);
    check(v->isPlayable() && te, QStringLiteral("the audio stays in the first editor, the transcript is in the second"));
    if (!te)
        return failures + 1;
    te->setFocus();
    {
        QTextCursor c = te->textCursor();
        c.movePosition(QTextCursor::End);
        te->setTextCursor(c);
    }
    settle(100);
    const QString textBefore = te->toPlainText();
    keys(&w, QKeySequence(Qt::CTRL | Qt::Key_Return));
    const bool playing = waitFor([&] { return v->isPlaying() && (!sound || v->position() > 800); }, 5000);
    check(playing && te->toPlainText() == textBefore && te->hasFocus(),
          QStringLiteral("Ctrl+Return while typing in the other editor plays the audio; the text and the keyboard focus stay"),
          QStringLiteral("playing %1, position %2").arg(v->isPlaying()).arg(v->position()));
    waitFor([&] { return controls->findChild<QLabel *>(QStringLiteral("mediaTime"))->text() != QLatin1String("0:00 / 0:10"); }, 3000);
    check(ctl("mediaPlayButton")->toolTip().startsWith(QLatin1String("Pause")) && slider->value() > 0,
          QStringLiteral("while playing: the button pauses, the slider and the time move"), timeLabel->text());
    keys(&w, QKeySequence(Qt::CTRL | Qt::Key_Return));
    settle(250);
    const qint64 pausedAt = v->position();
    settle(500);
    check(!v->isPlaying() && qAbs(v->position() - pausedAt) < 50, QStringLiteral("Ctrl+Return again pauses"),
          QStringLiteral("%1 -> %2").arg(pausedAt).arg(v->position()));

    // ---------------------------------------------------------------- pause and rewind
    {
        QAction *rw = byKey(&w, QStringLiteral("mediaPauseRewind"));
        rw->trigger();
        settle(100);
        check(as.mediaPauseRewind && rw->isChecked() && ctl("mediaPauseRewind")->isChecked(),
              QStringLiteral("Pause and Rewind (menu) switches the footer button on as well"));
        v->seek(1000);
        settle(200);
        keys(&w, QKeySequence(Qt::CTRL | Qt::Key_Return));
        waitFor([&] { return v->position() >= 4000; }, 8000);
        keys(&w, QKeySequence(Qt::CTRL | Qt::Key_Return));
        const qint64 before = pausedAt; // not used further
        Q_UNUSED(before)
        settle(300);
        const qint64 at = v->position();
        check(!v->isPlaying() && at >= 1500 && at <= 2900,
              QStringLiteral("with pause and rewind, pausing at about 0:04 goes back 2 seconds"), QString::number(at));
        ctl("mediaPauseRewind")->click();
        settle(100);
        check(!as.mediaPauseRewind && !rw->isChecked(), QStringLiteral("the footer button switches it off again (menu follows)"));
    }

    // ---------------------------------------------------------------- skip, start
    {
        v->seek(2000);
        settle(200);
        keys(&w, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Period));
        const qint64 fwd = v->position();
        keys(&w, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Comma));
        const qint64 back = v->position();
        keys(&w, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Comma));
        const qint64 floor0 = v->position();
        check(qAbs(fwd - 7000) < 300 && qAbs(back - 2000) < 300 && floor0 == 0,
              QStringLiteral("Ctrl+Alt+. skips 5 s forward, Ctrl+Alt+, 5 s back (not below 0:00)"),
              QStringLiteral("%1 / %2 / %3").arg(fwd).arg(back).arg(floor0));
        v->seek(6000);
        settle(100);
        ctl("mediaStartButton")->click();
        settle(150);
        check(v->position() < 200, QStringLiteral("the start button goes to 0:00"));
    }

    // ---------------------------------------------------------------- slider
    {
        slider->setSliderDown(true);
        slider->setSliderPosition(6000);
        settle(50);
        const bool label = timeLabel->text().startsWith(QLatin1String("0:06"));
        slider->setSliderDown(false);
        settle(300);
        check(label && qAbs(v->position() - 6000) < 300, QStringLiteral("dragging the position slider shows the time and seeks on release"),
              QStringLiteral("%1, %2").arg(timeLabel->text()).arg(v->position()));
    }

    // ---------------------------------------------------------------- timestamps
    {
        v->seek(3500);
        settle(250);
        te->setFocus();
        QTextCursor c = te->textCursor();
        c.movePosition(QTextCursor::End);
        te->setTextCursor(c);
        keys(&w, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Return));
        const QString last = te->document()->lastBlock().text();
        check(last.endsWith(QLatin1String("[00:03] ")), QStringLiteral("Ctrl+Shift+Return inserts the timestamp [00:03] at the cursor"), last);
        te->insertPlainText(QStringLiteral("He said hello."));
        // the line "[00:07] The boat came at seven."
        QTextBlock b = te->document()->begin();
        while (b.isValid() && !b.text().startsWith(QLatin1String("[00:07]")))
            b = b.next();
        QTextCursor c2(b);
        c2.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 12); // in "The boat came ...", after [00:07]
        te->setTextCursor(c2);
        byKey(&w, QStringLiteral("mediaGoToTimestamp"))->trigger();
        settle(300);
        check(b.isValid() && qAbs(v->position() - 7000) < 300, QStringLiteral("Go to Timestamp at Cursor: the line's [00:07] seeks to 0:07"),
              QString::number(v->position()) + QLatin1String(" / ") + te->toPlainText().replace(QLatin1Char('\n'), QLatin1String(" | ")));
        check(playback::parseTimestamp(QStringLiteral("at 1:02:03.5 and"), 0) == 3723500
                  && playback::parseTimestamp(QStringLiteral("[12:05] x [13:00]"), 9) == 725000
                  && playback::parseTimestamp(QStringLiteral("[12:05] x [13:00]"), 16) == 780000
                  && playback::parseTimestamp(QStringLiteral("10:75 no"), 0) == -1
                  && playback::formatTimestamp(3723000, true) == QLatin1String("[1:02:03]"),
              QStringLiteral("timestamps: h:mm:ss.f, the one before the cursor, no seconds over 59"));
    }

    // ---------------------------------------------------------------- speed and volume
    {
        auto *speed = controls->findChild<QToolButton *>(QStringLiteral("mediaSpeed"));
        QAction *r15 = nullptr;
        for (QAction *a : speed->menu()->actions())
            if (qAbs(a->data().toDouble() - 1.5) < 0.001)
                r15 = a;
        if (r15)
            emit speed->menu()->triggered(r15);
        settle(150);
        const bool menuRate = qAbs(v->rate() - 1.5) < 0.001 && speed->text() == QStringLiteral("1.5×");
        byKey(&w, QStringLiteral("mediaFaster"))->trigger();
        const double faster = v->rate();
        byKey(&w, QStringLiteral("mediaSlower"))->trigger();
        byKey(&w, QStringLiteral("mediaSlower"))->trigger();
        const double slower = v->rate();
        byKey(&w, QStringLiteral("mediaNormalSpeed"))->trigger();
        settle(100);
        check(menuRate && qAbs(faster - 2.0) < 0.001 && qAbs(slower - 1.25) < 0.001 && qAbs(v->rate() - 1.0) < 0.001,
              QStringLiteral("speed: 1.5× from the footer menu, Faster 2×, Slower twice 1.25×, Normal Speed 1×"),
              QStringLiteral("%1 %2 %3").arg(faster).arg(slower).arg(v->rate()));
        if (sound) {
            v->seek(0);
            v->setRate(2.0);
            v->play();
            // the sound output needs a moment to start: the null sink of a test machine buffers 2 s,
            // during which the position hardly moves (a desktop's sound server starts at once)
            waitFor([&] { return v->position() > 3000; }, 8000);
            const qint64 from = v->position();
            QElapsedTimer t;
            t.start();
            settle(1500);
            const double ratio = double(v->position() - from) / qMax<qint64>(1, t.elapsed());
            v->pause(false);
            v->setRate(1.0);
            check(ratio > 1.6 && ratio < 2.4, QStringLiteral("at 2× the position moves twice as fast as the clock"), QString::number(ratio, 'f', 2));
        }
        as.mediaVolume = 1.0;
        byKey(&w, QStringLiteral("mediaVolumeDown"))->trigger();
        auto *vs = controls->findChild<QSlider *>(QStringLiteral("mediaVolumeSlider"));
        const bool down = qAbs(as.mediaVolume - 0.9) < 0.001 && vs && vs->value() == 90;
        byKey(&w, QStringLiteral("mediaMute"))->trigger();
        const bool muted = as.mediaVolume == 0 && byKey(&w, QStringLiteral("mediaMute"))->isChecked();
        byKey(&w, QStringLiteral("mediaMute"))->trigger();
        check(down && muted && qAbs(as.mediaVolume - 0.9) < 0.001 && !byKey(&w, QStringLiteral("mediaMute"))->isChecked(),
              QStringLiteral("volume: Volume Down 90 % (slider follows), Mute 0, unmute back to 90 %"));
        as.mediaVolume = 1.0;
        as.save();
        playback::hub().notifySettings();
    }

    // ---------------------------------------------------------------- footer play button, position remembered
    {
        ctl("mediaPlayButton")->click();
        const bool p1 = waitFor([&] { return v->isPlaying(); }, 3000);
        v->seek(6000);
        settle(200);
        ctl("mediaPlayButton")->click();
        settle(300);
        const qint64 at = v->position();
        check(p1 && !v->isPlaying(), QStringLiteral("the footer's play button plays and pauses"));
        pane0->showItems({f.transcript});
        settle(300);
        const bool released = !v->isPlayer() && playback::hub().views().isEmpty();
        const bool stored = proj->uiState.value(QStringLiteral("mediaPositions")).toMap().contains(mp3->id);
        pane0->showItems({mp3});
        const bool restored = waitFor([&] { return v->isPlayable() && qAbs(v->position() - at) < 400; }, 4000);
        check(released && stored && restored,
              QStringLiteral("another document in the editor stops the player; the MP3 opens again where it was paused"),
              QStringLiteral("released %1 stored %2 at %3 now %4").arg(released).arg(stored).arg(at).arg(v->position()));
        // playing, then another document: the sound stops
        v->play();
        waitFor([&] { return v->isPlaying(); });
        pane0->showItems({f.transcript});
        settle(200);
        check(!v->isPlaying() && !playback::hub().target(), QStringLiteral("a file that plays stops when its editor shows another document"));
        proj->save();
        QFile ui(QDir(proj->settingsDir()).filePath(QStringLiteral("ui.json")));
        const bool written = ui.open(QIODevice::ReadOnly) && ui.readAll().contains("mediaPositions");
        check(written, QStringLiteral("the positions are saved with the project"), ui.fileName());
    }

    // ---------------------------------------------------------------- video
    {
        pane0->showItems({mp4});
        waitFor([&] { return v->isPlayable() && v->hasVideo(); }, 5000);
        QWidget *vw = videoWidget(v);
        check(vw && vw->isVisible() && v->hasVideo(), QStringLiteral("an MP4 video is shown in the editor"));
        v->play();
        QRgb c = 0;
        const bool red = waitFor([&] {
            c = centerPixel(vw);
            return near(c, 255, 0, 0);
        }, 5000);
        check(red, QStringLiteral("the picture of the video (H.264) is shown: red"), rgbText(c));
        shot(&w, QStringLiteral("m11-video-editor"));
        if (vw) {
            QTest::mouseClick(vw, Qt::LeftButton, {}, vw->rect().center());
            settle(300);
        }
        check(!v->isPlaying(), QStringLiteral("a click on the picture pauses"));
        for (BinderItem *it : {webm, mkv}) {
            pane0->showItems({it});
            const bool ok = waitFor([&] { return v->isPlayable() && v->duration() > 9000; }, 5000);
            v->seek(7000);
            settle(400);
            const qint64 at = v->position();
            v->play();
            const bool blueOrGreen = waitFor([&] {
                c = centerPixel(videoWidget(v));
                return it == webm ? near(c, 0, 0, 255) : near(c, 0, 128, 0, 70);
            }, 5000);
            v->pause(false);
            check(ok && qAbs(at - 7000) < 400 && blueOrGreen,
                  QStringLiteral("%1: length 0:10 known, seeking to 0:07 works, the picture is shown (%2)")
                      .arg(it->title + QLatin1Char('.') + it->fileExt, it == webm ? QStringLiteral("VP9, blue") : QStringLiteral("MPEG-4, green")),
                  QStringLiteral("duration %1, at %2, %3").arg(v->duration()).arg(at).arg(rgbText(c)));
        }
        pane0->showItems({ogg});
        v->play();
        const bool oggPlays = waitFor([&] { return v->isPlaying() && (!sound || v->position() > 600); }, 5000);
        v->pause(false);
        check(oggPlays, QStringLiteral("an Ogg Vorbis file plays"));
    }

    // ---------------------------------------------------------------- copyholder
    {
        pane0->showItems({f.transcript});
        settle(200);
        pane1->openCopyholder(mp3);
        settle(400);
        MediaView *hv = pane1->copyholder()->findChild<MediaView *>();
        auto *own = hv ? hv->findChild<playback::MediaControls *>(QStringLiteral("mediaControls")) : nullptr;
        const bool ready = waitFor([&] { return hv && hv->isPlayable(); });
        check(ready && own && own->isVisible() && playback::hub().target() == hv,
              QStringLiteral("an MP3 in the copyholder has play controls of its own and is the one Navigate > Media acts on"));
        te->setFocus();
        keys(&w, QKeySequence(Qt::CTRL | Qt::Key_Return));
        const bool plays = hv && waitFor([&] { return hv->isPlaying(); }, 3000);
        check(plays, QStringLiteral("Ctrl+Return plays the copyholder's audio while typing beside it"));
        shot(&w, QStringLiteral("m11-copyholder"));
        byKey(&w, QStringLiteral("closeCopyholder"))->trigger();
        settle(300);
        check(!(hv && hv->isPlaying()) && playback::hub().views().isEmpty(), QStringLiteral("closing the copyholder stops it"));
    }

    // ---------------------------------------------------------------- Quick Reference panel
    {
        f.binder->select({ogg}, false);
        f.binder->tree()->setFocus();
        settle(100);
        byKey(&w, QStringLiteral("quickRef"))->trigger();
        settle(500);
        QuickRefWindow *qr = nullptr;
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *q = qobject_cast<QuickRefWindow *>(tl))
                if (q->isVisible())
                    qr = q;
        MediaView *qv = qr ? qr->findChild<MediaView *>() : nullptr;
        const bool ready = qv && waitFor([&] { return qv->isPlayable(); });
        auto *own = qv ? qv->findChild<playback::MediaControls *>(QStringLiteral("mediaControls")) : nullptr;
        QToolButton *pb = own ? own->findChild<QToolButton *>(QStringLiteral("mediaPlayButton")) : nullptr;
        if (pb)
            pb->click();
        const bool plays = qv && waitFor([&] { return qv->isPlaying(); }, 3000);
        check(ready && own && own->isVisible() && plays, QStringLiteral("a Quick Reference panel plays an Ogg file with its own controls"));
        if (qr) {
            // the shortcut works in the panel too
            keys(qr, QKeySequence(Qt::CTRL | Qt::Key_Return));
            check(qv && !qv->isPlaying(), QStringLiteral("Ctrl+Return in the panel pauses it"));
            shot(qr, QStringLiteral("m11-quickref"));
            qr->close();
            settle(400);
        }
        check(playback::hub().views().isEmpty(), QStringLiteral("closing the panel removes its player"));
    }

    // ---------------------------------------------------------------- composition mode
    {
        pane0->showItems({mp4});
        waitFor([&] { return v->isPlayable(); });
        pane1->showItems({f.transcript});
        settle(200);
        te = pane1->textView()->editors().value(0);
        if (te)
            te->setFocus();
        settle(100);
        byKey(&w, QStringLiteral("composition"))->trigger();
        settle(600);
        CompositionWindow *comp = nullptr;
        for (QWidget *tl : QApplication::topLevelWidgets())
            if (auto *c = qobject_cast<CompositionWindow *>(tl))
                if (c->isVisible())
                    comp = c;
        if (comp) {
            keys(comp, QKeySequence(Qt::CTRL | Qt::Key_Return));
            const bool plays = waitFor([&] { return v->isPlaying(); }, 3000);
            keys(comp, QKeySequence(Qt::CTRL | Qt::Key_Return));
            check(plays && !v->isPlaying(), QStringLiteral("in composition mode Ctrl+Return plays and pauses the video in the main window"));
            comp->close();
            settle(400);
        } else {
            check(false, QStringLiteral("composition mode opens"));
        }
    }

    // ---------------------------------------------------------------- a missing file, a file that is no media
    {
        BinderItem *gone = f.proj->model()->createItem(proj->research(), -1, ItemType::Media, QStringLiteral("Lost Tape"));
        gone->fileExt = QStringLiteral("mp3");
        pane0->showItems({gone});
        const bool said = waitFor([&] { return !v->playerError().isEmpty(); }, 3000);
        check(said && v->playerError().contains(QLatin1String("missing")) && !v->isPlayable()
                  && !byKey(&w, QStringLiteral("mediaPlayPause"))->isEnabled(),
              QStringLiteral("a media document whose file is missing says so; nothing to play"), v->playerError());
        QFile junk(f.projects->path() + QStringLiteral("/notes.xyz"));
        junk.open(QIODevice::WriteOnly);
        junk.write("data");
        junk.close();
        QStringList warnings;
        const QList<BinderItem *> imp = importer::importFiles(proj, {junk.fileName()}, proj->research(), -1, &warnings);
        if (!imp.isEmpty()) {
            pane0->showItems({imp.first()});
            settle(200);
            auto *hint = v->findChild<QLabel *>(QStringLiteral("mediaHint"));
            check(!v->isPlayer() && hint && !hint->isVisible(), QStringLiteral("a file that is no audio or video is shown as before"));
        }
    }

    // ---------------------------------------------------------------- options
    {
        OptionsDialog dlg(&w, OptionsDialog::Behaviors);
        auto *rewind = dlg.findChild<QDoubleSpinBox *>(QStringLiteral("mediaRewindSeconds"));
        auto *skip = dlg.findChild<QSpinBox *>(QStringLiteral("mediaSkipSeconds"));
        auto *remember = dlg.findChild<QCheckBox *>(QStringLiteral("mediaRemember"));
        auto *backend = dlg.findChild<QComboBox *>(QStringLiteral("mediaBackend"));
        auto *group = dlg.findChild<QGroupBox *>(QStringLiteral("playbackGroup"));
        check(rewind && skip && remember && backend && backend->count() == 2 && backend->currentData().toString() == QLatin1String("ffmpeg"),
              QStringLiteral("Options > Behaviors > Playback: rewind seconds, skip seconds, continue where paused, decoding"));
        if (group) {
            dlg.show();
            settle(200);
            shot(&dlg, QStringLiteral("m11-options"));
        }
        if (rewind && skip) {
            rewind->setValue(3.5);
            skip->setValue(10);
            dlg.accept();
            settle(200);
            w.activateWindow();
            (void)QTest::qWaitForWindowActive(&w, 2000);
            pane0->showItems({mp3});
            waitFor([&] { return v->isPlayable(); });
            v->seek(1000);
            settle(150);
            keys(&w, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Period));
            check(qAbs(as.mediaRewindSeconds - 3.5) < 0.01 && as.mediaSkipSeconds == 10 && qAbs(v->position() - 10000) < 300
                      && byKey(&w, QStringLiteral("mediaBack"))->text().contains(QLatin1String("10"))
                      && ctl("mediaForwardButton")->toolTip().contains(QLatin1String("10 seconds")),
                  QStringLiteral("new values are used: skip 10 s (menu text and tooltip follow), rewind 3.5 s"),
                  QString::number(v->position()));
            as.mediaSkipSeconds = 5;
            as.mediaRewindSeconds = 2;
            as.save();
        }
    }

    // ---------------------------------------------------------------- dark theme
    {
        as.theme = QStringLiteral("dark");
        theme::apply();
        settle(300);
        shot(&w, QStringLiteral("m11-audio-dark"));
        as.theme = QStringLiteral("light");
        theme::apply();
    }
    // for a run of the AppImage afterwards: the MP3 in the left editor, the transcript in the right one
    pane0->showItems({mp3});
    pane1->showItems({f.transcript});
    settle(300);
    const QString folder = proj->folderPath();
    w.close(); // saves the project with the state of the editors
    settle(200);
    const QString keep = qEnvironmentVariable("QB_TEST_KEEP");
    if (!keep.isEmpty()) {
        QProcess::execute(QStringLiteral("cp"), {QStringLiteral("-r"), folder, keep});
        std::printf("      (project copied to %s)\n", qPrintable(keep));
    }
    delete f.w;
    check(playback::hub().views().isEmpty(), QStringLiteral("no player is left when the window is gone"));

    // ================================================================ child runs with the add-on
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment(); // the same home: the add-on is installed
        failures += runChild(QStringLiteral("--gstreamer"), env);
        env.insert(QStringLiteral("PULSE_SERVER"), QStringLiteral("unix:/nonexistent/native"));
        failures += runChild(QStringLiteral("--no-sound"), env);
    }
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
