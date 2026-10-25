#pragma once
#include "core/Project.h"
#include <QGraphicsObject>
#include <QGraphicsView>
#include <QPointer>
#include <QTimer>
#include <QUrl>

class QLineEdit;
class QPlainTextEdit;

namespace qb {


class CardItem : public QGraphicsObject
{
    Q_OBJECT
public:
    CardItem(Project *p, BinderItem *item, const QSizeF &size);
    BinderItem *item() const { return m_item; }
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    void setCardSize(const QSizeF &s);
    QSizeF cardSize() const { return m_size; }
    QRectF titleRect() const;
    QRectF iconRect() const;
    QRectF bodyRect() const;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    Project *m_project;
    BinderItem *m_item;
    QSizeF m_size;
};

class Corkboard : public QGraphicsView
{
    Q_OBJECT
public:
    enum class Mode { Grid, Freeform, LabelThreads };
    explicit Corkboard(QWidget *parent = nullptr);

    void setProject(Project *p);
    // containers: their children are shown (stacked if several).
    // loose: shown as cards themselves (multiple selection of documents).
    void setContent(const QList<BinderItem *> &containers, const QList<BinderItem *> &loose);
    QList<BinderItem *> selectedItems() const;
    void selectItems(const QList<BinderItem *> &items);
    QList<BinderItem *> containers() const { return m_containers; }
    // the cards shown, in their order (File > Print Current Document)
    QList<BinderItem *> shownItems() const;
    void setMode(Mode m);
    Mode mode() const { return m_mode; }
    void commitFreeformOrder();
    void rebuild();
    void editTitleOf(BinderItem *item);
    // Filter bar: only matching cards are shown.
    void setFilter(const ItemFilter &f);
    ItemFilter filter() const { return m_filter; }
    // cards shown / cards there are (without the filter)
    void filterCounts(int *shown, int *total) const;

signals:
    void selectionChanged();
    void openRequested(qb::BinderItem *item);
    void contextMenuRequested(const QPoint &globalPos);
    void deleteRequested();
    void spacePressed();
    // a double click on the background, or Return on a card (File > Options > Behaviors, 0.21)
    void newCardRequested();
    void filesDropped(qb::BinderItem *container, int row, const QList<QUrl> &urls);

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    struct Slot {
        BinderItem *container; // null for loose cards
        int row;               // insertion row in the container
        QPointF marker;        // where to draw the insertion line
    };
    void layoutCards();
    QSize layoutSize() const;   // room for the cards, the same with and without scroll bars
    void updateTexture();
    CardItem *cardAt(const QPoint &viewPos) const;
    Slot slotAt(const QPointF &scenePos) const;
    Slot visibleSlotAt(const QPointF &scenePos) const;
    void startEdit(CardItem *card, bool title);
    void finishEdit(bool commit);

    QPointer<Project> m_project;
    QList<BinderItem *> m_containers;
    QList<BinderItem *> m_loose;
    QList<CardItem *> m_cards;
    QList<QGraphicsItem *> m_decor; // group titles, lanes
    QGraphicsLineItem *m_marker = nullptr;
    Mode m_mode = Mode::Grid;
    ItemFilter m_filter;
    int m_unfilteredCount = 0;
    QList<int> m_containerCards;   // cards shown per container
    QPoint m_pressPos;
    CardItem *m_pressCard = nullptr;
    bool m_dragging = false;
    QPixmap m_texture;
    QColor m_textureColor;
    QTimer m_rebuildTimer;
    // inline editing
    QLineEdit *m_titleEdit = nullptr;
    QPlainTextEdit *m_bodyEdit = nullptr;
    QPointer<CardItem> m_editCard;
    struct Placement {
        CardItem *card;
        QPointF pos;
    };
    QList<QPair<BinderItem *, QList<QRectF>>> m_groupRects; // container -> card rects (grid slots)
    bool m_bodyHooked = false;
};

} // namespace qb
