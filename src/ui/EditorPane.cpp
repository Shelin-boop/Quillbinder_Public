#include "ui/EditorPane.h"
#include <cmath>
#include "ui/Zoom.h"
#include "core/Search.h"
#include "ui/FilterBar.h"
#include "ui/DragDrop.h"
#include "ui/Ruler.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/Copyholder.h"
#include "ui/Corkboard.h"
#include "ui/MediaPlayback.h"
#include "ui/MediaView.h"
#include "ui/Outliner.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace qb {

static QToolButton *toolButton(QWidget *parent, const QString &icon, const QString &tip)
{
    auto *b = new QToolButton(parent);
    b->setIcon(icons::action(icon));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setIconSize(QSize(16, 16));
    return b;
}

EditorPane::EditorPane(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    buildHeader();
    lay->addWidget(m_header);
    buildFindBar();
    lay->addWidget(m_findBar);
    m_filterBar = new FilterBar(this);
    m_filterBar->hide();
    lay->addWidget(m_filterBar);
    connect(m_filterBar, &FilterBar::filterChanged, this, [this] { applyFilter(); });
    connect(m_filterBar, &FilterBar::done, this, &EditorPane::closeFilter);
    // the editor itself (ruler, view, footer bar) and, beside or below it, the copyholder
    m_holderSplit = new QSplitter(Qt::Horizontal, this);
    m_holderSplit->setObjectName(QStringLiteral("copyholderSplit"));
    m_holderSplit->setChildrenCollapsible(false);
    m_holderSplit->setHandleWidth(3);
    m_mainArea = new QWidget(m_holderSplit);
    auto *ml = new QVBoxLayout(m_mainArea);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->setSpacing(0);
    m_ruler = new Ruler(m_mainArea);
    m_ruler->hide();
    ml->addWidget(m_ruler);

    m_stack = new QStackedWidget(m_mainArea);
    m_text = new TextColumnView(m_stack);
    m_cork = new Corkboard(m_stack);
    m_outliner = new Outliner(m_stack);
    m_media = new MediaView(m_stack);
    m_empty = new QWidget(m_stack);
    m_empty->setAutoFillBackground(true);
    m_empty->setBackgroundRole(QPalette::Base);
    {
        auto *el = new QVBoxLayout(m_empty);
        auto *lbl = new QLabel(tr("Select a document in the binder"), m_empty);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setEnabled(false);
        el->addWidget(lbl);
    }
    m_stack->addWidget(m_text);
    m_stack->addWidget(m_cork);
    m_stack->addWidget(m_outliner);
    m_stack->addWidget(m_media);
    m_stack->addWidget(m_empty);
    m_stack->setCurrentWidget(m_empty);
    ml->addWidget(m_stack, 1);

    buildFooter();
    ml->addWidget(m_footer);
    m_holderSplit->addWidget(m_mainArea);
    m_copyholder = new Copyholder(m_holderSplit);
    m_holderSplit->addWidget(m_copyholder);
    m_copyholder->hide();
    m_copyholder->setProperty("placement", QStringLiteral("right"));
    lay->addWidget(m_holderSplit, 1);
    connect(m_copyholder, &Copyholder::closeRequested, this, &EditorPane::closeCopyholder);
    // Ctrl + mouse wheel (0.21): over the text (and the copyholder's text) the
    // project's text zoom changes, as with the zoom list of the footer; the
    // outliner, the corkboard and pictures zoom by themselves
    zoom::setHandler(this, [this](int steps) {
        if (m_project)
            m_project->docs()->setZoom(m_project->docs()->zoom() + steps * zoom::kStep);
    });
    zoom::mark(m_outliner, QLatin1String(zoom::kOutliner));
    zoom::attachFonts(m_outliner, QLatin1String(zoom::kOutliner));
    zoom::mark(m_cork, QLatin1String(zoom::kCorkboard));
    zoom::setHandler(m_media, [this](int steps) { m_media->zoomBy(std::pow(1.15, steps)); });
    connect(m_copyholder, &Copyholder::quickReferenceRequested, this, [this](BinderItem *it) {
        closeCopyholder();
        emit quickReferenceRequested(it);
    });
    connect(m_copyholder, &Copyholder::placementRequested, this, &EditorPane::setCopyholderPlacement);
    connect(m_copyholder, &Copyholder::itemsDropped, this, [this](const QList<BinderItem *> &items) {
        openCopyholder(items.first());
    });
    connect(m_copyholder, &Copyholder::activated, this, &EditorPane::emitActivated);
    connect(m_copyholder, &Copyholder::cursorMoved, this, &EditorPane::cursorMoved);
    connect(m_copyholder, &Copyholder::linkActivated, this, [this](const QString &h) { emit linkActivated(this, h); });
    connect(m_text, &TextColumnView::pagesChanged, this, [this] {
        updatePageLabel();
        m_ruler->update();
    });
    connect(m_text->horizontalScrollBar(), &QScrollBar::valueChanged, m_ruler, qOverload<>(&QWidget::update));

    // signals from the views
    connect(m_text, &TextColumnView::activeItemChanged, this, [this] {
        emit currentItemChanged(this);
        updateFooter();
    });
    connect(m_text, &TextColumnView::activeEditorChanged, this, [this] {
        emitActivated();
        updateRuler();
    });
    connect(m_text, &TextColumnView::linkActivated, this, [this](const QString &h) { emit linkActivated(this, h); });
    connect(m_text, &TextColumnView::noteClicked, this, &EditorPane::noteClicked);
    connect(m_text, &TextColumnView::cursorMoved, this, [this](TextEditor *e) {
        emit cursorMoved(e);
        updateCounts();
    });
    connect(m_cork, &Corkboard::selectionChanged, this, [this] {
        emit selectionChanged(this);
        emit currentItemChanged(this);
        if (m_affects == AffectsOtherEditor && !m_cork->selectedItems().isEmpty())
            emit openInOtherRequested(this, m_cork->selectedItems());
        else if (m_affects == AffectsCopyholder && hasCopyholder() && !m_cork->selectedItems().isEmpty())
            m_copyholder->showItem(m_cork->selectedItems().first());
    });
    connect(m_cork, &Corkboard::openRequested, this, [this](BinderItem *it) { showItems({it}); });
    connect(m_outliner, &Outliner::openRequested, this, [this](BinderItem *it) { showItems({it}); });
    connect(m_cork, &Corkboard::spacePressed, this, [this] {
        for (BinderItem *it : m_cork->selectedItems())
            emit quickReferenceRequested(it);
    });
    connect(m_outliner, &Outliner::spacePressed, this, [this] {
        for (BinderItem *it : m_outliner->selectedItems())
            emit quickReferenceRequested(it);
    });
    connect(m_cork, &Corkboard::contextMenuRequested, this,
            [this](const QPoint &p) { emit itemContextMenuRequested(this, p); });
    connect(m_cork, &Corkboard::deleteRequested, this, [this] { emit deleteRequested(this); });
    connect(m_cork, &Corkboard::newCardRequested, this, [this] { emit newItemRequested(this); });
    connect(m_outliner, &Outliner::newItemRequested, this, [this] { emit newItemRequested(this); });
    connect(m_cork, &Corkboard::filesDropped, this, &EditorPane::filesDropped);
    connect(m_outliner, &Outliner::selectionChangedByUser, this, [this] {
        emit selectionChanged(this);
        emit currentItemChanged(this);
        if (m_affects == AffectsOtherEditor && !m_outliner->selectedItems().isEmpty())
            emit openInOtherRequested(this, m_outliner->selectedItems());
        else if (m_affects == AffectsCopyholder && hasCopyholder() && !m_outliner->selectedItems().isEmpty())
            m_copyholder->showItem(m_outliner->selectedItems().first());
    });
    connect(m_outliner, &Outliner::contextMenuRequested, this,
            [this](const QPoint &p) { emit itemContextMenuRequested(this, p); });
    connect(m_outliner, &Outliner::deleteRequested, this, [this] { emit deleteRequested(this); });
    connect(m_media, &MediaView::infoChanged, this, &EditorPane::updateFooter);

    // activation on any click inside
    for (QWidget *w : {static_cast<QWidget *>(m_cork->viewport()), static_cast<QWidget *>(m_outliner->viewport()),
                       static_cast<QWidget *>(m_media), static_cast<QWidget *>(m_header), static_cast<QWidget *>(m_text->viewport())})
        w->installEventFilter(this);
}

void EditorPane::buildHeader()
{
    m_header = new QWidget(this);
    m_header->setObjectName(QStringLiteral("editorHeader"));
    m_header->setAutoFillBackground(true);
    m_header->setAcceptDrops(true);
    m_header->installEventFilter(this);
    auto *hl = new QHBoxLayout(m_header);
    hl->setContentsMargins(4, 2, 4, 2);
    hl->setSpacing(2);
    m_back = toolButton(m_header, QStringLiteral("go-previous"), tr("Back (Ctrl+[)"));
    m_forward = toolButton(m_header, QStringLiteral("go-next"), tr("Forward (Ctrl+])"));
    m_iconButton = toolButton(m_header, QStringLiteral("text-x-generic"), tr("Document options"));
    m_title = new QLineEdit(m_header);
    m_title->setFrame(false);
    m_title->setPlaceholderText(tr("Untitled"));
    QFont tf = m_title->font();
    tf.setBold(true);
    m_title->setFont(tf);
    m_title->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; }"));
    // a document dragged onto the header bar must reach the bar, not be typed into the title as text
    m_title->setAcceptDrops(false);
    m_contentsButton = toolButton(m_header, QStringLiteral("view-list-details"), tr("Go to a section"));
    m_contentsButton->setPopupMode(QToolButton::InstantPopup);
    m_up = toolButton(m_header, QStringLiteral("go-up"), tr("Previous document (Alt+Shift+Up)"));
    m_down = toolButton(m_header, QStringLiteral("go-down"), tr("Next document (Alt+Shift+Down)"));
    m_splitButton = toolButton(m_header, QStringLiteral("view-split-top-bottom"),
                               tr("Split the editor / close the split (Alt+click: other direction)"));
    hl->addWidget(m_back);
    hl->addWidget(m_forward);
    hl->addWidget(m_iconButton);
    hl->addWidget(m_title, 1);
    hl->addWidget(m_contentsButton);
    hl->addWidget(m_up);
    hl->addWidget(m_down);
    hl->addWidget(m_splitButton);

    connect(m_back, &QToolButton::clicked, this, &EditorPane::goBack);
    connect(m_forward, &QToolButton::clicked, this, &EditorPane::goForward);
    // holding the button down or a right-click lists the documents to go back / forward to
    m_back->setObjectName(QStringLiteral("historyBack"));
    m_forward->setObjectName(QStringLiteral("historyForward"));
    for (QToolButton *b : {m_back, m_forward}) {
        const bool back = b == m_back;
        auto *menu = new QMenu(b);
        menu->setObjectName(QStringLiteral("historyMenu"));
        connect(menu, &QMenu::aboutToShow, this, [this, menu, back] { fillHistoryMenu(menu, back); });
        auto *hold = new QTimer(b);
        hold->setSingleShot(true);
        hold->setInterval(450);
        connect(b, &QToolButton::pressed, hold, qOverload<>(&QTimer::start));
        connect(b, &QToolButton::released, hold, &QTimer::stop);
        connect(hold, &QTimer::timeout, this, [b, menu] {
            if (!b->isDown())
                return;
            b->setDown(false); // the menu takes over; no click when the button is let go
            menu->popup(b->mapToGlobal(QPoint(0, b->height())));
        });
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(b, &QWidget::customContextMenuRequested, this, [b, menu](const QPoint &) {
            if (b->isEnabled())
                menu->popup(b->mapToGlobal(QPoint(0, b->height())));
        });
    }
    // the icon stands for the document: it can be dragged like a document in the binder
    m_iconButton->setObjectName(QStringLiteral("headerIcon"));
    m_iconButton->setToolTip(tr("Document options (drag the icon to put the document somewhere else)"));
    dnd::makeDragHandle(
        m_iconButton, [this] { return m_project.data(); }, [this] { return m_items; });
    connect(m_up, &QToolButton::clicked, this, [this] { emit navigateSibling(this, false); });
    connect(m_down, &QToolButton::clicked, this, [this] { emit navigateSibling(this, true); });
    connect(m_splitButton, &QToolButton::clicked, this, [this] {
        emit splitButtonClicked(this, QApplication::keyboardModifiers() & Qt::AltModifier);
    });
    connect(m_iconButton, &QToolButton::clicked, this,
            [this] { emit headerMenuRequested(this, m_iconButton->mapToGlobal(QPoint(0, m_iconButton->height()))); });
    connect(m_title, &QLineEdit::editingFinished, this, [this] {
        if (!m_project || m_items.size() != 1)
            return;
        BinderItem *it = m_items.first();
        if (m_title->text() != it->title)
            m_project->model()->setData(m_project->model()->indexFor(it), m_title->text(), Qt::EditRole);
    });
    auto *contentsMenu = new QMenu(m_contentsButton);
    m_contentsButton->setMenu(contentsMenu);
    connect(contentsMenu, &QMenu::aboutToShow, this, [this, contentsMenu] {
        contentsMenu->clear();
        for (BinderItem *it : m_text->items()) {
            QString indent;
            for (int i = 0; i < it->level() - (m_items.isEmpty() ? 0 : m_items.first()->level()); ++i)
                indent += QStringLiteral("   ");
            contentsMenu->addAction(icons::forItem(m_project, it), indent + (it->title.isEmpty() ? tr("Untitled") : it->title),
                                    this, [this, it] { m_text->focusItem(it); });
        }
    });
}

void EditorPane::buildFindBar()
{
    m_findBar = new QWidget(this);
    auto *vl = new QVBoxLayout(m_findBar);
    vl->setContentsMargins(6, 3, 6, 3);
    vl->setSpacing(2);
    auto *row1 = new QHBoxLayout;
    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->setPlaceholderText(tr("Find"));
    m_findEdit->setClearButtonEnabled(true);
    auto *prev = toolButton(m_findBar, QStringLiteral("go-up"), tr("Find previous (Shift+F3)"));
    auto *next = toolButton(m_findBar, QStringLiteral("go-down"), tr("Find next (F3)"));
    m_findCase = new QCheckBox(tr("Match case"), m_findBar);
    m_findWhole = new QCheckBox(tr("Whole words"), m_findBar);
    m_findCount = new QLabel(m_findBar);
    m_findCount->setObjectName(QStringLiteral("findCount"));
    auto *close = toolButton(m_findBar, QStringLiteral("window-close"), tr("Close"));
    m_findEdit->setObjectName(QStringLiteral("findField"));
    row1->addWidget(m_findEdit, 1);
    row1->addWidget(m_findCount);
    row1->addWidget(prev);
    row1->addWidget(next);
    row1->addWidget(m_findCase);
    row1->addWidget(m_findWhole);
    row1->addWidget(close);
    vl->addLayout(row1);
    m_replaceRow = new QWidget(m_findBar);
    auto *row2 = new QHBoxLayout(m_replaceRow);
    row2->setContentsMargins(0, 0, 0, 0);
    m_replaceEdit = new QLineEdit(m_replaceRow);
    m_replaceEdit->setObjectName(QStringLiteral("replaceField"));
    m_replaceEdit->setPlaceholderText(tr("Replace with"));
    auto *rep = new QPushButton(tr("Replace"), m_replaceRow);
    auto *repAll = new QPushButton(tr("Replace All"), m_replaceRow);
    rep->setObjectName(QStringLiteral("replaceOne"));
    repAll->setObjectName(QStringLiteral("replaceAll"));
    row2->addWidget(m_replaceEdit, 1);
    row2->addWidget(rep);
    row2->addWidget(repAll);
    vl->addWidget(m_replaceRow);
    m_findBar->hide();

    connect(m_findEdit, &QLineEdit::returnPressed, this, [this] { findNext(false); });
    connect(m_findEdit, &QLineEdit::textChanged, this, [this] { updateFindHighlights(); });
    connect(m_findCase, &QCheckBox::toggled, this, [this] { updateFindHighlights(); });
    connect(m_findWhole, &QCheckBox::toggled, this, [this] { updateFindHighlights(); });
    connect(prev, &QToolButton::clicked, this, [this] { findNext(true); });
    connect(next, &QToolButton::clicked, this, [this] { findNext(false); });
    connect(close, &QToolButton::clicked, this, [this] {
        const QList<TextEditor *> eds = findEditors();
        m_findBar->hide();
        if (m_findInHolder)
            for (TextEditor *e : eds)
                e->setSearchPattern(QRegularExpression(QStringLiteral("(?")));
        m_findInHolder = false;
        updateFindHighlights(); // back to the matches of the project search, if there is one
        if (!eds.isEmpty() && eds.first()->isVisible())
            eds.first()->setFocus();
        else
            focusEditor();
    });
    connect(rep, &QPushButton::clicked, this, [this] {
        const QList<TextEditor *> eds = findEditors();
        TextEditor *e = nullptr;
        for (TextEditor *x : eds)
            if (x->hasFocus() || x == currentEditor())
                e = x;
        if (!e && !eds.isEmpty())
            e = eds.first();
        if (!e)
            return;
        QTextCursor c = e->textCursor();
        const QRegularExpressionMatch m = findPattern().match(c.selectedText());
        if (c.hasSelection() && m.hasMatch() && m.capturedLength() == c.selectedText().size())
            c.insertText(m_replaceEdit->text());
        findNext(false);
    });
    connect(repAll, &QPushButton::clicked, this, [this] {
        int n = 0;
        for (TextEditor *e : findEditors())
            n += e->replaceAll(m_findEdit->text(), m_replaceEdit->text(), m_findCase->isChecked(), m_findWhole->isChecked());
        m_replaceEdit->setToolTip(text::plural(n, tr("replacement"), tr("replacements")));
        updateFindHighlights();
        m_findCount->setText(text::plural(n, tr("replacement"), tr("replacements")));
    });
}

QList<TextEditor *> EditorPane::findEditors() const
{
    if (m_findInHolder && hasCopyholder() && m_copyholder->editor())
        return {m_copyholder->editor()};
    return m_text->editors();
}

QRegularExpression EditorPane::findPattern() const
{
    return search::pattern(m_findEdit->text(), m_findCase->isChecked(), m_findWhole->isChecked(), false);
}

bool EditorPane::findBarVisible() const { return !m_findBar->isHidden(); }

// What is highlighted in the texts: the find bar's text while it is open,
// otherwise the matches of the project search.
void EditorPane::updateFindHighlights()
{
    const bool bar = findBarVisible();
    const QRegularExpression re = bar ? findPattern() : m_searchRe;
    int n = 0;
    for (TextEditor *e : findEditors()) {
        e->setSearchPattern(re);
        n += e->searchMatchCount();
    }
    m_findCount->setText(bar && !m_findEdit->text().isEmpty() ? text::plural(n, tr("match"), tr("matches")) : QString());
}

void EditorPane::buildFooter()
{
    m_footer = new QStackedWidget(this);
    m_footer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    // text footer
    auto *tf = new QWidget(m_footer);
    auto *tl = new QHBoxLayout(tf);
    tl->setContentsMargins(4, 1, 4, 1);
    m_zoom = new QComboBox(tf);
    for (int z : {50, 75, 100, 110, 125, 150, 175, 200, 250, 300})
        m_zoom->addItem(QStringLiteral("%1%").arg(z), z);
    m_zoom->setToolTip(tr("Text zoom (does not change the font size)"));
    m_zoom->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    tl->addWidget(m_zoom);
    m_pageLabel = new QLabel(tf);
    m_pageLabel->setObjectName(QStringLiteral("pageLabel"));
    m_pageLabel->setToolTip(tr("Page view: the page of the cursor and the number of pages shown"));
    m_pageLabel->hide();
    tl->addWidget(m_pageLabel);
    tl->addStretch(1);
    m_countButton = new QToolButton(tf);
    m_countButton->setAutoRaise(true);
    m_countButton->setToolTip(tr("Click for details"));
    tl->addWidget(m_countButton);
    tl->addStretch(1);
    // script mode: what Tab and Enter do, and the element of the paragraph
    m_scriptHint = new QLabel(tf);
    m_scriptHint->setObjectName(QStringLiteral("scriptHint"));
    m_scriptHint->setToolTip(tr("Script mode: what the Tab and Enter keys do at the cursor"));
    m_scriptHint->hide();
    tl->addWidget(m_scriptHint);
    m_scriptElement = new QToolButton(tf);
    m_scriptElement->setObjectName(QStringLiteral("scriptElementButton"));
    m_scriptElement->setAutoRaise(true);
    m_scriptElement->setPopupMode(QToolButton::InstantPopup);
    m_scriptElement->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_scriptElement->setToolTip(tr("Script element of the paragraph (click to change it)"));
    m_scriptElement->hide();
    auto *elementMenu = new QMenu(m_scriptElement);
    elementMenu->setObjectName(QStringLiteral("scriptFooterMenu"));
    m_scriptElement->setMenu(elementMenu);
    connect(elementMenu, &QMenu::aboutToShow, this, [this, elementMenu] {
        elementMenu->clear();
        TextEditor *e = currentEditor();
        if (!e)
            return;
        QMenu *src = e->scriptElementsMenu();
        for (QAction *a : src->actions()) {
            if (a->isSeparator()) {
                elementMenu->addSeparator();
                continue;
            }
            QAction *n = elementMenu->addAction(a->text());
            n->setCheckable(true);
            n->setChecked(a->isChecked());
            const QString name = a->data().toString();
            connect(n, &QAction::triggered, this, [this, name] {
                if (TextEditor *ed = currentEditor()) {
                    ed->setScriptElement(name);
                    ed->setFocus();
                }
            });
        }
        src->deleteLater();
    });
    tl->addWidget(m_scriptElement);
    m_targetBar = new QProgressBar(tf);
    m_targetBar->setMaximumWidth(120);
    m_targetBar->setMaximumHeight(12);
    m_targetBar->setTextVisible(false);
    m_targetBar->installEventFilter(this);
    m_targetBar->setToolTip(tr("Document target (click to edit)"));
    tl->addWidget(m_targetBar);
    m_targetButton = toolButton(tf, QStringLiteral("flag"), tr("Set a word target for this document"));
    tl->addWidget(m_targetButton);
    m_footer->addWidget(tf);
    connect(m_zoom, qOverload<int>(&QComboBox::activated), this, [this](int i) {
        if (m_project)
            m_project->docs()->setZoom(m_zoom->itemData(i).toInt() / 100.0);
        m_text->applySettings();
    });
    connect(m_targetButton, &QToolButton::clicked, this, &EditorPane::editTarget);
    connect(m_countButton, &QToolButton::clicked, this, [this] {
        QString txt;
        int words = 0, chars = 0, nospace = 0, paras = 0;
        for (TextEditor *e : m_text->editors()) {
            const text::Counts c = text::countDocument(e->document());
            words += c.words;
            chars += c.chars;
            nospace += c.charsNoSpaces;
            paras += c.paragraphs;
        }
        QLocale loc;
        txt = tr("Words: %1\nCharacters: %2\nCharacters (no spaces): %3\nParagraphs: %4\nPages (250 words): %5")
                  .arg(loc.toString(words), loc.toString(chars), loc.toString(nospace), loc.toString(paras))
                  .arg(loc.toString(words / 250.0, 'f', 1));
        QMenu m;
        for (const QString &l : txt.split(QLatin1Char('\n')))
            m.addAction(l)->setEnabled(false);
        m.exec(m_countButton->mapToGlobal(QPoint(0, -int(m.sizeHint().height()))));
    });

    // corkboard footer
    auto *cf = new QWidget(m_footer);
    auto *cl = new QHBoxLayout(cf);
    cl->setContentsMargins(4, 1, 4, 1);
    m_corkGrid = toolButton(cf, QStringLiteral("view-list-icons"), tr("Grid corkboard"));
    m_corkFree = toolButton(cf, QStringLiteral("games-config-board"), tr("Freeform corkboard: place cards anywhere"));
    m_corkLabels = toolButton(cf, QStringLiteral("view-time-schedule-baselined"), tr("Arrange by label (label threads)"));
    for (QToolButton *b : {m_corkGrid, m_corkFree, m_corkLabels})
        b->setCheckable(true);
    m_corkGrid->setChecked(true);
    m_corkCommit = new QToolButton(cf);
    m_corkCommit->setText(tr("Commit Order"));
    m_corkCommit->setToolTip(tr("Reorder the documents in the binder to match the freeform arrangement"));
    m_corkCommit->hide();
    m_cardSize = new QSlider(Qt::Horizontal, cf);
    m_cardSize->setRange(120, 480);
    m_cardSize->setValue(AppSettings::instance().cardWidth);
    m_cardSize->setMaximumWidth(140);
    m_cardSize->setToolTip(tr("Card size (Ctrl+mouse wheel zooms the whole board)"));
    m_affectCork = toolButton(cf, QStringLiteral("document-export"), tr("Selection affects the other editor"));
    m_affectCork->setCheckable(true);
    cl->addWidget(m_corkGrid);
    cl->addWidget(m_corkFree);
    cl->addWidget(m_corkLabels);
    cl->addWidget(m_corkCommit);
    cl->addStretch(1);
    cl->addWidget(new QLabel(tr("Size"), cf));
    cl->addWidget(m_cardSize);
    cl->addWidget(m_affectCork);
    m_footer->addWidget(cf);
    connect(m_corkGrid, &QToolButton::clicked, this, [this] { setCorkMode(int(Corkboard::Mode::Grid)); });
    connect(m_corkFree, &QToolButton::clicked, this, [this] { setCorkMode(int(Corkboard::Mode::Freeform)); });
    connect(m_corkLabels, &QToolButton::clicked, this, [this] { setCorkMode(int(Corkboard::Mode::LabelThreads)); });
    connect(m_corkCommit, &QToolButton::clicked, m_cork, &Corkboard::commitFreeformOrder);
    connect(m_cardSize, &QSlider::valueChanged, this, [this](int v) {
        AppSettings::instance().cardWidth = v;
        m_cork->rebuild();
    });
    connect(m_cardSize, &QSlider::sliderReleased, this, [] { AppSettings::instance().save(); });

    // outliner footer
    auto *of = new QWidget(m_footer);
    auto *ol = new QHBoxLayout(of);
    ol->setContentsMargins(4, 1, 4, 1);
    m_synopsisToggle = toolButton(of, QStringLiteral("view-list-details"), tr("Show / hide synopses"));
    m_synopsisToggle->setCheckable(true);
    m_synopsisToggle->setChecked(true);
    auto *cols = toolButton(of, QStringLiteral("view-choose"), tr("Choose columns"));
    m_affectOutliner = toolButton(of, QStringLiteral("document-export"), tr("Selection affects the other editor"));
    m_affectOutliner->setCheckable(true);
    ol->addWidget(m_synopsisToggle);
    ol->addWidget(cols);
    ol->addStretch(1);
    ol->addWidget(m_affectOutliner);
    m_footer->addWidget(of);
    connect(m_synopsisToggle, &QToolButton::toggled, m_outliner, &Outliner::setShowSynopsis);
    connect(cols, &QToolButton::clicked, this, [this, cols] { m_outliner->showColumnMenu(cols->mapToGlobal(QPoint(0, 0))); });
    for (QToolButton *b : {m_affectCork, m_affectOutliner})
        connect(b, &QToolButton::clicked, this, &EditorPane::cycleSelectionAffects);

    // media footer: picture and PDF controls, or the play controls of audio and video
    m_mediaFooter = new QStackedWidget(m_footer);
    auto *mf = new QWidget(m_mediaFooter);
    auto *ml = new QHBoxLayout(mf);
    ml->setContentsMargins(4, 1, 4, 1);
    auto *zoomOut = toolButton(mf, QStringLiteral("zoom-out"), tr("Zoom out"));
    auto *zoomIn = toolButton(mf, QStringLiteral("zoom-in"), tr("Zoom in"));
    auto *fit = toolButton(mf, QStringLiteral("zoom-fit-best"), tr("Fit"));
    auto *pPrev = toolButton(mf, QStringLiteral("go-previous"), tr("Previous page"));
    auto *pNext = toolButton(mf, QStringLiteral("go-next"), tr("Next page"));
    m_mediaInfo = new QLabel(mf);
    ml->addWidget(zoomOut);
    ml->addWidget(zoomIn);
    ml->addWidget(fit);
    ml->addStretch(1);
    ml->addWidget(pPrev);
    ml->addWidget(m_mediaInfo);
    ml->addWidget(pNext);
    m_mediaFooter->addWidget(mf);
    m_mediaControls = new playback::MediaControls(m_mediaFooter);
    m_mediaControls->setView(m_media);
    m_mediaFooter->addWidget(m_mediaControls);
    m_footer->addWidget(m_mediaFooter);
    connect(m_media, &MediaView::playbackChanged, this, [this] {
        m_mediaFooter->setCurrentWidget(m_media->isPlayer() ? static_cast<QWidget *>(m_mediaControls) : m_mediaFooter->widget(0));
    });
    connect(zoomOut, &QToolButton::clicked, this, [this] { m_media->zoomBy(1 / 1.25); });
    connect(zoomIn, &QToolButton::clicked, this, [this] { m_media->zoomBy(1.25); });
    connect(fit, &QToolButton::clicked, m_media, &MediaView::fitToWindow);
    connect(pPrev, &QToolButton::clicked, this, [this] { m_media->pdfPage(-1); });
    connect(pNext, &QToolButton::clicked, this, [this] { m_media->pdfPage(1); });

    // empty footer
    m_footer->addWidget(new QWidget(m_footer));
    m_footer->setCurrentIndex(4);
}

void EditorPane::setProject(Project *p)
{
    if (m_project)
        disconnect(m_project, nullptr, this, nullptr);
    m_project = p;
    m_text->setProject(p);
    m_cork->setProject(p);
    m_outliner->setProject(p);
    m_media->setProject(p);
    m_copyholder->setProject(p);
    m_copyholder->hide();
    m_filterBar->setProject(p);
    closeFilter();
    m_findInHolder = false;
    m_searchRe = QRegularExpression(QStringLiteral("(?"));
    if (m_affects == AffectsCopyholder)
        m_affects = AffectsNothing;
    updateAffectButtons();
    m_items.clear();
    m_history.clear();
    m_historyIndex = -1;
    m_mode = ViewMode::Empty;
    applyMode();
    if (p) {
        connect(p, &Project::itemChanged, this, [this](BinderItem *it) {
            if (m_items.contains(it))
                refreshHeader();
            m_text->refreshTitles();
            if (it == currentItem())
                updateFooter();
        });
        connect(p->docs(), &DocumentStore::countsUpdated, this, [this] { updateCounts(); });
        connect(p->docs(), &DocumentStore::zoomChanged, this, [this](double z) {
            const int idx = m_zoom->findData(qRound(z * 100));
            if (idx >= 0)
                m_zoom->setCurrentIndex(idx);
            m_text->applySettings();
        });
        const int idx = m_zoom->findData(qRound(p->docs()->zoom() * 100));
        m_zoom->setCurrentIndex(idx >= 0 ? idx : 2);
    }
}

bool EditorPane::isGroup() const
{
    return m_items.size() > 1 || (m_items.size() == 1 && m_items.first()->isContainer());
}

QList<BinderItem *> EditorPane::combinedItems() const
{
    QList<BinderItem *> out;
    for (BinderItem *it : BinderModel::topLevelOnly(m_items)) {
        if (it->hasText() && it->type != ItemType::Trash)
            out << it;
        for (BinderItem *d : it->descendants())
            if (d->hasText())
                out << d;
    }
    // the Draft folder's own (usually empty) text is not interesting
    if (out.size() > 1 && m_project && out.first() == m_project->draft() && m_project->docs()->plainText(out.first()->id).isEmpty())
        out.removeFirst();
    return out;
}

void EditorPane::showItems(const QList<BinderItem *> &itemsIn, bool addToHistory, int forcedMode)
{
    if (!m_project)
        return;
    QList<BinderItem *> items;
    for (BinderItem *it : itemsIn)
        if (it && m_project->item(it->id))
            items << it;
    m_items = items;
    if (forcedMode >= 0) {
        m_mode = ViewMode(forcedMode);
    } else if (items.isEmpty()) {
        m_mode = ViewMode::Empty;
    } else if (items.size() == 1 && isMediaType(items.first()->type)) {
        m_mode = ViewMode::Media;
    } else if (items.size() == 1 && !items.first()->isContainer()) {
        m_mode = ViewMode::Single;
    } else if (items.size() == 1 && !items.first()->isFolder() && !AppSettings::instance().subdocsAsFolders) {
        // File > Options > Behaviors > Folders & Files (0.21): a text with subdocuments shows its own text
        m_mode = ViewMode::Single;
    } else {
        m_mode = groupMode();
        if (m_mode == ViewMode::Single && items.size() > 1)
            m_mode = ViewMode::Combined;
    }
    if (m_mode == ViewMode::Media && (items.size() != 1 || !isMediaType(items.first()->type)))
        m_mode = ViewMode::Single;
    applyMode();
    if (addToHistory)
        pushHistory();
    refreshHeader();
    emit currentItemChanged(this);
    emit viewChanged(this);
}

void EditorPane::setViewMode(ViewMode m)
{
    if (m_items.isEmpty())
        return;
    if (m_mode == m)
        return;
    if (isGroup() && m != ViewMode::Media)
        setGroupMode(m);
    m_mode = m;
    if (m_mode == ViewMode::Single && m_items.size() > 1)
        m_mode = ViewMode::Combined;
    applyMode();
    if (m_historyIndex >= 0 && m_historyIndex < m_history.size())
        m_history[m_historyIndex].mode = int(m_mode);
    refreshHeader();
    emit currentItemChanged(this);
    emit viewChanged(this);
}

ViewMode EditorPane::groupMode() const
{
    int remembered = -1;
    if (m_project) {
        remembered = m_project->uiState.value(QStringLiteral("groupMode") + m_stateSuffix, -1).toInt();
        if (remembered < 0)
            remembered = m_project->uiState.value(QStringLiteral("groupMode"), -1).toInt();
    }
    if (remembered >= 0 && remembered <= int(ViewMode::Outliner))
        return ViewMode(remembered);
    return AppSettings::instance().startWithCorkboard ? ViewMode::Corkboard : ViewMode::Combined;
}

void EditorPane::setGroupMode(ViewMode m)
{
    if (!m_project || int(m) > int(ViewMode::Outliner))
        return;
    const QString key = QStringLiteral("groupMode") + m_stateSuffix;
    if (m_project->uiState.value(key, -1).toInt() == int(m))
        return;
    m_project->uiState[key] = int(m);
    m_project->markIndexDirty();
}

void EditorPane::setOutlinerSynopsis(bool on)
{
    m_synopsisToggle->setChecked(on); // the button switches the outliner
    emit viewChanged(this);
}

int EditorPane::corkMode() const { return int(m_cork->mode()); }

void EditorPane::setCorkMode(int mode)
{
    const Corkboard::Mode m = Corkboard::Mode(qBound(0, mode, 2));
    if (m_cork->mode() != m)
        m_cork->setMode(m);
    m_corkGrid->setChecked(m == Corkboard::Mode::Grid);
    m_corkFree->setChecked(m == Corkboard::Mode::Freeform);
    m_corkLabels->setChecked(m == Corkboard::Mode::LabelThreads);
    m_corkCommit->setVisible(m == Corkboard::Mode::Freeform);
    emit viewChanged(this);
}

void EditorPane::setSelectionAffectsOther(bool on)
{
    setSelectionAffects(on ? AffectsOtherEditor : AffectsNothing);
}

void EditorPane::setSelectionAffects(int what)
{
    if (what == AffectsCopyholder && !hasCopyholder())
        what = AffectsNothing;
    m_affects = qBound(int(AffectsNothing), what, int(AffectsCopyholder));
    updateAffectButtons();
    emit viewChanged(this);
}

void EditorPane::cycleSelectionAffects()
{
    // off -> other editor -> copyholder (while one is open) -> off
    if (m_affects == AffectsNothing)
        setSelectionAffects(AffectsOtherEditor);
    else if (m_affects == AffectsOtherEditor && hasCopyholder())
        setSelectionAffects(AffectsCopyholder);
    else
        setSelectionAffects(AffectsNothing);
}

void EditorPane::updateAffectButtons()
{
    const bool holder = m_affects == AffectsCopyholder;
    const QString tip = m_affects == AffectsNothing ? tr("Selection affects nothing (click: the other editor)")
                        : holder                    ? tr("Selection opens in the copyholder (click: switch off)")
                        : hasCopyholder()           ? tr("Selection opens in the other editor (click: the copyholder)")
                                                    : tr("Selection opens in the other editor (click: switch off)");
    for (QToolButton *b : {m_affectCork, m_affectOutliner}) {
        QSignalBlocker block(b);
        b->setChecked(m_affects != AffectsNothing);
        b->setIcon(icons::action(holder ? QStringLiteral("document-import") : QStringLiteral("document-export")));
        b->setToolTip(tip);
        b->setProperty("affects", m_affects);
    }
}

// ------------------------------------------------------------------ copyholder

bool EditorPane::hasCopyholder() const { return !m_copyholder->isHidden() && m_copyholder->item(); }

BinderItem *EditorPane::copyholderItem() const { return hasCopyholder() ? m_copyholder->item() : nullptr; }

QString EditorPane::copyholderPlacement() const { return m_copyholder->property("placement").toString(); }

void EditorPane::placeCopyholder(const QString &placement)
{
    const bool vertical = placement == QLatin1String("top") || placement == QLatin1String("bottom");
    const bool first = placement == QLatin1String("left") || placement == QLatin1String("top");
    m_holderSplit->setOrientation(vertical ? Qt::Vertical : Qt::Horizontal);
    if ((m_holderSplit->indexOf(m_copyholder) == 0) != first)
        m_holderSplit->insertWidget(first ? 0 : 1, m_copyholder);
    m_copyholder->setProperty("placement", placement);
}

void EditorPane::setCopyholderPlacement(const QString &placementIn)
{
    QString placement = placementIn;
    if (!QStringList{QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("top"), QStringLiteral("bottom")}
             .contains(placement))
        placement = QStringLiteral("right");
    const bool was = hasCopyholder();
    placeCopyholder(placement);
    if (m_project) {
        m_project->uiState[QStringLiteral("copyholderPlacement") + m_stateSuffix] = placement;
        m_project->markIndexDirty();
    }
    if (was) {
        const int total = m_holderSplit->orientation() == Qt::Vertical ? m_holderSplit->height() : m_holderSplit->width();
        const int holder = total * 2 / 5;
        m_holderSplit->setSizes(m_holderSplit->indexOf(m_copyholder) == 0 ? QList<int>{holder, total - holder}
                                                                             : QList<int>{total - holder, holder});
        emit copyholderChanged(this);
    }
}

void EditorPane::openCopyholder(BinderItem *item)
{
    if (!m_project || !item || !m_project->item(item->id))
        return;
    const bool was = hasCopyholder();
    if (!was) {
        // where it was the last time; the first time beside a wide editor, below a narrow one
        QString placement = m_project->uiState.value(QStringLiteral("copyholderPlacement") + m_stateSuffix).toString();
        if (placement.isEmpty())
            placement = m_holderSplit->width() >= 900 ? QStringLiteral("right") : QStringLiteral("bottom");
        placeCopyholder(placement);
        m_copyholder->show();
    }
    m_copyholder->showItem(item);
    if (!was) {
        const int total = m_holderSplit->orientation() == Qt::Vertical ? m_holderSplit->height() : m_holderSplit->width();
        const int holder = total * 2 / 5;
        m_holderSplit->setSizes(m_holderSplit->indexOf(m_copyholder) == 0 ? QList<int>{holder, total - holder}
                                                                             : QList<int>{total - holder, holder});
    }
    updateAffectButtons();
    emit copyholderChanged(this);
}

void EditorPane::closeCopyholder()
{
    if (m_copyholder->isHidden())
        return;
    const bool hadFocus = m_copyholder->isAncestorOf(QApplication::focusWidget());
    if (m_findInHolder) {
        m_findBar->hide();
        m_findInHolder = false;
    }
    m_copyholder->hide();
    m_copyholder->clear();
    if (m_affects == AffectsCopyholder)
        m_affects = AffectsNothing;
    updateAffectButtons();
    if (hadFocus)
        focusEditor();
    emit copyholderChanged(this);
    emit viewChanged(this);
}

TextEditor *EditorPane::focusedCopyholderEditor() const
{
    if (!hasCopyholder())
        return nullptr;
    TextEditor *e = m_copyholder->editor();
    return e && e->hasFocus() ? e : nullptr;
}

void EditorPane::updatePageLabel()
{
    int page = 0, pages = 0;
    const bool text = m_mode == ViewMode::Single || m_mode == ViewMode::Combined;
    if (text && m_text->pageInfo(&page, &pages)) {
        m_pageLabel->setText(page > 0 ? tr("Page %1 of %2").arg(page).arg(pages) : text::plural(pages, tr("page"), tr("pages")));
        m_pageLabel->show();
    } else {
        m_pageLabel->hide();
    }
}

void EditorPane::applyMode()
{
    if (m_findInHolder && findBarVisible() && hasCopyholder()) {
        // the find bar of the copyholder stays with it
    } else {
        m_findBar->hide();
        m_findInHolder = false;
    }
    const bool lists = m_mode == ViewMode::Corkboard || m_mode == ViewMode::Outliner;
    m_filterBar->setVisible(m_filterOpen && lists);
    // the view that is not shown has no filter (it would be invisible there)
    const ItemFilter f = m_filterOpen ? m_filterBar->filter() : ItemFilter();
    m_cork->setFilter(m_mode == ViewMode::Corkboard ? f : ItemFilter());
    m_outliner->setFilter(m_mode == ViewMode::Outliner ? f : ItemFilter());
    if (m_mode != ViewMode::Media)
        m_media->release(); // stops what was playing here
    switch (m_mode) {
    case ViewMode::Empty:
        m_text->clear();
        m_stack->setCurrentWidget(m_empty);
        m_footer->setCurrentIndex(4);
        break;
    case ViewMode::Single:
        if (!m_items.isEmpty() && m_items.first()->hasText()) {
            m_text->setItems({m_items.first()}, false);
            m_stack->setCurrentWidget(m_text);
            m_footer->setCurrentIndex(0);
        } else {
            m_text->clear();
            m_stack->setCurrentWidget(m_empty);
            m_footer->setCurrentIndex(4);
        }
        break;
    case ViewMode::Combined:
        m_text->setItems(combinedItems(), true);
        m_stack->setCurrentWidget(m_text);
        m_footer->setCurrentIndex(0);
        break;
    case ViewMode::Corkboard:
    case ViewMode::Outliner: {
        m_text->clear();
        QList<BinderItem *> containers, loose;
        if (m_items.size() == 1) {
            containers << m_items.first();
        } else {
            for (BinderItem *it : m_items)
                (it->isContainer() ? containers : loose) << it;
        }
        if (m_mode == ViewMode::Corkboard) {
            m_cork->setContent(containers, loose);
            m_stack->setCurrentWidget(m_cork);
            m_footer->setCurrentIndex(1);
        } else {
            m_outliner->setContent(containers, loose);
            m_stack->setCurrentWidget(m_outliner);
            m_footer->setCurrentIndex(2);
            m_synopsisToggle->setChecked(m_outliner->showSynopsis());
        }
        break;
    }
    case ViewMode::Media:
        m_text->clear();
        m_media->showItem(m_items.first());
        m_stack->setCurrentWidget(m_media);
        m_footer->setCurrentIndex(3);
        break;
    }
    if (lists && m_filterOpen) {
        int shown = 0, total = 0;
        if (m_mode == ViewMode::Corkboard)
            m_cork->filterCounts(&shown, &total);
        else
            m_outliner->filterCounts(&shown, &total);
        m_filterBar->setCount(shown, total);
    }
    updateFindHighlights();
    updateFooter();
    updateRuler();
}

void EditorPane::updateRuler()
{
    TextEditor *e = currentEditor();
    m_ruler->setEditor(e);
    m_ruler->setVisible(e && AppSettings::instance().showRuler);
    m_ruler->update();
}

void EditorPane::refresh()
{
    if (!m_project)
        return;
    QList<BinderItem *> still;
    for (BinderItem *it : m_items)
        if (m_project->item(it->id))
            still << it;
    if (m_copyholder->item() && !m_project->item(m_copyholder->item()->id))
        closeCopyholder(); // its document is gone
    if (still.size() != m_items.size()) {
        m_items = still;
        applyMode();
    } else if (m_mode == ViewMode::Combined) {
        // structure may have changed: rebuild if the list differs
        if (m_text->items() != combinedItems()) {
            const int scroll = m_text->scrollValue();
            applyMode();
            m_text->setScrollValue(scroll);
        }
    }
    refreshHeader();
}

void EditorPane::pushHistory()
{
    HistoryEntry e;
    for (BinderItem *it : m_items)
        e.ids << it->id;
    e.mode = int(m_mode);
    if (m_historyIndex >= 0 && m_historyIndex < m_history.size() && m_history[m_historyIndex].ids == e.ids)
        return;
    while (m_history.size() > m_historyIndex + 1)
        m_history.removeLast();
    m_history.append(e);
    if (m_history.size() > 100)
        m_history.removeFirst();
    m_historyIndex = m_history.size() - 1;
}

void EditorPane::goBack()
{
    if (!canGoBack() || !m_project)
        return;
    --m_historyIndex;
    const HistoryEntry &e = m_history[m_historyIndex];
    QList<BinderItem *> items;
    for (const QString &id : e.ids)
        if (BinderItem *it = m_project->item(id))
            items << it;
    showItems(items, false, e.mode);
}

void EditorPane::goForward()
{
    if (!canGoForward() || !m_project)
        return;
    ++m_historyIndex;
    const HistoryEntry &e = m_history[m_historyIndex];
    QList<BinderItem *> items;
    for (const QString &id : e.ids)
        if (BinderItem *it = m_project->item(id))
            items << it;
    showItems(items, false, e.mode);
}

QString EditorPane::historyTitle(int index) const
{
    if (index < 0 || index >= m_history.size() || !m_project)
        return QString();
    QStringList titles;
    for (const QString &id : m_history[index].ids)
        if (BinderItem *it = m_project->item(id))
            titles << (it->title.isEmpty() ? tr("Untitled") : it->title);
    if (titles.isEmpty())
        return QString();
    if (titles.size() == 1)
        return titles.first();
    return tr("%1 and %2 more").arg(titles.first()).arg(titles.size() - 1);
}

void EditorPane::goToHistory(int index)
{
    if (index < 0 || index >= m_history.size() || index == m_historyIndex || !m_project)
        return;
    m_historyIndex = index;
    const HistoryEntry e = m_history[index];
    QList<BinderItem *> items;
    for (const QString &id : e.ids)
        if (BinderItem *it = m_project->item(id))
            items << it;
    if (items.isEmpty()) { // deleted meanwhile
        refreshHeader();
        return;
    }
    showItems(items, false, e.mode);
}

void EditorPane::fillHistoryMenu(QMenu *menu, bool back)
{
    menu->clear();
    int shown = 0;
    // nearest first, as the button would go
    for (int i = back ? m_historyIndex - 1 : m_historyIndex + 1; i >= 0 && i < m_history.size() && shown < 20;
         i += back ? -1 : 1) {
        const QString title = historyTitle(i);
        if (title.isEmpty())
            continue;
        BinderItem *first = m_project ? m_project->item(m_history[i].ids.value(0)) : nullptr;
        QAction *ac = menu->addAction(first && m_history[i].ids.size() == 1 ? icons::forItem(m_project, first)
                                                                              : icons::get(QStringLiteral("document-multiple")),
                                      QString(title).replace(QLatin1Char('&'), QLatin1String("&&")));
        connect(ac, &QAction::triggered, this, [this, i] { goToHistory(i); });
        ++shown;
    }
    if (!shown)
        menu->addAction(back ? tr("Nothing to go back to") : tr("Nothing to go forward to"))->setEnabled(false);
}

bool EditorPane::canGoToEnclosingGroup() const
{
    BinderItem *it = m_items.size() == 1 ? m_items.first() : currentItem();
    return m_project && it && it->parent && it->parent != m_project->root();
}

void EditorPane::goToEnclosingGroup()
{
    if (!canGoToEnclosingGroup())
        return;
    BinderItem *it = m_items.size() == 1 ? m_items.first() : currentItem();
    BinderItem *group = it->parent;
    showItems({group});
    // the document we came from stays selected among its siblings
    if (m_mode == ViewMode::Corkboard)
        m_cork->selectItems({it});
    else if (m_mode == ViewMode::Outliner)
        m_outliner->selectItems({it});
    else if (m_mode == ViewMode::Combined)
        m_text->focusItem(it);
}

void EditorPane::refreshHeader()
{
    m_back->setEnabled(canGoBack());
    m_forward->setEnabled(canGoForward());
    m_contentsButton->setVisible(m_mode == ViewMode::Combined);
    if (m_items.isEmpty()) {
        m_title->setText(QString());
        m_title->setReadOnly(true);
        m_iconButton->setIcon(icons::action(QStringLiteral("text-x-generic")));
    } else if (m_items.size() == 1) {
        BinderItem *it = m_items.first();
        if (!m_title->hasFocus())
            m_title->setText(it->title);
        m_title->setReadOnly(false);
        m_iconButton->setIcon(icons::forItem(m_project, it));
    } else {
        m_title->setText(text::plural(int(m_items.size()), tr("document"), tr("documents")));
        m_title->setReadOnly(true);
        m_iconButton->setIcon(icons::get(QStringLiteral("document-multiple")));
    }
    QPalette pal = QApplication::palette();
    QColor bg = theme::color(theme::Role::HeaderBar);
    QColor fg = theme::color(theme::Role::HeaderBarText);
    if (m_locked)
        bg = theme::color(theme::Role::HeaderBarLocked);
    else if (m_active)
        bg = theme::color(theme::Role::HeaderBarActive);
    if ((bg.lightness() < 128) == (fg.lightness() < 128)) // e.g. a pink lock bar in a dark theme
        fg = bg.lightness() < 128 ? QColor(240, 240, 240) : QColor(30, 30, 30);
    pal.setColor(QPalette::Window, bg);
    pal.setColor(QPalette::WindowText, fg);
    pal.setColor(QPalette::ButtonText, fg);
    m_header->setPalette(pal);
    m_title->setFont(theme::font(theme::FontRole::HeaderTitle));
    // the title field is styled; give it the text color of the header bar
    m_title->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; color: %1; }").arg(fg.name()));
}

void EditorPane::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    if (e->type() == QEvent::ApplicationPaletteChange || e->type() == QEvent::PaletteChange)
        if (!m_inPaletteChange) {
            m_inPaletteChange = true;
            refreshHeader();
            m_inPaletteChange = false;
        }
}

void EditorPane::setLocked(bool on)
{
    m_locked = on;
    refreshHeader();
}

void EditorPane::setSplitIcon(Qt::Orientation o)
{
    m_splitButton->setIcon(icons::action(o == Qt::Vertical ? QStringLiteral("view-split-top-bottom")
                                                            : QStringLiteral("view-split-left-right")));
}

void EditorPane::setActive(bool on, bool splitExists)
{
    m_active = on && splitExists;
    refreshHeader();
}

void EditorPane::emitActivated() { emit activated(this); }

BinderItem *EditorPane::currentItem() const
{
    switch (m_mode) {
    case ViewMode::Single:
    case ViewMode::Media:
        return m_items.isEmpty() ? nullptr : m_items.first();
    case ViewMode::Combined:
        return m_text->activeItem() ? m_text->activeItem() : (m_items.isEmpty() ? nullptr : m_items.first());
    case ViewMode::Corkboard: {
        const auto sel = m_cork->selectedItems();
        if (sel.size() == 1)
            return sel.first();
        return m_items.size() == 1 ? m_items.first() : nullptr;
    }
    case ViewMode::Outliner: {
        const auto sel = m_outliner->selectedItems();
        if (sel.size() == 1)
            return sel.first();
        return m_items.size() == 1 ? m_items.first() : nullptr;
    }
    case ViewMode::Empty: break;
    }
    return nullptr;
}

QList<BinderItem *> EditorPane::selectedItems() const
{
    if (m_mode == ViewMode::Corkboard)
        return m_cork->selectedItems();
    if (m_mode == ViewMode::Outliner)
        return m_outliner->selectedItems();
    BinderItem *c = currentItem();
    return c ? QList<BinderItem *>{c} : QList<BinderItem *>();
}

TextEditor *EditorPane::currentEditor() const
{
    if (m_mode == ViewMode::Single || m_mode == ViewMode::Combined)
        return m_text->activeEditor();
    return nullptr;
}

void EditorPane::focusEditor()
{
    if (TextEditor *e = currentEditor())
        e->setFocus();
    else if (m_mode == ViewMode::Corkboard)
        m_cork->setFocus();
    else if (m_mode == ViewMode::Outliner)
        m_outliner->setFocus();
}

void EditorPane::updateCounts()
{
    if (m_mode != ViewMode::Single && m_mode != ViewMode::Combined)
        return;
    const bool chars = AppSettings::instance().countCharacters;
    TextEditor *e = currentEditor();
    // a script shows its elements where other text shows the word count
    const bool script = e && e->scriptActive();
    m_countButton->setVisible(!script);
    m_scriptHint->setVisible(script);
    m_scriptElement->setVisible(script);
    if (script) {
        const QString el = e->scriptElementAtCursor();
        m_scriptElement->setText((el.isEmpty() ? tr("General Text") : el) + QStringLiteral(" \u25BE"));
        QStringList hints;
        const QString tab = e->scriptTabHint(), enter = e->scriptEnterHint();
        if (!tab.isEmpty())
            hints << tr("Tab: %1").arg(tab);
        if (!enter.isEmpty())
            hints << tr("Enter: %1").arg(enter);
        m_scriptHint->setText(hints.join(QStringLiteral("   \u00B7   ")));
        updatePageLabel();
        return;
    }
    QLocale loc;
    if (e && e->textCursor().hasSelection()) {
        const text::Counts c = text::count(e->textCursor().selection().toPlainText());
        m_countButton->setText(chars ? tr("Selection: %1 characters").arg(loc.toString(c.chars))
                                     : tr("Selection: %1 words").arg(loc.toString(c.words)));
        return;
    }
    int words = 0, cs = 0;
    for (BinderItem *it : m_text->items()) {
        words += it->wordCount;
        cs += it->charCount;
    }
    m_countButton->setText(chars ? tr("Characters: %1").arg(loc.toString(cs)) : tr("Words: %1").arg(loc.toString(words)));
    updatePageLabel();
}

void EditorPane::updateFooter()
{
    updateCounts();
    BinderItem *it = m_mode == ViewMode::Single ? (m_items.isEmpty() ? nullptr : m_items.first()) : m_text->activeItem();
    if (it && it->target > 0) {
        const int cur = it->targetChars ? it->charCount : it->wordCount;
        m_targetBar->setMaximum(it->target);
        m_targetBar->setValue(qMin(cur, it->target));
        m_targetBar->setToolTip(tr("%1 of %2 %3 (click to edit)")
                                    .arg(cur)
                                    .arg(it->target)
                                    .arg(it->targetChars ? tr("characters") : tr("words")));
        m_targetBar->show();
        m_targetButton->hide();
    } else {
        m_targetBar->hide();
        m_targetButton->setVisible(it != nullptr);
    }
    if (m_mode == ViewMode::Media)
        m_mediaInfo->setText(m_media->footerInfo());
}

void EditorPane::editTarget()
{
    BinderItem *it = m_mode == ViewMode::Single ? (m_items.isEmpty() ? nullptr : m_items.first()) : m_text->activeItem();
    if (!it || !m_project)
        return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Document Target"));
    auto *form = new QFormLayout(&dlg);
    auto *spin = new QSpinBox(&dlg);
    spin->setRange(0, 10000000);
    spin->setValue(it->target);
    spin->setSpecialValueText(tr("No target"));
    auto *type = new QComboBox(&dlg);
    type->addItems({tr("words"), tr("characters")});
    type->setCurrentIndex(it->targetChars ? 1 : 0);
    auto *row = new QHBoxLayout;
    row->addWidget(spin);
    row->addWidget(type);
    form->addRow(tr("Target for \"%1\":").arg(it->title), row);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;
    it->target = spin->value();
    it->targetChars = type->currentIndex() == 1;
    m_project->notifyItemChanged(it, false);
    updateFooter();
}

void EditorPane::showFindBar(bool replace)
{
    if (m_mode == ViewMode::Corkboard || m_mode == ViewMode::Outliner) {
        showFilterBar();
        return;
    }
    TextEditor *holder = focusedCopyholderEditor();
    if (m_mode != ViewMode::Single && m_mode != ViewMode::Combined && !holder)
        return;
    if (findBarVisible() && m_findInHolder != (holder != nullptr))
        for (TextEditor *e : findEditors())   // the bar moves to the other text
            e->setSearchPattern(m_findInHolder ? QRegularExpression(QStringLiteral("(?")) : m_searchRe);
    m_findInHolder = holder != nullptr;
    m_findBar->show();
    m_replaceRow->setVisible(replace);
    m_findEdit->setPlaceholderText(m_findInHolder ? tr("Find in the copyholder") : tr("Find"));
    TextEditor *e = holder ? holder : currentEditor();
    if (e && e->textCursor().hasSelection() && !e->textCursor().selectedText().contains(QChar(0x2029)))
        m_findEdit->setText(e->textCursor().selectedText());
    updateFindHighlights();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void EditorPane::useSelectionForFind()
{
    TextEditor *holder = focusedCopyholderEditor();
    TextEditor *e = holder ? holder : currentEditor();
    if (!e || !e->textCursor().hasSelection())
        return;
    m_findEdit->setText(e->textCursor().selectedText());
}

void EditorPane::findNext(bool backward)
{
    // the find bar's text; without the bar, the matches of the project search
    const bool bar = findBarVisible() || !m_searchRe.isValid();
    if (bar && m_findEdit->text().isEmpty())
        return;
    const QRegularExpression re = bar ? findPattern() : m_searchRe;
    const QList<TextEditor *> eds = findEditors();
    if (eds.isEmpty())
        return;
    TextEditor *cur = eds.contains(currentEditor()) ? currentEditor() : eds.first();
    int idx = int(eds.indexOf(cur));
    // search in the current editor without wrapping, then move through the others
    for (int n = 0; n <= eds.size(); ++n) {
        TextEditor *e = eds[(idx + (backward ? -n : n) + eds.size() * 2) % eds.size()];
        if (n > 0) {
            QTextCursor c = e->textCursor();
            c.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
            e->setTextCursor(c);
        }
        if (e->findNext(re, backward, eds.size() == 1)) {
            e->setFocus();
            if (!m_findInHolder)
                m_text->ensureCursorVisible(e);
            return;
        }
    }
}

// The first match of the search in the documents shown is selected and
// scrolled into view (a search result or a quick search entry was chosen).
bool EditorPane::revealFirstMatch()
{
    if (!m_searchRe.isValid() || m_mode == ViewMode::Corkboard || m_mode == ViewMode::Outliner)
        return false;
    for (TextEditor *e : m_text->editors()) {
        QTextCursor c = e->textCursor();
        c.movePosition(QTextCursor::Start);
        e->setTextCursor(c);
        if (e->findNext(m_searchRe, false, false)) {
            m_text->ensureCursorVisible(e);
            return true;
        }
    }
    return false;
}

void EditorPane::setSearchHighlight(const QString &term)
{
    setSearchPattern(search::pattern(term, false, false, false), term);
}

void EditorPane::setSearchPattern(const QRegularExpression &re, const QString &findText)
{
    m_searchRe = re.pattern().isEmpty() ? QRegularExpression(QStringLiteral("(?")) : re;
    if (!findBarVisible() && !findText.isEmpty()) {
        QSignalBlocker b(m_findEdit);
        m_findEdit->setText(findText);
    }
    updateFindHighlights();
}

// ------------------------------------------------------------------ filter

bool EditorPane::filterBarVisible() const { return !m_filterBar->isHidden(); }

void EditorPane::showFilterBar()
{
    if (m_mode != ViewMode::Corkboard && m_mode != ViewMode::Outliner)
        return;
    m_filterOpen = true;
    m_filterBar->setProject(m_project);
    m_filterBar->show();
    m_filterBar->focusField();
    applyFilter();
}

void EditorPane::closeFilter()
{
    m_filterOpen = false;
    m_filterBar->hide();
    {
        QSignalBlocker b(m_filterBar);
        m_filterBar->setFilter(ItemFilter());
    }
    m_cork->setFilter(ItemFilter());
    m_outliner->setFilter(ItemFilter());
    if (m_mode == ViewMode::Corkboard || m_mode == ViewMode::Outliner)
        focusEditor();
}

void EditorPane::applyFilter()
{
    const ItemFilter f = m_filterOpen ? m_filterBar->filter() : ItemFilter();
    int shown = 0, total = 0;
    if (m_mode == ViewMode::Corkboard) {
        m_cork->setFilter(f);
        m_cork->filterCounts(&shown, &total);
    } else if (m_mode == ViewMode::Outliner) {
        m_outliner->setFilter(f);
        m_outliner->filterCounts(&shown, &total);
    }
    m_filterBar->setCount(shown, total);
}

bool EditorPane::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::FocusIn)
        emitActivated();
    if (o == m_targetBar && e->type() == QEvent::MouseButtonRelease) {
        editTarget();
        return true;
    }
    if (o == m_header) {
        if (e->type() == QEvent::DragEnter) {
            auto *de = static_cast<QDragEnterEvent *>(e);
            if (de->mimeData()->hasFormat(QString::fromLatin1(kItemsMime))) {
                de->acceptProposedAction();
                return true;
            }
        } else if (e->type() == QEvent::Drop) {
            auto *de = static_cast<QDropEvent *>(e);
            const QList<BinderItem *> items = BinderModel::itemsFromMime(de->mimeData(), m_project);
            if (!items.isEmpty())
                emit itemsDroppedOnHeader(this, items, de->modifiers() & Qt::AltModifier);
            de->setDropAction(Qt::CopyAction); // shown here; it stays where it is in the binder
            de->accept();
            return true;
        } else if (e->type() == QEvent::ContextMenu) {
            emit headerMenuRequested(this, static_cast<QContextMenuEvent *>(e)->globalPos());
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

QVariantMap EditorPane::saveState() const
{
    QVariantMap m;
    QStringList ids;
    for (BinderItem *it : m_items)
        ids << it->id;
    m[QStringLiteral("items")] = ids;
    m[QStringLiteral("mode")] = int(m_mode);
    m[QStringLiteral("locked")] = m_locked;
    {
        // the last 30 steps of the history, so Back still works after reopening the project
        QVariantList hist;
        const int from = qMax(0, int(m_history.size()) - 30);
        for (int i = from; i < m_history.size(); ++i)
            hist << QVariantMap{{QStringLiteral("ids"), m_history[i].ids}, {QStringLiteral("mode"), m_history[i].mode}};
        m[QStringLiteral("history")] = hist;
        m[QStringLiteral("historyIndex")] = m_historyIndex - from;
    }
    m[QStringLiteral("scroll")] = m_text->scrollValue();
    if (TextEditor *e = currentEditor()) {
        m[QStringLiteral("cursorItem")] = e->item() ? e->item()->id : QString();
        m[QStringLiteral("cursorPos")] = e->textCursor().position();
    }
    if (BinderItem *held = copyholderItem()) {
        m[QStringLiteral("copyholder")] = held->id;
        m[QStringLiteral("copyholderPlacement")] = copyholderPlacement();
        const QList<int> sizes = m_holderSplit->sizes();
        const int total = sizes.value(0) + sizes.value(1);
        if (total > 0)
            m[QStringLiteral("copyholderShare")] = double(sizes.value(m_holderSplit->indexOf(m_copyholder))) / total;
    }
    return m;
}

void EditorPane::restoreState(const QVariantMap &state)
{
    if (!m_project)
        return;
    QList<BinderItem *> items;
    for (const QString &id : state.value(QStringLiteral("items")).toStringList())
        if (BinderItem *it = m_project->item(id))
            items << it;
    if (items.isEmpty())
        return;
    showItems(items, true, state.value(QStringLiteral("mode"), -1).toInt());
    {
        // history: what is left of the stored steps, with what is shown now at its place
        QList<HistoryEntry> hist;
        int index = -1;
        const QVariantList stored = state.value(QStringLiteral("history")).toList();
        const int storedIndex = state.value(QStringLiteral("historyIndex"), -1).toInt();
        QStringList shownIds;
        for (BinderItem *it : m_items)
            shownIds << it->id;
        for (int i = 0; i < stored.size(); ++i) {
            const QVariantMap sm = stored[i].toMap();
            HistoryEntry he;
            for (const QString &id : sm.value(QStringLiteral("ids")).toStringList())
                if (m_project->item(id))
                    he.ids << id;
            he.mode = sm.value(QStringLiteral("mode")).toInt();
            if (he.ids.isEmpty() || (!hist.isEmpty() && hist.last().ids == he.ids && i != storedIndex))
                continue;
            hist << he;
            if (i == storedIndex && he.ids == shownIds)
                index = int(hist.size()) - 1;
        }
        if (index >= 0) {
            m_history = hist;
            m_historyIndex = index;
            refreshHeader();
        }
    }
    setLocked(state.value(QStringLiteral("locked")).toBool());
    if (BinderItem *held = m_project->item(state.value(QStringLiteral("copyholder")).toString())) {
        const QString placement = state.value(QStringLiteral("copyholderPlacement")).toString();
        if (!placement.isEmpty())
            m_project->uiState[QStringLiteral("copyholderPlacement") + m_stateSuffix] = placement;
        openCopyholder(held);
        const double share = qBound(0.1, state.value(QStringLiteral("copyholderShare"), 0.4).toDouble(), 0.9);
        QTimer::singleShot(0, this, [this, share] {
            if (!hasCopyholder())
                return;
            const int total = m_holderSplit->orientation() == Qt::Vertical ? m_holderSplit->height() : m_holderSplit->width();
            const int holder = int(total * share);
            m_holderSplit->setSizes(m_holderSplit->indexOf(m_copyholder) == 0 ? QList<int>{holder, total - holder}
                                                                                 : QList<int>{total - holder, holder});
        });
    }
    const QString cursorItem = state.value(QStringLiteral("cursorItem")).toString();
    const int pos = state.value(QStringLiteral("cursorPos")).toInt();
    const int scroll = state.value(QStringLiteral("scroll")).toInt();
    QTimer::singleShot(50, this, [this, cursorItem, pos, scroll] {
        for (TextEditor *e : m_text->editors()) {
            if (e->item() && e->item()->id == cursorItem) {
                QTextCursor c = e->textCursor();
                c.setPosition(qBound(0, pos, e->document()->characterCount() - 1));
                e->setTextCursor(c);
            }
        }
        m_text->setScrollValue(scroll);
    });
}

} // namespace qb
