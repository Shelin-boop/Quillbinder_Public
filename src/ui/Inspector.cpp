#include "ui/Inspector.h"
#include "ui/LookMenu.h"
#include "ui/DocMirror.h"
#include "ui/MindMapView.h"
#include "ui/ViewMemory.h"
#include "ui/Zoom.h"
#include "text/DisplayFormat.h"
#include <QSignalBlocker>
#include "ui/BookmarkList.h"
#include "ui/MindMapDoc.h"
#include "ui/MindMapUi.h"
#include "ui/ObjectsUi.h"
#include "ui/DragDrop.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "text/TextProps.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/TextEditor.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateEdit>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QContextMenuEvent>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace qb {

static QToolButton *smallButton(QWidget *parent, const QString &icon, const QString &tip)
{
    auto *b = new QToolButton(parent);
    b->setIcon(icons::action(icon));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setIconSize(QSize(16, 16));
    return b;
}

static QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    l->setContentsMargins(2, 6, 2, 2);
    return l;
}

namespace {
// index card look for the synopsis
class CardEdit : public QPlainTextEdit
{
public:
    using QPlainTextEdit::QPlainTextEdit;
    QColor paper; // the label's tint; invalid = the theme's card color

protected:
    void paintEvent(QPaintEvent *e) override
    {
        {
            QPainter p(viewport());
            p.fillRect(viewport()->rect(), paper.isValid() ? paper : theme::color(theme::Role::SynopsisPaper));
            QColor ruled = theme::color(theme::Role::CardLines);
            ruled.setAlpha(90);
            p.setPen(QPen(ruled, 1));
            const int lh = fontMetrics().lineSpacing();
            for (int y = lh + 4; y < viewport()->height(); y += lh)
                p.drawLine(0, y, viewport()->width(), y);
        }
        QPlainTextEdit::paintEvent(e);
    }
    // the usual menu, and the colors and fonts of the inspector (0.40.2)
    void contextMenuEvent(QContextMenuEvent *e) override
    {
        QMenu *menu = createStandardContextMenu(e->pos());
        menu->setAttribute(Qt::WA_DeleteOnClose);
        lookmenu::add(menu, lookmenu::Part::Inspector, window());
        menu->popup(e->globalPos());
    }
};

} // namespace

Inspector::Inspector(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *tabs = new QWidget(this);
    auto *tl = new QHBoxLayout(tabs);
    tl->setContentsMargins(4, 2, 4, 2);
    tl->addStretch(1);
    // in the order of the Tab numbers; the mind map (0.17) and the objects (0.20) are shown
    // between Bookmarks and Metadata
    const QList<QPair<QString, QString>> defs = {{QStringLiteral("view-pim-notes"), tr("Synopsis and notes")},
                                                 {QStringLiteral("bookmarks"), tr("Bookmarks")},
                                                 {QStringLiteral("tag"), tr("Metadata")},
                                                 {QStringLiteral("camera-photo"), tr("Snapshots")},
                                                 {QStringLiteral("edit-comment"), tr("Comments and footnotes")},
                                                 {QStringLiteral("qb-mindmap"), tr("Mind map of this document or folder")},
                                                 {QStringLiteral("qb-objects"),
                                                  tr("Objects of the project: characters, locations, environments, ...")}};
    auto *group = new QButtonGroup(this);
    for (int i = 0; i < defs.size(); ++i) {
        auto *b = smallButton(tabs, defs[i].first, defs[i].second);
        b->setCheckable(true);
        b->setIconSize(QSize(20, 20));
        group->addButton(b, i);
        m_tabButtons << b;
    }
    m_tabButtons[MindMap]->setObjectName(QStringLiteral("inspectorMindMapButton"));
    m_tabButtons[Objects]->setObjectName(QStringLiteral("inspectorObjectsButton"));
    for (int i : {int(Notes), int(Bookmarks), int(MindMap), int(Objects), int(Metadata), int(Snapshots), int(Comments)})
        tl->addWidget(m_tabButtons[i]);
    tl->addStretch(1);
    lay->addWidget(tabs);
    m_tabsRow = tabs;

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildNotesTab());
    m_stack->addWidget(buildBookmarksTab());
    m_stack->addWidget(buildMetadataTab());
    m_stack->addWidget(buildSnapshotsTab());
    m_stack->addWidget(buildCommentsTab());
    m_mindMap = new MindMapPanel(m_stack);
    m_stack->addWidget(m_mindMap);
    connect(m_mindMap, &MindMapPanel::openWindowRequested, this, &Inspector::openMindMapWindow);
    m_objects = new ObjectsPanel(false, m_stack);
    m_objects->setZoomKeys(QLatin1String(zoom::kInspObjectList), QLatin1String(zoom::kInspObjectSheet));
    m_stack->addWidget(m_objects);
    connect(m_objects, &ObjectsPanel::openWindowRequested, this, &Inspector::openObjectsWindow);
    connect(m_objects, &ObjectsPanel::openDocumentRequested, this, &Inspector::openItemRequested);
    connect(m_objects, &ObjectsPanel::linkActivated, this, &Inspector::objectLinkActivated);
    lay->addWidget(m_stack, 1);
    connect(group, &QButtonGroup::idClicked, this, [this](int id) { showTab(Tab(id)); });
    setAutoFillBackground(true);
    applyTheme();
    connect(&theme::Notifier::instance(), &theme::Notifier::changed, this, &Inspector::applyTheme);

    // label & status
    auto *bottom = new QWidget(this);
    auto *bl = new QGridLayout(bottom);
    bl->setContentsMargins(6, 4, 6, 4);
    m_labelTitle = new QLabel(tr("Label"), bottom);
    m_statusTitle = new QLabel(tr("Status"), bottom);
    m_label = new QComboBox(bottom);
    m_status = new QComboBox(bottom);
    bl->addWidget(m_labelTitle, 0, 0);
    bl->addWidget(m_statusTitle, 0, 1);
    bl->addWidget(m_label, 1, 0);
    bl->addWidget(m_status, 1, 1);
    // the inspector's own zoom (0.21): also Ctrl + mouse wheel anywhere over it;
    // since 0.21.1 the zoom of the part with the keyboard (or zoomed last)
    auto *zoomTitle = new QLabel(tr("Zoom"), bottom);
    zoomTitle->setObjectName(QStringLiteral("inspectorZoomTitle"));
    m_zoomTitle = zoomTitle;
    m_bottomRow = bottom;
    m_zoomBox = new QComboBox(bottom);
    m_zoomBox->setObjectName(QStringLiteral("inspectorZoom"));
    m_zoomBox->setToolTip(tr("Size of the text of this part of the inspector (also Ctrl + mouse wheel over it). "
                             "Parts with a zoom of their own: synopsis and notes, bookmark lists and the bookmarked text, "
                             "snapshot list and text, object list and sheet. Right-click: one zoom for the other tabs or one per tab."));
    m_zoomBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_zoomBox->setContextMenuPolicy(Qt::CustomContextMenu);
    bl->addWidget(zoomTitle, 0, 2);
    bl->addWidget(m_zoomBox, 1, 2);
    bl->setColumnStretch(0, 1);
    bl->setColumnStretch(1, 1);
    lay->addWidget(bottom);
    connect(m_zoomBox, qOverload<int>(&QComboBox::activated), this, [this](int i) {
        const double f = m_zoomBox->itemData(i).toInt() / 100.0;
        const QString key = currentZoomKey();
        if (key == QLatin1String("board")) {
            if (m_mindMap->view())
                m_mindMap->view()->setZoom(f);
            return;
        }
        zoom::setFactor(key, f);
    });
    connect(m_zoomBox, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu;
        QAction *per = menu.addAction(tr("Zoom Each Tab Separately"));
        per->setCheckable(true);
        per->setChecked(zoom::perTab());
        connect(per, &QAction::toggled, this, [](bool on) { zoom::setPerTab(on); });
        menu.addAction(tr("Reset Zoom"), this, [this] {
            const QString key = currentZoomKey();
            if (key == QLatin1String("board")) {
                if (m_mindMap->view())
                    m_mindMap->view()->setZoom(zoom::defaultFactor(QLatin1String(zoom::kMindMap)));
                return;
            }
            zoom::reset(key);
        });
        menu.exec(m_zoomBox->mapToGlobal(pos));
    });
    connect(&zoom::notifier(), &zoom::Notifier::changed, this, [this](const QString &key, double) {
        if (key == QLatin1String(zoom::kInspNotes))
            applyNotesZoom();
        else if (key == QLatin1String(zoom::kInspPreview))
            applyPreviewZoom();
        else if (key == QLatin1String(zoom::kInspSnapText))
            showSnapshot();
        // the part zoomed last is the one the zoom list shows (in its tab)
        static const QHash<QString, int> tabOf = {{QLatin1String(zoom::kInspSynopsis), int(Notes)},
                                                  {QLatin1String(zoom::kInspNotes), int(Notes)},
                                                  {QLatin1String(zoom::kInspBookmarks), int(Bookmarks)},
                                                  {QLatin1String(zoom::kInspPreview), int(Bookmarks)},
                                                  {QLatin1String(zoom::kInspSnapList), int(Snapshots)},
                                                  {QLatin1String(zoom::kInspSnapText), int(Snapshots)},
                                                  {QLatin1String(zoom::kInspObjectList), int(Objects)},
                                                  {QLatin1String(zoom::kInspObjectSheet), int(Objects)}};
        if (tabOf.contains(key))
            m_lastZoomPart[tabOf.value(key)] = key;
        refreshZoomBox();
    });
    // the zoom list follows the keyboard from part to part
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (now && isAncestorOf(now))
            refreshZoomBox();
    });
    connect(&zoom::notifier(), &zoom::Notifier::modeChanged, this, &Inspector::applyZoomMode);
    applyZoomMode();
    connect(m_label, qOverload<int>(&QComboBox::activated), this, [this](int i) {
        if (!m_item || !m_project)
            return;
        const int id = m_label->itemData(i).toInt();
        if (id == -2) {
            emit projectSettingsRequested(0);
            refreshLabelStatus();
            return;
        }
        m_item->label = id;
        m_project->notifyItemChanged(m_item, false);
        applyLabelTint();
    });
    connect(m_status, qOverload<int>(&QComboBox::activated), this, [this](int i) {
        if (!m_item || !m_project)
            return;
        const int id = m_status->itemData(i).toInt();
        if (id == -2) {
            emit projectSettingsRequested(1);
            refreshLabelStatus();
            return;
        }
        m_item->status = id;
        m_project->notifyItemChanged(m_item, false);
    });

    m_commentTimer.setSingleShot(true);
    m_commentTimer.setInterval(400);
    connect(&m_commentTimer, &QTimer::timeout, this, &Inspector::refreshComments);
    showTab(Notes);
}

// ------------------------------------------------------------------ notes tab

QWidget *Inspector::buildNotesTab()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(6, 4, 6, 4);
    auto *split = new QSplitter(Qt::Vertical, w);
    auto *card = new QWidget(split);
    auto *cl = new QVBoxLayout(card);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);
    auto *head = new QHBoxLayout;
    m_cardTitle = new QLineEdit(card);
    m_cardTitle->setPlaceholderText(tr("Title"));
    m_cardTitle->setObjectName(QStringLiteral("cardTitle"));
    QFont tf = m_cardTitle->font();
    tf.setBold(true);
    m_cardTitle->setFont(tf);
    head->addWidget(m_cardTitle, 1);
    m_imageToggle = smallButton(card, QStringLiteral("insert-image"), tr("Use an image instead of the synopsis text"));
    m_imageToggle->setCheckable(true);
    m_imageToggle->setObjectName(QStringLiteral("synopsisImageToggle"));
    head->addWidget(m_imageToggle);
    cl->addLayout(head);
    m_synopsis = new CardEdit(card);
    m_synopsis->setPlaceholderText(tr("Synopsis: a short summary shown on index cards and in the outliner."));
    m_synopsis->setMinimumHeight(90);
    cl->addWidget(m_synopsis, 1);
    m_synopsisImage = new QLabel(card);
    m_synopsisImage->setAlignment(Qt::AlignCenter);
    m_synopsisImage->setMinimumHeight(90);
    m_synopsisImage->setObjectName(QStringLiteral("synopsisImage"));
    m_synopsisImage->setWordWrap(true);
    m_synopsisImage->setFrameShape(QFrame::StyledPanel);
    m_synopsisImage->setAutoFillBackground(true);
    m_synopsisImage->setBackgroundRole(QPalette::AlternateBase);
    m_synopsisImage->setContextMenuPolicy(Qt::CustomContextMenu);
    m_synopsisImage->installEventFilter(this);
    m_synopsisImage->hide();
    cl->addWidget(m_synopsisImage, 1);
    // an image file, a picture from the binder or a copied picture dropped on the card
    new dnd::DropTarget(
        m_synopsisImage,
        [this](const QMimeData *md) {
            if (!m_item || !m_project)
                return false;
            if (md->hasFormat(QString::fromLatin1(kItemsMime))) {
                for (BinderItem *it : BinderModel::itemsFromMime(md, m_project))
                    if (it->type == ItemType::Image)
                        return true;
                return false;
            }
            if (md->hasUrls()) {
                for (const QUrl &u : md->urls())
                    if (u.isLocalFile() && dnd::isImageFile(u.toLocalFile()))
                        return true;
                return false;
            }
            return md->hasImage();
        },
        [this](const QMimeData *md, const QPoint &, Qt::KeyboardModifiers) {
            const QString file = dnd::imageFileFromMime(md, m_project);
            if (!file.isEmpty())
                setSynopsisImage(file);
        });
    connect(m_synopsisImage, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        if (!m_item || !m_project)
            return;
        QMenu menu(this);
        menu.addAction(tr("Choose an Image File…"), this, [this] {
            const QString f = QFileDialog::getOpenFileName(this, tr("Choose an Image"), QDir::homePath(),
                                                           tr("Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp)"));
            if (!f.isEmpty())
                setSynopsisImage(f);
        });
        QAction *rm = menu.addAction(tr("Remove the Image"), this, [this] { setSynopsisImage(QString()); });
        rm->setEnabled(!m_item->synopsisImage.isEmpty());
        menu.exec(m_synopsisImage->mapToGlobal(pos));
    });

    auto *notesBox = new QWidget(split);
    auto *nl = new QVBoxLayout(notesBox);
    nl->setContentsMargins(0, 4, 0, 0);
    nl->setSpacing(2);
    m_notesScope = new QComboBox(notesBox);
    m_notesScope->addItems({tr("Document Notes"), tr("Project Notes")});
    nl->addWidget(m_notesScope);
    m_notes = new TextEditor(notesBox);
    m_notes->setFrameShape(QFrame::StyledPanel);
    m_notes->setLineWrapMode(QTextEdit::WidgetWidth);
    nl->addWidget(m_notes, 1);
    split->addWidget(card);
    split->addWidget(notesBox);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);
    l->addWidget(split);
    m_notesSplit = split;
    split->setObjectName(QStringLiteral("inspectorNotesSplit"));
    // two parts with a zoom each (0.21.1): the card (and the rest of the page) and the notes
    zoom::mark(w, QLatin1String(zoom::kInspSynopsis));
    zoom::attachFonts(w, QLatin1String(zoom::kInspSynopsis));
    zoom::mark(notesBox, QLatin1String(zoom::kInspNotes));
    zoom::attachFonts(notesBox, QLatin1String(zoom::kInspNotes));

    connect(m_cardTitle, &QLineEdit::editingFinished, this, [this] {
        if (m_item && m_project && m_cardTitle->text() != m_item->title)
            m_project->model()->setData(m_project->model()->indexFor(m_item), m_cardTitle->text(), Qt::EditRole);
    });
    connect(m_synopsis, &QPlainTextEdit::textChanged, this, [this] {
        if (m_updating || !m_item || !m_project)
            return;
        m_item->synopsis = m_synopsis->toPlainText();
        m_project->notifyItemChanged(m_item, false);
    });
    connect(m_notesScope, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        refreshNotesTab();
        applyLabelTint(); // the project's notes are no document's: no label color
    });
    connect(m_imageToggle, &QToolButton::toggled, this, [this](bool on) {
        if (m_updating || !m_item || !m_project)
            return;
        Q_UNUSED(on) // without an image yet, the card shows where to drop one
        refreshNotesTab();
    });
    return w;
}

void Inspector::setSynopsisImage(const QString &file)
{
    if (!m_item || !m_project)
        return;
    const QString dir = m_project->contentDir(m_item->id);
    if (!m_item->synopsisImage.isEmpty())
        QFile::remove(dir + QLatin1Char('/') + m_item->synopsisImage);
    if (file.isEmpty()) {
        m_item->synopsisImage.clear();
    } else {
        QDir().mkpath(dir);
        const QString suffix = QFileInfo(file).suffix().toLower();
        const QString name = QStringLiteral("card.") + (suffix.isEmpty() ? QStringLiteral("png") : suffix);
        QFile::remove(dir + QLatin1Char('/') + name);
        if (!QFile::copy(file, dir + QLatin1Char('/') + name))
            return;
        m_item->synopsisImage = name;
    }
    m_project->notifyItemChanged(m_item, false);
    m_updating = true;
    m_imageToggle->setChecked(true);
    m_updating = false;
    refreshNotesTab();
}

void Inspector::refreshNotesTab()
{
    m_updating = true;
    const bool hasItem = m_item && m_project;
    m_cardTitle->setEnabled(hasItem);
    m_synopsis->setEnabled(hasItem);
    m_cardTitle->setText(hasItem ? m_item->title : QString());
    if (!m_synopsis->hasFocus() || !hasItem || m_synopsis->toPlainText() != m_item->synopsis) {
        if (!(m_synopsis->hasFocus() && hasItem && m_synopsis->toPlainText() == m_item->synopsis))
            m_synopsis->setPlainText(hasItem ? m_item->synopsis : QString());
    }
    const bool showImage = hasItem && m_imageToggle->isChecked();
    m_synopsis->setVisible(!showImage);
    m_synopsisImage->setVisible(showImage);
    if (showImage && !m_item->synopsisImage.isEmpty()) {
        QPixmap pm(m_project->contentDir(m_item->id) + QLatin1Char('/') + m_item->synopsisImage);
        m_synopsisImage->setPixmap(pm.scaled(m_synopsisImage->size().expandedTo(QSize(200, 120)), Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation));
        m_synopsisImage->setToolTip(tr("Drop another image here to replace it; right-click for more."));
    } else if (showImage) {
        m_synopsisImage->setPixmap(QPixmap());
        m_synopsisImage->setText(tr("Drop an image here.\n(An image file, or a picture from the binder; double-click to choose a file.)"));
        m_synopsisImage->setToolTip(QString());
    }
    if (m_project) {
        // the notes come back where the writer was in them (0.21.1)
        const QString key = m_notesScope->currentIndex() == 1 ? QStringLiteral("notes:project")
                            : hasItem                         ? QStringLiteral("notes:") + m_item->id
                                                              : QString();
        const bool other = key != m_notesKey;
        if (other) {
            rememberNotesPlace();
            m_notesKey = key;
        }
        if (m_notesScope->currentIndex() == 1) {
            m_notes->setEnabled(true);
            m_notes->setProjectDocument(m_project, nullptr, m_project->docs()->projectNotes(), true);
            if (other)
                restoreNotesPlace();
        } else if (hasItem) {
            m_notes->setEnabled(true);
            m_notes->setProjectDocument(m_project, m_item, m_project->docs()->notes(m_item->id), true);
            if (other)
                restoreNotesPlace();
        } else {
            m_notes->detachDocument();
            m_notes->setEnabled(false);
        }
    } else {
        m_notes->detachDocument();
        m_notes->setEnabled(false);
    }
    m_updating = false;
}

void Inspector::rememberNotesPlace()
{
    if (!m_project || m_notesKey.isEmpty() || !m_notes->document())
        return;
    viewmem::TextPlace t;
    t.cursor = m_notes->textCursor().position();
    t.top = m_notes->verticalScrollBar()->value(); // (the notes: pixels of the scroll bar)
    viewmem::setTextPlace(m_project, m_notesKey, t);
}

void Inspector::restoreNotesPlace()
{
    if (!m_project || m_notesKey.isEmpty())
        return;
    const viewmem::TextPlace t = viewmem::textPlace(m_project, m_notesKey);
    if (!t.valid())
        return;
    QTextCursor c = m_notes->textCursor();
    c.setPosition(qBound(0, t.cursor, m_notes->document()->characterCount() - 1));
    m_notes->setTextCursor(c);
    const QString key = m_notesKey;
    const int scroll = t.top;
    QTimer::singleShot(0, this, [this, key, scroll] {
        if (key == m_notesKey)
            m_notes->verticalScrollBar()->setValue(scroll);
    });
}

// ------------------------------------------------------------------ bookmarks

QList<Bookmark> &Inspector::bookmarksOf(bool project)
{
    static QList<Bookmark> empty;
    if (project && m_project)
        return m_project->projectBookmarks;
    if (!project && m_item)
        return m_item->bookmarks;
    empty.clear();
    return empty;
}

// The tab shows the bookmarks of the document and those of the project, one
// list above the other (until 0.14.0 a list chose between them, and a project
// bookmark added from the binder did not show up there).
QWidget *Inspector::buildBookmarksTab()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(6, 4, 6, 4);
    auto *split = new QSplitter(Qt::Vertical, w);
    split->setChildrenCollapsible(false);
    auto section = [this, split](bool project) {
        auto *box = new QWidget(split);
        auto *bl = new QVBoxLayout(box);
        bl->setContentsMargins(0, 0, 0, 0);
        bl->setSpacing(2);
        auto *head = new QHBoxLayout;
        auto *title = sectionLabel(project ? tr("Project Bookmarks") : tr("Document Bookmarks"), box);
        title->setObjectName(project ? QStringLiteral("projectBookmarksTitle") : QStringLiteral("documentBookmarksTitle"));
        head->addWidget(title, 1);
        auto *add = smallButton(box, QStringLiteral("list-add"), project ? tr("Add a project bookmark") : tr("Add a bookmark to this document"));
        add->setObjectName(project ? QStringLiteral("addProjectBookmark") : QStringLiteral("addDocumentBookmark"));
        add->setPopupMode(QToolButton::InstantPopup);
        auto *remove = smallButton(box, QStringLiteral("list-remove"), tr("Remove bookmark"));
        head->addWidget(add);
        head->addWidget(remove);
        bl->addLayout(head);
        auto *list = new BookmarkListWidget(box);
        list->setObjectName(project ? QStringLiteral("inspectorProjectBookmarks") : QStringLiteral("inspectorBookmarks"));
        bl->addWidget(list, 1);
        (project ? m_projectBookmarks : m_bookmarks) = list;

        auto *menu = new QMenu(add);
        menu->addAction(tr("Internal Document…"), this, [this, project] {
            if (!m_project)
                return;
            QStringList titles;
            QList<BinderItem *> items;
            for (BinderItem *it : m_project->allItems())
                if (!it->isRootFolder() && !m_project->isInTrash(it)) {
                    titles << it->title;
                    items << it;
                }
            bool ok = false;
            const QString t = QInputDialog::getItem(this, tr("Add Bookmark"), tr("Document:"), titles, 0, false, &ok);
            if (!ok)
                return;
            BinderItem *it = items.value(titles.indexOf(t));
            if (!it)
                return;
            bookmarksOf(project).append({it->title, QStringLiteral("qb://") + it->id});
            m_project->notifyBookmarksChanged();
        });
        menu->addAction(tr("File on Disk…"), this, [this, project] {
            const QString f = QFileDialog::getOpenFileName(this, tr("Bookmark a File"), QDir::homePath());
            if (f.isEmpty() || !m_project)
                return;
            bookmarksOf(project).append({QFileInfo(f).fileName(), QUrl::fromLocalFile(f).toString()});
            m_project->notifyBookmarksChanged();
        });
        menu->addAction(tr("Web Address…"), this, [this, project] {
            bool ok = false;
            const QString u = QInputDialog::getText(this, tr("Bookmark a Web Page"), tr("Address:"), QLineEdit::Normal,
                                                    QStringLiteral("https://"), &ok);
            if (!ok || u.trimmed().isEmpty() || !m_project)
                return;
            bookmarksOf(project).append({u.trimmed(), u.trimmed()});
            m_project->notifyBookmarksChanged();
        });
        add->setMenu(menu);
        connect(remove, &QToolButton::clicked, this, [list] { list->removeBookmark(list->currentRow()); });
        connect(list, &QListWidget::currentRowChanged, this, [this, project](int r) {
            if (r >= 0) // one selection for both lists: the preview shows its bookmark
                (project ? m_bookmarks : m_projectBookmarks)->setCurrentRow(-1);
            const QList<Bookmark> &bms = bookmarksOf(project);
            if (r < 0 || r >= bms.size() || !m_project) {
                if (r >= 0 || (m_bookmarks->currentRow() < 0 && m_projectBookmarks->currentRow() < 0))
                    stopPreview();
                return;
            }
            const Bookmark b = bms[r];
            BinderItem *it = b.isInternal() ? m_project->item(b.internalId()) : nullptr;
            if (it && it->hasText()) {
                showBookmarkPreview(it);
            } else {
                stopPreview();
            }
        });
        connect(list, &QListWidget::itemDoubleClicked, this, [this, list, project](QListWidgetItem *li) {
            const int r = list->row(li);
            const QList<Bookmark> &bms = bookmarksOf(project);
            if (r < 0 || r >= bms.size() || !m_project)
                return;
            const Bookmark b = bms[r];
            if (b.isInternal()) {
                if (BinderItem *it = m_project->item(b.internalId()))
                    emit openItemRequested(it);
            } else {
                QDesktopServices::openUrl(QUrl(b.target));
            }
        });
        split->addWidget(box);
    };
    section(false);
    section(true);
    m_bookmarkPreview = new TextEditor(split);
    m_bookmarkPreview->setObjectName(QStringLiteral("bookmarkPreview"));
    m_bookmarkPreview->setReadOnly(false);
    m_bookmarkPreview->setLineWrapMode(QTextEdit::WidgetWidth);
    m_bookmarkPreview->hide();
    split->addWidget(m_bookmarkPreview);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 2);
    l->addWidget(split, 1);
    m_bookmarkSplit = split;
    split->setObjectName(QStringLiteral("inspectorBookmarkSplit"));
    // two parts with a zoom each (0.21.1): the lists, and the text shown below them
    // (a copy of the document at a zoom of its own, see showBookmarkPreview)
    zoom::mark(w, QLatin1String(zoom::kInspBookmarks));
    zoom::attachFonts(w, QLatin1String(zoom::kInspBookmarks));
    zoom::mark(m_bookmarkPreview, QLatin1String(zoom::kInspPreview));
    auto *hint = new QLabel(tr("Drag documents, files or web links here; drag a bookmark to move it. Double-click to open."), w);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    l->addWidget(hint);
    return w;
}

void Inspector::showBookmarkPreview(BinderItem *it)
{
    // All views of a document share one layout, which wraps at the narrowest of
    // them and has one zoom: the preview shows a copy of the document, at the zoom
    // of its own part, kept in step with the document both ways (0.21.1; before, a
    // document an editor showed was a read-only copy, and a font chosen there was lost).
    QTextDocument *doc = m_project ? m_project->docs()->text(it->id) : nullptr;
    if (!doc) {
        stopPreview();
        return;
    }
    if (m_previewMirror && m_previewMirror->source() == doc && m_previewCopy && m_previewItemId == it->id) {
        m_bookmarkPreview->show();
        return;
    }
    stopPreview();
    QTextDocument *copy = doc->clone(this);
    m_previewCopy = copy;
    m_previewItemId = it->id;
    m_bookmarkPreview->setProjectDocument(m_project, it, copy);
    m_previewMirror = new DocMirror(doc, copy, this);
    connect(m_previewMirror, &DocMirror::copyReplaced, this, [this](QTextDocument *fresh) {
        // the copy was made anew (it had drifted from the document): show the new one
        QPointer<QTextDocument> old = m_previewCopy;
        m_previewCopy = fresh;
        const int pos = m_bookmarkPreview->textCursor().position();
        BinderItem *item = m_project ? m_project->item(m_previewItemId) : nullptr;
        m_bookmarkPreview->setProjectDocument(m_project, item, fresh);
        applyPreviewZoom();
        QTextCursor c(fresh);
        c.setPosition(qBound(0, pos, fresh->characterCount() - 1));
        m_bookmarkPreview->setTextCursor(c);
        if (old && old != fresh)
            old->deleteLater();
    });
    applyPreviewZoom();
    m_bookmarkPreview->setToolTip(tr("The bookmarked document: what you write or format here is written into the document "
                                     "(and shows in every editor at once). Double-click the bookmark to open it."));
    m_bookmarkPreview->show();
}

void Inspector::stopPreview()
{
    delete m_previewMirror;
    m_previewMirror = nullptr;
    m_previewItemId.clear();
    if (m_bookmarkPreview) {
        m_bookmarkPreview->hide();
        m_bookmarkPreview->detachDocument();
    }
    if (m_previewCopy)
        m_previewCopy->deleteLater();
    m_previewCopy = nullptr;
}

void Inspector::applyPreviewZoom()
{
    // the copy's own zoom: its default font, and the sizes set in the text (the highlighter)
    if (!m_previewCopy || !m_project)
        return;
    const double z = zoom::factor(QLatin1String(zoom::kInspPreview));
    m_previewCopy->setDefaultFont(display::baseFont(m_project->formatting, z));
    m_previewCopy->setProperty("qbZoom", z);
    m_bookmarkPreview->setFont(m_previewCopy->defaultFont());
}

void Inspector::refreshBookmarks()
{
    m_bookmarks->setSource(m_project, [this]() -> QList<Bookmark> * { return m_project ? &bookmarksOf(false) : nullptr; });
    m_projectBookmarks->setSource(m_project, [this]() -> QList<Bookmark> * { return m_project ? &bookmarksOf(true) : nullptr; });
    m_bookmarks->setEnabled(m_item != nullptr);
    if (m_bookmarks->currentRow() < 0 && m_projectBookmarks->currentRow() < 0)
        stopPreview();
}

// ------------------------------------------------------------------ metadata

QWidget *Inspector::buildMetadataTab()
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(6, 4, 6, 4);

    l->addWidget(sectionLabel(tr("General"), w));
    auto *gen = new QFormLayout;
    m_created = new QLabel(w);
    m_modified = new QLabel(w);
    m_include = new QCheckBox(tr("Include in Compile"), w);
    m_sectionType = new QComboBox(w);
    gen->addRow(tr("Created:"), m_created);
    gen->addRow(tr("Modified:"), m_modified);
    gen->addRow(QString(), m_include);
    gen->addRow(tr("Section type:"), m_sectionType);
    l->addLayout(gen);

    auto *customHead = new QHBoxLayout;
    customHead->addWidget(sectionLabel(tr("Custom Metadata"), w), 1);
    auto *editFields = smallButton(w, QStringLiteral("configure"), tr("Set up custom metadata fields"));
    customHead->addWidget(editFields);
    l->addLayout(customHead);
    m_customBox = new QWidget(w);
    m_customForm = new QFormLayout(m_customBox);
    m_customForm->setContentsMargins(0, 0, 0, 0);
    l->addWidget(m_customBox);

    auto *kwHead = new QHBoxLayout;
    kwHead->addWidget(sectionLabel(tr("Keywords"), w), 1);
    auto *addKw = smallButton(w, QStringLiteral("list-add"), tr("Add keyword"));
    addKw->setPopupMode(QToolButton::InstantPopup);
    auto *remKw = smallButton(w, QStringLiteral("list-remove"), tr("Remove keyword"));
    auto *allKw = smallButton(w, QStringLiteral("tag"), tr("Project keywords…"));
    kwHead->addWidget(addKw);
    kwHead->addWidget(remKw);
    kwHead->addWidget(allKw);
    l->addLayout(kwHead);
    m_keywords = new QListWidget(w);
    m_keywords->setObjectName(QStringLiteral("inspectorKeywords"));
    m_keywords->setMaximumHeight(160);
    l->addWidget(m_keywords);
    // keywords dragged from the Project Keywords window (with Alt: together with their groups)
    new dnd::DropTarget(
        m_keywords->viewport(),
        [this](const QMimeData *md) { return m_item && m_project && md->hasFormat(QString::fromLatin1(kKeywordsMime)); },
        [this](const QMimeData *md, const QPoint &, Qt::KeyboardModifiers mods) {
            if (!m_item || !m_project)
                return;
            dnd::assignKeywords(m_project, {m_item}, dnd::keywordsFromMime(md, m_project), mods & Qt::AltModifier);
            refreshKeywords();
        });
    l->addStretch(1);
    scroll->setWidget(w);

    connect(m_include, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating || !m_item || !m_project)
            return;
        m_item->includeInCompile = on;
        m_project->notifyItemChanged(m_item, false);
    });
    connect(m_sectionType, qOverload<int>(&QComboBox::activated), this, [this](int i) {
        if (!m_item || !m_project)
            return;
        const QString id = m_sectionType->itemData(i).toString();
        if (id == QLatin1String("__edit")) {
            emit projectSettingsRequested(2);
            refreshMetadata();
            return;
        }
        m_item->sectionType = id;
        m_project->notifyItemChanged(m_item, false);
        refreshMetadata();
    });
    connect(editFields, &QToolButton::clicked, this, [this] { emit projectSettingsRequested(4); });
    connect(allKw, &QToolButton::clicked, this, &Inspector::keywordsDialogRequested);
    auto *kwMenu = new QMenu(addKw);
    addKw->setMenu(kwMenu);
    connect(kwMenu, &QMenu::aboutToShow, this, [this, kwMenu] {
        kwMenu->clear();
        if (!m_project || !m_item)
            return;
        for (const Keyword &k : m_project->keywords) {
            if (m_item->keywords.contains(k.id))
                continue;
            QPixmap pm(12, 12);
            pm.fill(k.color.isValid() ? k.color : Qt::gray);
            const QString id = k.id;
            kwMenu->addAction(QIcon(pm), k.name, this, [this, id] {
                m_item->keywords << id;
                m_project->notifyItemChanged(m_item, false);
                refreshKeywords();
            });
        }
        kwMenu->addSeparator();
        kwMenu->addAction(tr("New Keyword…"), this, [this] {
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("New Keyword"), tr("Keyword:"), QLineEdit::Normal, QString(), &ok);
            if (!ok || name.trimmed().isEmpty())
                return;
            QString id;
            for (const Keyword &k : m_project->keywords)
                if (k.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
                    id = k.id;
            if (id.isEmpty()) {
                Keyword k;
                k.id = newId();
                k.name = name.trimmed();
                k.color = QColor::fromHsv(int(m_project->keywords.size() * 47) % 360, 120, 220);
                m_project->keywords << k;
                id = k.id;
                m_project->notifySettingsChanged();
            }
            if (!m_item->keywords.contains(id))
                m_item->keywords << id;
            m_project->notifyItemChanged(m_item, false);
            refreshKeywords();
        });
    });
    connect(remKw, &QToolButton::clicked, this, [this] {
        QListWidgetItem *li = m_keywords->currentItem();
        if (!li || !m_item || !m_project)
            return;
        m_item->keywords.removeAll(li->data(Qt::UserRole).toString());
        m_project->notifyItemChanged(m_item, false);
        refreshKeywords();
    });
    return scroll;
}

void Inspector::refreshMetadata()
{
    m_updating = true;
    const bool has = m_item && m_project;
    QLocale loc;
    m_created->setText(has ? m_item->created.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString());
    m_modified->setText(has ? m_item->modified.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString());
    m_include->setEnabled(has && m_project->isInDraft(m_item) && m_item != m_project->draft());
    m_include->setChecked(has && m_item->includeInCompile);
    m_sectionType->clear();
    if (has) {
        const SectionType *def = nullptr;
        {
            const QString saved = m_item->sectionType;
            m_item->sectionType.clear();
            def = m_project->sectionTypeById(m_project->effectiveSectionType(m_item));
            m_item->sectionType = saved;
        }
        m_sectionType->addItem(tr("Structure-Based (%1)").arg(def ? def->name : tr("none")), QString());
        for (const SectionType &st : m_project->sectionTypes)
            m_sectionType->addItem(st.name, st.id);
        m_sectionType->insertSeparator(m_sectionType->count());
        m_sectionType->addItem(tr("Edit Section Types…"), QStringLiteral("__edit"));
        const int idx = m_sectionType->findData(m_item->sectionType);
        m_sectionType->setCurrentIndex(qMax(0, idx));
        QFont f = m_sectionType->font();
        f.setItalic(m_item->sectionType.isEmpty());
        m_sectionType->setFont(f);
    }
    m_sectionType->setEnabled(has);
    m_updating = false;
    refreshCustomFields();
    refreshKeywords();
}

void Inspector::refreshCustomFields()
{
    while (m_customForm->rowCount() > 0)
        m_customForm->removeRow(0);
    if (!m_project)
        return;
    if (m_project->customFields.isEmpty()) {
        auto *l = new QLabel(tr("No custom fields. Use the gear button to add fields such as POV, location or date."), m_customBox);
        l->setWordWrap(true);
        l->setEnabled(false);
        m_customForm->addRow(l);
        return;
    }
    for (const CustomField &f : m_project->customFields) {
        const QString fid = f.id;
        const QString value = m_item ? m_item->custom.value(fid, f.defaultValue) : QString();
        QWidget *w = nullptr;
        switch (f.type) {
        case CustomFieldType::Checkbox: {
            auto *cb = new QCheckBox(m_customBox);
            cb->setChecked(value == QLatin1String("1") || value == QLatin1String("true"));
            connect(cb, &QCheckBox::toggled, this, [this, fid](bool on) {
                if (!m_item)
                    return;
                m_item->custom[fid] = on ? QStringLiteral("1") : QStringLiteral("0");
                m_project->notifyItemChanged(m_item, false);
            });
            w = cb;
            break;
        }
        case CustomFieldType::List: {
            auto *cb = new QComboBox(m_customBox);
            cb->addItem(QString());
            cb->addItems(f.options);
            cb->setCurrentText(value);
            connect(cb, &QComboBox::textActivated, this, [this, fid](const QString &t) {
                if (!m_item)
                    return;
                m_item->custom[fid] = t;
                m_project->notifyItemChanged(m_item, false);
            });
            w = cb;
            break;
        }
        case CustomFieldType::Date: {
            auto *de = new QDateEdit(m_customBox);
            de->setCalendarPopup(true);
            de->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
            de->setSpecialValueText(tr("none"));
            de->setMinimumDate(QDate(1, 1, 1));
            const QDate d = QDate::fromString(value, Qt::ISODate);
            de->setDate(d.isValid() ? d : de->minimumDate());
            connect(de, &QDateEdit::dateChanged, this, [this, fid, de](const QDate &d) {
                if (!m_item)
                    return;
                m_item->custom[fid] = d == de->minimumDate() ? QString() : d.toString(Qt::ISODate);
                m_project->notifyItemChanged(m_item, false);
            });
            w = de;
            break;
        }
        case CustomFieldType::Text: {
            auto *le = new QLineEdit(m_customBox);
            le->setText(value);
            connect(le, &QLineEdit::editingFinished, this, [this, fid, le] {
                if (!m_item || m_item->custom.value(fid) == le->text())
                    return;
                m_item->custom[fid] = le->text();
                m_project->notifyItemChanged(m_item, false);
            });
            w = le;
            break;
        }
        }
        w->setEnabled(m_item != nullptr);
        m_customForm->addRow(f.title + QLatin1Char(':'), w);
    }
}

void Inspector::refreshKeywords()
{
    m_keywords->clear();
    if (!m_item || !m_project)
        return;
    for (const QString &k : m_item->keywords) {
        const Keyword *kw = m_project->keywordById(k);
        if (!kw)
            continue;
        QPixmap pm(12, 12);
        pm.fill(kw->color.isValid() ? kw->color : Qt::gray);
        auto *li = new QListWidgetItem(QIcon(pm), kw->name);
        li->setData(Qt::UserRole, kw->id);
        m_keywords->addItem(li);
    }
}

// ------------------------------------------------------------------ snapshots

QWidget *Inspector::buildSnapshotsTab()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(6, 4, 6, 4);
    auto *head = new QHBoxLayout;
    auto *take = smallButton(w, QStringLiteral("camera-photo"), tr("Take snapshot (Ctrl+5)"));
    auto *del = smallButton(w, QStringLiteral("list-remove"), tr("Delete snapshot"));
    auto *roll = new QToolButton(w);
    roll->setText(tr("Roll Back"));
    roll->setToolTip(tr("Replace the current text with the selected snapshot"));
    m_compare = new QToolButton(w);
    m_compare->setText(tr("Compare"));
    m_compare->setCheckable(true);
    m_compare->setToolTip(tr("Show what changed since the snapshot (or between two selected snapshots)"));
    m_granularity = new QComboBox(w);
    m_granularity->addItems({tr("Words"), tr("Sentences"), tr("Paragraphs"), tr("Characters")});
    m_granularity->setToolTip(tr("Comparison detail"));
    head->addWidget(take);
    head->addWidget(del);
    head->addStretch(1);
    head->addWidget(m_compare);
    head->addWidget(m_granularity);
    head->addWidget(roll);
    l->addLayout(head);
    auto *split = new QSplitter(Qt::Vertical, w);
    m_snapList = new QListWidget(split);
    m_snapList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_snapList->setEditTriggers(QAbstractItemView::DoubleClicked);
    m_snapView = new QTextBrowser(split);
    m_snapView->setObjectName(QStringLiteral("snapshotView"));
    split->addWidget(m_snapList);
    split->addWidget(m_snapView);
    split->setStretchFactor(1, 3);
    l->addWidget(split, 1);
    m_snapSplit = split;
    split->setObjectName(QStringLiteral("inspectorSnapshotSplit"));
    // two parts with a zoom each (0.21.1): the list (and buttons), the text of the
    // snapshot (shown in the project's font at its own zoom, see showSnapshot)
    zoom::mark(w, QLatin1String(zoom::kInspSnapList));
    zoom::attachFonts(w, QLatin1String(zoom::kInspSnapList));
    zoom::mark(m_snapView, QLatin1String(zoom::kInspSnapText));

    connect(take, &QToolButton::clicked, this, [this] { takeSnapshot(true); });
    connect(del, &QToolButton::clicked, this, [this] {
        const int r = m_snapList->currentRow();
        if (r < 0 || r >= m_snaps.size() || !m_project)
            return;
        if (QMessageBox::question(this, tr("Delete Snapshot"), tr("Delete the snapshot \"%1\"?").arg(m_snaps[r].title))
            != QMessageBox::Yes)
            return;
        snapshots::remove(m_project, m_snaps[r]);
        refreshSnapshots();
    });
    connect(roll, &QToolButton::clicked, this, [this] {
        const int r = m_snapList->currentRow();
        if (r < 0 || r >= m_snaps.size() || !m_project || !m_item)
            return;
        const auto answer = QMessageBox::question(
            this, tr("Roll Back"),
            tr("Replace the current text with the snapshot \"%1\"?\n\nDo you want to take a snapshot of the current "
               "text first?").arg(m_snaps[r].title),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
        if (answer == QMessageBox::Cancel)
            return;
        const Snapshot s = m_snaps[r];
        if (answer == QMessageBox::Yes)
            snapshots::take(m_project, m_item->id, tr("Before rolling back"));
        snapshots::rollBack(m_project, s);
        refreshSnapshots();
    });
    connect(m_snapList, &QListWidget::itemSelectionChanged, this, &Inspector::showSnapshot);
    connect(m_compare, &QToolButton::toggled, this, &Inspector::showSnapshot);
    connect(m_granularity, qOverload<int>(&QComboBox::currentIndexChanged), this, &Inspector::showSnapshot);
    connect(m_snapList, &QListWidget::itemChanged, this, [this](QListWidgetItem *li) {
        const int r = m_snapList->row(li);
        if (m_updating || r < 0 || r >= m_snaps.size())
            return;
        const QString t = li->text().section(QStringLiteral("  —  "), 0, 0);
        snapshots::setTitle(m_project, m_snaps[r], t);
        m_snaps[r].title = t;
    });
    return w;
}

void Inspector::refreshSnapshots()
{
    m_updating = true;
    m_snapList->clear();
    m_snapView->clear();
    m_snaps.clear();
    if (m_project && m_item && m_item->hasText()) {
        m_snaps = snapshots::list(m_project, m_item->id);
        for (const Snapshot &s : m_snaps) {
            auto *li = new QListWidgetItem(s.title + QStringLiteral("  —  ") + s.date.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
            li->setFlags(li->flags() | Qt::ItemIsEditable);
            m_snapList->addItem(li);
        }
    }
    m_updating = false;
    updateTabDots();
}

static QString diffHtml(const QList<DiffOp> &ops)
{
    QString html = QStringLiteral("<div style=\"white-space: pre-wrap;\">");
    for (const DiffOp &op : ops) {
        QString t = op.text.toHtmlEscaped();
        t.replace(QLatin1Char('\n'), QLatin1String("<br>"));
        switch (op.type) {
        case DiffOp::Equal: html += t; break;
        case DiffOp::Insert: html += QStringLiteral("<span style=\"color:#1a5fd0; text-decoration:underline;\">%1</span>").arg(t); break;
        case DiffOp::Delete: html += QStringLiteral("<span style=\"color:#c02020; text-decoration:line-through;\">%1</span>").arg(t); break;
        }
    }
    return html + QStringLiteral("</div>");
}

void Inspector::showSnapshot()
{
    if (!m_project || !m_item) {
        m_snapView->clear();
        return;
    }
    const QList<QListWidgetItem *> sel = m_snapList->selectedItems();
    if (sel.isEmpty()) {
        m_snapView->clear();
        return;
    }
    QList<int> rows;
    for (QListWidgetItem *li : sel)
        rows << m_snapList->row(li);
    std::sort(rows.begin(), rows.end());
    QTextDocument snapDoc;
    snapshots::load(m_project, m_snaps[rows.last()], &snapDoc); // oldest selected
    // The text is shown in the project's font at the zoom of its part (0.21.1; before,
    // as HTML with fixed sizes, which did not zoom). Sizes set in the text are scaled too.
    const double z = zoom::factor(QLatin1String(zoom::kInspSnapText));
    auto showDoc = [this, z](QTextDocument *d) {
        d->setDefaultFont(display::baseFont(m_project->formatting, z));
        // (Qt deletes the browser's own first document when another is set: a guarded pointer)
        QPointer<QTextDocument> old = m_snapView->document();
        m_snapView->setDocument(d);
        m_snapView->setFont(d->defaultFont());
        if (old && old != d && old->parent() == m_snapView)
            old->deleteLater();
    };
    if (!m_compare->isChecked()) {
        QTextDocument *d = snapDoc.clone(m_snapView);
        if (!qFuzzyCompare(z, 1.0)) {
            QTextCursor c(d);
            for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
                for (auto it = b.begin(); !it.atEnd(); ++it) {
                    const QTextFragment f = it.fragment();
                    if (!f.charFormat().hasProperty(QTextFormat::FontPointSize) || f.charFormat().fontPointSize() <= 0)
                        continue;
                    QTextCharFormat cf;
                    cf.setFontPointSize(f.charFormat().fontPointSize() * z);
                    c.setPosition(f.position());
                    c.setPosition(f.position() + f.length(), QTextCursor::KeepAnchor);
                    c.mergeCharFormat(cf);
                }
        }
        showDoc(d);
        return;
    }
    QString newer;
    if (rows.size() >= 2) {
        QTextDocument d2;
        snapshots::load(m_project, m_snaps[rows.first()], &d2);
        newer = text::plainText(&d2, true);
    } else {
        newer = text::plainText(m_project->docs()->text(m_item->id), true);
    }
    const DiffGranularity g = m_granularity->currentIndex() == 1   ? DiffGranularity::Sentence
                              : m_granularity->currentIndex() == 2 ? DiffGranularity::Paragraph
                              : m_granularity->currentIndex() == 3 ? DiffGranularity::Character
                                                                   : DiffGranularity::Word;
    auto *d = new QTextDocument(m_snapView);
    d->setDefaultFont(display::baseFont(m_project->formatting, z));
    d->setHtml(diffHtml(diffTexts(text::plainText(&snapDoc, true), newer, g)));
    showDoc(d);
}

void Inspector::takeSnapshot(bool askTitle)
{
    if (!m_project || !m_item || !m_item->hasText())
        return;
    QString title = tr("Snapshot");
    if (askTitle) {
        bool ok = false;
        title = QInputDialog::getText(this, tr("Take Snapshot"), tr("Title for this snapshot:"), QLineEdit::Normal,
                                      tr("Untitled Snapshot"), &ok);
        if (!ok)
            return;
    }
    QString err;
    if (!snapshots::take(m_project, m_item->id, title, &err))
        QMessageBox::warning(this, tr("Snapshot"), tr("The snapshot could not be saved.\n%1").arg(err));
    refreshSnapshots();
}

// ------------------------------------------------------------------ comments

namespace {
class NoteCard : public QFrame
{
public:
    QPlainTextEdit *edit = nullptr;
    QLabel *head = nullptr;
    QString noteId;
    NoteKind kind = NoteKind::None;
};
} // namespace

QWidget *Inspector::buildCommentsTab()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(6, 4, 6, 4);
    auto *head = new QHBoxLayout;
    auto *addC = new QToolButton(w);
    addC->setText(tr("+ Comment"));
    addC->setToolTip(tr("Attach a comment to the selected text (Shift+F2)"));
    auto *addF = new QToolButton(w);
    addF->setText(tr("+ Footnote"));
    addF->setToolTip(tr("Attach a footnote to the selected text (Ctrl+Shift+F2)"));
    head->addWidget(addC);
    head->addWidget(addF);
    head->addStretch(1);
    l->addLayout(head);
    m_commentScroll = new QScrollArea(w);
    m_commentScroll->setWidgetResizable(true);
    m_commentScroll->setFrameShape(QFrame::NoFrame);
    m_commentList = new QWidget;
    m_commentLayout = new QVBoxLayout(m_commentList);
    m_commentLayout->setContentsMargins(0, 0, 0, 0);
    m_commentLayout->addStretch(1);
    m_commentScroll->setWidget(m_commentList);
    l->addWidget(m_commentScroll, 1);
    connect(addC, &QToolButton::clicked, this, [this] { addLinkedNote(false); });
    connect(addF, &QToolButton::clicked, this, [this] { addLinkedNote(true); });
    return w;
}

void Inspector::addLinkedNote(bool footnote)
{
    if (!m_editor)
        return;
    const QString id = m_editor->addLinkedNote(footnote ? NoteKind::LinkedFootnote : NoteKind::Comment);
    if (id.isEmpty()) {
        QMessageBox::information(this, tr("Comments"), tr("Select some text first; the note is attached to it."));
        return;
    }
    showTab(Comments);
    m_focusNote = id;
    refreshComments();
}

void Inspector::focusNote(const QString &noteId)
{
    m_focusNote = noteId;
    refreshComments();
    if (QWidget *card = m_noteCards.value(noteId)) {
        m_commentScroll->ensureWidgetVisible(card);
        if (auto *nc = static_cast<NoteCard *>(card); nc->edit && !nc->edit->isReadOnly())
            nc->edit->setFocus();
    }
}

namespace {
struct NoteInfo {
    QString id;
    NoteKind kind;
    QString text;
    QColor color;
};

void forEachNoteFragment(QTextDocument *doc, const QString &noteId, const std::function<void(const QTextFragment &)> &fn)
{
    QList<QTextFragment> frags;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it)
            if (it.fragment().charFormat().stringProperty(props::NoteId) == noteId)
                frags << it.fragment();
    for (const QTextFragment &f : frags)
        fn(f);
}

QList<NoteInfo> collectNotes(const QTextDocument *doc)
{
    QList<NoteInfo> notes;
    QHash<QString, int> index;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat cf = it.fragment().charFormat();
            const auto kind = NoteKind(cf.intProperty(props::NoteKind));
            if (kind == NoteKind::None)
                continue;
            const QString id = cf.stringProperty(props::NoteId);
            if (id.isEmpty())
                continue;
            if (index.contains(id)) {
                if (isInlineNote(kind))
                    notes[index.value(id)].text += it.fragment().text();
                continue;
            }
            index.insert(id, notes.size());
            notes.append({id, kind, isInlineNote(kind) ? it.fragment().text() : cf.stringProperty(props::NoteText),
                          cf.property(props::NoteColor).value<QColor>()});
        }
    }
    return notes;
}

QString kindName(NoteKind k)
{
    switch (k) {
    case NoteKind::Comment: return Inspector::tr("Comment");
    case NoteKind::LinkedFootnote: return Inspector::tr("Footnote");
    case NoteKind::InlineAnnotation: return Inspector::tr("Inline annotation");
    case NoteKind::InlineFootnote: return Inspector::tr("Inline footnote");
    default: return QString();
    }
}

QColor kindColor(const NoteInfo &n)
{
    switch (n.kind) {
    case NoteKind::Comment: return n.color.isValid() ? n.color : defaultCommentColor();
    case NoteKind::LinkedFootnote: return QColor(225, 225, 225);
    case NoteKind::InlineAnnotation: return QColor(250, 215, 215);
    case NoteKind::InlineFootnote: return footnoteBubbleColor();
    default: return QColor(240, 240, 240);
    }
}
} // namespace

void Inspector::setCommentScope(const QList<BinderItem *> &items)
{
    QString key;
    for (BinderItem *it : items)
        key += it->id + QLatin1Char(',');
    if (key == m_scopeKey)
        return;
    m_scopeKey = key;
    m_commentScope = items;
    // the cards are made anew for the other kind of list
    for (QWidget *w : std::as_const(m_noteCards))
        w->deleteLater();
    m_noteCards.clear();
    for (QWidget *w : m_commentList->findChildren<QWidget *>(QStringLiteral("scopeHeader"), Qt::FindDirectChildrenOnly))
        w->deleteLater();
    refreshComments();
}

void Inspector::refreshScopeComments()
{
    // a note being typed into stays as it is until the typing pauses
    if (QWidget *f = QApplication::focusWidget(); f && m_commentList->isAncestorOf(f)) {
        m_commentTimer.start();
        return;
    }
    for (QWidget *w : std::as_const(m_noteCards))
        w->deleteLater();
    m_noteCards.clear();
    for (QWidget *w : m_commentList->findChildren<QWidget *>(QStringLiteral("scopeHeader"), Qt::FindDirectChildrenOnly))
        w->deleteLater();
    if (!m_project)
        return;
    int pos = 0;
    int total = 0;
    for (BinderItem *item : std::as_const(m_commentScope)) {
        if (!m_project->item(item->id) || !item->hasText())
            continue;
        QList<NoteInfo> notes;
        m_project->docs()->editStored(item->id, false, [&](QTextDocument *d) {
            notes = collectNotes(d);
            return 0;
        });
        if (notes.isEmpty())
            continue;
        total += notes.size();
        // the document: a heading that opens it
        auto *head = new QLabel(m_commentList);
        head->setObjectName(QStringLiteral("scopeHeader"));
        head->setText(QStringLiteral("<b><a href=\"open\" style=\"text-decoration:none\">%1</a></b> <span style=\"color:gray\">(%2)</span>")
                          .arg((item->title.isEmpty() ? tr("Untitled") : item->title).toHtmlEscaped())
                          .arg(notes.size()));
        head->setToolTip(tr("Open the document in the editor"));
        head->setWordWrap(true);
        head->setContentsMargins(2, pos ? 8 : 2, 2, 0);
        connect(head, &QLabel::linkActivated, this, [this, item] { emit openItemRequested(item); });
        m_commentLayout->insertWidget(pos++, head);
        for (const NoteInfo &n : notes) {
            auto *card = new NoteCard;
            card->noteId = n.id;
            card->kind = n.kind;
            card->setFrameShape(QFrame::StyledPanel);
            card->setAutoFillBackground(true);
            auto *cl = new QVBoxLayout(card);
            cl->setContentsMargins(4, 2, 4, 4);
            card->head = new QLabel(card);
            QFont f = card->head->font();
            f.setPointSizeF(f.pointSizeF() * 0.85);
            card->head->setFont(f);
            card->head->setText(QStringLiteral("<a href=\"note\" style=\"text-decoration:none; color:#303030\">%1 ▸</a>").arg(kindName(n.kind)));
            card->head->setToolTip(tr("Show this note in the editor"));
            const QString noteId = n.id;
            connect(card->head, &QLabel::linkActivated, this, [this, item, noteId] { emit openNoteRequested(item, noteId); });
            cl->addWidget(card->head);
            card->edit = new QPlainTextEdit(card);
            card->edit->setFixedHeight(64);
            card->edit->setFrameShape(QFrame::NoFrame);
            card->edit->setPlainText(n.text);
            const bool inlineNote = isInlineNote(n.kind);
            card->edit->setReadOnly(inlineNote);
            card->edit->setToolTip(inlineNote ? tr("Inline notes are edited directly in the text.") : QString());
            cl->addWidget(card->edit);
            const QColor bg = kindColor(n);
            QPalette pal = card->palette();
            pal.setColor(QPalette::Window, bg);
            pal.setColor(QPalette::Base, bg.lighter(108));
            pal.setColor(QPalette::Text, QColor(20, 20, 20));
            pal.setColor(QPalette::WindowText, QColor(40, 40, 40));
            card->setPalette(pal);
            card->edit->setPalette(pal);
            if (!inlineNote) {
                // typing pauses: the note's text goes into its document (open or not)
                auto *t = new QTimer(card);
                t->setSingleShot(true);
                t->setInterval(500);
                const QString docId = item->id;
                QPointer<NoteCard> guard(card);
                connect(card->edit, &QPlainTextEdit::textChanged, t, qOverload<>(&QTimer::start));
                connect(t, &QTimer::timeout, this, [this, guard, docId, noteId] {
                    if (!guard || !m_project)
                        return;
                    const QString text = guard->edit->toPlainText();
                    m_updating = true;
                    m_project->docs()->editStored(docId, false, [&](QTextDocument *d) {
                        int changed = 0;
                        QTextCursor c(d);
                        c.beginEditBlock();
                        forEachNoteFragment(d, noteId, [&](const QTextFragment &fr) {
                            QTextCharFormat cf = fr.charFormat();
                            if (cf.stringProperty(props::NoteText) == text)
                                return;
                            cf.setProperty(props::NoteText, text);
                            QTextCursor fc(d);
                            fc.setPosition(fr.position());
                            fc.setPosition(fr.position() + fr.length(), QTextCursor::KeepAnchor);
                            fc.setCharFormat(cf);
                            ++changed;
                        });
                        c.endEditBlock();
                        return changed;
                    });
                    m_updating = false;
                });
            }
            m_noteCards.insert(item->id + QLatin1Char('/') + n.id, card);
            m_commentLayout->insertWidget(pos++, card);
        }
    }
    if (total == 0) {
        auto *none = new QLabel(tr("No comments or footnotes in these documents."), m_commentList);
        none->setObjectName(QStringLiteral("scopeHeader"));
        none->setWordWrap(true);
        none->setEnabled(false);
        m_commentLayout->insertWidget(0, none);
    }
    updateTabDots();
}

void Inspector::refreshComments()
{
    if (!m_commentScope.isEmpty()) {
        refreshScopeComments();
        return;
    }
    struct Info {
        QString id;
        NoteKind kind;
        QString text;
        QColor color;
    };
    QList<Info> notes;
    if (m_editor) {
        QHash<QString, int> index;
        for (QTextBlock b = m_editor->document()->begin(); b.isValid(); b = b.next()) {
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const QTextCharFormat cf = it.fragment().charFormat();
                const auto kind = NoteKind(cf.intProperty(props::NoteKind));
                if (kind == NoteKind::None)
                    continue;
                const QString id = cf.stringProperty(props::NoteId);
                if (id.isEmpty())
                    continue;
                if (index.contains(id)) {
                    if (isInlineNote(kind))
                        notes[index.value(id)].text += it.fragment().text();
                    continue;
                }
                index.insert(id, notes.size());
                notes.append({id, kind, isInlineNote(kind) ? it.fragment().text() : cf.stringProperty(props::NoteText),
                              cf.property(props::NoteColor).value<QColor>()});
            }
        }
    }
    // remove cards of vanished notes
    QSet<QString> ids;
    for (const Info &n : notes)
        ids.insert(n.id);
    for (auto it = m_noteCards.begin(); it != m_noteCards.end();) {
        if (!ids.contains(it.key())) {
            it.value()->deleteLater();
            it = m_noteCards.erase(it);
        } else {
            ++it;
        }
    }
    int pos = 0;
    for (const Info &n : notes) {
        auto *card = static_cast<NoteCard *>(m_noteCards.value(n.id));
        if (!card) {
            card = new NoteCard;
            card->noteId = n.id;
            card->setFrameShape(QFrame::StyledPanel);
            card->setAutoFillBackground(true);
            auto *cl = new QVBoxLayout(card);
            cl->setContentsMargins(4, 2, 4, 4);
            card->head = new QLabel(card);
            QFont f = card->head->font();
            f.setPointSizeF(f.pointSizeF() * 0.85);
            card->head->setFont(f);
            card->head->setCursor(Qt::PointingHandCursor);
            cl->addWidget(card->head);
            card->edit = new QPlainTextEdit(card);
            card->edit->setFixedHeight(70);
            card->edit->setFrameShape(QFrame::NoFrame);
            cl->addWidget(card->edit);
            card->setContextMenuPolicy(Qt::CustomContextMenu);
            const QString id = n.id;
            connect(card->edit, &QPlainTextEdit::textChanged, this, [this, card, id] {
                if (m_updating || !m_editor || card->edit->isReadOnly())
                    return;
                m_updating = true;
                m_editor->setNoteText(id, card->edit->toPlainText());
                m_updating = false;
            });
            card->head->installEventFilter(this);
            connect(card, &QWidget::customContextMenuRequested, this, [this, card, id](const QPoint &p) {
                if (!m_editor)
                    return;
                QMenu menu;
                if (card->kind == NoteKind::Comment) {
                    for (const auto &c : {qMakePair(tr("Yellow"), QColor(255, 236, 150)), qMakePair(tr("Green"), QColor(190, 235, 170)),
                                          qMakePair(tr("Blue"), QColor(180, 215, 250)), qMakePair(tr("Pink"), QColor(250, 190, 215)),
                                          qMakePair(tr("Orange"), QColor(250, 210, 160))}) {
                        QPixmap pm(12, 12);
                        pm.fill(c.second);
                        const QColor col = c.second;
                        menu.addAction(QIcon(pm), c.first, this, [this, id, col] { m_editor->setNoteColor(id, col); });
                    }
                    menu.addSeparator();
                    menu.addAction(tr("Convert to Footnote"), this, [this, id] { m_editor->convertNote(id, NoteKind::LinkedFootnote); });
                } else if (card->kind == NoteKind::LinkedFootnote) {
                    menu.addAction(tr("Convert to Comment"), this, [this, id] { m_editor->convertNote(id, NoteKind::Comment); });
                } else if (card->kind == NoteKind::InlineAnnotation) {
                    menu.addAction(tr("Convert to Inline Footnote"), this, [this, id] { m_editor->convertNote(id, NoteKind::InlineFootnote); });
                } else if (card->kind == NoteKind::InlineFootnote) {
                    menu.addAction(tr("Convert to Inline Annotation"), this, [this, id] { m_editor->convertNote(id, NoteKind::InlineAnnotation); });
                }
                menu.addSeparator();
                menu.addAction(icons::action(QStringLiteral("edit-delete")), tr("Delete"), this, [this, id] { m_editor->removeNote(id); });
                menu.exec(card->mapToGlobal(p));
            });
            m_noteCards.insert(n.id, card);
        }
        card->kind = n.kind;
        QString kindName;
        QColor bg;
        switch (n.kind) {
        case NoteKind::Comment:
            kindName = tr("Comment");
            bg = n.color.isValid() ? n.color : defaultCommentColor();
            break;
        case NoteKind::LinkedFootnote:
            kindName = tr("Footnote");
            bg = QColor(225, 225, 225);
            break;
        case NoteKind::InlineAnnotation:
            kindName = tr("Inline annotation");
            bg = QColor(250, 215, 215);
            break;
        case NoteKind::InlineFootnote:
            kindName = tr("Inline footnote");
            bg = footnoteBubbleColor();
            break;
        default: break;
        }
        card->head->setText(kindName + QStringLiteral(" ▸"));
        QPalette pal = card->palette();
        pal.setColor(QPalette::Window, bg);
        pal.setColor(QPalette::Base, bg.lighter(108));
        pal.setColor(QPalette::Text, QColor(20, 20, 20));
        pal.setColor(QPalette::WindowText, QColor(40, 40, 40));
        card->setPalette(pal);
        card->edit->setPalette(pal);
        const bool inlineNote = isInlineNote(n.kind);
        card->edit->setReadOnly(inlineNote);
        card->edit->setToolTip(inlineNote ? tr("Inline notes are edited directly in the text.") : QString());
        if (card->edit->toPlainText() != n.text && !card->edit->hasFocus()) {
            m_updating = true;
            card->edit->setPlainText(n.text);
            m_updating = false;
        }
        m_commentLayout->insertWidget(pos++, card);
        QFrame *frame = card;
        frame->setLineWidth(n.id == m_focusNote ? 2 : 1);
    }
    updateTabDots();
}

bool Inspector::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_synopsisImage && e->type() == QEvent::MouseButtonDblClick && m_item && m_project) {
        const QString f = QFileDialog::getOpenFileName(this, tr("Choose an Image"), QDir::homePath(),
                                                       tr("Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp)"));
        if (!f.isEmpty())
            setSynopsisImage(f);
        return true;
    }
    if (e->type() == QEvent::MouseButtonPress) {
        for (auto it = m_noteCards.cbegin(); it != m_noteCards.cend(); ++it) {
            auto *card = static_cast<NoteCard *>(it.value());
            if (card->head == o) {
                m_focusNote = it.key();
                if (m_editor)
                    m_editor->selectNote(it.key());
                emit selectNoteRequested(it.key());
                return true;
            }
        }
    }
    return QWidget::eventFilter(o, e);
}

// ------------------------------------------------------------------ general

void Inspector::setProject(Project *p)
{
    if (m_project) {
        disconnect(m_project, nullptr, this, nullptr);
        disconnect(m_project->docs(), nullptr, this, nullptr);
        if (m_project->objectsLoaded())
            disconnect(m_project->objects(), nullptr, this, nullptr);
    }
    rememberNotesPlace();
    m_notesKey.clear();
    stopPreview();
    m_project = p;
    m_item = nullptr;
    applyNotesZoom(); // the notes of this project in the inspector's zoom (0.21)
    m_editor = nullptr;
    m_commentScope.clear();
    m_scopeKey.clear();
    if (p) {
        connect(p->docs(), &DocumentStore::textEdited, this, [this](const QString &id) {
            if (m_updating || m_commentScope.isEmpty())
                return;
            for (BinderItem *it : std::as_const(m_commentScope))
                if (it->id == id) {
                    m_commentTimer.start();
                    return;
                }
        });
        connect(p, &Project::itemChanged, this, [this](BinderItem *it) {
            if (it == m_item && !m_updating) {
                m_updating = true;
                if (!m_cardTitle->hasFocus())
                    m_cardTitle->setText(it->title);
                if (!m_synopsis->hasFocus() && m_synopsis->toPlainText() != it->synopsis)
                    m_synopsis->setPlainText(it->synopsis);
                m_updating = false;
                refreshLabelStatus();
                m_modified->setText(it->modified.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
            }
        });
        connect(p, &Project::settingsChanged, this, [this] {
            refreshLabelStatus();
            refreshMetadata();
            applyPreviewZoom(); // the project's font for the bookmarked text
        });
        // the bookmarked document goes (emptied trash, reloaded): so does its copy
        connect(p->docs(), &DocumentStore::aboutToDeleteDocument, this, [this](QTextDocument *d) {
            if (m_previewMirror && m_previewMirror->source() == d)
                stopPreview();
        });
        connect(p, &Project::bookmarksChanged, this, [this] { refreshBookmarks(); });
        // "In this document" and the tab mark follow the links of the objects
        auto *st = p->objects();
        connect(st, &objects::Store::objectChanged, this, [this] { updateTabDots(); });
        connect(st, &objects::Store::objectRemoved, this, [this] { updateTabDots(); });
    }
    m_objects->setProject(p);
    refreshAll();
}

void Inspector::setItem(BinderItem *item)
{
    if (item == m_item)
        return;
    m_item = item;
    m_imageToggle->setChecked(item && !item->synopsisImage.isEmpty());
    m_objects->setDocument(item);
    refreshAll();
}

void Inspector::setEditor(TextEditor *editor)
{
    if (m_editor == editor)
        return;
    if (m_editor)
        disconnect(m_editor->document(), nullptr, this, nullptr);
    m_editor = editor;
    if (m_editor)
        connect(m_editor->document(), &QTextDocument::contentsChanged, this, [this] {
            if (!m_updating)
                m_commentTimer.start();
        });
    refreshComments();
}

void Inspector::refreshAll()
{
    refreshMindMap();
    refreshNotesTab();
    refreshBookmarks();
    refreshMetadata();
    refreshSnapshots();
    refreshComments();
    refreshLabelStatus();
    updateTabDots();
}

void Inspector::refreshLabelStatus()
{
    m_label->clear();
    m_status->clear();
    const bool has = m_item && m_project;
    m_label->setEnabled(has);
    m_status->setEnabled(has);
    if (!m_project)
        return;
    m_labelTitle->setText(m_project->labelTitle);
    m_statusTitle->setText(m_project->statusTitle);
    m_label->addItem(tr("No %1").arg(m_project->labelTitle), -1);
    for (const Label &l : m_project->labels) {
        QPixmap pm(12, 12);
        pm.fill(l.color);
        m_label->addItem(QIcon(pm), l.name, l.id);
    }
    m_label->insertSeparator(m_label->count());
    m_label->addItem(tr("Edit…"), -2);
    m_status->addItem(tr("No %1").arg(m_project->statusTitle), -1);
    for (const Status &s : m_project->statuses)
        m_status->addItem(s.name, s.id);
    m_status->insertSeparator(m_status->count());
    m_status->addItem(tr("Edit…"), -2);
    if (has) {
        m_label->setCurrentIndex(qMax(0, m_label->findData(m_item->label)));
        m_status->setCurrentIndex(qMax(0, m_status->findData(m_item->status)));
    }
    applyLabelTint();
}

// The label color of the document, with 15 % of its saturation, at the
// brightness of the paper (so dark themes stay dark); invalid without a label.
QColor Inspector::labelTint(const QColor &paper) const
{
    if (!m_item || !m_project || m_item->label < 0)
        return QColor();
    const Label *l = m_project->labelById(m_item->label);
    if (!l || !l->color.isValid())
        return QColor();
    const QColor c = l->color.toHsv();
    if (c.hsvHueF() < 0) // grey: no hue to show
        return QColor();
    return QColor::fromHsvF(c.hsvHueF(), c.hsvSaturationF() * 0.15f, paper.valueF());
}

void Inspector::applyLabelTint()
{
    auto *card = static_cast<CardEdit *>(m_synopsis);
    card->paper = labelTint(theme::color(theme::Role::SynopsisPaper));
    card->setProperty("qbTint", card->paper);
    theme::styleView(m_notes, theme::Role::NotesPaper, theme::Role::NotesText);
    const QColor notesTint = m_notesScope->currentIndex() == 0 ? labelTint(theme::color(theme::Role::NotesPaper)) : QColor();
    if (notesTint.isValid()) {
        QPalette pal = m_notes->palette();
        pal.setColor(QPalette::Base, notesTint);
        pal.setColor(QPalette::Window, notesTint);
        m_notes->setPalette(pal);
    }
    m_synopsis->viewport()->update();
    m_notes->viewport()->update();
}

void Inspector::showTab(Tab t)
{
    m_stack->setCurrentIndex(int(t));
    for (int i = 0; i < m_tabButtons.size(); ++i)
        m_tabButtons[i]->setChecked(i == int(t));
    if (t == MindMap)
        refreshMindMap();
    refreshZoomBox();
}

void Inspector::refreshMindMapZoom()
{
    // the board's zoom shown in the zoom list while the Mind Map tab is shown (0.21.1)
    if (m_mindMap->view())
        connect(m_mindMap->view(), &MindMapView::zoomChanged, this, &Inspector::refreshZoomBox, Qt::UniqueConnection);
    refreshZoomBox();
}

// ------------------------------------------------------------------ zoom (0.21)

// The names of the tabs in the settings (zoom/inspector/<name> when each tab has its own zoom).
static const char *const kTabNames[] = {"notes", "bookmarks", "metadata", "snapshots", "comments", "mindmap", "objects"};

QString Inspector::zoomKey(int tab) const
{
    if (tab < 0)
        tab = m_stack ? m_stack->currentIndex() : 0;
    return zoom::inspectorKey(QLatin1String(kTabNames[qBound(0, tab, 6)]));
}

// tabs with two parts, each with a zoom of its own (0.21.1)
static bool twoParts(int tab)
{
    return tab == Inspector::Notes || tab == Inspector::Bookmarks || tab == Inspector::Snapshots || tab == Inspector::Objects;
}

void Inspector::applyZoomMode()
{
    // One zoom for the whole inspector: the stack of tabs is one area. One per
    // tab: every page is an area of its own (and the stack is none). The tabs
    // with two parts have a zoom per part in both modes (their pages are marked
    // once, when they are made); the row of tab buttons and the bottom row go
    // with the inspector's zoom.
    if (!m_stack)
        return;
    const bool per = zoom::perTab();
    for (int i = 0; i < m_stack->count(); ++i) {
        if (twoParts(i))
            continue;
        QWidget *page = m_stack->widget(i);
        zoom::detachFonts(page);
        zoom::unmark(page);
    }
    zoom::detachFonts(m_stack);
    zoom::unmark(m_stack);
    if (per) {
        for (int i = 0; i < m_stack->count(); ++i) {
            if (twoParts(i))
                continue;
            zoom::mark(m_stack->widget(i), zoomKey(i));
            zoom::attachFonts(m_stack->widget(i), zoomKey(i));
        }
    } else {
        zoom::mark(m_stack, QLatin1String(zoom::kInspector));
        zoom::attachFonts(m_stack, QLatin1String(zoom::kInspector));
    }
    for (QWidget *row : {m_tabsRow, m_bottomRow})
        if (row) {
            zoom::mark(row, QLatin1String(zoom::kInspector));
            zoom::attachFonts(row, QLatin1String(zoom::kInspector));
        }
    applyNotesZoom();
    refreshZoomBox();
}

void Inspector::applyNotesZoom()
{
    // The notes are documents of the project: their text gets the zoom in the
    // document store (fonts set in the text are scaled as well). Since 0.21.1
    // the notes are a part of their own (the synopsis card has another zoom).
    if (m_project)
        m_project->docs()->setNotesZoom(zoom::factor(QLatin1String(zoom::kInspNotes)));
}

QString Inspector::currentZoomKey() const
{
    const int tab = m_stack ? m_stack->currentIndex() : 0;
    if (tab == MindMap && m_mindMap && m_mindMap->view())
        return QStringLiteral("board");
    if (!twoParts(tab))
        return zoomKey(tab);
    // the part with the keyboard
    QWidget *page = m_stack->widget(tab);
    if (QWidget *f = QApplication::focusWidget(); f && page->isAncestorOf(f)) {
        for (QWidget *x = f; x && x != this; x = x->parentWidget()) {
            const QString key = x->property("qbZoomArea").toString();
            if (key.startsWith(QLatin1String("inspector.")))
                return key;
        }
    }
    if (m_lastZoomPart.contains(tab))
        return m_lastZoomPart.value(tab);
    switch (tab) {
    case Notes: return QLatin1String(zoom::kInspNotes);
    case Bookmarks: return QLatin1String(zoom::kInspPreview);
    case Snapshots: return QLatin1String(zoom::kInspSnapText);
    default: return QLatin1String(zoom::kInspObjectSheet);
    }
}

void Inspector::refreshZoomBox()
{
    if (!m_zoomBox)
        return;
    const QString key = currentZoomKey();
    double f = 1.0;
    if (key == QLatin1String("board"))
        f = m_mindMap->view()->zoom();
    else
        f = zoom::factor(key);
    const int now = qRound(f * 100);
    if (m_zoomTitle) {
        // which part the list shows: a short name above it (the row is narrow), the full one as its tip
        static const QHash<QString, const char *> shortNames = {
            {QLatin1String(zoom::kInspSynopsis), QT_TR_NOOP("Synopsis")}, {QLatin1String(zoom::kInspNotes), QT_TR_NOOP("Notes")},
            {QLatin1String(zoom::kInspBookmarks), QT_TR_NOOP("Lists")},    {QLatin1String(zoom::kInspPreview), QT_TR_NOOP("Text")},
            {QLatin1String(zoom::kInspSnapList), QT_TR_NOOP("List")},      {QLatin1String(zoom::kInspSnapText), QT_TR_NOOP("Text")},
            {QLatin1String(zoom::kInspObjectList), QT_TR_NOOP("List")},    {QLatin1String(zoom::kInspObjectSheet), QT_TR_NOOP("Sheet")},
            {QStringLiteral("board"), QT_TR_NOOP("Map")}};
        const char *part = shortNames.value(key, nullptr);
        m_zoomTitle->setText(part ? tr("Zoom: %1").arg(tr(part)) : tr("Zoom"));
        const QString full = key == QLatin1String("board") ? tr("the mind map of this document") : zoom::areaName(key);
        m_zoomTitle->setToolTip(tr("The zoom list shows and sets the zoom of: %1").arg(full));
        m_zoomBox->setToolTip(m_zoomTitle->toolTip() + QStringLiteral("\n")
                              + tr("Ctrl + mouse wheel over a part changes the zoom of that part. "
                                   "Right-click: one zoom for the tabs Metadata and Comments, or one each."));
    }
    const QSignalBlocker block(m_zoomBox);
    m_zoomBox->clear();
    QList<int> values = {50, 75, 90, 100, 110, 125, 150, 175, 200, 250, 300};
    if (!values.contains(now)) {
        values << now;
        std::sort(values.begin(), values.end());
    }
    for (int v : values)
        m_zoomBox->addItem(QStringLiteral("%1%").arg(v), v);
    m_zoomBox->setCurrentIndex(m_zoomBox->findData(now));
}

// ------------------------------------------------------------------ state (0.21.1)

QVariantMap Inspector::saveState()
{
    rememberNotesPlace();
    QVariantMap m;
    m[QStringLiteral("tab")] = currentTab();
    auto sizes = [](QSplitter *sp) {
        QVariantList l;
        if (sp)
            for (int v : sp->sizes())
                l << v;
        return l;
    };
    m[QStringLiteral("notesSplit")] = sizes(m_notesSplit);
    m[QStringLiteral("bookmarkSplit")] = sizes(m_bookmarkSplit);
    m[QStringLiteral("snapshotSplit")] = sizes(m_snapSplit);
    if (m_bookmarks->currentRow() >= 0)
        m[QStringLiteral("bookmark")] = QVariantMap{{QStringLiteral("project"), false}, {QStringLiteral("row"), m_bookmarks->currentRow()}};
    else if (m_projectBookmarks->currentRow() >= 0)
        m[QStringLiteral("bookmark")] = QVariantMap{{QStringLiteral("project"), true}, {QStringLiteral("row"), m_projectBookmarks->currentRow()}};
    if (m_bookmarkPreview && m_bookmarkPreview->isVisible())
        m[QStringLiteral("previewCursor")] = m_bookmarkPreview->textCursor().position();
    m[QStringLiteral("notesScope")] = m_notesScope->currentIndex();
    m[QStringLiteral("objects")] = m_objects->saveState();
    return m;
}

void Inspector::restoreState(const QVariantMap &m)
{
    if (m.isEmpty())
        return;
    auto setSizes = [](QSplitter *sp, const QVariant &v) {
        const QVariantList l = v.toList();
        if (!sp || l.size() != sp->count())
            return;
        QList<int> sizes;
        int total = 0;
        for (const QVariant &x : l) {
            sizes << qMax(0, x.toInt());
            total += sizes.last();
        }
        if (total > 0)
            sp->setSizes(sizes);
    };
    setSizes(m_notesSplit, m.value(QStringLiteral("notesSplit")));
    setSizes(m_bookmarkSplit, m.value(QStringLiteral("bookmarkSplit")));
    setSizes(m_snapSplit, m.value(QStringLiteral("snapshotSplit")));
    if (m.contains(QStringLiteral("notesScope")))
        m_notesScope->setCurrentIndex(qBound(0, m.value(QStringLiteral("notesScope")).toInt(), 1));
    const int tab = m.value(QStringLiteral("tab"), -1).toInt();
    if (tab >= 0 && tab < m_stack->count())
        showTab(Tab(tab));
    const QVariantMap bm = m.value(QStringLiteral("bookmark")).toMap();
    if (!bm.isEmpty()) {
        BookmarkListWidget *list = bm.value(QStringLiteral("project")).toBool() ? m_projectBookmarks : m_bookmarks;
        const int row = bm.value(QStringLiteral("row")).toInt();
        if (row >= 0 && row < list->count()) {
            list->setCurrentRow(row);
            const int pos = m.value(QStringLiteral("previewCursor"), -1).toInt();
            if (pos >= 0 && m_previewCopy && m_bookmarkPreview->isVisible()) {
                QTextCursor c(m_previewCopy);
                c.setPosition(qBound(0, pos, m_previewCopy->characterCount() - 1));
                m_bookmarkPreview->setTextCursor(c);
                m_bookmarkPreview->ensureCursorVisible();
            }
        }
    }
    m_objects->restoreState(m.value(QStringLiteral("objects")).toMap());
}

void Inspector::refreshMindMap()
{
    // the board is opened only while the tab is shown (each document has one)
    if (m_stack->currentIndex() != int(MindMap))
        return;
    m_mindMap->setItem(m_project, m_item ? m_item->id : QString());
    refreshMindMapZoom();
}

int Inspector::currentTab() const { return m_stack->currentIndex(); }

void Inspector::applyTheme()
{
    QPalette p = QApplication::palette();
    p.setColor(QPalette::Window, theme::color(theme::Role::InspectorBackground));
    p.setColor(QPalette::WindowText, theme::color(theme::Role::InspectorText));
    setPalette(p);
    theme::styleView(m_synopsis, theme::Role::SynopsisPaper, theme::Role::SynopsisText, theme::FontRole::Synopsis);
    theme::styleView(m_notes, theme::Role::NotesPaper, theme::Role::NotesText);
    applyLabelTint();
}

void Inspector::updateTabDots()
{
    // a dot marks tabs with content
    auto mark = [this](int i, bool on) {
        QString tip = m_tabButtons[i]->toolTip();
        tip.remove(QStringLiteral(" •"));
        m_tabButtons[i]->setToolTip(on ? tip + QStringLiteral(" •") : tip);
        QFont f = m_tabButtons[i]->font();
        f.setUnderline(on);
        m_tabButtons[i]->setFont(f);
        m_tabButtons[i]->setStyleSheet(on ? QStringLiteral("QToolButton { border-bottom: 2px solid palette(highlight); }")
                                          : QString());
    };
    if (!m_item || !m_project) {
        for (int i = 0; i < m_tabButtons.size(); ++i)
            mark(i, false);
        return;
    }
    mark(Notes, !m_item->synopsis.isEmpty() || !m_project->docs()->notesPlainText(m_item->id).trimmed().isEmpty());
    mark(Bookmarks, !m_item->bookmarks.isEmpty());
    mark(Metadata, !m_item->keywords.isEmpty() || !m_item->custom.isEmpty());
    mark(Snapshots, m_item->hasSnapshots);
    mark(Comments, !m_noteCards.isEmpty());
    mark(MindMap, mindmaps::hasMap(m_project, m_item->id));
    mark(Objects, !m_project->objects()->objectsForDocument(m_item->id).isEmpty());
}

} // namespace qb

namespace qb {
void Inspector::contextMenuEvent(QContextMenuEvent *e)
{
    lookmenu::popup(lookmenu::Part::Inspector, e->globalPos(), window());
}
} // namespace qb
