#include "ui/MindMapView.h"
#include "core/Project.h"
#include "ui/ViewMemory.h"
#include "ui/Zoom.h"
#include "ui/MindMapController.h"
#include "ui/MindMapDoc.h"
#include "ui/MindMapScene.h"
#include "ui/MindMapUi.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

namespace qb {

using namespace mindmap;

MindMapView::MindMapView(QWidget *parent) : QGraphicsView(parent)
{
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    zoom::markOwn(this); // Ctrl + mouse wheel zooms the board around the mouse (wheelEvent)
    setDragMode(QGraphicsView::NoDrag);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    m_scrollTimer = new QTimer(this);
    m_scrollTimer->setInterval(30);
    connect(m_scrollTimer, &QTimer::timeout, this, [this] {
        if (m_mode != Mode::Move && m_mode != Mode::Marquee && m_mode != Mode::MoveLine) {
            m_scrollTimer->stop();
            return;
        }
        autoScroll(m_scrollPos);
    });
}

void MindMapView::setViewMemory(Project *project, const QString &itemId, const QString &kind)
{
    m_memProject = project;
    m_memItem = itemId;
    m_memKind = kind;
    m_viewRestored = false;
    if (!m_memTimer) {
        m_memTimer = new QTimer(this);
        m_memTimer->setSingleShot(true);
        m_memTimer->setInterval(400);
        connect(m_memTimer, &QTimer::timeout, this, &MindMapView::saveView);
        auto later = [this] {
            if (m_viewRestored)
                m_memTimer->start();
        };
        connect(this, &MindMapView::zoomChanged, this, later);
        connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, later);
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, later);
    }
    if (isVisible())
        QTimer::singleShot(0, this, [this] { restoreView(); });
}

void MindMapView::showEvent(QShowEvent *e)
{
    QGraphicsView::showEvent(e);
    if (!m_viewRestored && !m_memItem.isEmpty())
        QTimer::singleShot(0, this, [this] { restoreView(); });
}

void MindMapView::restoreView()
{
    if (m_viewRestored || !m_scene)
        return;
    m_viewRestored = true;
    const viewmem::BoardView v = viewmem::boardView(m_memProject, m_memItem, m_memKind);
    if (v.valid()) {
        setTransform(QTransform::fromScale(qBound(0.1, v.zoom, 5.0), qBound(0.1, v.zoom, 5.0)));
        updateSceneRect();
        centerOn(v.center);
        emit zoomChanged(zoom());
        return;
    }
    // a board seen for the first time here: the default zoom of new boards
    const double f = zoom::defaultFactor(QLatin1String(zoom::kMindMap));
    if (!qFuzzyCompare(f, zoom()))
        setZoom(f);
}

void MindMapView::saveView()
{
    if (!m_viewRestored || !m_memProject || m_memItem.isEmpty() || !isVisible())
        return;
    viewmem::setBoardView(m_memProject, m_memItem, m_memKind, {zoom(), visibleCenter()});
}

void MindMapView::setBoardScene(MindMapScene *scene)
{
    if (m_scene)
        disconnect(m_scene->doc(), nullptr, this, nullptr);
    m_scene = scene;
    setScene(scene);
    if (scene) {
        connect(scene->doc(), &MindMapDoc::boardChanged, this, [this] { updateSceneRect(); });
        updateSceneRect();
        // the first view shows the top left of the board
        const QRectF c = scene->contentRect();
        if (!c.isNull())
            centerOn(c.center().x(), c.top() + viewport()->height() / 2.0 / zoom() - 20);
    }
}

double MindMapView::zoom() const { return transform().m11(); }

void MindMapView::setZoom(double factor, const QPointF &anchor, bool anchored)
{
    factor = qBound(0.1, factor, 5.0);
    const QPointF keepScene = anchored ? anchor : visibleCenter();
    const QPoint keepView = anchored ? mapFromScene(anchor) : viewport()->rect().center();
    setTransform(QTransform::fromScale(factor, factor));
    updateSceneRect();
    // the anchor stays where it was in the view
    const QPointF now = mapToScene(keepView);
    const QPointF d = (now - keepScene) * factor;
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - int(std::round(d.x())));
    verticalScrollBar()->setValue(verticalScrollBar()->value() - int(std::round(d.y())));
    emit zoomChanged(factor);
}

void MindMapView::zoomBy(double step) { setZoom(zoom() * step); }

void MindMapView::zoomToRect(const QRectF &r)
{
    if (r.isNull())
        return;
    const QRectF target = r.adjusted(-30, -30, 30, 30);
    const double f = qMin(viewport()->width() / target.width(), viewport()->height() / target.height());
    setTransform(QTransform::fromScale(qBound(0.1, f, 5.0), qBound(0.1, f, 5.0)));
    updateSceneRect();
    centerOn(target.center());
    emit zoomChanged(zoom());
}

void MindMapView::zoomToFit()
{
    if (m_scene)
        zoomToRect(m_scene->contentRect());
}

QPointF MindMapView::visibleCenter() const { return mapToScene(viewport()->rect().center()); }

QRectF MindMapView::visibleRect() const { return mapToScene(viewport()->rect()).boundingRect(); }

void MindMapView::ensureItemVisible(int id)
{
    if (!m_scene)
        return;
    const QRectF r = m_scene->liveRect(id);
    if (!r.isNull())
        ensureVisible(r, 40, 40);
}

void MindMapView::updateSceneRect()
{
    if (!m_scene)
        return;
    // the board reaches a little beyond its notes and is at least as large as the view
    QRectF r = m_scene->contentRect();
    if (r.isNull())
        r = QRectF(0, 0, 10, 10);
    const QSizeF view = QSizeF(viewport()->size()) / zoom();
    r = r.adjusted(-view.width() * 0.6, -view.height() * 0.6, view.width() * 0.6, view.height() * 0.6);
    r = r.united(QRectF(0, 0, 10, 10));
    // never smaller than what is shown now (no jumping while working)
    r = r.united(visibleRect());
    setSceneRect(r);
}

void MindMapView::setMovementMode(bool on)
{
    if (m_moveMode == on)
        return;
    m_moveMode = on;
    emit movementModeChanged(on);
}

// ------------------------------------------------------------------ hit tests

BoardItem *MindMapView::boardItemAt(const QPoint &viewPos, bool notesFirst) const
{
    BoardItem *shape = nullptr;
    for (QGraphicsItem *gi : items(viewPos)) {
        if (auto *bi = dynamic_cast<BoardItem *>(gi)) {
            if (!bi->isShape() && notesFirst)
                return bi;
            if (bi->isShape() && !shape)
                shape = bi;
            else if (!bi->isShape())
                return bi;
        } else if (gi->parentItem()) {
            if (auto *bi = dynamic_cast<BoardItem *>(gi->parentItem()); bi && !bi->isShape())
                return bi;
        }
    }
    return shape;
}

int MindMapView::resizeEdgeAt(const QPoint &viewPos, int *itemId) const
{
    if (!m_scene || m_scene->doc()->locked())
        return 0;
    const double tol = 5.0 / zoom();
    const QPointF sp = mapToScene(viewPos);
    // notes: the left and right edge (pictures: also top and bottom)
    for (QGraphicsItem *gi : items(QRect(viewPos - QPoint(6, 6), QSize(12, 12)))) {
        auto *bi = dynamic_cast<BoardItem *>(gi);
        if (!bi)
            continue;
        const QRectF r = m_scene->liveRect(bi->id());
        if (!r.adjusted(-tol, -tol, tol, tol).contains(sp))
            continue;
        int edges = 0;
        if (qAbs(sp.x() - r.left()) <= tol)
            edges |= 1;
        else if (qAbs(sp.x() - r.right()) <= tol)
            edges |= 2;
        const Note *n = m_scene->board().note(bi->id());
        if (bi->isShape() || (n && n->isImage())) {
            if (qAbs(sp.y() - r.top()) <= tol)
                edges |= 4;
            else if (qAbs(sp.y() - r.bottom()) <= tol)
                edges |= 8;
        }
        if (edges) {
            *itemId = bi->id();
            return edges;
        }
    }
    return 0;
}

// ------------------------------------------------------------------ keys

bool MindMapView::handleDispatch(QKeyEvent *e, bool trigger)
{
    if (e->key() == Qt::Key_unknown || e->key() == Qt::Key_Shift || e->key() == Qt::Key_Control || e->key() == Qt::Key_Alt
        || e->key() == Qt::Key_Meta)
        return false;
    const QKeySequence ks(e->keyCombination());
    for (QAction *a : m_keyActions) {
        for (const QKeySequence &s : a->shortcuts()) {
            if (s.matches(ks) == QKeySequence::ExactMatch) {
                if (trigger && a->isEnabled())
                    a->trigger();
                return true;
            }
        }
    }
    return false;
}

bool MindMapView::event(QEvent *e)
{
    if (e->type() == QEvent::ShortcutOverride && m_scene) {
        auto *ke = static_cast<QKeyEvent *>(e);
        const bool editing = m_scene->editingNote() != nullptr;
        if (editing) {
            // typing in a note: the keys of the text go to the text, except the note commands
            const int k = ke->key();
            const Qt::KeyboardModifiers m = ke->modifiers() & ~Qt::KeypadModifier;
            const bool textKey = (m == Qt::NoModifier || m == Qt::ShiftModifier) || ke->matches(QKeySequence::Copy)
                                 || ke->matches(QKeySequence::Paste) || ke->matches(QKeySequence::Cut)
                                 || ke->matches(QKeySequence::Undo) || ke->matches(QKeySequence::Redo)
                                 || ke->matches(QKeySequence::SelectAll) || k == Qt::Key_Left || k == Qt::Key_Right
                                 || k == Qt::Key_Up || k == Qt::Key_Down || k == Qt::Key_Home || k == Qt::Key_End
                                 || k == Qt::Key_Backspace || k == Qt::Key_Delete;
            if (textKey && !(m == Qt::NoModifier && k == Qt::Key_Escape && false)) {
                e->accept();
                return true;
            }
        }
        if (handleDispatch(ke, false)) {
            e->accept();
            return true;
        }
        // the board's own keys
        const int k = ke->key();
        if (ke->modifiers() == Qt::NoModifier
            && (k == Qt::Key_Delete || k == Qt::Key_Backspace || k == Qt::Key_Escape || k == Qt::Key_M || k == Qt::Key_Z
                || k == Qt::Key_Space || k == Qt::Key_Tab)) {
            e->accept();
            return true;
        }
    }
    return QGraphicsView::event(e);
}

void MindMapView::keyPressEvent(QKeyEvent *e)
{
    if (!m_scene) {
        QGraphicsView::keyPressEvent(e);
        return;
    }
    NoteItem *editing = m_scene->editingNote();
    if (editing) {
        // note commands (bold, split ...) first; the rest is typing
        const Qt::KeyboardModifiers m = e->modifiers() & ~Qt::KeypadModifier;
        if (m != Qt::NoModifier && m != Qt::ShiftModifier && !e->matches(QKeySequence::Copy) && !e->matches(QKeySequence::Paste)
            && !e->matches(QKeySequence::Cut) && !e->matches(QKeySequence::Undo) && !e->matches(QKeySequence::Redo)
            && !e->matches(QKeySequence::SelectAll) && handleDispatch(e, true))
            return;
        QGraphicsView::keyPressEvent(e);
        return;
    }
    if (m_mode != Mode::None && e->key() == Qt::Key_Escape) {
        cancelDrag();
        return;
    }
    if (handleDispatch(e, true))
        return;
    const bool locked = m_scene->doc()->locked();
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    switch (e->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (m_ctl)
            m_ctl->deleteSelection();
        return;
    case Qt::Key_Escape:
    case Qt::Key_F2:
        if (m_scene->selectedNotes().size() == 1)
            m_scene->startEditing(m_scene->selectedNotes().first(), true);
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (MindMapPrefs::instance().enterCreatesNotes) {
            if (m_ctl)
                m_ctl->newNote();
        } else if (m_scene->selectedNotes().size() == 1) {
            m_scene->startEditing(m_scene->selectedNotes().first(), false);
        }
        return;
    case Qt::Key_Tab:
        if (m_ctl && !locked)
            m_ctl->indent(1);
        return;
    case Qt::Key_Backtab:
        if (m_ctl && !locked)
            m_ctl->indent(-1);
        return;
    case Qt::Key_M:
        if (e->modifiers() == Qt::NoModifier) {
            setMovementMode(!m_moveMode);
            return;
        }
        break;
    case Qt::Key_Z:
        if (e->modifiers() == Qt::NoModifier && !e->isAutoRepeat() && !m_quickZoom) {
            m_quickZoom = true;
            m_quickTransform = transform();
            zoomToFit();
            return;
        }
        if (e->isAutoRepeat())
            return;
        break;
    case Qt::Key_Space:
        if (!e->isAutoRepeat()) {
            m_spaceDown = true;
            viewport()->setCursor(Qt::OpenHandCursor);
        }
        return;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
        const int dx = e->key() == Qt::Key_Left ? -1 : e->key() == Qt::Key_Right ? 1 : 0;
        const int dy = e->key() == Qt::Key_Up ? -1 : e->key() == Qt::Key_Down ? 1 : 0;
        if (m_moveMode) {
            if (m_ctl && !locked) {
                const double step = (e->modifiers() & Qt::ControlModifier) && shift ? 50 : shift ? 10 : 1;
                m_ctl->nudge(dx * step, dy * step);
            }
        } else {
            selectInDirection(dx, dy, shift);
        }
        return;
    }
    default: break;
    }
    QGraphicsView::keyPressEvent(e);
}

void MindMapView::keyReleaseEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Z && !e->isAutoRepeat() && m_quickZoom) {
        m_quickZoom = false;
        // back to the zoom of before, with the place under the mouse in view
        const QPointF target = mapToScene(viewport()->mapFromGlobal(QCursor::pos()));
        setTransform(m_quickTransform);
        updateSceneRect();
        centerOn(target);
        emit zoomChanged(zoom());
        return;
    }
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        m_spaceDown = false;
        viewport()->unsetCursor();
        return;
    }
    QGraphicsView::keyReleaseEvent(e);
}

void MindMapView::focusOutEvent(QFocusEvent *e)
{
    m_spaceDown = false;
    QGraphicsView::focusOutEvent(e);
}

void MindMapView::selectInDirection(int dx, int dy, bool add)
{
    const Board &b = m_scene->board();
    const int cur = m_scene->selectedIds().isEmpty() ? 0 : m_scene->selectedIds().last();
    if (!cur) {
        // nothing selected: the note nearest the middle of the view
        int best = 0;
        double bestD = 1e18;
        const QPointF c = visibleCenter();
        for (const Note &n : b.notes) {
            const QPointF d = m_scene->liveRect(n.id).center() - c;
            if (QPointF::dotProduct(d, d) < bestD) {
                bestD = QPointF::dotProduct(d, d);
                best = n.id;
            }
        }
        if (best)
            m_scene->selectIds({best});
        return;
    }
    int next = 0;
    // stacked notes prefer each other
    if (dy > 0) {
        const QList<int> below = b.stackBelow(cur);
        if (!below.isEmpty())
            next = below.first();
    } else if (dy < 0) {
        next = b.stackAbove(cur);
    }
    if (!next) {
        const QRectF r = m_scene->liveRect(cur);
        const QPointF c = r.center();
        double bestScore = 1e18;
        for (const Note &n : b.notes) {
            if (n.id == cur)
                continue;
            const QPointF d = m_scene->liveRect(n.id).center() - c;
            const double along = d.x() * dx + d.y() * dy;
            if (along <= 1)
                continue;
            const double across = qAbs(d.x() * dy) + qAbs(d.y() * dx);
            const double score = along + across * 2.5;
            if (score < bestScore) {
                bestScore = score;
                next = n.id;
            }
        }
    }
    if (next) {
        m_scene->selectIds({next}, add);
        ensureItemVisible(next);
    }
}

// ------------------------------------------------------------------ mouse

void MindMapView::mousePressEvent(QMouseEvent *e)
{
    if (!m_scene) {
        QGraphicsView::mousePressEvent(e);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    m_pressView = e->pos();
    m_pressScene = mapToScene(e->pos());
    m_pressMods = e->modifiers();
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && m_spaceDown)) {
        m_mode = Mode::Pan;
        m_panLast = e->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() != Qt::LeftButton) {
        QGraphicsView::mousePressEvent(e);
        return;
    }
    // a click into the note being edited goes to its text
    if (NoteItem *editing = m_scene->editingNote()) {
        if (m_scene->liveRect(editing->id()).contains(m_pressScene)) {
            QGraphicsView::mousePressEvent(e);
            return;
        }
        m_scene->endEditing();
    }
    const bool locked = m_scene->doc()->locked();
    int resizeId = 0;
    const int edges = locked ? 0 : resizeEdgeAt(e->pos(), &resizeId);
    if (edges) {
        if (!m_scene->selectedIds().contains(resizeId))
            m_scene->selectIds({resizeId});
        m_resizeEdges = edges;
        m_startRects.clear();
        const bool shape = m_scene->shapeItem(resizeId);
        for (int id : m_scene->selectedIds())
            if (bool(m_scene->shapeItem(id)) == shape)
                m_startRects.insert(id, m_scene->liveRect(id));
        m_pressId = resizeId;
        m_mode = shape ? Mode::ResizeShape : Mode::ResizeNote;
        return;
    }
    BoardItem *hit = boardItemAt(e->pos());
    if (hit && hit->isShape() && (e->modifiers() & Qt::ShiftModifier)) {
        hit = nullptr; // Shift: a marquee inside the shape
    }
    if (hit) {
        const int id = hit->id();
        m_pressId = id;
        m_pressWasSelected = hit->isSelected();
        if (hit->isShape() && (e->modifiers() & Qt::AltModifier)) {
            // the shape and everything on it
            QList<int> ids{id};
            ids << m_scene->board().overlapping(id);
            m_scene->selectIds(ids, e->modifiers() & Qt::ControlModifier);
            m_mode = Mode::Pending;
            return;
        }
        if (e->modifiers() & Qt::ControlModifier) {
            // toggled on release unless the notes are dragged (Ctrl+drag copies)
            if (!hit->isSelected())
                m_scene->selectIds({id}, true);
        } else if (!hit->isSelected()) {
            m_scene->selectIds({id});
        }
        m_mode = Mode::Pending;
        return;
    }
    const auto line = m_scene->lineAt(m_pressScene, 5.0 / zoom());
    if (line.first) {
        m_line = line;
        m_scene->selectIds({line.first, line.second});
        m_mode = Mode::Pending;
        m_pressId = 0;
        return;
    }
    // the background: a marquee
    m_mode = Mode::Marquee;
    m_marquee = QRectF(m_pressScene, QSizeF(0, 0));
    m_marqueeBase = (e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier)) ? m_scene->selectedIds() : QList<int>();
    if (m_marqueeBase.isEmpty())
        m_scene->selectIds({});
}

void MindMapView::startMove(const QPointF &)
{
    m_startPos.clear();
    QList<int> moving = m_line.first ? QList<int>{m_line.first, m_line.second} : m_scene->selectedIds();
    // magnetic shapes take what lies on them
    QList<int> extra;
    for (int id : moving)
        if (const mindmap::Shape *s = m_scene->board().shape(id); s && s->magnetic)
            for (int o : m_scene->board().overlapping(id))
                if (!moving.contains(o) && !extra.contains(o))
                    extra << o;
    moving << extra;
    for (int id : moving)
        if (BoardItem *it = m_scene->itemFor(id))
            m_startPos.insert(id, it->pos());
    m_mode = Mode::Move;
}

void MindMapView::updateMove(const QPointF &sp, Qt::KeyboardModifiers mods)
{
    const QPointF d = sp - m_pressScene;
    for (auto it = m_startPos.cbegin(); it != m_startPos.cend(); ++it)
        if (BoardItem *bi = m_scene->itemFor(it.key()))
            bi->setPos(it.value() + d);
    m_scene->syncLines();
    // a note (or shape) under the mouse that is not moved: dropping connects
    int target = 0;
    if (!m_line.first) {
        const bool draggingShape = m_scene->shapeItem(m_pressId) != nullptr;
        for (QGraphicsItem *gi : items(mapFromScene(sp))) {
            auto *bi = dynamic_cast<BoardItem *>(gi);
            if (!bi && gi->parentItem())
                bi = dynamic_cast<BoardItem *>(gi->parentItem());
            if (!bi || m_startPos.contains(bi->id()))
                continue;
            // notes are placed on shapes, not connected to them; a shape can be dropped on anything
            if (bi->isShape() && !draggingShape)
                continue;
            target = bi->id();
            break;
        }
    }
    if (target != m_dropTarget) {
        if (BoardItem *old = m_scene->itemFor(m_dropTarget))
            old->setDropTarget(false);
        if (BoardItem *now = m_scene->itemFor(target))
            now->setDropTarget(true);
        m_dropTarget = target;
    }
    viewport()->setCursor(m_dropTarget ? Qt::DragLinkCursor : (mods & Qt::ControlModifier) ? Qt::DragCopyCursor : Qt::ClosedHandCursor);
}

void MindMapView::cancelDrag()
{
    if (m_mode == Mode::Move || m_mode == Mode::MoveLine) {
        for (auto it = m_startPos.cbegin(); it != m_startPos.cend(); ++it)
            if (BoardItem *bi = m_scene->itemFor(it.key()))
                bi->setPos(it.value());
    }
    if (m_mode == Mode::ResizeNote || m_mode == Mode::ResizeShape)
        m_scene->syncAll();
    if (BoardItem *old = m_scene->itemFor(m_dropTarget))
        old->setDropTarget(false);
    m_dropTarget = 0;
    m_mode = Mode::None;
    m_line = {0, 0};
    m_startPos.clear();
    viewport()->unsetCursor();
    m_scene->syncLines();
    viewport()->update();
}

void MindMapView::finishMove(Qt::KeyboardModifiers mods)
{
    MindMapDoc *doc = m_scene->doc();
    Board &b = doc->board;
    QPointF delta;
    if (!m_startPos.isEmpty())
        if (BoardItem *bi = m_scene->itemFor(m_startPos.constBegin().key()))
            delta = bi->pos() - m_startPos.constBegin().value();
    // the items go back; the board decides where they end up
    for (auto it = m_startPos.cbegin(); it != m_startPos.cend(); ++it)
        if (BoardItem *bi = m_scene->itemFor(it.key()))
            bi->setPos(it.value());
    const QList<int> moved = m_startPos.keys();
    const int target = m_dropTarget;
    if (BoardItem *old = m_scene->itemFor(m_dropTarget))
        old->setDropTarget(false);
    m_dropTarget = 0;
    if (doc->locked()) {
        m_scene->syncAll();
        return;
    }
    if (target) {
        // dropped on another note: connect (or disconnect, or change the arrows)
        const bool alt = mods & Qt::AltModifier, ctrl = mods & Qt::ControlModifier, shift = mods & Qt::ShiftModifier;
        QList<int> dragged = m_line.first ? QList<int>{} : m_scene->selectedIds();
        doc->change(QObject::tr("Connect"), [&] {
            for (int id : dragged) {
                if (id == target)
                    continue;
                if (alt && ctrl && shift) {
                    b.connect(id, target, true, true);
                } else if (alt && ctrl) {
                    // an arrow from the note dropped on back to the dragged one (again: none)
                    if (b.pointsTo(target, id))
                        b.setArrow(target, id, false);
                    else
                        b.connect(id, target, false, true);
                } else if (alt) {
                    if (b.pointsTo(id, target))
                        b.setArrow(id, target, false);
                    else
                        b.connect(id, target, true, false);
                } else {
                    if (b.isConnected(id, target))
                        b.disconnect(id, target);
                    else
                        b.connect(id, target);
                }
            }
        });
        return;
    }
    if (delta.manhattanLength() < 0.01)
        return;
    if ((mods & Qt::ControlModifier) && !m_line.first) {
        // Ctrl at the drop: copies
        if (m_ctl)
            m_ctl->duplicate(moved, delta);
        return;
    }
    doc->change(QObject::tr("Move"), [&] {
        // a note taken out of the middle of a stack: the notes below close the gap
        for (int id : moved) {
            const Note *n = b.note(id);
            if (!n || !b.stackAbove(id))
                continue;
            const QList<int> below = b.stackBelow(id);
            if (below.isEmpty() || moved.contains(below.first()))
                continue;
            const double h = b.rectOf(id).height();
            for (int bid : below)
                if (!moved.contains(bid))
                    b.note(bid)->pos.ry() -= h;
        }
        for (int id : moved) {
            if (Note *n = b.note(id))
                n->pos += delta;
            else if (mindmap::Shape *s = b.shape(id))
                s->rect.translate(delta);
        }
    });
}

void MindMapView::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_scene) {
        QGraphicsView::mouseMoveEvent(e);
        return;
    }
    const QPointF sp = mapToScene(e->pos());
    switch (m_mode) {
    case Mode::Pan: {
        const QPoint d = e->pos() - m_panLast;
        m_panLast = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        return;
    }
    case Mode::Pending:
        if ((e->pos() - m_pressView).manhattanLength() >= QApplication::startDragDistance() && !m_scene->doc()->locked()) {
            startMove(m_pressScene);
            m_mode = m_line.first ? Mode::MoveLine : Mode::Move;
            updateMove(sp, e->modifiers());
            m_scrollPos = e->pos();
            m_scrollTimer->start();
        }
        return;
    case Mode::Move:
    case Mode::MoveLine:
        updateMove(sp, e->modifiers());
        m_scrollPos = e->pos();
        return;
    case Mode::Marquee: {
        m_marquee = QRectF(m_pressScene, sp).normalized();
        QList<int> ids = m_marqueeBase;
        const bool remove = m_pressMods & Qt::AltModifier;
        for (QGraphicsItem *gi : m_scene->items(m_marquee, Qt::IntersectsItemShape)) {
            auto *bi = dynamic_cast<BoardItem *>(gi);
            if (!bi)
                continue;
            if (remove)
                ids.removeAll(bi->id());
            else if (!ids.contains(bi->id()))
                ids << bi->id();
        }
        m_scene->selectIds(ids);
        m_scrollPos = e->pos();
        if (!m_scrollTimer->isActive())
            m_scrollTimer->start();
        viewport()->update();
        return;
    }
    case Mode::ResizeNote:
    case Mode::ResizeShape: {
        const QPointF d = sp - m_pressScene;
        const bool fromMiddle = e->modifiers() & Qt::AltModifier;
        for (auto it = m_startRects.cbegin(); it != m_startRects.cend(); ++it) {
            QRectF r = it.value();
            const bool image = m_scene->board().note(it.key()) && m_scene->board().note(it.key())->isImage();
            if (m_resizeEdges & 1) {
                r.setLeft(qMin(r.right() - 30, r.left() + d.x()));
                if (fromMiddle)
                    r.setRight(it.value().right() - d.x());
            }
            if (m_resizeEdges & 2) {
                r.setRight(qMax(r.left() + 30, r.right() + d.x()));
                if (fromMiddle)
                    r.setLeft(it.value().left() - d.x());
            }
            if (m_resizeEdges & 4) {
                r.setTop(qMin(r.bottom() - 20, r.top() + d.y()));
                if (fromMiddle)
                    r.setBottom(it.value().bottom() - d.y());
            }
            if (m_resizeEdges & 8) {
                r.setBottom(qMax(r.top() + 20, r.bottom() + d.y()));
                if (fromMiddle)
                    r.setTop(it.value().top() - d.y());
            }
            if (image) {
                // pictures keep their proportions
                const QImage &img = m_scene->board().note(it.key())->image;
                const double ratio = double(img.height()) / qMax(1, img.width());
                if (m_resizeEdges & (1 | 2))
                    r.setHeight(r.width() * ratio);
                else
                    r.setWidth(r.height() / ratio);
            }
            if (NoteItem *ni = m_scene->noteItem(it.key())) {
                ni->setPos(r.topLeft());
                ni->setWidthLive(r.width());
            } else if (ShapeItem *si = m_scene->shapeItem(it.key())) {
                mindmap::Shape *s = m_scene->board().shape(it.key());
                const QRectF keep = s->rect;
                s->rect = r; // shown at once; the board gets it for good on release
                si->sync();
                s->rect = keep;
            }
        }
        m_scene->syncLines();
        return;
    }
    case Mode::None: {
        // the cursor tells what a press would do
        int id = 0;
        const int edges = resizeEdgeAt(e->pos(), &id);
        if (edges == 1 || edges == 2)
            viewport()->setCursor(Qt::SizeHorCursor);
        else if (edges == 4 || edges == 8)
            viewport()->setCursor(Qt::SizeVerCursor);
        else if (edges == 5 || edges == 10)
            viewport()->setCursor(Qt::SizeFDiagCursor);
        else if (edges == 6 || edges == 9)
            viewport()->setCursor(Qt::SizeBDiagCursor);
        else if (m_spaceDown)
            viewport()->setCursor(Qt::OpenHandCursor);
        else
            viewport()->unsetCursor();
        QGraphicsView::mouseMoveEvent(e);
        return;
    }
    }
}

void MindMapView::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_scene) {
        QGraphicsView::mouseReleaseEvent(e);
        return;
    }
    m_scrollTimer->stop();
    const Mode mode = m_mode;
    m_mode = Mode::None;
    switch (mode) {
    case Mode::Pan:
        viewport()->setCursor(m_spaceDown ? Qt::OpenHandCursor : Qt::ArrowCursor);
        if (!m_spaceDown)
            viewport()->unsetCursor();
        return;
    case Mode::Pending:
        // a click without moving
        if (m_pressId && (m_pressMods & Qt::ControlModifier) && m_pressWasSelected) {
            QList<int> ids = m_scene->selectedIds();
            ids.removeAll(m_pressId);
            m_scene->selectIds(ids);
        } else if (m_pressId && !(m_pressMods & (Qt::ControlModifier | Qt::AltModifier)) && m_scene->selectedIds().size() > 1) {
            m_scene->selectIds({m_pressId});
        }
        if (m_pressId && (m_pressMods & Qt::ControlModifier))
            ensureItemVisible(m_pressId);
        // a click on a link opens it (web page, file, or a document of the project)
        if (m_pressId && !(m_pressMods & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier)) && m_ctl)
            if (NoteItem *ni = m_scene->noteItem(m_pressId)) {
                const QPointF local = ni->textItem()->mapFromScene(m_pressScene);
                const QString href = ni->textItem()->document()->documentLayout()->anchorAt(local);
                if (!href.isEmpty())
                    m_ctl->openLink(href);
            }
        m_line = {0, 0};
        return;
    case Mode::Move:
    case Mode::MoveLine:
        finishMove(e->modifiers());
        m_line = {0, 0};
        m_startPos.clear();
        viewport()->unsetCursor();
        return;
    case Mode::Marquee:
        m_marquee = QRectF();
        viewport()->update();
        return;
    case Mode::ResizeNote:
    case Mode::ResizeShape: {
        MindMapDoc *doc = m_scene->doc();
        QHash<int, QRectF> now;
        for (auto it = m_startRects.cbegin(); it != m_startRects.cend(); ++it)
            now.insert(it.key(), m_scene->liveRect(it.key()));
        doc->change(QObject::tr("Resize"), [&] {
            Board &b = doc->board;
            for (auto it = now.cbegin(); it != now.cend(); ++it) {
                if (Note *n = b.note(it.key())) {
                    const double oldBottom = b.rectOf(n->id).bottom();
                    n->pos = it.value().topLeft();
                    n->width = it.value().width();
                    if (n->isImage())
                        n->height = it.value().height();
                    b.restack(n->id, oldBottom);
                } else if (mindmap::Shape *s = b.shape(it.key())) {
                    s->rect = it.value();
                }
            }
        });
        m_startRects.clear();
        viewport()->unsetCursor();
        return;
    }
    case Mode::None: break;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

void MindMapView::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (!m_scene || e->button() != Qt::LeftButton) {
        QGraphicsView::mouseDoubleClickEvent(e);
        return;
    }
    m_mode = Mode::None;
    const QPointF sp = mapToScene(e->pos());
    if (NoteItem *editing = m_scene->editingNote(); editing && m_scene->liveRect(editing->id()).contains(sp)) {
        QGraphicsView::mouseDoubleClickEvent(e);
        return;
    }
    BoardItem *hit = boardItemAt(e->pos());
    if (hit && !hit->isShape()) {
        auto *ni = static_cast<NoteItem *>(hit);
        const Note *n = m_scene->board().note(ni->id());
        if (n && n->isImage())
            return;
        m_scene->startEditing(ni->id(), false);
        // the cursor where the double-click was
        const QPointF local = ni->textItem()->mapFromScene(sp);
        const int pos = ni->textItem()->document()->documentLayout()->hitTest(local, Qt::FuzzyHit);
        if (pos >= 0) {
            QTextCursor c = ni->textItem()->textCursor();
            c.setPosition(pos);
            c.select(QTextCursor::WordUnderCursor);
            ni->textItem()->setTextCursor(c);
        }
        return;
    }
    if (m_scene->doc()->locked())
        return;
    // a label or a line: edit the label, or put a note in between
    const auto line = m_scene->lineAt(sp, 5.0 / zoom());
    if (line.first && m_ctl) {
        const QString label = m_scene->board().lineLabel(line.first, line.second);
        QRectF lr(0, 0, 60, 18);
        lr.moveCenter(m_scene->labelPos(line.first, line.second));
        if (!label.isEmpty() && lr.contains(sp))
            m_ctl->editLineLabel(line.first, line.second);
        else
            m_ctl->noteBetween(line.first, line.second, sp);
        return;
    }
    if (m_ctl)
        m_ctl->newNoteAt(sp, e->modifiers());
}

void MindMapView::wheelEvent(QWheelEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        const double steps = e->angleDelta().y() / 120.0;
        if (!qFuzzyIsNull(steps))
            setZoom(zoom() * std::pow(1.15, steps), mapToScene(e->position().toPoint()), true);
        e->accept();
        return;
    }
    QGraphicsView::wheelEvent(e);
}

void MindMapView::contextMenuEvent(QContextMenuEvent *e)
{
    if (!m_scene)
        return;
    const QPointF sp = mapToScene(e->pos());
    m_contextPos = sp;
    m_contextLine = {0, 0};
    m_contextIconPlace = -1;
    if (!m_scene->editingNote()) {
        BoardItem *hit = boardItemAt(e->pos());
        if (hit) {
            if (!hit->isSelected())
                m_scene->selectIds({hit->id()});
            m_contextIconPlace = hit->iconPlaceAt(hit->mapFromScene(sp));
        } else {
            // a right-click on a line acts on that one line only (0.21): its
            // arrows, color and icon; the notes at its ends are not selected
            const auto line = m_scene->lineAt(sp, 6.0 / zoom());
            if (line.first)
                m_contextLine = line;
        }
    }
    emit contextMenuRequested(e->globalPos(), sp);
}

void MindMapView::autoScroll(const QPoint &pos)
{
    const int margin = 24;
    int dx = 0, dy = 0;
    if (pos.x() < margin)
        dx = -(margin - pos.x()) / 2 - 2;
    else if (pos.x() > viewport()->width() - margin)
        dx = (pos.x() - viewport()->width() + margin) / 2 + 2;
    if (pos.y() < margin)
        dy = -(margin - pos.y()) / 2 - 2;
    else if (pos.y() > viewport()->height() - margin)
        dy = (pos.y() - viewport()->height() + margin) / 2 + 2;
    if (!dx && !dy)
        return;
    // the board grows when needed
    QRectF sr = sceneRect();
    const QRectF vis = visibleRect().translated(dx / zoom(), dy / zoom());
    if (!sr.contains(vis))
        setSceneRect(sr.united(vis));
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + dx);
    verticalScrollBar()->setValue(verticalScrollBar()->value() + dy);
    QMouseEvent fake(QEvent::MouseMove, pos, viewport()->mapToGlobal(pos), Qt::NoButton, Qt::LeftButton, QApplication::keyboardModifiers());
    mouseMoveEvent(&fake);
}

void MindMapView::drawForeground(QPainter *p, const QRectF &rect)
{
    QGraphicsView::drawForeground(p, rect);
    if (m_mode == Mode::Marquee && !m_marquee.isNull()) {
        p->save();
        p->setPen(QPen(QColor(61, 174, 233), 1.0 / zoom(), Qt::DashLine));
        p->setBrush(QColor(61, 174, 233, 40));
        p->drawRect(m_marquee);
        p->restore();
    }
    if (m_ctl)
        m_ctl->drawPageGuides(p, rect);
}

void MindMapView::resizeEvent(QResizeEvent *e)
{
    QGraphicsView::resizeEvent(e);
    updateSceneRect();
}

// ------------------------------------------------------------------ drag and drop from elsewhere

void MindMapView::dragEnterEvent(QDragEnterEvent *e)
{
    if (m_ctl && !m_scene->doc()->locked() && m_ctl->canAccept(e->mimeData())) {
        e->acceptProposedAction();
        return;
    }
    QGraphicsView::dragEnterEvent(e);
}

void MindMapView::dragMoveEvent(QDragMoveEvent *e)
{
    if (m_ctl && !m_scene->doc()->locked() && m_ctl->canAccept(e->mimeData())) {
        e->acceptProposedAction();
        return;
    }
    QGraphicsView::dragMoveEvent(e);
}

void MindMapView::dropEvent(QDropEvent *e)
{
    if (m_ctl && !m_scene->doc()->locked() && m_ctl->canAccept(e->mimeData())) {
        const QPointF sp = mapToScene(e->position().toPoint());
        const QMimeData *md = e->mimeData();
        e->setDropAction(Qt::CopyAction);
        e->accept();
        // after the drag: a question (split the text?) must not run inside it
        auto *copy = new QMimeData;
        for (const QString &f : md->formats())
            copy->setData(f, md->data(f));
        if (md->hasUrls())
            copy->setUrls(md->urls());
        if (md->hasImage())
            copy->setImageData(md->imageData());
        const Qt::KeyboardModifiers mods = e->modifiers();
        QPointer<MindMapController> ctl = m_ctl;
        QTimer::singleShot(0, this, [ctl, copy, sp, mods] {
            if (ctl)
                ctl->dropData(copy, sp, mods);
            delete copy;
        });
        return;
    }
    QGraphicsView::dropEvent(e);
}

} // namespace qb
