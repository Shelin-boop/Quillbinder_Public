#include "ui/Corkboard.h"
#include "ui/Zoom.h"
#include "core/Search.h"
#include "ui/DragDrop.h"

#include <QSet>
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"

#include <QApplication>
#include <QDrag>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QRandomGenerator>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionGraphicsItem>
#include <QTextLayout>

namespace qb {

// ------------------------------------------------------------------ card

CardItem::CardItem(Project *p, BinderItem *item, const QSizeF &size) : m_project(p), m_item(item), m_size(size)
{
    setFlag(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
}

QRectF CardItem::boundingRect() const { return QRectF(-4, -8, m_size.width() + 10, m_size.height() + 14); }

void CardItem::setCardSize(const QSizeF &s)
{
    prepareGeometryChange();
    m_size = s;
    update();
}

QRectF CardItem::iconRect() const { return QRectF(6, 6, 16, 16); }
QRectF CardItem::titleRect() const { return QRectF(26, 3, m_size.width() - 32, 22); }
QRectF CardItem::bodyRect() const { return QRectF(8, 30, m_size.width() - 16, m_size.height() - 36); }

QVariant CardItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemSelectedHasChanged)
        update();
    return QGraphicsObject::itemChange(change, value);
}

static void drawWrapped(QPainter *p, const QRectF &r, const QString &text, const QFont &font, const QColor &color)
{
    p->setFont(font);
    p->setPen(color);
    QTextLayout layout(text, font);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(opt);
    layout.beginLayout();
    qreal y = 0;
    const QFontMetricsF fm(font);
    QList<QTextLine> lines;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(r.width());
        if (y + fm.height() > r.height()) {
            break;
        }
        line.setPosition(QPointF(0, y));
        y += fm.height();
        lines << line;
    }
    layout.endLayout();
    for (int i = 0; i < lines.size(); ++i) {
        const QTextLine &l = lines[i];
        const bool last = i == lines.size() - 1 && l.textStart() + l.textLength() < text.size();
        if (last) {
            QString s = text.mid(l.textStart(), l.textLength()).trimmed() + QChar(0x2026);
            s = fm.elidedText(s, Qt::ElideRight, r.width());
            p->drawText(QPointF(r.left(), r.top() + l.y() + fm.ascent()), s);
        } else {
            l.draw(p, r.topLeft());
        }
    }
}

void CardItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    const AppSettings &s = AppSettings::instance();
    const QRectF r(0, 0, m_size.width(), m_size.height());
    p->setRenderHint(QPainter::Antialiasing);
    // shadow
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(0, 0, 0, 55));
    p->drawRoundedRect(r.translated(2, 3), 2, 2);

    const Label *label = m_project->labelById(m_item->label);
    QColor bg = theme::color(theme::Role::CardPaper);
    if (s.tintCardsWithLabel && label) {
        const QColor lc = label->color;
        bg = QColor((bg.red() * 3 + lc.red()) / 4, (bg.green() * 3 + lc.green()) / 4, (bg.blue() * 3 + lc.blue()) / 4);
    }
    const bool isImage = m_item->type == ItemType::Image;
    p->setBrush(bg);
    p->setPen(isSelected() ? QPen(QApplication::palette().color(QPalette::Highlight), 2.5) : QPen(QColor(0, 0, 0, 40), 1));
    p->drawRoundedRect(r, 2, 2);

    // header
    icons::forItem(m_project, m_item).paint(p, iconRect().toRect());
    const QFont tf = theme::font(theme::FontRole::CardTitle);
    p->setFont(tf);
    p->setPen(theme::color(theme::Role::CardTitle));
    const QString title = m_item->title.isEmpty() ? QObject::tr("Untitled") : m_item->title;
    p->drawText(titleRect(), Qt::AlignVCenter | Qt::AlignLeft,
                QFontMetrics(tf).elidedText(title, Qt::ElideRight, int(titleRect().width())));
    p->setPen(QPen(theme::color(theme::Role::CardTitleLine), 1));
    p->drawLine(QPointF(4, 27), QPointF(r.width() - 4, 27));

    const QRectF body = bodyRect();
    // a picture document, or a document whose index card has a picture (inspector), shows the picture
    QString picture;
    if (isImage)
        picture = m_project->mediaFilePath(m_item);
    else if (!m_item->synopsisImage.isEmpty())
        picture = m_project->contentDir(m_item->id) + QLatin1Char('/') + m_item->synopsisImage;
    if (!picture.isEmpty()) {
        QImage img(picture);
        if (!img.isNull()) {
            const QSizeF sz = img.size().scaled(body.size().toSize(), Qt::KeepAspectRatio);
            const QRectF ir(body.center().x() - sz.width() / 2, body.center().y() - sz.height() / 2, sz.width(), sz.height());
            p->drawImage(ir, img);
        }
    } else {
        // ruled lines
        QColor ruled = theme::color(theme::Role::CardLines);
        ruled.setAlpha(90);
        p->setPen(QPen(ruled, 1));
        QFont bfnt = theme::font(theme::FontRole::CardText);
        const qreal lh = QFontMetricsF(bfnt).height();
        for (qreal y = body.top() + lh; y < body.bottom(); y += lh)
            p->drawLine(QPointF(4, y + 1), QPointF(r.width() - 4, y + 1));
        QString text = m_item->synopsis;
        QColor col = theme::color(theme::Role::CardText);
        if (text.isEmpty() && m_item->hasText()) {
            text = text::previewText(m_project->docs()->plainText(m_item->id), 400);
            col.setAlpha(130); // text taken from the document, not a synopsis
            bfnt.setItalic(true);
        }
        drawWrapped(p, body, text, bfnt, col);
    }

    // keywords chips
    int kx = int(r.width()) - 8;
    for (const QString &k : m_item->keywords) {
        const Keyword *kw = m_project->keywordById(k);
        if (!kw)
            continue;
        kx -= 9;
        p->setPen(Qt::NoPen);
        p->setBrush(kw->color.isValid() ? kw->color : QColor(150, 150, 150));
        p->drawRect(QRectF(kx, r.height() - 9, 7, 5));
    }

    // pin with the label colour
    if (label) {
        const QPointF c(r.width() / 2, 1);
        p->setPen(QPen(label->color.darker(150), 1));
        QRadialGradient g(c + QPointF(-2, -2), 8);
        g.setColorAt(0, label->color.lighter(160));
        g.setColorAt(1, label->color);
        p->setBrush(g);
        p->drawEllipse(c, 6, 6);
    }

    // status stamp
    if (s.showStatusStamps) {
        if (const Status *st = m_project->statusById(m_item->status)) {
            p->save();
            p->translate(r.center());
            p->rotate(-18);
            QFont sf = QApplication::font();
            sf.setBold(true);
            sf.setPointSizeF(qMax(8.0, m_size.width() / 14.0));
            sf.setCapitalization(QFont::AllUppercase);
            p->setFont(sf);
            const QRectF tr = QFontMetricsF(sf).boundingRect(st->name);
            const QRectF box(-tr.width() / 2 - 6, -tr.height() / 2 - 2, tr.width() + 12, tr.height() + 4);
            QColor stamp = theme::color(theme::Role::StatusStamp);
            stamp.setAlpha(115);
            p->setPen(QPen(stamp, 2));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(box, 3, 3);
            p->setPen(stamp);
            p->drawText(box, Qt::AlignCenter, st->name);
            p->restore();
        }
    }
}

// ------------------------------------------------------------------ board

Corkboard::Corkboard(QWidget *parent) : QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    setAcceptDrops(true);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::NoDrag);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setFrameShape(QFrame::NoFrame);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    // Ctrl + mouse wheel (0.21): the cards and their text are shown larger or
    // smaller together (a scale of the whole board); the card size of the footer
    // stays what it is. The event filter of ui/Zoom.cpp sends the wheel steps.
    setTransform(QTransform::fromScale(zoom::factor(QLatin1String(zoom::kCorkboard)), zoom::factor(QLatin1String(zoom::kCorkboard))));
    connect(&zoom::notifier(), &zoom::Notifier::changed, this, [this](const QString &key, double f) {
        if (key != QLatin1String(zoom::kCorkboard))
            return;
        setTransform(QTransform::fromScale(f, f));
        if (m_project)
            layoutCards();
    });
    m_marker = new QGraphicsLineItem;
    m_marker->setPen(QPen(QColor(40, 110, 220), 3));
    m_marker->setZValue(100);
    m_marker->hide();
    scene()->addItem(m_marker);
    connect(scene(), &QGraphicsScene::selectionChanged, this, &Corkboard::selectionChanged);
    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(50);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &Corkboard::rebuild);

    updateTexture();
    connect(&theme::Notifier::instance(), &theme::Notifier::changed, this, [this] {
        updateTexture();
        for (CardItem *c : m_cards)
            c->update();
        viewport()->update();
    });
}

void Corkboard::setProject(Project *p)
{
    if (m_project)
        disconnect(m_project, nullptr, this, nullptr);
    if (m_project != p) {
        // the items of the old project are gone: never look at them again
        m_containers.clear();
        m_loose.clear();
    }
    m_project = p;
    if (p) {
        connect(p, &Project::itemChanged, this, [this](BinderItem *it) {
            for (CardItem *c : m_cards)
                if (c->item() == it)
                    c->update();
        });
        connect(p, &Project::settingsChanged, this, [this] {
            for (CardItem *c : m_cards)
                c->update();
        });
        auto schedule = [this] { m_rebuildTimer.start(); };
        connect(p->model(), &QAbstractItemModel::rowsInserted, this, schedule);
        connect(p->model(), &QAbstractItemModel::rowsRemoved, this, schedule);
        connect(p->model(), &QAbstractItemModel::rowsMoved, this, schedule);
        connect(p->model(), &QAbstractItemModel::modelReset, this, schedule);
    }
}

QList<BinderItem *> Corkboard::shownItems() const
{
    QList<BinderItem *> out;
    for (CardItem *c : m_cards)
        out << c->item();
    return out;
}

void Corkboard::setContent(const QList<BinderItem *> &containers, const QList<BinderItem *> &loose)
{
    m_containers = containers;
    m_loose = loose;
    rebuild();
    verticalScrollBar()->setValue(0);
}

void Corkboard::rebuild()
{
    finishEdit(false);
    const QList<BinderItem *> sel = selectedItems();
    {
        // A selected card that is taken away makes the scene announce a new
        // selection while the list of cards is half gone; whoever asks then
        // read deleted cards (crash, found by the menu audit of 0.14.0:
        // Navigate > Go To > Enclosing Group from a corkboard with a selected card).
        const QList<CardItem *> old = m_cards;
        m_cards.clear();
        QSignalBlocker block(scene());
        for (CardItem *c : old) {
            scene()->removeItem(c);
            delete c;
        }
    }
    for (QGraphicsItem *d : m_decor) {
        scene()->removeItem(d);
        delete d;
    }
    m_decor.clear();
    if (!m_project)
        return;
    // drop items that were deleted (compare pointers, a deleted item must not be read)
    const QList<BinderItem *> all = m_project->allItems();
    QSet<BinderItem *> alive(all.cbegin(), all.cend());
    alive.insert(m_project->root());
    QList<BinderItem *> valid;
    for (BinderItem *c : m_containers)
        if (alive.contains(c))
            valid << c;
    m_containers = valid;
    valid.clear();
    for (BinderItem *it : m_loose)
        if (alive.contains(it))
            valid << it;
    m_loose = valid;
    const QSizeF size(AppSettings::instance().cardWidth, qRound(AppSettings::instance().cardWidth * 0.62));
    m_unfilteredCount = 0;
    auto addCard = [&](BinderItem *it) {
        ++m_unfilteredCount;
        if (m_filter.isActive() && !search::matches(m_project, it, m_filter))
            return;
        auto *card = new CardItem(m_project, it, size);
        scene()->addItem(card);
        m_cards << card;
        if (m_mode == Mode::Freeform)
            card->setFlag(QGraphicsItem::ItemIsMovable);
    };
    m_containerCards.clear();
    for (BinderItem *c : m_containers) {
        const int before = int(m_cards.size());
        for (BinderItem *ch : c->children)
            addCard(ch);
        m_containerCards << int(m_cards.size()) - before;
    }
    for (BinderItem *it : m_loose)
        addCard(it);
    layoutCards();
    selectItems(sel);
    if (selectedItems() != sel)
        emit scene()->selectionChanged();
}

void Corkboard::setFilter(const ItemFilter &f)
{
    const bool was = m_filter.isActive();
    m_filter = f;
    if (was || f.isActive())
        rebuild();
}

void Corkboard::filterCounts(int *shown, int *total) const
{
    *shown = int(m_cards.size());
    *total = m_unfilteredCount;
}

QSize Corkboard::layoutSize() const
{
    // The room for the cards as if both scroll bars were shown, so that the
    // layout does not depend on them: otherwise a board just at the edge
    // switched them on and off for ever (100 % processor; found in 0.11.0,
    // there in every version with the corkboard).
    const int ext = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this);
    const int fullW = viewport()->width() + (verticalScrollBar()->isVisible() ? verticalScrollBar()->width() : 0);
    const int fullH = viewport()->height() + (horizontalScrollBar()->isVisible() ? horizontalScrollBar()->height() : 0);
    // the board is shown scaled (zoom): it has that much less room in its own units
    const double z = qMax(0.1, transform().m11());
    return QSize(qMax(1, int((fullW - ext) / z)), qMax(1, int((fullH - ext) / z)));
}

void Corkboard::layoutCards()
{
    m_groupRects.clear();
    for (QGraphicsItem *d : m_decor) {
        scene()->removeItem(d);
        delete d;
    }
    m_decor.clear();
    const qreal margin = 24, spacing = 20;
    const qreal w = AppSettings::instance().cardWidth;
    const qreal h = qRound(w * 0.62);
    for (CardItem *c : m_cards)
        c->setCardSize(QSizeF(w, h));
    const QSize room = layoutSize();
    const qreal avail = qMax(w, room.width() - 2 * margin);
    int perRow = AppSettings::instance().cardsPerRow;
    if (perRow <= 0)
        perRow = qMax(1, int((avail + spacing) / (w + spacing)));
    qreal y = margin;
    qreal maxX = room.width();

    if (m_mode == Mode::LabelThreads) {
        // one lane per label, cards in binder order along the lane
        QList<int> labelIds{-1};
        for (const Label &l : m_project->labels)
            labelIds << l.id;
        QHash<int, qreal> laneY;
        for (int li = 0; li < labelIds.size(); ++li) {
            const qreal ly = margin + li * (h + 50);
            laneY.insert(labelIds[li], ly);
            const Label *lab = m_project->labelById(labelIds[li]);
            const QColor col = lab ? lab->color : QColor(120, 120, 120);
            auto *line = scene()->addLine(0, ly + h / 2, 10000, ly + h / 2, QPen(col, 6));
            line->setZValue(-1);
            line->setOpacity(0.7);
            m_decor << line;
            auto *t = scene()->addSimpleText(lab ? lab->name : tr("No Label"));
            QFont f = font();
            f.setBold(true);
            t->setFont(f);
            t->setBrush(Qt::white);
            t->setPos(margin, ly - 22);
            m_decor << t;
        }
        for (int i = 0; i < m_cards.size(); ++i) {
            CardItem *c = m_cards[i];
            const int lid = laneY.contains(c->item()->label) ? c->item()->label : -1;
            c->setPos(margin + 90 + i * (w * 0.8 + spacing), laneY.value(lid));
            maxX = qMax(maxX, c->pos().x() + w + margin);
        }
        y = margin + labelIds.size() * (h + 50);
        scene()->setSceneRect(0, 0, maxX, y + margin);
        return;
    }

    if (m_mode == Mode::Freeform) {
        int i = 0;
        for (CardItem *c : m_cards) {
            const QStringList xy = c->item()->corkboardPos.split(QLatin1Char(','));
            QPointF pos;
            if (xy.size() == 2)
                pos = QPointF(xy[0].toDouble(), xy[1].toDouble());
            else
                pos = QPointF(margin + (i % perRow) * (w + spacing), margin + (i / perRow) * (h + spacing));
            c->setPos(pos);
            maxX = qMax(maxX, pos.x() + w + margin);
            y = qMax(y, pos.y() + h + margin);
            ++i;
        }
        scene()->setSceneRect(0, 0, maxX, y + 200);
        return;
    }

    // grid, possibly several stacked groups
    const bool stacked = m_containers.size() > 1 || (!m_containers.isEmpty() && !m_loose.isEmpty());
    int cardIndex = 0;
    auto layoutGroup = [&](BinderItem *container, int count, const QString &title) {
        if (stacked) {
            auto *t = scene()->addSimpleText(title);
            QFont f = font();
            f.setBold(true);
            f.setPointSizeF(f.pointSizeF() * 1.15);
            t->setFont(f);
            const QColor board = theme::color(theme::Role::CorkboardBackground);
            t->setBrush(board.lightness() < 128 ? QColor(235, 228, 215) : QColor(60, 40, 20));
            t->setPos(margin, y);
            m_decor << t;
            y += 30;
        }
        QList<QRectF> rects;
        for (int i = 0; i < count; ++i) {
            CardItem *c = m_cards[cardIndex++];
            const QPointF pos(margin + (i % perRow) * (w + spacing), y + (i / perRow) * (h + spacing));
            c->setPos(pos);
            rects << QRectF(pos, QSizeF(w, h));
        }
        const int rows = qMax(1, (count + perRow - 1) / perRow);
        m_groupRects.append({container, rects});
        y += rows * (h + spacing) + (stacked ? 16 : 0);
        if (stacked) {
            auto *line = scene()->addLine(margin, y - 8, margin + perRow * (w + spacing) - spacing, y - 8,
                                          QPen(theme::color(theme::Role::CorkboardBackground).lightness() < 128
                                                   ? QColor(230, 220, 200, 110)
                                                   : QColor(90, 60, 30, 120),
                                               2));
            m_decor << line;
            y += 8;
        }
    };
    for (int i = 0; i < m_containers.size(); ++i)
        layoutGroup(m_containers[i], m_containerCards.value(i), m_containers[i]->title);
    if (!m_loose.isEmpty())
        layoutGroup(nullptr, int(m_cards.size()) - cardIndex, tr("Selection"));
    maxX = qMax<qreal>(maxX, margin * 2 + perRow * (w + spacing) - spacing);
    scene()->setSceneRect(0, 0, maxX - 2, qMax<qreal>(y + margin, room.height() - 2));
}

// The board: the theme's corkboard color with a little grain.
void Corkboard::updateTexture()
{
    const QColor base = theme::color(theme::Role::CorkboardBackground);
    if (base == m_textureColor && !m_texture.isNull())
        return;
    m_textureColor = base;
    QImage tex(160, 160, QImage::Format_RGB32);
    QRandomGenerator rng(20260926); // the same grain every time
    for (int y = 0; y < tex.height(); ++y)
        for (int x = 0; x < tex.width(); ++x) {
            const int d = int(rng.bounded(28)) - 14;
            tex.setPixel(x, y, qRgb(qBound(0, base.red() + d, 255), qBound(0, base.green() + d, 255),
                                    qBound(0, base.blue() + d / 2, 255)));
        }
    m_texture = QPixmap::fromImage(tex);
    setBackgroundBrush(Qt::NoBrush);
    resetCachedContent();
}

void Corkboard::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->fillRect(rect, QBrush(m_texture));
}

void Corkboard::resizeEvent(QResizeEvent *e)
{
    QGraphicsView::resizeEvent(e);
    if (m_mode == Mode::Grid)
        layoutCards();
}

QList<BinderItem *> Corkboard::selectedItems() const
{
    QList<BinderItem *> out;
    for (CardItem *c : m_cards)
        if (c->isSelected())
            out << c->item();
    return out;
}

void Corkboard::selectItems(const QList<BinderItem *> &items)
{
    QSignalBlocker b(scene());
    for (CardItem *c : m_cards)
        c->setSelected(items.contains(c->item()));
    for (CardItem *c : m_cards)
        if (c->isSelected()) {
            ensureVisible(c);
            break;
        }
}

void Corkboard::setMode(Mode m)
{
    if (m_mode == m)
        return;
    m_mode = m;
    rebuild();
}

void Corkboard::commitFreeformOrder()
{
    if (!m_project || m_containers.size() != 1 || m_filter.isActive())
        return; // with a filter, the cards shown are not all there is to put in order
    BinderItem *container = m_containers.first();
    QList<CardItem *> cards = m_cards;
    std::sort(cards.begin(), cards.end(), [](CardItem *a, CardItem *b) {
        const qreal ay = qRound(a->pos().y() / 40.0), by = qRound(b->pos().y() / 40.0);
        if (ay != by)
            return ay < by;
        return a->pos().x() < b->pos().x();
    });
    int row = 0;
    for (CardItem *c : cards) {
        if (c->item()->parent == container)
            m_project->model()->moveItems({c->item()}, container, row++);
    }
}

CardItem *Corkboard::cardAt(const QPoint &viewPos) const
{
    for (QGraphicsItem *gi : items(viewPos))
        if (auto *c = dynamic_cast<CardItem *>(gi))
            return c;
    return nullptr;
}

void Corkboard::mousePressEvent(QMouseEvent *e)
{
    finishEdit(true);
    setFocus();
    m_pressPos = e->pos();
    m_pressCard = cardAt(e->pos());
    m_dragging = false;
    if (m_pressCard && e->button() == Qt::LeftButton && m_mode != Mode::Freeform) {
        // manual selection handling so that a drag keeps a multi selection
        if (e->modifiers() & Qt::ControlModifier) {
            m_pressCard->setSelected(!m_pressCard->isSelected());
        } else if (e->modifiers() & Qt::ShiftModifier) {
            const int a = m_cards.indexOf(m_pressCard);
            int b = a;
            for (int i = 0; i < m_cards.size(); ++i)
                if (m_cards[i]->isSelected())
                    b = i;
            for (int i = qMin(a, b); i <= qMax(a, b); ++i)
                m_cards[i]->setSelected(true);
        } else if (!m_pressCard->isSelected()) {
            scene()->clearSelection();
            m_pressCard->setSelected(true);
        }
        return;
    }
    if (!m_pressCard && e->button() == Qt::LeftButton && m_mode == Mode::Freeform)
        setDragMode(QGraphicsView::RubberBandDrag);
    QGraphicsView::mousePressEvent(e);
}

void Corkboard::mouseMoveEvent(QMouseEvent *e)
{
    if (m_pressCard && (e->buttons() & Qt::LeftButton) && m_mode != Mode::Freeform && !m_dragging
        && (e->pos() - m_pressPos).manhattanLength() > QApplication::startDragDistance()) {
        m_dragging = true;
        const QList<BinderItem *> items = selectedItems();
        auto *drag = new QDrag(this);
        drag->setMimeData(BinderModel::mimeForItems(items, m_project));
        QPixmap pm(m_pressCard->cardSize().toSize());
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            p.setOpacity(0.8);
            QStyleOptionGraphicsItem opt;
            m_pressCard->paint(&p, &opt, nullptr);
        }
        drag->setPixmap(pm.scaledToWidth(qMin(160, pm.width()), Qt::SmoothTransformation));
        drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
        m_pressCard = nullptr;
        m_marker->hide();
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void Corkboard::mouseReleaseEvent(QMouseEvent *e)
{
    QGraphicsView::mouseReleaseEvent(e);
    setDragMode(QGraphicsView::NoDrag);
    if (m_pressCard && !m_dragging && m_mode != Mode::Freeform && e->button() == Qt::LeftButton
        && !(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
        scene()->clearSelection();
        m_pressCard->setSelected(true);
    }
    if (m_mode == Mode::Freeform && m_project) {
        for (CardItem *c : m_cards) {
            const QString pos = QStringLiteral("%1,%2").arg(qRound(c->pos().x())).arg(qRound(c->pos().y()));
            if (c->item()->corkboardPos != pos) {
                c->item()->corkboardPos = pos;
                m_project->markIndexDirty();
            }
        }
    }
    m_pressCard = nullptr;
}

void Corkboard::mouseDoubleClickEvent(QMouseEvent *e)
{
    CardItem *c = cardAt(e->pos());
    if (!c) {
        // File > Options > Behaviors > Double-Clicking: a new card (0.21)
        if (e->button() == Qt::LeftButton && AppSettings::instance().corkDoubleClickNew && m_project) {
            scene()->clearSelection();
            setFocus();
            emit newCardRequested();
            return;
        }
        QGraphicsView::mouseDoubleClickEvent(e);
        return;
    }
    const QPointF local = c->mapFromScene(mapToScene(e->pos()));
    if (c->iconRect().adjusted(-4, -4, 4, 4).contains(local)) {
        emit openRequested(c->item());
        return;
    }
    startEdit(c, c->titleRect().adjusted(-30, -4, 0, 2).contains(local));
}

void Corkboard::editTitleOf(BinderItem *item)
{
    for (CardItem *c : m_cards)
        if (c->item() == item) {
            ensureVisible(c);
            startEdit(c, true);
            return;
        }
}

void Corkboard::startEdit(CardItem *card, bool title)
{
    finishEdit(true);
    m_editCard = card;
    const QRectF local = title ? card->titleRect() : card->bodyRect();
    const QRect vr = mapFromScene(card->mapToScene(local)).boundingRect();
    if (title) {
        if (!m_titleEdit) {
            m_titleEdit = new QLineEdit(viewport());
            m_titleEdit->installEventFilter(this);
            connect(m_titleEdit, &QLineEdit::returnPressed, this, [this] { finishEdit(true); });
            connect(m_titleEdit, &QLineEdit::editingFinished, this, [this] { finishEdit(true); });
        }
        QPalette ep = m_titleEdit->palette();
        ep.setColor(QPalette::Base, theme::color(theme::Role::CardPaper));
        ep.setColor(QPalette::Text, theme::color(theme::Role::CardTitle));
        m_titleEdit->setPalette(ep);
        m_titleEdit->setFont(theme::font(theme::FontRole::CardTitle));
        m_titleEdit->setText(card->item()->title);
        m_titleEdit->setGeometry(vr.adjusted(-2, -1, 2, 1));
        m_titleEdit->show();
        m_titleEdit->setFocus();
        m_titleEdit->selectAll();
    } else {
        if (!m_bodyEdit) {
            m_bodyEdit = new QPlainTextEdit(viewport());
            m_bodyEdit->setPlaceholderText(tr("Synopsis"));
            m_bodyEdit->installEventFilter(this); // Return ends editing (File > Options > Behaviors)
        }
        QPalette ep = m_bodyEdit->palette();
        ep.setColor(QPalette::Base, theme::color(theme::Role::CardPaper));
        ep.setColor(QPalette::Text, theme::color(theme::Role::CardText));
        m_bodyEdit->setPalette(ep);
        m_bodyEdit->setFont(theme::font(theme::FontRole::CardText));
        m_bodyEdit->setPlainText(card->item()->synopsis);
        m_bodyEdit->setGeometry(vr.adjusted(-2, -2, 2, 2));
        m_bodyEdit->show();
        m_bodyEdit->setFocus();
        m_bodyEdit->moveCursor(QTextCursor::End);
        // commit when focus leaves
        if (!m_bodyHooked) {
            m_bodyHooked = true;
            connect(qApp, &QApplication::focusChanged, m_bodyEdit, [this](QWidget *, QWidget *now) {
                if (m_bodyEdit && m_bodyEdit->isVisible() && now != m_bodyEdit)
                    finishEdit(true);
            });
        }
    }
}

void Corkboard::finishEdit(bool commit)
{
    if (!m_editCard) {
        if (m_titleEdit)
            m_titleEdit->hide();
        if (m_bodyEdit)
            m_bodyEdit->hide();
        return;
    }
    CardItem *card = m_editCard;
    m_editCard = nullptr;
    if (m_titleEdit && m_titleEdit->isVisible()) {
        m_titleEdit->hide();
        if (commit && m_project && m_titleEdit->text() != card->item()->title)
            m_project->model()->setData(m_project->model()->indexFor(card->item()), m_titleEdit->text(), Qt::EditRole);
    }
    if (m_bodyEdit && m_bodyEdit->isVisible()) {
        m_bodyEdit->hide();
        if (commit && m_project && m_bodyEdit->toPlainText() != card->item()->synopsis) {
            card->item()->synopsis = m_bodyEdit->toPlainText();
            m_project->notifyItemChanged(card->item(), false);
        }
    }
    card->update();
    setFocus();
}

// File > Options > Behaviors > Return Key (0.21): Return ends editing a synopsis,
// Shift+Return makes a new line (off: Return makes a new line, as before 0.21).
bool Corkboard::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_bodyEdit && e->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(e);
        if ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && !(k->modifiers() & Qt::ShiftModifier)
            && AppSettings::instance().returnEndsSynopsis) {
            finishEdit(true);
            return true;
        }
        if (k->key() == Qt::Key_Escape) {
            finishEdit(false);
            return true;
        }
    }
    return QGraphicsView::eventFilter(o, e);
}

void Corkboard::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        finishEdit(false);
        return;
    }
    if (e->key() == Qt::Key_Delete) {
        emit deleteRequested();
        return;
    }
    if (e->key() == Qt::Key_Space && e->modifiers() == Qt::NoModifier && !selectedItems().isEmpty()) {
        emit spacePressed(); // Quick Reference panels for the selected cards
        return;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !selectedItems().isEmpty()) {
        // File > Options > Behaviors > Return Key: a new card after the selected one (0.21)
        if (AppSettings::instance().returnCreatesItem && e->modifiers() == Qt::NoModifier) {
            emit newCardRequested();
            return;
        }
        for (CardItem *c : m_cards)
            if (c->isSelected()) {
                startEdit(c, true);
                break;
            }
        return;
    }
    const int dx = e->key() == Qt::Key_Left ? -1 : e->key() == Qt::Key_Right ? 1 : 0;
    const int dy = e->key() == Qt::Key_Up ? -1 : e->key() == Qt::Key_Down ? 1 : 0;
    if ((dx || dy) && !m_cards.isEmpty()) {
        int cur = -1;
        for (int i = 0; i < m_cards.size(); ++i)
            if (m_cards[i]->isSelected())
                cur = i;
        int next = 0;
        if (cur >= 0) {
            const qreal w = AppSettings::instance().cardWidth + 20;
            const int perRow = AppSettings::instance().cardsPerRow > 0 ? AppSettings::instance().cardsPerRow
                                                                        : qMax(1, int((qMax<qreal>(w - 20, layoutSize().width() - 48) + 20) / w));
            next = qBound(0, cur + dx + dy * perRow, int(m_cards.size()) - 1);
        }
        scene()->clearSelection();
        m_cards[next]->setSelected(true);
        ensureVisible(m_cards[next]);
        return;
    }
    QGraphicsView::keyPressEvent(e);
}

void Corkboard::contextMenuEvent(QContextMenuEvent *e)
{
    CardItem *c = cardAt(e->pos());
    if (c && !c->isSelected()) {
        scene()->clearSelection();
        c->setSelected(true);
    }
    emit contextMenuRequested(e->globalPos());
}

void Corkboard::wheelEvent(QWheelEvent *e)
{
    // Ctrl + mouse wheel zooms (0.21, ui/Zoom.cpp catches it before it comes here)
    QGraphicsView::wheelEvent(e);
}

Corkboard::Slot Corkboard::slotAt(const QPointF &sp) const
{
    Slot s = visibleSlotAt(sp);
    if (!m_filter.isActive() || !s.container || s.row < 0 || m_mode == Mode::LabelThreads)
        return s;
    // the place among the cards shown -> the place among all documents of the group
    QList<BinderItem *> shown;
    for (CardItem *c : m_cards)
        if (c->item()->parent == s.container)
            shown << c->item();
    if (s.row < shown.size())
        s.row = shown[s.row]->row();
    else
        s.row = shown.isEmpty() ? int(s.container->children.size()) : shown.last()->row() + 1;
    return s;
}

Corkboard::Slot Corkboard::visibleSlotAt(const QPointF &sp) const
{
    Slot s{nullptr, -1, QPointF()};
    if (m_mode == Mode::LabelThreads && m_containers.size() == 1) {
        BinderItem *container = m_containers.first();
        s.container = container;
        s.row = container->children.size();
        for (CardItem *c : m_cards) {
            if (sp.x() < c->pos().x() + c->cardSize().width() / 2) {
                s.row = c->item()->row();
                s.marker = c->pos();
                return s;
            }
        }
        return s;
    }
    for (const auto &g : m_groupRects) {
        const QList<QRectF> &rects = g.second;
        if (rects.isEmpty()) {
            s.container = g.first;
            s.row = 0;
            s.marker = QPointF(24, 24);
            continue;
        }
        const qreal top = rects.first().top() - 20;
        const qreal bottom = rects.last().bottom() + 20;
        if (sp.y() < top || sp.y() > bottom)
            continue;
        s.container = g.first;
        for (int i = 0; i < rects.size(); ++i) {
            const QRectF &r = rects[i];
            if ((sp.y() < r.bottom() + 10 && sp.x() < r.center().x()) || sp.y() < r.top() - 10) {
                s.row = i;
                s.marker = QPointF(r.left() - 10, r.top());
                return s;
            }
        }
        s.row = rects.size();
        s.marker = QPointF(rects.last().right() + 10, rects.last().top());
        return s;
    }
    if (!m_groupRects.isEmpty()) {
        const auto &g = m_groupRects.last();
        s.container = g.first;
        s.row = g.second.size();
        if (!g.second.isEmpty())
            s.marker = QPointF(g.second.last().right() + 10, g.second.last().top());
    }
    return s;
}

void Corkboard::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasFormat(QString::fromLatin1(kItemsMime)) || e->mimeData()->hasUrls()
        || e->mimeData()->hasFormat(QString::fromLatin1(kKeywordsMime)))
        e->acceptProposedAction();
    else
        e->ignore();
}

void Corkboard::dragMoveEvent(QDragMoveEvent *e)
{
    if (e->mimeData()->hasFormat(QString::fromLatin1(kKeywordsMime))) {
        // keywords go onto a card
        m_marker->hide();
        if (cardAt(e->position().toPoint())) {
            e->setDropAction(Qt::CopyAction);
            e->accept();
        } else {
            e->ignore();
        }
        return;
    }
    const Slot s = slotAt(mapToScene(e->position().toPoint()));
    if (!s.container) {
        m_marker->hide();
        e->ignore();
        return;
    }
    const qreal h = qRound(AppSettings::instance().cardWidth * 0.62);
    m_marker->setLine(s.marker.x(), s.marker.y(), s.marker.x(), s.marker.y() + h);
    m_marker->show();
    e->acceptProposedAction();
}

void Corkboard::dragLeaveEvent(QDragLeaveEvent *e)
{
    m_marker->hide();
    QGraphicsView::dragLeaveEvent(e);
}

void Corkboard::dropEvent(QDropEvent *e)
{
    m_marker->hide();
    if (!m_project)
        return;
    if (e->mimeData()->hasFormat(QString::fromLatin1(kKeywordsMime))) {
        if (CardItem *card = cardAt(e->position().toPoint())) {
            dnd::dropKeywords(m_project, card->item(), selectedItems(), e->mimeData(), e->modifiers());
            e->setDropAction(Qt::CopyAction);
            e->accept();
            rebuild();
        }
        return;
    }
    const QPointF sp = mapToScene(e->position().toPoint());
    const Slot s = slotAt(sp);
    if (!s.container)
        return;
    if (e->mimeData()->hasFormat(QString::fromLatin1(kItemsMime))) {
        const QList<BinderItem *> items = BinderModel::itemsFromMime(e->mimeData(), m_project);
        if (m_mode == Mode::LabelThreads) {
            // vertical position picks the label
            QList<int> labelIds{-1};
            for (const Label &l : m_project->labels)
                labelIds << l.id;
            const qreal h = qRound(AppSettings::instance().cardWidth * 0.62);
            const int lane = qBound(0, int((sp.y() - 24) / (h + 50)), int(labelIds.size()) - 1);
            for (BinderItem *it : items) {
                it->label = labelIds[lane];
                m_project->notifyItemChanged(it, false);
            }
        }
        m_project->model()->moveItems(items, s.container, s.row);
        e->acceptProposedAction();
        rebuild();
        selectItems(items);
        return;
    }
    if (e->mimeData()->hasUrls()) {
        emit filesDropped(s.container, s.row, e->mimeData()->urls());
        e->acceptProposedAction();
    }
}

} // namespace qb
