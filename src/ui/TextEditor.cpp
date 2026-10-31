#include "ui/TextEditor.h"
#include "ui/Zoom.h"
#include "core/Replace.h"
#include "core/Search.h"
#include "core/BinderItem.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/Project.h"
#include "text/AutoCorrect.h"
#include "text/DirectFormat.h"
#include "text/DisplayFormat.h"
#include "text/PageLayout.h"
#include "text/RtfIO.h"
#include "text/Styles.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/SpellChecker.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDialog>
#include <QMenu>
#include <QMimeData>
#include <functional>
#include <QPointer>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QFrame>
#include <QPainter>
#include <QTextFrame>
#include <QTimer>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTextList>
#include <QUrl>
#include <QUuid>
#include <cmath>

namespace qb {

int TextEditor::s_revision = 0;

// ------------------------------------------------------------------ widths

namespace widths {
static QHash<QTextDocument *, QList<TextEditor *>> &registry()
{
    static QHash<QTextDocument *, QList<TextEditor *>> r;
    return r;
}

void registerEditor(TextEditor *e)
{
    auto &r = registry();
    QList<TextEditor *> &l = r[e->document()];
    if (!l.contains(e))
        l.append(e);
    update(e->document());
}

void unregisterEditor(TextEditor *e)
{
    auto &r = registry();
    for (auto it = r.begin(); it != r.end();) {
        it->removeAll(e);
        if (it->isEmpty())
            it = r.erase(it);
        else
            ++it;
    }
}

void update(QTextDocument *doc)
{
    auto &r = registry();
    auto it = r.find(doc);
    if (it == r.end() || it->isEmpty())
        return;
    const QList<TextEditor *> editors = *it; // the calls below may come back here
    // page view: one editor asking for pages is enough, the layout is shared
    for (TextEditor *e : editors) {
        if (!e->pageMode() || !e->project())
            continue;
        const PageMetrics m = e->project()->docs()->pageMetrics();
        for (TextEditor *x : editors)
            if (x->lineWrapColumnOrWidth() != m.paperW)
                x->setLineWrapColumnOrWidth(m.paperW); // (this drops the page height)
        if (pagelayout::apply(doc, m))
            for (TextEditor *x : editors)
                x->pageLayoutChanged();
        return;
    }
    int w = INT_MAX;
    for (TextEditor *e : editors) {
        // an editor that is not on screen (the second editor after the split was closed) has no say
        if (!e->isVisible())
            continue;
        const int a = e->availableTextWidth();
        if (a > 40)
            w = qMin(w, a);
    }
    if (w == INT_MAX) {
        if (pagelayout::isPaged(doc) && pagelayout::clear(doc, editors.first()->lineWrapColumnOrWidth()))
            for (TextEditor *x : editors)
                x->pageLayoutChanged();
        return;
    }
    const bool wasPaged = pagelayout::clear(doc, w);
    for (TextEditor *e : editors) {
        if (e->lineWrapColumnOrWidth() != w)
            e->setLineWrapColumnOrWidth(w);
    }
    if (wasPaged)
        for (TextEditor *x : editors)
            x->pageLayoutChanged();
}
} // namespace widths

// ------------------------------------------------------------------ editor

TextEditor::TextEditor(QWidget *parent) : QTextEdit(parent)
{
    zoom::skip(this); // the zoom of a text comes from its document (DocumentStore), not from an area around it
    setLineWrapMode(QTextEdit::FixedPixelWidth);
    setLineWrapColumnOrWidth(600);
    setAcceptRichText(true);
    setTabChangesFocus(false);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    // File > Options > Editing: a wide insertion point (0.21)
    auto cursorLook = [this] {
        const AppSettings &s = AppSettings::instance();
        setCursorWidth(s.blockCursor ? qBound(2, s.blockCursorWidth, 20) : 1);
    };
    cursorLook();
    connect(&AppSettings::instance(), &AppSettings::changed, this, cursorLook);
}

TextEditor::~TextEditor() { widths::unregisterEditor(this); }

void TextEditor::setProjectDocument(Project *project, BinderItem *item, QTextDocument *doc, bool isNotes)
{
    widths::unregisterEditor(this);
    if (m_project && m_project->docs())
        disconnect(m_project->docs(), nullptr, this, nullptr);
    m_project = project;
    m_item = item;
    m_isNotes = isNotes;
    if (!doc) {
        detachDocument();
        return;
    }
    setDocument(doc);
    setFont(doc->defaultFont());
    // a document whose file could not be read is shown empty and must not be edited (0.20)
    const bool unreadable = project && item && project->docs()->isUnreadable(item->id, isNotes);
    if (unreadable) {
        setReadOnly(true);
        setProperty("qbUnreadable", true);
        setPlaceholderText(tr("The file of this document cannot be read, so it is shown empty and cannot be edited "
                              "(the file is not overwritten). Check its permissions or the drive it is on, then reopen the project."));
    } else if (property("qbUnreadable").toBool()) {
        setReadOnly(false);
        setProperty("qbUnreadable", false);
        setPlaceholderText(QString());
    }
    if (project) {
        connect(project->docs(), &DocumentStore::zoomChanged, this, [this] { setFont(document()->defaultFont()); });
        // notes have a zoom of their own (the inspector's, 0.21)
        connect(project->docs(), &DocumentStore::notesZoomChanged, this, [this] { setFont(document()->defaultFont()); });
        connect(project->docs(), &DocumentStore::aboutToDeleteDocument, this, [this](QTextDocument *d) {
            if (d == document())
                detachDocument();
        });
    }
    connect(doc->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
            &TextEditor::updateHeight, Qt::UniqueConnection);
    SpellChecker::attach(doc);
    widths::registerEditor(this);
    updateHeight();
    updateSearchSelections();
}

void TextEditor::detachDocument()
{
    widths::unregisterEditor(this);
    if (!m_placeholder)
        m_placeholder = new QTextDocument(this);
    m_item = nullptr;
    if (document() != m_placeholder)
        setDocument(m_placeholder);
}

void TextEditor::setAutoHeight(bool on)
{
    m_autoHeight = on;
    setVerticalScrollBarPolicy(on ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (on)
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    updateHeight();
}

int TextEditor::availableTextWidth() const
{
    int w = viewport()->width();
    if (m_maxTextWidth > 0)
        w = qMin(w, m_maxTextWidth);
    return w;
}

void TextEditor::setMaxTextWidth(int w)
{
    m_maxTextWidth = w;
    widths::update(document());
}

void TextEditor::updateHeight()
{
    if (m_pageMode && m_project && !m_healQueued && !pagelayout::matches(document(), m_project->docs()->pageMetrics())) {
        // the pages were lost (new content, another editor changed the line width): lay them out again
        m_healQueued = true;
        QTimer::singleShot(0, this, [this] {
            m_healQueued = false;
            widths::update(document());
        });
    }
    if (!m_autoHeight)
        return;
    int target;
    if (isPaged()) {
        const PageMetrics m = pagelayout::metrics(document());
        const int pages = pagelayout::pageCount(document());
        target = pages * m.step();
        if (m_trimLastPage) {
            const int bottom = pagelayout::textBottom(document()) + m.bottom + m.gapBelow();
            target = qBound((pages - 1) * m.step() + m.top + m.bottom + m.gap + 40, bottom, target);
        }
    } else {
        const int h = int(std::ceil(document()->size().height())) + frameWidth() * 2 + 2;
        target = qMax(h, m_minHeight);
    }
    if (target != height()) {
        setFixedHeight(target);
        emit heightChanged();
    }
}

// ------------------------------------------------------------------ page view

void TextEditor::setPageMode(bool on)
{
    if (m_pageMode == on)
        return;
    m_pageMode = on;
    widths::update(document());
    pageLayoutChanged();
}

bool TextEditor::isPaged() const { return pagelayout::isPaged(document()); }

void TextEditor::setTrimLastPage(bool on)
{
    if (m_trimLastPage == on)
        return;
    m_trimLastPage = on;
    updateHeight();
}

void TextEditor::setPageSurround(const QColor &outside, const QColor &edge)
{
    m_pageOutside = outside;
    m_pageEdge = edge;
    if (isPaged())
        viewport()->update();
}

void TextEditor::pageLayoutChanged()
{
    updateHeight();
    viewport()->update();
}

int TextEditor::pageCount() const { return pagelayout::pageCount(document()); }

int TextEditor::pageAt(int position) const
{
    if (!isPaged())
        return 0;
    const int step = pagelayout::metrics(document()).step();
    QTextCursor c(document());
    c.setPosition(qBound(0, position, document()->characterCount() - 1));
    const int y = cursorRect(c).center().y() + verticalScrollBar()->value();
    return qBound(1, y / step + 1, pageCount());
}

int TextEditor::pageOfCursor() const { return pageAt(textCursor().position()); }

int TextEditor::textInsetLeft() const
{
    return qRound(document()->rootFrame()->frameFormat().leftMargin());
}

int TextEditor::textInsetRight() const
{
    return qRound(document()->rootFrame()->frameFormat().rightMargin());
}

void TextEditor::paintEvent(QPaintEvent *e)
{
    QTextEdit::paintEvent(e);
    if (!isPaged())
        return;
    // The sheets follow one another every step() pixels of the layout; the
    // gap between two sheets lies inside the margins and is painted over.
    const PageMetrics m = pagelayout::metrics(document());
    const int step = m.step();
    const int yOff = verticalScrollBar()->value();
    const int xOff = horizontalScrollBar()->value();
    const int total = m_autoHeight ? height() : pagelayout::pageCount(document()) * step; // layout coordinates
    const QColor outside = m_pageOutside.isValid() ? m_pageOutside : palette().color(QPalette::Window);
    const QColor edge = m_pageEdge.isValid() ? m_pageEdge : palette().color(QPalette::Mid);
    QPainter p(viewport());
    p.translate(-xOff, -yOff);
    const QRect area = e->rect().translated(xOff, yOff);
    const int first = qMax(0, area.top() / step - 1), last = area.bottom() / step + 1;
    for (int i = first; i <= last; ++i) {
        const int top = i * step + m.gapAbove();                            // top edge of sheet i
        if (top >= total)
            break;
        const int bottom = qMin((i + 1) * step, total) - m.gapBelow();      // one past its bottom edge
        // gap above the sheet, and below the last one
        p.fillRect(QRect(0, i * step, m.paperW, m.gapAbove()), outside);
        p.fillRect(QRect(0, bottom, m.paperW, m.gapBelow()), outside);
        p.setPen(edge);
        p.drawRect(QRect(0, top, m.paperW - 1, bottom - top - 1));
    }
}

void TextEditor::resizeEvent(QResizeEvent *e)
{
    QTextEdit::resizeEvent(e);
    widths::update(document());
    updateHeight();
}

void TextEditor::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::FontChange) {
        // never let a widget font change alter the shared document font
        const QFont docFont = document()->defaultFont();
        QTextEdit::changeEvent(e);
        if (document()->defaultFont() != docFont)
            document()->setDefaultFont(docFont);
        return;
    }
    QTextEdit::changeEvent(e);
}

// The narrowest editor that shows a document decides its line width; when an
// editor appears or disappears, the others may have more or less room.
void TextEditor::showEvent(QShowEvent *e)
{
    QTextEdit::showEvent(e);
    if (document())
        widths::update(document());
}

void TextEditor::hideEvent(QHideEvent *e)
{
    QTextEdit::hideEvent(e);
    if (document())
        widths::update(document());
}

void TextEditor::focusInEvent(QFocusEvent *e)
{
    QTextEdit::focusInEvent(e);
    emit focused(this);
}

bool TextEditor::isRtfProject() const { return m_project && m_project->storage == StorageFormat::Rtf; }

void TextEditor::setRevisionLevel(int level) { s_revision = level; }
int TextEditor::revisionLevel() { return s_revision; }

// ------------------------------------------------------------------ keys

void TextEditor::smartTyping(QKeyEvent *e, bool *handled)
{
    *handled = false;
    const QString t = e->text();
    if (t.size() != 1)
        return;
    const AppSettings &s = AppSettings::instance();
    QTextCursor c = textCursor();
    const QChar ch = t.at(0);
    auto prevChar = [&]() -> QChar {
        if (c.hasSelection())
            return QChar();
        const int pos = c.position();
        if (pos == c.block().position())
            return QChar(' ');
        return document()->characterAt(pos - 1);
    };
    // [[Title]]: a link to the document of that title (File > Options > Corrections, 0.21)
    if (s.detectDocLinks && ch == QLatin1Char(']') && prevChar() == QLatin1Char(']') && makeDocumentLink()) {
        *handled = true;
        return;
    }
    // File > Options > Corrections: no smart punctuation in script mode
    if (s.scriptNoSmart && scriptActive())
        return;
    if (s.smartQuotes && (ch == QLatin1Char('"') || ch == QLatin1Char('\''))) {
        // the quotes of the style chosen in File > Options > Corrections (0.21)
        c.insertText(QString(autocorrect::smartQuote(ch, prevChar(), s.quoteStyle)));
        setTextCursor(c);
        *handled = true;
        return;
    }
    // three periods become an ellipsis (0.21; off by default)
    if (s.ellipsis && ch == QLatin1Char('.') && !c.hasSelection() && c.positionInBlock() >= 2
        && document()->characterAt(c.position() - 1) == QLatin1Char('.') && document()->characterAt(c.position() - 2) == QLatin1Char('.')) {
        c.beginEditBlock();
        c.deletePreviousChar();
        c.deletePreviousChar();
        c.insertText(QString(QChar(0x2026)));
        c.endEditBlock();
        setTextCursor(c);
        *handled = true;
        return;
    }
    if (s.smartDashes && ch == QLatin1Char('-') && !c.hasSelection() && c.position() > c.block().position()
        && document()->characterAt(c.position() - 1) == QLatin1Char('-')) {
        c.beginEditBlock();
        c.deletePreviousChar();
        c.insertText(QString(QChar(0x2014)));
        c.endEditBlock();
        setTextCursor(c);
        *handled = true;
    }
}

// The corrections of File > Options > Corrections when a word ends (text/AutoCorrect.h).
void TextEditor::correctWordBefore(QKeyEvent *e)
{
    const AppSettings &s = AppSettings::instance();
    if (!(s.capitalizeSentences || s.capitalizeI || s.detectUrls || s.substitutionsOn))
        return;
    const bool enter = e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Tab;
    const QString t = e->text();
    const QChar ch = t.size() == 1 ? t.at(0) : QChar();
    const bool space = enter || (!ch.isNull() && ch.isSpace());
    static const QString punct = QStringLiteral(".,;:!?)");
    if (!space && (ch.isNull() || !punct.contains(ch)))
        return;
    QTextCursor c = textCursor();
    if (c.hasSelection() || c.atBlockStart())
        return;
    autocorrect::Options o;
    o.capSentences = s.capitalizeSentences;
    o.capI = s.capitalizeI;
    o.urls = s.detectUrls && space;   // "www.example" followed by "." is not finished yet
    o.substitutions = s.substitutionsOn;
    o.substitutionList = s.substitutions;
    if (!autocorrect::afterWord(c, o).isEmpty())
        setTextCursor(c);
}

// "[[Title]" + "]": the brackets go and the title becomes a link to the document of
// that title; a new text document in the folder of this one when there is none.
bool TextEditor::makeDocumentLink()
{
    if (!m_project || !m_item || m_isNotes)
        return false;
    QTextCursor c = textCursor();
    const QString text = c.block().text().left(c.positionInBlock());
    const int open = int(text.lastIndexOf(QLatin1String("[[")));
    if (open < 0 || !text.endsWith(QLatin1Char(']')))
        return false;
    const QString title = text.mid(open + 2, text.size() - open - 3).trimmed();
    if (title.isEmpty() || title.contains(QLatin1Char('[')) || title.contains(QLatin1Char(']')))
        return false;
    BinderItem *target = nullptr;
    for (BinderItem *it : m_project->allItems())
        if (!it->isRootFolder() && !m_project->isInTrash(it) && it->title.compare(title, Qt::CaseInsensitive) == 0) {
            target = it;
            break;
        }
    if (!target) {
        BinderItem *parent = m_item->parent ? m_item->parent : m_project->draft();
        target = m_project->model()->createItem(parent, m_item->parent ? m_item->row() + 1 : -1, ItemType::Text, title);
        if (!target)
            return false;
    }
    c.beginEditBlock();
    c.setPosition(c.block().position() + open);
    c.setPosition(textCursor().position(), QTextCursor::KeepAnchor);
    c.removeSelectedText();
    c.endEditBlock();
    setTextCursor(c);
    insertLink(QStringLiteral("qb://") + target->id, target->title);
    return true;
}

void TextEditor::keyPressEvent(QKeyEvent *e)
{
    const bool noMods = !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    // File > Options > Editing: the Insert key switches overtype on and off (0.21)
    if (e->key() == Qt::Key_Insert && noMods && !(e->modifiers() & Qt::ShiftModifier) && AppSettings::instance().overtypeKey) {
        setOverwriteMode(!overwriteMode());
        return;
    }
    // File > Options > Corrections: the word just finished is corrected before the space,
    // Return or punctuation is typed (0.21)
    if (noMods && !isReadOnly())
        correctWordBefore(e);
    const bool script = scriptActive();
    if (script && scriptKeyPress(e))
        return;
    QTextCursor c = textCursor();

    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !(e->modifiers() & Qt::ShiftModifier)) {
        const QString style = c.block().blockFormat().property(props::Style).toString();
        const Formatting f = m_project ? m_project->formatting : Formatting();
        const ParagraphStyle *ps = styles::find(f, style);
        // empty list item ends the list
        if (QTextList *list = c.block().textList()) {
            if (c.block().text().isEmpty()) {
                list->remove(c.block());
                QTextBlockFormat bf = display::normalBlockFormat(f);
                c.setBlockFormat(bf);
                return;
            }
        }
        // the style says which style follows it (headings: body text)
        if (ps && !ps->next.isEmpty() && c.atBlockEnd()) {
            const Style *nextStyle = ps->next == styles::noStyle() ? nullptr : styles::find(f, ps->next);
            const QString nextName = nextStyle ? nextStyle->name : QString();
            c.beginEditBlock();
            const QTextCharFormat cf = display::charFormatFor(f, nextName);
            c.insertBlock(display::blockFormatFor(f, nextName), cf);
            c.setCharFormat(cf);
            c.endEditBlock();
            setTextCursor(c);
            ensureCursorVisible();
            return;
        }
    }
    if (m_autoHeight && noMods) {
        if (e->key() == Qt::Key_Up) {
            QTextCursor t = c;
            if (!t.movePosition(QTextCursor::Up) || t.position() == c.position()) {
                emit navigateBeyond(this, false);
                return;
            }
        } else if (e->key() == Qt::Key_Down) {
            QTextCursor t = c;
            if (!t.movePosition(QTextCursor::Down) || t.position() == c.position()) {
                emit navigateBeyond(this, true);
                return;
            }
        } else if (e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown) {
            emit pageRequest(this, e->key() == Qt::Key_PageDown);
            return;
        }
    }
    const bool isText = !e->text().isEmpty() && e->text().at(0).isPrint() && noMods;
    if (isText) {
        // do not extend links and linked notes when typing at their end
        QTextCharFormat cur = c.charFormat();
        const auto kind = NoteKind(cur.intProperty(props::NoteKind));
        if ((cur.isAnchor() || isLinkedNote(kind)) && !c.hasSelection()) {
            const int pos = c.position();
            const QTextCharFormat nextFmt = [&] {
                QTextCursor n(document());
                n.setPosition(qMin(pos + 1, document()->characterCount() - 1));
                return n.charFormat();
            }();
            const bool atEnd = c.atBlockEnd()
                               || (cur.isAnchor() && nextFmt.anchorHref() != cur.anchorHref())
                               || (isLinkedNote(kind) && nextFmt.stringProperty(props::NoteId) != cur.stringProperty(props::NoteId));
            if (atEnd) {
                QTextCharFormat clean = cur;
                if (cur.isAnchor()) {
                    clean.setAnchor(false);
                    clean.clearProperty(QTextFormat::AnchorHref);
                    clean.setFontUnderline(false);
                    clean.clearForeground();
                }
                if (isLinkedNote(kind))
                    text::clearNoteProps(clean);
                c.setCharFormat(clean);
                setTextCursor(c);
                setCurrentCharFormat(clean);
            }
        }
        if (s_revision > 0)
            mergeCurrentCharFormat(text::revisionFormat(s_revision));
        bool handled = false;
        smartTyping(e, &handled);
        if (handled) {
            if (script)
                scriptAfterKey(e);
            return;
        }
    }
    QTextEdit::keyPressEvent(e);
    if (script)
        scriptAfterKey(e);
}

// ------------------------------------------------------------------ mouse

// While the mouse moves the cursor, the view around the editor must not scroll by a margin
// (TextColumnView::ensureCursorVisible asks cursorMovedByMouse()).
namespace {
struct MouseMoving {
    int &n;
    explicit MouseMoving(int &count) : n(count) { ++n; }
    ~MouseMoving() { --n; }
};
} // namespace

void TextEditor::mousePressEvent(QMouseEvent *e)
{
    MouseMoving m(m_mouseMoving);
    QTextEdit::mousePressEvent(e);
    // Qt's text part ignores every button but the left one; such a press must not travel on to
    // the paper behind the text, whose click handler would move the cursor (0.40.2: a
    // right-click moved the cursor to the end of the text, and the view jumped there)
    if (e->button() != Qt::LeftButton)
        e->accept();
}

void TextEditor::mouseDoubleClickEvent(QMouseEvent *e)
{
    MouseMoving m(m_mouseMoving);
    QTextEdit::mouseDoubleClickEvent(e);
    if (e->button() != Qt::LeftButton)
        e->accept();
}

void TextEditor::mouseReleaseEvent(QMouseEvent *e)
{
    {
        MouseMoving m(m_mouseMoving);
        QTextEdit::mouseReleaseEvent(e);
    }
    if (e->button() != Qt::LeftButton)
        e->accept();
    if (e->button() != Qt::LeftButton || textCursor().hasSelection())
        return;
    const QString href = anchorAt(e->pos());
    if (!href.isEmpty()
        && (e->modifiers() & Qt::ControlModifier || href.startsWith(QLatin1String("qb://")) || href.startsWith(QLatin1String("qbobj://")))) {
        emit linkActivated(href);
        return;
    }
    const QTextCursor c = cursorForPosition(e->pos());
    const QString nid = noteIdAt(c.position());
    if (nid.isEmpty())
        return;
    if (e->modifiers() & Qt::AltModifier)
        showNotePopover(nid, e->globalPosition().toPoint());
    else
        emit noteClicked(nid);
}

void TextEditor::mouseMoveEvent(QMouseEvent *e)
{
    {
        MouseMoving m(m_mouseMoving);
        QTextEdit::mouseMoveEvent(e);
    }
    const QString href = anchorAt(e->pos());
    viewport()->setCursor(!href.isEmpty() ? Qt::PointingHandCursor : Qt::IBeamCursor);
    if (!href.isEmpty())
        setToolTip(href.startsWith(QLatin1String("qb://"))      ? tr("Document link (click to open)")
                   : href.startsWith(QLatin1String("qbobj://")) ? tr("Object link (click to show the object)")
                                                                : href + tr("\nCtrl+click to open"));
    else
        setToolTip(QString());
}

QString TextEditor::noteIdAt(int position) const
{
    QTextCursor c(document());
    c.setPosition(qBound(0, position, document()->characterCount() - 1));
    QTextCharFormat cf = c.charFormat();
    QString id = cf.stringProperty(props::NoteId);
    if (id.isEmpty() && !c.atBlockEnd()) {
        c.movePosition(QTextCursor::NextCharacter);
        cf = c.charFormat();
        id = cf.stringProperty(props::NoteId);
    }
    if (!isLinkedNote(NoteKind(cf.intProperty(props::NoteKind))) && NoteKind(cf.intProperty(props::NoteKind)) == NoteKind::None)
        return QString();
    return id;
}

// ------------------------------------------------------------------ context menu

static std::function<void(TextEditor *, QMenu *, const QPoint &)> &contextHook()
{
    static std::function<void(TextEditor *, QMenu *, const QPoint &)> hook;
    return hook;
}

void setTextEditorContextHook(std::function<void(TextEditor *, QMenu *, const QPoint &)> f) { contextHook() = f; }

QString TextEditor::wordUnderCursor(QTextCursor *wordCursor) const
{
    QTextCursor c = textCursor();
    c.select(QTextCursor::WordUnderCursor);
    if (wordCursor)
        *wordCursor = c;
    return c.selectedText();
}

void TextEditor::contextMenuEvent(QContextMenuEvent *e)
{
    // move the cursor to the click position unless clicking inside a selection
    QTextCursor clickCursor = cursorForPosition(e->pos());
    const QTextCursor cur = textCursor();
    if (!(cur.hasSelection() && clickCursor.position() >= cur.selectionStart() && clickCursor.position() <= cur.selectionEnd())) {
        MouseMoving m(m_mouseMoving); // the line is on screen: the view stays where it is
        setTextCursor(clickCursor);
    }

    QMenu *top = new QMenu(this);
    top->setAttribute(Qt::WA_DeleteOnClose);
    // On a misspelled word the menu is short (0.40.3): the suggestions, Add and Ignore, and
    // all other commands in one submenu. The long menu did not fit below the word, opened
    // upward and put the suggestions far from the mouse.
    QMenu *menu = top;
    // images: change the display size
    {
        const int at = clickCursor.position();
        for (int p : {at, at - 1}) {
            if (p < 0 || p >= document()->characterCount() - 1)
                continue;
            QTextCursor t(document());
            t.setPosition(p);
            t.setPosition(p + 1, QTextCursor::KeepAnchor);
            if (t.charFormat().isImageFormat()) {
                menu->addAction(tr("Image Size…"), this, [this, t] { editImageSize(t); });
                menu->addSeparator();
                break;
            }
        }
    }
    // spelling
    if (SpellChecker::isEnabled() && SpellChecker::instance().isAvailable() && !textCursor().hasSelection()) {
        QTextCursor wc;
        QString word = wordUnderCursor(&wc);
        while (word.endsWith(QLatin1Char('\'')) || word.endsWith(QChar(0x2019)))
            word.chop(1);
        if (word.size() > 1 && word.at(0).isLetter() && !SpellChecker::instance().isCorrect(word)) {
            const QStringList sug = SpellChecker::instance().suggestions(word);
            for (const QString &s : sug) {
                QAction *a = menu->addAction(s);
                QFont f = a->font();
                f.setBold(true);
                a->setFont(f);
                connect(a, &QAction::triggered, this, [this, wc, s, word]() mutable {
                    QTextCursor c = wc;
                    c.beginEditBlock();
                    // keep a trailing apostrophe outside the replacement
                    c.setPosition(wc.selectionStart());
                    c.setPosition(wc.selectionStart() + word.size(), QTextCursor::KeepAnchor);
                    c.insertText(s);
                    c.endEditBlock();
                });
            }
            if (sug.isEmpty())
                menu->addAction(tr("(no suggestions)"))->setEnabled(false);
            menu->addSeparator();
            menu->addAction(tr("Add \"%1\" to Dictionary").arg(word), this,
                            [word] { SpellChecker::instance().addToPersonal(word); });
            menu->addAction(tr("Ignore \"%1\"").arg(word), this, [word] { SpellChecker::instance().ignore(word); });
            menu->addSeparator();
            menu = top->addMenu(tr("More Commands"));
            menu->setObjectName(QStringLiteral("spellingMore"));
        }
    }
    QMenu *std = createStandardContextMenu(e->pos());
    for (QAction *a : std->actions()) {
        a->setParent(menu);
        menu->addAction(a);
    }
    std->deleteLater();
    if (contextHook())
        contextHook()(this, menu, e->pos());
    top->popup(e->globalPos());
}

// ------------------------------------------------------------------ clipboard

bool TextEditor::canInsertFromMimeData(const QMimeData *source) const
{
    return source->hasImage() || source->hasUrls() || source->hasFormat(QString::fromLatin1(kItemsMime))
           || source->hasFormat(QStringLiteral("text/rtf")) || QTextEdit::canInsertFromMimeData(source);
}

QMimeData *TextEditor::createMimeDataFromSelection() const
{
    QMimeData *md = QTextEdit::createMimeDataFromSelection();
    const QTextDocumentFragment frag = textCursor().selection();
    QTextDocument tmp;
    QTextCursor c(&tmp);
    c.insertFragment(frag);
    md->setData(QStringLiteral("text/rtf"), rtf::write(&tmp, m_project ? m_project->formatting : Formatting()));
    // text copied inside the program keeps its fonts when pasted
    md->setData(QStringLiteral("application/x-quillbinder-text"), QByteArray("1"));
    // plain text without annotation bubbles is more useful elsewhere
    md->setText(textCursor().selection().toPlainText());
    return md;
}

static void insertSemantic(QTextCursor &c, QTextDocument *src, const Formatting &f, bool keepFonts)
{
    bool first = true;
    for (QTextBlock b = src->begin(); b.isValid(); b = b.next()) {
        QString style = b.blockFormat().property(props::Style).toString();
        if (style.isEmpty() && b.blockFormat().headingLevel() > 0)
            style = styles::forHeadingLevel(b.blockFormat().headingLevel());
        if (!first)
            c.insertBlock(display::blockFormatFor(f, style), display::charFormatFor(f, style));
        first = false;
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            const QTextCharFormat in = frag.charFormat();
            if (in.isImageFormat()) {
                c.insertImage(in.toImageFormat());
                continue;
            }
            QTextCharFormat cf = display::semanticCharFormat(in);
            if (keepFonts)
                direct::mergeCharSpec(cf, direct::charSpec(in));
            QTextCharFormat merged = display::charFormatFor(f, style);
            if (const Style *cs = styles::findChar(f, cf.stringProperty(props::CharStyle)))
                styles::applyChars(merged, *cs);
            else
                cf.clearProperty(props::CharStyle);
            merged.merge(cf);
            c.insertText(frag.text(), merged);
        }
    }
}

void TextEditor::insertFromMimeData(const QMimeData *source)
{
    QTextCursor c = textCursor();
    const Formatting f = m_project ? m_project->formatting : Formatting();
    if (source->hasFormat(QString::fromLatin1(kItemsMime)) && m_project) {
        const QList<BinderItem *> items = BinderModel::itemsFromMime(source, m_project);
        c.beginEditBlock();
        for (BinderItem *it : items)
            insertLink(QStringLiteral("qb://") + it->id, it->title);
        c.endEditBlock();
        return;
    }
    if (source->hasImage() && m_project && m_item) {
        const QImage img = qvariant_cast<QImage>(source->imageData());
        if (!img.isNull()) {
            const QString rel = m_project->docs()->importImage(m_item->id, img);
            QTextImageFormat imf;
            imf.setName(rel);
            if (img.width() > 600) {
                imf.setWidth(600);
                imf.setHeight(img.height() * 600.0 / img.width());
            }
            document()->addResource(QTextDocument::ImageResource, QUrl(rel), img);
            c.insertImage(imf);
            return;
        }
    }
    if (source->hasUrls()) {
        bool any = false;
        for (const QUrl &u : source->urls()) {
            if (!u.isLocalFile())
                continue;
            const QString path = u.toLocalFile();
            const QString ext = QFileInfo(path).suffix().toLower();
            if (QStringList{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("gif"),
                            QStringLiteral("bmp"), QStringLiteral("webp")}
                    .contains(ext)) {
                insertImageFile(path);
                any = true;
            }
        }
        if (any)
            return;
    }
    c.beginEditBlock();
    if (source->hasFormat(QStringLiteral("text/rtf")) || source->hasFormat(QStringLiteral("application/rtf"))) {
        const QByteArray data = source->hasFormat(QStringLiteral("text/rtf")) ? source->data(QStringLiteral("text/rtf"))
                                                                              : source->data(QStringLiteral("application/rtf"));
        QTextDocument tmp;
        // Markdown projects take the look of the text they are pasted into,
        // unless the text was copied in this program
        const bool keep = isRtfProject() || source->hasFormat(QStringLiteral("application/x-quillbinder-text"));
        rtf::ReadOptions o;
        o.keepFormatting = keep;
        rtf::read(data, &tmp, f, o);
        insertSemantic(c, &tmp, f, keep);
    } else if (source->hasHtml()) {
        QTextDocument tmp;
        tmp.setHtml(source->html());
        insertSemantic(c, &tmp, f, false);
    } else if (source->hasText()) {
        QString t = source->text();
        t.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        const QStringList lines = t.split(QLatin1Char('\n'));
        QTextCharFormat cf = c.charFormat();
        text::clearNoteProps(cf);
        for (int i = 0; i < lines.size(); ++i) {
            if (i > 0)
                c.insertBlock(display::normalBlockFormat(f));
            c.insertText(lines[i], cf);
        }
    }
    c.endEditBlock();
    setTextCursor(c);
}

void TextEditor::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasFormat(QString::fromLatin1(kItemsMime))) {
        e->acceptProposedAction();
        return;
    }
    QTextEdit::dragEnterEvent(e);
}

void TextEditor::dragMoveEvent(QDragMoveEvent *e)
{
    if (e->mimeData()->hasFormat(QString::fromLatin1(kItemsMime))) {
        e->acceptProposedAction();
        return;
    }
    QTextEdit::dragMoveEvent(e);
}

void TextEditor::dropEvent(QDropEvent *e)
{
    if (e->mimeData()->hasFormat(QString::fromLatin1(kItemsMime)) && m_project) {
        const QList<BinderItem *> items = BinderModel::itemsFromMime(e->mimeData(), m_project);
        QTextCursor sel = textCursor();
        const QTextCursor dropPos = cursorForPosition(e->position().toPoint());
        const bool onSelection = sel.hasSelection() && dropPos.position() >= sel.selectionStart()
                                 && dropPos.position() <= sel.selectionEnd();
        if (e->modifiers() & Qt::AltModifier) {
            // insert the text of the dragged documents
            QTextCursor c = dropPos;
            c.beginEditBlock();
            for (BinderItem *it : items) {
                if (!it->hasText())
                    continue;
                QTextDocument *src = m_project->docs()->text(it->id);
                c.insertFragment(QTextDocumentFragment(src));
            }
            c.endEditBlock();
        } else if (!items.isEmpty() && items.first()->type == ItemType::Image && !isNotes()) {
            // a picture from the binder goes into the text as a picture
            setTextCursor(dropPos);
            for (BinderItem *it : items)
                if (it->type == ItemType::Image)
                    insertImageFile(m_project->mediaFilePath(it));
        } else if (!items.isEmpty()) {
            if (!onSelection)
                setTextCursor(dropPos);
            insertLink(QStringLiteral("qb://") + items.first()->id, items.first()->title);
        }
        e->setDropAction(Qt::CopyAction); // the documents stay where they are
        e->accept();
        return;
    }
    QTextEdit::dropEvent(e);
}

// ------------------------------------------------------------------ formatting

void TextEditor::mergeFormat(const QTextCharFormat &fmt)
{
    QTextCursor c = textCursor();
    if (!c.hasSelection())
        c.select(QTextCursor::WordUnderCursor);
    c.mergeCharFormat(fmt);
    mergeCurrentCharFormat(fmt);
}

ToggleState toggleStateOf(const QTextEdit *edit, const FormatAdjust &adjust)
{
    auto stateOf = [&](const QTextCharFormat &cf, const QTextBlock &b) {
        const QTextCharFormat c = adjust ? adjust(cf, b) : cf;
        ToggleState s;
        s.bold = c.fontWeight() >= QFont::DemiBold;
        s.italic = c.fontItalic();
        s.underline = c.fontUnderline() && !cf.isAnchor(); // links are underlined by themselves
        s.strike = c.fontStrikeOut();
        return s;
    };
    const QTextCursor c = edit->textCursor();
    if (!c.hasSelection())
        return stateOf(edit->currentCharFormat(), c.block());
    ToggleState all{true, true, true, true};
    bool any = false;
    const int from = c.selectionStart(), to = c.selectionEnd();
    for (QTextBlock b = edit->document()->findBlock(from); b.isValid() && b.position() < to; b = b.next()) {
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            if (!fr.isValid() || fr.position() + fr.length() <= from || fr.position() >= to)
                continue;
            if (fr.charFormat().isImageFormat())
                continue; // a picture has no letters to make bold
            const ToggleState s = stateOf(fr.charFormat(), b);
            all.bold = all.bold && s.bold;
            all.italic = all.italic && s.italic;
            all.underline = all.underline && s.underline;
            all.strike = all.strike && s.strike;
            any = true;
        }
    }
    return any ? all : stateOf(edit->currentCharFormat(), c.block());
}

ToggleState TextEditor::toggleState(bool byHand) const
{
    if (!byHand)
        return toggleStateOf(this);
    const Formatting f = m_project ? m_project->formatting : Formatting();
    return toggleStateOf(this, [&f](const QTextCharFormat &cf, const QTextBlock &b) {
        return styles::withoutStyles(cf, styles::find(f, b.blockFormat().stringProperty(props::Style)), f);
    });
}

BarState TextEditor::barState() const
{
    BarState s;
    s.toggles = toggleStateOf(this);
    auto lineOf = [](const QTextBlockFormat &bf) {
        if (bf.lineHeightType() == QTextBlockFormat::SingleHeight)
            return 1.0;
        if (bf.lineHeightType() == QTextBlockFormat::ProportionalHeight && bf.lineHeight() > 0)
            return bf.lineHeight() / 100.0;
        return 0.0;
    };
    const QTextCursor c = textCursor();
    if (!c.hasSelection()) {
        const QTextCharFormat cf = currentCharFormat();
        s.superscript = cf.verticalAlignment() == QTextCharFormat::AlignSuperScript;
        s.subscript = cf.verticalAlignment() == QTextCharFormat::AlignSubScript;
        s.family = fontFamilyAtCursor();
        s.size = fontSizeAtCursor();
        s.align = bodyAlignment(c.blockFormat().alignment());
        s.lineSpacing = lineOf(c.blockFormat());
        return s;
    }
    const QString projectFamily = m_project ? display::resolvedFontFamily(m_project->formatting) : font().family();
    const double projectSize = m_project ? m_project->formatting.fontSize : font().pointSizeF();
    const int from = c.selectionStart(), to = c.selectionEnd();
    bool any = false, sup = true, sub = true, sameFamily = true, sameSize = true;
    QString family;
    double size = 0;
    for (QTextBlock b = document()->findBlock(from); b.isValid() && b.position() < to; b = b.next()) {
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            if (!fr.isValid() || fr.position() + fr.length() <= from || fr.position() >= to || fr.charFormat().isImageFormat())
                continue;
            const QTextCharFormat cf = fr.charFormat();
            const direct::CharSpec sp = direct::charSpec(cf);
            const QString fam = sp.family.isEmpty() ? projectFamily : sp.family;
            const double sz = sp.size > 0 ? sp.size : projectSize;
            if (!any) {
                family = fam;
                size = sz;
            }
            sameFamily = sameFamily && fam.compare(family, Qt::CaseInsensitive) == 0;
            sameSize = sameSize && qAbs(sz - size) < 0.05;
            sup = sup && cf.verticalAlignment() == QTextCharFormat::AlignSuperScript;
            sub = sub && cf.verticalAlignment() == QTextCharFormat::AlignSubScript;
            any = true;
        }
    }
    if (any) {
        s.superscript = sup;
        s.subscript = sub;
        s.family = sameFamily ? family : QString();
        s.size = sameSize ? size : 0.0;
    } else { // only pictures selected
        s.family = fontFamilyAtCursor();
        s.size = fontSizeAtCursor();
    }
    bool firstBlock = true;
    for (QTextBlock b = document()->findBlock(from); b.isValid() && (firstBlock || b.position() < to); b = b.next()) {
        const Qt::Alignment al = bodyAlignment(b.blockFormat().alignment());
        const double line = lineOf(b.blockFormat());
        if (firstBlock) {
            s.align = al;
            s.lineSpacing = line;
            firstBlock = false;
            continue;
        }
        if (al != s.align)
            s.align = Qt::Alignment();
        if (s.lineSpacing >= 0 && qAbs(line - s.lineSpacing) > 0.005)
            s.lineSpacing = -1;
    }
    return s;
}

void TextEditor::setBold(bool on)
{
    QTextCharFormat f;
    f.setFontWeight(on ? QFont::Bold : QFont::Normal);
    // not bold inside a bold style is kept as set by hand (0.40.3)
    f.setProperty(props::NotBold, !on);
    mergeFormat(f);
}

void TextEditor::setItalic(bool on)
{
    QTextCharFormat f;
    f.setFontItalic(on);
    f.setProperty(props::NotItalic, !on);
    mergeFormat(f);
}

void TextEditor::setUnderline(bool on)
{
    QTextCharFormat f;
    f.setFontUnderline(on);
    mergeFormat(f);
}

void TextEditor::setStrike(bool on)
{
    QTextCharFormat f;
    f.setFontStrikeOut(on);
    mergeFormat(f);
}

// a selection that is only partly bold becomes all bold, as in other word processors
void TextEditor::toggleBold() { setBold(!toggleState(false).bold); }
void TextEditor::toggleItalic() { setItalic(!toggleState(false).italic); }
void TextEditor::toggleUnderline() { setUnderline(!toggleState(false).underline); }
void TextEditor::toggleStrike() { setStrike(!toggleState(false).strike); }

void TextEditor::toggleSuperscript()
{
    QTextCharFormat f;
    f.setVerticalAlignment(currentCharFormat().verticalAlignment() == QTextCharFormat::AlignSuperScript
                               ? QTextCharFormat::AlignNormal
                               : QTextCharFormat::AlignSuperScript);
    mergeFormat(f);
}

void TextEditor::toggleSubscript()
{
    QTextCharFormat f;
    f.setVerticalAlignment(currentCharFormat().verticalAlignment() == QTextCharFormat::AlignSubScript
                               ? QTextCharFormat::AlignNormal
                               : QTextCharFormat::AlignSubScript);
    mergeFormat(f);
}

void TextEditor::toggleHighlight(const QColor &color)
{
    QTextCursor c = textCursor();
    if (!c.hasSelection())
        return;
    const bool on = !c.charFormat().property(props::Highlight).toBool();
    // apply per fragment so that notes keep their look
    const int start = c.selectionStart(), end = c.selectionEnd();
    c.beginEditBlock();
    for (QTextBlock b = document()->findBlock(start); b.isValid() && b.position() <= end; b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            const int s = qMax(start, fr.position()), e = qMin(end, fr.position() + fr.length());
            if (s >= e)
                continue;
            QTextCharFormat cf = fr.charFormat();
            if (cf.hasProperty(props::NoteKind))
                continue;
            if (on) {
                cf.merge(text::highlightFormat(color));
            } else {
                cf.clearProperty(props::Highlight);
                cf.clearBackground();
            }
            QTextCursor fc(document());
            fc.setPosition(s);
            fc.setPosition(e, QTextCursor::KeepAnchor);
            fc.setCharFormat(cf);
        }
    }
    c.endEditBlock();
}

void TextEditor::applyTextColor(const QColor &col)
{
    if (textCursor().hasSelection()) {
        setTextColor(col);
        return;
    }
    // what is typed next
    QTextCharFormat cf = currentCharFormat();
    if (col.isValid())
        cf.setForeground(col);
    else
        cf.clearForeground();
    setCurrentCharFormat(cf);
}

void TextEditor::applyHighlight(const QColor &color)
{
    QTextCursor c = textCursor();
    if (!c.hasSelection()) {
        QTextCharFormat cf = currentCharFormat();
        if (color.isValid()) {
            cf.merge(text::highlightFormat(color));
        } else {
            cf.clearProperty(props::Highlight);
            cf.clearBackground();
        }
        setCurrentCharFormat(cf);
        return;
    }
    // per fragment, so that notes keep their look (as toggleHighlight)
    const int start = c.selectionStart(), end = c.selectionEnd();
    c.beginEditBlock();
    for (QTextBlock b = document()->findBlock(start); b.isValid() && b.position() <= end; b = b.next()) {
        QList<QPair<QPair<int, int>, QTextCharFormat>> pieces;
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            const int s = qMax(start, fr.position()), e = qMin(end, fr.position() + fr.length());
            if (s >= e)
                continue;
            QTextCharFormat cf = fr.charFormat();
            if (cf.hasProperty(props::NoteKind))
                continue;
            if (color.isValid()) {
                cf.merge(text::highlightFormat(color));
            } else {
                cf.clearProperty(props::Highlight);
                cf.clearBackground();
            }
            pieces.append({{s, e}, cf});
        }
        for (const auto &pc : pieces) {
            QTextCursor fc(document());
            fc.setPosition(pc.first.first);
            fc.setPosition(pc.first.second, QTextCursor::KeepAnchor);
            fc.setCharFormat(pc.second);
        }
    }
    c.endEditBlock();
}

void TextEditor::setParagraphStyle(const QString &style)
{
    display::applyParagraphStyle(textCursor(), style, m_project ? m_project->formatting : Formatting());
}

static QTextCursor selectionOrWord(QTextCursor c)
{
    if (!c.hasSelection())
        c.select(QTextCursor::WordUnderCursor);
    return c;
}

void TextEditor::setCharacterStyle(const QString &style)
{
    const Formatting f = m_project ? m_project->formatting : Formatting();
    QTextCursor c = textCursor();
    if (!c.hasSelection() && style.isEmpty()) {
        // no selection: the whole stretch of the character style at the cursor
        const QString cur = c.charFormat().stringProperty(props::CharStyle);
        if (cur.isEmpty())
            return;
        const QTextBlock b = c.block();
        int from = c.position(), to = c.position();
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            if (!fr.isValid() || fr.charFormat().stringProperty(props::CharStyle) != cur)
                continue;
            if (fr.position() + fr.length() >= from && fr.position() <= to) {
                from = qMin(from, fr.position());
                to = qMax(to, fr.position() + fr.length());
            }
        }
        styles::applyCharacterStyle(document(), from, to, QString(), f);
        QTextCharFormat cf = currentCharFormat();
        cf.clearProperty(props::CharStyle);
        setCurrentCharFormat(cf);
        return;
    }
    const QTextCursor range = selectionOrWord(c);
    if (range.hasSelection()) {
        styles::applyCharacterStyle(document(), range.selectionStart(), range.selectionEnd(), style, f);
    } else if (const Style *cs = styles::findChar(f, style)) {
        // nothing to style yet: what is typed next gets the style
        QTextCharFormat cf = currentCharFormat();
        styles::applyChars(cf, *cs);
        cf.setProperty(props::CharStyle, cs->name);
        setCurrentCharFormat(cf);
    }
}

void TextEditor::applyStyle(const QString &style)
{
    const Formatting f = m_project ? m_project->formatting : Formatting();
    if (style.isEmpty()) {
        // "No Style": character styles of the selection go first; text
        // without any loses its paragraph style
        const QTextCursor c = textCursor();
        bool hasChar = !c.charFormat().stringProperty(props::CharStyle).isEmpty();
        if (c.hasSelection()) {
            hasChar = false;
            for (QTextBlock b = document()->findBlock(c.selectionStart()); b.isValid() && b.position() < c.selectionEnd();
                 b = b.next())
                for (auto it = b.begin(); !it.atEnd(); ++it) {
                    const QTextFragment fr = it.fragment();
                    if (fr.isValid() && fr.position() < c.selectionEnd() && fr.position() + fr.length() > c.selectionStart()
                        && fr.charFormat().hasProperty(props::CharStyle))
                        hasChar = true;
                }
        }
        if (hasChar)
            setCharacterStyle(QString());
        else
            setParagraphStyle(QString());
        return;
    }
    if (styles::findChar(f, style))
        setCharacterStyle(style);
    else
        setParagraphStyle(style);
}

QString TextEditor::paragraphStyleAtCursor() const
{
    return textCursor().blockFormat().stringProperty(props::Style);
}

bool TextEditor::paragraphStylesMixed() const
{
    const QTextCursor c = textCursor();
    if (!c.hasSelection())
        return false;
    const QTextBlock first = document()->findBlock(c.selectionStart());
    const QString style = first.blockFormat().stringProperty(props::Style);
    for (QTextBlock b = first.next(); b.isValid() && b.position() < c.selectionEnd(); b = b.next())
        if (b.blockFormat().stringProperty(props::Style) != style)
            return true;
    return false;
}

QString TextEditor::characterStyleAtCursor() const
{
    QTextCursor c = textCursor();
    if (c.hasSelection()) {
        QTextCursor p(document());
        p.setPosition(c.selectionStart() + 1);
        return p.charFormat().stringProperty(props::CharStyle);
    }
    return currentCharFormat().stringProperty(props::CharStyle);
}

void TextEditor::setAlignment(Qt::Alignment a)
{
    direct::ParaSpec sp;
    sp.bits = direct::Align;
    sp.align = a;
    applyParaSpec(sp);
}

void TextEditor::toggleList(bool numbered)
{
    QTextCursor c = textCursor();
    c.beginEditBlock();
    if (QTextList *l = c.currentList()) {
        const bool isNumbered = l->format().style() == QTextListFormat::ListDecimal;
        if (isNumbered == numbered) {
            // remove from list
            QTextBlock b = document()->findBlock(c.selectionStart());
            const QTextBlock last = document()->findBlock(c.selectionEnd());
            while (b.isValid()) {
                if (QTextList *bl = b.textList())
                    bl->remove(b);
                QTextCursor bc(b);
                QTextBlockFormat bf = bc.blockFormat();
                bf.setIndent(0);
                bc.setBlockFormat(bf);
                if (b == last)
                    break;
                b = b.next();
            }
            c.endEditBlock();
            return;
        }
        QTextListFormat lf = l->format();
        lf.setStyle(numbered ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc);
        l->setFormat(lf);
        c.endEditBlock();
        return;
    }
    QTextBlockFormat bf = c.blockFormat();
    bf.setTextIndent(0);
    c.setBlockFormat(bf);
    QTextListFormat lf;
    lf.setStyle(numbered ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc);
    lf.setIndent(1);
    c.createList(lf);
    c.endEditBlock();
}

void TextEditor::indentList(int delta)
{
    QTextCursor c = textCursor();
    QTextList *l = c.currentList();
    if (!l)
        return;
    QTextListFormat lf = l->format();
    lf.setIndent(qBound(1, lf.indent() + delta, 8));
    if (lf.style() != QTextListFormat::ListDecimal)
        lf.setStyle(lf.indent() % 3 == 1 ? QTextListFormat::ListDisc
                    : lf.indent() % 3 == 2 ? QTextListFormat::ListCircle
                                           : QTextListFormat::ListSquare);
    c.beginEditBlock();
    l->remove(c.block());
    c.createList(lf);
    c.endEditBlock();
}

void TextEditor::clearFormatting()
{
    QTextCursor c = textCursor();
    if (!c.hasSelection())
        return;
    QTextCharFormat plain;
    const int from = c.selectionStart(), to = c.selectionEnd();
    c.beginEditBlock();
    c.setCharFormat(plain);
    // the paragraph styles keep their say
    const Formatting f = m_project ? m_project->formatting : Formatting();
    for (QTextBlock b = document()->findBlock(from); b.isValid() && b.position() < to; b = b.next()) {
        const QTextCharFormat scf = display::charFormatFor(f, b.blockFormat().stringProperty(props::Style));
        if (scf.isEmpty())
            continue;
        QTextCursor bc(document());
        bc.setPosition(qMax(from, b.position()));
        bc.setPosition(qMin(to, b.position() + b.length() - 1), QTextCursor::KeepAnchor);
        bc.mergeCharFormat(scf);
    }
    c.endEditBlock();
    setCurrentCharFormat(plain);
}

void TextEditor::setTextColor(const QColor &col)
{
    direct::CharSpec sp;
    sp.color = col;
    applyCharSpec(sp, direct::Color);
}

void TextEditor::setFontFamily(const QString &fam)
{
    direct::CharSpec sp;
    sp.family = fam;
    applyCharSpec(sp, direct::Family);
}

void TextEditor::setFontSize(double pt)
{
    direct::CharSpec sp;
    sp.size = pt;
    applyCharSpec(sp, direct::Size);
}

void TextEditor::setLineSpacing(double factor)
{
    direct::ParaSpec sp;
    sp.bits = direct::Line;
    sp.lineType = QTextBlockFormat::ProportionalHeight;
    sp.line = factor * 100.0;
    applyParaSpec(sp);
}

// ------------------------------------------------------------------ fonts and paragraphs

static void setMembers(direct::CharSpec &to, const direct::CharSpec &from, int members)
{
    if (members & direct::Family)
        to.family = from.family;
    if (members & direct::Size)
        to.size = from.size;
    if (members & direct::Weight)
        to.weight = from.weight;
    if (members & direct::Color)
        to.color = from.color;
    if (members & direct::Spacing)
        to.spacing = from.spacing;
    if (members & direct::Caps)
        to.caps = from.caps;
}

direct::CharSpec TextEditor::charSpecAtCursor() const
{
    QTextCursor c = textCursor();
    if (c.hasSelection()) {
        // format of the first selected character
        QTextCursor p(document());
        p.setPosition(c.selectionStart() + 1);
        return direct::charSpec(p.charFormat());
    }
    return direct::charSpec(currentCharFormat());
}

double TextEditor::fontSizeAtCursor() const
{
    const direct::CharSpec sp = charSpecAtCursor();
    if (sp.size > 0)
        return sp.size;
    return m_project ? m_project->formatting.fontSize : font().pointSizeF();
}

QString TextEditor::fontFamilyAtCursor() const
{
    const direct::CharSpec sp = charSpecAtCursor();
    if (!sp.family.isEmpty())
        return sp.family;
    return m_project ? display::resolvedFontFamily(m_project->formatting) : font().family();
}

// The project's font and size are "no formatting": choosing them removes
// the font from the text instead of storing it.
static void dropDefaults(direct::CharSpec &sp, const Project *project)
{
    if (!project)
        return;
    if (!sp.family.isEmpty()
        && sp.family.compare(display::resolvedFontFamily(project->formatting), Qt::CaseInsensitive) == 0)
        sp.family.clear();
    if (sp.size > 0 && qAbs(sp.size - project->formatting.fontSize) < 0.01)
        sp.size = 0;
    if (sp.spacing == 100)
        sp.spacing = 0;
}

void TextEditor::applyCharSpec(const direct::CharSpec &spec, int members)
{
    QTextCursor c = textCursor();
    if (!c.hasSelection())
        c.select(QTextCursor::WordUnderCursor);
    struct Piece {
        int s, e;
        QTextCharFormat cf;
    };
    QList<Piece> pieces;
    if (c.hasSelection()) {
        const int start = c.selectionStart(), end = c.selectionEnd();
        for (QTextBlock b = document()->findBlock(start); b.isValid() && b.position() < end; b = b.next()) {
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const QTextFragment fr = it.fragment();
                if (!fr.isValid())
                    continue;
                const int s = qMax(start, fr.position()), e = qMin(end, fr.position() + fr.length());
                if (s >= e)
                    continue;
                QTextCharFormat cf = fr.charFormat();
                if (cf.isImageFormat())
                    continue;
                // the text color of an inline annotation is the color of the annotation itself (select it
                // and choose another text color); before 0.15.0 it did not show and was lost
                const bool annotationColor = (members & direct::Color)
                                             && NoteKind(cf.intProperty(props::NoteKind)) == NoteKind::InlineAnnotation;
                direct::CharSpec sp = direct::charSpec(cf);
                setMembers(sp, spec, annotationColor ? (members & ~direct::Color) : members);
                if (annotationColor) {
                    sp.color = QColor();
                    if (spec.color.isValid())
                        cf.setProperty(props::NoteColor, spec.color);
                    else
                        cf.clearProperty(props::NoteColor);
                }
                dropDefaults(sp, m_project);
                direct::applyCharSpec(cf, sp);
                if (m_project) {
                    // where nothing is set by hand the styles of the text decide again
                    const Formatting &pf = m_project->formatting;
                    if (const Style *ps = styles::find(pf, b.blockFormat().stringProperty(props::Style)))
                        styles::applyChars(cf, *ps);
                    if (const Style *cs = styles::findChar(pf, cf.stringProperty(props::CharStyle)))
                        styles::applyChars(cf, *cs);
                    if (!sp.isEmpty())
                        direct::mergeCharSpec(cf, sp);
                    if (sp.size > 0)
                        cf.clearProperty(QTextFormat::FontSizeAdjustment);
                }
                if (annotationColor)
                    text::applyNoteDisplay(cf);
                pieces.append({s, e, cf});
            }
        }
    }
    c.beginEditBlock();
    for (const Piece &p : pieces) {
        QTextCursor fc(document());
        fc.setPosition(p.s);
        fc.setPosition(p.e, QTextCursor::KeepAnchor);
        fc.setCharFormat(p.cf);
    }
    c.endEditBlock();
    // what is typed next (with a selection this would overwrite its formats)
    if (!textCursor().hasSelection()) {
        QTextCharFormat cur = currentCharFormat();
        direct::CharSpec sp = direct::charSpec(cur);
        setMembers(sp, spec, members);
        dropDefaults(sp, m_project);
        direct::applyCharSpec(cur, sp);
        setCurrentCharFormat(cur);
    }
}

void TextEditor::changeFontSize(double delta)
{
    QTextCursor c = textCursor();
    if (!c.hasSelection())
        c.select(QTextCursor::WordUnderCursor);
    const double base = m_project ? m_project->formatting.fontSize : font().pointSizeF();
    if (!c.hasSelection()) {
        setFontSize(qBound(4.0, fontSizeAtCursor() + delta, 400.0));
        return;
    }
    // every run keeps its own size and grows or shrinks by delta
    const int start = c.selectionStart(), end = c.selectionEnd();
    struct Piece {
        int s, e;
        QTextCharFormat cf;
    };
    QList<Piece> pieces;
    for (QTextBlock b = document()->findBlock(start); b.isValid() && b.position() < end; b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            if (!fr.isValid() || fr.charFormat().isImageFormat())
                continue;
            const int s = qMax(start, fr.position()), e = qMin(end, fr.position() + fr.length());
            if (s >= e)
                continue;
            QTextCharFormat cf = fr.charFormat();
            direct::CharSpec sp = direct::charSpec(cf);
            sp.size = qBound(4.0, (sp.size > 0 ? sp.size : base) + delta, 400.0);
            dropDefaults(sp, m_project);
            direct::applyCharSpec(cf, sp);
            pieces.append({s, e, cf});
        }
    }
    c.beginEditBlock();
    for (const Piece &p : pieces) {
        QTextCursor fc(document());
        fc.setPosition(p.s);
        fc.setPosition(p.e, QTextCursor::KeepAnchor);
        fc.setCharFormat(p.cf);
    }
    c.endEditBlock();
    if (!textCursor().hasSelection()) {
        QTextCharFormat cur = currentCharFormat();
        direct::CharSpec sp = direct::charSpec(cur);
        sp.size = qBound(4.0, (sp.size > 0 ? sp.size : base) + delta, 400.0);
        dropDefaults(sp, m_project);
        direct::applyCharSpec(cur, sp);
        setCurrentCharFormat(cur);
    }
}

QList<QTextBlock> TextEditor::selectedBlocks() const
{
    QList<QTextBlock> out;
    const QTextCursor c = textCursor();
    QTextBlock b = document()->findBlock(c.selectionStart());
    const QTextBlock last = document()->findBlock(c.selectionEnd());
    while (b.isValid()) {
        out << b;
        if (b == last)
            break;
        b = b.next();
    }
    return out;
}

direct::ParaSpec TextEditor::paraSpecAtCursor() const
{
    const QTextBlockFormat bf = textCursor().blockFormat();
    direct::ParaSpec sp = direct::effectiveParaSpec(bf);
    sp.bits = bf.intProperty(props::ParaDirect) & direct::AllPara;
    return sp;
}

void TextEditor::applyParaSpec(const direct::ParaSpec &spec)
{
    if (!spec.bits)
        return;
    QTextCursor c = textCursor();
    c.beginEditBlock();
    for (const QTextBlock &b : selectedBlocks()) {
        QTextBlockFormat bf = b.blockFormat();
        if (m_project && !isRtfProject()) {
            bf = direct::withParaSpec(bf, spec, m_project->formatting, b.textList() != nullptr);
        } else {
            // RTF text carries complete paragraph formatting of its own
            direct::applyParaSpec(bf, spec);
            if ((spec.bits & direct::Align) && !(spec.bits & direct::First)) {
                if (spec.align & (Qt::AlignHCenter | Qt::AlignRight))
                    bf.setTextIndent(0);
                else if (m_project && bf.property(props::Style).toString().isEmpty() && !b.textList()
                         && !(bf.intProperty(props::ParaDirect) & direct::First))
                    bf.setTextIndent(m_project->formatting.firstLineIndent);
            }
        }
        QTextCursor bc(b);
        bc.setBlockFormat(bf);
    }
    c.endEditBlock();
}

void TextEditor::resetParagraphFormatting(int bits)
{
    if (!m_project)
        return;
    const QTextCursor c = textCursor();
    direct::removeDirect(document(), m_project->formatting, false, bits, c.selectionStart(), c.selectionEnd());
}

void TextEditor::changeLeftIndent(double deltaPx)
{
    QTextCursor c = textCursor();
    c.beginEditBlock();
    for (const QTextBlock &b : selectedBlocks()) {
        direct::ParaSpec sp;
        sp.bits = direct::Left;
        sp.left = qMax(0.0, b.blockFormat().leftMargin() + deltaPx);
        QTextCursor bc(b);
        setTextCursor(bc);
        applyParaSpec(sp);
    }
    c.endEditBlock();
    setTextCursor(c);
}

namespace {
struct CopiedFormat {
    bool valid = false;
    bool bold = false, italic = false, underline = false, strike = false;
    direct::CharSpec chars;
    direct::ParaSpec para;
    QString style;
    QString charStyle;
};
CopiedFormat s_copied;
} // namespace

bool TextEditor::hasCopiedFormatting() { return s_copied.valid; }

void TextEditor::copyFormatting()
{
    QTextCursor c = textCursor();
    QTextCharFormat cf = currentCharFormat();
    if (c.hasSelection()) {
        QTextCursor p(document());
        p.setPosition(c.selectionStart() + 1);
        cf = p.charFormat();
    }
    const QTextBlockFormat bf = document()->findBlock(c.selectionStart()).blockFormat();
    const Formatting f = m_project ? m_project->formatting : Formatting();
    const ParagraphStyle *ps = styles::find(f, bf.property(props::Style).toString());
    s_copied.charStyle = cf.stringProperty(props::CharStyle);
    cf = styles::withoutStyles(cf, ps, f); // what was set by hand
    s_copied.valid = true;
    s_copied.bold = cf.fontWeight() >= QFont::DemiBold;
    s_copied.italic = cf.fontItalic();
    s_copied.underline = cf.fontUnderline() && !cf.isAnchor();
    s_copied.strike = cf.fontStrikeOut();
    s_copied.chars = direct::charSpec(cf);
    s_copied.para = paraSpecAtCursor();
    if (isRtfProject())
        s_copied.para.bits = direct::AllPara;
    s_copied.style = bf.property(props::Style).toString();
}

void TextEditor::pasteFormatting()
{
    if (!s_copied.valid)
        return;
    QTextCursor c = textCursor();
    const QTextCursor original = c;
    c.beginEditBlock();
    const bool wholeParagraphs = !c.hasSelection();
    // paragraphs: style, then what was set by hand
    if (m_project) {
        const QList<QTextBlock> blocks = selectedBlocks();
        bool sameStyle = true;
        for (const QTextBlock &b : blocks)
            sameStyle = sameStyle && b.blockFormat().property(props::Style).toString() == s_copied.style;
        if (!sameStyle)
            display::applyParagraphStyle(c, s_copied.style, m_project->formatting);
        resetParagraphFormatting(direct::AllPara);
        applyParaSpec(s_copied.para);
    }
    // characters
    if (wholeParagraphs) {
        c.movePosition(QTextCursor::StartOfBlock);
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        setTextCursor(c);
    }
    const Formatting pf = m_project ? m_project->formatting : Formatting();
    const ParagraphStyle *ps = styles::find(pf, s_copied.style);
    QTextCharFormat sem;
    if (!(ps && ps->bold))
        sem.setFontWeight(s_copied.bold ? QFont::Bold : QFont::Normal);
    if (!(ps && ps->italic))
        sem.setFontItalic(s_copied.italic);
    sem.setFontStrikeOut(s_copied.strike);
    if (textCursor().hasSelection()) {
        // links keep their underline
        const int start = textCursor().selectionStart(), end = textCursor().selectionEnd();
        QTextCursor fc(document());
        fc.setPosition(start);
        fc.setPosition(end, QTextCursor::KeepAnchor);
        fc.mergeCharFormat(sem);
        for (QTextBlock b = document()->findBlock(start); b.isValid() && b.position() < end; b = b.next()) {
            QList<QPair<int, int>> ranges;
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const QTextFragment fr = it.fragment();
                if (fr.isValid() && !fr.charFormat().isAnchor() && !fr.charFormat().isImageFormat())
                    ranges.append({qMax(start, fr.position()), qMin(end, fr.position() + fr.length())});
            }
            QTextCharFormat ul;
            ul.setFontUnderline(s_copied.underline);
            for (const auto &r : ranges) {
                if (r.first >= r.second)
                    continue;
                fc.setPosition(r.first);
                fc.setPosition(r.second, QTextCursor::KeepAnchor);
                fc.mergeCharFormat(ul);
            }
        }
    }
    applyCharSpec(s_copied.chars, direct::AllChar);
    // the character style comes along; the styles' own formatting stays under what was set by hand
    if (textCursor().hasSelection())
        styles::applyCharacterStyle(document(), textCursor().selectionStart(), textCursor().selectionEnd(),
                                    s_copied.charStyle, pf);
    c.endEditBlock();
    setTextCursor(original);
}

// ------------------------------------------------------------------ notes

// File > Options > Editing > "Terminate footnotes and comments before punctuation" (0.21):
// a selection that ends with . , ; : ! ? stops before it.
static void stopBeforePunctuation(QTextCursor &c)
{
    if (!AppSettings::instance().notesBeforePunctuation || !c.hasSelection())
        return;
    static const QString punct = QStringLiteral(".,;:!?…");
    const int start = c.selectionStart();
    int end = c.selectionEnd();
    while (end - 1 > start && punct.contains(c.document()->characterAt(end - 1)))
        --end;
    if (end != c.selectionEnd()) {
        c.setPosition(start);
        c.setPosition(end, QTextCursor::KeepAnchor);
    }
}

QString TextEditor::addLinkedNote(NoteKind kind, const QString &noteText)
{
    QTextCursor c = textCursor();
    if (!c.hasSelection()) {
        c.select(QTextCursor::WordUnderCursor);
        if (!c.hasSelection())
            return QString();
    }
    stopBeforePunctuation(c);
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QTextCharFormat nf = text::noteFormat(kind, id, noteText);
    c.beginEditBlock();
    const int start = c.selectionStart(), end = c.selectionEnd();
    for (QTextBlock b = document()->findBlock(start); b.isValid() && b.position() <= end; b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            const int s = qMax(start, fr.position()), e = qMin(end, fr.position() + fr.length());
            if (s >= e)
                continue;
            QTextCharFormat cf = fr.charFormat();
            text::clearNoteProps(cf);
            cf.merge(nf);
            QTextCursor fc(document());
            fc.setPosition(s);
            fc.setPosition(e, QTextCursor::KeepAnchor);
            fc.setCharFormat(cf);
        }
    }
    c.endEditBlock();
    c.clearSelection();
    setTextCursor(c);
    return id;
}

void TextEditor::addInlineNote(NoteKind kind)
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QTextCharFormat nf = text::noteFormat(kind, id);
    QTextCursor c = textCursor();
    if (c.hasSelection()) {
        stopBeforePunctuation(c);
        c.mergeCharFormat(nf);
        return;
    }
    QTextCharFormat cf = c.charFormat();
    text::clearNoteProps(cf);
    cf.merge(nf);
    setCurrentCharFormat(cf);
}

QTextCursor TextEditor::noteCursor(const QString &noteId) const
{
    int start = -1, end = -1;
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            if (fr.charFormat().stringProperty(props::NoteId) == noteId) {
                if (start < 0)
                    start = fr.position();
                end = fr.position() + fr.length();
            } else if (start >= 0 && fr.position() > end) {
                break;
            }
        }
        if (start >= 0 && b.position() > end)
            break;
    }
    QTextCursor c(document());
    if (start < 0)
        return QTextCursor();
    c.setPosition(start);
    c.setPosition(end, QTextCursor::KeepAnchor);
    return c;
}

bool TextEditor::selectNote(const QString &noteId)
{
    QTextCursor c = noteCursor(noteId);
    if (c.isNull())
        return false;
    setTextCursor(c);
    ensureCursorVisible();
    return true;
}

static void forEachNoteFragment(QTextDocument *doc, const QString &noteId,
                                const std::function<void(const QTextFragment &)> &fn)
{
    QList<QTextFragment> frags;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it)
            if (it.fragment().charFormat().stringProperty(props::NoteId) == noteId)
                frags << it.fragment();
    for (const QTextFragment &f : frags)
        fn(f);
}

QString TextEditor::noteText(const QString &noteId) const
{
    QString text;
    bool found = false;
    forEachNoteFragment(document(), noteId, [&](const QTextFragment &f) {
        if (!found)
            text = f.charFormat().stringProperty(props::NoteText);
        found = true;
    });
    return text;
}

namespace {
// the small window of a comment or footnote; hands its text back when it closes
class NotePopover : public QFrame
{
public:
    NotePopover(QWidget *parent, const QString &title, const QString &text, const QColor &color)
        : QFrame(parent, Qt::Popup)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setObjectName(QStringLiteral("notePopover"));
        setFrameShape(QFrame::StyledPanel);
        auto *l = new QVBoxLayout(this);
        l->setContentsMargins(8, 6, 8, 8);
        l->setSpacing(4);
        auto *head = new QLabel(title, this);
        QFont f = head->font();
        f.setBold(true);
        head->setFont(f);
        l->addWidget(head);
        m_edit = new QPlainTextEdit(text, this);
        m_edit->setObjectName(QStringLiteral("notePopoverText"));
        m_edit->setFrameShape(QFrame::NoFrame);
        if (color.isValid()) {
            // the bubble's own color, as in the inspector (dark text: the bubbles are light in every theme)
            QPalette pal = m_edit->palette();
            pal.setColor(QPalette::Base, color.lighter(150));
            pal.setColor(QPalette::Text, QColor(30, 30, 30));
            m_edit->setPalette(pal);
        }
        l->addWidget(m_edit, 1);
        resize(320, 170);
    }
    std::function<void(const QString &)> onClose;

protected:
    void hideEvent(QHideEvent *e) override
    {
        if (onClose) {
            auto f = onClose;
            onClose = nullptr;
            f(m_edit->toPlainText());
        }
        QFrame::hideEvent(e);
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Escape)
            close();
        else
            QFrame::keyPressEvent(e);
    }

private:
    QPlainTextEdit *m_edit;
};
} // namespace

QWidget *TextEditor::showNotePopover(const QString &noteId, const QPoint &globalPos)
{
    NoteKind kind = NoteKind::None;
    QColor color;
    QString text;
    forEachNoteFragment(document(), noteId, [&](const QTextFragment &f) {
        if (kind != NoteKind::None)
            return;
        kind = NoteKind(f.charFormat().intProperty(props::NoteKind));
        text = f.charFormat().stringProperty(props::NoteText);
        color = f.charFormat().property(props::NoteColor).value<QColor>();
    });
    if (!isLinkedNote(kind))
        return nullptr; // inline notes have their text in the text itself
    if (kind == NoteKind::LinkedFootnote)
        color = footnoteBubbleColor();
    else if (!color.isValid())
        color = defaultCommentColor();
    auto *pop = new NotePopover(this, kind == NoteKind::LinkedFootnote ? tr("Footnote") : tr("Comment"), text, color);
    QPointer<TextEditor> self(this);
    pop->onClose = [self, noteId, text](const QString &now) {
        if (self && now != text)
            self->setNoteText(noteId, now);
    };
    pop->move(globalPos + QPoint(8, 12));
    pop->show();
    if (auto *edit = pop->findChild<QPlainTextEdit *>())
        edit->setFocus();
    return pop;
}

void TextEditor::setNoteText(const QString &noteId, const QString &t)
{
    QTextCursor c(document());
    c.beginEditBlock();
    forEachNoteFragment(document(), noteId, [&](const QTextFragment &f) {
        QTextCharFormat cf = f.charFormat();
        if (cf.stringProperty(props::NoteText) == t)
            return;
        cf.setProperty(props::NoteText, t);
        QTextCursor fc(document());
        fc.setPosition(f.position());
        fc.setPosition(f.position() + f.length(), QTextCursor::KeepAnchor);
        fc.setCharFormat(cf);
    });
    c.endEditBlock();
}

void TextEditor::setNoteColor(const QString &noteId, const QColor &color)
{
    QTextCursor c(document());
    c.beginEditBlock();
    forEachNoteFragment(document(), noteId, [&](const QTextFragment &f) {
        QTextCharFormat cf = f.charFormat();
        cf.setProperty(props::NoteColor, color);
        text::applyNoteDisplay(cf);
        QTextCursor fc(document());
        fc.setPosition(f.position());
        fc.setPosition(f.position() + f.length(), QTextCursor::KeepAnchor);
        fc.setCharFormat(cf);
    });
    c.endEditBlock();
}

void TextEditor::convertNote(const QString &noteId, NoteKind to)
{
    QTextCursor c(document());
    c.beginEditBlock();
    forEachNoteFragment(document(), noteId, [&](const QTextFragment &f) {
        QTextCharFormat cf = f.charFormat();
        const QString noteText = cf.stringProperty(props::NoteText);
        const QColor col = cf.property(props::NoteColor).value<QColor>();
        text::clearNoteProps(cf);
        cf.merge(text::noteFormat(to, noteId, noteText, col));
        QTextCursor fc(document());
        fc.setPosition(f.position());
        fc.setPosition(f.position() + f.length(), QTextCursor::KeepAnchor);
        fc.setCharFormat(cf);
    });
    c.endEditBlock();
}

void TextEditor::removeNote(const QString &noteId)
{
    QTextCursor range = noteCursor(noteId);
    if (range.isNull())
        return;
    const auto kind = NoteKind(range.charFormat().intProperty(props::NoteKind));
    QTextCursor c(document());
    c.beginEditBlock();
    if (isInlineNote(kind)) {
        range.removeSelectedText();
    } else {
        forEachNoteFragment(document(), noteId, [&](const QTextFragment &f) {
            QTextCharFormat cf = f.charFormat();
            text::clearNoteProps(cf);
            QTextCursor fc(document());
            fc.setPosition(f.position());
            fc.setPosition(f.position() + f.length(), QTextCursor::KeepAnchor);
            fc.setCharFormat(cf);
        });
    }
    c.endEditBlock();
}

void TextEditor::removeNoteAtCursor()
{
    const QString id = noteIdAt(textCursor().position());
    if (!id.isEmpty())
        removeNote(id);
}

// ------------------------------------------------------------------ links / images

void TextEditor::insertLink(const QString &href, const QString &textIfNoSelection)
{
    // File > Options > Behaviors > Document Links: the linked document gets a bookmark back
    // to this one (Document Bookmarks in its inspector; 0.21)
    if (AppSettings::instance().backLinks && m_project && m_item && !m_isNotes && href.startsWith(QLatin1String("qb://"))) {
        BinderItem *target = m_project->item(href.mid(5));
        const QString back = QStringLiteral("qb://") + m_item->id;
        if (target && target != m_item) {
            bool has = false;
            for (const Bookmark &b : target->bookmarks)
                has = has || b.target == back;
            if (!has) {
                target->bookmarks.append({m_item->title, back});
                m_project->notifyItemChanged(target, false);
            }
        }
    }
    QTextCursor c = textCursor();
    const QTextCharFormat lf = text::linkFormat(href);
    if (c.hasSelection()) {
        c.mergeCharFormat(lf);
    } else {
        QTextCharFormat cf = c.charFormat();
        text::clearNoteProps(cf);
        cf.merge(lf);
        c.insertText(textIfNoSelection, cf);
        QTextCharFormat after = cf;
        after.setAnchor(false);
        after.clearProperty(QTextFormat::AnchorHref);
        after.setFontUnderline(false);
        after.clearForeground();
        setTextCursor(c);
        setCurrentCharFormat(after);
    }
}

QString TextEditor::anchorAtCursor() const { return textCursor().charFormat().anchorHref(); }

void TextEditor::editImageSize(QTextCursor imageCursor)
{
    QTextImageFormat fmt = imageCursor.charFormat().toImageFormat();
    const QImage img = document()->resource(QTextDocument::ImageResource, QUrl(fmt.name())).value<QImage>();
    const QSize natural = img.isNull() ? QSize(400, 300) : img.size();
    const int curW = fmt.width() > 0 ? qRound(fmt.width()) : natural.width();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Image Size"));
    auto *form = new QFormLayout(&dlg);
    auto *width = new QSpinBox(&dlg);
    width->setRange(16, 4000);
    width->setSuffix(tr(" px"));
    width->setValue(curW);
    form->addRow(tr("Width on screen:"), width);
    auto *info = new QLabel(tr("Original size: %1 × %2 px. The height follows the width.")
                                .arg(natural.width())
                                .arg(natural.height()),
                            &dlg);
    info->setWordWrap(true);
    form->addRow(info);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset, &dlg);
    buttons->button(QDialogButtonBox::Reset)->setText(tr("Original Size"));
    connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, width,
            [width, natural] { width->setValue(natural.width()); });
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const int w = width->value();
    fmt.setWidth(w);
    fmt.setHeight(natural.width() > 0 ? double(w) * natural.height() / natural.width() : w);
    imageCursor.setCharFormat(fmt);
}

void TextEditor::insertImageFile(const QString &path)
{
    if (!m_project || !m_item)
        return;
    QImage img(path);
    if (img.isNull())
        return;
    const QString rel = m_project->docs()->importImage(m_item->id, img);
    document()->addResource(QTextDocument::ImageResource, QUrl(rel), img);
    QTextImageFormat imf;
    imf.setName(rel);
    if (img.width() > 600) {
        imf.setWidth(600);
        imf.setHeight(img.height() * 600.0 / img.width());
    }
    textCursor().insertImage(imf);
}

// ------------------------------------------------------------------ search

void TextEditor::setSearchHighlights(const QString &term, bool caseSensitive)
{
    setSearchPattern(search::pattern(term, caseSensitive, false, false));
}

void TextEditor::setSearchPattern(const QRegularExpression &re)
{
    m_searchRe = re;
    m_searchOn = re.isValid() && !re.pattern().isEmpty();
    updateSearchSelections();
}

void TextEditor::updateSearchSelections()
{
    QList<QTextEdit::ExtraSelection> sels;
    m_searchCount = 0;
    if (m_searchOn) {
        QTextCharFormat hl;
        QColor hit = theme::color(theme::Role::SearchHit);
        hit.setAlpha(160);
        hl.setBackground(hit);
        for (QTextBlock b = document()->begin(); b.isValid() && sels.size() < 5000; b = b.next()) {
            auto it = m_searchRe.globalMatch(b.text());
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                if (m.capturedLength() == 0)
                    continue;
                QTextEdit::ExtraSelection s;
                s.cursor = QTextCursor(document());
                s.cursor.setPosition(b.position() + int(m.capturedStart()));
                s.cursor.setPosition(b.position() + int(m.capturedEnd()), QTextCursor::KeepAnchor);
                s.format = hl;
                sels << s;
            }
        }
        m_searchCount = int(sels.size());
    }
    setExtraSelections(sels);
}

// The match after the position (backward: the last one that ends at or before it).
static QTextCursor findPattern(QTextDocument *doc, const QRegularExpression &re, int from, bool backward)
{
    QTextCursor found;
    if (!re.isValid())
        return found;
    QTextBlock b = doc->findBlock(qBound(0, from, qMax(0, doc->characterCount() - 1)));
    while (b.isValid()) {
        const int base = b.position();
        int start = -1, end = -1;
        auto it = re.globalMatch(b.text());
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            if (m.capturedLength() == 0)
                continue;
            const int ms = base + int(m.capturedStart()), me = base + int(m.capturedEnd());
            if (backward) {
                if (me <= from) {
                    start = ms;
                    end = me;
                }
            } else if (ms >= from) {
                start = ms;
                end = me;
                break;
            }
        }
        if (start >= 0) {
            found = QTextCursor(doc);
            found.setPosition(start);
            found.setPosition(end, QTextCursor::KeepAnchor);
            return found;
        }
        b = backward ? b.previous() : b.next();
    }
    return found;
}

bool TextEditor::findNext(const QString &term, bool backward, bool caseSensitive, bool wholeWord, bool wrap)
{
    if (term.isEmpty())
        return false;
    return findNext(search::pattern(term, caseSensitive, wholeWord, false), backward, wrap);
}

bool TextEditor::findNext(const QRegularExpression &re, bool backward, bool wrap)
{
    const QTextCursor cur = textCursor();
    QTextCursor c = findPattern(document(), re, backward ? cur.selectionStart() : cur.selectionEnd(), backward);
    if (c.isNull() && wrap)
        c = findPattern(document(), re, backward ? document()->characterCount() : 0, backward);
    if (c.isNull())
        return false;
    setTextCursor(c);
    ensureCursorVisible();
    return true;
}

int TextEditor::replaceAll(const QString &term, const QString &with, bool caseSensitive, bool wholeWord)
{
    if (term.isEmpty())
        return 0;
    return replace::inDocument(document(), search::pattern(term, caseSensitive, wholeWord, false), with, false, false);
}

} // namespace qb
