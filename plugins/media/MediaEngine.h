#pragma once
// The engine of the add-on "Media Playback" (stage 1).
//
// This library is what the program loads at start: it needs nothing but
// QtCore. It does not touch Qt Multimedia until the first file is played;
// then start() makes the libraries of the add-on available and loads the
// part that plays (stage 2, media/libqb-media-impl.so, built against
// Qt Multimedia). Two stages, because
//  - Qt Multimedia and FFmpeg are only loaded when someone plays something,
//  - a library the system lacks gives a message, not a plugin that fails to
//    load without a word,
//  - the add-on brings its own FFmpeg (and, as a fallback, PulseAudio's and
//    VA-API's client libraries) and has to load them before Qt Multimedia.
//
// Layout of the installed add-on:
//   plugins/libqb-media.so                this library
//   media/libqb-media-impl.so             stage 2 (links Qt6Multimedia)
//   media/lib/                            libQt6Multimedia.so.6, FFmpeg 6.1 (avutil, swresample, swscale, avcodec, avformat)
//   media/lib/fallback/                   used only if the system has none: libpulse, libva
//   media/qtplugins/multimedia/           Qt's two backends (ffmpeg, gstreamer)
// QB_MEDIA_DIR overrides the media/ folder (developers).
//
// Since 1.1 (program 0.50) this add-on's FFmpeg decodes free formats only. The add-on
// "Extra Video and Audio Formats" (id media-extra-formats) brings a full FFmpeg of the same
// version in <addons>/media-extra-formats/media-extra/lib/; when it is installed and switched
// on, its libraries are loaded instead (all five, so they always fit together).
// QB_MEDIA_EXTRA_DIR overrides that folder (empty: none).

#include "media/MediaApi.h"

#include <QObject>

namespace qbmedia {

class MediaEngine : public QObject, public qb::media::EngineInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QB_MEDIA_IID)
    Q_INTERFACES(qb::media::EngineInterface)
public:
    QString id() const override { return QStringLiteral("media-playback"); }
    QString name() const override { return tr("Media Playback"); }
    QString version() const override { return QStringLiteral("1.1"); }
    QList<qb::media::Backend> backends() const override;
    bool start(const QString &backend, QString *error) override;
    bool isStarted() const override { return m_impl != nullptr; }
    QString startedBackend() const override { return m_backend; }
    QString description() const override;
    QStringList fileSuffixes() const override;
    qb::media::Player *createPlayer() override;

    QString mediaDir() const;
    // media-extra/ of the add-on "Extra Video and Audio Formats", empty when it is not
    // installed or switched off
    QString extraDir() const;
    // where the FFmpeg libraries come from (with a slash at the end)
    QString ffmpegLibDir() const;

private:
    bool preload(const QString &backend, QString *error);
    qb::media::EngineInterface *m_impl = nullptr;
    QString m_backend;
    QString m_error;
};

} // namespace qbmedia
