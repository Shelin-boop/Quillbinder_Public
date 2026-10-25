// Stage 2 of the add-on "Media Playback": players built on Qt Multimedia.
// Loaded by MediaEngine::start() (see MediaEngine.h) after the add-on's
// libraries; its only entry point is qb_media_impl_create().

#include "media/MediaApi.h"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QMediaDevices>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QPainter>
#include <QPluginLoader>
#include <QPointer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include <QWidget>

#include <dlfcn.h>

namespace qbmedia {

namespace {

// The picture of a video, painted with QPainter: works with every platform
// plugin (also without OpenGL) and inside every layout. QVideoWidget of
// Qt 6.4 is a native window inside the widget, which does not mix well with
// splitters and the copyholder.
class VideoView : public QWidget
{
public:
    VideoView()
    {
        setAttribute(Qt::WA_OpaquePaintEvent);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumSize(64, 48);
        setObjectName(QStringLiteral("qbVideoView"));
        m_sink = new QVideoSink(this);
        QObject::connect(m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &f) {
            m_frame = f;
            update();
        });
    }
    QVideoSink *sink() const { return m_sink; }
    void clear()
    {
        m_frame = QVideoFrame();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        if (!m_frame.isValid())
            return;
        QVideoFrame f(m_frame);
        QVideoFrame::PaintOptions o;
        o.backgroundColor = Qt::black;
        o.aspectRatioMode = Qt::KeepAspectRatio;
        f.paint(&p, QRectF(rect()), o);
    }

private:
    QVideoSink *m_sink;
    QVideoFrame m_frame;
};

qb::media::State toState(QMediaPlayer::PlaybackState s)
{
    switch (s) {
    case QMediaPlayer::PlayingState: return qb::media::State::Playing;
    case QMediaPlayer::PausedState: return qb::media::State::Paused;
    default: return qb::media::State::Stopped;
    }
}

class QtPlayer : public QObject, public qb::media::Player
{
public:
    QtPlayer()
    {
        m_player = new QMediaPlayer(this);
        m_audio = new QAudioOutput(this);
        attachAudio();
        m_video = new VideoView;
        m_player->setVideoSink(m_video->sink());

        connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this] { notifyState(); });
        connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
            if (s == QMediaPlayer::NoMedia)
                m_video->clear();
            notifyState();
        });
        connect(m_player, &QMediaPlayer::hasVideoChanged, this, [this] { notifyState(); });
        connect(m_player, &QMediaPlayer::hasAudioChanged, this, [this] { notifyState(); });
        connect(m_player, &QMediaPlayer::seekableChanged, this, [this] { notifyState(); });
        connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &msg) {
            m_error = msg.isEmpty() ? QCoreApplication::translate("qbmedia", "The file cannot be played.") : msg;
            notifyState();
        });
        connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
            if (m_listener)
                m_listener->playerPositionChanged(ms);
        });
        connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 ms) {
            if (m_listener)
                m_listener->playerDurationChanged(ms);
        });
        connect(m_player, &QMediaPlayer::metaDataChanged, this, [this] {
            if (m_listener)
                m_listener->playerMetaDataChanged();
        });
        // headphones plugged in, another default output chosen in the desktop,
        // the sound server started (again)
        connect(&m_devices, &QMediaDevices::audioOutputsChanged, this, [this] {
            attachAudio();
            notifyState();
        });
    }

    // Qt 6.4's FFmpeg backend crashes when it plays sound without an output
    // device (no sound server running): then the player gets no audio output
    // at all and plays the picture only.
    void attachAudio()
    {
        const QAudioDevice def = QMediaDevices::defaultAudioOutput();
        if (def.isNull()) {
            if (m_player->audioOutput())
                m_player->setAudioOutput(nullptr);
            m_noSound = true;
            return;
        }
        if (m_audio->device() != def)
            m_audio->setDevice(def);
        if (!m_player->audioOutput())
            m_player->setAudioOutput(m_audio);
        m_noSound = false;
    }

    ~QtPlayer() override
    {
        m_listener = nullptr;
        m_player->stop();
        m_player->setVideoSink(nullptr);
        delete m_video.data(); // owned by the player even while the program shows it
    }

    void setListener(qb::media::PlayerListener *l) override { m_listener = l; }

    void setSource(const QString &file) override
    {
        m_error.clear();
        m_file = file;
        m_video->clear();
        // Qt 6.4's FFmpeg backend does not hand the player's speed to the
        // decoder of a new file: every file starts at normal speed
        m_player->setPlaybackRate(1.0);
        m_player->setSource(file.isEmpty() ? QUrl() : QUrl::fromLocalFile(file));
        if (!file.isEmpty() && !QFileInfo::exists(file))
            m_error = QCoreApplication::translate("qbmedia", "The file is missing.");
        notifyState();
    }
    QString source() const override { return m_file; }
    QWidget *videoWidget() override { return m_video; }

    void play() override
    {
        m_error.clear();
        if (m_noSound && !m_player->hasVideo())
            return; // nothing to hear or see
        if (m_player->mediaStatus() == QMediaPlayer::EndOfMedia)
            m_player->setPosition(0);
        m_player->play();
    }
    void pause() override { m_player->pause(); }
    void stop() override { m_player->stop(); }
    qb::media::State state() const override { return toState(m_player->playbackState()); }

    qb::media::Status status() const override
    {
        if (!m_error.isEmpty())
            return qb::media::Status::Invalid;
        switch (m_player->mediaStatus()) {
        case QMediaPlayer::NoMedia: return qb::media::Status::NoMedia;
        case QMediaPlayer::LoadingMedia: return qb::media::Status::Loading;
        case QMediaPlayer::StalledMedia:
        case QMediaPlayer::BufferingMedia: return qb::media::Status::Buffering;
        case QMediaPlayer::EndOfMedia: return qb::media::Status::EndOfMedia;
        case QMediaPlayer::InvalidMedia: return qb::media::Status::Invalid;
        default: return qb::media::Status::Loaded;
        }
    }
    QString errorString() const override
    {
        if (!m_error.isEmpty())
            return m_error;
        if (m_noSound && !m_file.isEmpty() && !m_player->hasVideo()
            && m_player->mediaStatus() != QMediaPlayer::LoadingMedia && m_player->mediaStatus() != QMediaPlayer::NoMedia)
            return QCoreApplication::translate("qbmedia", "No sound output was found (is PipeWire or PulseAudio running?).");
        if (m_player->mediaStatus() == QMediaPlayer::InvalidMedia)
            return m_player->errorString().isEmpty()
                       ? QCoreApplication::translate("qbmedia", "The file cannot be played.")
                       : m_player->errorString();
        return QString();
    }

    qint64 position() const override { return m_player->position(); }
    void setPosition(qint64 ms) override
    {
        const qint64 d = m_player->duration();
        m_player->setPosition(qBound<qint64>(0, ms, d > 0 ? d : ms));
    }
    qint64 duration() const override { return m_player->duration(); }
    bool isSeekable() const override { return m_player->isSeekable(); }

    double rate() const override { return m_player->playbackRate(); }
    void setRate(double r) override { m_player->setPlaybackRate(qBound(0.25, r, 4.0)); }
    double volume() const override { return m_audio->volume(); }
    void setVolume(double v) override { m_audio->setVolume(float(qBound(0.0, v, 1.0))); }
    bool isMuted() const override { return m_audio->isMuted(); }
    void setMuted(bool m) override { m_audio->setMuted(m); }

    bool hasVideo() const override { return m_player->hasVideo(); }
    bool hasAudio() const override { return m_player->hasAudio(); }
    QSize videoSize() const override
    {
        const QVariant r = m_player->metaData().value(QMediaMetaData::Resolution);
        return r.toSize();
    }
    QVariantMap metaData() const override
    {
        const QMediaMetaData md = m_player->metaData();
        QVariantMap out;
        auto put = [&](const char *key, QMediaMetaData::Key k) {
            const QString v = md.stringValue(k).trimmed();
            if (!v.isEmpty())
                out.insert(QString::fromLatin1(key), v);
        };
        put("title", QMediaMetaData::Title);
        put("artist", QMediaMetaData::ContributingArtist);
        if (!out.contains(QStringLiteral("artist")))
            put("artist", QMediaMetaData::AlbumArtist);
        put("album", QMediaMetaData::AlbumTitle);
        put("date", QMediaMetaData::Date);
        put("comment", QMediaMetaData::Comment);
        // codec names are left out: Qt 6.4's GStreamer backend reports some wrongly (VP9 as MPEG-1)
        return out;
    }

private:
    void notifyState()
    {
        if (m_listener)
            m_listener->playerStateChanged();
    }


    QMediaPlayer *m_player;
    QAudioOutput *m_audio;
    QPointer<VideoView> m_video;
    QMediaDevices m_devices;
    qb::media::PlayerListener *m_listener = nullptr;
    QString m_file;
    QString m_error;
    bool m_noSound = false;
};

QString ffmpegVersion()
{
    using Fn = const char *(*)();
    void *sym = dlsym(RTLD_DEFAULT, "av_version_info");
    if (!sym) {
        if (void *h = dlopen("libavutil.so.58", RTLD_LAZY | RTLD_NOLOAD))
            sym = dlsym(h, "av_version_info");
    }
    return sym ? QString::fromLatin1(reinterpret_cast<Fn>(sym)()) : QString();
}

class QtEngine : public qb::media::EngineInterface
{
public:
    QString id() const override { return QStringLiteral("media-playback-impl"); }
    QString name() const override { return QStringLiteral("Qt Multimedia"); }
    QString version() const override { return QStringLiteral("1.0"); }
    QList<qb::media::Backend> backends() const override { return {}; }

    bool start(const QString &backend, QString *error) override
    {
        // Load Qt's backend now, to report a missing library here and not as
        // a player that stays silent.
        const QString file = QStringLiteral("lib%1mediaplugin.so").arg(backend);
        QString found;
        for (const QString &dir : QCoreApplication::libraryPaths()) {
            const QString p = dir + QLatin1String("/multimedia/") + file;
            if (QFileInfo::exists(p)) {
                found = p;
                break;
            }
        }
        if (found.isEmpty()) {
            if (error)
                *error = QCoreApplication::translate("qbmedia", "Qt's %1 backend is missing (%2).").arg(backend, file);
            return false;
        }
        QPluginLoader loader(found);
        if (!loader.load()) {
            if (error)
                *error = loader.errorString();
            return false;
        }
        m_backend = backend;
        m_backendFile = found;
        return true;
    }
    bool isStarted() const override { return !m_backend.isEmpty(); }
    QString startedBackend() const override { return m_backend; }

    QString description() const override
    {
        QString s = QStringLiteral("Qt Multimedia %1").arg(QString::fromLatin1(qVersion()));
        if (m_backend == QLatin1String("ffmpeg")) {
            const QString v = ffmpegVersion();
            s += QCoreApplication::translate("qbmedia", " with FFmpeg %1").arg(v.isEmpty() ? QStringLiteral("6.1") : v);
        } else if (m_backend == QLatin1String("gstreamer")) {
            s += QCoreApplication::translate("qbmedia", " with GStreamer of the system");
        }
        if (QMediaDevices::defaultAudioOutput().isNull())
            s += QCoreApplication::translate("qbmedia", "; no sound output found");
        return s;
    }
    QStringList fileSuffixes() const override { return {}; }
    qb::media::Player *createPlayer() override { return new QtPlayer; }

private:
    QString m_backend, m_backendFile;
};

} // namespace
} // namespace qbmedia

extern "C" __attribute__((visibility("default"))) qb::media::EngineInterface *qb_media_impl_create()
{
    return new qbmedia::QtEngine;
}
