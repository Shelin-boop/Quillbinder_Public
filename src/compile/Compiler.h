#pragma once
#include "compile/CompileFormat.h"
#include "compile/ExporterApi.h"
#include "core/ProjectTypes.h"

#include <QMap>
#include <QString>

class QTextDocument;

namespace qb {

class Project;
class BinderItem;

namespace compile {

// The author's address, phone and e-mail (File > Options > General > Author
// Information, 0.21) for the placeholders <$street>, <$city>, <$state>, <$zip>,
// <$country>, <$phone>, <$email> and <$address>; keys as these names.
void setAuthorContact(const QMap<QString, QString> &contact);
QMap<QString, QString> authorContact();

struct AuthorInfo {
    QString forename;
    QString surname;
    QString fullName() const { return (forename + QLatin1Char(' ') + surname).trimmed(); }
};

// Replaces <$...> placeholders. Counters (<$n>, <$w:chapter> ...) advance
// every time they are used.
class Placeholders
{
public:
    QString projectTitle, author, forename, surname;
    QString sectionTitle;
    int wordCount = 0;
    QString apply(const QString &s);
    void reset() { m_counters.clear(); }
    static QString numberToWords(int n);
    static QString toRoman(int n);

private:
    QMap<QString, int> m_counters;
};

struct CompileEntry {
    BinderItem *item = nullptr;
    int level = 0;
    QString sectionType;
    bool included = true;
    enum Part { Front, Main, Back } part = Main;
};

// File > Print Current Document: the documents an editor shows, in the look
// of the editor.
struct PrintPart {
    BinderItem *item = nullptr;
    int level = 1;                    // 1 = the top level: the size of its title
    QTextDocument *doc = nullptr;     // the text to print (the document's own, or a selection of it)
};
struct PrintOptions {
    bool titles = true;               // the document's title above its text
    bool synopses = false;
    bool notes = false;               // the document notes after the text
};

class Compiler
{
public:
    Compiler(Project *project, const Format &format, const CompileProjectSettings &settings, const AuthorInfo &author);

    QList<CompileEntry> entries() const;
    QString layoutNameFor(const CompileEntry &e) const;
    Manuscript build();
    // The texts of these documents with every style as it looks in the editor
    // (the compile format gives notes, highlights and the page).
    Manuscript buildPrint(const QList<PrintPart> &parts, const PrintOptions &options);

    // Convert one text document (used by build and by tests)
    void appendDocument(Manuscript &m, QTextDocument *doc, const QString &sourceId, bool noIndentFirst,
                        bool pageBreakFirst, Paragraph::Style forceStyle = Paragraph::Normal);

private:
    Project *m_project;
    Format m_format;
    CompileProjectSettings m_settings;
    AuthorInfo m_author;
    Placeholders m_ph;
    int m_listCounter = 0;
    int m_docBaseLevel = 0;   // outline level of the headings inside the document being added, minus one
    int m_anchorCounter = 0;
    bool m_editorLook = false; // buildPrint: styles keep their editor look
    void finish(Manuscript &m);
    // File > Compile, "Add object sheets as back matter" (0.21): a heading per
    // category of the Objects sheet, a section per object with its fields.
    void appendObjectSheets(Manuscript &m);
    BinderItem *compileRoot() const;
    void collect(BinderItem *item, int level, CompileEntry::Part part, QList<CompileEntry> &out,
                 const BinderItem *root) const;
};

// Comments and inline annotations (Manuscript::comments) become notes at the
// bottom of the page: a note mark after the last word of each comment, the
// comment's text (after `prefix`) in italics.
void commentsAsFootnotes(Manuscript &m, const QString &prefix);

} // namespace compile
} // namespace qb
