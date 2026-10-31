#pragma once
// Fonts that keep several designs under one family name (0.40.1).
//
// Some fonts put several designs of one typeface into one family. Windows 11's
// Sitka, for example, is one variable font file with the optical sizes Small,
// Text, Subheading, Heading, Display and Banner, each with Regular, Semibold
// and Bold; fonts with widths (Condensed, Narrow ...) do the same. Qt 6.4 tells
// the faces of a family apart only by weight, italic and width, so asking for
// "Sitka Text" in bold could give Banner Bold: the text looked smaller and
// narrower when it was made bold.
//
// separate() gives every design a family of its own in Qt's list of fonts
// ("Sitka Small", "Sitka Text", "Sitka Banner" ...) that holds only the faces of
// that design, so Bold and Italic stay within the design everywhere: in the
// font lists, in the text, in printing and in PDF files. The font files are not
// changed or copied: the new families point at the same files and faces. The
// family the font came with keeps the faces of its own design (a family called
// "Sitka Text" keeps the Text faces; a family without a design word in its name
// keeps the faces without one), so texts saved earlier keep their font.
//
// It works on Qt's own font list (private Qt classes of the Qt 6.4.2 the
// program runs on). It runs at every start, also in the self-test that runs
// before a program update is installed, so a crash there stops the update
// before it is installed. QB_KEEP_FONT_FAMILIES=1 switches it off.

#include <QHash>
#include <QString>
#include <QStringList>

namespace qb {
namespace fontfaces {

// What the words of a style name mean: "Banner Bold Italic" is the design
// "Banner", the weight "Bold" and italic.
struct StyleParts {
    QString design;  // words that are neither weight nor slant ("Banner", "SmText", "Condensed"); may be empty
    QString weight;  // the weight words as written ("Semibold"); empty for a regular weight
    bool italic = false;
    // The style name inside the design's own family: "Bold Italic", "Italic", "Regular" ...
    QString styleInDesign() const;
};
StyleParts parseStyle(const QString &styleName);

// Look at every family of Qt's font list and give each design of a family with
// several designs a family of its own. Returns the number of families made.
// Does nothing a second time (the families made are single designs).
int separate();

// separate() now and again whenever Qt reads the fonts anew (fonts added or
// removed while the program runs). Call once after the application object exists.
void install();

// The families separate() made, each with the family it came from.
QHash<QString, QString> separatedFamilies();

} // namespace fontfaces
} // namespace qb
