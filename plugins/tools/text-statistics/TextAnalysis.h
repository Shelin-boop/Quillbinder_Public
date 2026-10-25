#pragma once
// Counting for the writing tool "Text Statistics" (no widgets).
//
// Readability formulas (all need sentences, words and syllables; syllables
// are counted by a rule of thumb, so the scores are estimates):
//   Flesch Reading Ease (English, Flesch 1948): 206.835 - 1.015 * W/S - 84.6 * Sy/W
//   Flesch-Kincaid Grade (English, Kincaid et al. 1975): 0.39 * W/S + 11.8 * Sy/W - 15.59
//   Amstad (German Flesch, 1978): 180 - W/S - 58.5 * Sy/W
//   Wiener Sachtextformel 1 (German, Bamberger & Vanecek 1984):
//       0.1935 * MS + 0.1672 * SL + 0.1297 * IW - 0.0327 * ES - 0.875
//       MS: % words with 3+ syllables, SL: words per sentence, IW: % words with more than 6 letters,
//       ES: % words with one syllable
//   LIX (any language, Bjoernsson 1968): W/S + 100 * (words with more than 6 letters) / W

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <functional>

namespace textstats {

enum class Language { Auto = 0, English, German, Other };

struct Input {
    QString id, title, path, text;
};

struct DocStats {
    QString id, title, path;
    int words = 0, chars = 0, sentences = 0;
};

struct Result {
    Language language = Language::Other;  // the one used (Auto resolved)
    int words = 0;            // the program's own count
    int tokens = 0;           // words as found here (frequencies, averages)
    int chars = 0, charsNoSpaces = 0;
    int paragraphs = 0, sentences = 0;
    int letters = 0;
    int syllables = 0, monosyllables = 0, polysyllables = 0, longWords = 0;
    int longestSentence = 0;
    QString longestSentenceText;
    int uniqueWords = 0;
    QHash<QString, int> counts;           // lower case -> count
    QHash<QString, QString> shown;        // lower case -> most frequent spelling
    QHash<QString, int> exact;            // spelling as written -> count
    QList<DocStats> docs;
    QStringList sentenceTokens;           // not kept (memory); see phrases()
    // readability; NaN when there is too little text
    double fleschEase = 0, fleschKincaid = 0, amstad = 0, wstf = 0, lix = 0;
    bool hasReadability = false;
};

Result analyze(const QList<Input> &inputs, Language lang, const std::function<int(const QString &)> &countWords);

QStringList tokenize(const QString &text);           // words in order
QStringList sentences(const QString &paragraph);     // a paragraph cut into sentences
int syllables(const QString &word, Language lang);   // rule of thumb, at least 1
Language detect(const QString &sample);
const QStringList &commonWords(Language lang);        // function words to hide in lists
bool isCommon(const QString &lowerWord, Language lang);

// Words with their counts, most frequent first.
QList<QPair<QString, int>> frequency(const Result &r, bool hideCommon, int minLength, bool caseSensitive);

// Repeated phrases of 2..maxWords words inside sentences, most frequent first.
// Phrases of common words only are left out, as are shorter phrases that only
// ever occur inside a longer one.
QList<QPair<QString, int>> phrases(const QList<Input> &inputs, Language lang, int minCount, int maxWords, int limit = 500);

QString ease(double fleschEase);     // "fairly easy" ... (Flesch's bands)

} // namespace textstats
