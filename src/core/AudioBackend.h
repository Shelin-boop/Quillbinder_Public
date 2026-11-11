#pragma once
// The sound system Qt Multimedia uses on Linux (0.80.0). Header-only, for the add-ons that bring Qt Multimedia
// (Media Playback, Speech Transcription); Qt Core only.
//
// Qt 6.12 tries PipeWire first whenever PipeWire's client library is installed. When the library is there but its
// client configuration is not (libpipewire installed without its "common" package, a system that runs PulseAudio
// only), pw_context_new() fails and Qt 6.12.0 uses the missing context anyway: the program ended with a crash at
// the first sound (seen in the test machine; Qt's qpipewire_audiocontextmanager.cpp does not check the context).
// Such a system has no PipeWire to talk to, so Qt is told to use PulseAudio there (QT_AUDIO_BACKEND, any value but
// "pipewire" skips PipeWire). A choice made in the environment is left alone.

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace audiobackend {

// true when PipeWire's client could read its configuration (the places libpipewire 1.x looks in)
inline bool pipewireClientConfigured()
{
    const QString name = qEnvironmentVariableIsSet("PIPEWIRE_CONFIG_NAME") ? qEnvironmentVariable("PIPEWIRE_CONFIG_NAME")
                                                                         : QStringLiteral("client.conf");
    if (QDir::isAbsolutePath(name))
        return QFileInfo::exists(name);
    QStringList dirs;
    if (qEnvironmentVariableIsSet("PIPEWIRE_CONFIG_DIR")) {
        dirs << qEnvironmentVariable("PIPEWIRE_CONFIG_DIR");
    } else {
        const QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
        dirs << (xdg.isEmpty() ? QDir::homePath() + QStringLiteral("/.config") : xdg) + QStringLiteral("/pipewire")
             << QStringLiteral("/etc/pipewire") << QStringLiteral("/usr/share/pipewire");
    }
    for (const QString &d : std::as_const(dirs))
        if (QFileInfo::exists(d + QLatin1Char('/') + name))
            return true;
    return false;
}

// call before the first QMediaDevices, QAudioOutput or QAudioSource of the process
inline void avoidUnconfiguredPipeWire()
{
#ifdef Q_OS_LINUX
    if (qEnvironmentVariableIsSet("QT_AUDIO_BACKEND"))
        return;
    if (!pipewireClientConfigured())
        qputenv("QT_AUDIO_BACKEND", "pulseaudio");
#endif
}

} // namespace audiobackend
