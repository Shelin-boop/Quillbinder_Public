#pragma once
// ---------------------------------------------------------------------------
// Public API for compile exporters (plugins).
//
// A plugin is a Qt plugin (shared library) that implements
// qb::ExporterInterface. It receives the compiled manuscript as a simple
// document model (paragraphs of formatted runs, footnotes, comments, page
// settings) and writes it to a file.
//
// Build a plugin against this header, put the .so into
//   ~/.local/share/Quillbinder/plugins/
// and it appears in the "Compile for" list. See plugins/markdown-exporter
// for a complete example.
//
// Programs that convert files (pandoc, LibreOffice ...) can be added
// without C++: drop a JSON "command plugin" into the same folder
// (see plugins/command/*.json).
// ---------------------------------------------------------------------------

#include <QColor>
#include <QImage>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSizeF>
#include <QString>
#include <QtPlugin>

namespace qb {
namespace compile {

struct Run {
    enum Kind { Text, FootnoteRef, Image, Tab, LineBreak };
    Kind kind = Text;
    QString text;
    bool bold = false, italic = false, underline = false, strike = false;
    bool superscript = false, subscript = false;
    QString link;          // external URL (empty = none)
    QColor highlight;      // invalid = none
    int footnote = -1;     // FootnoteRef: index into Manuscript::footnotes
    int comment = -1;      // this run lies inside the anchor of comment N
    QImage image;          // Image
    QSizeF imageSize;      // points
    // formatting set by hand in the editor (interface 1.1)
    QString fontFamily;        // empty = font of the manuscript
    double fontSize = 0;       // points; 0 = size of the manuscript / the style
    int fontWeight = 0;        // 100..900 other than normal and bold; 0 = not set
    QColor color;              // invalid = automatic
    double letterSpacingEm = 0; // extra space between letters, in units of the font size
    int capitals = 0;          // 1 = small capitals, 2 = all capitals
    // interface 1.3: the character style of the run (its look is in the
    // members above; the name lets DOCX and EPUB name it)
    QString charStyle;
};

struct TabStop {
    enum Type { Left, Right, Center, Decimal };
    double positionMm = 0;     // from the left edge of the text area
    Type type = Left;
};

struct Paragraph {
    enum Style {
        Normal,
        Title,
        Subtitle,
        Heading1,
        Heading2,
        Heading3,
        BlockQuote,
        Centered,
        Separator,   // scene break line ("#", "* * *")
        Caption,
        Synopsis,    // synopsis / notes included by compile
        ListBullet,
        ListNumber
    };
    Style style = Normal;
    int listLevel = 0;
    int listId = 0;               // paragraphs of one list share an id
    Qt::Alignment align = {};     // 0 = style default
    bool pageBreakBefore = false;
    bool keepWithNext = false;
    bool noIndent = false;        // e.g. first paragraph after a heading
    QList<Run> runs;
    QString sourceId;             // binder item this paragraph came from
    // formatting set by hand in the editor (interface 1.1): the members named
    // in "direct" replace what the style and the manuscript settings say
    enum Direct { DAlign = 1, DLeft = 2, DRight = 4, DFirst = 8, DBefore = 16, DAfter = 32, DLine = 64, DTabs = 128 };
    int direct = 0;
    double leftIndentMm = 0, rightIndentMm = 0;
    double firstLineMm = 0;       // negative = hanging indent
    double spaceBeforePt = 0, spaceAfterPt = 0;
    double lineSpacing = 0;       // multiple of single spacing; 0 = see lineHeightPt
    double lineHeightPt = 0;      // fixed line height (or the minimum if lineAtLeast)
    bool lineAtLeast = false;
    QList<TabStop> tabs;
    // script documents (interface 1.2): what the paragraph is in a script,
    // "scene", "action", "character", "parenthetical", "dialogue",
    // "transition", "shot", "lyrics", "section" (empty = not a script element)
    QString script;
    // interface 1.3
    QString styleName;            // the paragraph style of the text (own styles: named in DOCX and EPUB)
    int outlineLevel = 0;         // 1..6: an entry of the table of contents / PDF bookmarks; 0 = none
    QString tocText;              // the entry's text when it is not the paragraph's (titles of two lines)
    QString anchor;               // a name links can point to (EPUB, PDF)

    QString plainText() const
    {
        QString s;
        for (const Run &r : runs)
            if (r.kind == Run::Text)
                s += r.text;
            else if (r.kind == Run::Tab)
                s += QLatin1Char('\t');
            else if (r.kind == Run::LineBreak)
                s += QLatin1Char('\n');
        return s;
    }
};

struct Footnote {
    QList<Paragraph> paragraphs;
};

struct Comment {
    QString author;
    QString text;
};

struct PageSettings {
    double widthMm = 210, heightMm = 297;
    double marginTopMm = 25.4, marginBottomMm = 25.4, marginLeftMm = 25.4, marginRightMm = 25.4;
    bool mirrorMargins = false;
    // header texts; "<$p>" is replaced by the page number
    QString headerLeft, headerCenter, headerRight;
    bool headerOnFirstPage = false;
    QString footerCenter;          // e.g. "<$p>"
    // interface 1.3
    QString footerLeft, footerRight;
    bool mirrorHeaders = false;      // facing pages: left texts only on left-hand (even) pages, right texts only on right-hand pages
    bool headerOnChapterPages = true; // header and footer on pages where a chapter starts
    bool chaptersOnRightPage = false; // chapters start on an odd page (books)
    int firstPageNumber = 1;
    int pageNumberStyle = 0;         // 0 = 1 2 3, 1 = i ii iii, 2 = I II III
    double headerFontSize = 0;       // 0 = text size minus 2 pt
};

// interface 1.3: settings of the output that are not text
struct OutputOptions {
    // table of contents (PDF, EPUB, DOCX)
    bool toc = false;
    QString tocTitle = QStringLiteral("Contents");
    int tocLevels = 2;
    // notes: 0 = at the bottom of the page (DOCX, PDF; EPUB: pop-up notes at the end of
    // each file), 1 = at the end of each chapter, 2 = at the end of the text
    int footnotePlacement = 0;
    QString endnotesTitle = QStringLiteral("Notes");
    double footnoteSize = 0;         // points; 0 = text size minus 2 pt
    // PDF
    bool pdfBookmarks = true;
    bool pdfLinks = true;
    // e-book
    QImage cover;
    QString publisher, description, subject, identifier, date; // identifier: "" = a new UUID
    bool splitAtPageBreaks = true;
    bool tocPage = false;            // EPUB: a contents page in the text as well (the reader's menu always has one)
};

// interface 1.3: a style of the text with its look, for formats that name styles
struct NamedStyle {
    QString name;
    bool character = false;
    QString fontFamily;
    double fontSize = 0;
    int bold = -1, italic = -1, underline = -1;  // -1 = not part of the style
    QColor color;
    int capitals = 0;
    int align = -1;                  // 0 left, 1 center, 2 right, 3 justified
    double leftIndentMm = -1, rightIndentMm = -1, firstLineMm = -1000;
    double spaceBeforePt = -1, spaceAfterPt = -1;
    double lineSpacing = 0;
};

struct Manuscript {
    QString title;
    QString author;
    QString language = QStringLiteral("en-US");
    QString fontFamily = QStringLiteral("Times New Roman");
    double fontSize = 12;          // points
    double lineSpacing = 2.0;      // multiple of single spacing
    double firstLineIndentMm = 12.7;
    double paragraphSpacingPt = 0; // space after paragraphs
    bool justify = false;
    QString headingFontFamily;     // empty = body font
    double headingScale = 1.0;     // heading size relative to body text
    bool headingBold = false;
    PageSettings page;
    QList<Paragraph> paragraphs;
    QList<Footnote> footnotes;
    QList<Comment> comments;
    QMap<QString, QString> metadata; // free form (keywords, date ...)
    // interface 1.3
    OutputOptions options;
    QList<NamedStyle> styles;        // the styles named by Paragraph::styleName and Run::charStyle
};

} // namespace compile

class ExporterInterface
{
public:
    virtual ~ExporterInterface() = default;
    virtual QString id() const = 0;             // unique, e.g. "docx"
    virtual QString name() const = 0;           // shown in the Compile dialog
    virtual QString fileExtension() const = 0;  // without dot
    virtual bool exportManuscript(const compile::Manuscript &manuscript, const QString &path, QString *error) = 0;
};

} // namespace qb

// 1.1 (program version 0.3.0): fonts, colours and paragraph metrics in Run and
// Paragraph. Plugins built for 1.0 must be rebuilt.
// 1.2 (program version 0.10.0): Paragraph::script. Plugins built for 1.1 must be rebuilt.
// 1.3 (program version 0.14.0): style names, outline levels, OutputOptions, NamedStyle,
// more page settings. Plugins built for 1.2 must be rebuilt.
#define QB_EXPORTER_IID "org.quillbinder.ExporterInterface/1.3"
Q_DECLARE_INTERFACE(qb::ExporterInterface, QB_EXPORTER_IID)
