#include "ui/OptionsDialog.h"
#include "compile/PluginManager.h"
#include "core/Backup.h"
#include "core/CrashGuard.h"
#include "core/ItemIcons.h"
#include "core/Objects.h"
#include "media/MediaApi.h"
#include "text/DirectFormat.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include "ui/ColorPalette.h"
#include "ui/CrashDialogs.h"
#include "ui/FormatDialogs.h"
#include "ui/KeyboardPage.h"
#include "ui/MediaPlayback.h"
#include "ui/MindMapDoc.h"
#include "ui/SpellChecker.h"
#include "ui/ThemeDialogs.h"
#include "ui/ToolHost.h"
#include "ui/Zoom.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFontDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace qb {

namespace {

void setButtonColor(QPushButton *b, const QColor &c)
{
    QPixmap pm(36, 16);
    pm.fill(c.isValid() ? c : QColor(Qt::white));
    if (!c.isValid()) {
        QPainter p(&pm);
        p.setPen(QPen(QColor(200, 40, 40), 1.5));
        p.drawLine(2, 14, 34, 2);
    }
    b->setIcon(QIcon(pm));
    b->setIconSize(pm.size());
}

// A grey line of explanation under an option.
QLabel *hint(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setEnabled(false);
    return l;
}

// The parts of the window on the Appearance pages (Scrivener's list), with the
// keys of the theme's colors and fonts that belong to each (see ui/Theme.cpp).
struct LookPage {
    const char *name;
    QStringList colors;
    QStringList fonts;
};
enum LookPageId { LGeneral, LBinder, LComposition, LCorkboard, LCards, LInspector, LEditor, LOutliner, LPageView, LCombined, LTargets, LMarks };

QList<LookPage> lookPages()
{
    auto l = [](std::initializer_list<const char *> keys) {
        QStringList out;
        for (const char *k : keys)
            out << QLatin1String(k);
        return out;
    };
    return {{QT_TRANSLATE_NOOP("OptionsDialog", "General Interface"),
             l({"window", "windowText", "base", "alternateBase", "text", "button", "buttonText", "highlight", "highlightedText", "link",
                "toolTipBase", "toolTipText", "mid", "dark", "light", "disabledText"}),
             l({"interface"})},
            {QT_TRANSLATE_NOOP("OptionsDialog", "Binder"), l({"binderBackground", "binderText"}), l({"binder"})},
            {QT_TRANSLATE_NOOP("OptionsDialog", "Composition Mode"), l({"compositionBackground", "compositionPaper", "compositionText"}), {}},
            {QT_TRANSLATE_NOOP("OptionsDialog", "Corkboard"), l({"corkboardBackground", "statusStamp"}), {}},
            {QT_TRANSLATE_NOOP("OptionsDialog", "Index Cards"), l({"cardPaper", "cardTitle", "cardText", "cardTitleLine", "cardLines"}),
             l({"cardTitle", "cardText"})},
            {QT_TRANSLATE_NOOP("OptionsDialog", "Inspector & Notes"),
             l({"inspectorBackground", "inspectorText", "synopsisPaper", "synopsisText", "notesPaper", "notesText"}), l({"synopsis"})},
            {QT_TRANSLATE_NOOP("OptionsDialog", "Main Editor"),
             l({"editorPaper", "editorText", "editorMargin", "headerBar", "headerBarText", "headerBarActive", "headerBarLocked"}),
             l({"headerTitle"})},
            {QT_TRANSLATE_NOOP("OptionsDialog", "Outliner"), l({"outlinerBackground", "outlinerAlternate", "outlinerText"}), l({"outliner"})},
            {QT_TRANSLATE_NOOP("OptionsDialog", "Page View"), l({"editorPageSurround"}), {}},
            {QT_TRANSLATE_NOOP("OptionsDialog", "Combined Text"), {}, l({"combinedTitle"})},
            {QT_TRANSLATE_NOOP("OptionsDialog", "Target Progress Bars"), l({"targetSession", "targetDraft", "targetReached"}), {}},
            {QT_TRANSLATE_NOOP("OptionsDialog", "Textual Marks"), l({"editorLink", "searchHit", "spellingError"}), {}}};
}

const theme::RoleInfo *lookRoleInfo(const QString &key)
{
    for (const theme::RoleInfo &ri : theme::roles())
        if (QLatin1String(ri.key) == key)
            return &ri;
    return nullptr;
}

const theme::FontInfo *lookFontInfo(const QString &key)
{
    for (const theme::FontInfo &fi : theme::fontRoles())
        if (QLatin1String(fi.key) == key)
            return &fi;
    return nullptr;
}

// The separators of Options > General > Separators.
void fillSeparators(QComboBox *c, bool pageBreak)
{
    c->addItem(QObject::tr("Single Return"), QStringLiteral("return"));
    c->addItem(QObject::tr("Empty Line"), QStringLiteral("empty"));
    if (pageBreak)
        c->addItem(QObject::tr("Page Break"), QStringLiteral("page"));
    c->addItem(QObject::tr("None"), QStringLiteral("none"));
    c->addItem(QObject::tr("Custom"), QStringLiteral("custom"));
}

} // namespace

// ==================================================================== frame

OptionsDialog::OptionsDialog(QWidget *parent, int page) : QDialog(parent)
{
    setWindowTitle(tr("Options"));
    resize(1080, 740);
    auto *lay = new QVBoxLayout(this);

    // the icon row: one large icon per section, as at the top of Scrivener's Options
    m_pages = new QListWidget(this);
    m_pages->setObjectName(QStringLiteral("optionPages"));
    m_pages->setViewMode(QListView::IconMode);
    m_pages->setFlow(QListView::LeftToRight);
    m_pages->setWrapping(false);
    m_pages->setMovement(QListView::Static);
    m_pages->setResizeMode(QListView::Adjust);
    m_pages->setIconSize(QSize(32, 32));
    // each item as wide as its grid cell: without that the names are cut to the icon's width
    const QSize cell(98, 62);
    m_pages->setGridSize(cell);
    m_pages->setFixedHeight(cell.height() + 8);
    m_pages->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pages->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pages->setFrameShape(QFrame::NoFrame);
    m_pages->setBackgroundRole(QPalette::Window);
    const QList<std::pair<QString, QString>> sections = {
        {tr("General"), QStringLiteral("configure")},          {tr("Editing"), QStringLiteral("document-edit")},
        {tr("Behaviors"), QStringLiteral("view-choose")},       {tr("Appearance"), QStringLiteral("format-text-color")},
        {tr("Corrections"), QStringLiteral("edit-find-replace")}, {tr("Keyboard"), QStringLiteral("configure-shortcuts")},
        {tr("Sharing"), QStringLiteral("document-export")},     {tr("Backup"), QStringLiteral("backup")},
        {tr("Extras"), QStringLiteral("qb-mindmap")}};
    for (const auto &s : sections) {
        auto *li = new QListWidgetItem(icons::action(s.second), s.first, m_pages);
        li->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        li->setSizeHint(cell - QSize(4, 2));
    }
    m_pages->setMinimumWidth(cell.width() * int(sections.size()) + 4);
    lay->addWidget(m_pages);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(generalSection());
    m_stack->addWidget(editingSection());
    m_stack->addWidget(behaviorsSection());
    m_stack->addWidget(appearanceSection());
    m_stack->addWidget(correctionsSection());
    m_keyboard = new KeyboardPage(this);
    m_stack->addWidget(m_keyboard);
    m_stack->addWidget(sharingSection());
    m_stack->addWidget(backupSection());
    m_stack->addWidget(extrasSection());
    lay->addWidget(m_stack, 1);
    // number fields and lists as wide as their contents (a form would stretch them over the page)
    for (QWidget *w : m_stack->findChildren<QWidget *>()) {
        if (m_keyboard->isAncestorOf(w) || w->objectName() == QLatin1String("themeCombo"))
            continue;
        if (qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QComboBox *>(w))
            w->setSizePolicy(QSizePolicy::Fixed, w->sizePolicy().verticalPolicy());
    }
    connect(m_pages, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);

    // Manage, Defaults on the left; OK, Cancel, Apply on the right (as in Scrivener)
    auto *bottom = new QHBoxLayout;
    auto *manage = new QPushButton(tr("Manage"), this);
    manage->setObjectName(QStringLiteral("optionsManage"));
    m_manageMenu = new QMenu(manage);
    manage->setMenu(m_manageMenu);
    connect(m_manageMenu, &QMenu::aboutToShow, this, &OptionsDialog::fillManageMenu);
    auto *defaults = new QPushButton(tr("Defaults"), this);
    defaults->setObjectName(QStringLiteral("optionsDefaults"));
    defaults->setToolTip(tr("Set every option back to the program's standard"));
    connect(defaults, &QPushButton::clicked, this, [this] { restoreDefaults(true); });
    bottom->addWidget(manage);
    bottom->addWidget(defaults);
    bottom->addStretch(1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    bottom->addWidget(bb);
    lay->addLayout(bottom);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &OptionsDialog::apply);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // the pages of 0.20 -> the sections
    const int map[] = {SGeneral, SAppearance, SEditing, SBehaviors, SKeyboard, SBackup, SExtras};
    const int section = map[qBound(0, page, 6)];
    showSection(section, page == Plugins ? tr("Add-ons and Plugins") : QString());
}

QWidget *OptionsDialog::page(QFormLayout **form)
{
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    f->setRowWrapPolicy(QFormLayout::WrapLongRows);
    *form = f;
    return w;
}

QWidget *OptionsDialog::subPages(int section, const QList<std::pair<QString, QWidget *>> &pages, const QString &listName)
{
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    auto *list = new QListWidget(w);
    list->setObjectName(listName);
    list->setFixedWidth(190);
    auto *stack = new QStackedWidget(w);
    for (const auto &p : pages) {
        list->addItem(p.first);
        auto *scroll = new QScrollArea(stack);
        scroll->setFrameShape(QFrame::StyledPanel);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(p.second);
        stack->addWidget(scroll);
    }
    h->addWidget(list);
    h->addWidget(stack, 1);
    connect(list, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
    list->setCurrentRow(0);
    m_subLists.insert(section, list);
    return w;
}

QWidget *OptionsDialog::tabbed(int section, const QList<std::pair<QString, QWidget *>> &pages, const QString &tabsName)
{
    auto *tabs = new QTabWidget;
    tabs->setObjectName(tabsName);
    for (const auto &p : pages) {
        auto *scroll = new QScrollArea(tabs);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(p.second);
        tabs->addTab(scroll, p.first);
    }
    m_tabs.insert(section, tabs);
    return tabs;
}

void OptionsDialog::showSection(int section, const QString &pageName)
{
    section = qBound(0, section, int(SectionCount) - 1);
    m_pages->setCurrentRow(section);
    m_stack->setCurrentIndex(section);
    if (pageName.isEmpty())
        return;
    if (QListWidget *l = m_subLists.value(section)) {
        const QList<QListWidgetItem *> hit = l->findItems(pageName, Qt::MatchFixedString);
        if (!hit.isEmpty())
            l->setCurrentItem(hit.first());
    } else if (QTabWidget *t = m_tabs.value(section)) {
        for (int i = 0; i < t->count(); ++i)
            if (t->tabText(i).compare(pageName, Qt::CaseInsensitive) == 0)
                t->setCurrentIndex(i);
    }
}

QStringList OptionsDialog::pageNames(int section) const
{
    QStringList out;
    if (QListWidget *l = m_subLists.value(section))
        for (int i = 0; i < l->count(); ++i)
            out << l->item(i)->text();
    else if (QTabWidget *t = m_tabs.value(section))
        for (int i = 0; i < t->count(); ++i)
            out << t->tabText(i);
    return out;
}

// ==================================================================== General

QWidget *OptionsDialog::generalSection()
{
    const AppSettings &s = AppSettings::instance();
    QList<std::pair<QString, QWidget *>> pages;
    QFormLayout *f = nullptr;

    // Startup
    QWidget *startup = page(&f);
    m_openLast = new QCheckBox(tr("Reopen the project that was open when the program was closed"), startup);
    m_openLast->setChecked(s.openLastProject);
    f->addRow(m_openLast);
    m_portal = new QCheckBox(tr("Use the desktop's file dialogs (KDE/GNOME portal; needs a restart)"), startup);
    m_portal->setChecked(s.useDesktopPortal);
    f->addRow(m_portal);
    f->addRow(hint(tr("Without a project to open, the start window offers the templates and the recent projects."), startup));
    pages.append({tr("Startup"), startup});

    // Saving
    QWidget *saving = page(&f);
    m_recentCount = new QSpinBox(saving);
    m_recentCount->setObjectName(QStringLiteral("optRecentCount"));
    m_recentCount->setRange(3, 50);
    m_recentCount->setSuffix(tr(" projects"));
    m_recentCount->setValue(s.recentCount);
    f->addRow(tr("Number of recent projects:"), m_recentCount);
    m_autosave = new QSpinBox(saving);
    m_autosave->setRange(1, 300);
    m_autosave->setSuffix(tr(" seconds of inactivity"));
    m_autosave->setValue(s.autosaveSeconds);
    f->addRow(tr("Save automatically after:"), m_autosave);
    m_snapOnSave = new QCheckBox(tr("Take snapshots of changed text documents on manual save (File > Save)"), saving);
    m_snapOnSave->setObjectName(QStringLiteral("optSnapshotOnSave"));
    m_snapOnSave->setChecked(s.snapshotOnManualSave);
    f->addRow(m_snapOnSave);
    pages.append({tr("Saving"), saving});

    // Author Information
    QWidget *author = page(&f);
    auto line = [&](QLineEdit *&e, const QString &value, const char *name) {
        e = new QLineEdit(value, author);
        e->setObjectName(QLatin1String(name));
        return e;
    };
    auto *nameRow = new QHBoxLayout;
    nameRow->addWidget(line(m_forename, s.forename, "optForename"));
    nameRow->addWidget(line(m_surname, s.surname, "optSurname"));
    m_forename->setPlaceholderText(tr("First name"));
    m_surname->setPlaceholderText(tr("Last name"));
    f->addRow(tr("Name:"), nameRow);
    f->addRow(tr("Street:"), line(m_street, s.street, "optStreet"));
    f->addRow(tr("City:"), line(m_city, s.city, "optCity"));
    f->addRow(tr("State/County:"), line(m_state, s.state, "optState"));
    f->addRow(tr("ZIP/Postcode:"), line(m_zip, s.zip, "optZip"));
    f->addRow(tr("Country:"), line(m_country, s.country, "optCountry"));
    f->addRow(tr("Phone Number:"), line(m_phone, s.phone, "optPhone"));
    f->addRow(tr("Email:"), line(m_email, s.email, "optEmail"));
    f->addRow(hint(tr("Used for title pages and manuscript headers when compiling and printing: <$author>, "
                      "<$firstname>, <$surname>, <$street>, <$city>, <$state>, <$zip>, <$country>, <$phone>, "
                      "<$email>, and <$address> (street, ZIP and city, state, country on lines of their own). "
                      "Help > List of All Placeholders shows them all."),
                   author));
    pages.append({tr("Author Information"), author});

    // the writing tools' sections (the scratch pad's folder and the like): a page each
    toolhost::host().attachTools();
    bool scratch = false;
    for (tools::ToolInterface *t : compile::PluginManager::instance().tools()) {
        if (t->optionsPage() != QLatin1String("General"))
            continue;
        auto *pw = new QWidget;
        tools::OptionsSection *sec = t->createOptions(pw);
        if (!sec || !sec->widget()) {
            delete pw;
            continue;
        }
        m_toolSections.emplace_back(sec);
        auto *v = new QVBoxLayout(pw);
        auto *box = new QGroupBox(t->name(), pw);
        box->setObjectName(QStringLiteral("toolOptions:") + t->id());
        auto *bl = new QVBoxLayout(box);
        bl->addWidget(sec->widget());
        v->addWidget(box);
        v->addStretch(1);
        scratch |= t->id() == QLatin1String("scratch-pad");
        pages.append({t->id() == QLatin1String("scratch-pad") ? tr("Scratch Pad") : t->name(), pw});
    }
    if (!scratch) {
        QWidget *sp = page(&f);
        f->addRow(hint(tr("The Scratch Pad (Window > Scratch Pad) comes with the add-on \"Scratch Pad\"; once it is "
                          "installed (Help > Add-on Manager), its folder and the kind of new notes are set here."),
                       sp));
        pages.append({tr("Scratch Pad"), sp});
    }

    // Shared Templates
    QWidget *shared = page(&f);
    {
        auto *row = new QHBoxLayout;
        m_sharedTemplates = new QLineEdit(s.sharedTemplatesDir, shared);
        m_sharedTemplates->setObjectName(QStringLiteral("optSharedTemplates"));
        m_sharedTemplates->setPlaceholderText(tr("No folder chosen"));
        auto *choose = new QPushButton(tr("Choose..."), shared);
        auto *open = new QPushButton(tr("Open"), shared);
        auto *clear = new QPushButton(tr("Clear"), shared);
        row->addWidget(m_sharedTemplates, 1);
        row->addWidget(choose);
        row->addWidget(open);
        row->addWidget(clear);
        f->addRow(tr("Shared project templates folder:"), row);
        f->addRow(hint(tr("The project templates in this folder (for example a folder synchronized between your computers) "
                          "appear in File > New Project next to your own. Copy a template there from your templates folder "
                          "to share it."),
                       shared));
        connect(choose, &QPushButton::clicked, this, [this] {
            const QString d = QFileDialog::getExistingDirectory(this, tr("Shared Templates Folder"), m_sharedTemplates->text());
            if (!d.isEmpty())
                m_sharedTemplates->setText(d);
        });
        connect(open, &QPushButton::clicked, this, [this] {
            if (!m_sharedTemplates->text().isEmpty()) {
                QDir().mkpath(m_sharedTemplates->text());
                QDesktopServices::openUrl(QUrl::fromLocalFile(m_sharedTemplates->text()));
            }
        });
        connect(clear, &QPushButton::clicked, m_sharedTemplates, &QLineEdit::clear);
    }
    pages.append({tr("Shared Templates"), shared});

    // Citations
    QWidget *cites = page(&f);
    {
        auto *row = new QHBoxLayout;
        m_citations = new QLineEdit(s.citationsApp, cites);
        m_citations->setObjectName(QStringLiteral("optCitations"));
        m_citations->setPlaceholderText(tr("None (for example zotero or jabref)"));
        auto *choose = new QPushButton(tr("Choose..."), cites);
        auto *reset = new QPushButton(tr("Reset"), cites);
        row->addWidget(m_citations, 1);
        row->addWidget(choose);
        row->addWidget(reset);
        f->addRow(tr("Bibliography/Citations manager:"), row);
        f->addRow(hint(tr("Format > Bibliography/Citations... starts this program (a program name or a full command), so "
                          "that you can copy a citation from it into your text."),
                       cites));
        connect(choose, &QPushButton::clicked, this, [this] {
            const QString file = QFileDialog::getOpenFileName(this, tr("Bibliography/Citations Manager"), QStringLiteral("/usr/bin"));
            if (!file.isEmpty())
                m_citations->setText(file);
        });
        connect(reset, &QPushButton::clicked, m_citations, &QLineEdit::clear);
    }
    pages.append({tr("Citations"), cites});

    // Separators
    QWidget *seps = page(&f);
    {
        auto row = [&](QComboBox *&combo, QLineEdit *&custom, const QString &value, const QString &customText, bool pageBreak,
                       const char *name) {
            auto *h = new QHBoxLayout;
            combo = new QComboBox(seps);
            combo->setObjectName(QLatin1String(name));
            fillSeparators(combo, pageBreak);
            combo->setCurrentIndex(qMax(0, combo->findData(value)));
            custom = new QLineEdit(customText, seps);
            custom->setObjectName(QLatin1String(name) + QStringLiteral("Custom"));
            custom->setEnabled(value == QLatin1String("custom"));
            h->addWidget(combo);
            h->addWidget(custom, 1);
            connect(combo, &QComboBox::currentIndexChanged, custom,
                    [combo, custom] { custom->setEnabled(combo->currentData().toString() == QLatin1String("custom")); });
            return h;
        };
        f->addRow(tr("Merged documents:"), row(m_sepMerge, m_sepMergeCustom, s.sepMerge, s.sepMergeCustom, true, "optSepMerge"));
        f->addRow(tr("Scratch pad notes added to a document:"),
                  row(m_sepScratch, m_sepScratchCustom, s.sepScratch, s.sepScratchCustom, false, "optSepScratch"));
        f->addRow(hint(tr("What goes between the texts when documents are merged (Documents > Merge) and when a scratch pad "
                          "note is added to the end of a text document. A custom separator is a line of its own."),
                       seps));
    }
    pages.append({tr("Separators"), seps});

    // Automatic Quit
    QWidget *quit = page(&f);
    m_autoQuit = new QCheckBox(tr("Automatically quit after a period of inactivity"), quit);
    m_autoQuit->setObjectName(QStringLiteral("optAutoQuit"));
    m_autoQuit->setChecked(s.autoQuit);
    f->addRow(m_autoQuit);
    m_autoQuitMinutes = new QSpinBox(quit);
    m_autoQuitMinutes->setObjectName(QStringLiteral("optAutoQuitMinutes"));
    m_autoQuitMinutes->setRange(1, 600);
    m_autoQuitMinutes->setSuffix(tr(" minute(s)").replace(QStringLiteral("(s)"), QStringLiteral("s")));
    m_autoQuitMinutes->setValue(s.autoQuitMinutes);
    m_autoQuitMinutes->setEnabled(s.autoQuit);
    connect(m_autoQuit, &QCheckBox::toggled, m_autoQuitMinutes, &QWidget::setEnabled);
    f->addRow(tr("Quit after inactive for:"), m_autoQuitMinutes);
    f->addRow(hint(tr("The project is saved and backed up as on a normal exit."), quit));
    pages.append({tr("Automatic Quit"), quit});

    // Warnings
    QWidget *warn = page(&f);
    {
        auto *log = new QPushButton(tr("Show Internal Log Console"), warn);
        log->setObjectName(QStringLiteral("optShowLog"));
        connect(log, &QPushButton::clicked, this, [this] { showLogConsole(this); });
        f->addRow(log);
        f->addRow(hint(tr("The messages of the program and of Qt while it runs (the same as the last lines of a crash report)."), warn));
        auto *reset = new QPushButton(tr("Reset All Warnings"), warn);
        reset->setObjectName(QStringLiteral("optResetWarnings"));
        connect(reset, &QPushButton::clicked, this, [this, reset] {
            // the questions answered with "Don't ask again" come back
            QSettings st;
            st.remove(QStringLiteral("dontAsk"));
            emit AppSettings::instance().warningsReset();
            reset->setText(tr("Warnings Reset"));
            reset->setEnabled(false);
        });
        f->addRow(reset);
        f->addRow(hint(tr("Questions you answered with \"Don't ask again\" are asked again (for example the folder of new "
                          "bookmarked documents)."),
                       warn));
    }
    pages.append({tr("Warnings"), warn});
    return subPages(SGeneral, pages, QStringLiteral("optGeneralPages"));
}

// ==================================================================== Editing

QWidget *OptionsDialog::editingSection()
{
    const AppSettings &s = AppSettings::instance();
    QFormLayout *f = nullptr;

    // ---- Options tab
    QWidget *opts = page(&f);
    m_reloadZoom = new QCheckBox(tr("Reload the main editors' last selected zoom on startup"), opts);
    m_reloadZoom->setObjectName(QStringLiteral("optReloadZoom"));
    m_reloadZoom->setChecked(s.reloadZoom);
    f->addRow(m_reloadZoom);
    {
        auto *row = new QHBoxLayout;
        m_zoomArea = new QComboBox(opts);
        m_zoomArea->setObjectName(QStringLiteral("optZoomArea"));
        for (const QString &k : zoom::areas()) {
            m_zoomArea->addItem(zoom::areaName(k), k);
            m_zoomDefaults.insert(k, k == QLatin1String(zoom::kComposition) ? s.compositionZoomPercent / 100.0 : zoom::defaultFactor(k));
        }
        m_zoomPercent = new QSpinBox(opts);
        m_zoomPercent->setObjectName(QStringLiteral("optZoomPercent"));
        m_zoomPercent->setRange(50, 300);
        m_zoomPercent->setSingleStep(10);
        m_zoomPercent->setSuffix(QStringLiteral("%"));
        auto show = [this] {
            const QSignalBlocker b(m_zoomPercent);
            m_zoomPercent->setValue(qRound(m_zoomDefaults.value(m_zoomArea->currentData().toString(), 1.0) * 100));
        };
        show();
        connect(m_zoomArea, &QComboBox::currentIndexChanged, this, show);
        connect(m_zoomPercent, &QSpinBox::valueChanged, this,
                [this](int v) { m_zoomDefaults[m_zoomArea->currentData().toString()] = v / 100.0; });
        row->addWidget(m_zoomArea, 1);
        row->addWidget(m_zoomPercent);
        f->addRow(tr("Default text zoom:"), row);
        f->addRow(hint(tr("Ctrl + mouse wheel changes the zoom of the area under the mouse; View > Reset Zoom (Ctrl+0) "
                          "brings the editors back to this."),
                       opts));
    }
    {
        auto *row = new QHBoxLayout;
        m_rulerUnit = new QComboBox(opts);
        m_rulerUnit->addItem(tr("As in the system settings"), QString());
        m_rulerUnit->addItem(tr("Centimeters"), QStringLiteral("cm"));
        m_rulerUnit->addItem(tr("Inches"), QStringLiteral("in"));
        m_rulerUnit->setCurrentIndex(qMax(0, m_rulerUnit->findData(s.rulerUnit)));
        m_rulerSnap = new QDoubleSpinBox(opts);
        m_rulerSnap->setObjectName(QStringLiteral("optRulerSnap"));
        m_rulerSnap->setRange(0, 20);
        m_rulerSnap->setDecimals(1);
        m_rulerSnap->setSingleStep(0.5);
        m_rulerSnap->setSuffix(tr(" mm"));
        m_rulerSnap->setSpecialValueText(tr("Standard"));
        m_rulerSnap->setValue(s.rulerSnapMm);
        row->addWidget(m_rulerUnit, 1);
        row->addWidget(new QLabel(tr("Ruler snap every:"), opts));
        row->addWidget(m_rulerSnap);
        f->addRow(tr("Ruler units:"), row);
    }
    m_overtype = new QCheckBox(tr("Use the Insert key to switch overtype mode on and off"), opts);
    m_overtype->setObjectName(QStringLiteral("optOvertype"));
    m_overtype->setChecked(s.overtypeKey);
    f->addRow(m_overtype);
    {
        auto *row = new QHBoxLayout;
        m_dateCustom = new QCheckBox(tr("Insert the date using a custom format:"), opts);
        m_dateCustom->setObjectName(QStringLiteral("optDateCustom"));
        m_dateCustom->setChecked(!s.customDateFormat.isEmpty());
        m_dateFormat = new QLineEdit(s.customDateFormat.isEmpty() ? QStringLiteral("yyyy-MM-dd 'at' HH:mm:ss") : s.customDateFormat, opts);
        m_dateFormat->setObjectName(QStringLiteral("optDateFormat"));
        m_dateFormat->setEnabled(m_dateCustom->isChecked());
        auto *preview = new QLabel(opts);
        preview->setEnabled(false);
        auto refresh = [this, preview] { preview->setText(QDateTime::currentDateTime().toString(m_dateFormat->text())); };
        refresh();
        connect(m_dateFormat, &QLineEdit::textChanged, preview, refresh);
        connect(m_dateCustom, &QCheckBox::toggled, m_dateFormat, &QWidget::setEnabled);
        row->addWidget(m_dateCustom);
        row->addWidget(m_dateFormat, 1);
        f->addRow(row);
        f->addRow(tr("Insert > Current Date gives:"), preview);
    }
    m_liveCounts = new QComboBox(opts);
    m_liveCounts->setObjectName(QStringLiteral("optLiveCounts"));
    m_liveCounts->addItem(tr("Words"), false);
    m_liveCounts->addItem(tr("Characters"), true);
    m_liveCounts->setCurrentIndex(s.countCharacters ? 1 : 0);
    f->addRow(tr("Live counts show:"), m_liveCounts);
    {
        auto *box = new QGroupBox(tr("Footnotes and Comments"), opts);
        auto *bl = new QVBoxLayout(box);
        m_noColorAnnotations = new QCheckBox(tr("Do not color the text of inline annotations"), box);
        m_noColorAnnotations->setObjectName(QStringLiteral("optNoColorAnnotations"));
        m_noColorAnnotations->setChecked(s.noColorInlineAnnotations);
        m_notesBeforePunct = new QCheckBox(tr("Terminate footnotes and comments before punctuation"), box);
        m_notesBeforePunct->setObjectName(QStringLiteral("optNotesBeforePunct"));
        m_notesBeforePunct->setChecked(s.notesBeforePunctuation);
        bl->addWidget(m_noColorAnnotations);
        bl->addWidget(m_notesBeforePunct);
        bl->addWidget(hint(tr("A comment or footnote made on a selection that ends with a period, comma, question mark ... "
                              "stops before it, so the mark stands before the punctuation."),
                           box));
        f->addRow(box);
    }
    {
        auto *row = new QHBoxLayout;
        m_blockCursor = new QCheckBox(tr("Use a block insertion point, width:"), opts);
        m_blockCursor->setObjectName(QStringLiteral("optBlockCursor"));
        m_blockCursor->setChecked(s.blockCursor);
        m_blockWidth = new QSpinBox(opts);
        m_blockWidth->setRange(2, 20);
        m_blockWidth->setSuffix(tr(" pixels"));
        m_blockWidth->setValue(s.blockCursorWidth);
        m_blockWidth->setEnabled(s.blockCursor);
        connect(m_blockCursor, &QCheckBox::toggled, m_blockWidth, &QWidget::setEnabled);
        row->addWidget(m_blockCursor);
        row->addWidget(m_blockWidth);
        row->addStretch(1);
        f->addRow(row);
    }

    // ---- Formatting tab (formatting of new projects, since 0.3)
    QWidget *fmt = page(&f);
    {
        auto *box = new QGroupBox(tr("Main text style for new projects"), fmt);
        box->setObjectName(QStringLiteral("newFormatting"));
        auto *bv = new QVBoxLayout(box);
        auto *bf = new QFormLayout;
        bv->addLayout(bf);
        m_newAutoFont = new QCheckBox(tr("Automatic serif font"), box);
        m_newAutoFont->setChecked(s.newFontFamily.isEmpty());
        m_newFont = new QFontComboBox(box);
        if (!s.newFontFamily.isEmpty())
            m_newFont->setCurrentFont(QFont(s.newFontFamily));
        m_newFont->setEnabled(!s.newFontFamily.isEmpty());
        connect(m_newAutoFont, &QCheckBox::toggled, m_newFont, &QWidget::setDisabled);
        m_newSize = new QDoubleSpinBox(box);
        m_newSize->setRange(6, 48);
        m_newSize->setDecimals(1);
        m_newSize->setSuffix(tr(" pt"));
        m_newSize->setValue(s.newFontSize);
        m_newSpacing = new QDoubleSpinBox(box);
        m_newSpacing->setRange(0.8, 3.0);
        m_newSpacing->setSingleStep(0.1);
        m_newSpacing->setValue(s.newLineSpacing);
        m_newIndent = new QDoubleSpinBox(box);
        m_newIndent->setDecimals(2);
        m_newIndent->setRange(0, units::fromPx(400));
        m_newIndent->setSingleStep(units::metric() ? 0.25 : 0.125);
        m_newIndent->setSuffix(units::suffix());
        m_newIndent->setValue(units::fromPx(s.newFirstLineIndent));
        m_newParaSpacing = new QDoubleSpinBox(box);
        m_newParaSpacing->setDecimals(1);
        m_newParaSpacing->setRange(0, 72);
        m_newParaSpacing->setSuffix(tr(" pt"));
        m_newParaSpacing->setValue(direct::pxToPt(s.newParagraphSpacing));
        m_newJustify = new QCheckBox(tr("Justify text"), box);
        m_newJustify->setChecked(s.newJustify);
        auto *useCurrent = new QPushButton(tr("Use Formatting in Current Editor"), box);
        useCurrent->setObjectName(QStringLiteral("useCurrentFormatting"));
        qb::Formatting probe;
        useCurrent->setEnabled(currentFormatting::get(&probe));
        connect(useCurrent, &QPushButton::clicked, this, [this] {
            qb::Formatting cur;
            if (!currentFormatting::get(&cur))
                return;
            m_newAutoFont->setChecked(cur.fontFamily.isEmpty());
            if (!cur.fontFamily.isEmpty())
                m_newFont->setCurrentFont(QFont(cur.fontFamily));
            m_newSize->setValue(cur.fontSize);
            m_newSpacing->setValue(cur.lineSpacing);
            m_newIndent->setValue(units::fromPx(cur.firstLineIndent));
            m_newParaSpacing->setValue(direct::pxToPt(cur.paragraphSpacing));
            m_newJustify->setChecked(cur.justify);
        });
        bf->addRow(QString(), m_newAutoFont);
        bf->addRow(tr("Font:"), m_newFont);
        bf->addRow(tr("Size:"), m_newSize);
        bf->addRow(tr("Line spacing:"), m_newSpacing);
        bf->addRow(tr("First line indent:"), m_newIndent);
        bf->addRow(tr("Space after paragraphs:"), m_newParaSpacing);
        bf->addRow(QString(), m_newJustify);
        bf->addRow(QString(), useCurrent);
        auto *row = new QHBoxLayout;
        auto *state = new QLabel(s.newStylesXml.isEmpty() ? tr("Built-in styles") : tr("Styles saved from a project"), box);
        state->setObjectName(QStringLiteral("newStylesState"));
        auto *builtin = new QPushButton(tr("Use Built-in Styles"), box);
        builtin->setObjectName(QStringLiteral("newStylesBuiltin"));
        builtin->setEnabled(!s.newStylesXml.isEmpty());
        builtin->setToolTip(tr("To use the styles of a project in new projects, open it and choose "
                               "Format > Style > Use These Styles in New Projects."));
        connect(builtin, &QPushButton::clicked, this, [this, state, builtin] {
            m_clearNewStyles = true;
            state->setText(tr("Built-in styles"));
            builtin->setEnabled(false);
        });
        row->addWidget(state);
        row->addWidget(builtin);
        row->addStretch(1);
        bf->addRow(tr("Styles:"), row);
        bv->addWidget(hint(tr("Existing projects keep their formatting; change it under Project > Project Settings > Formatting."), box));
        f->addRow(box);
    }

    // ---- Revisions tab
    QWidget *rev = page(&f);
    {
        m_revColors = s.revisionColors.size() == 5 ? s.revisionColors : text::standardRevisionColors();
        const QStringList names = {tr("First revision:"), tr("Second revision:"), tr("Third revision:"), tr("Fourth revision:"),
                                   tr("Fifth revision:")};
        for (int i = 0; i < 5; ++i) {
            auto *b = new QPushButton(rev);
            b->setObjectName(QStringLiteral("optRevision%1").arg(i + 1));
            setButtonColor(b, m_revColors[i]);
            m_revButtons << b;
            connect(b, &QPushButton::clicked, this, [this, b, i] {
                QMenu menu;
                palette::fillMenu(&menu, nullptr, m_revColors[i], [this, b, i](const QColor &c) {
                    if (!c.isValid())
                        return;
                    m_revColors[i] = c;
                    setButtonColor(b, c);
                }, this, QString(), tr("Revision Color"));
                menu.exec(b->mapToGlobal(QPoint(0, b->height())));
            });
            f->addRow(names[i], b);
        }
        auto *def = new QPushButton(tr("Load Default Revision Colors"), rev);
        def->setObjectName(QStringLiteral("optRevisionDefaults"));
        connect(def, &QPushButton::clicked, this, [this] {
            m_revColors = text::standardRevisionColors();
            for (int i = 0; i < 5; ++i)
                setButtonColor(m_revButtons[i], m_revColors[i]);
        });
        f->addRow(def);
        f->addRow(hint(tr("Format > Revision Mode marks new text in the color of the revision. Text marked before keeps "
                          "its color in RTF projects; Markdown projects show it in the new color."),
                       rev));
    }
    return tabbed(SEditing, {{tr("Options"), opts}, {tr("Formatting"), fmt}, {tr("Revisions"), rev}}, QStringLiteral("optEditingTabs"));
}

// ==================================================================== Behaviors

QWidget *OptionsDialog::behaviorsSection()
{
    const AppSettings &s = AppSettings::instance();
    QList<std::pair<QString, QWidget *>> pages;
    QFormLayout *f = nullptr;

    // Composition Mode
    QWidget *comp = page(&f);
    m_compScreen = new QComboBox(comp);
    m_compScreen->setObjectName(QStringLiteral("optCompScreen"));
    m_compScreen->addItem(tr("Current Screen"), QStringLiteral("current"));
    m_compScreen->addItem(tr("Primary Screen"), QStringLiteral("primary"));
    for (QScreen *sc : QGuiApplication::screens())
        m_compScreen->addItem(sc->name(), sc->name());
    if (m_compScreen->findData(s.compositionScreen) < 0)
        m_compScreen->addItem(s.compositionScreen, s.compositionScreen); // a screen not connected now
    m_compScreen->setCurrentIndex(qMax(0, m_compScreen->findData(s.compositionScreen)));
    f->addRow(tr("Open composition mode on:"), m_compScreen);
    m_compHideMain = new QCheckBox(tr("Hide the main window in composition mode"), comp);
    m_compHideMain->setObjectName(QStringLiteral("optCompHideMain"));
    m_compHideMain->setChecked(s.compositionHideMain);
    f->addRow(m_compHideMain);
    m_compEsc = new QCheckBox(tr("The Escape key closes composition mode"), comp);
    m_compEsc->setObjectName(QStringLiteral("optCompEsc"));
    m_compEsc->setChecked(s.compositionEscCloses);
    f->addRow(m_compEsc);
    f->addRow(hint(tr("F11 and the Exit button in the bar at the bottom always close it."), comp));
    pages.append({tr("Composition Mode"), comp});

    // Document Links
    QWidget *links = page(&f);
    m_backLinks = new QCheckBox(tr("Document links create back-link bookmarks"), links);
    m_backLinks->setObjectName(QStringLiteral("optBackLinks"));
    m_backLinks->setChecked(s.backLinks);
    f->addRow(m_backLinks);
    f->addRow(hint(tr("A link to another document adds a bookmark to that document that leads back to the document "
                      "the link is in (Inspector > Bookmarks)."),
                   links));
    m_linkOpens = new QComboBox(links);
    m_linkOpens->setObjectName(QStringLiteral("linkOpens"));
    m_linkOpens->addItem(tr("In the other editor (split if needed)"), QStringLiteral("other"));
    m_linkOpens->addItem(tr("In the current editor"), QStringLiteral("current"));
    m_linkOpens->addItem(tr("In the copyholder of the current editor"), QStringLiteral("copyholder"));
    m_linkOpens->addItem(tr("In a Quick Reference panel"), QStringLiteral("window"));
    m_linkOpens->setCurrentIndex(qMax(0, m_linkOpens->findData(s.linkOpens)));
    f->addRow(tr("Open clicked document links:"), m_linkOpens);
    pages.append({tr("Document Links"), links});

    // Double-Clicking
    QWidget *dbl = page(&f);
    m_corkDoubleClick = new QCheckBox(tr("Double-clicking the corkboard background creates a new card"), dbl);
    m_corkDoubleClick->setObjectName(QStringLiteral("optCorkDoubleClick"));
    m_corkDoubleClick->setChecked(s.corkDoubleClickNew);
    f->addRow(m_corkDoubleClick);
    f->addRow(hint(tr("On a freeform corkboard the new card lies where you double-clicked."), dbl));
    pages.append({tr("Double-Clicking"), dbl});

    // Dragging & Dropping
    QWidget *drag = page(&f);
    m_dropOnText = new QCheckBox(tr("Documents can be dropped onto text documents (they become subdocuments)"), drag);
    m_dropOnText->setObjectName(QStringLiteral("dropOnText"));
    m_dropOnText->setChecked(s.dropOnTextDocuments);
    f->addRow(m_dropOnText);
    f->addRow(hint(tr("Switched off, documents can still be dropped between other documents and onto folders."), drag));
    m_dragFormat = new QComboBox(drag);
    m_dragFormat->setObjectName(QStringLiteral("dragFormat"));
    m_dragFormat->addItem(tr("Rich Text (.rtf)"), QStringLiteral("rtf"));
    m_dragFormat->addItem(tr("Word (.docx)"), QStringLiteral("docx"));
    m_dragFormat->addItem(tr("Markdown (.md)"), QStringLiteral("md"));
    m_dragFormat->addItem(tr("Plain text (.txt)"), QStringLiteral("txt"));
    m_dragFormat->setCurrentIndex(qMax(0, m_dragFormat->findData(s.dragExportFormat)));
    f->addRow(tr("Text dragged to the file manager or the desktop is saved as:"), m_dragFormat);
    m_dragChildren = new QCheckBox(tr("With the subdocuments (folders become folders)"), drag);
    m_dragChildren->setChecked(s.dragExportChildren);
    f->addRow(m_dragChildren);
    f->addRow(hint(tr("Inside the program: drag a document onto text to link it, with Alt to insert its text, onto an "
                      "editor's header bar to open it there (with Alt: in the copyholder), onto the Quick Reference "
                      "or Bookmarks button of the toolbar, or onto a collection."),
                   drag));
    pages.append({tr("Dragging & Dropping"), drag});

    // Folders & Files
    QWidget *folders = page(&f);
    m_folderCork = new QCheckBox(tr("Folders open in corkboard view by default"), folders);
    m_folderCork->setChecked(s.startWithCorkboard);
    f->addRow(m_folderCork);
    m_subdocsAsFolders = new QCheckBox(tr("Treat all documents with subdocuments as folders"), folders);
    m_subdocsAsFolders->setObjectName(QStringLiteral("optSubdocsAsFolders"));
    m_subdocsAsFolders->setChecked(s.subdocsAsFolders);
    f->addRow(m_subdocsAsFolders);
    f->addRow(hint(tr("A text document with documents inside it then opens like a folder (corkboard, outliner or "
                      "combined text), not as its own text."),
                   folders));
    pages.append({tr("Folders & Files"), folders});

    // Playback (add-on "Media Playback")
    QWidget *pb = page(&f);
    pb->setObjectName(QStringLiteral("playbackGroup"));
    m_mediaRewind = new QDoubleSpinBox(pb);
    m_mediaRewind->setObjectName(QStringLiteral("mediaRewindSeconds"));
    m_mediaRewind->setRange(0.5, 30);
    m_mediaRewind->setSingleStep(0.5);
    m_mediaRewind->setDecimals(1);
    m_mediaRewind->setSuffix(tr(" s"));
    m_mediaRewind->setValue(s.mediaRewindSeconds);
    f->addRow(tr("Rewind when paused by:"), m_mediaRewind);
    m_mediaSkip = new QSpinBox(pb);
    m_mediaSkip->setObjectName(QStringLiteral("mediaSkipSeconds"));
    m_mediaSkip->setRange(1, 300);
    m_mediaSkip->setSuffix(tr(" s"));
    m_mediaSkip->setValue(s.mediaSkipSeconds);
    f->addRow(tr("Skip back / forward by:"), m_mediaSkip);
    m_mediaRemember = new QCheckBox(tr("Audio and video files continue where they were paused"), pb);
    m_mediaRemember->setObjectName(QStringLiteral("mediaRemember"));
    m_mediaRemember->setChecked(s.mediaRememberPosition);
    f->addRow(m_mediaRemember);
    m_mediaBackend = new QComboBox(pb);
    m_mediaBackend->setObjectName(QStringLiteral("mediaBackend"));
    auto *engineNote = hint(QString(), pb);
    if (media::EngineInterface *e = playback::engine()) {
        for (const media::Backend &b : e->backends()) {
            m_mediaBackend->addItem(b.name, b.id);
            const int i = m_mediaBackend->count() - 1;
            m_mediaBackend->setItemData(i, b.note, Qt::ToolTipRole);
            if (!b.available)
                m_mediaBackend->setItemData(i, false, Qt::UserRole - 1); // greyed out
        }
        m_mediaBackend->setCurrentIndex(qMax(0, m_mediaBackend->findData(s.mediaBackend)));
        auto showNote = [this, engineNote, e] {
            QString note;
            for (const media::Backend &b : e->backends())
                if (b.id == m_mediaBackend->currentData().toString())
                    note = b.note;
            const QString running = e->isStarted() ? e->startedBackend() : QString();
            if (!running.isEmpty() && running != m_mediaBackend->currentData().toString())
                note += QLatin1Char(' ') + tr("Takes effect when the program is started again.");
            engineNote->setText(playback::status() + QStringLiteral("\n") + note);
        };
        connect(m_mediaBackend, &QComboBox::currentIndexChanged, this, showNote);
        showNote();
    } else {
        m_mediaBackend->addItem(tr("Not installed"));
        m_mediaBackend->setEnabled(false);
        engineNote->setText(playback::status());
    }
    f->addRow(tr("Decoding:"), m_mediaBackend);
    f->addRow(engineNote);
    f->addRow(hint(tr("\"Pause and rewind\" must be switched on in the footer bar of the player."), pb));
    pages.append({tr("Playback"), pb});

    // Quick Reference
    QWidget *qr = page(&f);
    m_reopenQuickRefs = new QCheckBox(tr("Reopen the Quick Reference panels of a project when it is opened"), qr);
    m_reopenQuickRefs->setChecked(s.reopenQuickRefs);
    f->addRow(m_reopenQuickRefs);
    pages.append({tr("Quick Reference"), qr});

    // Return Key
    QWidget *ret = page(&f);
    m_returnEndsSynopsis = new QCheckBox(tr("Return ends editing the synopsis in corkboard and outliner views"), ret);
    m_returnEndsSynopsis->setObjectName(QStringLiteral("optReturnEndsSynopsis"));
    m_returnEndsSynopsis->setChecked(s.returnEndsSynopsis);
    f->addRow(m_returnEndsSynopsis);
    f->addRow(hint(tr("Shift+Return then starts a new line in the synopsis."), ret));
    m_returnCreates = new QCheckBox(tr("Return creates a new item in corkboard and outliner views"), ret);
    m_returnCreates->setObjectName(QStringLiteral("optReturnCreates"));
    m_returnCreates->setChecked(s.returnCreatesItem);
    f->addRow(m_returnCreates);
    f->addRow(hint(tr("Off, Return on a selected card or row edits its title (in the binder Return always makes a new item)."), ret));
    pages.append({tr("Return Key"), ret});

    // Snapshots
    QWidget *snap = page(&f);
    m_snapNotify = new QCheckBox(tr("Show a notification when snapshots are taken"), snap);
    m_snapNotify->setObjectName(QStringLiteral("optSnapNotify"));
    m_snapNotify->setChecked(s.snapshotNotify);
    f->addRow(m_snapNotify);
    f->addRow(hint(tr("A message in the status bar names the documents."), snap));
    pages.append({tr("Snapshots"), snap});
    return subPages(SBehaviors, pages, QStringLiteral("optBehaviorPages"));
}

// ==================================================================== Appearance

QWidget *OptionsDialog::appearanceSection()
{
    const AppSettings &s = AppSettings::instance();
    m_ownColors = s.ownColors;
    m_ownFonts = s.ownFonts;
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);

    // theme, own changes, preview (the themes are also in Manage below, as in Scrivener)
    auto *top = new QHBoxLayout;
    auto *left = new QVBoxLayout;
    auto *themeRow = new QHBoxLayout;
    themeRow->addWidget(new QLabel(tr("Theme:"), w));
    m_theme = new QComboBox(w);
    m_theme->setObjectName(QStringLiteral("themeCombo"));
    themeRow->addWidget(m_theme, 1);
    left->addLayout(themeRow);
    auto *buttons = new QHBoxLayout;
    auto *save = new QPushButton(tr("Save as Theme..."), w);
    save->setObjectName(QStringLiteral("saveTheme"));
    save->setToolTip(tr("Save the theme with your changes as a new theme of your own"));
    auto *manage = new QPushButton(tr("Manage Themes..."), w);
    manage->setObjectName(QStringLiteral("manageThemes"));
    buttons->addWidget(save);
    buttons->addWidget(manage);
    buttons->addStretch(1);
    left->addLayout(buttons);
    m_ownInfo = new QLabel(w);
    m_ownInfo->setObjectName(QStringLiteral("ownInfo"));
    m_ownInfo->setWordWrap(true);
    left->addWidget(m_ownInfo);
    m_resetLook = new QPushButton(tr("Reset Colors and Fonts to the Theme"), w);
    m_resetLook->setObjectName(QStringLiteral("resetLook"));
    left->addWidget(m_resetLook, 0, Qt::AlignLeft);
    left->addStretch(1);
    top->addLayout(left, 1);
    m_preview = new ThemePreview(w);
    m_preview->setFixedSize(300, 150);
    top->addWidget(m_preview);
    v->addLayout(top);

    // the parts of the window, in Scrivener's list
    auto *areas = new QHBoxLayout;
    m_areas = new QListWidget(w);
    m_areas->setObjectName(QStringLiteral("appearanceAreas"));
    m_areas->setFixedWidth(190);
    m_areaStack = new QStackedWidget(w);
    const QList<LookPage> pages = lookPages();
    for (int i = 0; i < pages.size(); ++i) {
        m_areas->addItem(tr(pages[i].name));
        auto *scroll = new QScrollArea(m_areaStack);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(appearancePage(i));
        m_areaStack->addWidget(scroll);
    }
    areas->addWidget(m_areas);
    areas->addWidget(m_areaStack, 1);
    v->addLayout(areas, 1);
    connect(m_areas, &QListWidget::currentRowChanged, m_areaStack, &QStackedWidget::setCurrentIndex);
    m_areas->setCurrentRow(0);
    m_subLists.insert(SAppearance, m_areas);

    reloadThemes(s.theme);
    connect(m_theme, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refreshLook(); });
    connect(m_resetLook, &QPushButton::clicked, this, &OptionsDialog::resetLook);
    connect(save, &QPushButton::clicked, this, [this] {
        const QString name = themeui::askName(this, tr("Save as Theme"), tr("%1 (My Version)").arg(m_theme->currentText()));
        if (name.isEmpty())
            return;
        QString error;
        if (saveLookAsTheme(name, &error).isEmpty())
            QMessageBox::warning(this, tr("Save as Theme"), error);
    });
    connect(manage, &QPushButton::clicked, this, [this] {
        const QString before = AppSettings::instance().theme;
        ThemeManagerDialog dlg(this);
        dlg.select(m_theme->currentData().toString());
        dlg.exec();
        // "Use This Theme" in that window takes effect at once
        const QString now = AppSettings::instance().theme;
        if (now != before) {
            m_ownColors = AppSettings::instance().ownColors;
            m_ownFonts = AppSettings::instance().ownFonts;
        }
        reloadThemes(now != before ? now : m_theme->currentData().toString());
    });
    refreshLook();
    return w;
}

// One part of the window: its options, fonts and colors.
QWidget *OptionsDialog::appearancePage(int which)
{
    const AppSettings &s = AppSettings::instance();
    const LookPage lp = lookPages().value(which);
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(8, 0, 4, 0);

    auto *options = new QVBoxLayout;
    auto check = [&](QCheckBox *&box, const QString &text, bool on, const char *name = nullptr) {
        box = new QCheckBox(text, page);
        box->setChecked(on);
        if (name)
            box->setObjectName(QLatin1String(name));
        options->addWidget(box);
    };
    auto spinRow = [&](QSpinBox *&spin, const QString &label, int lo, int hi, int value, const QString &suffix, const char *name) {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(label, page));
        spin = new QSpinBox(page);
        spin->setObjectName(QLatin1String(name));
        spin->setRange(lo, hi);
        spin->setSuffix(suffix);
        spin->setValue(value);
        row->addWidget(spin);
        row->addStretch(1);
        options->addLayout(row);
    };
    switch (which) {
    case LGeneral: {
        options->addWidget(hint(tr("A theme sets the colors and fonts of every area. Changes you make here sit on top "
                                   "of the theme; \"Save as Theme\" turns the result into a theme of your own."),
                                page));
        check(m_fullPath, tr("Show the full project path in the title bar"), s.showFullPath, "optFullPath");
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("Main toolbar icon size:"), page));
        m_toolbarIcons = new QComboBox(page);
        m_toolbarIcons->setObjectName(QStringLiteral("optToolbarIcons"));
        m_toolbarIcons->addItem(tr("Small (16 px)"), 16);
        m_toolbarIcons->addItem(tr("Medium (22 px)"), 22);
        m_toolbarIcons->addItem(tr("Large (32 px)"), 32);
        m_toolbarIcons->setCurrentIndex(qMax(0, m_toolbarIcons->findData(s.toolbarIconSize)));
        row->addWidget(m_toolbarIcons);
        row->addStretch(1);
        options->addLayout(row);
        break;
    }
    case LBinder:
        check(m_labelBinder, tr("Show label colors in the binder"), s.labelColorInBinder);
        check(m_labelIcons, tr("Let the label color shine through the icons"), s.labelColorInIcons, "optLabelIcons");
        spinRow(m_binderIndent, tr("Binder items extra indent:"), 4, 40, s.binderIndent, tr(" px"), "optBinderIndent");
        m_binderIndent->setToolTip(tr("Qt's standard is 20 px; the program uses 12 px"));
        break;
    case LComposition:
        options->addWidget(hint(tr("Paper width, zoom and background fade are set in the bar at the bottom of "
                                   "composition mode; a background picture under Project > Project Settings."),
                                page));
        break;
    case LCorkboard:
        check(m_stamps, tr("Show status stamps on index cards"), s.showStatusStamps);
        check(m_tint, tr("Tint index cards with the label color"), s.tintCardsWithLabel);
        spinRow(m_cardWidth, tr("Card width:"), 120, 480, s.cardWidth, tr(" px"), "optCardWidth");
        break;
    case LEditor:
        check(m_fixedWidth, tr("Use fixed width editor (text as a centered column)"), s.fixedWidth);
        spinRow(m_editorWidth, tr("Default editor width:"), 300, 2000, s.editorWidth, tr(" px"), "optEditorWidth");
        check(m_typewriter, tr("Typewriter scrolling in the main editor"), s.typewriterScrolling);
        break;
    case LPageView:
        options->addWidget(hint(tr("View > Text Editing > Page View shows the text on pages; their size and margins are set "
                                   "under View > Text Editing > Page Size and Margins."),
                                page));
        break;
    case LCombined:
        check(m_titles, tr("Show document titles in Combined Text"), s.showTitlesInCombined);
        break;
    default: break;
    }
    if (options->count())
        v->addLayout(options);
    else
        delete options;

    auto resetButton = [&](QWidget *parent, const QString &tip) {
        auto *b = new QToolButton(parent);
        b->setText(QStringLiteral("↺"));
        b->setAutoRaise(true);
        b->setToolTip(tip);
        return b;
    };

    // fonts
    if (!lp.fonts.isEmpty()) {
        auto *fontBox = new QGroupBox(tr("Fonts"), page);
        auto *fg = new QGridLayout(fontBox);
        fg->setColumnStretch(1, 1);
        for (const QString &key : lp.fonts) {
            const theme::FontInfo *fi = lookFontInfo(key);
            if (!fi)
                continue;
            const int row = fg->rowCount();
            auto *label = new QLabel(fi->label + QLatin1Char(':'), fontBox);
            auto *button = new QPushButton(fontBox);
            button->setObjectName(QStringLiteral("font_") + key);
            auto *reset = resetButton(fontBox, tr("Back to the theme's font"));
            reset->setObjectName(QStringLiteral("fontReset_") + key);
            fg->addWidget(label, row, 0);
            fg->addWidget(button, row, 1);
            fg->addWidget(reset, row, 2);
            m_fontButtons.insert(key, button);
            m_fontResets.insert(key, reset);
            m_lookLabels.insert(QStringLiteral("f:") + key, label);
            const theme::FontRole role = fi->role;
            connect(button, &QPushButton::clicked, this, [this, key, role] {
                const theme::Theme t = shownTheme();
                const theme::FontSpec spec = m_ownFonts.contains(key) ? theme::FontSpec::fromString(m_ownFonts.value(key))
                                                                      : t.fonts.value(role);
                bool ok = false;
                const QFont chosen = QFontDialog::getFont(&ok, spec.applied(theme::baseFont(role)), this, tr("Font"));
                if (ok)
                    setOwnFont(key, theme::FontSpec::fromFont(chosen, theme::baseFont(role)));
            });
            connect(reset, &QToolButton::clicked, this, [this, key] { setOwnFont(key, theme::FontSpec()); });
        }
        v->addWidget(fontBox);
    }

    // colors, in two columns; a click shows the shared palette (0.21) and "Other..."
    if (!lp.colors.isEmpty()) {
        auto *colorBox = new QGroupBox(tr("Colors"), page);
        auto *cg = new QGridLayout(colorBox);
        cg->setColumnStretch(3, 1);
        cg->setColumnMinimumWidth(3, 16);
        const int perColumn = (int(lp.colors.size()) + 1) / 2;
        int n = 0;
        for (const QString &key : lp.colors) {
            const theme::RoleInfo *ri = lookRoleInfo(key);
            if (!ri)
                continue;
            const int row = n % perColumn, col = (n / perColumn) * 4;
            ++n;
            auto *label = new QLabel(ri->label + QLatin1Char(':'), colorBox);
            auto *button = new QPushButton(colorBox);
            button->setObjectName(QStringLiteral("color_") + key);
            button->setFixedWidth(52);
            auto *reset = resetButton(colorBox, tr("Back to the theme's color"));
            reset->setObjectName(QStringLiteral("colorReset_") + key);
            cg->addWidget(label, row, col);
            cg->addWidget(button, row, col + 1);
            cg->addWidget(reset, row, col + 2);
            m_colorButtons.insert(key, button);
            m_colorResets.insert(key, reset);
            m_lookLabels.insert(QStringLiteral("c:") + key, label);
            const QString title = tr(lp.name) + QStringLiteral(": ") + ri->label;
            connect(button, &QPushButton::clicked, this, [this, key, title, button] {
                QMenu menu;
                palette::fillMenu(&menu, nullptr, shownColor(key), [this, key](const QColor &c) {
                    if (c.isValid())
                        setOwnColor(key, c);
                }, this, QString(), title);
                menu.exec(button->mapToGlobal(QPoint(0, button->height())));
            });
            connect(reset, &QToolButton::clicked, this, [this, key] { setOwnColor(key, QColor()); });
        }
        v->addWidget(colorBox);
    }
    v->addStretch(1);
    return page;
}

theme::Theme OptionsDialog::shownTheme() const
{
    theme::Theme t;
    if (!theme::load(m_theme->currentData().toString(), &t))
        theme::load(QStringLiteral("system"), &t);
    return t;
}

void OptionsDialog::reloadThemes(const QString &selectId)
{
    const QSignalBlocker block(m_theme);
    m_theme->clear();
    for (const theme::Entry &te : theme::available())
        m_theme->addItem(te.name, te.id);
    m_theme->setCurrentIndex(qMax(0, m_theme->findData(selectId)));
    if (m_preview)
        refreshLook();
}

QColor OptionsDialog::shownColor(const QString &key) const
{
    theme::Role r;
    return theme::roleFromKey(key, &r) ? m_shown.value(r) : QColor();
}

// Swatches, font buttons and the preview for the chosen theme and the changes made here.
void OptionsDialog::refreshLook()
{
    const theme::Theme t = shownTheme();
    m_shown = theme::resolvedColors(t, m_ownColors);
    auto mark = [this](const QString &labelKey, bool own) {
        if (QLabel *l = m_lookLabels.value(labelKey)) {
            QFont f = l->font();
            f.setBold(own);
            l->setFont(f);
        }
    };
    for (const theme::RoleInfo &ri : theme::roles()) {
        const QString key = QLatin1String(ri.key);
        QPushButton *b = m_colorButtons.value(key);
        if (!b)
            continue;
        const bool own = m_ownColors.contains(key);
        const QColor c = m_shown.value(ri.role);
        setButtonColor(b, c);
        b->setToolTip(own ? tr("%1 (your own color)").arg(c.name())
                          : t.colors.contains(ri.role) ? tr("%1 (from the theme)").arg(c.name())
                                                       : tr("%1 (automatic: follows the other colors)").arg(c.name()));
        m_colorResets.value(key)->setEnabled(own);
        mark(QStringLiteral("c:") + key, own);
    }
    QMap<theme::FontRole, theme::FontSpec> fonts = t.fonts;
    for (const theme::FontInfo &fi : theme::fontRoles()) {
        const QString key = QLatin1String(fi.key);
        const bool own = m_ownFonts.contains(key);
        if (own)
            fonts.insert(fi.role, theme::FontSpec::fromString(m_ownFonts.value(key)));
        QPushButton *b = m_fontButtons.value(key);
        if (!b)
            continue;
        const theme::FontSpec spec = fonts.value(fi.role);
        const QFont f = spec.applied(theme::baseFont(fi.role));
        QString text = QStringLiteral("%1, %2 pt").arg(f.family(), QLocale().toString(f.pointSizeF(), 'g', 3));
        if (f.bold())
            text += tr(", bold");
        if (f.italic())
            text += tr(", italic");
        if (!spec.isSet())
            text = tr("Standard (%1)").arg(text);
        b->setText(text);
        m_fontResets.value(key)->setEnabled(own);
        mark(QStringLiteral("f:") + key, own);
    }
    m_preview->setColors(m_shown);
    m_preview->setFonts(fonts);
    const int own = ownChanges();
    m_ownInfo->setText(own == 0 ? tr("Colors and fonts are those of the theme.")
                                : tr("%1 of your own on top of the theme (shown in bold below).")
                                      .arg(text::plural(own, tr("change"), tr("changes"))));
    m_resetLook->setEnabled(own > 0);
}

void OptionsDialog::selectTheme(const QString &id)
{
    const int i = m_theme->findData(id);
    if (i >= 0)
        m_theme->setCurrentIndex(i);
}

void OptionsDialog::setOwnColor(const QString &key, const QColor &color)
{
    theme::Role r;
    if (!theme::roleFromKey(key, &r))
        return;
    if (color.isValid())
        m_ownColors.insert(key, QColor(color.red(), color.green(), color.blue()));
    else
        m_ownColors.remove(key);
    refreshLook();
}

void OptionsDialog::setOwnFont(const QString &key, const theme::FontSpec &font)
{
    theme::FontRole r;
    if (!theme::fontRoleFromKey(key, &r))
        return;
    if (font.isSet())
        m_ownFonts.insert(key, font.toString());
    else
        m_ownFonts.remove(key);
    refreshLook();
}

void OptionsDialog::resetLook()
{
    m_ownColors.clear();
    m_ownFonts.clear();
    refreshLook();
}

QString OptionsDialog::saveLookAsTheme(const QString &name, QString *error)
{
    theme::Theme look = shownTheme();
    for (auto it = m_ownColors.constBegin(); it != m_ownColors.constEnd(); ++it) {
        theme::Role r;
        if (theme::roleFromKey(it.key(), &r))
            look.colors.insert(r, it.value());
    }
    for (auto it = m_ownFonts.constBegin(); it != m_ownFonts.constEnd(); ++it) {
        theme::FontRole r;
        if (theme::fontRoleFromKey(it.key(), &r))
            look.fonts.insert(r, theme::FontSpec::fromString(it.value()));
    }
    if (look.followDesktop && look.colors.contains(theme::Role::Window) && look.colors.contains(theme::Role::Text))
        look.followDesktop = false;
    const QString id = themeui::saveLookAs(look, name, error);
    if (id.isEmpty())
        return id;
    // the changes are part of the new theme now
    m_ownColors.clear();
    m_ownFonts.clear();
    reloadThemes(id);
    return id;
}

// ==================================================================== Corrections

QWidget *OptionsDialog::correctionsSection()
{
    const AppSettings &s = AppSettings::instance();
    QFormLayout *f = nullptr;

    QWidget *corr = page(&f);
    {
        auto *box = new QGroupBox(tr("Auto-Correction"), corr);
        auto *bl = new QVBoxLayout(box);
        m_capSentences = new QCheckBox(tr("Fix capitalization of sentences"), box);
        m_capSentences->setObjectName(QStringLiteral("optCapSentences"));
        m_capSentences->setChecked(s.capitalizeSentences);
        m_capI = new QCheckBox(tr("Capitalize \"i\""), box);
        m_capI->setObjectName(QStringLiteral("optCapI"));
        m_capI->setChecked(s.capitalizeI);
        bl->addWidget(m_capSentences);
        bl->addWidget(m_capI);
        f->addRow(box);
    }
    {
        auto *box = new QGroupBox(tr("Punctuation"), corr);
        auto *bl = new QVBoxLayout(box);
        auto *row = new QHBoxLayout;
        m_quotes = new QCheckBox(tr("Use smart quotes"), box);
        m_quotes->setObjectName(QStringLiteral("optSmartQuotes"));
        m_quotes->setChecked(s.smartQuotes);
        m_quoteStyle = new QComboBox(box);
        m_quoteStyle->setObjectName(QStringLiteral("optQuoteStyle"));
        m_quoteStyle->addItem(QStringLiteral("“abc” ‘abc’"), QStringLiteral("en"));
        m_quoteStyle->addItem(QStringLiteral("„abc“ ‚abc‘"), QStringLiteral("de"));
        m_quoteStyle->addItem(QStringLiteral("»abc« ›abc‹"), QStringLiteral("de2"));
        m_quoteStyle->addItem(QStringLiteral("«abc» ‹abc›"), QStringLiteral("fr"));
        m_quoteStyle->addItem(QStringLiteral("”abc” ’abc’"), QStringLiteral("sv"));
        m_quoteStyle->setCurrentIndex(qMax(0, m_quoteStyle->findData(s.quoteStyle)));
        row->addWidget(m_quotes);
        row->addWidget(m_quoteStyle);
        row->addStretch(1);
        bl->addLayout(row);
        m_dashes = new QCheckBox(tr("Replace double hyphens with em dashes (—)"), box);
        m_dashes->setChecked(s.smartDashes);
        m_ellipsis = new QCheckBox(tr("Replace triple periods with ellipses (…)"), box);
        m_ellipsis->setObjectName(QStringLiteral("optEllipsis"));
        m_ellipsis->setChecked(s.ellipsis);
        m_scriptNoSmart = new QCheckBox(tr("Disable smart quotes, em dash and ellipsis substitution in script mode"), box);
        m_scriptNoSmart->setObjectName(QStringLiteral("optScriptNoSmart"));
        m_scriptNoSmart->setChecked(s.scriptNoSmart);
        bl->addWidget(m_dashes);
        bl->addWidget(m_ellipsis);
        bl->addWidget(m_scriptNoSmart);
        f->addRow(box);
    }
    {
        auto *box = new QGroupBox(tr("Data Detection"), corr);
        auto *bl = new QVBoxLayout(box);
        m_detectUrls = new QCheckBox(tr("Automatically detect web addresses"), box);
        m_detectUrls->setObjectName(QStringLiteral("optDetectUrls"));
        m_detectUrls->setChecked(s.detectUrls);
        m_detectLinks = new QCheckBox(tr("Automatically detect [[document links]]"), box);
        m_detectLinks->setObjectName(QStringLiteral("optDetectLinks"));
        m_detectLinks->setChecked(s.detectDocLinks);
        bl->addWidget(m_detectUrls);
        bl->addWidget(m_detectLinks);
        bl->addWidget(hint(tr("A web address becomes a link when you type a space after it. [[Title]] becomes a link to the "
                              "document of that title, which is made in the folder of the text when there is none yet."),
                           box));
        f->addRow(box);
    }
    {
        auto *box = new QGroupBox(tr("Auto-Completion"), corr);
        auto *bl = new QVBoxLayout(box);
        m_completions = new QCheckBox(tr("Suggest completions as you type (in script mode)"), box);
        m_completions->setObjectName(QStringLiteral("optCompletions"));
        m_completions->setChecked(s.completions);
        bl->addWidget(m_completions);
        f->addRow(box);
    }
    {
        auto *row = new QHBoxLayout;
        m_substitutionsOn = new QCheckBox(tr("Enable additional substitutions"), corr);
        m_substitutionsOn->setObjectName(QStringLiteral("optSubstitutions"));
        m_substitutionsOn->setChecked(s.substitutionsOn);
        m_substitutions = s.substitutions;
        auto *edit = new QPushButton(tr("Edit Substitutions..."), corr);
        edit->setObjectName(QStringLiteral("optEditSubstitutions"));
        row->addWidget(m_substitutionsOn);
        row->addWidget(edit);
        row->addStretch(1);
        f->addRow(row);
        connect(edit, &QPushButton::clicked, this, [this] {
            // a table of "what you type" and "what it becomes"
            QDialog dlg(this);
            dlg.setObjectName(QStringLiteral("substitutionsDialog"));
            dlg.setWindowTitle(tr("Substitutions"));
            dlg.resize(460, 420);
            auto *l = new QVBoxLayout(&dlg);
            auto *table = new QTableWidget(0, 2, &dlg);
            table->setObjectName(QStringLiteral("substitutionsTable"));
            table->setHorizontalHeaderLabels({tr("Replace"), tr("With")});
            table->horizontalHeader()->setStretchLastSection(true);
            table->verticalHeader()->hide();
            for (const auto &p : m_substitutions) {
                const int r = table->rowCount();
                table->insertRow(r);
                table->setItem(r, 0, new QTableWidgetItem(p.first));
                table->setItem(r, 1, new QTableWidgetItem(p.second));
            }
            l->addWidget(table, 1);
            auto *row2 = new QHBoxLayout;
            auto *add = new QPushButton(tr("Add"), &dlg);
            auto *rem = new QPushButton(tr("Remove"), &dlg);
            auto *std = new QPushButton(tr("Standard Substitutions"), &dlg);
            row2->addWidget(add);
            row2->addWidget(rem);
            row2->addWidget(std);
            row2->addStretch(1);
            l->addLayout(row2);
            l->addWidget(hint(tr("A substitution is made when the typed text is followed by a space or punctuation."), &dlg));
            auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
            l->addWidget(bb);
            connect(add, &QPushButton::clicked, &dlg, [table] {
                table->insertRow(table->rowCount());
                table->setItem(table->rowCount() - 1, 0, new QTableWidgetItem);
                table->setItem(table->rowCount() - 1, 1, new QTableWidgetItem);
                table->editItem(table->item(table->rowCount() - 1, 0));
            });
            connect(rem, &QPushButton::clicked, &dlg, [table] {
                if (table->currentRow() >= 0)
                    table->removeRow(table->currentRow());
            });
            connect(std, &QPushButton::clicked, &dlg, [table] {
                table->setRowCount(0);
                for (const auto &p : AppSettings::standardSubstitutions()) {
                    const int r = table->rowCount();
                    table->insertRow(r);
                    table->setItem(r, 0, new QTableWidgetItem(p.first));
                    table->setItem(r, 1, new QTableWidgetItem(p.second));
                }
            });
            connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
            if (dlg.exec() != QDialog::Accepted)
                return;
            m_substitutions.clear();
            for (int r = 0; r < table->rowCount(); ++r) {
                const QString a = table->item(r, 0) ? table->item(r, 0)->text() : QString();
                if (!a.isEmpty())
                    m_substitutions.append({a, table->item(r, 1) ? table->item(r, 1)->text() : QString()});
            }
        });
    }

    QWidget *spell = page(&f);
    m_spell = new QCheckBox(tr("Check spelling as you type"), spell);
    m_spell->setChecked(s.spellCheck);
    f->addRow(m_spell);
    m_spellNumbers = new QCheckBox(tr("Ignore words with numbers"), spell);
    m_spellNumbers->setObjectName(QStringLiteral("optSpellNumbers"));
    m_spellNumbers->setChecked(s.spellIgnoreNumbers);
    f->addRow(m_spellNumbers);
    m_spellUnderline = new QComboBox(spell);
    m_spellUnderline->setObjectName(QStringLiteral("optSpellUnderline"));
    m_spellUnderline->addItem(tr("Waves"), QStringLiteral("waves"));
    m_spellUnderline->addItem(tr("Dots"), QStringLiteral("dots"));
    m_spellUnderline->addItem(tr("Straight line"), QStringLiteral("line"));
    m_spellUnderline->setCurrentIndex(qMax(0, m_spellUnderline->findData(s.spellUnderline)));
    f->addRow(tr("Underline style:"), m_spellUnderline);
    m_spellLang = new QComboBox(spell);
    const QStringList langs = SpellChecker::instance().availableLanguages();
    m_spellLang->addItems(langs);
    m_spellLang->setCurrentText(s.spellLanguage);
    if (langs.isEmpty()) {
        m_spellLang->addItem(tr("No dictionaries found"));
        m_spellLang->setEnabled(false);
    }
    f->addRow(tr("Dictionary:"), m_spellLang);
    f->addRow(hint(tr("Dictionaries come from your system (e.g. install the package hunspell-de-de for German)."), spell));
    auto *words = new QPushButton(tr("View Personal Word List..."), spell);
    words->setObjectName(QStringLiteral("optPersonalWords"));
    f->addRow(words);
    connect(words, &QPushButton::clicked, this, [this] {
        QDialog dlg(this);
        dlg.setObjectName(QStringLiteral("personalWordsDialog"));
        dlg.setWindowTitle(tr("Personal Word List"));
        dlg.resize(360, 420);
        auto *l = new QVBoxLayout(&dlg);
        l->addWidget(hint(tr("Words added with \"Learn Spelling\" in the right-click menu of the text."), &dlg));
        auto *list = new QListWidget(&dlg);
        list->addItems(SpellChecker::instance().personalWords());
        list->setSelectionMode(QAbstractItemView::ExtendedSelection);
        l->addWidget(list, 1);
        auto *rem = new QPushButton(tr("Remove"), &dlg);
        l->addWidget(rem, 0, Qt::AlignLeft);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        l->addWidget(bb);
        connect(rem, &QPushButton::clicked, &dlg, [list] { qDeleteAll(list->selectedItems()); });
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted)
            return;
        QStringList keep;
        for (int i = 0; i < list->count(); ++i)
            keep << list->item(i)->text();
        SpellChecker::instance().setPersonalWords(keep);
    });
    return subPages(SCorrections, {{tr("Corrections"), corr}, {tr("Spelling"), spell}}, QStringLiteral("optCorrectionPages"));
}

// ==================================================================== Sharing

QWidget *OptionsDialog::sharingSection()
{
    const AppSettings &s = AppSettings::instance();
    QFormLayout *f = nullptr;

    QWidget *imp = page(&f);
    {
        auto *box = new QGroupBox(tr("Rich Text (RTF, Word, LibreOffice)"), imp);
        auto *bl = new QVBoxLayout(box);
        m_importComments = new QCheckBox(tr("Import comments as inline annotations"), box);
        m_importComments->setObjectName(QStringLiteral("optImportComments"));
        m_importComments->setChecked(s.importCommentsInline);
        m_importFootnotes = new QCheckBox(tr("Import footnotes and endnotes as inline footnotes"), box);
        m_importFootnotes->setObjectName(QStringLiteral("optImportFootnotes"));
        m_importFootnotes->setChecked(s.importFootnotesInline);
        m_importIgnoreStyles = new QCheckBox(tr("Ignore stylesheet information in imported documents"), box);
        m_importIgnoreStyles->setObjectName(QStringLiteral("optImportIgnoreStyles"));
        m_importIgnoreStyles->setChecked(s.importIgnoreStyles);
        bl->addWidget(m_importComments);
        bl->addWidget(m_importFootnotes);
        bl->addWidget(m_importIgnoreStyles);
        f->addRow(box);
    }
    m_encoding = new QComboBox(imp);
    m_encoding->setObjectName(QStringLiteral("optEncoding"));
    m_encoding->addItem(tr("Automatic (UTF-8, else Windows Western)"), QStringLiteral("auto"));
    for (const char *e : {"UTF-8", "UTF-16", "windows-1252", "ISO-8859-1", "ISO-8859-15"})
        m_encoding->addItem(QLatin1String(e), QLatin1String(e));
    m_encoding->setCurrentIndex(qMax(0, m_encoding->findData(s.plainTextEncoding)));
    f->addRow(tr("Encoding for plain text:"), m_encoding);
    m_htmlAsText = new QCheckBox(tr("Convert HTML files to text"), imp);
    m_htmlAsText->setObjectName(QStringLiteral("optHtmlAsText"));
    m_htmlAsText->setChecked(s.htmlAsText);
    f->addRow(m_htmlAsText);
    f->addRow(hint(tr("Off, an imported .html file becomes a web page document (read-only, with its pictures); on, a text "
                      "document you can edit."),
                   imp));

    QWidget *exp = page(&f);
    {
        QSettings st;
        m_exportFormat = new QComboBox(exp);
        m_exportFormat->setObjectName(QStringLiteral("optExportFormat"));
        m_exportFormat->addItem(tr("Rich Text (.rtf)"), QStringLiteral("rtf"));
        m_exportFormat->addItem(tr("Word (.docx)"), QStringLiteral("docx"));
        m_exportFormat->addItem(tr("Markdown (.md)"), QStringLiteral("md"));
        m_exportFormat->addItem(tr("Plain text (.txt)"), QStringLiteral("txt"));
        m_exportFormat->setCurrentIndex(qMax(0, m_exportFormat->findData(st.value(QStringLiteral("export/format"), QStringLiteral("rtf")))));
        f->addRow(tr("File > Export > Files writes texts as:"), m_exportFormat);
        m_exportComments = new QCheckBox(tr("Comments and inline annotations in exported texts"), exp);
        m_exportComments->setChecked(st.value(QStringLiteral("export/comments"), true).toBool());
        m_exportNotes = new QCheckBox(tr("Document notes as files of their own"), exp);
        m_exportNotes->setChecked(st.value(QStringLiteral("export/notes"), true).toBool());
        f->addRow(m_exportComments);
        f->addRow(m_exportNotes);
        f->addRow(hint(tr("The same choices as in the File > Export > Files window, which remembers what you chose last."), exp));
    }

    QWidget *conv = page(&f);
    m_docConverter = new QComboBox(conv);
    m_docConverter->setObjectName(QStringLiteral("optDocConverter"));
    m_docConverter->addItem(tr("The program's own reader (add-on \"Word 97-2003 Import\")"), QStringLiteral("builtin"));
    m_docConverter->addItem(tr("LibreOffice (when installed)"), QStringLiteral("libreoffice"));
    m_docConverter->setCurrentIndex(qMax(0, m_docConverter->findData(s.docConverter)));
    f->addRow(tr("Import .doc files (Word 97-2003) via:"), m_docConverter);
    f->addRow(hint(tr("The own reader needs no other program; LibreOffice also reads headers, drawings and Word 6/95 files. "
                      "When the chosen way fails, the other one is tried."),
                   conv));
    return tabbed(SSharing, {{tr("Import"), imp}, {tr("Export"), exp}, {tr("Conversion"), conv}}, QStringLiteral("optSharingTabs"));
}

// ==================================================================== Backup

QWidget *OptionsDialog::backupSection()
{
    const AppSettings &s = AppSettings::instance();
    QFormLayout *f = nullptr;
    QWidget *w = page(&f);
    m_backupOpen = new QCheckBox(tr("Back up on project open"), w);
    m_backupOpen->setChecked(s.backupOnOpen);
    m_backupClose = new QCheckBox(tr("Back up on project close"), w);
    m_backupClose->setChecked(s.backupOnClose);
    m_backupManual = new QCheckBox(tr("Back up with each manual save (File > Save)"), w);
    m_backupManual->setObjectName(QStringLiteral("optBackupManual"));
    m_backupManual->setChecked(s.backupOnManualSave);
    f->addRow(m_backupOpen);
    f->addRow(m_backupClose);
    f->addRow(m_backupManual);
    m_backupKeep = new QSpinBox(w);
    m_backupKeep->setRange(0, 100);
    m_backupKeep->setSpecialValueText(tr("all"));
    m_backupKeep->setValue(s.backupKeep);
    f->addRow(tr("Retain backup files per project:"), m_backupKeep);
    auto *row = new QHBoxLayout;
    m_backupDir = new QLineEdit(s.backupDir.isEmpty() ? backup::defaultDirectory() : s.backupDir, w);
    auto *choose = new QPushButton(tr("Choose..."), w);
    auto *open = new QPushButton(tr("Open Backup Folder..."), w);
    row->addWidget(m_backupDir, 1);
    row->addWidget(choose);
    row->addWidget(open);
    f->addRow(tr("Backup location:"), row);
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Backup Folder"), m_backupDir->text());
        if (!d.isEmpty())
            m_backupDir->setText(d);
    });
    connect(open, &QPushButton::clicked, this, [this] {
        QDir().mkpath(m_backupDir->text());
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_backupDir->text()));
    });
    f->addRow(hint(tr("Backups are ZIP files of the whole project folder with the date in their names. To restore one, "
                      "unpack it and open the project inside."),
                   w));
    return w;
}

// ==================================================================== Extras

QWidget *OptionsDialog::extrasSection()
{
    const AppSettings &s = AppSettings::instance();
    QList<std::pair<QString, QWidget *>> pages;
    QFormLayout *f = nullptr;

    // Mind Map (also in the board's own Options window)
    const MindMapPrefs &mp = MindMapPrefs::instance();
    QWidget *mm = page(&f);
    auto mcheck = [&](QCheckBox *&b, const QString &text, bool on, const char *name) {
        b = new QCheckBox(text, mm);
        b->setObjectName(QLatin1String(name));
        b->setChecked(on);
        f->addRow(b);
    };
    mcheck(m_mmEnterEnds, tr("Enter ends editing a note (Shift+Enter: new line)"), mp.enterEndsEditing, "optMmEnterEnds");
    mcheck(m_mmEnterCreates, tr("Enter on a selected note makes a new note"), mp.enterCreatesNotes, "optMmEnterCreates");
    mcheck(m_mmUseSelected, tr("A new note takes the look of the selected note"), mp.useSelectedStyle, "optMmUseSelected");
    mcheck(m_mmDashed, tr("Lines without arrows are dashed"), mp.dashedLines, "optMmDashed");
    mcheck(m_mmAutoSize, tr("A short note becomes as narrow as its text"), mp.autoSize, "optMmAutoSize");
    m_mmFaded = new QSpinBox(mm);
    m_mmFaded->setRange(10, 95);
    m_mmFaded->setSuffix(QStringLiteral(" %"));
    m_mmFaded->setValue(mp.fadedPercent);
    f->addRow(tr("Faded notes fade by:"), m_mmFaded);
    auto *box = new QGroupBox(tr("New notes"), mm);
    auto *bf = new QFormLayout(box);
    m_mmNoteSize = new QDoubleSpinBox(box);
    m_mmNoteSize->setRange(6, 72);
    m_mmNoteSize->setSuffix(tr(" pt"));
    m_mmNoteSize->setValue(mp.newNoteSize);
    m_mmNoteWidth = new QDoubleSpinBox(box);
    m_mmNoteWidth->setRange(40, 800);
    m_mmNoteWidth->setDecimals(0);
    m_mmNoteWidth->setSuffix(tr(" px"));
    m_mmNoteWidth->setValue(mp.newNoteWidth);
    m_mmNoteShape = new QComboBox(box);
    m_mmNoteShape->setObjectName(QStringLiteral("optMmNoteShape"));
    m_mmNoteShape->addItem(tr("Rounded Rectangle"), 0);
    m_mmNoteShape->addItem(tr("Rectangle"), 1);
    m_mmNoteShape->addItem(tr("Jagged"), 2);
    m_mmNoteShape->addItem(tr("Cloud"), 3);
    m_mmNoteShape->setCurrentIndex(qMax(0, m_mmNoteShape->findData(mp.newNoteShape)));
    m_mmNoteBorder = new QSpinBox(box);
    m_mmNoteBorder->setRange(0, 5);
    m_mmNoteBorder->setSuffix(tr(" px"));
    m_mmNoteBorder->setSpecialValueText(tr("No border"));
    m_mmNoteBorder->setValue(mp.newNoteBorder);
    m_mmFill = mp.newNoteFill;
    m_mmNoteFill = new QPushButton(box);
    m_mmNoteFill->setObjectName(QStringLiteral("optMmNoteFill"));
    m_mmNoteFill->setFixedWidth(52);
    setButtonColor(m_mmNoteFill, m_mmFill);
    connect(m_mmNoteFill, &QPushButton::clicked, this, [this] {
        QMenu menu;
        palette::fillMenu(&menu, nullptr, m_mmFill, [this](const QColor &c) {
            m_mmFill = c;
            setButtonColor(m_mmNoteFill, c);
        }, this, tr("No Fill"), tr("Fill of New Notes"));
        menu.exec(m_mmNoteFill->mapToGlobal(QPoint(0, m_mmNoteFill->height())));
    });
    bf->addRow(tr("Font size:"), m_mmNoteSize);
    bf->addRow(tr("Width:"), m_mmNoteWidth);
    bf->addRow(tr("Shape:"), m_mmNoteShape);
    bf->addRow(tr("Border:"), m_mmNoteBorder);
    bf->addRow(tr("Fill:"), m_mmNoteFill);
    f->addRow(box);
    {
        // the background of every board that has no color of its own (0.21.1): one for
        // light themes, one for dark themes; such boards follow a change at once
        auto *bg = new QGroupBox(tr("Background of boards (when a board has no color of its own)"), mm);
        auto *gf = new QFormLayout(bg);
        m_mmBgLight = mp.boardBackground;
        m_mmBgDark = mp.boardBackgroundDark;
        auto colorButton = [this, bg](QPushButton *&b, QColor *color, const char *name, const QString &title) {
            b = new QPushButton(bg);
            b->setObjectName(QLatin1String(name));
            b->setFixedWidth(52);
            setButtonColor(b, *color);
            QPushButton *btn = b;
            connect(b, &QPushButton::clicked, this, [this, btn, color, title] {
                QMenu menu;
                menu.setObjectName(QStringLiteral("optMmBackgroundMenu"));
                palette::fillMenu(&menu, nullptr, *color, [btn, color](const QColor &c) {
                    if (!c.isValid())
                        return;
                    *color = c;
                    setButtonColor(btn, c);
                }, this, QString(), title);
                menu.exec(btn->mapToGlobal(QPoint(0, btn->height())));
            });
        };
        colorButton(m_mmBgLightBtn, &m_mmBgLight, "optMmBackgroundLight", tr("Background in Light Themes"));
        colorButton(m_mmBgDarkBtn, &m_mmBgDark, "optMmBackgroundDark", tr("Background in Dark Themes"));
        gf->addRow(tr("Light themes:"), m_mmBgLightBtn);
        gf->addRow(tr("Dark themes:"), m_mmBgDarkBtn);
        gf->addRow(hint(tr("A board's own color is chosen by right-clicking an empty place of the board > Background Color; "
                           "it is kept for the kind of theme in use (light or dark), the other kind keeps its own."),
                        bg));
        f->addRow(bg);
    }
    m_mmIconSize = new QSpinBox(mm);
    m_mmIconSize->setObjectName(QStringLiteral("optMmIconSize"));
    m_mmIconSize->setRange(10, 48);
    m_mmIconSize->setSuffix(tr(" px"));
    m_mmIconSize->setValue(mp.iconSize);
    f->addRow(tr("Size of icons on notes and lines:"), m_mmIconSize);
    f->addRow(hint(tr("Right-click a note for its colors, shape and icons; right-click a line for its arrows, color and "
                      "icon. Help > Mind Map Help shows how lines and arrows are drawn."),
                   mm));
    pages.append({tr("Mind Map"), mm});

    // Objects
    QWidget *obj = page(&f);
    m_objectsTemplates = new QLabel(objects::hasOwnSchemaForNewProjects() ? tr("Your own templates (saved with \"Use for New Projects\")")
                                                                         : tr("The standard templates"),
                                    obj);
    m_objectsTemplates->setObjectName(QStringLiteral("optObjectsTemplates"));
    f->addRow(tr("Templates and colors of new projects:"), m_objectsTemplates);
    auto *std = new QPushButton(tr("Use the Standard Templates for New Projects"), obj);
    std->setObjectName(QStringLiteral("optObjectsStandard"));
    std->setEnabled(objects::hasOwnSchemaForNewProjects());
    connect(std, &QPushButton::clicked, this, [this, std] {
        m_resetObjectTemplates = true;
        m_objectsTemplates->setText(tr("The standard templates"));
        std->setEnabled(false);
    });
    f->addRow(std);
    f->addRow(hint(tr("The categories, fields and colors of a project are changed under Project > Object Templates and "
                      "Colors; \"Use for New Projects\" there makes them the templates of new projects."),
                   obj));
    pages.append({tr("Objects"), obj});

    // Add-ons and Plugins (the Plugins page of 0.20)
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        auto *view = new QTextBrowser(w);
        auto &pm = compile::PluginManager::instance();
        QString html = QStringLiteral("<h3>%1</h3><ul>").arg(tr("Compile formats"));
        for (ExporterInterface *e : pm.exporters())
            html += QStringLiteral("<li>%1 <i>(.%2)</i></li>").arg(e->name().toHtmlEscaped(), e->fileExtension());
        html += QStringLiteral("</ul>");
        if (!pm.tools().isEmpty()) {
            html += QStringLiteral("<h3>%1</h3><ul>").arg(tr("Writing tools (Edit > Writing Tools)"));
            for (tools::ToolInterface *t : pm.tools())
                html += QStringLiteral("<li>%1</li>").arg(t->name().toHtmlEscaped());
            html += QStringLiteral("</ul>");
        }
        if (!pm.unavailable().isEmpty()) {
            html += QStringLiteral("<h3>%1</h3><ul>").arg(tr("Installed but not usable"));
            for (const QString &u : pm.unavailable())
                html += QStringLiteral("<li>%1</li>").arg(u.toHtmlEscaped());
            html += QStringLiteral("</ul>");
        }
        html += QStringLiteral("<h3>%1</h3><p>%2</p><ul>").arg(tr("Adding formats"),
                                                               tr("Put plugin files (.so libraries or .json command "
                                                                  "descriptions) into one of these folders and restart:"));
        for (const QString &d : pm.searchDirs())
            html += QStringLiteral("<li><code>%1</code></li>").arg(d.toHtmlEscaped());
        html += QStringLiteral("</ul>");
        if (!pm.messages().isEmpty()) {
            html += QStringLiteral("<h3>%1</h3><pre>").arg(tr("Log"));
            html += pm.messages().join(QLatin1Char('\n')).toHtmlEscaped();
            html += QStringLiteral("</pre>");
        }
        view->setHtml(html);
        l->addWidget(view, 1);
        auto *row = new QHBoxLayout;
        auto *open = new QPushButton(tr("Open My Plugin Folder"), w);
        row->addWidget(open);
        row->addStretch(1);
        l->addLayout(row);
        l->addWidget(hint(tr("Add-ons are installed with File > Install Update or Add-on and switched on and off in Help > Add-on Manager."), w));
        connect(open, &QPushButton::clicked, this, [] {
            const QString d = compile::PluginManager::instance().userPluginDir();
            QDir().mkpath(d);
            QDesktopServices::openUrl(QUrl::fromLocalFile(d));
        });
        pages.append({tr("Add-ons and Plugins"), w});
    }

    // Crash Reports
    QWidget *cr = page(&f);
    m_freezeSeconds = new QSpinBox(cr);
    m_freezeSeconds->setObjectName(QStringLiteral("optFreezeSeconds"));
    m_freezeSeconds->setRange(10, 600);
    m_freezeSeconds->setSuffix(tr(" s"));
    m_freezeSeconds->setValue(s.freezeSeconds);
    f->addRow(tr("Write a freeze report when the program does not answer for:"), m_freezeSeconds);
    m_keepReports = new QSpinBox(cr);
    m_keepReports->setObjectName(QStringLiteral("optKeepReports"));
    m_keepReports->setRange(1, 200);
    m_keepReports->setValue(s.keepReports);
    f->addRow(tr("Keep the newest reports:"), m_keepReports);
    auto *openReports = new QPushButton(tr("Open the Folder of the Reports"), cr);
    connect(openReports, &QPushButton::clicked, this, [] {
        QDir().mkpath(crash::reportDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(crash::reportDir()));
    });
    f->addRow(openReports);
    f->addRow(hint(tr("Reports stay on this computer; Help > Crash Reports shows them. Fewer reports to keep take effect at the next start."), cr));
    pages.append({tr("Crash Reports"), cr});
    return subPages(SExtras, pages, QStringLiteral("optExtrasPages"));
}

// ==================================================================== Manage, Defaults

void OptionsDialog::fillManageMenu()
{
    m_manageMenu->clear();
    m_manageMenu->addAction(tr("Load Options from File..."), this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, tr("Load Options"), QDir::homePath(), tr("Options (*.qboptions)"));
        if (file.isEmpty())
            return;
        QString err;
        if (!loadOptionsFromFile(file, &err))
            QMessageBox::warning(this, tr("Load Options"), err);
    })->setObjectName(QStringLiteral("manageLoad"));
    m_manageMenu->addAction(tr("Save Options to File..."), this, [this] {
        apply();
        const QString file = QFileDialog::getSaveFileName(this, tr("Save Options"), QDir::home().filePath(QStringLiteral("Quillbinder.qboptions")),
                                                          tr("Options (*.qboptions)"));
        if (file.isEmpty())
            return;
        QString err;
        if (!saveOptionsToFile(file, &err))
            QMessageBox::warning(this, tr("Save Options"), err);
    })->setObjectName(QStringLiteral("manageSave"));
    m_manageMenu->addAction(tr("Save Theme Options to File..."), this, [this] {
        // the theme as shown here, with the changes made on top of it
        theme::Theme look = shownTheme();
        for (auto it = m_ownColors.constBegin(); it != m_ownColors.constEnd(); ++it) {
            theme::Role r;
            if (theme::roleFromKey(it.key(), &r))
                look.colors.insert(r, it.value());
        }
        for (auto it = m_ownFonts.constBegin(); it != m_ownFonts.constEnd(); ++it) {
            theme::FontRole r;
            if (theme::fontRoleFromKey(it.key(), &r))
                look.fonts.insert(r, theme::FontSpec::fromString(it.value()));
        }
        const QString file = QFileDialog::getSaveFileName(this, tr("Save Theme Options"),
                                                          QDir::home().filePath(text::safeFileName(m_theme->currentText()) + QStringLiteral(".qbtheme")),
                                                          tr("Themes (*.qbtheme)"));
        if (file.isEmpty())
            return;
        QFile out(file);
        if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(look.toJson()).toJson()) < 0)
            QMessageBox::warning(this, tr("Save Theme Options"), out.errorString());
    })->setObjectName(QStringLiteral("manageSaveTheme"));
    m_manageMenu->addSeparator();
    // the themes: a click shows one here (OK or Apply uses it)
    for (const theme::Entry &te : theme::available()) {
        QAction *a = m_manageMenu->addAction(te.name, this, [this, id = te.id] {
            selectTheme(id);
            showSection(SAppearance);
        });
        a->setCheckable(true);
        a->setChecked(te.id == m_theme->currentData().toString());
    }
    m_manageMenu->addSeparator();
    m_manageMenu->addAction(tr("Load Theme from File..."), this, [this] {
        const QString id = themeui::importFromFile(this);
        if (!id.isEmpty()) {
            reloadThemes(id);
            showSection(SAppearance);
        }
    });
    m_manageMenu->addAction(tr("Reveal Themes Folder..."), this, [] {
        QDir().mkpath(theme::userThemeDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(theme::userThemeDir()));
    });
}

bool OptionsDialog::saveOptionsToFile(const QString &file, QString *error)
{
    // a copy of the program's settings (INI text), without what belongs to this
    // computer only: recent projects, window places, the desktop integration
    QSettings from;
    from.sync();
    QFile::remove(file);
    QSettings to(file, QSettings::IniFormat);
    for (const QString &k : from.allKeys()) {
        if (k.startsWith(QLatin1String("window/")) || k == QLatin1String("app/recent") || k == QLatin1String("app/lastDir")
            || k == QLatin1String("app/integrated") || k == QLatin1String("app/welcomeFolder") || k == QLatin1String("app/welcomeShown"))
            continue;
        to.setValue(k, from.value(k));
    }
    to.setValue(QStringLiteral("quillbinderOptions/version"), 1);
    to.sync();
    if (to.status() != QSettings::NoError) {
        if (error)
            *error = tr("The file could not be written.");
        return false;
    }
    return true;
}

bool OptionsDialog::loadOptionsFromFile(const QString &file, QString *error)
{
    QSettings from(file, QSettings::IniFormat);
    if (from.status() != QSettings::NoError || !from.contains(QStringLiteral("quillbinderOptions/version"))) {
        if (error)
            *error = tr("This is not a file of options saved by Quillbinder.");
        return false;
    }
    QSettings to;
    for (const QString &k : from.allKeys())
        if (!k.startsWith(QLatin1String("quillbinderOptions/")))
            to.setValue(k, from.value(k));
    to.sync();
    AppSettings::instance().load();
    MindMapPrefs::instance().load();
    AppSettings::instance().notifyChanged();
    QMessageBox::information(this, tr("Load Options"),
                             tr("The options were loaded. Keyboard shortcuts and toolbars from the file take effect when the "
                                "program is started again."));
    // the window shows the new values when it is opened again
    reject();
    return true;
}

bool OptionsDialog::restoreDefaults(bool ask)
{
    if (ask && QMessageBox::question(this, tr("Defaults"),
                                     tr("Set every option back to the program's standard?\n\nYour recent projects, themes of your "
                                        "own, layouts, toolbars and keyboard shortcuts stay as they are."))
                   != QMessageBox::Yes)
        return false;
    AppSettings::instance().resetToDefaults();
    MindMapPrefs::instance() = MindMapPrefs();
    MindMapPrefs::instance().save();
    if (ask)
        QMessageBox::information(this, tr("Defaults"), tr("The options are back to the standard."));
    reject(); // the fields of this window show the old values
    return true;
}

// ==================================================================== OK, Apply

void OptionsDialog::accept()
{
    apply();
    QDialog::accept();
}

void OptionsDialog::apply()
{
    for (const auto &sec : m_toolSections)
        sec->apply();
    QTimer::singleShot(0, &toolhost::host(), [] { toolhost::host().notifySettingsChanged(); });
    AppSettings &s = AppSettings::instance();
    // General
    s.openLastProject = m_openLast->isChecked();
    s.useDesktopPortal = m_portal->isChecked();
    s.recentCount = m_recentCount->value();
    while (s.recentProjects.size() > s.recentCount)
        s.recentProjects.removeLast();
    s.autosaveSeconds = m_autosave->value();
    s.snapshotOnManualSave = m_snapOnSave->isChecked();
    s.forename = m_forename->text().trimmed();
    s.surname = m_surname->text().trimmed();
    s.street = m_street->text().trimmed();
    s.city = m_city->text().trimmed();
    s.state = m_state->text().trimmed();
    s.zip = m_zip->text().trimmed();
    s.country = m_country->text().trimmed();
    s.phone = m_phone->text().trimmed();
    s.email = m_email->text().trimmed();
    s.sharedTemplatesDir = m_sharedTemplates->text().trimmed();
    s.citationsApp = m_citations->text().trimmed();
    s.sepMerge = m_sepMerge->currentData().toString();
    s.sepMergeCustom = m_sepMergeCustom->text();
    s.sepScratch = m_sepScratch->currentData().toString();
    s.sepScratchCustom = m_sepScratchCustom->text();
    s.autoQuit = m_autoQuit->isChecked();
    s.autoQuitMinutes = m_autoQuitMinutes->value();
    // Editing
    s.reloadZoom = m_reloadZoom->isChecked();
    for (auto it = m_zoomDefaults.cbegin(); it != m_zoomDefaults.cend(); ++it) {
        if (it.key() == QLatin1String(zoom::kComposition)) {
            s.compositionZoomPercent = qRound(it.value() * 100);
            continue;
        }
        if (qFuzzyCompare(zoom::defaultFactor(it.key()), it.value()))
            continue;
        // a new default: the area takes it at once (the editors through the main window)
        zoom::setDefaultFactor(it.key(), it.value());
        zoom::setFactor(it.key(), it.value());
    }
    s.rulerUnit = m_rulerUnit->currentData().toString();
    s.rulerSnapMm = m_rulerSnap->value();
    s.overtypeKey = m_overtype->isChecked();
    s.customDateFormat = m_dateCustom->isChecked() ? m_dateFormat->text().trimmed() : QString();
    s.countCharacters = m_liveCounts->currentData().toBool();
    s.noColorInlineAnnotations = m_noColorAnnotations->isChecked();
    s.notesBeforePunctuation = m_notesBeforePunct->isChecked();
    s.blockCursor = m_blockCursor->isChecked();
    s.blockCursorWidth = m_blockWidth->value();
    s.revisionColors = m_revColors == text::standardRevisionColors() ? QList<QColor>() : m_revColors;
    s.newFontFamily = m_newAutoFont->isChecked() ? QString() : m_newFont->currentFont().family();
    s.newFontSize = m_newSize->value();
    s.newLineSpacing = m_newSpacing->value();
    if (qAbs(m_newIndent->value() - units::fromPx(s.newFirstLineIndent)) > 0.006)
        s.newFirstLineIndent = units::toPx(m_newIndent->value());
    if (qAbs(m_newParaSpacing->value() - direct::pxToPt(s.newParagraphSpacing)) > 0.06)
        s.newParagraphSpacing = direct::ptToPx(m_newParaSpacing->value());
    s.newJustify = m_newJustify->isChecked();
    if (m_clearNewStyles)
        s.newStylesXml.clear();
    // Behaviors
    s.compositionScreen = m_compScreen->currentData().toString();
    s.compositionHideMain = m_compHideMain->isChecked();
    s.compositionEscCloses = m_compEsc->isChecked();
    s.backLinks = m_backLinks->isChecked();
    s.linkOpens = m_linkOpens->currentData().toString();
    s.corkDoubleClickNew = m_corkDoubleClick->isChecked();
    s.dropOnTextDocuments = m_dropOnText->isChecked();
    s.dragExportFormat = m_dragFormat->currentData().toString();
    s.dragExportChildren = m_dragChildren->isChecked();
    s.startWithCorkboard = m_folderCork->isChecked();
    s.subdocsAsFolders = m_subdocsAsFolders->isChecked();
    s.mediaRewindSeconds = m_mediaRewind->value();
    s.mediaSkipSeconds = m_mediaSkip->value();
    s.mediaRememberPosition = m_mediaRemember->isChecked();
    if (m_mediaBackend->isEnabled())
        s.mediaBackend = m_mediaBackend->currentData().toString();
    QTimer::singleShot(0, &playback::hub(), [] { playback::hub().notifySettings(); });
    s.reopenQuickRefs = m_reopenQuickRefs->isChecked();
    s.returnEndsSynopsis = m_returnEndsSynopsis->isChecked();
    s.returnCreatesItem = m_returnCreates->isChecked();
    s.snapshotNotify = m_snapNotify->isChecked();
    // Appearance
    s.theme = m_theme->currentData().toString();
    s.ownColors = m_ownColors;
    s.ownFonts = m_ownFonts;
    s.showFullPath = m_fullPath->isChecked();
    s.toolbarIconSize = m_toolbarIcons->currentData().toInt();
    s.labelColorInBinder = m_labelBinder->isChecked();
    s.labelColorInIcons = m_labelIcons->isChecked();
    s.binderIndent = m_binderIndent->value();
    s.showStatusStamps = m_stamps->isChecked();
    s.tintCardsWithLabel = m_tint->isChecked();
    s.cardWidth = m_cardWidth->value();
    s.fixedWidth = m_fixedWidth->isChecked();
    s.editorWidth = m_editorWidth->value();
    s.typewriterScrolling = m_typewriter->isChecked();
    s.showTitlesInCombined = m_titles->isChecked();
    // Corrections
    s.capitalizeSentences = m_capSentences->isChecked();
    s.capitalizeI = m_capI->isChecked();
    s.smartQuotes = m_quotes->isChecked();
    s.quoteStyle = m_quoteStyle->currentData().toString();
    s.smartDashes = m_dashes->isChecked();
    s.ellipsis = m_ellipsis->isChecked();
    s.scriptNoSmart = m_scriptNoSmart->isChecked();
    s.detectUrls = m_detectUrls->isChecked();
    s.detectDocLinks = m_detectLinks->isChecked();
    s.completions = m_completions->isChecked();
    s.substitutionsOn = m_substitutionsOn->isChecked();
    s.substitutions = m_substitutions;
    s.spellCheck = m_spell->isChecked();
    s.spellIgnoreNumbers = m_spellNumbers->isChecked();
    s.spellUnderline = m_spellUnderline->currentData().toString();
    if (m_spellLang->isEnabled())
        s.spellLanguage = m_spellLang->currentText();
    // Sharing
    s.importCommentsInline = m_importComments->isChecked();
    s.importFootnotesInline = m_importFootnotes->isChecked();
    s.importIgnoreStyles = m_importIgnoreStyles->isChecked();
    s.plainTextEncoding = m_encoding->currentData().toString();
    s.htmlAsText = m_htmlAsText->isChecked();
    s.docConverter = m_docConverter->currentData().toString();
    {
        QSettings st;
        st.setValue(QStringLiteral("export/format"), m_exportFormat->currentData().toString());
        st.setValue(QStringLiteral("export/comments"), m_exportComments->isChecked());
        st.setValue(QStringLiteral("export/notes"), m_exportNotes->isChecked());
    }
    // Backup
    s.backupOnClose = m_backupClose->isChecked();
    s.backupOnOpen = m_backupOpen->isChecked();
    s.backupOnManualSave = m_backupManual->isChecked();
    s.backupKeep = m_backupKeep->value();
    s.backupDir = m_backupDir->text();
    // Extras
    MindMapPrefs &mp = MindMapPrefs::instance();
    mp.enterEndsEditing = m_mmEnterEnds->isChecked();
    mp.enterCreatesNotes = m_mmEnterCreates->isChecked();
    mp.useSelectedStyle = m_mmUseSelected->isChecked();
    mp.dashedLines = m_mmDashed->isChecked();
    mp.autoSize = m_mmAutoSize->isChecked();
    mp.fadedPercent = m_mmFaded->value();
    mp.newNoteSize = m_mmNoteSize->value();
    mp.newNoteWidth = m_mmNoteWidth->value();
    mp.newNoteShape = m_mmNoteShape->currentData().toInt();
    mp.newNoteBorder = m_mmNoteBorder->value();
    mp.newNoteFill = m_mmFill;
    mp.iconSize = m_mmIconSize->value();
    mp.boardBackground = m_mmBgLight;
    mp.boardBackgroundDark = m_mmBgDark;
    mp.save();
    mindmaps::refreshAll();
    if (m_resetObjectTemplates) {
        objects::resetSchemaForNewProjects();
        m_resetObjectTemplates = false;
    }
    s.freezeSeconds = m_freezeSeconds->value();
    s.keepReports = m_keepReports->value();
    m_keyboard->apply();
    s.notifyChanged();
}

} // namespace qb
