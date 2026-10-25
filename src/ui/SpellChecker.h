#pragma once
#include <QSet>
#include <QStringList>
#include <QSyntaxHighlighter>

class QTextDocument;

namespace qb {

// Hunspell based spell checking (dictionaries from /usr/share/hunspell).
class SpellChecker
{
public:
    static SpellChecker &instance();
    bool isAvailable() const { return m_handle != nullptr; }
    bool setLanguage(const QString &lang);
    QString language() const { return m_language; }
    QStringList availableLanguages() const;
    bool isCorrect(const QString &word);
    QStringList suggestions(const QString &word);
    void addToPersonal(const QString &word);
    // The personal word list (File > Options > Corrections > Spelling, 0.21)
    QStringList personalWords() const;
    void setPersonalWords(const QStringList &words);
    void ignore(const QString &word);
    // the program's name and its own terms (outliner, copyholder ...): always correct
    static const QStringList &programWords();

    // attach a highlighter to the document (once)
    static void attach(QTextDocument *doc);
    static void setEnabled(bool on);
    static bool isEnabled();
    // How the texts are shown (File > Options > Corrections > Spelling and Editing, 0.21);
    // display only, nothing of it is stored in the texts.
    struct Display {
        bool ignoreNumbers = true;        // words with digits are not checked
        int underline = 0;                // 0 waves, 1 dots, 2 straight line
        bool plainAnnotations = false;    // inline annotations in the text color (the tint stays)
    };
    static void setDisplay(const Display &d);
    static Display display();
    static void rehighlightAll();

private:
    SpellChecker();
    ~SpellChecker();
    void *m_handle = nullptr;
    QString m_language;
    QSet<QString> m_personal;
    QSet<QString> m_ignored;
    QHash<QString, bool> m_cache;
    QString personalFile() const;
};

class SpellHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit SpellHighlighter(QTextDocument *doc);

protected:
    void highlightBlock(const QString &text) override;
    bool eventFilter(QObject *o, QEvent *e) override;
};

} // namespace qb
