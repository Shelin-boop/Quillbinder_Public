#pragma once
// The picture of a mind map board (0.17): one item per note and per
// background shape, and one item that draws all lines. The board
// (MindMapDoc::board) is the truth; the items show it and are made anew or
// updated by syncAll() after every change. Editing the text of a note happens
// in the note's text item; the board learns the text when editing ends.

#include "core/MindMap.h"

#include <QGraphicsObject>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QHash>
#include <QPointer>

namespace qb {

class MindMapDoc;
class MindMapScene;

class NoteTextItem : public QGraphicsTextItem
{
    Q_OBJECT
public:
    using QGraphicsTextItem::QGraphicsTextItem;
signals:
    void focusLost();

protected:
    void focusOutEvent(QFocusEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
};

class BoardItem : public QGraphicsObject
{
    Q_OBJECT
public:
    BoardItem(MindMapScene *scene, int id) : m_scene(scene), m_id(id) {}
    int id() const { return m_id; }
    virtual QRectF frame() const = 0;   // the note or shape itself (item coordinates)
    virtual bool isShape() const { return false; }
    void setDropTarget(bool on);
    bool isDropTarget() const { return m_dropTarget; }
    // the icons at the corners stick out by half their size (0.21)
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    // Which of the eight icon places (mindmap::IconPos) lies under a point of the
    // item, or -1 (used by the right-click menu to offer "Remove" for that icon).
    int iconPlaceAt(const QPointF &itemPos) const;

protected:
    void paintBody(QPainter *p, const mindmap::Appearance &look, const QRectF &r, bool selected, double opacity);
    // the icons of the note or shape at their places on the frame (0.21)
    void paintIcons(QPainter *p, const mindmap::IconMap &icons, const QRectF &r);
    MindMapScene *m_scene;
    int m_id;
    bool m_dropTarget = false;
};

class NoteItem : public BoardItem
{
    Q_OBJECT
public:
    NoteItem(MindMapScene *scene, int id);
    void sync();                         // from the board
    QRectF frame() const override { return QRectF(0, 0, m_width, m_height); }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *w) override;
    NoteTextItem *textItem() const { return m_text; }
    bool isEditing() const { return m_editing; }
    void startEditing(bool selectAll);
    void endEditing();                   // the text goes back into the board
    // while editing: the width / height the text needs now
    void setWidthLive(double w);

private:
    void relayout();
    NoteTextItem *m_text = nullptr;
    double m_width = 160, m_height = 30;
    bool m_editing = false;
    QImage m_image;
};

class ShapeItem : public BoardItem
{
    Q_OBJECT
public:
    ShapeItem(MindMapScene *scene, int id);
    void sync();
    QRectF frame() const override { return QRectF(QPointF(0, 0), m_size); }
    bool isShape() const override { return true; }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *w) override;

private:
    QSizeF m_size;
};

// All lines between items, with arrows and labels (drawn between the shapes and the notes).
class LinesItem : public QGraphicsItem
{
public:
    explicit LinesItem(MindMapScene *scene) : m_scene(scene) {}
    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt, QWidget *w) override;
    void refresh();

private:
    MindMapScene *m_scene;
    QRectF m_bounds;
};

class MindMapScene : public QGraphicsScene
{
    Q_OBJECT
public:
    explicit MindMapScene(MindMapDoc *doc);
    ~MindMapScene() override;
    MindMapDoc *doc() const { return m_doc; }
    mindmap::Board &board() const;

    void rebuild();                      // every item anew
    void syncAll();                      // items follow the board (made, updated, removed)
    void syncLines() { m_lines->refresh(); }
    BoardItem *itemFor(int id) const;
    NoteItem *noteItem(int id) const { return m_notes.value(id); }
    ShapeItem *shapeItem(int id) const { return m_shapes.value(id); }
    // where an item is right now (while being dragged the board is not told yet)
    QRectF liveRect(int id) const;

    // selection, in the order it was made (Scapple: the first one counts)
    QList<int> selectedIds() const;
    QList<int> selectedNotes() const;
    QList<int> selectedShapes() const;
    void selectIds(const QList<int> &ids, bool add = false);
    int firstSelected() const;

    // the note whose text is being edited (or null)
    NoteItem *editingNote() const;
    void startEditing(int id, bool selectAll = true);
    void endEditing();

    // a line near a point: the two ends (0, 0 when none)
    std::pair<int, int> lineAt(const QPointF &scenePos, double tolerance = 5.0) const;
    // the point where a line between two rectangles starts at the first one
    static QPointF edgePoint(const QRectF &r, const QPointF &towards);
    QPointF labelPos(int a, int b) const;
    // the middle of a line (where its icon is drawn, 0.21)
    QPointF lineMiddle(int a, int b) const;
    // an icon of the binder's set (IconCatalog) as the board shows it, also the project's own icons
    QIcon iconFor(const QString &iconId) const;
    // icon size on the board (Options > Extras > Mind Map), px at 100 %
    static double iconSize();
    // the point of an icon place on a rectangle (corner or middle of a side)
    static QPointF iconPoint(const QRectF &r, int place);

    double fadedOpacity() const;
    bool isFaded(int id) const;          // faded itself or lying on a faded shape
    QColor textColor(const mindmap::Appearance &look) const;
    QRectF contentRect() const;          // all items, for exports and Zoom to Fit
    // the board as a picture / in a PDF or on paper (scene coordinates)
    void render(QPainter *p, const QRectF &target, const QRectF &source, bool background, bool fadedFull);
    bool printing() const { return m_printing; }

signals:
    void editingChanged(bool editing);
    void selectionOrderChanged();

protected:
    void drawBackground(QPainter *p, const QRectF &rect) override;

private:
    void onSelectionChanged();
    MindMapDoc *m_doc;
    QHash<int, NoteItem *> m_notes;
    QHash<int, ShapeItem *> m_shapes;
    LinesItem *m_lines = nullptr;
    QList<int> m_order;
    bool m_printing = false;
    bool m_printBackground = true;
    bool m_printFadedFull = false;
};

// Drawing helpers shared with the style list
namespace mindmapdraw {
QPainterPath bodyPath(const QRectF &r, mindmap::BorderStyle style);
}

} // namespace qb
