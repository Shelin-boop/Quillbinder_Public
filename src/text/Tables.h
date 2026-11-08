#pragma once
// Tables and figures in the text (0.62).
//
// A table is a QTextTable whose design is kept as tables::Spec (JSON in the table format's
// property props::TableSpec): ready-made styles (grid, lines, header, striped, none) or the
// writer's own values; tables::apply() turns the design into Qt's table and cell formats again
// after every change (rows added keep the stripes). A cell's own background is props::CellBackground.
//
// A figure is a picture with its caption: a QTextFrame (props::Figure = its position) holding
// the picture's paragraph and the caption's paragraphs (style "Caption"). Left and right: the
// frame floats and the text flows around it; centered: the frame stands on its own lines.
// A picture in the line (no frame) has no caption. What a picture shows (its description, the
// alt text of e-books and Word) is props::ImageDescription of the picture's character.
#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTextFormat>
#include <QTextFrameFormat>

class QTextCursor;
class QTextDocument;
class QTextFrame;
class QTextTable;

namespace qb {

struct Formatting;

namespace tables {

struct Spec {
    QString style = QStringLiteral("grid"); // the ready-made style it came from ("custom" after own changes)
    double border = 1;                      // px; 0 = no lines
    QColor borderColor = QColor(0x80, 0x80, 0x80);
    bool linesOnly = false;                 // only the lines between the rows (no lines left and right)
    double padding = 4;                     // px inside the cells
    double spacing = 0;                     // px between the cells; 0 = single lines
    int headerRows = 1;                     // repeated on every page in print and compile
    QColor headerColor;                     // background of the header rows (invalid = none)
    QColor stripeColor;                     // every other row below the header (invalid = none)
    int widthPercent = 100;                 // of the text column; 0 = as wide as the contents
    Qt::Alignment align = Qt::AlignLeft;    // left, centered, right (in the text)
    int flow = 0;                           // 0 in the text, 1 left with text beside it, 2 right
    QList<double> columns;                  // share of each column in percent; empty = equal

    QJsonObject toJson() const;
    static Spec fromJson(const QJsonObject &o);
    bool operator==(const Spec &o) const { return toJson() == o.toJson(); }
};

// The ready-made styles: id and title ("grid" "Grid", ...).
QList<std::pair<QString, QString>> styles();
Spec preset(const QString &id);

Spec specOf(const QTextTable *t);
// The design onto the table and its cells (own cell backgrounds stay).
void apply(QTextTable *t, const Spec &s);
// A new table at the cursor; the cells take the paragraph format given (no first-line indent).
QTextTable *insert(QTextCursor &c, int rows, int columns, const Spec &s, const QTextBlockFormat &cellBlock,
                   const QTextCharFormat &cellChar);
// A cell's own background (invalid: the design's again).
void setCellBackground(QTextTable *t, int row, int column, const QColor &c);

} // namespace tables

namespace figures {

enum Position { InLine = 0, Left = 1, Right = 2, Centered = 3 };

bool isFigure(const QTextFrame *f);
// The figure the position lies in (nullptr: none).
QTextFrame *at(QTextDocument *doc, int position);
Position positionOf(const QTextFrame *f);
// The position of the picture character of a figure (-1: none).
int imagePosition(const QTextFrame *f);
QString caption(const QTextFrame *f);

// The picture at imagePos gets the position (and caption; "" = none). A picture in the line
// becomes a figure (its paragraph is split around it), a figure set to InLine becomes a picture
// in a paragraph of its own again (the caption is dropped). Returns the picture's new position.
int setLayout(QTextDocument *doc, int imagePos, Position p, const QString &caption, const Formatting &f);

QString description(const QTextFormat &imageFormat);
void setDescription(QTextDocument *doc, int imagePos, const QString &text);

// The width a figure takes: the picture's width (px) plus its margins.
double frameWidth(double imageWidth);
// The frame of a figure at that position, for a picture of that width (px).
QTextFrameFormat frameFormatFor(Position p, double imageWidth);

} // namespace figures
} // namespace qb
