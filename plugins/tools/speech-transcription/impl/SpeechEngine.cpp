// The engine of the add-on "Speech Transcription" (libqb-speech-impl.so): the microphone
// through Qt Multimedia, pieces between pauses (SpeechLogic), whisper.cpp in a thread of its
// own. Loaded by the writing tool (libqb-speech.so) when it is first needed; see SpeechApi.h.
#include "../SpeechApi.h"
#include "core/TestSwitches.h"
#include "../SpeechLogic.h"
#include "core/AudioBackend.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMediaDevices>
#include <QMutex>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QtEndian>

#include <atomic>
#include <cmath>
#include <dlfcn.h>

#include "ggml-backend.h"
#include "whisper.h"

Q_DECLARE_METATYPE(speech::EngineSettings)

namespace {

using speech::EngineSettings;

// the folder of the compute modules (libggml-cpu-*.so, libggml-vulkan.so): lib/ next to this library
QString moduleDir()
{
    const QByteArray env = qgetenv("QB_SPEECH_GGML_DIR");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env);
    Dl_info info;
    if (dladdr(reinterpret_cast<void *>(&moduleDir), &info) && info.dli_fname)
        return QFileInfo(QString::fromLocal8Bit(info.dli_fname)).absolutePath() + QStringLiteral("/lib");
    return QString();
}

void quietLog(enum ggml_log_level level, const char *text, void *)
{
    static const bool debug = qEnvironmentVariableIsSet("QB_SPEECH_DEBUG");
    if (debug || level == GGML_LOG_LEVEL_ERROR)
        fputs(text, stderr);
}

void loadBackends()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    whisper_log_set(quietLog, nullptr);
    ggml_log_set(quietLog, nullptr);
    const QString dir = moduleDir();
    if (!dir.isEmpty() && QFileInfo::exists(dir))
        ggml_backend_load_all_from_path(QFile::encodeName(dir).constData());
    else
        ggml_backend_load_all();
}

// a WAV file (PCM 8/16/24/32 bit or float) as 16 kHz mono
bool readWav(const QString &file, QVector<float> *out, QString *error)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("Cannot read %1").arg(file);
        return false;
    }
    const QByteArray d = f.readAll();
    if (d.size() < 44 || !d.startsWith("RIFF") || d.mid(8, 4) != "WAVE") {
        *error = QObject::tr("%1 is no WAV file").arg(file);
        return false;
    }
    int pos = 12, fmt = 0, channels = 0, rate = 0, bits = 0;
    QByteArray data;
    while (pos + 8 <= d.size()) {
        const QByteArray id = d.mid(pos, 4);
        const int len = int(qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(d.constData()) + pos + 4));
        const auto *p = reinterpret_cast<const uchar *>(d.constData()) + pos + 8;
        if (id == "fmt ") {
            fmt = qFromLittleEndian<quint16>(p);
            channels = qFromLittleEndian<quint16>(p + 2);
            rate = int(qFromLittleEndian<quint32>(p + 4));
            bits = qFromLittleEndian<quint16>(p + 14);
            if (fmt == 0xfffe && len >= 26) // extensible: the sub-format's first two bytes
                fmt = qFromLittleEndian<quint16>(p + 24);
        } else if (id == "data") {
            data = d.mid(pos + 8, qMin(len, int(d.size()) - pos - 8));
        }
        pos += 8 + len + (len & 1);
    }
    if (channels <= 0 || rate <= 0 || data.isEmpty() || (fmt != 1 && fmt != 3)) {
        *error = QObject::tr("%1: a WAV format that is not read here").arg(file);
        return false;
    }
    const int bytes = bits / 8;
    const int frames = int(data.size()) / (bytes * channels);
    QVector<float> inter(frames * channels);
    const auto *p = reinterpret_cast<const uchar *>(data.constData());
    for (int i = 0; i < frames * channels; ++i, p += bytes) {
        float v = 0;
        if (fmt == 3 && bits == 32)
            v = qFromLittleEndian<float>(p);
        else if (bits == 16)
            v = qFromLittleEndian<qint16>(p) / 32768.f;
        else if (bits == 8)
            v = (int(*p) - 128) / 128.f;
        else if (bits == 24)
            v = float(qint32(quint32(p[0]) << 8 | quint32(p[1]) << 16 | quint32(p[2]) << 24) >> 8) / 8388608.f;
        else if (bits == 32)
            v = qFromLittleEndian<qint32>(p) / 2147483648.f;
        inter[i] = v;
    }
    *out = speech::toMono16k(inter.constData(), frames, channels, rate);
    return true;
}

whisper_context *openModel(const EngineSettings &s, QString *error)
{
    loadBackends();
    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu = s.gpuIndex >= 0;
    cp.gpu_device = qMax(0, s.gpuIndex);
    cp.flash_attn = s.gpuIndex >= 0;
    whisper_context *ctx = whisper_init_from_file_with_params(QFile::encodeName(s.modelFile).constData(), cp);
    if (!ctx && error)
        *error = s.gpuIndex >= 0 ? QObject::tr("The model could not be loaded onto the graphics card. Choose the processor "
                                              "under Options > Speech > Processing, or a smaller model.")
                                 : QObject::tr("The model could not be loaded (damaged file, or not enough memory).");
    return ctx;
}

QString segmentText(whisper_context *ctx, bool guard)
{
    QString out;
    const int n = whisper_full_n_segments(ctx);
    for (int i = 0; i < n; ++i) {
        if (guard && whisper_full_get_segment_no_speech_prob(ctx, i) > 0.6f)
            continue; // most likely no speech: whisper invents text for such stretches
        out += QString::fromUtf8(whisper_full_get_segment_text(ctx, i));
    }
    return speech::cleanText(out, guard);
}

// One run of whisper over a piece. `language` empty: whisper's own guess.
QString runWhisper(whisper_context *ctx, const EngineSettings &s, const QVector<float> &audio, const QString &language,
                   bool translate, const QString &context, QString *error)
{
    whisper_full_params p = whisper_full_default_params(s.careful ? WHISPER_SAMPLING_BEAM_SEARCH : WHISPER_SAMPLING_GREEDY);
    if (s.careful)
        p.beam_search.beam_size = 5;
    p.n_threads = qMax(1, s.threads);
    p.translate = translate;
    const QByteArray lang = language.isEmpty() ? QByteArrayLiteral("auto") : language.toLatin1();
    p.language = lang.constData();
    p.no_timestamps = true;
    p.no_context = true;
    p.print_progress = false;
    p.print_realtime = false;
    p.print_special = false;
    p.print_timestamps = false;
    p.suppress_blank = true;
    p.suppress_nst = s.silenceGuard;
    QString prompt = s.hint.simplified();
    if (s.useContext && !context.isEmpty())
        prompt = (prompt.isEmpty() ? QString() : prompt + QLatin1Char(' ')) + context.right(200);
    const QByteArray promptBytes = prompt.toUtf8();
    if (!promptBytes.isEmpty())
        p.initial_prompt = promptBytes.constData();
    if (whisper_full(ctx, p, audio.constData(), int(audio.size())) != 0) {
        if (error)
            *error = QObject::tr("whisper could not transcribe a piece.");
        return QString();
    }
    return segmentText(ctx, s.silenceGuard);
}

// ------------------------------------------------------------------ the thread with the model

class Worker : public QObject
{
    Q_OBJECT
public:
    ~Worker() override { unload(); }

public slots:
    void prepare(const speech::EngineSettings &s)
    {
        m_s = s;
        m_pieces = 0;
        // tests: QB_SPEECH_FAKE writes "Piece <n> (<seconds> s)" for each piece instead of running
        // the model (the pieces, the queue and the file can be checked without a real model)
        if (QB_TEST_ENV_SET("QB_SPEECH_FAKE")) {
            m_fake = true;
            emit ready();
            return;
        }
        m_fake = false;
        if (m_ctx && (m_file != s.modelFile || m_gpu != s.gpuIndex))
            unload();
        if (!m_ctx) {
            emit state(QStringLiteral("loading"));
            QString error;
            m_ctx = openModel(s, &error);
            if (!m_ctx) {
                emit failed(error);
                return;
            }
            m_file = s.modelFile;
            m_gpu = s.gpuIndex;
        }
        m_context.clear();
        emit ready();
    }

    void process(const QVector<float> &audio, bool newParagraph)
    {
        ++m_pieces;
        if (m_fake) {
            QThread::msleep(50);
            emit done(QStringLiteral("Piece %1 (%2 s)").arg(m_pieces).arg(audio.size() / 16000.0, 0, 'f', 1), newParagraph);
            return;
        }
        if (!m_ctx) {
            emit done(QString(), newParagraph);
            return;
        }
        const bool translate = speech::canTranslate(m_s.mainLanguage, m_s.fallbackLanguage, m_s.modelTranslates);
        const bool two = !m_s.fallbackLanguage.isEmpty() && m_s.fallbackLanguage != m_s.mainLanguage;
        // Into English: the main language as the spoken one, and whisper translates (one run of
        // the model per piece). Otherwise, and for "keep the original", the language of each piece
        // is found first (one more run of the model's encoder).
        QString language = m_s.mainLanguage;
        bool spokenOther = false;
        if (two && (!translate || m_s.keepOriginal || m_s.careful)) {
            if (whisper_pcm_to_mel(m_ctx, audio.constData(), int(audio.size()), qMax(1, m_s.threads)) == 0) {
                QVector<float> probs(whisper_lang_max_id() + 1);
                if (whisper_lang_auto_detect(m_ctx, 0, qMax(1, m_s.threads), probs.data()) >= 0) {
                    const int a = whisper_lang_id(m_s.mainLanguage.toLatin1().constData());
                    const int b = whisper_lang_id(m_s.fallbackLanguage.toLatin1().constData());
                    const speech::LanguageChoice c = speech::chooseLanguage(
                        m_s.mainLanguage, m_s.fallbackLanguage, a >= 0 ? probs[a] : 0.f, b >= 0 ? probs[b] : 0.f,
                        m_s.modelTranslates);
                    language = c.language;
                    spokenOther = language != m_s.mainLanguage;
                }
            }
        }
        QString error;
        QString text = runWhisper(m_ctx, m_s, audio, language, translate, m_context, &error);
        if (!error.isEmpty())
            emit failed(error);
        if (translate && spokenOther && m_s.keepOriginal && !text.isEmpty()) {
            const QString original = runWhisper(m_ctx, m_s, audio, language, false, QString(), nullptr);
            if (!original.isEmpty() && original != text)
                text += QStringLiteral(" [%1: %2]").arg(language, original);
        }
        if (!text.isEmpty())
            m_context += QLatin1Char(' ') + text;
        emit done(text, newParagraph);
    }

    void unload()
    {
        if (m_ctx)
            whisper_free(m_ctx);
        m_ctx = nullptr;
        m_file.clear();
    }

signals:
    void state(const QString &s);
    void ready();
    void failed(const QString &message);
    void done(const QString &text, bool newParagraph);

private:
    whisper_context *m_ctx = nullptr;
    QString m_file;
    int m_gpu = -1;
    speech::EngineSettings m_s;
    QString m_context;
    int m_pieces = 0;
    bool m_fake = false;
};

// ------------------------------------------------------------------ the engine

class Engine : public QObject, public speech::EngineInterface
{
    Q_OBJECT
public:
    Engine()
    {
        qRegisterMetaType<speech::EngineSettings>("speech::EngineSettings");
        qRegisterMetaType<QVector<float>>("QVector<float>");
        m_worker = new Worker;
        m_worker->moveToThread(&m_thread);
        connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
        connect(m_worker, &Worker::state, this, [this](const QString &s) { report(s); });
        connect(m_worker, &Worker::failed, this, [this](const QString &m) {
            if (onError)
                onError(m);
            if (!m_modelReady) { // the model never came: nothing more to do
                stopAudio();
                m_feedTimer.stop();
                m_recording = false;
                m_fileFast = false;
                m_waiting = 0;
                m_busy = false;
                report(QStringLiteral("finished"));
            }
        });
        connect(m_worker, &Worker::ready, this, [this] {
            m_modelReady = true;
            report(m_recording ? activeState() : QStringLiteral("transcribing %1").arg(m_waiting));
        });
        connect(m_worker, &Worker::done, this, &Engine::pieceDone);
        m_thread.setObjectName(QStringLiteral("speech-transcription"));
        m_thread.start();
        m_idle.setSingleShot(true);
        m_idle.setInterval(5 * 60 * 1000); // the model stays loaded 5 minutes after a recording
        connect(&m_idle, &QTimer::timeout, this, [this] { unloadModel(); });
        m_feedTimer.setInterval(100);
        connect(&m_feedTimer, &QTimer::timeout, this, &Engine::feedFromFile);
    }

    ~Engine() override
    {
        stopAudio();
        m_thread.quit();
        m_thread.wait();
    }

    QList<speech::AudioInput> audioInputs() override
    {
        QList<speech::AudioInput> out;
        const QAudioDevice def = QMediaDevices::defaultAudioInput();
        for (const QAudioDevice &d : QMediaDevices::audioInputs())
            out << speech::AudioInput{d.id(), d.description(), d.id() == def.id()};
        return out;
    }

    QList<speech::ComputeDevice> computeDevices() override
    {
        loadBackends();
        QList<speech::ComputeDevice> out;
        out << speech::ComputeDevice{-1, tr("Processor"), 0, 0, false};
        int gpu = 0;
        for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
            ggml_backend_dev_t dev = ggml_backend_dev_get(i);
            const auto type = ggml_backend_dev_type(dev);
            if (type != GGML_BACKEND_DEVICE_TYPE_GPU && type != GGML_BACKEND_DEVICE_TYPE_IGPU)
                continue;
            size_t freeMem = 0, total = 0;
            ggml_backend_dev_memory(dev, &freeMem, &total);
            out << speech::ComputeDevice{gpu++, QString::fromUtf8(ggml_backend_dev_description(dev)), qint64(total),
                                         qint64(freeMem), type == GGML_BACKEND_DEVICE_TYPE_IGPU};
        }
        return out;
    }

    speech::TestResult transcribeFile(const QString &wavFile, const EngineSettings &s, bool translate) override
    {
        speech::TestResult r;
        QVector<float> audio;
        if (!readWav(wavFile, &audio, &r.error))
            return r;
        r.audioSeconds = audio.size() / 16000.0;
        QElapsedTimer t;
        t.start();
        whisper_context *ctx = openModel(s, &r.error);
        if (!ctx)
            return r;
        r.loadSeconds = t.elapsed() / 1000.0;
        r.modelType = QString::fromUtf8(whisper_model_type_readable(ctx));
        t.restart();
        r.text = runWhisper(ctx, s, audio, translate ? QString() : QStringLiteral("en"), translate, QString(), &r.error);
        r.seconds = t.elapsed() / 1000.0;
        whisper_free(ctx);
        r.ok = r.error.isEmpty();
        return r;
    }

    bool start(const EngineSettings &s, QString *error, const QString &wavFile) override
    {
        if (m_busy) {
            if (error)
                *error = tr("A transcription is still running.");
            return false;
        }
        if (s.recordOnly)
            return startRecordOnly(s, error, wavFile);
        if (!QFileInfo::exists(s.modelFile)) {
            if (error)
                *error = tr("No model is installed.");
            return false;
        }
        m_s = s;
        m_idle.stop();
        speech::Segmenter::Params sp;
        sp.sensitivity = s.sensitivity;
        sp.pauseMs = s.pauseMs;
        sp.paragraphMs = s.paragraphMs;
        m_seg.reset(new speech::Segmenter(sp));
        m_waiting = 0;
        m_modelReady = false;
        m_levelClock.start();
        if (!wavFile.isEmpty()) {
            QString e;
            if (!readWav(wavFile, &m_fileAudio, &e)) {
                if (error)
                    *error = e;
                return false;
            }
            m_filePos = 0;
            // a recording transcribed later: as fast as the model takes the pieces (feedFromFile
            // waits while pieces are queued); tests: faster than real time
            m_fileFast = s.fileAsFast;
            m_feedTimer.setInterval(s.fileAsFast ? 0 : QB_TEST_ENV_SET("QB_SPEECH_FAST") ? 5 : 100);
            m_feedTimer.start();
        } else if (!startAudio(s.audioInput, error)) {
            return false;
        }
        m_recording = true;
        m_busy = true;
        QMetaObject::invokeMethod(m_worker, "prepare", Qt::QueuedConnection, Q_ARG(speech::EngineSettings, s));
        return true;
    }

    void stop() override
    {
        if (!m_recording)
            return;
        if (m_writer) {
            // record only: the file is complete; nothing else to wait for
            stopAudio();
            m_feedTimer.stop();
            m_recording = false;
            m_writer->close();
            m_writer.reset();
            m_busy = false;
            report(QStringLiteral("finished"));
            return;
        }
        stopAudio();
        m_feedTimer.stop();
        m_recording = false;
        if (m_seg) {
            m_seg->flush();
            sendPieces();
        }
        if (m_waiting == 0)
            finish();
        else
            report(QStringLiteral("transcribing %1").arg(m_waiting));
    }

    bool isRecording() const override { return m_recording; }
    bool isBusy() const override { return m_busy; }
    int waiting() const override { return m_waiting; }

    void unloadModel() override
    {
        if (m_busy)
            return;
        QMetaObject::invokeMethod(m_worker, "unload", Qt::BlockingQueuedConnection);
    }

    QString version() const override { return QStringLiteral("1.9.5"); }

private:
    bool startAudio(const QByteArray &id, QString *error)
    {
        QAudioDevice dev = QMediaDevices::defaultAudioInput();
        for (const QAudioDevice &d : QMediaDevices::audioInputs())
            if (!id.isEmpty() && d.id() == id)
                dev = d;
        if (dev.isNull()) {
            if (error)
                *error = tr("No microphone was found.");
            return false;
        }
        QAudioFormat f;
        f.setSampleRate(16000);
        f.setChannelCount(1);
        f.setSampleFormat(QAudioFormat::Float);
        if (!dev.isFormatSupported(f))
            f = dev.preferredFormat();
        m_format = f;
        m_source = new QAudioSource(dev, f, this);
        m_source->setBufferSize(f.bytesForDuration(200000));
        m_io = m_source->start();
        if (!m_io || m_source->error() != QAudio::NoError) {
            if (error)
                *error = tr("The microphone \"%1\" could not be opened.").arg(dev.description());
            stopAudio();
            return false;
        }
        connect(m_io, &QIODevice::readyRead, this, &Engine::readAudio);
        return true;
    }

    void stopAudio()
    {
        if (m_source) {
            m_source->stop();
            m_source->deleteLater();
        }
        m_source = nullptr;
        m_io = nullptr;
    }

    void readAudio()
    {
        if (!m_io)
            return;
        const QByteArray d = m_io->readAll();
        const int bpf = m_format.bytesPerFrame();
        if (bpf <= 0)
            return;
        const int frames = int(d.size()) / bpf;
        const int ch = m_format.channelCount();
        QVector<float> inter(frames * ch);
        const char *p = d.constData();
        for (int i = 0; i < frames * ch; ++i)
            inter[i] = m_format.normalizedSampleValue(p + i * m_format.bytesPerSample());
        const QVector<float> mono = speech::toMono16k(inter.constData(), frames, ch, m_format.sampleRate());
        feed(mono);
    }

    void feedFromFile()
    {
        const int step = 1600; // 100 ms
        if (m_filePos >= m_fileAudio.size()) {
            m_feedTimer.stop();
            stop();
            return;
        }
        // a recording read as fast as possible: at most two pieces wait for the model, so the
        // memory stays small however long the recording is
        if (m_fileFast && m_waiting >= 2) {
            m_feedTimer.stop(); // pieceDone starts it again: no busy waiting
            return;
        }
        for (int round = 0; round < (m_fileFast ? 50 : 1) && m_filePos < m_fileAudio.size(); ++round) {
            const int n = int(qMin<qsizetype>(step, m_fileAudio.size() - m_filePos));
            feed(QVector<float>(m_fileAudio.constData() + m_filePos, m_fileAudio.constData() + m_filePos + n));
            m_filePos += n;
            if (m_fileFast && m_waiting >= 2)
                break;
        }
        if (m_fileFast && m_modelReady) {
            const int pct = int(100 * m_filePos / qMax<qsizetype>(1, m_fileAudio.size()));
            if (pct != m_lastPct) {
                m_lastPct = pct;
                report(QStringLiteral("reading %1").arg(pct));
            }
        }
    }

    // Record only (0.64): the microphone (or, for the tests, a WAV file in real time) into a WAV file
    bool startRecordOnly(const EngineSettings &s, QString *error, const QString &wavFile)
    {
        m_s = s;
        m_idle.stop();
        m_levelClock.start();
        m_writer.reset(new speech::WavWriter);
        if (!m_writer->open(s.recordFile, error)) {
            m_writer.reset();
            return false;
        }
        if (!wavFile.isEmpty()) {
            QString e;
            if (!readWav(wavFile, &m_fileAudio, &e)) {
                if (error)
                    *error = e;
                m_writer->close();
                m_writer.reset();
                QFile::remove(s.recordFile);
                return false;
            }
            m_filePos = 0;
            m_fileFast = false;
            m_feedTimer.setInterval(QB_TEST_ENV_SET("QB_SPEECH_FAST") ? 5 : 100);
            m_feedTimer.start();
        } else if (!startAudio(s.audioInput, error)) {
            m_writer->close();
            m_writer.reset();
            QFile::remove(s.recordFile);
            return false;
        }
        m_recording = true;
        m_busy = true;
        report(QStringLiteral("recording"));
        return true;
    }

    void feed(const QVector<float> &mono)
    {
        if (m_writer) {
            m_writer->write(mono.constData(), int(mono.size()));
            if (onLevel && m_levelClock.isValid() && m_levelClock.elapsed() >= 50 && !mono.isEmpty()) {
                m_levelClock.restart();
                double sum = 0;
                for (float v : mono)
                    sum += double(v) * v;
                const float db = 20.f * std::log10(float(std::sqrt(sum / mono.size())) + 1e-9f);
                onLevel(qBound(0.f, (db + 60.f) / 60.f, 1.f));
            }
            return;
        }
        if (!m_seg)
            return;
        m_seg->feed(mono.constData(), int(mono.size()));
        if (onLevel && m_levelClock.isValid() && m_levelClock.elapsed() >= 50) {
            m_levelClock.restart();
            onLevel(m_seg->level());
        }
        sendPieces();
    }

    void sendPieces()
    {
        for (const speech::Piece &piece : m_seg->take()) {
            ++m_waiting;
            QMetaObject::invokeMethod(m_worker, "process", Qt::QueuedConnection, Q_ARG(QVector<float>, piece.audio),
                                      Q_ARG(bool, piece.newParagraph));
        }
        if (m_waiting > 0 && m_modelReady)
            report(m_recording ? activeState() : QStringLiteral("transcribing %1").arg(m_waiting));
    }

    void pieceDone(const QString &text, bool newParagraph)
    {
        m_waiting = qMax(0, m_waiting - 1);
        if (m_fileFast && m_recording && !m_feedTimer.isActive() && m_waiting < 2)
            m_feedTimer.start();
        if (!text.isEmpty() && onText)
            onText(text, newParagraph);
        if (!m_recording && m_waiting == 0)
            finish();
        else if (!m_recording)
            report(QStringLiteral("transcribing %1").arg(m_waiting));
    }

    void finish()
    {
        m_fileFast = false;
        m_lastPct = -1;
        m_busy = false;
        m_idle.start();
        report(QStringLiteral("finished"));
    }

    QString activeState() const
    {
        return m_fileFast ? QStringLiteral("reading %1").arg(qMax(0, m_lastPct)) : QStringLiteral("listening");
    }

    void report(const QString &s)
    {
        if (onState)
            onState(s);
    }

    QThread m_thread;
    Worker *m_worker = nullptr;
    EngineSettings m_s;
    QScopedPointer<speech::Segmenter> m_seg;
    QAudioSource *m_source = nullptr;
    QIODevice *m_io = nullptr;
    QAudioFormat m_format;
    QVector<float> m_fileAudio;
    qsizetype m_filePos = 0;
    QTimer m_feedTimer;
    bool m_fileFast = false;
    int m_lastPct = -1;
    QScopedPointer<speech::WavWriter> m_writer;   // record only (0.64)
    QTimer m_idle;
    QElapsedTimer m_levelClock;
    int m_waiting = 0;
    bool m_recording = false;
    bool m_busy = false;
    bool m_modelReady = false;
};

} // namespace

extern "C" Q_DECL_EXPORT speech::EngineInterface *qb_speech_impl_create()
{
    audiobackend::avoidUnconfiguredPipeWire(); // 0.80.0: before Qt Multimedia looks for a microphone
    return new Engine;
}

#include "SpeechEngine.moc"
