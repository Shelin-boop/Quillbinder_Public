#pragma once
// Between the two libraries of the add-on "Speech Transcription" (0.61):
//  - libqb-speech.so       the writing tool the program loads (Qt Widgets, Network): commands,
//                          the inspector tab, the options, models and their download
//  - libqb-speech-impl.so  the engine, loaded by the tool when it is needed: microphone (Qt
//                          Multimedia, brought by the add-on), pauses, whisper.cpp
// The tool finds the engine in the add-on's speech/ folder and calls qb_speech_impl_create().
// Both are built from this source together, so this header may change with them.
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

namespace speech {

struct AudioInput {
    QByteArray id;        // QAudioDevice::id()
    QString name;
    bool isDefault = false;
};

// A place to compute on: the processor, or a graphics card found by whisper.cpp's Vulkan module.
struct ComputeDevice {
    int gpuIndex = -1;     // -1: the processor; else the number among graphics cards (whisper's gpu_device)
    QString name;
    qint64 memoryTotal = 0; // bytes (0: not known)
    qint64 memoryFree = 0;
    bool integrated = false;
};

struct EngineSettings {
    QString modelFile;
    int gpuIndex = -1;        // -1: processor
    int threads = 4;
    QString mainLanguage = QStringLiteral("en");      // the text's language
    QString fallbackLanguage = QStringLiteral("de");  // also spoken; empty: none
    bool modelTranslates = true;  // false: the model cannot translate (turbo)
    bool keepOriginal = false;    // translated pieces keep what was said, in brackets after them
    bool careful = false;         // beam search instead of greedy decoding
    QString hint;                 // words and names the text uses
    bool useContext = false;      // the text before as context for the next piece
    bool silenceGuard = true;     // leave out what whisper invents in silence or noise
    int sensitivity = 1;          // 0 low, 1 medium, 2 high: how quiet speech may be
    int pauseMs = 700;            // a pause this long ends a piece
    int paragraphMs = 3000;       // a pause this long starts a new paragraph (0: never)
    QByteArray audioInput;        // empty: the system's default input
};

struct TestResult {
    bool ok = false;
    QString text;          // what whisper wrote
    double audioSeconds = 0;
    double seconds = 0;    // time taken (loading not counted)
    double loadSeconds = 0;
    QString modelType;     // whisper's name for the loaded model ("medium", "large v3" ...)
    QString error;
};

class EngineInterface
{
public:
    virtual ~EngineInterface() = default;
    virtual QList<AudioInput> audioInputs() = 0;
    virtual QList<ComputeDevice> computeDevices() = 0;
    // Transcribes a WAV file (16-bit PCM) with these settings (the model's own test, the speed
    // test); blocks: call it outside the main thread.
    virtual TestResult transcribeFile(const QString &wavFile, const EngineSettings &settings, bool translate) = 0;

    // Recording: from the microphone (wavFile empty) or from a WAV file played in real time
    // (wavFile, used by the tests). The callbacks come in the thread that called start().
    std::function<void(const QString &text, bool newParagraph)> onText;
    std::function<void(float level)> onLevel;          // 0..1, about 20 times a second while recording
    std::function<void(const QString &state)> onState; // "loading", "listening", "transcribing <n>", "finished"
    std::function<void(const QString &message)> onError;
    virtual bool start(const EngineSettings &settings, QString *error, const QString &wavFile = QString()) = 0;
    // Stops the microphone; the pieces waiting are still written, then onState("finished").
    virtual void stop() = 0;
    virtual bool isRecording() const = 0;
    virtual bool isBusy() const = 0;     // recording or still transcribing
    virtual int waiting() const = 0;     // pieces not transcribed yet
    // Frees the model (it is kept a few minutes after a recording).
    virtual void unloadModel() = 0;
    virtual QString version() const = 0; // whisper.cpp's version
};

} // namespace speech

using SpeechCreateFn = speech::EngineInterface *(*)();
// extern "C" speech::EngineInterface *qb_speech_impl_create();
