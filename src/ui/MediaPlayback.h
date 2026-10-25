#pragma once
// Audio and video in the program (milestone M11).
//
// Playing is done by a media engine from an add-on ("Media Playback",
// src/media/MediaApi.h); without it audio and video files are shown as
// before (Open with Default Application). Everything the writer touches is
// here and in MediaView: the play controls (MediaControls: the footer bar of
// an editor, the bottom of a copyholder or Quick Reference panel), the
// commands of Navigate > Media (MainWindow), the options (Behaviors > Playback).
//
// playback::hub() knows every MediaView that has a player and which one the
// commands act on (target()): the one the writer used last, else one that
// plays, else the first one.

#include <QObject>
#include <QPointer>
#include <QString>
#include <QWidget>

class QLabel;
class QSlider;
class QToolButton;

namespace qb {

class MediaView;

namespace media {
class EngineInterface;
}

namespace playback {

// The engine of the installed add-on (null if there is none).
media::EngineInterface *engine();
// Starts the engine with the backend of the options (once per program run).
// False and the reason if there is no engine or it cannot run here.
bool start(QString *error = nullptr);
// One line for the options: what plays audio and video, or why nothing does.
QString status();
// Can this file be played by the engine (by its suffix)?
bool canPlay(const QString &suffix);

QString formatTime(qint64 ms, bool hours = false);    // "3:07", "1:02:03"
QString formatTimestamp(qint64 ms, bool hours);         // "[03:07]", "[1:02:03]"
// A timestamp ("[03:07]", "1:02:03", "12:05.5") in the line at `pos`, the
// nearest one before or around the cursor; -1 if there is none.
qint64 parseTimestamp(const QString &line, int pos);

class Hub : public QObject
{
    Q_OBJECT
public:
    void add(MediaView *v);
    void remove(MediaView *v);
    void touch(MediaView *v);          // the writer used this player
    MediaView *target() const;
    QList<MediaView *> views() const;
    void notifyChanged() { emit changed(); }
    void notifySettings() { emit settingsChanged(); }

signals:
    void changed();          // a player came or went, started or stopped, another rate ...
    void settingsChanged();  // pause and rewind, volume (shared by all players)

private:
    QList<QPointer<MediaView>> m_views;
    QPointer<MediaView> m_last;
};
Hub &hub();

// Play controls bound to one MediaView: pause and rewind, go to start, skip
// back, play / pause, skip forward, position, time, speed, volume.
class MediaControls : public QWidget
{
    Q_OBJECT
public:
    explicit MediaControls(QWidget *parent = nullptr);
    void setView(MediaView *v);
    MediaView *view() const;
    void refresh();

private:
    void refreshPosition();
    void refreshSettings();
    QPointer<MediaView> m_view;
    QToolButton *m_rewind, *m_start, *m_back, *m_play, *m_forward, *m_speed, *m_volume;
    QSlider *m_slider;
    QSlider *m_volumeSlider;
    QLabel *m_time;
    bool m_dragging = false;
};

} // namespace playback
} // namespace qb
