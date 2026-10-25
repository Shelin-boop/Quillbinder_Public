#pragma once
// File > Compare and Transfer (0.18): compares a Word or LibreOffice file
// with what Compile makes of the project, finds which part of the file
// belongs to which document, and the places where the texts differ (words,
// bold, italic, underline), and takes one side's text over to the other:
// into the project (snapshots first) or into a copy of the file (a Word
// file gets tracked changes, a LibreOffice file the changes made directly).
// Only algorithms, no AI: four-word sequences decide what belongs together,
// Myers' difference algorithm finds the changed places.

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

class QTextDocument;

namespace qb {

class Project;
struct Formatting;

namespace compare {

enum FmtBit : quint8 { Bold = 1, Italic = 2, Underline = 4 };

struct Para {
    QString text;                 // the visible text (notes left out)
    QVector<quint8> fmt;          // per character: Bold | Italic | Underline set by hand
    int heading = 0;              // heading level of the paragraph (0: body text)
    int source = -1;              // project: block number; file: paragraph number in the file
    QVector<int> pos;             // project: the document position of each character
    int words() const;
};

// a word or a punctuation mark of a paragraph
struct Token {
    int start = 0, end = 0;       // characters of the paragraph
    QString key;                  // compared form (curly quotes, dashes, ... made plain)
    bool word = false;
    quint8 fmt = 0;
};
QVector<Token> tokenize(const Para &p);
QStringList normalizedWords(const QString &text);   // lower case, for "related?"

struct WordOp {
    enum Type { Replace, Insert, Delete, Format } type = Replace;
    int pStart = 0, pEnd = 0;     // characters of the project's paragraph
    int fStart = 0, fEnd = 0;     // characters of the file's paragraph
};

struct Change {
    enum Kind {
        Changed,        // a paragraph with other words (ops)
        Formatting,     // the same words, other bold / italic / underline (ops of type Format)
        OnlyInProject,  // paragraphs the file does not have
        OnlyInFile      // paragraphs the project does not have
    };
    Kind kind = Changed;
    int pFirst = 0, pCount = 0;   // paragraphs of Pair::project (OnlyInFile: the place, before pFirst)
    int fFirst = 0, fCount = 0;   // paragraphs of Pair::file (OnlyInProject: the place, before fFirst)
    QList<WordOp> ops;
    bool accepted = true;
};

struct Pair {
    enum Kind { Matched, OnlyInFile, OnlyInProject };
    Kind kind = Matched;
    QString docId;                // the binder document (empty: a section only in the file)
    QString title;                // the document's title, or the file's heading of the section
    QString fileTitle;            // the heading the file has for it (as compiled)
    QList<Para> project;          // the document's paragraphs with words
    QList<Para> file;             // the file's paragraphs of this document (without its title)
    QList<int> titleParas;        // the file's heading paragraphs taken as its title
    QList<Change> changes;
    bool included = true;
    bool userSet = false;         // the writer ticked or unticked it
    bool moved = false;           // the file has it in another order than Compile
    int words() const;
    int changedPlaces() const;
};

struct Result {
    QString filePath;
    QString fileKind;             // docx, odt, doc, rtf, ...
    int related = 0;              // % of the file's four-word sequences that the compiled text has
    int covered = 0;              // % of the compiled text's four-word sequences that the file has
    int compiledWords = 0, fileWords = 0;
    QList<Pair> pairs;            // in the order of the file (documents missing from it at their compile place)
    bool canWriteFile = false;    // .docx and .odt can be changed (a copy)
    int changeCount() const;
};

enum class Direction { IntoProject, IntoFile };

// what a row is when nobody ticked it: whole documents are not deleted unless asked
bool defaultIncluded(const Pair &p, Direction d);

// The file as paragraphs (also for the tests): every format the program reads.
class FileText;
std::unique_ptr<FileText> openFile(const QString &path, const Formatting &f, QString *error);

// Reads the file, compiles the project (with the format of the last compile)
// and compares the two. The relatedness (Result::related) is computed before
// anything else; the caller decides what to do below 65 %.
bool compareWithFile(Project *p, const QString &path, Result *out, QString *error);
// the same with a file already read and given documents (tests)
Result compareTexts(Project *p, const QList<QString> &docIds, const QString &compiledText, FileText *file);

// the paragraphs of a text document of the project (notes left out)
QList<Para> projectParagraphs(QTextDocument *doc, const Formatting &f);
// the text Compile makes of the project (plain), and the documents it takes, in order
QString compiledText(Project *p, QStringList *docIds);

struct Report {
    int documentsChanged = 0, documentsAdded = 0, documentsTrashed = 0;
    int places = 0;               // changes taken over
    int snapshots = 0;
    QString writtenFile;          // IntoFile: the copy
    QStringList warnings;
};

// Takes the accepted changes of the included rows over. IntoFile writes
// outPath (a copy of the file, never the file itself).
bool apply(Project *p, Result &r, Direction d, const QString &outPath, Report *report, QString *error);

// "<name> (Quillbinder).docx" beside the file
QString suggestedCopyPath(const QString &filePath);

} // namespace compare
} // namespace qb
