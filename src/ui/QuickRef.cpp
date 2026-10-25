#include "ui/QuickRef.h"
#include <cmath>
#include "ui/Zoom.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/ItemIcons.h"
#include "ui/AppSettings.h"
#include "ui/BookmarkList.h"
#include "ui/DragDrop.h"
#include "ui/MediaView.h"
#include "ui/TextColumnView.h"
#include "ui/TextEditor.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace qb {

static QToolButton *barButton(QWidget *parent, const QString &icon, const QString &tip)
{
    auto *b = new QToolButton(parent);
    b->setIcon(icons::action(icon));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setIconSize(QSize(16, 16));
    return b;
}

QuickRefWindow::QuickRefWindow(Project *p, BinderItem *item, QWidget *parent)
    : QWidget(parent, Qt::Window), m_project(p)
{
    setAttribute(Qt::WA_DeleteOnClose);
    resize(560, 640);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // ---- header: icon (menu, drag handle), title, lower pane, float
    m_header = new QWidget(this);
    m_header->setObjectName(QStringLiteral("quickRefHeader"));
    auto *hl = new QHBoxLayout(m_header);
    hl->setContentsMargins(6, 3, 6, 3);
    hl->setSpacing(4);
    m_icon = barButton(m_header, QStringLiteral("text-x-generic"),
                       tr("Document options (drag the icon to put the document somewhere else)"));
    m_icon->setObjectName(QStringLiteral("quickRefIcon"));
    hl->addWidget(m_icon);
    m_title = new QLineEdit(m_header);
    m_title->setObjectName(QStringLiteral("quickRefTitle"));
    m_title->setFrame(false);
    m_title->setPlaceholderText(tr("Untitled"));
    m_title->setAcceptDrops(false); // a document dropped on the header bar is for the panel, not text for the title
    QFont f = m_title->font();
    f.setBold(true);
    m_title->setFont(f);
    m_title->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; }"));
    hl->addWidget(m_title, 1);
    m_panelButton = barButton(m_header, QStringLiteral("view-list-details"),
                              tr("Show the synopsis, notes, keywords or metadata of the document below it"));
    m_panelButton->setObjectName(QStringLiteral("quickRefPanelButton"));
    m_panelButton->setPopupMode(QToolButton::InstantPopup);
    hl->addWidget(m_panelButton);
    m_pin = barButton(m_header, QStringLiteral("window-pin"),
                      tr("Keep this window above the main window (for all windows: Window ▸ Float Quick Reference Panels)"));
    m_pin->setText(tr("Float"));
    m_pin->setCheckable(true);
    hl->addWidget(m_pin);
    lay->addWidget(m_header);

    {
        auto *menu = new QMenu(m_panelButton);
        auto *group = new QActionGroup(menu);
        const QList<QPair<Panel, QString>> entries{{NoPanel, tr("Document Only")},
                                                   {Synopsis, tr("Synopsis")},
                                                   {Notes, tr("Notes")},
                                                   {Keywords, tr("Keywords")},
                                                   {Metadata, tr("Metadata")}};
        for (const auto &en : entries) {
            QAction *ac = menu->addAction(en.second);
            ac->setCheckable(true);
            ac->setData(int(en.first));
            group->addAction(ac);
            if (en.first == NoPanel)
                menu->addSeparator();
            connect(ac, &QAction::triggered, this, [this, panel = en.first] { setPanel(panel); });
        }
        connect(menu, &QMenu::aboutToShow, this, [this, group] {
            for (QAction *ac : group->actions())
                ac->setChecked(ac->data().toInt() == int(m_panel));
        });
        m_panelButton->setMenu(menu);
    }

    // ---- body: bookmarks sidebar | document over lower pane
    m_sideSplit = new QSplitter(Qt::Horizontal, this);
    m_sideSplit->setChildrenCollapsible(false);
    m_sidebar = new QWidget(m_sideSplit);
    m_sidebar->setObjectName(QStringLiteral("quickRefSidebar"));
    auto *sl = new QVBoxLayout(m_sidebar);
    sl->setContentsMargins(4, 4, 0, 2);
    sl->setSpacing(2);
    auto *sideTitle = new QLabel(tr("Project Bookmarks"), m_sidebar);
    QFont sf = sideTitle->font();
    sf.setBold(true);
    sf.setPointSizeF(qMax(7.0, sf.pointSizeF() - 1));
    sideTitle->setFont(sf);
    sl->addWidget(sideTitle);
    m_bookmarks = new BookmarkListWidget(m_sidebar);
    m_bookmarks->setObjectName(QStringLiteral("quickRefBookmarks"));
    sl->addWidget(m_bookmarks, 1);
    auto *sideFoot = new QHBoxLayout;
    auto *addDoc = barButton(m_sidebar, QStringLiteral("list-add"), tr("New document, added to the project bookmarks"));
    addDoc->setObjectName(QStringLiteral("quickRefNewBookmark"));
    auto *removeBm = barButton(m_sidebar, QStringLiteral("list-remove"),
                               tr("Remove the bookmark (the document stays in the binder)"));
    removeBm->setObjectName(QStringLiteral("quickRefRemoveBookmark"));
    sideFoot->addWidget(addDoc);
    sideFoot->addWidget(removeBm);
    sideFoot->addStretch(1);
    sl->addLayout(sideFoot);
    m_sideSplit->addWidget(m_sidebar);

    m_panelSplit = new QSplitter(Qt::Vertical, m_sideSplit);
    m_panelSplit->setChildrenCollapsible(false);
    m_content = new QStackedWidget(m_panelSplit);
    m_view = new TextColumnView(m_content);
    m_view->setProject(p);
    m_media = new MediaView(m_content);
    m_media->setProject(p);
    // Ctrl + mouse wheel (0.21): the text follows the project's text zoom (one layout per document)
    zoom::setHandler(this, [this](int steps) {
        if (m_project)
            m_project->docs()->setZoom(m_project->docs()->zoom() + steps * zoom::kStep);
    });
    zoom::setHandler(m_media, [this](int steps) { m_media->zoomBy(std::pow(1.15, steps)); });
    m_media->setOwnControls(true);
    m_noContent = new QLabel(tr("Nothing to show"), m_content);
    m_noContent->setAlignment(Qt::AlignCenter);
    m_noContent->setEnabled(false);
    m_content->addWidget(m_view);
    m_content->addWidget(m_media);
    m_content->addWidget(m_noContent);
    m_panelSplit->addWidget(m_content);
    buildPanels();
    m_panelSplit->addWidget(m_panelBox);
    m_panelSplit->setStretchFactor(0, 3);
    m_panelSplit->setStretchFactor(1, 1);
    m_sideSplit->addWidget(m_panelSplit);
    m_sideSplit->setStretchFactor(0, 0);
    m_sideSplit->setStretchFactor(1, 1);
    lay->addWidget(m_sideSplit, 1);
    m_sidebar->hide();
    m_panelBox->hide();

    // ---- footer: bookmarks, count, label, status
    auto *foot = new QWidget(this);
    auto *fl = new QHBoxLayout(foot);
    fl->setContentsMargins(6, 3, 6, 3);
    m_bookmarkToggle = barButton(foot, QStringLiteral("bookmarks"),
                                 tr("Show the project bookmarks beside the document; a click on one shows it in this panel"));
    m_bookmarkToggle->setObjectName(QStringLiteral("quickRefBookmarkToggle"));
    m_bookmarkToggle->setCheckable(true);
    fl->addWidget(m_bookmarkToggle);
    m_count = new QLabel(foot);
    m_count->setObjectName(QStringLiteral("quickRefCount"));
    m_count->setEnabled(false);
    fl->addWidget(m_count);
    fl->addStretch(1);
    m_label = new QComboBox(foot);
    m_label->setObjectName(QStringLiteral("quickRefLabel"));
    m_status = new QComboBox(foot);
    m_status->setObjectName(QStringLiteral("quickRefStatus"));
    fl->addWidget(m_label);
    fl->addWidget(m_status);
    lay->addWidget(foot);

    // ---- behaviour
    connect(m_pin, &QToolButton::toggled, this, &QuickRefWindow::setFloating);
    connect(m_icon, &QToolButton::clicked, this, &QuickRefWindow::headerMenu);
    dnd::makeDragHandle(
        m_icon, [this] { return m_project.data(); },
        [this] { return m_item ? QList<BinderItem *>{m_item} : QList<BinderItem *>(); });
    // a document dropped on the header bar is shown in this panel
    new dnd::DropTarget(
        m_header,
        [this](const QMimeData *md) { return m_project && !BinderModel::itemsFromMime(md, m_project).isEmpty(); },
        [this](const QMimeData *md, const QPoint &, Qt::KeyboardModifiers) {
            const QList<BinderItem *> items = BinderModel::itemsFromMime(md, m_project);
            if (!items.isEmpty())
                showItem(items.first());
        });
    m_header->installEventFilter(this);
    connect(m_title, &QLineEdit::editingFinished, this, [this] {
        if (m_item && m_project && m_title->text() != m_item->title)
            m_project->model()->setData(m_project->model()->indexFor(m_item), m_title->text(), Qt::EditRole);
    });
    connect(m_label, qOverload<int>(&QComboBox::activated), this, [this](int i) {
        if (m_item && m_project) {
            m_item->label = m_label->itemData(i).toInt();
            m_project->notifyItemChanged(m_item, false);
        }
    });
    connect(m_status, qOverload<int>(&QComboBox::activated), this, [this](int i) {
        if (m_item && m_project) {
            m_item->status = m_status->itemData(i).toInt();
            m_project->notifyItemChanged(m_item, false);
        }
    });
    connect(m_bookmarkToggle, &QToolButton::toggled, this, &QuickRefWindow::setBookmarksShown);
    m_bookmarks->setSource(p, [this]() -> QList<Bookmark> * { return m_project ? &m_project->projectBookmarks : nullptr; });
    // a click on a bookmarked document shows it here; files and web links open with a double click
    connect(m_bookmarks, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_loading)
            return;
        if (BinderItem *it = m_bookmarks->documentAt(row))
            if (it != m_item)
                showItem(it);
    });
    connect(m_bookmarks, &QListWidget::itemClicked, this, [this](QListWidgetItem *li) {
        if (BinderItem *it = m_bookmarks->documentAt(m_bookmarks->row(li)))
            if (it != m_item)
                showItem(it);
    });
    connect(m_bookmarks, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *li) {
        const Bookmark b = m_bookmarks->bookmarkAt(m_bookmarks->row(li));
        if (!b.isInternal())
            BookmarkListWidget::openExternally(b);
    });
    connect(addDoc, &QToolButton::clicked, this, &QuickRefWindow::addBookmarkedDocument);
    connect(removeBm, &QToolButton::clicked, this, [this] { m_bookmarks->removeBookmark(m_bookmarks->currentRow()); });
    connect(m_view, &TextColumnView::linkActivated, this, &QuickRefWindow::linkActivated);

    connect(p, &Project::itemChanged, this, [this](BinderItem *it) {
        if (it != m_item)
            return;
        refreshHeader();
        refreshFooter();
        refreshPanel(true);
    });
    connect(p, &Project::settingsChanged, this, [this] {
        refreshFooter();
        refreshPanel(true);
    });
    connect(p, &Project::bookmarksChanged, this, [this] {
        m_loading = true;
        m_bookmarks->refresh();
        m_loading = false;
    });
    connect(p->docs(), &DocumentStore::countsUpdated, this, [this](const QString &id) {
        if (m_item && id == m_item->id)
            refreshFooter();
    });
    // the document was deleted for good (trash emptied): nothing left to show
    connect(p->model(), &QAbstractItemModel::rowsRemoved, this, [this] {
        if (m_project && !m_itemId.isEmpty() && !m_project->item(m_itemId)) {
            m_item = nullptr;
            QTimer::singleShot(0, this, &QWidget::close);
        }
    });
    showItem(item);
}

void QuickRefWindow::buildPanels()
{
    m_panelBox = new QWidget(m_panelSplit);
    m_panelBox->setObjectName(QStringLiteral("quickRefPanel"));
    auto *bl = new QVBoxLayout(m_panelBox);
    bl->setContentsMargins(6, 2, 6, 4);
    bl->setSpacing(2);
    m_panelTitle = new QLabel(m_panelBox);
    QFont f = m_panelTitle->font();
    f.setBold(true);
    f.setPointSizeF(qMax(7.0, f.pointSizeF() - 1));
    m_panelTitle->setFont(f);
    bl->addWidget(m_panelTitle);
    m_panels = new QStackedWidget(m_panelBox);
    bl->addWidget(m_panels, 1);

    m_panels->addWidget(new QWidget(m_panels)); // NoPanel

    m_synopsis = new QPlainTextEdit(m_panels);
    m_synopsis->setObjectName(QStringLiteral("quickRefSynopsis"));
    m_synopsis->setPlaceholderText(tr("Synopsis: a short summary shown on index cards and in the outliner."));
    m_panels->addWidget(m_synopsis);
    connect(m_synopsis, &QPlainTextEdit::textChanged, this, [this] {
        if (m_loading || !m_item || !m_project || m_item->synopsis == m_synopsis->toPlainText())
            return;
        m_item->synopsis = m_synopsis->toPlainText();
        m_project->notifyItemChanged(m_item, false);
    });

    m_notes = new TextEditor(m_panels);
    m_notes->setObjectName(QStringLiteral("quickRefNotes"));
    m_notes->setLineWrapMode(QTextEdit::WidgetWidth);
    m_panels->addWidget(m_notes);

    auto *kwPage = new QWidget(m_panels);
    auto *kl = new QHBoxLayout(kwPage);
    kl->setContentsMargins(0, 0, 0, 0);
    m_keywords = new QListWidget(kwPage);
    m_keywords->setObjectName(QStringLiteral("quickRefKeywords"));
    kl->addWidget(m_keywords, 1);
    auto *kb = new QVBoxLayout;
    auto *addKw = barButton(kwPage, QStringLiteral("list-add"), tr("Add a keyword of the project"));
    addKw->setPopupMode(QToolButton::InstantPopup);
    auto *remKw = barButton(kwPage, QStringLiteral("list-remove"), tr("Remove the keyword from this document"));
    kb->addWidget(addKw);
    kb->addWidget(remKw);
    kb->addStretch(1);
    kl->addLayout(kb);
    m_panels->addWidget(kwPage);
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
                if (m_item && m_project)
                    dnd::assignKeywords(m_project, {m_item}, {id}, false);
            });
        }
        if (kwMenu->isEmpty())
            kwMenu->addAction(tr("No more keywords (Project ▸ Project Keywords)"))->setEnabled(false);
    });
    connect(remKw, &QToolButton::clicked, this, [this] {
        QListWidgetItem *li = m_keywords->currentItem();
        if (!li || !m_item || !m_project)
            return;
        m_item->keywords.removeAll(li->data(Qt::UserRole).toString());
        m_project->notifyItemChanged(m_item, false);
    });
    // keywords dragged from the Project Keywords window (with Alt: together with their groups)
    new dnd::DropTarget(
        m_keywords->viewport(),
        [this](const QMimeData *md) { return m_item && m_project && md->hasFormat(QString::fromLatin1(kKeywordsMime)); },
        [this](const QMimeData *md, const QPoint &, Qt::KeyboardModifiers mods) {
            if (m_item && m_project)
                dnd::assignKeywords(m_project, {m_item}, dnd::keywordsFromMime(md, m_project), mods & Qt::AltModifier);
        });

    auto *scroll = new QScrollArea(m_panels);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_metaPage = new QWidget(scroll);
    m_metaPage->setObjectName(QStringLiteral("quickRefMetadata"));
    m_metaForm = new QFormLayout(m_metaPage);
    m_metaForm->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(m_metaPage);
    m_panels->addWidget(scroll);
}

// ------------------------------------------------------------------ content

void QuickRefWindow::showItem(BinderItem *item)
{
    if (!item || !m_project)
        return;
    m_item = item;
    m_itemId = item->id;
    const bool media = isMediaType(item->type);
    if (media) {
        m_media->showItem(item);
        m_content->setCurrentWidget(m_media);
    } else if (item->hasText()) {
        m_media->release();
        m_view->setItems({item}, false);
        m_content->setCurrentWidget(m_view);
    } else {
        m_media->release();
        m_content->setCurrentWidget(m_noContent);
    }
    refreshHeader();
    refreshFooter();
    refreshPanel();
    // the bookmark of this document, if it has one, is the selected one
    m_loading = true;
    int row = -1;
    for (int r = 0; r < m_bookmarks->count(); ++r)
        if (m_bookmarks->documentAt(r) == item)
            row = r;
    m_bookmarks->setCurrentRow(row);
    m_loading = false;
    emit itemShown(item);
}

void QuickRefWindow::refreshHeader()
{
    if (!m_item)
        return;
    const QString title = m_item->title.isEmpty() ? tr("Untitled") : m_item->title;
    setWindowTitle(title + tr(" — Quick Reference"));
    if (!m_title->hasFocus())
        m_title->setText(m_item->title);
    m_icon->setIcon(icons::forItem(m_project, m_item));
}

void QuickRefWindow::refreshFooter()
{
    if (!m_item || !m_project)
        return;
    {
        QSignalBlocker b1(m_label), b2(m_status);
        m_label->clear();
        m_status->clear();
        m_label->addItem(tr("No %1").arg(m_project->labelTitle), -1);
        for (const Label &l : m_project->labels) {
            QPixmap pm(12, 12);
            pm.fill(l.color);
            m_label->addItem(QIcon(pm), l.name, l.id);
        }
        m_status->addItem(tr("No %1").arg(m_project->statusTitle), -1);
        for (const Status &s : m_project->statuses)
            m_status->addItem(s.name, s.id);
        m_label->setCurrentIndex(qMax(0, m_label->findData(m_item->label)));
        m_status->setCurrentIndex(qMax(0, m_status->findData(m_item->status)));
    }
    if (m_item->hasText()) {
        const QLocale loc;
        m_count->setText(AppSettings::instance().countCharacters ? tr("%1 characters").arg(loc.toString(m_item->charCount))
                                                                 : tr("%1 words").arg(loc.toString(m_item->wordCount)));
    } else {
        m_count->clear();
    }
}

void QuickRefWindow::setPanel(Panel panel)
{
    m_panel = panel;
    m_panelBox->setVisible(panel != NoPanel);
    refreshPanel();
}

void QuickRefWindow::refreshPanel(bool later)
{
    if (!m_item || !m_project || m_panel == NoPanel)
        return;
    m_panels->setCurrentIndex(int(m_panel));
    m_loading = true;
    switch (m_panel) {
    case Synopsis:
        m_panelTitle->setText(tr("Synopsis"));
        if (m_synopsis->toPlainText() != m_item->synopsis)
            m_synopsis->setPlainText(m_item->synopsis);
        break;
    case Notes:
        m_panelTitle->setText(tr("Notes"));
        if (m_notes->item() != m_item || m_notes->document() != m_project->docs()->notes(m_item->id))
            m_notes->setProjectDocument(m_project, m_item, m_project->docs()->notes(m_item->id), true);
        break;
    case Keywords:
        m_panelTitle->setText(tr("Keywords"));
        refreshKeywords();
        break;
    case Metadata:
        m_panelTitle->setText(tr("Metadata"));
        if (!later) {
            rebuildMetadata();
        } else if (!m_metaRebuildPending && (!m_metaPage->isAncestorOf(QWidget::focusWidget()) || m_metaShownFor != m_item->id)) {
            // (not while a field of the form is being typed into, and never inside a signal of one of its fields)
            m_metaRebuildPending = true;
            QTimer::singleShot(0, this, [this] {
                m_metaRebuildPending = false;
                if (m_panel == Metadata)
                    rebuildMetadata();
            });
        }
        break;
    case NoPanel: break;
    }
    m_loading = false;
}

void QuickRefWindow::refreshKeywords()
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

void QuickRefWindow::rebuildMetadata()
{
    while (m_metaForm->rowCount())
        m_metaForm->removeRow(0);
    if (!m_item || !m_project)
        return;
    m_metaShownFor = m_item->id;
    const QString dateFormat = QStringLiteral("yyyy-MM-dd HH:mm");
    m_metaForm->addRow(tr("Created:"), new QLabel(m_item->created.toString(dateFormat), m_metaPage));
    m_metaForm->addRow(tr("Modified:"), new QLabel(m_item->modified.toString(dateFormat), m_metaPage));
    auto *include = new QCheckBox(tr("Include in Compile"), m_metaPage);
    include->setObjectName(QStringLiteral("quickRefInclude"));
    include->setChecked(m_item->includeInCompile);
    include->setEnabled(m_project->isInDraft(m_item));
    connect(include, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_item || !m_project || m_item->includeInCompile == on)
            return;
        m_item->includeInCompile = on;
        m_project->notifyItemChanged(m_item, false);
    });
    m_metaForm->addRow(QString(), include);
    auto *section = new QComboBox(m_metaPage);
    section->setObjectName(QStringLiteral("quickRefSection"));
    const SectionType *byStructure = m_project->sectionTypeById(m_project->effectiveSectionType(m_item));
    // (with a type set by hand, effectiveSectionType is that type; name the automatic one only when known)
    section->addItem(m_item->sectionType.isEmpty() && byStructure ? tr("Structure-Based (%1)").arg(byStructure->name)
                                                                 : tr("Structure-Based"),
                     QString());
    for (const SectionType &t : m_project->sectionTypes)
        section->addItem(t.name, t.id);
    section->setCurrentIndex(qMax(0, section->findData(m_item->sectionType)));
    connect(section, qOverload<int>(&QComboBox::activated), this, [this, section](int i) {
        if (!m_item || !m_project)
            return;
        m_item->sectionType = section->itemData(i).toString();
        m_project->notifyItemChanged(m_item, false);
    });
    m_metaForm->addRow(tr("Section type:"), section);

    for (const CustomField &f : m_project->customFields) {
        const QString fid = f.id;
        const QString value = m_item->custom.value(fid, f.defaultValue);
        QWidget *w = nullptr;
        switch (f.type) {
        case CustomFieldType::Checkbox: {
            auto *cb = new QCheckBox(m_metaPage);
            cb->setChecked(value == QLatin1String("1") || value == QLatin1String("true"));
            connect(cb, &QCheckBox::toggled, this, [this, fid](bool on) {
                if (!m_item || !m_project)
                    return;
                m_item->custom[fid] = on ? QStringLiteral("1") : QStringLiteral("0");
                m_project->notifyItemChanged(m_item, false);
            });
            w = cb;
            break;
        }
        case CustomFieldType::List: {
            auto *cb = new QComboBox(m_metaPage);
            cb->addItem(QString());
            cb->addItems(f.options);
            cb->setCurrentText(value);
            connect(cb, &QComboBox::textActivated, this, [this, fid](const QString &t) {
                if (!m_item || !m_project)
                    return;
                m_item->custom[fid] = t;
                m_project->notifyItemChanged(m_item, false);
            });
            w = cb;
            break;
        }
        case CustomFieldType::Date: {
            auto *de = new QDateEdit(m_metaPage);
            de->setCalendarPopup(true);
            de->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
            de->setSpecialValueText(tr("none"));
            de->setMinimumDate(QDate(1, 1, 1));
            const QDate d = QDate::fromString(value, Qt::ISODate);
            de->setDate(d.isValid() ? d : de->minimumDate());
            connect(de, &QDateEdit::dateChanged, this, [this, fid, de](const QDate &nd) {
                if (!m_item || !m_project)
                    return;
                m_item->custom[fid] = nd == de->minimumDate() ? QString() : nd.toString(Qt::ISODate);
                m_project->notifyItemChanged(m_item, false);
            });
            w = de;
            break;
        }
        case CustomFieldType::Text: {
            auto *le = new QLineEdit(value, m_metaPage);
            connect(le, &QLineEdit::editingFinished, this, [this, fid, le] {
                if (!m_item || !m_project || m_item->custom.value(fid) == le->text())
                    return;
                m_item->custom[fid] = le->text();
                m_project->notifyItemChanged(m_item, false);
            });
            w = le;
            break;
        }
        }
        w->setObjectName(QStringLiteral("quickRefCustom_") + fid);
        m_metaForm->addRow(f.title + QLatin1Char(':'), w);
    }
}

// ------------------------------------------------------------------ bookmarks sidebar

bool QuickRefWindow::bookmarksShown() const { return !m_sidebar->isHidden(); }

void QuickRefWindow::setBookmarksShown(bool on)
{
    {
        QSignalBlocker block(m_bookmarkToggle);
        m_bookmarkToggle->setChecked(on);
    }
    if (on == bookmarksShown())
        return;
    // the document keeps its width: the window grows by the sidebar (and shrinks again)
    const int side = 190;
    m_sidebar->setVisible(on);
    if (isVisible() && !isMaximized() && !isFullScreen())
        resize(width() + (on ? side : -qMin(side, m_sidebar->width())), height());
    if (on) {
        const int total = qMax(side + 100, width());
        m_sideSplit->setSizes({side, total - side});
        m_loading = true;
        m_bookmarks->refresh();
        int row = -1;
        for (int r = 0; r < m_bookmarks->count(); ++r)
            if (m_bookmarks->documentAt(r) == m_item)
                row = r;
        m_bookmarks->setCurrentRow(row);
        m_loading = false;
    }
}

QList<BinderItem *> QuickRefWindow::noteFolders(Project *p)
{
    QList<BinderItem *> out;
    if (!p)
        return out;
    for (BinderItem *it : p->allItems())
        if (it->isFolder() && it != p->trash() && !p->isInTrash(it))
            out << it;
    return out;
}

BinderItem *QuickRefWindow::createBookmarkedDocument(BinderItem *folder, const QString &title)
{
    if (!m_project || !folder)
        return nullptr;
    BinderItem *it = m_project->model()->createItem(folder, -1, ItemType::Text, title);
    if (!it)
        return nullptr;
    m_project->projectBookmarks.append({it->title, QStringLiteral("qb://") + it->id});
    m_project->notifyBookmarksChanged();
    showItem(it);
    return it;
}

void QuickRefWindow::addBookmarkedDocument()
{
    if (!m_project)
        return;
    BinderItem *folder = m_project->item(m_project->notesFolderId);
    if (folder && (m_project->isInTrash(folder) || !folder->isFolder()))
        folder = nullptr;
    if (!folder || !m_project->uiState.value(QStringLiteral("notesFolderDontAsk")).toBool()) {
        const QList<BinderItem *> folders = noteFolders(m_project);
        if (folders.isEmpty())
            return;
        QDialog dlg(this);
        dlg.setObjectName(QStringLiteral("newBookmarkDialog"));
        dlg.setWindowTitle(tr("New Bookmarked Document"));
        auto *fl = new QFormLayout(&dlg);
        auto *combo = new QComboBox(&dlg);
        combo->setObjectName(QStringLiteral("newBookmarkFolder"));
        for (BinderItem *f : folders)
            combo->addItem(icons::forItem(m_project, f), QString(f->level() * 2, QLatin1Char(' ')) + f->title, f->id);
        if (folder)
            combo->setCurrentIndex(qMax(0, combo->findData(folder->id)));
        else
            combo->setCurrentIndex(qMax(0, combo->findData(m_project->research()->id)));
        fl->addRow(tr("Create the new document in:"), combo);
        auto *dontAsk = new QCheckBox(tr("Don't ask again"), &dlg);
        dontAsk->setObjectName(QStringLiteral("newBookmarkDontAsk"));
        fl->addRow(QString(), dontAsk);
        auto *hint = new QLabel(tr("The folder can be changed later in Project ▸ Project Settings ▸ Special Folders."), &dlg);
        hint->setEnabled(false);
        hint->setWordWrap(true);
        fl->addRow(hint);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        fl->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted)
            return;
        folder = m_project->item(combo->currentData().toString());
        if (!folder)
            return;
        m_project->notesFolderId = folder->id;
        m_project->uiState[QStringLiteral("notesFolderDontAsk")] = dontAsk->isChecked();
        m_project->markIndexDirty();
    }
    bool ok = false;
    const QString title = QInputDialog::getText(this, tr("New Bookmarked Document"), tr("Name of the new document:"),
                                                QLineEdit::Normal, tr("New Note"), &ok);
    if (!ok)
        return;
    if (createBookmarkedDocument(folder, title.trimmed().isEmpty() ? tr("New Note") : title.trimmed()))
        if (TextEditor *e = m_view->activeEditor())
            e->setFocus();
}

// ------------------------------------------------------------------ header

void QuickRefWindow::headerMenu()
{
    if (!m_item || !m_project)
        return;
    QMenu menu(this);
    menu.addAction(icons::action(QStringLiteral("go-jump")), tr("Reveal in Binder"), this,
                   [this] { emit revealRequested(m_item); });
    menu.addAction(icons::action(QStringLiteral("document-open")), tr("Open in the Editor"), this,
                   [this] { emit openInEditorRequested(m_item); });
    menu.addSeparator();
    const QString target = QStringLiteral("qb://") + m_item->id;
    int at = -1;
    for (int i = 0; i < m_project->projectBookmarks.size(); ++i)
        if (m_project->projectBookmarks[i].target == target)
            at = i;
    if (at < 0)
        menu.addAction(icons::action(QStringLiteral("bookmark-new")), tr("Add to Project Bookmarks"), this, [this, target] {
            m_project->projectBookmarks.append({m_item->title, target});
            m_project->notifyBookmarksChanged();
        });
    else
        menu.addAction(tr("Remove From Project Bookmarks"), this, [this, at] {
            m_project->projectBookmarks.removeAt(at);
            m_project->notifyBookmarksChanged();
        });
    menu.exec(m_icon->mapToGlobal(QPoint(0, m_icon->height())));
}

bool QuickRefWindow::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_header && e->type() == QEvent::ContextMenu) {
        headerMenu();
        return true;
    }
    return QWidget::eventFilter(o, e);
}

void QuickRefWindow::setFloating(bool on)
{
    {
        QSignalBlocker block(m_pin);
        m_pin->setChecked(on);
    }
    if (isFloating() == on)
        return;
    const QRect g = geometry();
    const bool shown = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    setGeometry(g);
    if (shown)
        show();
}

bool QuickRefWindow::isFloating() const { return windowFlags() & Qt::WindowStaysOnTopHint; }

// ------------------------------------------------------------------ state

QVariantMap QuickRefWindow::saveState() const
{
    QVariantMap m;
    if (!m_item)
        return m;
    m[QStringLiteral("item")] = m_item->id;
    const QRect g = geometry();
    m[QStringLiteral("geometry")] = QVariantList{g.x(), g.y(), g.width(), g.height()};
    m[QStringLiteral("panel")] = int(m_panel);
    m[QStringLiteral("bookmarks")] = bookmarksShown();
    m[QStringLiteral("floating")] = isFloating();
    const QList<int> ps = m_panelSplit->sizes();
    if (m_panel != NoPanel && ps.value(0) + ps.value(1) > 0)
        m[QStringLiteral("panelShare")] = double(ps.value(1)) / (ps.value(0) + ps.value(1));
    return m;
}

void QuickRefWindow::restoreState(const QVariantMap &state)
{
    const QVariantList g = state.value(QStringLiteral("geometry")).toList();
    setBookmarksShown(state.value(QStringLiteral("bookmarks")).toBool());
    if (g.size() == 4 && g[2].toInt() > 100 && g[3].toInt() > 100)
        setGeometry(g[0].toInt(), g[1].toInt(), g[2].toInt(), g[3].toInt());
    setPanel(Panel(qBound(0, state.value(QStringLiteral("panel")).toInt(), int(Metadata))));
    if (state.value(QStringLiteral("floating")).toBool())
        setFloating(true);
    const double share = state.value(QStringLiteral("panelShare"), 0.0).toDouble();
    if (m_panel != NoPanel && share > 0.05 && share < 0.95) {
        const int total = qMax(200, height() - 60);
        m_panelSplit->setSizes({int(total * (1 - share)), int(total * share)});
    }
}

} // namespace qb
