// The engine of the add-on "Media Playback" on Windows (port, W2, 2026-11-07); Linux: MediaEngine.cpp.
//
// Layout of the installed add-on:
//   plugins/qb-media.dll                       this library (QtCore only)
//   media/qb-media-impl.dll                    stage 2 (links Qt6Multimedia)
//   media/Qt6Multimedia.dll                    Qt Multimedia of the same Qt as the program
//   media/qtplugins/multimedia/*mediaplugin.dll  Qt's backends that came with the add-on
//   media/lib/*.dll                            FFmpeg of Qt's FFmpeg backend, when the add-on brings it
// Backends: "windows" plays with the decoders of Windows (Media Foundation: MP3, AAC, H.264,
// WMA, WAV, FLAC ... whatever Windows has), so the add-on itself carries no decoders; "ffmpeg"
// is Qt's FFmpeg backend, which the add-on "Extra Video and Audio Formats" brings (W3):
//   <addons>/media-extra-formats/media-extra/qtplugins/multimedia/ffmpegmediaplugin.dll
//   <addons>/media-extra-formats/media-extra/lib/av*.dll, sw*.dll   (FFmpeg as built by The Qt Company)
// While that add-on is installed and switched on, "ffmpeg" is the default (it plays more formats).
// QB_MEDIA_DIR overrides the media/ folder, QB_MEDIA_EXTRA_DIR the media-extra/ folder (empty: none).
#include "MediaEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <windows.h>

namespace qbmedia {

namespace {

using CreateFn = qb::media::EngineInterface *(*)();

QString selfPath()
{
    HMODULE self = nullptr;
    if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(&selfPath), &self))
        return QString();
    wchar_t buf[4096];
    const DWORD n = ::GetModuleFileNameW(self, buf, DWORD(sizeof buf / sizeof buf[0]));
    return n ? QFileInfo(QDir::fromNativeSeparators(QString::fromWCharArray(buf, int(n)))).absoluteFilePath() : QString();
}

QString lastError()
{
    const DWORD code = ::GetLastError();
    wchar_t *msg = nullptr;
    ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                     code, 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    const QString text = msg ? QString::fromWCharArray(msg).trimmed() : QString();
    if (msg)
        ::LocalFree(msg);
    return QStringLiteral("%1 (%2)").arg(text).arg(code);
}

// Loads a library by its full path; the libraries it needs are looked for beside it first.
HMODULE loadLibrary(const QString &path)
{
    return ::LoadLibraryExW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()), nullptr,
                            LOAD_WITH_ALTERED_SEARCH_PATH);
}

// FFmpeg in the order its libraries need each other (the file names carry their versions)
const char *const kFFmpeg[] = {"avutil", "swresample", "swscale", "avcodec", "avformat"};

QString backendFile(const QString &mediaDir, const QString &backend)
{
    return mediaDir + QStringLiteral("/qtplugins/multimedia/%1mediaplugin.dll").arg(backend);
}

} // namespace

static bool hasQtFFmpeg(const QString &dir)
{
    return !dir.isEmpty() && QFileInfo::exists(backendFile(dir, QStringLiteral("ffmpeg")))
           && !QDir(dir + QLatin1String("/lib")).entryList({QStringLiteral("avcodec*.dll")}, QDir::Files).isEmpty();
}

QString MediaEngine::extraDir() const
{
    if (qEnvironmentVariableIsSet("QB_MEDIA_EXTRA_DIR")) {
        const QString env = qEnvironmentVariable("QB_MEDIA_EXTRA_DIR");
        return hasQtFFmpeg(env) ? QDir::cleanPath(env) : QString();
    }
    // <addons>/media-playback/plugins/qb-media.dll -> <addons>/media-extra-formats/media-extra; an add-on
    // switched off in the Add-on Manager is listed in <addons>/state.json under "disabled"
    const QString root = QDir::cleanPath(QFileInfo(selfPath()).absolutePath() + QLatin1String("/../.."));
    const QString dir = root + QLatin1String("/media-extra-formats/media-extra");
    if (!hasQtFFmpeg(dir))
        return QString();
    QFile st(root + QLatin1String("/state.json"));
    if (st.open(QIODevice::ReadOnly)) {
        const QJsonArray off = QJsonDocument::fromJson(st.readAll()).object().value(QStringLiteral("disabled")).toArray();
        for (const QJsonValue &v : off)
            if (v.toString() == QLatin1String("media-extra-formats"))
                return QString();
    }
    return dir;
}

// FFmpeg of the extra add-on, else beside the media folder (a build folder)
QString MediaEngine::ffmpegLibDir() const
{
    const QString extra = extraDir();
    return (extra.isEmpty() ? mediaDir() : extra) + QLatin1String("/lib/");
}

// the folder whose qtplugins/multimedia holds Qt's FFmpeg backend, empty when there is none
static QString ffmpegBackendDir(const MediaEngine &e)
{
    const QString extra = e.extraDir();
    if (!extra.isEmpty())
        return extra;
    return hasQtFFmpeg(e.mediaDir()) ? e.mediaDir() : QString();
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
        if (QFileInfo::exists(d + QLatin1String("/qb-media-impl.dll")))
            return d;
    }
    return QDir::cleanPath(self.absolutePath() + QLatin1String("/../media"));
}

QList<qb::media::Backend> MediaEngine::backends() const
{
    const QString dir = mediaDir();
    QList<qb::media::Backend> out;

    qb::media::Backend win;
    win.id = QStringLiteral("windows");
    win.name = tr("The codecs of Windows (Media Foundation)");
    win.available = QFileInfo::exists(backendFile(dir, win.id));
    win.note = win.available ? tr("Plays what Windows plays (MP3, AAC, H.264, WMA, WAV, FLAC ...); more formats come with "
                                  "codec extensions from the Microsoft Store.")
                             : tr("Qt's Windows backend is missing in the add-on (%1).").arg(QDir::toNativeSeparators(backendFile(dir, win.id)));
    out << win;

    qb::media::Backend ff;
    ff.id = QStringLiteral("ffmpeg");
    ff.name = tr("FFmpeg (add-on \"Extra Video and Audio Formats\")");
    ff.available = !ffmpegBackendDir(*this).isEmpty();
    ff.note = ff.available ? tr("Plays more formats (also Ogg, Opus, WebM, Matroska ...) with the FFmpeg libraries "
                                "built by The Qt Company for Qt Multimedia.")
                           : tr("Install the add-on \"Extra Video and Audio Formats\" (and switch it on) to use it.");
    // the extra formats first: they are the default while the add-on is there
    if (ff.available)
        out.prepend(ff);
    else
        out << ff;
    return out;
}

bool MediaEngine::preload(const QString &backend, QString *error)
{
    const QString dir = mediaDir();
    // Qt Multimedia of the add-on (the program's Qt has none); when Qt's backend asks for a
    // library by name later, Windows takes the one already loaded
    QStringList paths;
    if (backend == QLatin1String("ffmpeg")) {
        const QFileInfoList libs = QDir(ffmpegLibDir()).entryInfoList({QStringLiteral("*.dll")}, QDir::Files, QDir::Name);
        for (const char *prefix : kFFmpeg)
            for (const QFileInfo &fi : libs)
                if (fi.fileName().startsWith(QLatin1String(prefix), Qt::CaseInsensitive)) {
                    paths << fi.absoluteFilePath();
                    break;
                }
    }
    paths << dir + QLatin1String("/Qt6Multimedia.dll");
    for (const QString &path : paths) {
        if (!QFileInfo::exists(path))
            continue; // build folder: Qt's own copy is used
        if (!loadLibrary(path)) {
            if (error)
                *error = tr("Cannot load %1: %2").arg(QFileInfo(path).fileName(), lastError());
            return false;
        }
    }
    return true;
}

bool MediaEngine::start(const QString &backend, QString *error)
{
    if (m_impl)
        return true;
    const QList<qb::media::Backend> list = backends();
    // the saved choice may name a Linux backend ("ffmpeg" by default, "gstreamer"): then the first
    // one that is available here
    QString chosen;
    for (const qb::media::Backend &b : list)
        if (b.id == backend && b.available)
            chosen = b.id;
    if (chosen.isEmpty())
        for (const qb::media::Backend &b : list)
            if (b.available) {
                chosen = b.id;
                break;
            }
    if (chosen.isEmpty()) {
        m_error = list.isEmpty() ? tr("No way of playing is available.") : tr("%1 cannot be used: %2").arg(list.first().name, list.first().note);
    } else if (preload(chosen, &m_error)) {
        const QString dir = mediaDir();
        // Qt Multimedia takes the backend named here when it starts (once per process)
        qputenv("QT_MEDIA_BACKEND", chosen.toLatin1());
        if (QFileInfo::exists(dir + QLatin1String("/qtplugins/multimedia")))
            QCoreApplication::addLibraryPath(dir + QLatin1String("/qtplugins"));
        const QString ffdir = ffmpegBackendDir(*this);
        if (chosen == QLatin1String("ffmpeg") && !ffdir.isEmpty() && ffdir != dir)
            QCoreApplication::addLibraryPath(ffdir + QLatin1String("/qtplugins"));
        const HMODULE h = loadLibrary(dir + QLatin1String("/qb-media-impl.dll"));
        if (!h) {
            m_error = tr("Cannot load the player: %1").arg(lastError());
        } else if (auto create = reinterpret_cast<CreateFn>(::GetProcAddress(h, "qb_media_impl_create"))) {
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
