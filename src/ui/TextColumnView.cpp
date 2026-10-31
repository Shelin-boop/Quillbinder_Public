#include "ui/TextColumnView.h"
#include "ui/ViewMemory.h"
#include <QPointer>
#include "core/BinderItem.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "ui/AppSettings.h"
#include "ui/LookMenu.h"
#include "ui/ProjectLook.h"
#include "ui/TextEditor.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QCoreApplication>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace qb {

namespace {
class Divider : public QWidget
{
public:
    explicit Divider(QWidget *parent) : QWidget(parent) { setFixedHeight(22); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QColor c = palette().color(QPalette::Text);
        c.setAlpha(90);
        QPen pen(c, 1, Qt::DashLine);
        p.setPen(pen);
        p.drawLine(0, height() / 2, width(), height() / 2);
    }
};
} // namespace

TextColumnView::TextColumnView(QWidget *parent) : QScrollArea(parent)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_container = new QWidget;
    m_container->setAutoFillBackground(true);
    m_container->setBackgroundRole(QPalette::Base);
    m_container->installEventFilter(this);
    auto *hl = new QHBoxLayout(m_container);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->addStretch(1);
    m_column = new QWidget(m_container);
    m_column->setObjectName(QStringLiteral("textColumn")); // the paper (tests find it by this name)
    m_column->setAutoFillBackground(true);
    m_column->setBackgroundRole(QPalette::Base);
    m_column->installEventFilter(this);
    hl->addWidget(m_column);
    hl->addStretch(1);
    m_colLayout = new QVBoxLayout(m_column);
    m_colLayout->setContentsMargins(0, m_topMargin, 0, 0);
    m_colLayout->setSpacing(0);
    m_bottomSpacer = new QWidget(m_column);
    m_bottomSpacer->setFixedHeight(80);
    m_bottomSpacer->installEventFilter(this);
    m_colLayout->addWidget(m_bottomSpacer);
    m_colLayout->addStretch(1);
    setWidget(m_container);
    m_typewriter = AppSettings::instance().typewriterScrolling;
    applyTheme();
    connect(&theme::Notifier::instance(), &theme::Notifier::changed, this, &TextColumnView::applyTheme);
}

void TextColumnView::setProject(Project *p)
{
    if (m_project == p)
        return;
    if (m_project && m_project->docs())
        disconnect(m_project->docs(), nullptr, this, nullptr);
    m_project = p;
    if (p) {
        connect(p->docs(), &DocumentStore::pageViewChanged, this, &TextColumnView::applySettings);
        connect(p->docs(), &DocumentStore::zoomChanged, this, &TextColumnView::applySettings);
    }
}

void TextColumnView::setFollowsPageView(bool on)
{
    if (m_followsPageView == on)
        return;
    m_followsPageView = on;
    applySettings();
}

bool TextColumnView::pageView() const
{
    return m_followsPageView && m_project && m_project->docs() && m_project->docs()->pageView();
}

bool TextColumnView::pageInfo(int *page, int *pages) const
{
    if (!pageView() || m_sections.isEmpty())
        return false;
    int total = 0, current = 0;
    TextEditor *active = activeEditor();
    for (const Section &s : m_sections) {
        if (s.editor == active)
            current = total + s.editor->pageOfCursor();
        total += s.editor->pageCount();
    }
    if (page)
        *page = current;
    if (pages)
        *pages = total;
    return total > 0;
}

void TextColumnView::clear()
{
    rememberPlace();
    ++m_placeGen;
    m_placePending = false;
    for (Section &s : m_sections) {
        delete s.title;
        delete s.divider;
        delete s.editor;
    }
    m_sections.clear();
    m_active = nullptr;
}

void TextColumnView::setItems(const QList<BinderItem *> &items, bool combined)
{
    clear();
    m_combined = combined && items.size() > 0;
    if (!m_project)
        return;
    const bool titles = m_combined && AppSettings::instance().showTitlesInCombined;
    int index = 0;
    for (BinderItem *it : items) {
        if (!it->hasText())
            continue;
        Section s;
        s.item = it;
        if (index > 0 && m_combined) {
            s.divider = new Divider(m_column);
            m_colLayout->insertWidget(m_colLayout->indexOf(m_bottomSpacer), s.divider);
        }
        if (titles) {
            s.title = new QLabel(it->title, m_column);
            s.title->setFont(theme::font(theme::FontRole::CombinedTitle));
            s.title->setContentsMargins(8, 6, 8, 4);
            s.title->setForegroundRole(QPalette::Text);
            m_colLayout->insertWidget(m_colLayout->indexOf(m_bottomSpacer), s.title);
        }
        s.editor = new TextEditor(m_column);
        s.editor->setAutoHeight(true);
        s.editor->setTrimLastPage(m_combined);
        s.editor->setPageMode(pageView());
        s.editor->setProjectDocument(m_project, it, m_project->docs()->text(it->id));
        m_colLayout->insertWidget(m_colLayout->indexOf(m_bottomSpacer), s.editor);
        TextEditor *ed = s.editor;
        connect(ed, &TextEditor::focused, this, &TextColumnView::onEditorFocused);
        connect(ed, &TextEditor::navigateBeyond, this, &TextColumnView::navigate);
        connect(ed, &TextEditor::pageRequest, this, &TextColumnView::page);
        connect(ed, &TextEditor::linkActivated, this, &TextColumnView::linkActivated);
        connect(ed, &TextEditor::noteClicked, this, &TextColumnView::noteClicked);
        connect(ed, &QTextEdit::cursorPositionChanged, this, [this, ed] {
            if (ed == activeEditor() && pageView())
                emit pagesChanged(); // the page of the cursor
            if (ed->hasFocus()) {
                ensureCursorVisible(ed);
                emit cursorMoved(ed);
            }
        });
        // a selection that changes at its other end leaves the cursor where it was: the
        // format bar still has to show the new selection (0.40.1). Only for a selection
        // there is: Qt also reports the selection cleared when a document is taken out of
        // the editor, which happens while a project or the window closes.
        connect(ed, &QTextEdit::selectionChanged, this, [this, ed] {
            if (ed->hasFocus() && ed->textCursor().hasSelection())
                emit cursorMoved(ed);
        });
        connect(ed, &TextEditor::scriptElementChanged, this, [this, ed] { emit cursorMoved(ed); });
        connect(ed, &TextEditor::heightChanged, this, [this, ed] {
            emit pagesChanged();
            if (ed->hasFocus()) {
                // (the editor may be gone by then: another document was opened in the same moment)
                QPointer<TextEditor> guard(ed);
                QTimer::singleShot(0, this, [this, guard] {
                    if (guard)
                        ensureCursorVisible(guard);
                });
            }
        });
        m_sections << s;
        ++index;
    }
    if (!m_sections.isEmpty())
        m_active = m_sections.first().editor;
    applyColors();
    applySettings();
    verticalScrollBar()->setValue(0);
    // back to where the writer was in these documents (0.21.1), once they are laid out
    if (!m_sections.isEmpty() && m_project && viewmem::textPlace(m_project, placeKey()).valid()) {
        m_placePending = true;
        const int gen = ++m_placeGen;
        QTimer::singleShot(30, this, [this, gen] {
            if (gen == m_placeGen)
                restorePlace();
        });
    }
}

QString TextColumnView::placeKey() const
{
    QStringList ids;
    for (const Section &s : m_sections)
        if (s.item)
            ids << s.item->id;
    return viewmem::groupKey(ids);
}

void TextColumnView::rememberPlace()
{
    // not while the place of these documents is still to be given back (it would be
    // replaced by the top of the text), and not for a view nobody saw
    if (!m_project || m_sections.isEmpty() || m_placePending || !isVisible())
        return;
    viewmem::TextPlace t;
    TextEditor *act = activeEditor();
    if (act && act->item()) {
        t.cursor = act->textCursor().position();
        t.cursorItem = act->item()->id;
    }
    // the first character at the top of the view
    for (const Section &s : std::as_const(m_sections)) {
        const QPoint topLeft = s.editor->mapTo(viewport(), QPoint(0, 0));
        if (topLeft.y() + s.editor->height() <= 0)
            continue;
        const QPoint inEditor = s.editor->viewport()->mapFrom(viewport(), QPoint(30, 1));
        t.top = inEditor.y() <= 0 ? 0 : s.editor->cursorForPosition(inEditor).position();
        t.topItem = s.item ? s.item->id : QString();
        break;
    }
    if (m_sections.size() == 1) {
        t.cursorItem.clear();
        t.topItem.clear();
    }
    viewmem::setTextPlace(m_project, placeKey(), t);
}

bool TextColumnView::restorePlace()
{
    m_placePending = false;
    if (!m_project || m_sections.isEmpty())
        return false;
    const viewmem::TextPlace t = viewmem::textPlace(m_project, placeKey());
    if (!t.valid())
        return false;
    // only when nothing else moved the cursor or the view since the documents were shown
    if (verticalScrollBar()->value() != 0)
        return false;
    for (const Section &s : std::as_const(m_sections))
        if (s.editor->textCursor().position() != 0 || s.editor->textCursor().hasSelection())
            return false;
    auto editorFor = [this](const QString &id) -> TextEditor * {
        for (const Section &s : std::as_const(m_sections))
            if (s.item && (id.isEmpty() || s.item->id == id))
                return s.editor;
        return nullptr;
    };
    // the whole text laid out, so that the column is as high as it will be
    for (const Section &s : std::as_const(m_sections))
        s.editor->document()->size();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    if (TextEditor *ce = editorFor(t.cursorItem); ce && t.cursor >= 0) {
        QTextCursor c = ce->textCursor();
        c.setPosition(qBound(0, t.cursor, ce->document()->characterCount() - 1));
        ce->setTextCursor(c);
        m_active = ce;
    }
    if (TextEditor *te = editorFor(t.topItem); te && t.top >= 0) {
        QTextCursor c(te->document());
        c.setPosition(qBound(0, t.top, te->document()->characterCount() - 1));
        const QRect r = te->cursorRect(c);
        const QPoint inContainer = te->viewport()->mapTo(m_container, r.topLeft());
        setScrollValue(qMax(0, inContainer.y()));
    }
    return true;
}

QList<BinderItem *> TextColumnView::items() const
{
    QList<BinderItem *> l;
    for (const Section &s : m_sections)
        l << s.item;
    return l;
}

QList<TextEditor *> TextColumnView::editors() const
{
    QList<TextEditor *> l;
    for (const Section &s : m_sections)
        l << s.editor;
    return l;
}

TextEditor *TextColumnView::activeEditor() const
{
    if (m_active)
        return m_active;
    return m_sections.isEmpty() ? nullptr : m_sections.first().editor;
}

BinderItem *TextColumnView::activeItem() const
{
    TextEditor *e = activeEditor();
    return e ? e->item() : nullptr;
}

int TextColumnView::sectionIndex(TextEditor *e) const
{
    for (int i = 0; i < m_sections.size(); ++i)
        if (m_sections[i].editor == e)
            return i;
    return -1;
}

void TextColumnView::onEditorFocused(TextEditor *e)
{
    const bool changed = m_active != e;
    m_active = e;
    if (changed) {
        emit activeEditorChanged(e);
        emit activeItemChanged(e->item());
    }
}

void TextColumnView::navigate(TextEditor *e, bool down)
{
    const int i = sectionIndex(e);
    const int j = down ? i + 1 : i - 1;
    if (j < 0 || j >= m_sections.size())
        return;
    TextEditor *t = m_sections[j].editor;
    t->setFocus();
    QTextCursor c = t->textCursor();
    c.movePosition(down ? QTextCursor::Start : QTextCursor::End);
    t->setTextCursor(c);
    ensureCursorVisible(t);
}

void TextColumnView::page(TextEditor *e, bool down)
{
    QScrollBar *sb = verticalScrollBar();
    const int step = viewport()->height() - 40;
    const QRect r = e->cursorRect();
    const QPoint inViewport = e->viewport()->mapTo(viewport(), r.center());
    sb->setValue(sb->value() + (down ? step : -step));
    // place the cursor at the same viewport height. The container is a child of the
    // viewport: map from the viewport into it (mapTo works only towards a parent, and
    // crashed here before 0.19 on every Page Down / Page Up).
    const QPoint target = m_container->mapFrom(viewport(), inViewport);
    TextEditor *gapEditor = nullptr; // the section after (down) or before (up) a gap between two sections
    for (const Section &s : m_sections) {
        const QRect g(s.editor->mapTo(m_container, QPoint(0, 0)), s.editor->size());
        if (target.y() >= g.top() && target.y() <= g.bottom()) {
            const QPoint local = s.editor->viewport()->mapFrom(m_container, target);
            QTextCursor c = s.editor->cursorForPosition(local);
            s.editor->setFocus();
            s.editor->setTextCursor(c);
            return;
        }
        if (down && !gapEditor && g.top() > target.y())
            gapEditor = s.editor;
        if (!down && g.bottom() < target.y())
            gapEditor = s.editor;
    }
    // In the space between two sections (the divider, a title): the start of the next
    // section going down, the end of the one before going up (0.40; before, the cursor
    // jumped to the end or the start of the whole text and skipped the documents between).
    // Beyond the text: the end / start.
    TextEditor *t = gapEditor ? gapEditor : (down ? m_sections.last().editor : m_sections.first().editor);
    t->setFocus();
    QTextCursor c = t->textCursor();
    c.movePosition((gapEditor ? !down : down) ? QTextCursor::End : QTextCursor::Start);
    t->setTextCursor(c);
}

void TextColumnView::focusItem(BinderItem *item, bool atEnd)
{
    for (const Section &s : m_sections) {
        if (s.item == item) {
            s.editor->setFocus();
            if (atEnd) {
                QTextCursor c = s.editor->textCursor();
                c.movePosition(QTextCursor::End);
                s.editor->setTextCursor(c);
            }
            scrollToItem(item);
            return;
        }
    }
}

void TextColumnView::scrollToItem(BinderItem *item)
{
    for (const Section &s : m_sections) {
        if (s.item == item) {
            QWidget *top = s.title ? static_cast<QWidget *>(s.title) : static_cast<QWidget *>(s.editor);
            const int y = top->mapTo(m_container, QPoint(0, 0)).y();
            verticalScrollBar()->setValue(qMax(0, y - 10));
            return;
        }
    }
}

void TextColumnView::setTypewriter(bool on)
{
    m_typewriter = on;
    updateMinHeights();
    if (TextEditor *e = activeEditor())
        ensureCursorVisible(e);
}

void TextColumnView::ensureCursorVisible(TextEditor *e)
{
    if (!e)
        return;
    const QRect r = e->cursorRect();
    const QPoint p = e->viewport()->mapTo(m_container, r.topLeft());
    if (e->cursorMovedByMouse()) {
        // a click or a drag: the cursor is where the mouse is, so the view moves only when the
        // mouse has left it (a drag beyond the edge), and only as far as needed (0.40.2)
        const int top = verticalScrollBar()->value(), bottom = top + viewport()->height();
        if (p.y() < top)
            verticalScrollBar()->setValue(p.y());
        else if (p.y() + r.height() > bottom)
            verticalScrollBar()->setValue(p.y() + r.height() - viewport()->height());
        return;
    }
    if (m_typewriter) {
        verticalScrollBar()->setValue(p.y() + r.height() / 2 - viewport()->height() / 2);
    } else {
        ensureVisible(p.x(), p.y() + r.height() / 2, 10, r.height() + 40);
    }
}

void TextColumnView::setTopMargin(int px)
{
    m_topMargin = px;
    updateColumnWidth();
}

void TextColumnView::setColumnWidthOverride(int px)
{
    m_widthOverride = px;
    updateColumnWidth();
}

void TextColumnView::setCustomColors(const QColor &text, const QColor &paper, const QColor &outside)
{
    m_customColors = true;
    m_text = text;
    m_paper = paper;
    m_outside = outside;
    m_pageSurround = QColor();
    applyColors();
}

void TextColumnView::applyTheme()
{
    if (!m_customColors) {
        m_text = theme::color(theme::Role::EditorText);
        m_paper = theme::color(theme::Role::EditorPaper);
        m_outside = theme::color(theme::Role::EditorMargin);
        m_pageSurround = theme::color(theme::Role::EditorPageSurround);
    }
    applyColors();
    for (Section &s : m_sections)
        if (s.title)
            s.title->setFont(theme::font(theme::FontRole::CombinedTitle));
    updateColumnWidth(); // the project's look may have another width or border (0.40.2)
}

void TextColumnView::applyColors()
{
    const bool paged = pageView();
    // in page view the sheets are painted by the editors; around them the surround shows
    const QColor around = paged && m_pageSurround.isValid() ? m_pageSurround : m_outside;
    QPalette pc = m_container->palette();
    pc.setColor(QPalette::Base, around);
    m_container->setPalette(pc);
    QColor onAround = m_text;
    if (paged && around.alpha() > 0 && qAbs(around.lightness() - m_text.lightness()) < 90)
        onAround = around.lightness() < 128 ? QColor(225, 225, 225) : QColor(40, 40, 40);
    QPalette pcol = m_column->palette();
    pcol.setColor(QPalette::Base, paged ? around : m_paper);
    pcol.setColor(QPalette::Text, paged ? onAround : m_text);
    m_column->setPalette(pcol);
    QColor edge = around.lightness() < 128 ? around.lighter(170) : around.darker(125);
    if (around.alpha() == 0)
        edge = QColor(128, 128, 128);
    for (Section &s : m_sections) {
        QPalette pal = s.editor->palette();
        pal.setColor(QPalette::Base, m_paper);
        pal.setColor(QPalette::Text, m_text);
        QColor faded = m_text;
        faded.setAlpha(120);
        pal.setColor(QPalette::PlaceholderText, faded);
        s.editor->setPalette(pal);
        s.editor->setPageSurround(around, edge);
        if (s.title) {
            QPalette tp = s.title->palette();
            tp.setColor(QPalette::Text, paged ? onAround : m_text);
            tp.setColor(QPalette::WindowText, paged ? onAround : m_text);
            s.title->setPalette(tp);
        }
        if (s.divider) {
            QPalette dp = s.divider->palette();
            dp.setColor(QPalette::Text, paged ? onAround : m_text);
            s.divider->setPalette(dp);
        }
    }
}

void TextColumnView::applyPageMode()
{
    const bool paged = pageView();
    for (Section &s : m_sections) {
        s.editor->setPageMode(paged);
        widths::update(s.editor->document()); // zoom or paper may have changed
    }
    // a page wider than the view can be scrolled sideways
    setHorizontalScrollBarPolicy(paged ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    if (m_bottomSpacer)
        m_bottomSpacer->setVisible(!paged || m_typewriter);
}

void TextColumnView::applySettings()
{
    applyPageMode();
    applyColors();
    updateColumnWidth();
    updateMinHeights();
    emit pagesChanged();
}

void TextColumnView::refreshTitles()
{
    for (Section &s : m_sections)
        if (s.title)
            s.title->setText(s.item->title);
}

void TextColumnView::updateColumnWidth()
{
    // The width the text gets must not depend on whether the vertical scroll
    // bar is shown: otherwise a text just at the edge goes round in circles
    // (bar shown -> narrower -> taller -> bar hidden -> wider -> shorter -> ...),
    // which kept the program busy (100 % processor) in narrow editors.
    int vw = viewport()->width();
    if (!verticalScrollBar()->isVisible())
        vw -= style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, verticalScrollBar());
    const int avail = qMax(200, vw - 48);
    const double zoom = m_project ? m_project->docs()->zoom() : 1.0;
    int w = avail;
    int border = 0;
    if (pageView()) {
        w = m_project->docs()->pageMetrics().paperW;
    } else if (m_widthOverride > 0) {
        w = qMin(m_widthOverride, vw);
    } else {
        // the border in the paper's color left and right of the text (0.40.2): outside the
        // text width, so that the lines break where they did; it gives way only when the view
        // is too narrow for both
        if (AppSettings::instance().fixedWidth)
            w = qMin(avail, int(look::editorWidth() * zoom));
        border = qMax(0, int(look::border() * zoom + 0.5));
        if (w + 2 * border > vw)
            border = qMax(0, (vw - w) / 2);
        w += 2 * border;
    }
    if (m_column->width() != w)
        m_column->setFixedWidth(w);
    const QMargins m(border, m_topMargin, border, 0);
    if (m_colLayout && m_colLayout->contentsMargins() != m)
        m_colLayout->setContentsMargins(m);
}

void TextColumnView::updateMinHeights()
{
    const int vh = viewport()->height();
    m_bottomSpacer->setFixedHeight(m_typewriter ? qMax(80, vh / 2) : 80);
    if (pageView()) {
        for (Section &s : m_sections)
            s.editor->setMinimumTextHeight(0);
    } else if (m_sections.size() == 1 && !m_combined) {
        m_sections.first().editor->setMinimumTextHeight(qMax(100, vh - 18 - m_bottomSpacer->height()));
    } else {
        for (Section &s : m_sections)
            s.editor->setMinimumTextHeight(40);
    }
}

void TextColumnView::resizeEvent(QResizeEvent *e)
{
    QScrollArea::resizeEvent(e);
    updateColumnWidth();
    updateMinHeights();
}

int TextColumnView::scrollValue() const { return verticalScrollBar()->value(); }
void TextColumnView::setScrollValue(int v) { verticalScrollBar()->setValue(v); }

bool TextColumnView::eventFilter(QObject *o, QEvent *e)
{
    const bool around = o == m_container || o == m_column || o == m_bottomSpacer;
    if (around && (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseButtonDblClick)) {
        auto *me = static_cast<QMouseEvent *>(e);
        auto *w = static_cast<QWidget *>(o);
        // Only a click on the paper or beside it, not one that a part of the text passed on
        // (0.40.2: Qt hands a right-button press in the text on to its parents), and only the
        // left button: a right-click must leave the cursor and the selection alone.
        if (insideText(w->childAt(me->pos())))
            return false;
        if (me->button() != Qt::LeftButton)
            return true;
        if (!m_sections.isEmpty())
            clickBesideText(w->mapTo(m_container, me->pos()));
        return true;
    }
    if (around && e->type() == QEvent::ContextMenu) {
        auto *ce = static_cast<QContextMenuEvent *>(e);
        auto *w = static_cast<QWidget *>(o);
        if (insideText(w->childAt(ce->pos())))
            return false;
        // the look of the paper and of what lies around it (0.40.2)
        lookmenu::popup(lookmenu::Part(m_lookPart), ce->globalPos(), window());
        return true;
    }
    return QScrollArea::eventFilter(o, e);
}

// The widget under the mouse belongs to one of the texts (an event that the text passed on).
bool TextColumnView::insideText(QWidget *under) const
{
    for (QWidget *x = under; x && x != m_column && x != m_container; x = x->parentWidget())
        if (qobject_cast<TextEditor *>(x))
            return true;
    return false;
}

// A click on the paper beside the text (the border, the area around the column) puts the
// cursor at the start or the end of the line beside the click; a click below the text at the
// end of the text above it (0.40.2; before, every such click went to the end of the text,
// and the view jumped there).
void TextColumnView::clickBesideText(const QPoint &inContainer)
{
    TextEditor *target = nullptr;
    QPoint inEditor;
    for (const Section &s : m_sections) {
        const QRect r(s.editor->mapTo(m_container, QPoint(0, 0)), s.editor->size());
        if (inContainer.y() >= r.top() && inContainer.y() <= r.bottom()) {
            target = s.editor;
            inEditor = s.editor->viewport()->mapFrom(m_container, inContainer);
            break;
        }
        if (inContainer.y() < r.top()) {
            // above this section (its title, the gap between documents): the end of the one before,
            // or the start of this one when it is the first
            target = s.editor;
            inEditor = QPoint(0, 0);
            break;
        }
    }
    if (target) {
        const QRect vr = target->viewport()->rect();
        const QPoint clamped(qBound(vr.left(), inEditor.x(), vr.right()), qBound(vr.top(), inEditor.y(), vr.bottom()));
        QTextCursor c = target->cursorForPosition(clamped);
        if (inEditor.x() > vr.right()) {
            c.movePosition(QTextCursor::EndOfLine);
        } else if (inEditor.x() < vr.left()) {
            c.movePosition(QTextCursor::StartOfLine);
        }
        const int keep = verticalScrollBar()->value();
        target->setFocus(Qt::MouseFocusReason);
        target->setTextCursor(c);
        verticalScrollBar()->setValue(keep); // the line is on screen already
        return;
    }
    // below the text
    TextEditor *last = m_sections.last().editor;
    last->setFocus(Qt::MouseFocusReason);
    QTextCursor c = last->textCursor();
    c.movePosition(QTextCursor::End);
    last->setTextCursor(c);
}

} // namespace qb
