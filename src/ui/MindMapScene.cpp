#include "ui/MindMapScene.h"
#include "core/IconCatalog.h"
#include "ui/MindMapDoc.h"
#include "ui/SpellChecker.h"

#include <QFocusEvent>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionGraphicsItem>
#include <QTextCursor>
#include <QTextDocument>
#include <QtMath>

namespace qb {

using namespace mindmap;

// ------------------------------------------------------------------ drawing helpers

namespace mindmapdraw {

QPainterPath bodyPath(const QRectF &r, BorderStyle style)
{
    QPainterPath p;
    switch (style) {
    case BorderStyle::Square: p.addRect(r); break;
    case BorderStyle::Rounded: p.addRoundedRect(r, qMin(7.0, r.height() / 2), qMin(7.0, r.height() / 2)); break;
    case BorderStyle::Jagged: {
        // little teeth along every side
        const double step = 6.0, depth = 2.4;
        auto side = [&](QPointF a, QPointF b, bool first) {
            const QLineF l(a, b);
            const int n = qMax(2, int(l.length() / step));
            const QPointF nrm = QPointF(-(b.y() - a.y()), b.x() - a.x()) / qMax(1.0, l.length());
            for (int i = 0; i <= n; ++i) {
                const QPointF pt = a + (b - a) * (double(i) / n) + ((i % 2) ? nrm * depth : QPointF());
                if (first && i == 0)
                    p.moveTo(pt);
                else
                    p.lineTo(pt);
            }
        };
        side(r.topLeft(), r.topRight(), true);
        side(r.topRight(), r.bottomRight(), false);
        side(r.bottomRight(), r.bottomLeft(), false);
        side(r.bottomLeft(), r.topLeft(), false);
        p.closeSubpath();
        break;
    }
    case BorderStyle::Cloud: {
        // round bumps along every side
        const double bump = 9.0;
        auto side = [&](QPointF a, QPointF b, bool first) {
            const QLineF l(a, b);
            const int n = qMax(1, int(std::round(l.length() / bump)));
            const QPointF nrm = QPointF(b.y() - a.y(), -(b.x() - a.x())) / qMax(1.0, l.length());
            if (first)
                p.moveTo(a);
            for (int i = 0; i < n; ++i) {
                const QPointF s = a + (b - a) * (double(i) / n), e = a + (b - a) * (double(i + 1) / n);
                const QPointF c = (s + e) / 2 + nrm * (l.length() / n) * 0.55;
                p.quadTo(c, e);
            }
        };
        const QRectF in = r.adjusted(3, 3, -3, -3);
        side(in.topLeft(), in.topRight(), true);
        side(in.topRight(), in.bottomRight(), false);
        side(in.bottomRight(), in.bottomLeft(), false);
        side(in.bottomLeft(), in.topLeft(), false);
        p.closeSubpath();
        break;
    }
    }
    return p;
}

} // namespace mindmapdraw

// ------------------------------------------------------------------ text item

void NoteTextItem::focusOutEvent(QFocusEvent *e)
{
    QGraphicsTextItem::focusOutEvent(e);
    // a context menu or a dialog takes the focus for a moment: editing goes on
    if (e->reason() != Qt::PopupFocusReason && e->reason() != Qt::ActiveWindowFocusReason)
        emit focusLost();
}

void NoteTextItem::keyPressEvent(QKeyEvent *e)
{
    const MindMapPrefs &prefs = MindMapPrefs::instance();
    if (e->key() == Qt::Key_Escape) {
        emit focusLost();
        return;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
        if (e->modifiers() & Qt::ShiftModifier) {
            textCursor().insertText(QStringLiteral("\n"));
            return;
        }
        if (prefs.enterEndsEditing && !(e->modifiers() & Qt::ControlModifier)) {
            emit focusLost();
            return;
        }
    }
    QGraphicsTextItem::keyPressEvent(e);
}

// ------------------------------------------------------------------ items

void BoardItem::setDropTarget(bool on)
{
    if (m_dropTarget == on)
        return;
    m_dropTarget = on;
    update();
}

// An icon drawn sharp at every zoom: the picture is taken at the size it has on the screen.
static void drawIcon(QPainter *p, const QIcon &icon, const QRectF &r)
{
    const double scale = qMax(1.0, std::sqrt(std::abs(p->transform().determinant())));
    const int side = qBound(8, int(std::ceil(r.width() * scale)), 256);
    const QPixmap pm = icon.pixmap(QSize(side, side));
    p->save();
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    p->drawPixmap(r, pm, QRectF(pm.rect()));
    p->restore();
}

QRectF BoardItem::boundingRect() const
{
    const double m = qMax(8.0, MindMapScene::iconSize() / 2 + 3);
    return frame().adjusted(-m, -m, m + 2, m + 2);
}

int BoardItem::iconPlaceAt(const QPointF &itemPos) const
{
    const double half = MindMapScene::iconSize() / 2 + 1;
    for (int i = 0; i < mindmap::kIconPositions; ++i) {
        const QPointF c = MindMapScene::iconPoint(frame(), i);
        if (QRectF(c - QPointF(half, half), QSizeF(2 * half, 2 * half)).contains(itemPos))
            return i;
    }
    return -1;
}

void BoardItem::paintIcons(QPainter *p, const IconMap &icons, const QRectF &r)
{
    if (icons.isEmpty())
        return;
    const double size = MindMapScene::iconSize();
    p->save();
    for (auto it = icons.cbegin(); it != icons.cend(); ++it) {
        const QIcon icon = m_scene->iconFor(it.value());
        if (icon.isNull())
            continue;
        QRectF ir(0, 0, size, size);
        ir.moveCenter(MindMapScene::iconPoint(r, it.key()));
        drawIcon(p, icon, ir);
    }
    p->restore();
}

QPainterPath BoardItem::shape() const
{
    QPainterPath p;
    p.addRect(frame());
    // the icons belong to the item for clicks too, also the halves outside its frame (0.21)
    const double half = MindMapScene::iconSize() / 2;
    const IconMap icons = m_scene->board().iconsOf(m_id);
    for (auto it = icons.cbegin(); it != icons.cend(); ++it)
        p.addRect(QRectF(MindMapScene::iconPoint(frame(), it.key()) - QPointF(half, half), QSizeF(2 * half, 2 * half)));
    return p.simplified();
}

void BoardItem::paintBody(QPainter *p, const Appearance &look, const QRectF &r, bool selected, double opacity)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setOpacity(opacity);
    const QPainterPath body = mindmapdraw::bodyPath(r, look.borderStyle);
    if (look.shadow) {
        p->save();
        p->translate(3, 3);
        p->fillPath(body, QColor(0, 0, 0, 60));
        p->restore();
    }
    QColor fill = look.fill;
    if (m_dropTarget)
        fill = (fill.isValid() ? fill : QColor(255, 255, 255)).darker(125);
    if (fill.isValid())
        p->fillPath(body, fill);
    else if (look.shadow)
        p->fillPath(body, m_scene->board().settings.background);
    if (look.borderWeight > 0) {
        QColor bc = look.borderColor.isValid() ? look.borderColor : m_scene->board().settings.effectiveTextColor();
        QPen pen(bc, look.borderWeight);
        pen.setJoinStyle(Qt::RoundJoin);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->drawPath(body);
    }
    if (selected && !m_scene->printing()) {
        // the halo of a selected item (it hides the border, as in Scapple)
        QColor halo(61, 174, 233, 150);
        QPen pen(halo, 3.5);
        p->setOpacity(1.0);
        p->setPen(pen);
        p->setBrush(QColor(61, 174, 233, 28));
        p->drawPath(mindmapdraw::bodyPath(r.adjusted(-2, -2, 2, 2), look.borderStyle == BorderStyle::Square ? BorderStyle::Square : BorderStyle::Rounded));
    }
    p->restore();
}

NoteItem::NoteItem(MindMapScene *scene, int id) : BoardItem(scene, id)
{
    setFlag(ItemIsSelectable);
    setAcceptHoverEvents(true);
    m_text = new NoteTextItem(this);
    m_text->setTextInteractionFlags(Qt::NoTextInteraction);
    m_text->document()->setUndoRedoEnabled(true);
    m_text->setOpenExternalLinks(false);
    connect(m_text->document(), &QTextDocument::contentsChanged, this, [this] {
        if (m_editing)
            relayout();
    });
    connect(m_text, &NoteTextItem::focusLost, this, [this] {
        if (m_editing)
            m_scene->endEditing();
    });
    sync();
}

void NoteItem::sync()
{
    const Board &b = m_scene->board();
    const Note *n = b.note(m_id);
    if (!n)
        return;
    setPos(n->pos);
    m_width = n->width;
    m_image = n->image;
    if (n->isImage()) {
        m_text->hide();
        m_height = n->height;
    } else {
        m_text->show();
        if (!m_editing) {
            b.fillDocument(m_text->document(), *n);
            m_text->setDefaultTextColor(m_scene->textColor(n->look));
        }
        relayout();
    }
    setOpacity(m_scene->isFaded(m_id) ? m_scene->fadedOpacity() : 1.0);
    update();
}

void NoteItem::relayout()
{
    const Note *n = m_scene->board().note(m_id);
    if (!n || n->isImage())
        return;
    const double indent = n->indent * kIndentStep;
    if (m_editing && MindMapPrefs::instance().autoSize) {
        // typing: a narrow note grows with its line up to the standard width of new notes
        const double limit = qMax(m_width, MindMapPrefs::instance().newNoteWidth);
        const double ideal = std::ceil(m_text->document()->idealWidth()) + 2 * kPadX + indent + 2;
        QTextDocument probe;
        probe.setDefaultFont(m_text->document()->defaultFont());
        probe.setDocumentMargin(0);
        probe.setPlainText(m_text->document()->toPlainText());
        probe.setTextWidth(-1);
        const double line = std::ceil(probe.idealWidth()) + 2 * kPadX + indent + 2;
        const double want = qMin(limit, qMax(ideal, line));
        if (want > m_width + 0.5) {
            prepareGeometryChange();
            m_width = want;
        }
    }
    m_text->setTextWidth(qMax(10.0, m_width - 2 * kPadX - indent));
    m_text->setPos(kPadX + (m_scene->board().settings.rightToLeft ? 0 : indent), kPadY);
    const double h = std::ceil(m_text->document()->size().height() + 2 * kPadY);
    if (qAbs(h - m_height) > 0.01) {
        prepareGeometryChange();
        m_height = h;
        if (m_editing) {
            // the notes stacked below follow while typing (the board learns it at the end)
            const Board &b = m_scene->board();
            const double dy = m_height - b.rectOf(m_id).height();
            for (int below : b.stackBelow(m_id))
                if (BoardItem *it = m_scene->itemFor(below))
                    if (const Note *bn = b.note(below))
                        it->setPos(bn->pos + QPointF(0, dy));
        }
    }
    m_scene->syncLines();
    update();
}

void NoteItem::setWidthLive(double w)
{
    prepareGeometryChange();
    m_width = qMax(30.0, w);
    if (!m_image.isNull()) {
        m_height = m_width * m_image.height() / qMax(1, m_image.width());
    }
    relayout();
    update();
}

void NoteItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    const Note *n = m_scene->board().note(m_id);
    if (!n)
        return;
    paintBody(p, n->look, frame(), isSelected() || m_editing, 1.0);
    if (!m_image.isNull()) {
        p->save();
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        p->drawImage(frame().adjusted(1, 1, -1, -1), m_image);
        p->restore();
    }
    paintIcons(p, n->icons, frame());
    if (m_editing && !m_scene->printing()) {
        p->save();
        p->setPen(QPen(QColor(61, 174, 233), 1.2, Qt::DashLine));
        p->setBrush(Qt::NoBrush);
        p->drawRect(frame().adjusted(0.5, 0.5, -0.5, -0.5));
        p->restore();
    }
}

void NoteItem::startEditing(bool selectAll)
{
    const Note *n = m_scene->board().note(m_id);
    if (!n || n->isImage() || m_editing)
        return;
    m_editing = true;
    m_text->setTextInteractionFlags(Qt::TextEditorInteraction | Qt::LinksAccessibleByMouse);
    SpellChecker::attach(m_text->document());
    m_text->setFocus(Qt::MouseFocusReason);
    QTextCursor c = m_text->textCursor();
    if (selectAll)
        c.select(QTextCursor::Document);
    else
        c.movePosition(QTextCursor::End);
    m_text->setTextCursor(c);
    setZValue(zValue()); // stays where it is
    update();
}

void NoteItem::endEditing()
{
    if (!m_editing)
        return;
    m_editing = false;
    QTextCursor c = m_text->textCursor();
    c.clearSelection();
    m_text->setTextCursor(c);
    m_text->setTextInteractionFlags(Qt::NoTextInteraction);
    m_text->clearFocus();
    update();
}

ShapeItem::ShapeItem(MindMapScene *scene, int id) : BoardItem(scene, id)
{
    setFlag(ItemIsSelectable);
    setAcceptHoverEvents(true);
    sync();
}

void ShapeItem::sync()
{
    const Shape *s = m_scene->board().shape(m_id);
    if (!s)
        return;
    prepareGeometryChange();
    setPos(s->rect.topLeft());
    m_size = s->rect.size();
    setOpacity(m_scene->isFaded(m_id) ? m_scene->fadedOpacity() : 1.0);
    update();
}

void ShapeItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    const Shape *s = m_scene->board().shape(m_id);
    if (!s)
        return;
    paintBody(p, s->look, frame(), isSelected(), 1.0);
    paintIcons(p, s->icons, frame());
    if (isSelected() && !m_scene->printing()) {
        // handles at the corners and in the middle of the sides
        p->save();
        p->setPen(QPen(QColor(40, 120, 190), 1));
        p->setBrush(Qt::white);
        const QRectF r = frame();
        for (const QPointF &pt : {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight(), QPointF(r.center().x(), r.top()),
                                  QPointF(r.center().x(), r.bottom()), QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y())})
            p->drawRect(QRectF(pt - QPointF(3, 3), QSizeF(6, 6)));
        p->restore();
    }
    if (s->magnetic && isSelected() && !m_scene->printing()) {
        p->save();
        QFont f = p->font();
        f.setPointSizeF(7.5);
        p->setFont(f);
        p->setPen(QColor(40, 120, 190));
        p->drawText(frame().adjusted(6, 4, -6, -4), Qt::AlignRight | Qt::AlignTop, QObject::tr("magnetic"));
        p->restore();
    }
}

// ------------------------------------------------------------------ lines

static QPainterPath arrowHead(const QPointF &tip, const QPointF &from, double size)
{
    const QLineF l(tip, from);
    const double a = std::atan2(-l.dy(), l.dx());
    QPainterPath p;
    p.moveTo(tip);
    p.lineTo(tip + QPointF(std::sin(a + M_PI / 3) * size, std::cos(a + M_PI / 3) * size));
    p.lineTo(tip + QPointF(std::sin(a + M_PI - M_PI / 3) * size, std::cos(a + M_PI - M_PI / 3) * size));
    p.closeSubpath();
    return p;
}

QRectF LinesItem::boundingRect() const { return m_bounds; }

void LinesItem::refresh()
{
    prepareGeometryChange();
    m_bounds = m_scene->contentRect().adjusted(-60, -60, 60, 60);
    update();
}

void LinesItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    const Board &b = m_scene->board();
    const MindMapPrefs &prefs = MindMapPrefs::instance();
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QColor ink = b.settings.effectiveTextColor();
    QSet<QString> done;
    auto drawLine = [&](int a, const QSet<int> &conn) {
        for (int other : conn) {
            const QString key = Board::lineKey(a, other);
            if (done.contains(key))
                continue;
            done.insert(key);
            const QRectF ra = m_scene->liveRect(a), rb = m_scene->liveRect(other);
            if (ra.isNull() || rb.isNull())
                continue;
            const QPointF pa = MindMapScene::edgePoint(ra, rb.center());
            const QPointF pb = MindMapScene::edgePoint(rb, ra.center());
            const bool toB = b.pointsTo(a, other), toA = b.pointsTo(other, a);
            const bool faded = m_scene->isFaded(a) && m_scene->isFaded(other);
            p->setOpacity(faded ? m_scene->fadedOpacity() : 1.0);
            const LineLook look = b.lineLook(a, other);   // color and middle icon (0.21)
            const QColor lineInk = look.color.isValid() ? look.color : ink;
            QPen pen(lineInk, look.color.isValid() ? 1.8 : 1.2);
            if (!toA && !toB && prefs.dashedLines)
                pen.setDashPattern({4, 3});
            p->setPen(pen);
            const double head = 8;
            QPointF s = pa, e = pb;
            const QLineF l(pa, pb);
            if (l.length() < 1)
                continue;
            const QPointF unit = (pb - pa) / l.length();
            if (toB)
                e -= unit * head * 0.8;
            if (toA)
                s += unit * head * 0.8;
            p->drawLine(s, e);
            p->setPen(Qt::NoPen);
            p->setBrush(lineInk);
            if (toB)
                p->drawPath(arrowHead(pb, pa, head));
            if (toA)
                p->drawPath(arrowHead(pa, pb, head));
            const double iconSide = MindMapScene::iconSize();
            if (!look.icon.isEmpty()) {
                const QIcon icon = m_scene->iconFor(look.icon);
                QRectF ir(0, 0, iconSide, iconSide);
                ir.moveCenter((pa + pb) / 2);
                // a disc of the paper behind the icon, so the line does not run through it
                p->setBrush(b.settings.background);
                p->drawEllipse(ir.adjusted(-2, -2, 2, 2));
                drawIcon(p, icon, ir);
            }
            const QString label = b.lineLabel(a, other);
            if (!label.isEmpty()) {
                QFont f(b.fontFamily());
                f.setPointSizeF(9);
                p->setFont(f);
                const QFontMetricsF fm(f);
                const QPointF c = (pa + pb) / 2;
                QRectF tr(0, 0, fm.horizontalAdvance(label) + 8, fm.height() + 2);
                tr.moveCenter(c);
                // with an icon in the middle, the label goes just below it
                if (!look.icon.isEmpty())
                    tr.moveTop(c.y() + iconSide / 2 + 3);
                QColor bg = b.settings.background;
                p->setBrush(bg);
                p->drawRoundedRect(tr, 3, 3);
                p->setPen(ink);
                p->drawText(tr, Qt::AlignCenter, label);
            }
        }
    };
    for (const Note &n : b.notes)
        drawLine(n.id, n.connected);
    for (const Shape &s : b.shapes)
        drawLine(s.id, s.connected);
    p->restore();
}

// ------------------------------------------------------------------ scene

MindMapScene::MindMapScene(MindMapDoc *doc) : QGraphicsScene(doc), m_doc(doc)
{
    m_lines = new LinesItem(this);
    m_lines->setZValue(-100);
    addItem(m_lines);
    connect(this, &QGraphicsScene::selectionChanged, this, &MindMapScene::onSelectionChanged);
}

MindMapScene::~MindMapScene()
{
    // the items go with QGraphicsScene's destructor: nobody is told about the
    // selection changes that brings (the lists below would be half gone)
    blockSignals(true);
    m_notes.clear();
    m_shapes.clear();
    m_order.clear();
}

Board &MindMapScene::board() const { return m_doc->board; }

void MindMapScene::rebuild()
{
    endEditing();
    const QList<int> sel = selectedIds();
    for (NoteItem *n : std::as_const(m_notes))
        delete n;
    for (ShapeItem *s : std::as_const(m_shapes))
        delete s;
    m_notes.clear();
    m_shapes.clear();
    m_order.clear();
    syncAll();
    QList<int> keep;
    for (int id : sel)
        if (board().exists(id))
            keep << id;
    selectIds(keep);
}

void MindMapScene::syncAll()
{
    Board &b = board();
    // shapes: below everything, in their order
    QSet<int> seen;
    int z = 0;
    for (const Shape &s : b.shapes) {
        seen.insert(s.id);
        ShapeItem *it = m_shapes.value(s.id);
        if (!it) {
            it = new ShapeItem(this, s.id);
            addItem(it);
            m_shapes.insert(s.id, it);
        }
        it->sync();
        it->setZValue(-1000 + z++);
    }
    for (auto it = m_shapes.begin(); it != m_shapes.end();) {
        if (!seen.contains(it.key())) {
            m_order.removeAll(it.key());
            delete it.value();
            it = m_shapes.erase(it);
        } else {
            ++it;
        }
    }
    seen.clear();
    z = 0;
    for (const Note &n : b.notes) {
        seen.insert(n.id);
        NoteItem *it = m_notes.value(n.id);
        if (!it) {
            it = new NoteItem(this, n.id);
            addItem(it);
            m_notes.insert(n.id, it);
        }
        it->sync();
        it->setZValue(z++);
    }
    for (auto it = m_notes.begin(); it != m_notes.end();) {
        if (!seen.contains(it.key())) {
            m_order.removeAll(it.key());
            delete it.value();
            it = m_notes.erase(it);
        } else {
            ++it;
        }
    }
    m_lines->refresh();
    update();
}

BoardItem *MindMapScene::itemFor(int id) const
{
    if (NoteItem *n = m_notes.value(id))
        return n;
    return m_shapes.value(id);
}

QRectF MindMapScene::liveRect(int id) const
{
    if (BoardItem *it = itemFor(id))
        return it->mapRectToScene(it->frame());
    return board().rectOf(id);
}

QList<int> MindMapScene::selectedIds() const
{
    QList<int> out;
    for (int id : m_order)
        if (BoardItem *it = itemFor(id); it && it->isSelected())
            out << id;
    // items selected without passing through the order (should not happen)
    for (QGraphicsItem *gi : selectedItems())
        if (auto *bi = dynamic_cast<BoardItem *>(gi); bi && !out.contains(bi->id()))
            out << bi->id();
    return out;
}

QList<int> MindMapScene::selectedNotes() const
{
    QList<int> out;
    for (int id : selectedIds())
        if (m_notes.contains(id))
            out << id;
    return out;
}

QList<int> MindMapScene::selectedShapes() const
{
    QList<int> out;
    for (int id : selectedIds())
        if (m_shapes.contains(id))
            out << id;
    return out;
}

int MindMapScene::firstSelected() const
{
    const QList<int> s = selectedIds();
    return s.isEmpty() ? 0 : s.first();
}

void MindMapScene::selectIds(const QList<int> &ids, bool add)
{
    {
        // the order is ours here (selectionChanged would append in the scene's order)
        const QSignalBlocker block(this);
        if (!add) {
            clearSelection();
            m_order.clear();
        }
        for (int id : ids)
            if (BoardItem *it = itemFor(id)) {
                it->setSelected(true);
                m_order.removeAll(id);
                m_order << id;
            }
    }
    emit selectionOrderChanged();
    update();
}

void MindMapScene::onSelectionChanged()
{
    QList<int> now;
    for (QGraphicsItem *gi : selectedItems())
        if (auto *bi = dynamic_cast<BoardItem *>(gi))
            now << bi->id();
    for (int i = m_order.size() - 1; i >= 0; --i)
        if (!now.contains(m_order[i]))
            m_order.removeAt(i);
    for (int id : now)
        if (!m_order.contains(id))
            m_order << id;
    emit selectionOrderChanged();
}

NoteItem *MindMapScene::editingNote() const
{
    for (NoteItem *n : m_notes)
        if (n->isEditing())
            return n;
    return nullptr;
}

void MindMapScene::startEditing(int id, bool selectAll)
{
    if (m_doc->locked())
        return;
    NoteItem *n = m_notes.value(id);
    if (!n)
        return;
    if (NoteItem *cur = editingNote(); cur && cur != n)
        endEditing();
    selectIds({id});
    n->startEditing(selectAll);
    emit editingChanged(true);
}

void MindMapScene::endEditing()
{
    NoteItem *n = editingNote();
    if (!n)
        return;
    const int id = n->id();
    Note *note = board().note(id);
    n->endEditing();
    if (note) {
        Note edited = *note;
        Board::readDocument(n->textItem()->document(), edited);
        const MindMapPrefs &prefs = MindMapPrefs::instance();
        const bool textChanged = edited.text != note->text || !(edited.ranges == note->ranges);
        double width = qMax(note->width, n->frame().width()); // it may have grown while typing
        if (prefs.autoSize && textChanged) {
            // a short note as narrow as its text; a longer one as wide as new notes (or as it was made)
            const double fit = board().fittingWidth(edited);
            if (fit <= width && !edited.text.contains(QLatin1Char('\n')))
                width = qMax(40.0, fit);
            else if (fit > width)
                width = qMin(fit, qMax(width, prefs.newNoteWidth));
        }
        if (textChanged || width != note->width) {
            const double oldBottom = board().rectOf(id).bottom();
            m_doc->change(QObject::tr("Typing"), [&] {
                Note *nn = board().note(id);
                nn->text = edited.text;
                nn->ranges = edited.ranges;
                nn->width = width;
                board().restack(id, oldBottom);
            });
        } else {
            n->sync();
        }
    }
    emit editingChanged(false);
}

std::pair<int, int> MindMapScene::lineAt(const QPointF &pos, double tol) const
{
    const Board &b = board();
    QSet<QString> done;
    std::pair<int, int> best{0, 0};
    double bestDist = tol;
    auto check = [&](int a, const QSet<int> &conn) {
        for (int o : conn) {
            const QString key = Board::lineKey(a, o);
            if (done.contains(key))
                continue;
            done.insert(key);
            const QRectF ra = liveRect(a), rb = liveRect(o);
            const QPointF pa = edgePoint(ra, rb.center()), pb = edgePoint(rb, ra.center());
            const QLineF l(pa, pb);
            if (l.length() < 1)
                continue;
            const QPointF d = pb - pa;
            const double t = qBound(0.0, QPointF::dotProduct(pos - pa, d) / QPointF::dotProduct(d, d), 1.0);
            const QPointF proj = pa + d * t;
            const double dist = QLineF(proj, pos).length();
            if (dist < bestDist) {
                bestDist = dist;
                best = {a, o};
            }
        }
    };
    for (const Note &n : b.notes)
        check(n.id, n.connected);
    for (const Shape &s : b.shapes)
        check(s.id, s.connected);
    return best;
}

QPointF MindMapScene::edgePoint(const QRectF &r, const QPointF &towards)
{
    const QPointF c = r.center();
    const QPointF d = towards - c;
    if (qFuzzyIsNull(d.x()) && qFuzzyIsNull(d.y()))
        return c;
    const double hw = r.width() / 2 + 2, hh = r.height() / 2 + 2;
    const double sx = qFuzzyIsNull(d.x()) ? 1e9 : hw / qAbs(d.x());
    const double sy = qFuzzyIsNull(d.y()) ? 1e9 : hh / qAbs(d.y());
    const double s = qMin(sx, sy);
    return c + d * qMin(s, 1.0);
}

QPointF MindMapScene::lineMiddle(int a, int b) const
{
    const QRectF ra = liveRect(a), rb = liveRect(b);
    return (edgePoint(ra, rb.center()) + edgePoint(rb, ra.center())) / 2;
}

QIcon MindMapScene::iconFor(const QString &iconId) const
{
    return icons::iconFor(m_doc ? m_doc->project() : nullptr, iconId);
}

double MindMapScene::iconSize() { return qBound(10, MindMapPrefs::instance().iconSize, 48); }

QPointF MindMapScene::iconPoint(const QRectF &r, int place)
{
    switch (IconPos(place)) {
    case IconPos::TopLeft: return r.topLeft();
    case IconPos::Top: return QPointF(r.center().x(), r.top());
    case IconPos::TopRight: return r.topRight();
    case IconPos::Right: return QPointF(r.right(), r.center().y());
    case IconPos::BottomRight: return r.bottomRight();
    case IconPos::Bottom: return QPointF(r.center().x(), r.bottom());
    case IconPos::BottomLeft: return r.bottomLeft();
    case IconPos::Left: return QPointF(r.left(), r.center().y());
    }
    return r.center();
}

QPointF MindMapScene::labelPos(int a, int b) const
{
    const QRectF ra = liveRect(a), rb = liveRect(b);
    return (edgePoint(ra, rb.center()) + edgePoint(rb, ra.center())) / 2;
}

double MindMapScene::fadedOpacity() const
{
    if (m_printing && m_printFadedFull)
        return 1.0;
    return 1.0 - MindMapPrefs::instance().fadedPercent / 100.0;
}

bool MindMapScene::isFaded(int id) const
{
    const Board &b = board();
    if (const Note *n = b.note(id); n && n->look.faded)
        return true;
    if (const Shape *s = b.shape(id); s && s->look.faded)
        return true;
    const QRectF r = liveRect(id);
    for (const Shape &s : b.shapes)
        if (s.id != id && s.look.faded && s.rect.intersects(r))
            return true;
    return false;
}

QColor MindMapScene::textColor(const Appearance &look) const
{
    if (m_printing && !m_printBackground && !look.textColor.isValid())
        return Qt::black;
    return look.textColor.isValid() ? look.textColor : board().settings.effectiveTextColor();
}

QRectF MindMapScene::contentRect() const
{
    QRectF r;
    for (NoteItem *n : m_notes)
        r = r.united(n->mapRectToScene(n->frame()));
    for (ShapeItem *s : m_shapes)
        r = r.united(s->mapRectToScene(s->frame()));
    return r;
}

void MindMapScene::drawBackground(QPainter *p, const QRectF &rect)
{
    if (m_printing && !m_printBackground) {
        p->fillRect(rect, Qt::white);
        return;
    }
    const Settings &s = board().settings;
    if (!s.texture.isNull())
        p->fillRect(rect, QBrush(s.texture));
    else
        p->fillRect(rect, s.background);
}

void MindMapScene::render(QPainter *p, const QRectF &target, const QRectF &source, bool background, bool fadedFull)
{
    const QList<int> sel = selectedIds();
    NoteItem *editing = editingNote();
    if (editing)
        endEditing();
    {
        const QSignalBlocker b(this);
        clearSelection();
    }
    m_printing = true;
    m_printBackground = background;
    m_printFadedFull = fadedFull;
    // text colors and faded notes as they are to be printed
    for (NoteItem *n : std::as_const(m_notes))
        n->sync();
    for (ShapeItem *s : std::as_const(m_shapes))
        s->sync();
    QGraphicsScene::render(p, target, source);
    m_printing = false;
    for (NoteItem *n : std::as_const(m_notes))
        n->sync();
    for (ShapeItem *s : std::as_const(m_shapes))
        s->sync();
    selectIds(sel);
}

} // namespace qb
