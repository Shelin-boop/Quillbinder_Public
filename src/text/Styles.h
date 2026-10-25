#pragma once
// Paragraph and character styles of a project.
//
// A style saves formatting under a name. Text remembers the name of its
// style (block property props::Style for paragraph styles, char property
// props::CharStyle for character styles), so that a changed style changes
// all text that uses it.
//
//  * Paragraph styles hold paragraph formatting (only the attributes named in
//    para.bits; the others follow the project's formatting) and optionally
//    character formatting for the whole paragraph.
//  * Character styles hold character formatting only.
//
// A project without styles of its own (Formatting::styles is null) uses the
// built-in set, styles::defaults().

#include "core/ProjectTypes.h"
#include "text/DirectFormat.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class QDomDocument;
class QDomElement;
class QTextDocument;

namespace qb {

struct Style {
    QString name;
    bool character = false;   // character style
    bool builtin = false;     // one of the styles every project starts with
    int headingLevel = 0;     // Markdown '#' level, outline level when compiled
    bool keepWithNext = false;
    // Paragraph style of the paragraph that follows when Enter is pressed at
    // the end: empty = the same style, "-" = No Style, else a style name.
    QString next;

    // character formatting (false / 0 / empty = the style does not set it)
    bool bold = false, italic = false, underline = false, strike = false;
    int sizeAdjust = 0;       // relative size steps (-2..4) of the built-in headings
    direct::CharSpec chars;   // font family, size, weight, color, letter spacing, capitals

    // paragraph formatting: the attributes named in para.bits
    direct::ParaSpec para;

    bool hasCharFormatting() const
    {
        return bold || italic || underline || strike || sizeAdjust != 0 || !chars.isEmpty();
    }
    bool sameFormatting(const Style &o) const;
    bool operator==(const Style &o) const;
    bool operator!=(const Style &o) const { return !(*this == o); }
};

// Older code and plugins know paragraph styles under this name.
using ParagraphStyle = Style;

struct StyleSheet {
    QList<Style> styles;
    // former names of renamed styles -> current name (snapshots and backups
    // made before the renaming still find their style)
    QHash<QString, QString> aliases;

    const Style *find(const QString &name) const;   // either kind, follows aliases
    int indexOf(const QString &name) const;         // exact name, -1 = none
    QString uniqueName(const QString &wanted) const;
    bool operator==(const StyleSheet &o) const { return styles == o.styles && aliases == o.aliases; }
};

namespace styles {

inline const QString &noStyle()
{
    static const QString s = QStringLiteral("-");
    return s;
}

const StyleSheet &defaults();
const StyleSheet &sheet(const Formatting &f);
// A copy of f that uses the given sheet.
Formatting withSheet(Formatting f, const StyleSheet &sheet);

const Style *find(const Formatting &f, const QString &name);      // paragraph style
const Style *findChar(const Formatting &f, const QString &name);  // character style
const Style *builtinDefault(const QString &name);                 // definition in the built-in set
QStringList names(const Formatting &f);                           // paragraph styles
QStringList charNames(const Formatting &f);                       // character styles
QString forHeadingLevel(int level);                               // "Heading N"
bool isHeadingName(const QString &name);
// Names that have a short form in the Markdown dialect or a fixed role when
// compiling; they can be redefined but not renamed or deleted.
bool isFixedName(const QString &name);
// "¶ ", "¶a " or "a " as shown in front of style names in menus.
QString marker(const Style &s);
// Valid as a style name (the Markdown dialect writes "::Name::" and class="Name").
bool validName(const QString &name);

// ---- formatting -----------------------------------------------------------
// Font size of text with a relative size step.
double adjustedSize(double baseSize, int sizeAdjust);
// Give a char format the character formatting of a style.
void applyChars(QTextCharFormat &cf, const Style &s);
// Take the character formatting of a style away again. Attributes the writer
// changed by hand (other value than the style's) are left alone.
void stripChars(QTextCharFormat &cf, const Style &s);
// The char format without what its paragraph style and character style give
// it: what the writer set by hand.
QTextCharFormat withoutStyles(const QTextCharFormat &cf, const Style *paragraph, const Formatting &f);

struct CaptureOptions {
    bool character = false;      // make a character style
    bool withChars = true;       // paragraph style: include character formatting
    bool withFamily = true;      // include the font
    bool withSize = true;        // include the font size
};
// A style from the formatting of a paragraph and of text in it. Paragraph
// attributes that equal the project's formatting are not stored, so the style
// keeps following the project there.
Style capture(const QString &name, const QTextBlockFormat &bf, const QTextCharFormat &cf, const Formatting &f,
              const CaptureOptions &opt);

// ---- applying -------------------------------------------------------------
// Apply a character style to a range (empty name = remove character styles).
void applyCharacterStyle(QTextDocument *doc, int from, int to, const QString &style, const Formatting &f);
// Name of the character style at a position ("" = none).
QString charStyleAt(const QTextDocument *doc, int position);

struct Changes {
    QHash<QString, QString> renamed;   // old name -> new name
    bool isEmpty() const { return renamed.isEmpty(); }
};
// Bring a document from the styles of oldF to the styles of newF: text keeps
// its style names (renamed ones follow the renaming) and gets the new
// formatting. Text of a style that no longer exists keeps its look as
// formatting set by hand. Returns true if the stored text changes in a
// Markdown project (names changed or styles were removed); in RTF projects
// every change of formatting changes the stored text.
bool restyle(QTextDocument *doc, const Formatting &oldF, const Formatting &newF, const Changes &changes);
// Styles of a document that are used at least once (paragraph and character).
QStringList usedStyles(const QTextDocument *doc);

// ---- storage --------------------------------------------------------------
QDomElement toXml(QDomDocument &dom, const StyleSheet &sheet);
StyleSheet fromXml(const QDomElement &el);
QString toXmlString(const StyleSheet &sheet);
bool fromXmlString(const QString &xml, StyleSheet *sheet);
// The <Styles> element of another project's index file.
bool readFromProjectFile(const QString &path, StyleSheet *sheet, QString *error);

} // namespace styles
} // namespace qb
