#pragma once
// Interface between the program and importers (version 1.0).
//
// An importer is a Qt plugin (a MODULE library, *.so) in an add-on's plugins/
// folder that implements qb::importers::ImporterInterface. It does one or
// both of:
//   - read files the program cannot read itself, chosen by suffix ("doc"):
//     File > Import > Files, Import and Split, files dropped on the binder,
//     corkboard or outliner;
//   - import web pages: File > Import > Web Page and web links dropped on the
//     binder, corkboard or outliner.
//
// What it reads it hands back as plain structs (Content). The program turns
// them into a document with the project's styles, notes, lists and pictures,
// so an importer needs nothing from the program but this header and Qt, and
// nothing is linked against the program. An importer may instead write a
// file in a format the program reads itself (Content::file), for example a
// .docx made by another program.
//
// All functions are called on the GUI thread. Plugins built for another
// interface version (QB_IMPORTER_IID) are not loaded.

#include <QByteArray>
#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtPlugin>

class QWidget;

namespace qb {
namespace importers {

struct Run {
    QString text;              // may hold '\t' and QChar::LineSeparator (a line break)
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    bool smallCaps = false;
    bool allCaps = false;
    int vertical = 0;          // 1 superscript, -1 subscript
    bool code = false;         // program code (character style "Code Span")
    QString link;              // web address or file link
    // Fonts, sizes and colors are kept in RTF projects only (as in every import).
    QString font;              // empty: the project's font
    double size = 0;           // points; 0: the project's size
    QColor color;              // invalid: automatic
    QColor highlight;          // invalid: none
    int image = -1;            // >= 0: the run is a picture (Content::images), text ignored
    int note = -1;             // >= 0: a footnote or endnote (Content::notes) stands here, text ignored
    int comment = -1;          // >= 0: the text of the run is commented on by a comment (Content::notes)
};

struct Paragraph {
    QList<Run> runs;
    // A style of the program ("Heading 1" … "Heading 6", "Title", "Subtitle",
    // "Block Quote", "Code Block", "Caption", "Attribution", "Centered"), a
    // style of the project by name, or empty (body text).
    QString style;
    int headingLevel = 0;      // 1-9 if the source marks a heading and style is empty
    int align = 0;             // 0 natural, 1 centered, 2 right, 3 justified
    // Paragraph formatting in points; kept in RTF projects only.
    double leftIndent = 0;
    double rightIndent = 0;
    double firstIndent = 0;
    int listLevel = -1;        // 0, 1, …: a list item of that level
    bool numbered = false;     // numbered list (else bullets)
    int listId = 0;            // items of one list share the id
    bool pageBreakBefore = false;
    bool tableRow = false;     // a row of a table: its cells separated by '\t'
};

struct Note {
    enum Kind { Footnote, Endnote, Comment };
    Kind kind = Footnote;
    QString text;              // plain text (paragraphs separated by '\n')
    QString author;            // comments
};

struct Image {
    QByteArray data;           // the file's bytes in a format QImage reads (png, jpg, gif, webp, bmp, svg …)
    double width = 0;          // shown size in points; 0: the picture's own size
    double height = 0;
    QString alt;               // description
};

struct Content {
    QList<Paragraph> paragraphs;
    QList<Note> notes;
    QList<Image> images;
    QString title;             // proposed title of the document (empty: the file name)
    QStringList warnings;      // told to the writer after the import
    // Instead of paragraphs: a file in a format the program reads itself
    // (.rtf .docx .odt .html .md .txt), written into the work folder.
    QString file;
};

// A fetched web page.
struct WebPage {
    Content content;           // text document: the article (or the whole page)
    QString url;               // address of the page (after redirections)
    QString title;             // title for the document and its bookmark
    QString siteName;          // may be empty
    QString author;            // may be empty
    QString published;         // date as given by the page, may be empty
    QString excerpt;           // short description for the synopsis, may be empty
    // A read-only web page document instead of a text document: htmlFile is
    // the page as .html in the work folder, its pictures in files beside it
    // (referred to by relative names). The program copies them.
    bool asWebPage = false;
    QString htmlFile;
};

class ImporterInterface
{
public:
    virtual ~ImporterInterface() = default;
    virtual QString id() const = 0;            // "import-word97"
    virtual QString name() const = 0;          // "Word 97–2003 Import"
    virtual QString version() const = 0;

    // ---- files
    // Lower-case suffixes without dot ("doc", "dot"); empty: no files.
    virtual QStringList suffixes() const = 0;
    virtual QString fileTypeName() const = 0;  // for file dialogs: "Word 97–2003 documents"
    // workDir: an empty folder for files the importer writes (removed by the
    // program afterwards). False with an error text if the file cannot be read.
    virtual bool readFile(const QString &path, const QString &workDir, Content *out, QString *error) = 0;

    // ---- web pages
    virtual bool importsWebPages() const = 0;
    // File > Import > Web Page (interactive: a window asks for the address,
    // shows a preview and the choices; address and title may be given) and
    // web links dropped on the binder (interactive false: no questions, the
    // choices made last time; a progress window may appear). `into` names the
    // folder the page will go into (shown only). Returns when the page is
    // fetched or the writer cancelled (false with an empty error).
    virtual bool importWebPage(QWidget *parent, const QString &address, const QString &title, bool interactive,
                               const QString &into, const QString &workDir, WebPage *out, QString *error) = 0;
};

} // namespace importers
} // namespace qb

#define QB_IMPORTER_IID "org.quillbinder.ImporterInterface/1.0"
Q_DECLARE_INTERFACE(qb::importers::ImporterInterface, QB_IMPORTER_IID)
