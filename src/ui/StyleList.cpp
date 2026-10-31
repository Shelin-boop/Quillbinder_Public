#include "ui/StyleList.h"

#include "core/ItemIcons.h"
#include "core/Project.h"
#include "text/Styles.h"
#include "ui/StyleDialogs.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <memory>

namespace qb {

namespace {
const int kNameRole = Qt::UserRole;      // style name; "" = No Style
const int kSeparatorRole = Qt::UserRole + 1;

QString keyOf(const QString &name) { return name.isEmpty() ? styles::noStyle() : name; }

QList<stylelist::Entry> baseEntries(const Formatting &f)
{
    QList<stylelist::Entry> out;
    out << stylelist::Entry{QString(), QObject::tr("No Style"), false, true};
    const StyleSheet &sheet = styles::sheet(f);
    for (int pass = 0; pass < 2; ++pass)
        for (const Style &s : sheet.styles)
            if (s.character == (pass == 1))
                out << stylelist::Entry{s.name, styles::marker(s) + s.name, s.character, true};
    return out;
}

void closeAllPopups()
{
    for (int i = 0; i < 10; ++i) {
        QWidget *w = QApplication::activePopupWidget();
        if (!w)
            break;
        w->close();
    }
}
} // namespace

namespace stylelist {

QList<Entry> all(const Project *p)
{
    const QList<Entry> base = baseEntries(p ? p->formatting : Formatting());
    if (!p)
        return base;
    QList<Entry> out;
    QSet<QString> used;
    for (const QString &n : p->styleListOrder) {
        const QString name = n == styles::noStyle() ? QString() : n;
        if (used.contains(name))
            continue;
        for (const Entry &e : base)
            if (e.name == name) {
                out << e;
                used.insert(name);
                break;
            }
    }
    // styles the chosen order does not name (new ones): at the end, in the order of the sheet
    for (const Entry &e : base)
        if (!used.contains(e.name))
            out << e;
    for (Entry &e : out)
        e.shown = !p->styleListHidden.contains(keyOf(e.name));
    return out;
}

QList<Entry> shown(const Project *p)
{
    QList<Entry> out;
    for (const Entry &e : all(p))
        if (e.shown)
            out << e;
    return out;
}

QListWidget *addTo(QMenu *menu, const QList<Entry> &entries, const Formatting &f, const QStringList &current,
                   const std::function<void(const QString &)> &apply)
{
    // a line where character styles follow paragraph styles (or the other way round)
    auto kindChanges = [&](int i) { return i > 0 && entries[i].character != entries[i - 1].character; };
    if (entries.size() <= kVisibleRows) {
        for (int i = 0; i < entries.size(); ++i) {
            if (kindChanges(i))
                menu->addSeparator();
            const Entry &e = entries[i];
            const QString name = e.name;
            QAction *ac = menu->addAction(e.text, menu, [apply, name] { apply(name); });
            ac->setData(name);
            const bool cur = current.contains(name);
            ac->setCheckable(cur);
            ac->setChecked(cur);
        }
        return nullptr;
    }
    auto *list = new QListWidget;
    list->setObjectName(QStringLiteral("styleListRows"));
    list->setFrameShape(QFrame::NoFrame);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    list->setMouseTracking(true);
    list->setUniformItemSizes(false);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setAutoFillBackground(false);
    list->viewport()->setAutoFillBackground(false);
    list->setIconSize(QSize(16, 16));
    QPixmap empty(16, 16);
    empty.fill(Qt::transparent);
    const QIcon blank(empty);
    const StyleSheet &sheet = styles::sheet(f);
    QListWidgetItem *first = nullptr;
    for (int i = 0; i < entries.size(); ++i) {
        if (kindChanges(i)) {
            auto *sep = new QListWidgetItem(list);
            sep->setFlags(Qt::NoItemFlags);
            sep->setData(kSeparatorRole, true);
            sep->setSizeHint(QSize(0, 9));
            auto *line = new QFrame;
            line->setFrameShape(QFrame::HLine);
            line->setFrameShadow(QFrame::Sunken);
            list->setItemWidget(sep, line);
        }
        const Entry &e = entries[i];
        auto *it = new QListWidgetItem(e.text, list);
        it->setData(kNameRole, e.name);
        if (const Style *s = e.name.isEmpty() ? nullptr : sheet.find(e.name)) {
            it->setFont(styleui::rowFont(*s, list->font()));
            it->setToolTip(styleui::describe(*s));
        }
        // the styles at the cursor carry a mark; the others an empty one, so the names line up
        if (current.contains(e.name)) {
            it->setIcon(icons::action(QStringLiteral("go-next")));
            if (!first)
                first = it;
        } else {
            it->setIcon(blank);
        }
    }
    // ten style rows high, as wide as the longest name
    int h = 0, rows = 0;
    for (int i = 0; i < list->count() && rows < kVisibleRows; ++i) {
        h += qMax(list->sizeHintForRow(i), list->item(i)->sizeHint().height());
        if (!list->item(i)->data(kSeparatorRole).toBool())
            ++rows;
    }
    list->setFixedHeight(h + 2 * list->frameWidth() + 2);
    list->setMinimumWidth(qMax(180, list->sizeHintForColumn(0) + list->verticalScrollBar()->sizeHint().width() + 24));
    if (first) {
        list->setCurrentItem(first);
        list->scrollToItem(first, QAbstractItemView::PositionAtCenter);
    }
    auto done = std::make_shared<bool>(false);
    auto choose = [apply, done](QListWidgetItem *it) {
        if (!it || *done || !(it->flags() & Qt::ItemIsEnabled) || it->data(kSeparatorRole).toBool())
            return;
        *done = true;
        const QString name = it->data(kNameRole).toString();
        closeAllPopups();
        apply(name);
    };
    QObject::connect(list, &QListWidget::itemClicked, list, choose);
    QObject::connect(list, &QListWidget::itemActivated, list, choose);
    QObject::connect(list, &QListWidget::itemEntered, list, [list](QListWidgetItem *it) {
        if (it && (it->flags() & Qt::ItemIsEnabled))
            list->setCurrentItem(it);
    });
    auto *wa = new QWidgetAction(menu);
    wa->setObjectName(QStringLiteral("styleListAction"));
    wa->setDefaultWidget(list);
    menu->addAction(wa);
    return list;
}

} // namespace stylelist

// ------------------------------------------------------------------ choosing the list

StyleListDialog::StyleListDialog(const Project *p, QWidget *parent) : QDialog(parent), m_project(p)
{
    setObjectName(QStringLiteral("styleListDialog"));
    setWindowTitle(tr("Customize Style List"));
    auto *v = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("The ticked entries show in the style list of the format bar and of the right-click "
                               "menus, in this order (drag them, or use the arrows). With more than %1 the list "
                               "scrolls. Format ▸ Style always shows all styles.")
                                .arg(stylelist::kVisibleRows),
                            this);
    hint->setWordWrap(true);
    v->addWidget(hint);
    auto *row = new QHBoxLayout;
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("styleListEntries"));
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    row->addWidget(m_list, 1);
    auto *side = new QVBoxLayout;
    auto button = [&](const char *name, const QString &icon, const QString &tip) {
        auto *b = new QPushButton(this);
        b->setObjectName(QLatin1String(name));
        b->setIcon(icons::action(icon));
        b->setToolTip(tip);
        side->addWidget(b);
        return b;
    };
    QPushButton *up = button("styleListUp", QStringLiteral("go-up"), tr("Move Up"));
    QPushButton *down = button("styleListDown", QStringLiteral("go-down"), tr("Move Down"));
    side->addStretch(1);
    row->addLayout(side);
    v->addLayout(row, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton *reset = buttons->addButton(tr("All, in the Order of the Styles"), QDialogButtonBox::ResetRole);
    reset->setObjectName(QStringLiteral("styleListReset"));
    v->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(up, &QPushButton::clicked, this, [this] { move(-1); });
    connect(down, &QPushButton::clicked, this, [this] { move(1); });
    connect(reset, &QPushButton::clicked, this,
            [this] { fill(baseEntries(m_project ? m_project->formatting : Formatting())); });
    fill(stylelist::all(p));
    resize(360, 460);
}

void StyleListDialog::fill(const QList<stylelist::Entry> &entries)
{
    m_list->clear();
    const StyleSheet &sheet = styles::sheet(m_project ? m_project->formatting : Formatting());
    for (const stylelist::Entry &e : entries) {
        auto *it = new QListWidgetItem(e.text, m_list);
        it->setData(kNameRole, keyOf(e.name));
        it->setFlags((it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled) & ~Qt::ItemIsDropEnabled);
        it->setCheckState(e.shown ? Qt::Checked : Qt::Unchecked);
        if (const Style *s = e.name.isEmpty() ? nullptr : sheet.find(e.name))
            it->setFont(styleui::rowFont(*s, m_list->font()));
    }
    if (m_list->count())
        m_list->setCurrentRow(0);
}

void StyleListDialog::move(int delta)
{
    const int r = m_list->currentRow();
    const int to = r + delta;
    if (r < 0 || to < 0 || to >= m_list->count())
        return;
    QListWidgetItem *it = m_list->takeItem(r);
    m_list->insertItem(to, it);
    m_list->setCurrentRow(to);
}

QStringList StyleListDialog::order() const
{
    QStringList out;
    for (int i = 0; i < m_list->count(); ++i)
        out << m_list->item(i)->data(kNameRole).toString();
    return out;
}

QStringList StyleListDialog::hidden() const
{
    QStringList out;
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->checkState() != Qt::Checked)
            out << m_list->item(i)->data(kNameRole).toString();
    return out;
}

// ------------------------------------------------------------------ the format bar's list

void StyleCombo::showPopup()
{
    if (m_popup)
        return;
    auto *menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("styleListPopup"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    if (fillPopup)
        fillPopup(menu);
    m_popup = menu;
    connect(menu, &QMenu::aboutToHide, this, [this] { m_popup = nullptr; });
    menu->setMinimumWidth(width());
    menu->popup(mapToGlobal(QPoint(0, height())));
}

void StyleCombo::hidePopup()
{
    if (m_popup)
        m_popup->close();
    QComboBox::hidePopup();
}

} // namespace qb
