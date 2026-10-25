#pragma once
#include "compile/ExporterApi.h"

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>

namespace qb {
namespace compile {

struct SectionLayout {
    QString name;
    bool includeTitle = false;
    bool showItemTitle = true;    // false: only prefix/suffix (e.g. "Chapter One")
    QString titlePrefix;          // may contain placeholders and "\n"
    QString titleSuffix;
    bool titleUppercase = false;
    Paragraph::Style titleStyle = Paragraph::Heading1;
    Qt::Alignment titleAlign = Qt::AlignHCenter;
    bool includeText = true;
    bool includeSynopsis = false;
    bool includeNotes = false;
    bool pageBreakBefore = false;
    // separator inserted before this section when it follows text:
    // "none", "blank", "page" or any literal text (e.g. "#")
    QString separator = QStringLiteral("none");
    bool noIndentFirstParagraph = false;
    int blankLinesBeforeTitle = 0;
};

// How a style of the text looks in a compile format (Format > Styles):
// as in the editor, a look of its own, like body text, or left out.
struct StyleOverride {
    enum Mode { Keep = 0, Restyle = 1, AsBody = 2, Remove = 3 };
    QString style;               // name of a paragraph or character style
    Mode mode = Restyle;
    // Restyle: what is set (the rest stays as the style has it)
    QString fontFamily;
    double fontSize = 0;         // points; 0 = unchanged
    int bold = -1, italic = -1, underline = -1; // -1 unchanged, 0 off, 1 on
    int capitals = -1;           // -1 unchanged, 0 none, 1 small, 2 all
    QColor color;                // invalid = unchanged
    int align = -1;              // -1 unchanged, 0 left, 1 center, 2 right, 3 justified
    double leftIndentMm = -1, rightIndentMm = -1, firstLineMm = -1000;
    double spaceBeforePt = -1, spaceAfterPt = -1;
    double lineSpacing = 0;      // multiple; 0 = unchanged
    QString prefix, suffix;      // text before and after (placeholders work)
    bool uppercase = false;
    bool pageBreakBefore = false; // paragraph styles
};

// Format > Replacements: text replaced in the compiled text only.
struct Replacement {
    QString find, replace;
    bool regex = false;          // \1 ... \9 in "replace"
    bool matchCase = true;
    bool wholeWords = false;
    bool enabled = true;
};

// Format > Transformations
struct Transformations {
    bool straightQuotes = false;     // “ ” ‘ ’ -> " '
    bool smartQuotes = false;        // " ' -> “ ” ‘ ’ (English)
    bool dashesToHyphens = false;    // — -> --, – -> -
    bool italicsToUnderline = false; // manuscript convention
    bool collapseSpaces = false;     // several spaces -> one
    bool removeTrailingSpaces = true;
    bool plainText = false;          // no character formatting at all
};

struct Format {
    QString name;
    bool builtin = false;
    bool useEditorFormatting = false; // "Default": keep the project's editor look
    QString fontFamily = QStringLiteral("Times New Roman");
    QString headingFontFamily;
    double fontSize = 12;
    double lineSpacing = 2.0;
    double firstLineIndentMm = 12.7;
    double paragraphSpacingPt = 0;
    bool justify = false;
    double headingScale = 1.0;        // heading size relative to body text
    bool headingBold = false;
    PageSettings page;
    QList<SectionLayout> layouts;
    QMap<QString, QString> defaultAssignments; // section type name (lower case) -> layout name
    // notes
    bool keepAnnotationsAsComments = false;    // otherwise inline annotations are removed
    bool keepComments = true;                  // linked comments -> document comments
    bool removeHighlights = true;
    // 0.14 (milestone M13)
    QList<StyleOverride> styleOverrides;
    QList<Replacement> replacements;
    Transformations transform;
    bool toc = false;                          // table of contents (PDF, EPUB, DOCX)
    QString tocTitle = QStringLiteral("Contents");
    int tocLevels = 2;
    int footnotePlacement = 0;                 // 0 page bottom, 1 end of chapter, 2 end of text
    QString endnotesTitle = QStringLiteral("Notes");
    double footnoteSize = 0;
    bool pdfBookmarks = true;
    bool pdfLinks = true;
    bool splitAtPageBreaks = true;             // EPUB: a file per section that starts on a new page
    bool tocPage = false;                      // EPUB: contents page in the text

    const StyleOverride *styleOverride(const QString &style) const;

    const SectionLayout *layout(const QString &name) const;
    QJsonObject toJson() const;
    static Format fromJson(const QJsonObject &o);
};

QList<Format> builtinFormats();
QList<Format> userFormats();
QList<Format> addonFormats();
QList<Format> allFormats();
Format formatByName(const QString &name);
bool saveUserFormat(const Format &f, QString *error);
// a format as a file of its own (.qbformat, JSON), to share it
bool exportFormat(const Format &f, const QString &path, QString *error);
bool importFormat(const QString &path, Format *f, QString *error);
bool deleteUserFormat(const QString &name);
QString userFormatsDir();

// Paper presets (name, width mm, height mm)
struct PaperSize {
    QString name;
    double w, h;
};
QList<PaperSize> paperSizes();

} // namespace compile
} // namespace qb
