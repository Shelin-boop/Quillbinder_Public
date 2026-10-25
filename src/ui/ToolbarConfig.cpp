#include "ui/ToolbarConfig.h"
#include "core/ItemIcons.h"
#include "ui/AppSettings.h"
#include "ui/Commands.h"

#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace qb {
namespace toolbars {

const QString Separator = QStringLiteral("|");
const QString Spacer = QStringLiteral("<>");

QList<Special> specials(Bar bar)
{
    if (bar == Bar::Main)
        return {
            {QStringLiteral("view"), QObject::tr("View (binder, collections, inspector, layouts)"), QStringLiteral("view-sidetree")},
            {QStringLiteral("add"), QObject::tr("Add (new text, folder, from template, import)"), QStringLiteral("document-new")},
            {QStringLiteral("searchMenu"), QObject::tr("Search (project search and the other search tools)"), QStringLiteral("search")},
            {QStringLiteral("search"), QObject::tr("Quick Search field with target bars"), QStringLiteral("edit-find")},
            {QStringLiteral("viewModes"), QObject::tr("View Mode (document, corkboard, outliner)"), QStringLiteral("view-list-icons")},
            {QStringLiteral("bookmarks"), QObject::tr("Project Bookmarks"), QStringLiteral("bookmarks")},
            {QStringLiteral("notes"), QObject::tr("Comments and Footnotes"), QStringLiteral("edit-comment")},
            {QStringLiteral("layoutsButton"), QObject::tr("Layouts"), QStringLiteral("view-split-left-right")},
        };
    return {
        {QStringLiteral("styleCombo"), QObject::tr("Style list"), QStringLiteral("format-text-bold")},
        {QStringLiteral("fontCombo"), QObject::tr("Font list"), QStringLiteral("font")},
        {QStringLiteral("sizeSpin"), QObject::tr("Font size"), QStringLiteral("format-font-size-more")},
        {QStringLiteral("lineCombo"), QObject::tr("Line spacing list"), QStringLiteral("format-justify-fill")},
    };
}

bool isSpecial(const QString &id)
{
    for (Bar b : {Bar::Main, Bar::Format})
        for (const Special &s : specials(b))
            if (s.id == id)
                return true;
    return false;
}

QStringList standard(Bar bar)
{
    if (bar == Bar::Main)
        return {QStringLiteral("view"), Separator, QStringLiteral("add"), QStringLiteral("newFolder"), QStringLiteral("trash"),
                Spacer, QStringLiteral("searchMenu"), QStringLiteral("search"), QStringLiteral("viewModes"), Spacer,
                QStringLiteral("bookmarks"), QStringLiteral("quickRef"), QStringLiteral("notes"), Separator,
                QStringLiteral("compile"), Separator, QStringLiteral("composition"), QStringLiteral("showInspector")};
    return {QStringLiteral("styleCombo"), QStringLiteral("fontCombo"), QStringLiteral("sizeSpin"), QStringLiteral("lineCombo"),
            QStringLiteral("defaultTextFormatting"), QStringLiteral("showFonts"), Separator,
            QStringLiteral("bold"), QStringLiteral("italic"), QStringLiteral("underline"), QStringLiteral("strike"),
            QStringLiteral("highlight"), QStringLiteral("textColor"), Separator,
            QStringLiteral("alignLeft"), QStringLiteral("alignCenter"), QStringLiteral("alignRight"), QStringLiteral("alignJustify"),
            Separator, QStringLiteral("bullets"), QStringLiteral("numbers"), Separator,
            QStringLiteral("comment"), QStringLiteral("footnote"), QStringLiteral("annotation"), QStringLiteral("image")};
}

QStringList configured(Bar bar)
{
    const AppSettings &s = AppSettings::instance();
    const QStringList own = bar == Bar::Main ? s.toolbarMain : s.toolbarFormat;
    return own.isEmpty() ? standard(bar) : own;
}

QString label(const QString &id)
{
    if (id == Separator)
        return QObject::tr("――  Separator  ――");
    if (id == Spacer)
        return QObject::tr("←  Flexible Space  →");
    for (Bar b : {Bar::Main, Bar::Format})
        for (const Special &s : specials(b))
            if (s.id == id)
                return s.label;
    if (const commands::Command *c = commands::find(id))
        return c->label();
    return QString();
}

QIcon icon(const QString &id)
{
    for (Bar b : {Bar::Main, Bar::Format})
        for (const Special &s : specials(b))
            if (s.id == id)
                return icons::action(s.icon);
    return commands::listIcon(id);
}

} // namespace toolbars

// ------------------------------------------------------------------ dialog

ToolbarDialog::ToolbarDialog(QWidget *parent, toolbars::Bar bar) : QDialog(parent)
{
    setWindowTitle(tr("Customize Toolbars"));
    resize(820, 560);
    const AppSettings &s = AppSettings::instance();
    m_lists[0] = toolbars::configured(toolbars::Bar::Main);
    m_lists[1] = toolbars::configured(toolbars::Bar::Format);

    auto *lay = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Toolbar:"), this));
    m_barCombo = new QComboBox(this);
    m_barCombo->setObjectName(QStringLiteral("toolbarBar"));
    m_barCombo->addItem(tr("Main Toolbar"));
    m_barCombo->addItem(tr("Format Bar"));
    top->addWidget(m_barCombo);
    top->addStretch(1);
    lay->addLayout(top);

    auto *grid = new QGridLayout;
    grid->addWidget(new QLabel(tr("Available commands"), this), 0, 0);
    grid->addWidget(new QLabel(tr("In the toolbar (from left to right)"), this), 0, 2);
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Search commands"));
    m_filter->setClearButtonEnabled(true);
    grid->addWidget(m_filter, 1, 0);
    m_available = new QListWidget(this);
    m_available->setObjectName(QStringLiteral("toolbarAvailable"));
    grid->addWidget(m_available, 2, 0);
    m_current = new QListWidget(this);
    m_current->setObjectName(QStringLiteral("toolbarCurrent"));
    m_current->setDragDropMode(QAbstractItemView::InternalMove);
    grid->addWidget(m_current, 1, 2, 2, 1);

    auto *mid = new QVBoxLayout;
    auto button = [this](const QString &icon, const QString &tip, const QString &name) {
        auto *b = new QToolButton(this);
        b->setIcon(icons::action(icon));
        b->setToolTip(tip);
        b->setObjectName(name);
        b->setIconSize(QSize(22, 22));
        return b;
    };
    auto *add = button(QStringLiteral("arrow-right"), tr("Add to the toolbar"), QStringLiteral("toolbarAdd"));
    auto *remove = button(QStringLiteral("arrow-left"), tr("Remove from the toolbar"), QStringLiteral("toolbarRemove"));
    auto *up = button(QStringLiteral("arrow-up"), tr("Move to the left"), QStringLiteral("toolbarUp"));
    auto *down = button(QStringLiteral("arrow-down"), tr("Move to the right"), QStringLiteral("toolbarDown"));
    mid->addStretch(1);
    for (QToolButton *b : {add, remove})
        mid->addWidget(b);
    mid->addSpacing(16);
    for (QToolButton *b : {up, down})
        mid->addWidget(b);
    mid->addStretch(1);
    grid->addLayout(mid, 2, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(2, 1);
    lay->addLayout(grid, 1);

    auto *opts = new QHBoxLayout;
    opts->addWidget(new QLabel(tr("Icons of the main toolbar:"), this));
    m_iconSize = new QComboBox(this);
    m_iconSize->setObjectName(QStringLiteral("toolbarIconSize"));
    m_iconSize->addItem(tr("Small"), 16);
    m_iconSize->addItem(tr("Medium"), 22);
    m_iconSize->addItem(tr("Large"), 32);
    m_iconSize->setCurrentIndex(qMax(0, m_iconSize->findData(s.toolbarIconSize)));
    opts->addWidget(m_iconSize);
    m_style = new QComboBox(this);
    m_style->setObjectName(QStringLiteral("toolbarStyle"));
    m_style->addItem(tr("Icons only"), QStringLiteral("icons"));
    m_style->addItem(tr("Text beside the icons"), QStringLiteral("beside"));
    m_style->addItem(tr("Text under the icons"), QStringLiteral("under"));
    m_style->setCurrentIndex(qMax(0, m_style->findData(s.toolbarStyle)));
    opts->addWidget(m_style);
    opts->addStretch(1);
    auto *reset = new QPushButton(tr("Standard Set"), this);
    reset->setObjectName(QStringLiteral("toolbarReset"));
    reset->setToolTip(tr("Put the buttons the program came with back into this toolbar"));
    opts->addWidget(reset);
    lay->addLayout(opts);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &ToolbarDialog::apply);

    connect(m_barCombo, qOverload<int>(&QComboBox::activated), this, [this](int i) { setBar(toolbars::Bar(i)); });
    connect(m_filter, &QLineEdit::textChanged, this, [this] { fillAvailable(); });
    auto addSelected = [this] {
        if (QListWidgetItem *it = m_available->currentItem())
            addItem(it->data(Qt::UserRole).toString(), m_current->currentRow() < 0 ? -1 : m_current->currentRow() + 1);
    };
    connect(add, &QToolButton::clicked, this, addSelected);
    connect(m_available, &QListWidget::itemDoubleClicked, this, addSelected);
    auto removeSelected = [this] {
        const int row = m_current->currentRow();
        if (row < 0)
            return;
        delete m_current->takeItem(row);
        m_current->setCurrentRow(qMin(row, m_current->count() - 1));
        fillAvailable();
    };
    connect(remove, &QToolButton::clicked, this, removeSelected);
    connect(m_current, &QListWidget::itemDoubleClicked, this, removeSelected);
    connect(up, &QToolButton::clicked, this, [this] { moveItem(m_current->currentRow(), -1); });
    connect(down, &QToolButton::clicked, this, [this] { moveItem(m_current->currentRow(), 1); });
    connect(reset, &QPushButton::clicked, this, &ToolbarDialog::resetToStandard);

    m_bar = bar;
    m_barCombo->setCurrentIndex(int(bar));
    fillCurrent(m_lists[int(m_bar)]);
    fillAvailable();
}

QStringList ToolbarDialog::items() const
{
    QStringList ids;
    for (int i = 0; i < m_current->count(); ++i)
        ids << m_current->item(i)->data(Qt::UserRole).toString();
    return ids;
}

void ToolbarDialog::storeCurrent() { m_lists[int(m_bar)] = items(); }

void ToolbarDialog::setBar(toolbars::Bar bar)
{
    storeCurrent();
    m_bar = bar;
    m_barCombo->setCurrentIndex(int(bar));
    fillCurrent(m_lists[int(bar)]);
    fillAvailable();
}

static QListWidgetItem *makeItem(const QString &id)
{
    const QString label = toolbars::label(id);
    if (label.isEmpty())
        return nullptr; // a command this version does not have
    auto *it = new QListWidgetItem(toolbars::icon(id), label);
    it->setData(Qt::UserRole, id);
    if (const commands::Command *c = commands::find(id))
        it->setToolTip(c->category + QStringLiteral(" ▸ ") + c->fullLabel());
    if (id == toolbars::Separator || id == toolbars::Spacer)
        it->setForeground(QPalette().color(QPalette::Disabled, QPalette::Text));
    return it;
}

void ToolbarDialog::fillCurrent(const QStringList &ids)
{
    m_current->clear();
    for (const QString &id : ids)
        if (QListWidgetItem *it = makeItem(id))
            m_current->addItem(it);
}

void ToolbarDialog::fillAvailable()
{
    const QString keep = m_available->currentItem() ? m_available->currentItem()->data(Qt::UserRole).toString() : QString();
    const QString filter = m_filter->text().trimmed();
    const QStringList used = items();
    m_available->clear();
    auto offer = [&](const QString &id, const QString &extra = QString()) {
        const bool repeatable = id == toolbars::Separator || id == toolbars::Spacer;
        if (!repeatable && used.contains(id))
            return;
        QListWidgetItem *it = makeItem(id);
        if (!it)
            return;
        if (!filter.isEmpty() && !it->text().contains(filter, Qt::CaseInsensitive) && !extra.contains(filter, Qt::CaseInsensitive)) {
            delete it;
            return;
        }
        m_available->addItem(it);
        if (id == keep)
            m_available->setCurrentItem(it);
    };
    offer(toolbars::Separator);
    offer(toolbars::Spacer);
    for (const toolbars::Special &s : toolbars::specials(m_bar))
        offer(s.id);
    for (const commands::Command &c : commands::all())
        if (!c.dynamic)
            offer(c.key, c.category + QLatin1Char(' ') + c.path);
}

bool ToolbarDialog::addItem(const QString &id, int row)
{
    const bool repeatable = id == toolbars::Separator || id == toolbars::Spacer;
    if (!repeatable && items().contains(id))
        return false;
    // styles and layouts come and go with projects and files: not for a toolbar
    if (id.contains(QLatin1Char(':')))
        return false;
    // the controls of one bar cannot move to the other one
    if (toolbars::isSpecial(id)) {
        bool mine = false;
        for (const toolbars::Special &s : toolbars::specials(m_bar))
            mine = mine || s.id == id;
        if (!mine)
            return false;
    }
    QListWidgetItem *it = makeItem(id);
    if (!it)
        return false;
    if (row < 0 || row > m_current->count())
        row = m_current->count();
    m_current->insertItem(row, it);
    m_current->setCurrentItem(it);
    fillAvailable();
    return true;
}

bool ToolbarDialog::removeItem(const QString &id)
{
    for (int i = 0; i < m_current->count(); ++i)
        if (m_current->item(i)->data(Qt::UserRole).toString() == id) {
            delete m_current->takeItem(i);
            fillAvailable();
            return true;
        }
    return false;
}

bool ToolbarDialog::moveItem(int row, int delta)
{
    const int to = row + delta;
    if (row < 0 || row >= m_current->count() || to < 0 || to >= m_current->count())
        return false;
    QListWidgetItem *it = m_current->takeItem(row);
    m_current->insertItem(to, it);
    m_current->setCurrentItem(it);
    return true;
}

void ToolbarDialog::resetToStandard()
{
    fillCurrent(toolbars::standard(m_bar));
    fillAvailable();
}

void ToolbarDialog::apply()
{
    storeCurrent();
    AppSettings &s = AppSettings::instance();
    // the standard set is stored as "nothing of one's own", so later versions can extend it
    s.toolbarMain = m_lists[0] == toolbars::standard(toolbars::Bar::Main) ? QStringList() : m_lists[0];
    s.toolbarFormat = m_lists[1] == toolbars::standard(toolbars::Bar::Format) ? QStringList() : m_lists[1];
    s.toolbarIconSize = m_iconSize->currentData().toInt();
    s.toolbarStyle = m_style->currentData().toString();
    s.notifyChanged();
}

void ToolbarDialog::accept()
{
    apply();
    QDialog::accept();
}

} // namespace qb
