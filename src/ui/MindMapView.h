#pragma once
// A view on a mind map board (0.17): zoom, scrolling, the mouse (select, move,
// connect by dropping one note on another, resize, draw a marquee, create notes
// by double-clicking) and the keyboard (arrows, Delete, Esc, Tab, M, Z).
// Menu commands live in MindMapController; the view calls it for the keys.

#include <QGraphicsView>
#include <QHash>
#include <QPointer>

namespace qb {

class MindMapScene;
class MindMapController;
class BoardItem;
class Project;

class MindMapView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit MindMapView(QWidget *parent = nullptr);
    void setController(MindMapController *c) { m_ctl = c; }
    void setBoardScene(MindMapScene *scene);
    MindMapScene *boardScene() const { return m_scene; }

    double zoom() const;
    void setZoom(double factor, const QPointF &anchorScene = QPointF(), bool anchored = false);
    void zoomBy(double step);
    void zoomToFit();
    void zoomToRect(const QRectF &r);
    void actualSize() { setZoom(1.0); }
    QPointF visibleCenter() const;
    QRectF visibleRect() const;
    void ensureItemVisible(int id);
    void updateSceneRect();

    bool movementMode() const { return m_moveMode; }
    void setMovementMode(bool on);
    // the keys of these actions reach them while the view has the keyboard,
    // also when another window has the same keys (the inspector's map in the main window)
    void setKeyActions(const QList<QAction *> &actions) { m_keyActions = actions; }
    QPointF lastContextPos() const { return m_contextPos; }
    // What the last right-click hit (0.21): a line (its two ends, else 0, 0)
    // and, on a note or shape, the icon place under the mouse (else -1).
    std::pair<int, int> contextLine() const { return m_contextLine; }
    int contextIconPlace() const { return m_contextIconPlace; }

    // The view of this board is kept with the project (0.21.1): zoom and the point in
    // the middle, one for the inspector's tab and one for the large window ("kind").
    // A board never zoomed starts at the default zoom of File > Options > Editing.
    void setViewMemory(Project *project, const QString &itemId, const QString &kind);
    void saveView();
    bool viewRestored() const { return m_viewRestored; }

signals:
    void zoomChanged(double zoom);
    void movementModeChanged(bool on);
    void contextMenuRequested(const QPoint &globalPos, const QPointF &scenePos);

protected:
    void showEvent(QShowEvent *e) override;
    bool event(QEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void drawForeground(QPainter *p, const QRectF &rect) override;
    void focusOutEvent(QFocusEvent *e) override;

private:
    void restoreView();
    QPointer<Project> m_memProject;
    QString m_memItem, m_memKind;
    bool m_viewRestored = false;
    class QTimer *m_memTimer = nullptr;
    enum class Mode { None, Pending, Marquee, Move, ResizeNote, ResizeShape, Pan, MoveLine };
    BoardItem *boardItemAt(const QPoint &viewPos, bool notesFirst = true) const;
    int resizeEdgeAt(const QPoint &viewPos, int *itemId) const; // 1 left, 2 right, 4 top, 8 bottom
    void startMove(const QPointF &scenePos);
    void updateMove(const QPointF &scenePos, Qt::KeyboardModifiers mods);
    void finishMove(Qt::KeyboardModifiers mods);
    void cancelDrag();
    void selectInDirection(int dx, int dy, bool add);
    void autoScroll(const QPoint &viewPos);
    bool handleDispatch(QKeyEvent *e, bool trigger);

    MindMapScene *m_scene = nullptr;
    QPointer<MindMapController> m_ctl;
    QList<QAction *> m_keyActions;
    Mode m_mode = Mode::None;
    QPoint m_pressView;
    QPointF m_pressScene;
    Qt::KeyboardModifiers m_pressMods;
    int m_pressId = 0;
    bool m_pressWasSelected = false;
    // moving
    QHash<int, QPointF> m_startPos;     // item -> position at the start of the move
    int m_dropTarget = 0;
    std::pair<int, int> m_line{0, 0};
    // resizing
    int m_resizeEdges = 0;
    QHash<int, QRectF> m_startRects;
    // marquee
    QRectF m_marquee;
    QList<int> m_marqueeBase;
    // panning
    bool m_spaceDown = false;
    QPoint m_panLast;
    // quick zoom (Z held down)
    bool m_quickZoom = false;
    QTransform m_quickTransform;
    bool m_moveMode = false;
    QPointF m_contextPos;
    std::pair<int, int> m_contextLine{0, 0};
    int m_contextIconPlace = -1;
    QTimer *m_scrollTimer = nullptr;
    QPoint m_scrollPos;
};

} // namespace qb
