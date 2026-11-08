#include "SpeechLogic.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QObject>
#include <QLocale>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtEndian>
#include <cmath>
#include <cstring>

namespace speech {

// ------------------------------------------------------------------ models

QList<CatalogEntry> catalog()
{
    // Files and SHA-1 sums of whisper.cpp's model list (huggingface.co/ggerganov/whisper.cpp,
    // models/README.md of whisper.cpp v1.9.5 and the source's README). Sizes as the source gives
    // them; the memory and speed notes are estimates (0.61, not measured on the writer's computer).
    QList<CatalogEntry> list;
    list << CatalogEntry{QStringLiteral("small"), QObject::tr("Small"), QStringLiteral("ggml-small-q5_1.bin"),
                         QStringLiteral("6fe57ddcfdd1c6b07cdcc73aaf620810ce5fc771"), 190000000LL, 12, 12, 80, true,
                         QObject::tr("Download 190 MB, memory about 0.5 GB. Fast, also on older processors. "
                                     "Recognition is good for clear speech; translation into English only rough. "
                                     "For quick notes.")};
    list << CatalogEntry{QStringLiteral("medium"), QObject::tr("Medium"), QStringLiteral("ggml-medium-q5_0.bin"),
                         QStringLiteral("7718d4c1ec62ca96998f058114db98236937490e"), 539000000LL, 24, 24, 80, true,
                         QObject::tr("Download 539 MB, memory about 1.2 GB. Keeps up with speech on most processors of "
                                     "recent years (estimate). Good recognition and translation.")};
    list << CatalogEntry{QStringLiteral("large-v3"), QObject::tr("Large v3"), QStringLiteral("ggml-large-v3-q5_0.bin"),
                         QStringLiteral("e6e2ed78495d403bef4b7cff42ef4aaadcfea8de"), 1080000000LL, 32, 32, 128, true,
                         QObject::tr("Download about 1.1 GB, memory about 2 GB. The best recognition and translation. On "
                                     "a processor often slower than speech: in long stretches the text falls behind "
                                     "(estimate). A graphics card helps.")};
    list << CatalogEntry{QStringLiteral("large-v3-turbo"), QObject::tr("Large v3 Turbo"),
                         QStringLiteral("ggml-large-v3-turbo-q5_0.bin"), QStringLiteral("e050f7970618a659205450ad97eb95a18d69c9ee"),
                         574000000LL, 32, 4, 128, false,
                         QObject::tr("Download 574 MB, memory about 0.9 GB. Recognition close to Large v3 and faster, but "
                                     "it cannot translate: what is said in the fallback language stays in that language.")};
    return list;
}

const CatalogEntry *catalogEntry(const QString &id)
{
    static const QList<CatalogEntry> list = catalog();
    for (const CatalogEntry &e : list)
        if (e.id == id)
            return &e;
    return nullptr;
}

const CatalogEntry *catalogEntryBySha1(const QString &sha1)
{
    static const QList<CatalogEntry> list = catalog();
    for (const CatalogEntry &e : list)
        if (e.sha1.compare(sha1, Qt::CaseInsensitive) == 0)
            return &e;
    return nullptr;
}

QString recommendedModel() { return QStringLiteral("medium"); }

QString officialBase()
{
    // the tests point this at a local server
    const QString test = qEnvironmentVariable("QB_SPEECH_SOURCE");
    return test.isEmpty() ? QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main") : test;
}

QString downloadUrl(const CatalogEntry &e, const QString &otherAddress)
{
    QString base = otherAddress.trimmed();
    if (base.isEmpty())
        base = officialBase();
    if (base.endsWith(QLatin1String(".bin"), Qt::CaseInsensitive))
        return base;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    return base + QLatin1Char('/') + e.file;
}

QString ModelHeader::kind() const
{
    if (!ok)
        return QString();
    QString k;
    switch (nAudioLayer) {
    case 4: k = QStringLiteral("tiny"); break;
    case 6: k = QStringLiteral("base"); break;
    case 12: k = QStringLiteral("small"); break;
    case 24: k = QStringLiteral("medium"); break;
    case 32: k = QStringLiteral("large"); break;
    default: k = QStringLiteral("unknown"); break;
    }
    if (nAudioLayer == 32 && nMels == 128)
        k = nTextLayer == 4 ? QStringLiteral("large-v3-turbo") : QStringLiteral("large-v3");
    return k;
}

bool ModelHeader::translates() const { return ok && multilingual() && !(nAudioLayer == 32 && nTextLayer == 4); }

ModelHeader readModelHeader(const QString &file)
{
    ModelHeader h;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        h.error = QObject::tr("The file cannot be read: %1").arg(f.errorString());
        return h;
    }
    const QByteArray d = f.read(48);
    if (d.size() < 48) {
        h.error = QObject::tr("The file is too short to be a model.");
        return h;
    }
    const auto *p = reinterpret_cast<const uchar *>(d.constData());
    if (qFromLittleEndian<quint32>(p) != 0x67676d6cu) {
        h.error = QObject::tr("The file is not a model in whisper.cpp's format (ggml).");
        return h;
    }
    int v[11];
    for (int i = 0; i < 11; ++i)
        v[i] = qFromLittleEndian<qint32>(p + 4 + 4 * i);
    h.nVocab = v[0], h.nAudioCtx = v[1], h.nAudioState = v[2], h.nAudioHead = v[3], h.nAudioLayer = v[4];
    h.nTextCtx = v[5], h.nTextState = v[6], h.nTextHead = v[7], h.nTextLayer = v[8], h.nMels = v[9], h.ftype = v[10];
    if (h.nVocab < 50000 || h.nVocab > 60000 || h.nAudioLayer <= 0 || h.nAudioLayer > 64 || h.nTextLayer <= 0
        || h.nTextLayer > 64 || (h.nMels != 80 && h.nMels != 128)) {
        h.error = QObject::tr("The model's description in the file makes no sense (it may be damaged).");
        return h;
    }
    h.ok = true;
    return h;
}

bool headerMatches(const ModelHeader &h, const CatalogEntry &e, QString *why)
{
    auto fail = [why](const QString &w) {
        if (why)
            *why = w;
        return false;
    };
    if (!h.ok)
        return fail(h.error);
    if (!h.multilingual())
        return fail(QObject::tr("The model knows English only."));
    if (h.nAudioLayer != e.audioLayers || h.nTextLayer != e.textLayers || h.nMels != e.mels)
        return fail(QObject::tr("The file holds the model \"%1\", not \"%2\".").arg(h.kind(), e.id));
    return true;
}

// ------------------------------------------------------------------ languages

QList<Language> languages()
{
    // whisper's languages in its own order (src/whisper.cpp, g_lang), names in English
    static const char *const codes[] = {
        "en", "zh", "de", "es", "ru", "ko", "fr", "ja", "pt", "tr", "pl", "ca", "nl", "ar", "sv", "it", "id", "hi", "fi", "vi",
        "he", "uk", "el", "ms", "cs", "ro", "da", "hu", "ta", "no", "th", "ur", "hr", "bg", "lt", "la", "mi", "ml", "cy", "sk",
        "te", "fa", "lv", "bn", "sr", "az", "sl", "kn", "et", "mk", "br", "eu", "is", "hy", "ne", "mn", "bs", "kk", "sq", "sw",
        "gl", "mr", "pa", "si", "km", "sn", "yo", "so", "af", "oc", "ka", "be", "tg", "sd", "gu", "am", "yi", "lo", "uz", "fo",
        "ht", "ps", "tk", "nn", "mt", "sa", "lb", "my", "bo", "tl", "mg", "as", "tt", "haw", "ln", "ha", "ba", "jw", "su", "yue"};
    QList<Language> out;
    for (const char *c : codes)
        out << Language{QString::fromLatin1(c), languageName(QString::fromLatin1(c))};
    return out;
}

QString languageName(const QString &code)
{
    // a few codes QLocale does not know (or knows by another name)
    if (code == QLatin1String("jw"))
        return QStringLiteral("Javanese");
    if (code == QLatin1String("haw"))
        return QStringLiteral("Hawaiian");
    if (code == QLatin1String("yue"))
        return QStringLiteral("Cantonese");
    if (code == QLatin1String("ht"))
        return QStringLiteral("Haitian Creole");
    if (code == QLatin1String("no"))
        return QStringLiteral("Norwegian");
    if (code == QLatin1String("nn"))
        return QStringLiteral("Norwegian Nynorsk");
    const QLocale l(code);
    if (l.language() == QLocale::C)
        return code;
    return QLocale::languageToString(l.language());
}

LanguageChoice chooseLanguage(const QString &mainLang, const QString &fallbackLang, float pMain, float pFallback,
                              bool modelTranslates)
{
    LanguageChoice c;
    c.language = mainLang;
    if (!fallbackLang.isEmpty() && fallbackLang != mainLang && pFallback > pMain)
        c.language = fallbackLang;
    // Into English: every piece is "translated"; for English speech that is the plain text, and
    // words of the other language inside English sentences come out in English as well.
    c.translate = mainLang == QLatin1String("en") && modelTranslates && !fallbackLang.isEmpty();
    return c;
}

bool canTranslate(const QString &mainLang, const QString &fallbackLang, bool modelTranslates)
{
    return mainLang == QLatin1String("en") && modelTranslates && !fallbackLang.isEmpty() && fallbackLang != mainLang;
}

// ------------------------------------------------------------------ pauses and pieces

static constexpr int kRate = 16000;
static constexpr int kFrame = 320;   // 20 ms
static constexpr int kFrameMs = 20;
static constexpr int kPreroll = 10;  // 200 ms before the first sound
static constexpr int kTail = 10;     // 200 ms of the pause kept after the last sound

Segmenter::Segmenter(const Params &p) : m_p(p) {}

void Segmenter::feed(const float *samples, int n)
{
    int i = 0;
    if (!m_pending.isEmpty()) {
        const int need = kFrame - int(m_pending.size());
        const int take = qMin(need, n);
        m_pending.append(QVector<float>(samples, samples + take));
        i = take;
        if (m_pending.size() == kFrame) {
            frame(m_pending.constData());
            m_pending.clear();
        }
    }
    for (; i + kFrame <= n; i += kFrame)
        frame(samples + i);
    if (i < n)
        m_pending.append(QVector<float>(samples + i, samples + n));
}

void Segmenter::frame(const float *f)
{
    double sum = 0;
    for (int i = 0; i < kFrame; ++i)
        sum += double(f[i]) * f[i];
    const float rms = float(std::sqrt(sum / kFrame));
    const float db = 20.f * std::log10(rms + 1e-9f);
    m_level = qBound(0.f, (db + 60.f) / 60.f, 1.f);
    // the noise floor: falls quickly to quiet frames, rises slowly (2.5 dB a second)
    if (db < m_floor)
        m_floor = m_floor * 0.7f + db * 0.3f;
    else if (!m_inPiece)
        m_floor += qMin(db - m_floor, 0.05f);
    m_floor = qBound(-90.f, m_floor, -20.f);
    static const float kAbove[] = {14.f, 9.f, 6.f};
    static const float kMin[] = {-48.f, -52.f, -58.f};
    const int s = qBound(0, m_p.sensitivity, 2);
    const bool speech = db > m_floor + kAbove[s] && db > kMin[s];

    const QVector<float> frameData(f, f + kFrame);
    if (!m_inPiece) {
        if (speech) {
            m_inPiece = true;
            m_paragraph = m_hadPiece && m_p.paragraphMs > 0 && m_silenceSince >= m_p.paragraphMs;
            m_piece.clear();
            m_frameDb.clear();
            for (const QVector<float> &pre : m_preroll) {
                m_piece += pre;
                m_frameDb << -90.f;
            }
            m_preroll.clear();
            m_piece += frameData;
            m_frameDb << db;
            m_speechFrames = 1;
            m_silenceRun = 0;
        } else {
            m_silenceSince += kFrameMs;
            m_preroll << frameData;
            while (m_preroll.size() > kPreroll)
                m_preroll.removeFirst();
        }
        return;
    }
    m_piece += frameData;
    m_frameDb << db;
    if (speech) {
        m_speechFrames++;
        m_silenceRun = 0;
    } else {
        m_silenceRun++;
    }
    if (m_silenceRun * kFrameMs >= m_p.pauseMs) {
        endPiece(false);
        return;
    }
    if (int(m_frameDb.size()) * kFrameMs >= m_p.maxMs)
        endPiece(true);
}

void Segmenter::endPiece(bool cut)
{
    if (!m_inPiece)
        return;
    QVector<float> rest;
    QVector<float> restDb;
    if (cut) {
        // a long stretch without a pause: cut at the quietest moment of its last 3 s
        const int frames = int(m_frameDb.size());
        int best = frames - 1;
        for (int i = qMax(1, frames - 150); i < frames; ++i)
            if (m_frameDb[i] < m_frameDb[best])
                best = i;
        rest = m_piece.mid(best * kFrame);
        restDb = m_frameDb.mid(best);
        m_piece.resize(best * kFrame);
        m_frameDb.resize(best);
    } else {
        // keep 200 ms of the pause after the last sound
        const int keep = int(m_frameDb.size()) - m_silenceRun + kTail;
        if (keep < m_frameDb.size()) {
            m_piece.resize(qMax(0, keep) * kFrame);
            m_frameDb.resize(qMax(0, keep));
        }
    }
    const int speechMs = m_speechFrames * kFrameMs;
    if (speechMs >= m_p.minSpeechMs && !m_piece.isEmpty()) {
        Piece p;
        p.audio = m_piece;
        p.newParagraph = m_paragraph;
        p.speechMs = speechMs;
        m_done << p;
        m_hadPiece = true;
    }
    m_piece.clear();
    m_frameDb.clear();
    m_paragraph = false;
    if (cut && !rest.isEmpty()) {
        m_piece = rest;
        m_frameDb = restDb;
        m_speechFrames = 0;
        for (float d : restDb)
            if (d > m_floor)
                ++m_speechFrames;
        m_silenceRun = 0;
        m_inPiece = true;
        return;
    }
    m_inPiece = false;
    m_silenceSince = cut ? 0 : m_silenceRun * kFrameMs;
    m_speechFrames = 0;
    m_silenceRun = 0;
}

void Segmenter::flush()
{
    if (!m_pending.isEmpty()) {
        m_pending.resize(kFrame); // zeros to a whole frame
        frame(m_pending.constData());
        m_pending.clear();
    }
    if (m_inPiece) {
        m_silenceRun = 0; // what is there counts
        endPiece(false);
    }
}

QList<Piece> Segmenter::take()
{
    QList<Piece> out;
    out.swap(m_done);
    return out;
}

QVector<float> toMono16k(const float *in, int frames, int channels, int rate)
{
    QVector<float> mono(frames);
    channels = qMax(1, channels);
    for (int i = 0; i < frames; ++i) {
        float s = 0;
        for (int c = 0; c < channels; ++c)
            s += in[i * channels + c];
        mono[i] = s / channels;
    }
    if (rate == kRate || rate <= 0)
        return mono;
    QVector<float> out;
    if (rate % kRate == 0) {
        // 32, 48, 96 kHz: the mean of each group (a simple low-pass before taking every n-th)
        const int n = rate / kRate;
        out.resize(frames / n);
        for (int i = 0; i < out.size(); ++i) {
            float s = 0;
            for (int k = 0; k < n; ++k)
                s += mono[i * n + k];
            out[i] = s / n;
        }
        return out;
    }
    // other rates (44.1 kHz ...): smooth over the step, then interpolate
    const double step = double(rate) / kRate;
    const int win = qMax(1, int(step));
    QVector<float> smooth(frames);
    double acc = 0;
    for (int i = 0; i < frames; ++i) {
        acc += mono[i];
        if (i >= win)
            acc -= mono[i - win];
        smooth[i] = float(acc / qMin(i + 1, win));
    }
    const int n = int(frames / step);
    out.resize(n);
    for (int i = 0; i < n; ++i) {
        const double pos = i * step;
        const int a = int(pos);
        const int b = qMin(a + 1, frames - 1);
        const float t = float(pos - a);
        out[i] = smooth[a] * (1 - t) + smooth[b] * t;
    }
    return out;
}

// ------------------------------------------------------------------ text

QString cleanText(const QString &text, bool guard)
{
    QString t = text;
    // marks for what is no speech: [BLANK_AUDIO], [Music], (Musik), *laughs*
    static const QRegularExpression onlyMark(QStringLiteral("^\\s*(\\[[^\\]]*\\]|\\([^)]*\\)|\\*[^*]*\\*)\\s*$"));
    if (onlyMark.match(t).hasMatch())
        return QString();
    t.remove(QStringLiteral("[BLANK_AUDIO]"));
    t.replace(QRegularExpression(QStringLiteral("\\[(music|musik|applause|applaus|laughter|lachen|noise)\\]"),
                                 QRegularExpression::CaseInsensitiveOption),
              QString());
    t = t.simplified();
    if (guard) {
        // what whisper is known to write for silence or noise (credits of subtitled videos)
        static const char *const invented[] = {"thank you for watching", "thanks for watching",
                                               "untertitel im auftrag des zdf", "untertitelung des zdf",
                                               "untertitel der amara.org-community", "vielen dank fürs zuschauen",
                                               "copyright wdr", "subtitles by the amara.org community"};
        QString key = t.toLower();
        key.remove(QRegularExpression(QStringLiteral("[\\s.,!?;:'\"-]+$")));
        for (const char *phrase : invented)
            if (key.startsWith(QString::fromUtf8(phrase)) && key.size() < int(qstrlen(phrase)) + 30)
                return QString();
    }
    return t;
}

QString joinPiece(const QString &tail, const QString &piece, bool newParagraph)
{
    if (piece.isEmpty())
        return QString();
    if (tail.trimmed().isEmpty())
        return piece;
    if (newParagraph) {
        if (tail.endsWith(QLatin1String("\n\n")))
            return piece;
        return (tail.endsWith(QLatin1Char('\n')) ? QStringLiteral("\n") : QStringLiteral("\n\n")) + piece;
    }
    if (tail.at(tail.size() - 1).isSpace())
        return piece;
    return QLatin1Char(' ') + piece;
}

// ------------------------------------------------------------------ files

QString defaultFolder()
{
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docs.isEmpty())
        docs = QDir::homePath();
    return docs + QStringLiteral("/Quillbinder Transcriptions");
}

QString newTranscriptFile(const QString &folder, const QDateTime &when)
{
    QDir().mkpath(folder);
    const QString base = when.toString(QStringLiteral("yyyy-MM-dd HH-mm"));
    QString path = folder + QLatin1Char('/') + base + QStringLiteral(".txt");
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = folder + QLatin1Char('/') + base + QStringLiteral(" (%1).txt").arg(n);
    return path;
}

QString newRecordingBase(const QString &folder, const QDateTime &when)
{
    QDir().mkpath(folder);
    const QString base = when.toString(QStringLiteral("yyyy-MM-dd HH-mm"));
    QString path = folder + QLatin1Char('/') + base;
    for (int n = 2; QFileInfo::exists(path + QStringLiteral(".txt")) || QFileInfo::exists(path + QStringLiteral(".wav")); ++n)
        path = folder + QLatin1Char('/') + base + QStringLiteral(" (%1)").arg(n);
    return path;
}

bool Transcript::pending() const
{
    return !audio.isEmpty() && !QFileInfo::exists(file);
}

QString partFileFor(const QString &textFile)
{
    return textFile + QStringLiteral(".part");
}

QList<Transcript> transcriptsIn(const QString &folder)
{
    QList<Transcript> out;
    QHash<QString, int> byTitle;
    const QFileInfoList files =
        QDir(folder).entryInfoList({QStringLiteral("*.txt"), QStringLiteral("*.wav")}, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        const QString title = fi.completeBaseName();
        const bool wav = fi.suffix().compare(QLatin1String("wav"), Qt::CaseInsensitive) == 0;
        int i = byTitle.value(title, -1);
        if (i < 0) {
            Transcript t;
            t.title = title;
            t.file = fi.absolutePath() + QLatin1Char('/') + title + QStringLiteral(".txt");
            // the time in the name the file got when it was made ("2026-11-08 05-07"); a renamed file: its last change
            t.created = QDateTime::fromString(t.title.left(16), QStringLiteral("yyyy-MM-dd HH-mm"));
            if (!t.created.isValid())
                t.created = fi.lastModified();
            out << t;
            i = int(out.size()) - 1;
            byTitle.insert(title, i);
        }
        if (wav)
            out[i].audio = fi.absoluteFilePath();
    }
    std::sort(out.begin(), out.end(), [](const Transcript &a, const Transcript &b) {
        if (a.created != b.created)
            return a.created > b.created;
        return a.title > b.title;
    });
    return out;
}

// ---- WavWriter

struct WavWriter::Impl {
    QFile f;
};

WavWriter::~WavWriter()
{
    close();
}

bool WavWriter::open(const QString &file, QString *error)
{
    close();
    d = new Impl;
    d->f.setFileName(file);
    if (!d->f.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
        if (error)
            *error = QObject::tr("The recording cannot be written: %1").arg(d->f.errorString());
        delete d;
        d = nullptr;
        return false;
    }
    m_path = file;
    m_samples = 0;
    m_sinceHeader = 0;
    d->f.write(QByteArray(44, '\0'));
    updateHeader();
    return true;
}

bool WavWriter::isOpen() const { return d && d->f.isOpen(); }

void WavWriter::write(const float *samples, int n)
{
    if (!isOpen() || n <= 0)
        return;
    QByteArray b(n * 2, Qt::Uninitialized);
    auto *p = reinterpret_cast<uchar *>(b.data());
    for (int i = 0; i < n; ++i) {
        const int v = qBound(-32768, int(std::lround(samples[i] * 32767.f)), 32767);
        qToLittleEndian<qint16>(qint16(v), p + 2 * i);
    }
    d->f.write(b);
    m_samples += n;
    m_sinceHeader += n;
    if (m_sinceHeader >= 16000) // about once a second
        updateHeader();
}

void WavWriter::updateHeader()
{
    if (!isOpen())
        return;
    m_sinceHeader = 0;
    const quint32 dataBytes = quint32(qMin<qint64>(m_samples * 2, 0xffffffffLL - 36));
    QByteArray h(44, '\0');
    auto *p = reinterpret_cast<uchar *>(h.data());
    memcpy(p, "RIFF", 4);
    qToLittleEndian<quint32>(36 + dataBytes, p + 4);
    memcpy(p + 8, "WAVEfmt ", 8);
    qToLittleEndian<quint32>(16, p + 16);
    qToLittleEndian<quint16>(1, p + 20);        // PCM
    qToLittleEndian<quint16>(1, p + 22);        // mono
    qToLittleEndian<quint32>(16000, p + 24);
    qToLittleEndian<quint32>(32000, p + 28);    // bytes per second
    qToLittleEndian<quint16>(2, p + 32);
    qToLittleEndian<quint16>(16, p + 34);
    memcpy(p + 36, "data", 4);
    qToLittleEndian<quint32>(dataBytes, p + 40);
    const qint64 at = d->f.pos();
    d->f.seek(0);
    d->f.write(h);
    d->f.seek(at);
    d->f.flush();
}

void WavWriter::close()
{
    if (!d)
        return;
    updateHeader();
    d->f.close();
    delete d;
    d = nullptr;
}

} // namespace speech
