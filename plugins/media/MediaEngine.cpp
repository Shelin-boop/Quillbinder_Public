#include "MediaEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <dlfcn.h>

namespace qbmedia {

namespace {

using CreateFn = qb::media::EngineInterface *(*)();

QString selfPath()
{
    Dl_info info;
    if (dladdr(reinterpret_cast<void *>(&selfPath), &info) && info.dli_fname)
        return QFileInfo(QString::fromLocal8Bit(info.dli_fname)).absoluteFilePath();
    return QString();
}

// Can the system's dynamic linker find this library (by its soname)?
bool systemHas(const char *soname)
{
    static QHash<QByteArray, bool> cache;
    const QByteArray key(soname);
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return *it;
    void *h = dlopen(soname, RTLD_LAZY | RTLD_LOCAL);
    if (h)
        dlclose(h);
    cache.insert(key, h != nullptr);
    return h != nullptr;
}

// FFmpeg in the order its libraries need each other. Loaded only for Qt's
// FFmpeg backend: with GStreamer they would stand in for the system's FFmpeg
// of the same soname, which the system's gst-libav needs (Ubuntu 24.04).
const char *const kFFmpeg[] = {"libavutil.so.58", "libswresample.so.4", "libswscale.so.7", "libavcodec.so.60",
                               "libavformat.so.60"};
// Taken from the add-on only if the system has none (client libraries that
// talk to services of the system; the system's own fit them best).
const char *const kFallback[] = {"libva.so.2"};

const char *const kGstLibs[] = {"libgstreamer-1.0.so.0", "libgstbase-1.0.so.0", "libgstapp-1.0.so.0",
                                "libgstvideo-1.0.so.0", "libgstpbutils-1.0.so.0", "libgstallocators-1.0.so.0",
                                "libgstgl-1.0.so.0"};

QStringList missingSystemLibs(bool gstreamer)
{
    QStringList out;
    for (const char *l : {"libEGL.so.1", "libOpenGL.so.0", "libpulse.so.0"})
        if (!systemHas(l))
            out << QString::fromLatin1(l);
    if (gstreamer)
        for (const char *l : kGstLibs)
            if (!systemHas(l))
                out << QString::fromLatin1(l);
    return out;
}

// The add-on that brings the full FFmpeg (0.50): installed beside this one.
const char kExtraId[] = "media-extra-formats";

bool hasFFmpeg(const QString &dir)
{
    return !dir.isEmpty() && QFileInfo::exists(dir + QLatin1String("/lib/libavcodec.so.60"));
}

} // namespace

QString MediaEngine::extraDir() const
{
    // QB_MEDIA_EXTRA_DIR: the media-extra/ folder of the add-on (developers, checks);
    // set but empty: no extra formats
    if (qEnvironmentVariableIsSet("QB_MEDIA_EXTRA_DIR")) {
        const QString env = qEnvironmentVariable("QB_MEDIA_EXTRA_DIR");
        return hasFFmpeg(env) ? QDir::cleanPath(env) : QString();
    }
    // installed add-ons: <addons>/media-playback/plugins/libqb-media.so and
    // <addons>/media-extra-formats/media-extra/; an add-on switched off in the
    // Add-on Manager is listed in <addons>/state.json under "disabled"
    const QFileInfo self(selfPath());
    const QString root = QDir::cleanPath(self.absolutePath() + QLatin1String("/../.."));
    const QString dir = root + QLatin1Char('/') + QLatin1String(kExtraId) + QLatin1String("/media-extra");
    if (!hasFFmpeg(dir))
        return QString();
    QFile st(root + QLatin1String("/state.json"));
    if (st.open(QIODevice::ReadOnly)) {
        const QJsonArray off = QJsonDocument::fromJson(st.readAll()).object().value(QStringLiteral("disabled")).toArray();
        for (const QJsonValue &v : off)
            if (v.toString() == QLatin1String(kExtraId))
                return QString();
    }
    return dir;
}

QString MediaEngine::ffmpegLibDir() const
{
    const QString extra = extraDir();
    return extra.isEmpty() ? mediaDir() + QLatin1String("/lib/") : extra + QLatin1String("/lib/");
}

QString MediaEngine::mediaDir() const
{
    const QString env = qEnvironmentVariable("QB_MEDIA_DIR");
    if (!env.isEmpty())
        return QDir::cleanPath(env);
    const QFileInfo self(selfPath());
    // installed add-on: plugins/../media; build folder: tool-plugins/media
    for (const QString &rel : {QStringLiteral("/../media"), QStringLiteral("/media")}) {
        const QString d = QDir::cleanPath(self.absolutePath() + rel);
        if (QFileInfo::exists(d + QLatin1String("/libqb-media-impl.so")))
            return d;
    }
    return QDir::cleanPath(self.absolutePath() + QLatin1String("/../media"));
}

QList<qb::media::Backend> MediaEngine::backends() const
{
    const QString lib = ffmpegLibDir();
    const bool extra = !extraDir().isEmpty();
    QList<qb::media::Backend> out;

    qb::media::Backend ff;
    ff.id = QStringLiteral("ffmpeg");
    ff.name = tr("Built in (FFmpeg 6.1, comes with the add-on)");
    const bool ffLibs = QFileInfo::exists(lib + QLatin1String("libavcodec.so.60")) || systemHas("libavcodec.so.60");
    QStringList missing = missingSystemLibs(false);
    if (!ffLibs)
        missing << QStringLiteral("libavcodec.so.60");
    ff.available = missing.isEmpty();
    ff.note = !ff.available
                  ? tr("Missing on this system: %1").arg(missing.join(QStringLiteral(", ")))
                  : extra ? tr("Plays common audio and video files without other programs, also H.264, H.265 and AAC "
                               "(add-on \"Extra Video and Audio Formats\").")
                          : tr("Plays free formats without other programs (MP3, Ogg, Opus, FLAC, WAV, WebM ...). H.264, "
                               "H.265, AAC and other formats licensed under patents need the add-on \"Extra Video and "
                               "Audio Formats\".");
    out << ff;

    qb::media::Backend gst;
    gst.id = QStringLiteral("gstreamer");
    gst.name = tr("GStreamer of the system");
    missing = missingSystemLibs(true);
    gst.available = missing.isEmpty();
    gst.note = gst.available
                   ? tr("Uses the codecs installed on the system (packages gstreamer1.0-plugins-good, gstreamer1.0-libav ...).")
                   : tr("Missing on this system: %1 (packages libgstreamer1.0-0, libgstreamer-plugins-base1.0-0, "
                        "libgstreamer-gl1.0-0)").arg(missing.join(QStringLiteral(", ")));
    out << gst;
    return out;
}

bool MediaEngine::preload(const QString &backend, QString *error)
{
    const QString lib = mediaDir() + QLatin1String("/lib/");
    // FFmpeg: the full set of the add-on "Extra Video and Audio Formats" when it is
    // installed and switched on, else this add-on's own set (free formats only)
    const QString ffLib = ffmpegLibDir();
    qputenv("QB_MEDIA_FFMPEG_SET", extraDir().isEmpty() ? "base" : "extra");
    // The add-on's own copies are loaded by their full path; when Qt's backend
    // asks for "libavcodec.so.60" later, the dynamic linker takes the library
    // of that name that is already loaded (also on a system that has another
    // FFmpeg or none). Qt Multimedia must be the add-on's (Qt 6.4.2, as the
    // AppImage), whatever Qt the system has.
    QList<const char *> names;
    if (backend == QLatin1String("ffmpeg"))
        for (const char *n : kFFmpeg)
            names << n;
    names << "libQt6Multimedia.so.6";
    for (const char *name : names) {
        const QString path = (qstrncmp(name, "libQt6", 6) == 0 ? lib : ffLib) + QString::fromLatin1(name);
        if (!QFileInfo::exists(path))
            continue; // build folder: the system's libraries are used
        if (!dlopen(QFile::encodeName(path).constData(), RTLD_NOW | RTLD_GLOBAL)) {
            if (error)
                *error = tr("Cannot load %1: %2").arg(QString::fromLatin1(name), QString::fromLocal8Bit(dlerror()));
            return false;
        }
    }
    for (const char *name : kFallback) {
        if (systemHas(name))
            continue;
        const QString path = lib + QLatin1String("fallback/") + QString::fromLatin1(name);
        if (QFileInfo::exists(path))
            dlopen(QFile::encodeName(path).constData(), RTLD_NOW | RTLD_GLOBAL);
    }
    return true;
}

bool MediaEngine::start(const QString &backend, QString *error)
{
    if (m_impl)
        return true;
    QString chosen = backend;
    const QList<qb::media::Backend> list = backends();
    if (chosen.isEmpty()) {
        for (const qb::media::Backend &b : list)
            if (b.available) {
                chosen = b.id;
                break;
            }
    }
    const qb::media::Backend *b = nullptr;
    for (const qb::media::Backend &x : list)
        if (x.id == chosen)
            b = &x;
    if (!b) {
        m_error = tr("Unknown way of playing \"%1\".").arg(chosen);
    } else if (!b->available) {
        m_error = tr("%1 cannot be used: %2").arg(b->name, b->note);
    } else if (preload(chosen, &m_error)) {
        const QString dir = mediaDir();
        // Qt Multimedia takes the backend named here when it starts (once per process)
        qputenv("QT_MEDIA_BACKEND", chosen.toLatin1());
        if (QFileInfo::exists(dir + QLatin1String("/qtplugins/multimedia")))
            QCoreApplication::addLibraryPath(dir + QLatin1String("/qtplugins"));
        const QByteArray impl = QFile::encodeName(dir + QLatin1String("/libqb-media-impl.so"));
        void *h = dlopen(impl.constData(), RTLD_NOW | RTLD_LOCAL);
        if (!h) {
            m_error = tr("Cannot load the player: %1").arg(QString::fromLocal8Bit(dlerror()));
        } else if (auto create = reinterpret_cast<CreateFn>(dlsym(h, "qb_media_impl_create"))) {
            qb::media::EngineInterface *e = create();
            if (e && e->start(chosen, &m_error)) {
                m_impl = e;
                m_backend = chosen;
                m_error.clear();
            } else {
                delete e;
            }
        } else {
            m_error = tr("The player library is damaged (no entry point).");
        }
    }
    if (error)
        *error = m_error;
    return m_impl != nullptr;
}

QString MediaEngine::description() const
{
    if (m_impl)
        return m_impl->description();
    return m_error.isEmpty() ? tr("Not started yet (starts with the first audio or video file).") : m_error;
}

QStringList MediaEngine::fileSuffixes() const
{
    return {QStringLiteral("mp3"),  QStringLiteral("wav"),  QStringLiteral("ogg"),  QStringLiteral("oga"),
            QStringLiteral("opus"), QStringLiteral("flac"), QStringLiteral("m4a"),  QStringLiteral("aac"),
            QStringLiteral("wma"),  QStringLiteral("aif"),  QStringLiteral("aiff"), QStringLiteral("amr"),
            QStringLiteral("mka"),  QStringLiteral("ac3"),  QStringLiteral("mp4"),  QStringLiteral("m4v"),
            QStringLiteral("mov"),  QStringLiteral("mkv"),  QStringLiteral("webm"), QStringLiteral("avi"),
            QStringLiteral("mpg"),  QStringLiteral("mpeg"), QStringLiteral("ts"),   QStringLiteral("mts"),
            QStringLiteral("m2ts"), QStringLiteral("wmv"),  QStringLiteral("flv"),  QStringLiteral("3gp"),
            QStringLiteral("ogv")};
}

qb::media::Player *MediaEngine::createPlayer()
{
    return m_impl ? m_impl->createPlayer() : nullptr;
}

} // namespace qbmedia
