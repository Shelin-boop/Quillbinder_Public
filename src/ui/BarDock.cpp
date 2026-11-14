#include "ui/BarDock.h"

#include "ui/GripBar.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QLayout>
#include <QMainWindow>
#include <QPainter>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>

namespace qb {

using namespace barplaces;

namespace {

constexpr int kSlack = 6; // the mouse may leave its row's height by this much and still count as in the row
constexpr int kZone = 28; // the strip under the top rows and over the bottom edge that takes a toolbar into a new row

// The line that shows where a dragged toolbar will stand. It paints itself, so no theme's style sheet changes it.
class Marker : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), palette().color(QPalette::Highlight));
    }
};

} // namespace

BarDock::BarDock(QMainWindow *window, const QList<GripBar *> &bars, const QList<QStringList> &standardTopRows)
    : QObject(window), m_window(window), m_bars(bars), m_standard(fromRows(standardTopRows)), m_places(m_standard)
{
    setObjectName(QStringLiteral("barDock"));
    m_marker = new Marker(window);
    m_marker->setObjectName(QStringLiteral("barMarker"));
    m_marker->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_marker->hide();
    m_refitTimer = new QTimer(this);
    m_refitTimer->setSingleShot(true);
    m_refitTimer->setInterval(0);
    connect(m_refitTimer, &QTimer::timeout, this, &BarDock::refit);
    for (GripBar *bar : bars) {
        connect(bar, &GripBar::gripPressed, this, [this, bar](const QPoint &pos) { pressed(bar, pos); });
        connect(bar, &GripBar::gripMoved, this, [this, bar](const QPoint &pos) { movedTo(bar, pos); });
        connect(bar, &GripBar::gripReleased, this, [this, bar](const QPoint &pos) { released(bar, pos); });
        connect(bar, &GripBar::contentsChanged, this, [this] { scheduleRefit(); });
        bar->installEventFilter(this);
    }
    window->installEventFilter(this);
}

GripBar *BarDock::barNamed(const QString &name) const
{
    for (GripBar *bar : m_bars)
        if (bar->objectName() == name)
            return bar;
    return nullptr;
}

QSet<QString> BarDock::hiddenBars() const
{
    QSet<QString> hidden;
    for (GripBar *bar : m_bars)
        if (bar->isHidden())
            hidden.insert(bar->objectName());
    return hidden;
}

int BarDock::smallestLead() const { return m_bars.isEmpty() ? 0 : m_bars.first()->smallestGap(); }

void BarDock::scheduleRefit()
{
    if (!m_arranging && !m_refitTimer->isActive())
        m_refitTimer->start();
}

bool BarDock::eventFilter(QObject *o, QEvent *e)
{
    // Esc ends a drag without a change (the filter is on the application only while a drag runs)
    if (m_drag.started && (e->type() == QEvent::KeyPress || e->type() == QEvent::ShortcutOverride)
        && static_cast<QKeyEvent *>(e)->key() == Qt::Key_Escape) {
        if (e->type() == QEvent::ShortcutOverride)
            e->accept(); // (the key press follows; no command with Esc as its shortcut runs)
        else
            cancelDrag();
        return true;
    }
    // a drag whose end the program may never hear of (the window lost the mouse, the toolbar went away): no change
    if (m_drag.started && ((o == m_window && e->type() == QEvent::WindowDeactivate) || (o == m_drag.bar && e->type() == QEvent::Hide))) {
        cancelDrag();
        return false;
    }
    if (o == m_window) {
        if (e->type() == QEvent::Resize)
            scheduleRefit();
    } else if (e->type() == QEvent::ShowToParent || e->type() == QEvent::HideToParent || e->type() == QEvent::LayoutRequest) {
        if (m_bars.contains(static_cast<GripBar *>(o))) // (compared as pointers only)
            scheduleRefit();
    }
    return false;
}

// The toolbars into the window in the places' order. A toolbar that is added again moves to the end of its edge's
// last row without being hidden in between, so adding all of them row by row, with a break between two rows, leaves
// exactly the rows of m_places (rows that lose their last toolbar on the way are dropped by Qt). Qt counts the rows of
// the bottom edge from the window's edge upward.
void BarDock::arrange()
{
    m_arranging = true;
    m_window->setUpdatesEnabled(false);
    for (int edge = 0; edge < 2; ++edge) {
        const Qt::ToolBarArea area = edge ? Qt::BottomToolBarArea : Qt::TopToolBarArea;
        const QList<Row> &rows = edge ? m_places.bottom : m_places.top;
        bool first = true;
        for (int k = 0; k < rows.size(); ++k) {
            const Row &row = rows[edge ? rows.size() - 1 - k : k];
            bool any = false;
            for (const Slot &s : row)
                any = any || barNamed(s.name) != nullptr;
            if (!any)
                continue;
            if (!first)
                m_window->addToolBarBreak(area);
            first = false;
            for (const Slot &s : row)
                if (GripBar *bar = barNamed(s.name))
                    m_window->addToolBar(area, bar);
        }
    }
    m_window->setUpdatesEnabled(true);
    m_arranging = false;
}

Places BarDock::placesOfWindow() const
{
    Places p;
    QList<Row> bottomUp;
    QLayout *lay = m_window->layout();
    // (the window's layout lists its toolbars edge by edge, row by row, from the left)
    for (int i = 0; lay; ++i) {
        QLayoutItem *item = lay->itemAt(i);
        if (!item)
            break;
        auto *bar = qobject_cast<GripBar *>(item->widget());
        if (!bar || !m_bars.contains(bar))
            continue;
        QList<Row> &rows = m_window->toolBarArea(bar) == Qt::BottomToolBarArea ? bottomUp : p.top;
        if (rows.isEmpty() || m_window->toolBarBreak(bar))
            rows << Row();
        rows.last() << Slot{bar->objectName(), 0};
    }
    for (int r = bottomUp.size() - 1; r >= 0; --r)
        p.bottom << bottomUp[r];
    return p;
}

void BarDock::load()
{
    QSettings s;
    const QString key = QStringLiteral("window/barRows");
    Places p;
    const bool kept = s.contains(key);
    if (kept) {
        p = fromText(s.value(key).toStringList());
    } else {
        // 0.91.0: the rows are in the window's state, the free space is kept as a gap after the bar on its left
        p = placesOfWindow();
        const QVariantMap gaps = s.value(QStringLiteral("window/barGaps")).toMap();
        for (QList<Row> *rows : {&p.top, &p.bottom})
            for (Row &row : *rows) {
                const GripBar *before = nullptr;
                for (Slot &slot : row) {
                    const GripBar *bar = barNamed(slot.name);
                    if (!bar || bar->isHidden())
                        continue;
                    if (before)
                        slot.lead = gaps.value(before->objectName()).toInt();
                    before = bar;
                }
            }
    }
    m_places = normalized(p, m_standard);
    arrange();
    refit();
    if (!kept || toText(m_places) != s.value(key).toStringList())
        save();
    emit placesChanged();
}

void BarDock::reset() { setPlaces(m_standard); }

void BarDock::setPlaces(const Places &p)
{
    cancelDrag();
    m_places = normalized(p, m_standard);
    arrange();
    refit();
    save();
    emit placesChanged();
}

void BarDock::save()
{
    QSettings s;
    s.setValue(QStringLiteral("window/barRows"), toText(m_places));
    // (what 0.91.0 reads, so that it shows the same free space when it is started later)
    QVariantMap gaps;
    for (GripBar *bar : m_bars)
        if (bar->gap() > 0 && !bar->isHidden())
            gaps.insert(bar->objectName(), bar->gap());
    s.setValue(QStringLiteral("window/barGaps"), gaps);
}

// Free space shows as the gap after the toolbar on the left: each visible toolbar gets the lead of the next visible
// one of its row, as far as the row has room for it.
void BarDock::refit()
{
    if (m_drag.started)
        return; // (a drag shows its own free space and settles it at its end)
    const int width = m_window->width(), least = smallestLead();
    for (const QList<Row> *rows : {&m_places.top, &m_places.bottom})
        for (const Row &row : *rows) {
            QList<GripBar *> bars;
            QList<Shown> shown;
            for (const Slot &s : row) {
                GripBar *bar = barNamed(s.name);
                if (!bar)
                    continue;
                if (bar->isHidden()) {
                    bar->setGap(0);
                    continue;
                }
                bars << bar;
                shown << Shown{s.name, bar->naturalWidth(), s.lead};
            }
            const QList<int> leads = fitted(shown, width, least);
            for (int i = 0; i < bars.size(); ++i)
                bars[i]->setGap(i + 1 < bars.size() ? leads[i + 1] : 0);
        }
}

// ---------------------------------------------------------------- the drag

QList<BarDock::Band> BarDock::bandsNow() const
{
    QList<Band> bands;
    for (int edge = 0; edge < 2; ++edge) {
        const QList<Row> &rows = edge ? m_places.bottom : m_places.top;
        for (int r = 0; r < rows.size(); ++r) {
            Band band;
            band.bottom = edge == 1;
            band.row = r;
            int top = 0, bottom = -1;
            for (const Slot &s : rows[r]) {
                GripBar *bar = barNamed(s.name);
                if (!bar || bar->isHidden())
                    continue;
                const QPoint at = bar->mapTo(m_window, QPoint(0, 0));
                if (band.bars.isEmpty() || at.y() < top)
                    top = at.y();
                bottom = qMax(bottom, at.y() + bar->height() - 1);
                band.bars << BandBar{bar, at.x(), bar->naturalWidth()};
            }
            if (band.bars.isEmpty())
                continue;
            band.rect = QRect(QPoint(0, top), QPoint(m_window->width() - 1, bottom));
            bands << band;
        }
    }
    return bands;
}

void BarDock::pressed(GripBar *bar, const QPoint &pos)
{
    cancelDrag();
    m_drag = Drag();
    m_drag.bar = bar;
    m_drag.press = bar->mapTo(m_window, pos);
    m_drag.grab = pos.x();
}

void BarDock::movedTo(GripBar *bar, const QPoint &pos)
{
    if (m_drag.bar != bar)
        return;
    const QPoint p = bar->mapTo(m_window, pos);
    if (!m_drag.started) {
        if ((p - m_drag.press).manhattanLength() < QApplication::startDragDistance())
            return;
        beginDrag();
        if (!m_drag.started)
            return;
    }
    follow(p);
}

void BarDock::beginDrag()
{
    m_refitTimer->stop();
    refit(); // what shows is what the places say
    if (QLayout *lay = m_window->layout())
        lay->activate(); // (and the toolbars stand where that puts them before their positions are read)
    m_drag.bands = bandsNow();
    for (int i = 0; i < m_drag.bands.size(); ++i)
        for (int k = 0; k < m_drag.bands[i].bars.size(); ++k)
            if (m_drag.bands[i].bars[k].bar == m_drag.bar) {
                m_drag.own = i;
                m_drag.index = k;
            }
    if (m_drag.own < 0) {
        m_drag = Drag();
        return;
    }
    // the row as it shows: each toolbar's width and the free space in front of it, read from where the toolbars stand
    const Band &band = m_drag.bands[m_drag.own];
    const int least = smallestLead();
    for (int i = 0; i < band.bars.size(); ++i) {
        int lead = i == 0 ? 0 : band.bars[i].x - (band.bars[i - 1].x + band.bars[i - 1].natural);
        if (lead <= least)
            lead = 0;
        m_drag.shown << Shown{band.bars[i].bar->objectName(), band.bars[i].natural, lead};
        m_drag.startLeads << lead;
    }
    m_drag.leads = m_drag.startLeads;
    m_drag.mode = Mode::Shift;
    m_drag.started = true;
    QGuiApplication::setOverrideCursor(Qt::ClosedHandCursor);
    m_cursorSet = true;
    qApp->installEventFilter(this);
}

void BarDock::showLeads(const QList<int> &leads)
{
    const Band &band = m_drag.bands[m_drag.own];
    for (int i = 0; i < band.bars.size(); ++i)
        band.bars[i].bar->setGap(i + 1 < band.bars.size() && i + 1 < leads.size() ? leads[i + 1] : 0);
}

void BarDock::follow(const QPoint &p)
{
    const Band &band = m_drag.bands[m_drag.own];
    const int rowLeft = band.bars.first().x;
    const int rowWidth = m_window->width() - rowLeft;
    const QRect ownRect = band.rect.adjusted(0, -kSlack, 0, kSlack);
    Mode mode = Mode::None;
    QList<int> leads = m_drag.startLeads;
    if (p.y() >= ownRect.top() && p.y() <= ownRect.bottom()) {
        // between which of the row's other toolbars the mouse is, by their middles as the drag began
        int k = 0;
        for (int i = 0; i < band.bars.size(); ++i)
            if (i != m_drag.index && band.bars[i].x + band.bars[i].natural / 2 <= p.x())
                ++k;
        const QList<int> moved = shifted(m_drag.shown, rowWidth, m_drag.index, p.x() - m_drag.grab - rowLeft, smallestLead());
        bool shift = k == m_drag.index;
        if (!shift && k > m_drag.index && m_drag.mode == Mode::Shift && m_drag.index + 1 < m_drag.shown.size()) {
            // past the next toolbar's old middle, but that toolbar is being pushed ahead: the drag stays a shift
            // until the mouse passes its middle where it stands now (at the row's end at the latest)
            QList<Shown> now = m_drag.shown;
            for (int i = 0; i < now.size(); ++i)
                now[i].lead = moved[i];
            const int next = m_drag.index + 1;
            shift = p.x() - rowLeft < lefts(now)[next] + now[next].width / 2;
        }
        if (shift) {
            mode = Mode::Shift;
            leads = moved;
        }
    }
    QRect marker;
    Target target;
    if (mode != Mode::Shift) {
        target = targetAt(p, &marker);
        mode = target.valid ? Mode::Place : Mode::None;
    }
    m_drag.mode = mode;
    m_drag.leads = leads;
    m_drag.target = target;
    m_drag.marker = mode == Mode::Place ? marker : QRect();
    showLeads(leads);
    if (mode == Mode::Place) {
        m_marker->setGeometry(marker);
        m_marker->raise();
        m_marker->show();
        if (!m_hinted && m_window->statusBar()) {
            m_window->statusBar()->showMessage(tr("Release the mouse button to put the toolbar at the marked place. Esc leaves it where it is."));
            m_hinted = true;
        }
    } else {
        m_marker->hide();
        if (m_hinted && m_window->statusBar())
            m_window->statusBar()->clearMessage();
        m_hinted = false;
    }
    if (m_cursorSet)
        QGuiApplication::changeOverrideCursor(mode == Mode::None ? Qt::ForbiddenCursor : Qt::ClosedHandCursor);
}

Target BarDock::targetAt(const QPoint &p, QRect *marker) const
{
    const int width = m_window->width();
    if (p.x() < 0 || p.x() >= width || p.y() < 0 || p.y() >= m_window->height())
        return {};
    const QString name = m_drag.bar->objectName();
    const QSet<QString> hidden = hiddenBars();
    auto worth = [&](Target t) {
        t.valid = true;
        t.valid = moved(m_places, name, t, hidden) != m_places; // (a place that changes nothing is none)
        return t;
    };
    // a row (the toolbar's own first: the mouse may leave its height a little)
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < m_drag.bands.size(); ++i) {
            if ((pass == 0) != (i == m_drag.own))
                continue;
            const Band &band = m_drag.bands[i];
            const QRect r = i == m_drag.own ? band.rect.adjusted(0, -kSlack, 0, kSlack) : band.rect;
            if (p.y() < r.top() || p.y() > r.bottom())
                continue;
            QList<BandBar> others;
            for (const BandBar &b : band.bars)
                if (b.bar != m_drag.bar)
                    others << b;
            int k = 0;
            while (k < others.size() && others[k].x + others[k].natural / 2 <= p.x())
                ++k;
            Target t;
            t.bottom = band.bottom;
            t.row = band.row;
            t.before = k < others.size() ? others[k].bar->objectName() : QString();
            // the marker stands where the toolbar's left edge will be: at the end of the toolbar before it
            const int x = k == 0 ? 0 : others[k - 1].x + others[k - 1].natural;
            *marker = QRect(qBound(0, x - 1, width - 3), band.rect.top(), 3, band.rect.height());
            return worth(t);
        }
    // a row of its own: just under the top rows, just over the bottom rows, or below them at the window's bottom edge
    const QWidget *central = m_window->centralWidget();
    if (!central)
        return {};
    const QRect c = central->geometry();
    int lowest = c.bottom();
    for (const Band &band : m_drag.bands)
        if (band.bottom)
            lowest = qMax(lowest, band.rect.bottom());
    Target t;
    t.newRow = true;
    if (p.y() >= c.top() && p.y() < c.top() + kZone) {
        t.row = int(m_places.top.size());
        *marker = QRect(0, qMax(0, c.top() - 1), width, 3);
    } else if (p.y() > c.bottom() - kZone && p.y() <= c.bottom()) {
        t.bottom = true;
        t.row = 0;
        *marker = QRect(0, c.bottom() - 1, width, 3);
    } else if (p.y() > lowest) {
        t.bottom = true;
        t.row = int(m_places.bottom.size());
        *marker = QRect(0, lowest - 1, width, 3);
    } else {
        return {};
    }
    return worth(t);
}

void BarDock::released(GripBar *bar, const QPoint &pos)
{
    if (m_drag.bar != bar)
        return;
    if (!m_drag.started) {
        m_drag = Drag(); // a click on the grip
        return;
    }
    follow(bar->mapTo(m_window, pos));
    const Drag d = m_drag;
    endDrag();
    if (d.mode == Mode::Shift) {
        // the free space of the row as it shows now is what the places keep
        for (int i = 1; i < d.shown.size() && i < d.leads.size(); ++i) {
            const Where w = find(m_places, d.shown[i].name);
            if (w.found && w.index > 0)
                (w.bottom ? m_places.bottom : m_places.top)[w.row][w.index].lead = d.leads[i];
        }
        refit();
        save();
        emit placesChanged();
    } else if (d.mode == Mode::Place && d.target.valid) {
        m_places = moved(m_places, bar->objectName(), d.target, hiddenBars());
        arrange();
        refit();
        save();
        emit placesChanged();
    } else {
        refit();
    }
}

void BarDock::endDrag()
{
    const bool was = m_drag.started;
    m_drag = Drag();
    m_marker->hide();
    if (m_hinted && m_window->statusBar())
        m_window->statusBar()->clearMessage();
    m_hinted = false;
    if (m_cursorSet) {
        QGuiApplication::restoreOverrideCursor();
        m_cursorSet = false;
    }
    if (was)
        qApp->removeEventFilter(this);
}

void BarDock::cancelDrag()
{
    if (!m_drag.started) {
        m_drag = Drag();
        return;
    }
    endDrag();
    refit(); // the free space as it was
}

QRect BarDock::markerRect() const { return m_marker->isVisible() ? m_marker->geometry() : QRect(); }

} // namespace qb
