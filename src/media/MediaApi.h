#pragma once
// Interface between the program and a media engine (version 1.0).
//
// Playing audio and video inside the program is delivered as an add-on
// ("Media Playback"), because the libraries that decode sound and pictures
// are large and not every writer needs them. The add-on brings a Qt plugin
// (a MODULE library in its plugins/ folder) that implements
// qb::media::EngineInterface; nothing in it is linked against the program,
// and the program needs nothing of the engine but this header.
//
// The program draws everything the writer touches: the play controls in the
// editor's footer bar, Navigate > Media, the options. The engine plays one
// file per Player and gives a widget that shows the picture of a video.
//
// Plugins built for another interface version (QB_MEDIA_IID) are not loaded.
// All functions are called on the GUI thread; so are the listener's.

#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtPlugin>

class QWidget;

namespace qb {
namespace media {

enum class State { Stopped, Playing, Paused };

enum class Status {
    NoMedia,     // nothing loaded
    Loading,
    Loaded,      // ready to play
    Buffering,
    EndOfMedia,  // played to the end (state is Stopped)
    Invalid      // cannot be played; errorString() says why
};

// What a player reports. The program sets one listener per player.
class PlayerListener
{
public:
    virtual ~PlayerListener() = default;
    virtual void playerStateChanged() = 0;          // state, status, error, hasVideo / hasAudio
    virtual void playerPositionChanged(qint64 ms) = 0;
    virtual void playerDurationChanged(qint64 ms) = 0;
    virtual void playerMetaDataChanged() = 0;        // metaData(), videoSize()
};

class Player
{
public:
    virtual ~Player() = default;
    virtual void setListener(PlayerListener *listener) = 0;

    // A local file; an empty path unloads.
    virtual void setSource(const QString &file) = 0;
    virtual QString source() const = 0;

    // Shows the picture (black while there is none). Owned by the player;
    // the program puts it into its own layout.
    virtual QWidget *videoWidget() = 0;

    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;   // back to the start
    virtual State state() const = 0;
    virtual Status status() const = 0;
    virtual QString errorString() const = 0;   // empty: no error

    virtual qint64 position() const = 0;        // milliseconds
    virtual void setPosition(qint64 ms) = 0;
    virtual qint64 duration() const = 0;        // 0 while unknown
    virtual bool isSeekable() const = 0;

    virtual double rate() const = 0;            // 1.0 = normal speed
    virtual void setRate(double rate) = 0;
    virtual double volume() const = 0;          // 0 ... 1
    virtual void setVolume(double volume) = 0;
    virtual bool isMuted() const = 0;
    virtual void setMuted(bool muted) = 0;

    virtual bool hasVideo() const = 0;
    virtual bool hasAudio() const = 0;
    virtual QSize videoSize() const = 0;
    // Keys used where known: "title", "artist", "album", "date", "comment".
    virtual QVariantMap metaData() const = 0;
};

// A way of decoding the engine offers (for the options).
struct Backend {
    QString id;          // "ffmpeg", "gstreamer"
    QString name;        // shown in the options
    bool available = true;
    QString note;        // why not available, or what it needs
};

class EngineInterface
{
public:
    virtual ~EngineInterface() = default;
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString version() const = 0;

    virtual QList<Backend> backends() const = 0;
    // Called once, before the first player. `backend` is one of backends()
    // (empty: the engine's own choice). Returns false and the reason if the
    // engine cannot play anything on this system.
    virtual bool start(const QString &backend, QString *error) = 0;
    virtual bool isStarted() const = 0;
    virtual QString startedBackend() const = 0;
    // One line for the options and the Add-on Manager, e.g.
    // "Qt Multimedia 6.4.2 with FFmpeg 6.1.1 (built in)".
    virtual QString description() const = 0;
    // Lower-case file name suffixes the engine is meant to play.
    virtual QStringList fileSuffixes() const = 0;
    // A new player (the caller owns it); null if the engine is not started.
    virtual Player *createPlayer() = 0;
};

} // namespace media
} // namespace qb

#define QB_MEDIA_IID "org.quillbinder.MediaEngine/1.0"
Q_DECLARE_INTERFACE(qb::media::EngineInterface, QB_MEDIA_IID)
