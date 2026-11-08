#pragma once
// The parts of the add-on "Speech Transcription" that need nothing but Qt Core, so the tests
// can check them directly: the list of models, the model file's header, pauses and pieces,
// the choice of language, cleaning whisper's text, the transcription files.
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

namespace speech {

// ---- models

struct CatalogEntry {
    QString id;            // "medium"
    QString title;         // "Medium (recommended)"
    QString file;          // file name at the source: "ggml-medium-q5_0.bin"
    QString sha1;          // of the file at the source (whisper.cpp's model list)
    qint64 approxBytes = 0;
    int audioLayers = 0, textLayers = 0, mels = 0;   // what the header must say
    bool translates = true;
    QString impact;        // one paragraph for the options
};

QList<CatalogEntry> catalog();
const CatalogEntry *catalogEntry(const QString &id);
const CatalogEntry *catalogEntryBySha1(const QString &sha1);
QString recommendedModel();               // "medium"
// The usable original source: whisper.cpp's model files on Hugging Face.
QString officialBase();                   // https://huggingface.co/ggerganov/whisper.cpp/resolve/main
// Where to fetch a model: the official source, or another address (a folder: the file name is
// added; an address that ends in ".bin": taken as it is).
QString downloadUrl(const CatalogEntry &e, const QString &otherAddress = QString());

struct ModelHeader {
    bool ok = false;
    QString error;
    int nVocab = 0, nAudioCtx = 0, nAudioState = 0, nAudioHead = 0, nAudioLayer = 0;
    int nTextCtx = 0, nTextState = 0, nTextHead = 0, nTextLayer = 0, nMels = 0, ftype = 0;
    bool multilingual() const { return nVocab >= 51865; }
    QString kind() const;              // "tiny", "base", "small", "medium", "large", "large-v3", "large-v3-turbo"
    bool translates() const;           // false for turbo (trained without translation)
};
ModelHeader readModelHeader(const QString &file);
// The header matches a catalog entry (the right model behind the name).
bool headerMatches(const ModelHeader &h, const CatalogEntry &e, QString *why = nullptr);

// ---- languages (whisper's codes)

struct Language {
    QString code;   // "en"
    QString name;   // "English"
};
QList<Language> languages();
QString languageName(const QString &code);

// What whisper is asked to do with a piece: the language it is spoken in and whether to
// translate it. Main is the language of the text; fallback the other language the writer
// speaks (empty: none). pMain, pFallback: whisper's probabilities of the two languages for the
// piece. Whisper translates only into English: with another main language, or a model that
// cannot translate, the piece stays in the language it was spoken in.
struct LanguageChoice {
    QString language;
    bool translate = false;
};
LanguageChoice chooseLanguage(const QString &mainLang, const QString &fallbackLang, float pMain, float pFallback,
                              bool modelTranslates);
// Whether a translation is possible at all with these settings (for the options' note).
bool canTranslate(const QString &mainLang, const QString &fallbackLang, bool modelTranslates);

// ---- pauses and pieces (16 kHz mono)

struct Piece {
    QVector<float> audio;
    bool newParagraph = false;  // a long pause came before it
    int speechMs = 0;
};

class Segmenter
{
public:
    struct Params {
        int sensitivity = 1;      // 0 low, 1 medium, 2 high
        int pauseMs = 700;
        int paragraphMs = 3000;   // 0: never
        int maxMs = 25000;        // whisper works on 30 s windows
        int minSpeechMs = 250;    // shorter sounds are no piece
    };
    explicit Segmenter(const Params &p);
    void feed(const float *samples, int n);
    void flush();                 // ends the piece being heard
    QList<Piece> take();          // pieces finished since the last call
    float level() const { return m_level; }   // 0..1 of the last 20 ms
    bool inSpeech() const { return m_inPiece; }
    float noiseFloorDb() const { return m_floor; }

private:
    void frame(const float *f);
    void endPiece(bool cut);
    Params m_p;
    QVector<float> m_pending;     // samples not yet a whole frame
    QList<QVector<float>> m_preroll;
    QVector<float> m_piece;
    QVector<float> m_frameDb;     // dB of each frame of the piece
    bool m_inPiece = false;
    bool m_hadPiece = false;
    bool m_paragraph = false;
    int m_speechFrames = 0, m_silenceRun = 0, m_silenceSince = 0;
    float m_floor = -60.f;
    float m_level = 0.f;
    QList<Piece> m_done;
};

// Audio in any format to 16 kHz mono float (the microphone's own rate and channels).
// `rate`: samples per second of the input; `channels`: interleaved channels.
QVector<float> toMono16k(const float *interleaved, int frames, int channels, int rate);

// ---- text

// Whisper's text of a piece made clean: non-speech marks ("[BLANK_AUDIO]", "(music)") out,
// spaces tidied; with `guard`, the phrases whisper is known to invent in silence are left out.
QString cleanText(const QString &text, bool guard);
// What to add to a text that ends with `tail` for a new piece.
QString joinPiece(const QString &tail, const QString &piece, bool newParagraph);

// ---- transcription files

QString defaultFolder();                                   // ~/Documents/Quillbinder Transcriptions
QString newTranscriptFile(const QString &folder, const QDateTime &when);   // "2026-11-08 05-07.txt", " (2)" ...
struct Transcript {
    QString file;
    QString title;          // file name without .txt
    QDateTime created;
};
QList<Transcript> transcriptsIn(const QString &folder);   // newest first

} // namespace speech
