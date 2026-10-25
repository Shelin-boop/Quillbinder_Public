#pragma once
// Scripts in the formats of other programs: Fountain (plain text screenplay
// markup, fountain.io) and Final Draft (.fdx, XML).
//
// Both go through one simple model, the Screenplay: paragraphs with a role
// (scene heading, action, character ...) and runs of text. A Screenplay is
// made from a text document of the project (its script elements say the
// roles) or from a compiled manuscript, and turned back into a document
// using the elements of the project's script format.

#include "core/ProjectTypes.h"
#include "text/Script.h"

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

class QTextDocument;

namespace qb {
namespace compile {
struct Manuscript;
}
namespace script {

struct Span {
    QString text;              // "\n" = line break inside the paragraph
    bool bold = false, italic = false, underline = false;
    bool note = false;         // a note of the writer (Fountain [[note]]), not part of the script
};

struct Para {
    Role role = Role::Action;
    QList<Span> spans;
    bool pageBreak = false;    // page break before the paragraph
    int depth = 1;             // Section: level of the heading
    QString text() const;      // without notes
    bool isEmpty() const;
};

struct Screenplay {
    QList<QPair<QString, QString>> titlePage;   // "Title", "Author", "Draft date" ... in order
    QList<Para> paras;
    QString titleValue(const QString &key) const;
};

// ---- Fountain
Screenplay fromFountain(const QString &text);
QString toFountain(const Screenplay &s);

// ---- Final Draft
bool fromFdx(const QByteArray &data, Screenplay *out, QString *error = nullptr);
QByteArray toFdx(const Screenplay &s);

// ---- documents of the project
Screenplay fromDocument(const QTextDocument *doc, const Formatting &f);
// Replaces the content of doc.
void toDocument(const Screenplay &s, QTextDocument *doc, const Formatting &f);
// "Title: ...\nAuthor: ..." (kept in the notes of an imported script)
QString titlePageText(const Screenplay &s);

// ---- compile
Screenplay fromManuscript(const compile::Manuscript &m);

} // namespace script
} // namespace qb
