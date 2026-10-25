#pragma once
// Mind maps (0.17): a board of notes per binder item, after Scapple.
//
// A board holds notes (text or a picture), background shapes and the lines
// between them. It is stored as <project>/content/<item id>/mindmap.scap, in
// the XML layout of Scapple files (ScappleDocument / Notes / Note with
// Position, Width, Appearance, String, ConnectedNoteIDs, PointsToNoteIDs;
// BackgroundShapes; NoteStyles; UISettings). That layout is known from a
// public reader of Scapple files (GitHub binaryloader/mcp-scapple), not from
// Scapple itself; what Scapple's files may hold beyond it (formatting ranges,
// pictures, labels of lines, fading, shadows, magnetic shapes) is written in
// elements of this program's choice (see MindMap.cpp) and read back by it.
//
// The model has no widgets: the scene (ui/MindMapScene) shows it, edits go
// through it, and undo restores whole states (toXml / fromXml).

#include <QColor>
#include <QHash>
#include <QImage>
#include <QList>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QStringList>

class QTextDocument;

namespace qb {
namespace mindmap {

enum class BorderStyle { Rounded, Square, Jagged, Cloud };
enum class Align { Left, Center, Right, Justify };

// Icons on a note or background shape (0.21): up to eight, one at each
// corner and one in the middle of each side, chosen from the binder's icons
// (Documents > Change Icon). The positions go round clockwise from the
// top-left corner. The value of each entry is an icon id as the binder uses
// it (see core/IconCatalog.h), so a note can carry "Red Flag" like a document.
enum class IconPos { TopLeft = 0, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };
constexpr int kIconPositions = 8;
using IconMap = QMap<int, QString>;   // IconPos as int -> icon id

// The look of one line between two items (0.21): its color (invalid = the
// board's ink) and an icon drawn in the middle of the line. The arrow heads
// are not part of it: they live in Note::pointsTo / Shape::pointsTo.
struct LineLook {
    QColor color;
    QString icon;
    bool isEmpty() const { return !color.isValid() && icon.isEmpty(); }
    bool operator==(const LineLook &o) const { return color == o.color && icon == o.icon; }
};

struct Appearance {
    QColor fill;                 // invalid: none
    QColor textColor;            // invalid: the board's text color
    QColor borderColor;          // invalid: the board's text color
    int borderWeight = 0;        // 0 = no border, 1 ... 5
    BorderStyle borderStyle = BorderStyle::Rounded;
    bool shadow = false;
    bool faded = false;
    bool operator==(const Appearance &o) const;
};

// Bold, italic ... of a part of a note's text (or a link).
struct TextRange {
    int start = 0, length = 0;
    bool bold = false, italic = false, underline = false, strike = false;
    QString link;
    bool operator==(const TextRange &o) const;
};

struct Note {
    int id = 0;
    QPointF pos;                 // top-left corner on the board
    double width = 160;          // text notes: the height follows the text
    double height = 0;           // pictures only (text notes: computed, see layoutHeight)
    double fontSize = 12;
    QString text;
    QList<TextRange> ranges;     // inline formatting
    bool bold = false, italic = false, underline = false, strike = false; // the whole note
    Align align = Align::Left;
    int indent = 0;              // stack indentation in steps of 1 cm (Tab / Shift+Tab)
    Appearance look;
    QImage image;                // a picture note when not null
    QSet<int> connected;         // lines (both ends list each other)
    QSet<int> pointsTo;          // arrow heads at the other end
    IconMap icons;               // icons at the corners and sides (0.21)
    bool isImage() const { return !image.isNull(); }
};

struct Shape {
    int id = 0;
    QRectF rect;
    Appearance look;
    bool magnetic = true;
    QSet<int> connected;
    QSet<int> pointsTo;
    IconMap icons;               // icons at the corners and sides (0.21)
};

struct NoteStyle {
    QString name;
    QString id;
    // what the style sets (Scapple's "Include ..." options)
    bool affectsFont = false;      // size, bold, italic, underline, strike
    bool affectsAlignment = false; // alignment and indent
    bool affectsTextColor = false;
    bool affectsBody = true;       // fill and border
    bool affectsSize = false;      // width (shapes: width and height)
    bool affectsFade = false;
    bool affectsMagnetic = false;
    double fontSize = 12;
    bool bold = false, italic = false, underline = false, strike = false;
    Align align = Align::Left;
    int indent = 0;
    Appearance look;
    double width = 0, height = 0;
    bool magnetic = false;
};

struct Settings {
    // The color the board shows (worked out by MindMapDoc: the board's own color for
    // the kind of theme in use, else the default of that kind; 0.21.1).
    QColor background = QColor(253, 251, 240); // Scapple's paper: off-white
    // Background colors the writer chose for this board (0.21.1): one for light themes,
    // one for dark themes; invalid: the default of File > Options > Extras > Mind Map,
    // followed when it changes.
    QColor ownLight, ownDark;
    // not stored: the default for light themes (written as BackgroundColor for older
    // versions and Scapple, which know one color only)
    QColor defaultLight;
    // not stored: the only color of a file written before 0.21.1 (MindMapDoc decides
    // whether it was the writer's own or the default of that time)
    QColor legacyBackground;
    QImage texture;                            // tiled over the background when set
    QColor textColor;                          // invalid: black or white, whichever fits the background
    QString font;                              // empty: the program's interface font
    bool rightToLeft = false;
    bool locked = false;                       // File > Prevent Editing
    bool footer = true;
    bool pageGuides = false;
    QColor effectiveTextColor() const;
};

class Board
{
public:
    Board();

    QList<Note> notes;      // back to front
    QList<Shape> shapes;    // back to front (always below the notes)
    QList<NoteStyle> styles;
    Settings settings;
    QString uuid;           // ScappleDocument ID
    // labels of lines: key "a-b" with a < b
    QHash<QString, QString> lineLabels;
    // color and middle icon of lines (0.21), same keys as lineLabels
    QHash<QString, LineLook> lineLooks;

    bool isEmpty() const { return notes.isEmpty() && shapes.isEmpty(); }
    // worth a file: something on it, or a look of its own (a background color, 0.21.1)
    bool worthSaving() const
    {
        return !isEmpty() || settings.ownLight.isValid() || settings.ownDark.isValid() || !settings.texture.isNull();
    }
    int nextId() const;
    Note *note(int id);
    const Note *note(int id) const;
    Shape *shape(int id);
    const Shape *shape(int id) const;
    bool exists(int id) const { return note(id) || shape(id); }
    QRectF rectOf(int id) const;       // notes: their laid out height
    QRectF itemsRect() const;          // everything on the board

    // lines between two items (notes or shapes)
    bool isConnected(int a, int b) const;
    bool pointsTo(int a, int b) const;
    void connect(int a, int b, bool arrowToB = false, bool arrowToA = false);
    void disconnect(int a, int b);
    void removeArrows(int a, int b);
    void setArrow(int from, int to, bool on);   // the arrow head at "to" of an existing line
    // Both arrow heads of a line at once (0.21, right-click on a line):
    // toB = a head at b's end, toA = a head at a's end.
    void setArrows(int a, int b, bool toB, bool toA);
    LineLook lineLook(int a, int b) const;
    void setLineLook(int a, int b, const LineLook &look);
    // the icons of a note or shape (empty for an unknown id)
    IconMap iconsOf(int id) const;
    void setIcon(int id, int position, const QString &iconId); // empty icon id: removes it
    QSet<int> connectionsOf(int id) const;
    QString lineLabel(int a, int b) const;
    void setLineLabel(int a, int b, const QString &label);
    static QString lineKey(int a, int b);

    void remove(int id);               // a note or shape and its lines
    void bringToFront(const QList<int> &ids);
    void sendToBack(const QList<int> &ids);

    // the height a text note needs at its width (laid out with Qt's text layout)
    double noteHeight(const Note &n) const;
    // the note as a text document (font, size, formatting ranges, alignment, indent)
    void fillDocument(QTextDocument *doc, const Note &n) const;
    // reads text and formatting ranges back from an edited document
    static void readDocument(const QTextDocument *doc, Note &n);
    QString fontFamily() const;
    // the note's width so that no line wraps (Notes > Auto-Fit)
    double fittingWidth(const Note &n) const;

    // the notes stacked below a note (touching it, same left edge), top to bottom
    QList<int> stackBelow(int id) const;
    int stackAbove(int id) const;      // the note this one hangs under, or 0
    // after a note changed its height: the notes stacked below it follow
    void restack(int id, double oldBottom);

    // Notes in a reading order (Scapple's rules for text exports): left to
    // right and top to bottom (right to left for that writing direction),
    // stacks together, connected notes after the note they hang on, nearest
    // first; each note once. Only the given ids when not empty.
    QList<int> readingOrder(const QSet<int> &only = {}) const;
    // the shapes a note or shape lies on (overlaps), topmost last
    QList<int> shapesUnder(int id) const;
    QList<int> overlapping(int shapeId) const;

    // export
    QString toPlainText(const QSet<int> &only, const QString &separator) const;
    QString toPlainList(const QSet<int> &only) const;
    QByteArray toOpml(const QSet<int> &only, bool notesField, const QString &title) const;

    // file
    QByteArray toXml() const;
    bool fromXml(const QByteArray &xml, QString *error = nullptr);
    bool save(const QString &path, QString *error = nullptr) const; // removes the file when the board is empty
    bool load(const QString &path, QString *error = nullptr);

    static QList<NoteStyle> defaultStyles();
};

// file names: inside a project's content folder, inside a .scriv's data folder
QString fileName();          // mindmap.scap
QString scrivenerFileName(); // quillbinder-mindmap.scap
QString pathFor(const QString &contentDir);

// One unit of note padding and the standard note width (as Scapple: 8 px)
constexpr double kPadX = 8.0;
constexpr double kPadY = 5.0;
constexpr double kIndentStep = 37.8; // 1 cm at 96 dpi

// importing text: one note, or one note per part between separators
QStringList splitText(const QString &text, const QString &separator);

} // namespace mindmap
} // namespace qb
