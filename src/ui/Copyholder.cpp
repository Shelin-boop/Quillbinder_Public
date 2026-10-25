#include "ui/Copyholder.h"
#include "ui/Zoom.h"
#include <cmath>
#include "ui/DragDrop.h"
#include "core/BinderModel.h"
#include "core/ItemIcons.h"
#include "ui/MediaView.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace qb {

Copyholder::Copyholder(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_header = new QWidget(this);
    m_header->setObjectName(QStringLiteral("copyholderHeader"));
    m_header->setAutoFillBackground(true);
    m_header->setAcceptDrops(true);
    m_header->installEventFilter(this);
    m_header->setToolTip(tr("Copyholder: right-click to place it left, right, above or below the editor"));
    auto *hl = new QHBoxLayout(m_header);
    hl->setContentsMargins(6, 0, 2, 0);
    hl->setSpacing(4);
    m_icon = new QLabel(m_header);
    m_title = new QLabel(m_header);
    m_title->setTextFormat(Qt::PlainText);
    m_title->setMinimumWidth(20);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QFont f = m_title->font();
    f.setPointSizeF(qMax(7.0, f.pointSizeF() - 1));
    m_title->setFont(f);
    auto button = [this](const QString &icon, const QString &tip) {
        auto *b = new QToolButton(m_header);
        b->setIcon(icons::action(icon));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setIconSize(QSize(12, 12));
        b->setFixedSize(18, 18);
        return b;
    };
    m_quickRef = button(QStringLiteral("window-new"), tr("Close the copyholder and open its document as a Quick Reference panel"));
    m_quickRef->setObjectName(QStringLiteral("copyholderQuickRef"));
    m_close = button(QStringLiteral("window-close"), tr("Close the copyholder"));
    m_close->setObjectName(QStringLiteral("copyholderClose"));
    hl->addWidget(m_icon);
    hl->addWidget(m_title, 1);
    hl->addWidget(m_quickRef);
    hl->addWidget(m_close);
    m_header->setFixedHeight(20);
    lay->addWidget(m_header);

    m_stack = new QStackedWidget(this);
    m_text = new TextColumnView(m_stack);
    m_text->setTypewriter(false);
    m_media = new MediaView(m_stack);
    m_media->setOwnControls(true); // a copyholder has no footer bar
    zoom::setHandler(m_media, [this](int steps) { m_media->zoomBy(std::pow(1.15, steps)); }); // pictures zoom by themselves (0.21)
    m_empty = new QLabel(tr("This document has no text"), m_stack);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setEnabled(false);
    m_empty->setAutoFillBackground(true);
    m_empty->setBackgroundRole(QPalette::Base);
    m_stack->addWidget(m_text);
    m_stack->addWidget(m_media);
    m_stack->addWidget(m_empty);
    lay->addWidget(m_stack, 1);

    // the icon can be dragged like the document itself (to the binder, another header bar, a toolbar button ...)
    m_icon->setObjectName(QStringLiteral("copyholderIcon"));
    dnd::makeDragHandle(
        m_icon, [this] { return m_project.data(); },
        [this] { return m_item ? QList<BinderItem *>{m_item} : QList<BinderItem *>(); });
    connect(m_close, &QToolButton::clicked, this, &Copyholder::closeRequested);
    connect(m_quickRef, &QToolButton::clicked, this, [this] {
        if (m_item)
            emit quickReferenceRequested(m_item);
    });
    connect(m_text, &TextColumnView::cursorMoved, this, &Copyholder::cursorMoved);
    connect(m_text, &TextColumnView::activeEditorChanged, this, [this] { emit activated(); });
    connect(m_text, &TextColumnView::linkActivated, this, &Copyholder::linkActivated);
    connect(m_text, &TextColumnView::noteClicked, this, &Copyholder::noteClicked);
    m_text->viewport()->installEventFilter(this);
    m_media->installEventFilter(this);
    connect(&theme::Notifier::instance(), &theme::Notifier::changed, this, &Copyholder::refreshHeader);
    // its header bar is tinted while the keyboard is in the copyholder
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (now && now->window() == window())
            setActiveLook(isAncestorOf(now));
    });
    refreshHeader();
}

void Copyholder::setProject(Project *p)
{
    if (m_project)
        disconnect(m_project, nullptr, this, nullptr);
    clear();
    m_project = p;
    m_text->setProject(p);
    m_media->setProject(p);
    if (p)
        connect(p, &Project::itemChanged, this, [this](BinderItem *it) {
            if (it == m_item)
                refreshHeader();
        });
}

void Copyholder::clear()
{
    m_item = nullptr;
    m_media->release();
    m_text->clear();
    m_stack->setCurrentWidget(m_empty);
    refreshHeader();
}

TextEditor *Copyholder::editor() const
{
    return m_stack->currentWidget() == m_text ? m_text->activeEditor() : nullptr;
}

void Copyholder::showItem(BinderItem *item)
{
    if (!m_project || !item || !m_project->item(item->id)) {
        clear();
        return;
    }
    m_item = item;
    if (isMediaType(item->type)) {
        m_text->clear();
        m_media->showItem(item);
        m_stack->setCurrentWidget(m_media);
    } else if (item->hasText()) {
        m_media->release();
        m_text->setItems({item}, false);
        m_stack->setCurrentWidget(m_text);
    } else {
        m_media->release();
        m_text->clear();
        m_stack->setCurrentWidget(m_empty);
    }
    refreshHeader();
}

void Copyholder::setActiveLook(bool on)
{
    if (m_activeLook == on)
        return;
    m_activeLook = on;
    refreshHeader();
}

void Copyholder::refreshHeader()
{
    const QString title = m_item ? (m_item->title.isEmpty() ? tr("Untitled") : m_item->title) : QString();
    m_title->setText(title);
    m_icon->setPixmap(m_item && m_project ? icons::forItem(m_project, m_item).pixmap(12, 12) : QPixmap());
    QPalette pal = QApplication::palette();
    QColor bg = theme::color(m_activeLook ? theme::Role::HeaderBarActive : theme::Role::HeaderBar);
    QColor fg = theme::color(theme::Role::HeaderBarText);
    if ((bg.lightness() < 128) == (fg.lightness() < 128))
        fg = bg.lightness() < 128 ? QColor(240, 240, 240) : QColor(30, 30, 30);
    pal.setColor(QPalette::Window, bg);
    pal.setColor(QPalette::WindowText, fg);
    pal.setColor(QPalette::ButtonText, fg);
    m_header->setPalette(pal);
}

void Copyholder::headerMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("copyholderMenu"));
    const QList<QPair<QString, QString>> places = {{tr("Copyholder on the &Left"), QStringLiteral("left")},
                                                   {tr("Copyholder on the &Right"), QStringLiteral("right")},
                                                   {tr("Copyholder at the &Top"), QStringLiteral("top")},
                                                   {tr("Copyholder at the &Bottom"), QStringLiteral("bottom")}};
    const QString now = property("placement").toString();
    for (const auto &pl : places) {
        const QString id = pl.second;
        QAction *a = menu.addAction(pl.first, this, [this, id] { emit placementRequested(id); });
        a->setCheckable(true);
        a->setChecked(id == now);
    }
    menu.addSeparator();
    menu.addAction(icons::action(QStringLiteral("window-new")), tr("Open as &Quick Reference"), this, [this] {
        if (m_item)
            emit quickReferenceRequested(m_item);
    });
    menu.addAction(icons::action(QStringLiteral("window-close")), tr("&Close Copyholder"), this, &Copyholder::closeRequested);
    menu.exec(globalPos);
}

bool Copyholder::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::FocusIn)
        emit activated();
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
                emit itemsDropped(items);
            de->setDropAction(Qt::CopyAction); // shown here; it stays where it is in the binder
            de->accept();
            return true;
        } else if (e->type() == QEvent::ContextMenu) {
            headerMenu(static_cast<QContextMenuEvent *>(e)->globalPos());
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

} // namespace qb
