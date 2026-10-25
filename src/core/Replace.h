#pragma once
// Project Replace: one replacement through titles, texts, notes, synopses,
// custom metadata and the texts of comments and footnotes of a whole project.
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <functional>

class QTextDocument;

namespace qb {

class Project;

struct ReplaceOptions {
    QString find;
    QString with;
    bool caseSensitive = false;
    bool wholeWord = false;
    bool regex = false;          // find is a regular expression; with may use \1 ... \9
    // where
    bool inTitles = true;
    bool inText = true;
    bool inNotes = true;
    bool inSynopses = true;
    bool inCustom = true;        // custom metadata (text fields)
    bool inComments = true;      // texts of inspector comments and footnotes
    QStringList scopeIds;        // empty = the whole project; else these documents and what is inside them
    bool includeTrash = false;
    bool snapshotFirst = false;  // take a snapshot of every text before it changes
    QString snapshotTitle;
};

struct ReplaceResult {
    int titles = 0, texts = 0, notes = 0, synopses = 0, custom = 0, comments = 0; // replacements
    int documents = 0;   // documents with at least one replacement
    int snapshots = 0;
    QString error;       // bad regular expression, or a file that could not be written
    int total() const { return titles + texts + notes + synopses + custom + comments; }
};

namespace replace {
QRegularExpression pattern(const ReplaceOptions &o);
// The text a match is replaced with (\0 ... \9 stand for the match and its groups in a regex search).
QString replacement(const QRegularExpressionMatch &m, const QString &with, bool regex);
// Replace in a string; returns the number of replacements.
int inString(QString *s, const QRegularExpression &re, const QString &with, bool regex, bool countOnly);
// Replace in the text of a document; formatting stays (the new text takes
// the formatting of the first character it replaces). One undo step.
int inDocument(QTextDocument *doc, const QRegularExpression &re, const QString &with, bool regex, bool countOnly);
// Replace in the texts of the comments and footnotes attached to a document.
int inNoteTexts(QTextDocument *doc, const QRegularExpression &re, const QString &with, bool regex, bool countOnly);

// countOnly: nothing is changed, the result says what would be.
// progress(done, total) may return false to stop (what was replaced so far stays).
ReplaceResult run(Project *p, const ReplaceOptions &o, bool countOnly, const std::function<bool(int, int)> &progress = {});
} // namespace replace

} // namespace qb
