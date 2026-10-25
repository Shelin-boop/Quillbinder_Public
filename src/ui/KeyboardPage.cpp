#include "ui/KeyboardPage.h"
#include "AppInfo.h"
#include "ui/ToolHost.h"
#include <QClipboard>
#include <QGuiApplication>
#include "AppInfo.h"
#include "ui/Commands.h"
#include "text/TextUtil.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace qb {

namespace {
enum { KeyRole = Qt::UserRole + 1 };

QString native(const QKeySequence &s) { return s.toString(QKeySequence::NativeText); }
} // namespace

KeyboardPage::KeyboardPage(QWidget *parent) : QWidget(parent), m_changes(commands::custom())
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    auto *top = new QHBoxLayout;
    m_filter = new QLineEdit(this);
    m_filter->setObjectName(QStringLiteral("shortcutFilter"));
    m_filter->setPlaceholderText(tr("Search for a command or a shortcut, for example “snapshot” or “Ctrl+K”"));
    m_filter->setClearButtonEnabled(true);
    top->addWidget(m_filter, 1);
    lay->addLayout(top);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("shortcutTree"));
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Command"), tr("Shortcut"), tr("Standard")});
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(false);
    lay->addWidget(m_tree, 1);

    auto *row = new QHBoxLayout;
    m_label = new QLabel(this);
    m_label->setMinimumWidth(220);
    row->addWidget(m_label);
    m_edit = new QKeySequenceEdit(this);
    m_edit->setObjectName(QStringLiteral("shortcutEdit"));
    m_edit->setMinimumWidth(170);
    m_edit->setToolTip(tr("Click here and press the keys of the new shortcut"));
    row->addWidget(m_edit);
    m_clear = new QPushButton(tr("No Shortcut"), this);
    m_clear->setObjectName(QStringLiteral("shortcutClear"));
    m_reset = new QPushButton(tr("Standard"), this);
    m_reset->setObjectName(QStringLiteral("shortcutReset"));
    m_reset->setToolTip(tr("Give this command the shortcut the program came with"));
    row->addWidget(m_clear);
    row->addWidget(m_reset);
    row->addStretch(1);
    lay->addLayout(row);

    m_hint = new QLabel(this);
    m_hint->setObjectName(QStringLiteral("shortcutHint"));
    m_hint->setWordWrap(true);
    m_hint->setMinimumHeight(m_hint->fontMetrics().lineSpacing() * 2);
    m_hint->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    lay->addWidget(m_hint);

    // Writing tools that run without the main window (the scratch pad) can be
    // called from every program: the desktop starts "<program> --tool <key>".
    {
        QStringList lines;
        for (const tools::Command &c : toolhost::host().allCommands()) {
            if (!c.standalone)
                continue;
            QString label = c.text;
            label.remove(QLatin1Char('&'));
            lines << QStringLiteral("<b>%1</b>: <code>%2</code>").arg(label.toHtmlEscaped(), toolhost::host().commandLine(c.key).toHtmlEscaped());
        }
        if (!lines.isEmpty()) {
            auto *global = new QLabel(this);
            global->setObjectName(QStringLiteral("globalShortcutHint"));
            global->setWordWrap(true);
            global->setTextInteractionFlags(Qt::TextSelectableByMouse);
            global->setText(tr("<p><b>Shortcuts in every program.</b> The shortcuts above work while %1 has the keyboard. "
                               "For a shortcut that also works in other programs, give the desktop the command below: "
                               "KDE System Settings &gt; Keyboard &gt; Shortcuts &gt; Add New &gt; Command or Script "
                               "(or the entry of the application menu, if %1 was added there with Help &gt; Add to "
                               "Application Menu).</p>")
                                .arg(QLatin1String(QB_APP_NAME))
                            + lines.join(QStringLiteral("<br>")));
            auto *copy = new QPushButton(tr("Copy Command"), this);
            copy->setObjectName(QStringLiteral("copyGlobalCommand"));
            QString first;
            for (const tools::Command &c : toolhost::host().allCommands())
                if (c.standalone && first.isEmpty())
                    first = toolhost::host().commandLine(c.key);
            connect(copy, &QPushButton::clicked, this, [first] { QGuiApplication::clipboard()->setText(first); });
            auto *row2 = new QHBoxLayout;
            row2->addWidget(global, 1);
            row2->addWidget(copy, 0, Qt::AlignTop);
            lay->addLayout(row2);
        }
    }

    auto *bottom = new QHBoxLayout;
    m_resetAll = new QPushButton(tr("All to Standard…"), this);
    m_resetAll->setObjectName(QStringLiteral("shortcutResetAll"));
    auto *imp = new QPushButton(tr("Import…"), this);
    auto *exp = new QPushButton(tr("Export…"), this);
    bottom->addWidget(m_resetAll);
    bottom->addStretch(1);
    bottom->addWidget(imp);
    bottom->addWidget(exp);
    lay->addLayout(bottom);

    connect(m_filter, &QLineEdit::textChanged, this, &KeyboardPage::setFilter);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this] { selectionChanged(); });
    connect(m_edit, &QKeySequenceEdit::editingFinished, this, [this] {
        const QString key = currentKey();
        if (key.isEmpty())
            return;
        // one key combination is enough; a second one typed in a hurry is dropped
        const QKeySequence typed = m_edit->keySequence();
        const QKeySequence seq = typed.isEmpty() ? QKeySequence() : QKeySequence(typed[0]);
        if (seq == shown(key)) {
            selectionChanged();
            return;
        }
        QString other;
        Result r = assign(key, seq, false, &other);
        if (r == Result::Conflict) {
            const commands::Command *c = commands::find(other);
            const commands::Command *mine = commands::find(key);
            const auto answer = QMessageBox::question(
                this, tr("Shortcut in Use"),
                tr("%1 is the shortcut of “%2” (%3).\n\nGive it to “%4” instead? “%2” will then have no shortcut.")
                    .arg(native(seq), c ? c->fullLabel() : other, c ? c->category : QString(), mine ? mine->label() : key),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer == QMessageBox::Yes)
                r = assign(key, seq, true);
        }
        selectionChanged();
        if (r == Result::Unusable) {
            QString why;
            commands::usable(seq, &why);
            m_hint->setText(why);
        }
    });
    connect(m_clear, &QPushButton::clicked, this, [this] {
        if (!currentKey().isEmpty())
            assign(currentKey(), QKeySequence(), false);
        selectionChanged();
    });
    connect(m_reset, &QPushButton::clicked, this, [this] {
        const QString key = currentKey();
        const commands::Command *c = commands::find(key);
        if (!c)
            return;
        // the standard shortcut may have gone to another command in the meantime
        QMap<QString, QString> without = m_changes;
        without.remove(key);
        const QString other = commands::conflict(key, c->standard, without);
        if (!other.isEmpty()) {
            const commands::Command *o = commands::find(other);
            if (QMessageBox::question(this, tr("Shortcut in Use"),
                                      tr("%1 is now the shortcut of “%2”. Take it away from there?")
                                          .arg(native(c->standard), o ? o->fullLabel() : other))
                != QMessageBox::Yes)
                return;
        }
        resetOne(key);
        selectionChanged();
    });
    connect(m_resetAll, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Keyboard Shortcuts"),
                                  tr("Give every command the shortcut the program came with? Your changes (%1) will be lost.")
                                      .arg(changes()))
            == QMessageBox::Yes)
            resetAll();
    });
    connect(exp, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, tr("Export Keyboard Shortcuts"),
                                                       QDir::homePath() + QLatin1String("/shortcuts.qbkeys"),
                                                       tr("%1 shortcuts (*.qbkeys)").arg(QLatin1String(QB_APP_NAME)));
        QString err;
        if (!f.isEmpty() && !exportTo(f, &err))
            QMessageBox::warning(this, tr("Export Keyboard Shortcuts"), err);
    });
    connect(imp, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Import Keyboard Shortcuts"), QDir::homePath(),
                                                       tr("%1 shortcuts (*.qbkeys)").arg(QLatin1String(QB_APP_NAME)));
        QString err;
        if (!f.isEmpty() && !importFrom(f, &err))
            QMessageBox::warning(this, tr("Import Keyboard Shortcuts"), err);
    });

    rebuild();
}

void KeyboardPage::rebuild()
{
    m_tree->clear();
    QHash<QString, QTreeWidgetItem *> groups;
    for (const commands::Command &c : commands::all()) {
        QTreeWidgetItem *group = groups.value(c.category);
        if (!group) {
            group = new QTreeWidgetItem(m_tree, {c.category});
            QFont f = group->font(0);
            f.setBold(true);
            group->setFont(0, f);
            group->setFlags(Qt::ItemIsEnabled);
            group->setFirstColumnSpanned(true);
            groups.insert(c.category, group);
        }
        auto *it = new QTreeWidgetItem(group);
        it->setData(0, KeyRole, c.key);
        it->setText(0, c.fullLabel());
        it->setIcon(0, commands::listIcon(c.key));
        refreshRow(it);
    }
    m_tree->expandAll();
    selectionChanged();
}

void KeyboardPage::refreshRow(QTreeWidgetItem *item)
{
    const commands::Command *c = commands::find(item->data(0, KeyRole).toString());
    if (!c)
        return;
    const QKeySequence now = commands::effective(*c, m_changes);
    item->setText(1, native(now));
    item->setText(2, native(c->standard));
    const bool changed = now != c->standard;
    QFont f = item->font(1);
    f.setBold(changed);
    item->setFont(1, f);
    item->setToolTip(1, changed ? tr("Changed by you") : QString());
}

void KeyboardPage::refreshAll()
{
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g)
        for (int i = 0; i < m_tree->topLevelItem(g)->childCount(); ++i)
            refreshRow(m_tree->topLevelItem(g)->child(i));
    setFilter(m_filter->text());
    selectionChanged();
}

QString KeyboardPage::currentKey() const
{
    return m_tree->currentItem() ? m_tree->currentItem()->data(0, KeyRole).toString() : QString();
}

void KeyboardPage::selectionChanged()
{
    const commands::Command *c = commands::find(currentKey());
    m_edit->setEnabled(c != nullptr);
    m_clear->setEnabled(c && !shown(c->key).isEmpty());
    m_reset->setEnabled(c && shown(c->key) != c->standard);
    m_resetAll->setEnabled(changes() > 0);
    QSignalBlocker block(m_edit);
    if (!c) {
        m_label->setText(tr("Choose a command in the list."));
        m_edit->clear();
        m_hint->clear();
        return;
    }
    m_label->setText(tr("Shortcut of “%1”:").arg(c->label()));
    m_edit->setKeySequence(shown(c->key));
    const int n = changes();
    m_hint->setText(n ? tr("%1 changed by you (shown in bold).").arg(text::plural(n, tr("shortcut"), tr("shortcuts"))) : QString());
}

QKeySequence KeyboardPage::shown(const QString &key) const
{
    const commands::Command *c = commands::find(key);
    return c ? commands::effective(*c, m_changes) : QKeySequence();
}

int KeyboardPage::changes() const
{
    int n = 0;
    for (const commands::Command &c : commands::all())
        if (commands::effective(c, m_changes) != c.standard)
            ++n;
    return n;
}

KeyboardPage::Result KeyboardPage::assign(const QString &key, const QKeySequence &seq, bool takeOver, QString *other)
{
    const commands::Command *c = commands::find(key);
    if (!c || !commands::usable(seq))
        return Result::Unusable;
    const QString holder = commands::conflict(key, seq, m_changes);
    if (!holder.isEmpty()) {
        if (other)
            *other = holder;
        if (!takeOver)
            return Result::Conflict;
        m_changes.insert(holder, QString());
    }
    if (seq == c->standard)
        m_changes.remove(key);
    else
        m_changes.insert(key, seq.toString(QKeySequence::PortableText));
    refreshAll();
    return Result::Done;
}

void KeyboardPage::resetOne(const QString &key)
{
    const commands::Command *c = commands::find(key);
    if (!c)
        return;
    m_changes.remove(key);
    const QString holder = commands::conflict(key, c->standard, m_changes);
    if (!holder.isEmpty())
        m_changes.insert(holder, QString());
    refreshAll();
}

void KeyboardPage::resetAll()
{
    // entries of commands that are not listed now (styles of other projects) go too
    m_changes.clear();
    refreshAll();
}

void KeyboardPage::select(const QString &key)
{
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g)
        for (int i = 0; i < m_tree->topLevelItem(g)->childCount(); ++i) {
            QTreeWidgetItem *it = m_tree->topLevelItem(g)->child(i);
            if (it->data(0, KeyRole).toString() == key) {
                m_tree->setCurrentItem(it);
                m_tree->scrollToItem(it);
                return;
            }
        }
}

void KeyboardPage::setFilter(const QString &text)
{
    if (m_filter->text() != text) {
        QSignalBlocker block(m_filter);
        m_filter->setText(text);
    }
    const QString needle = text.trimmed();
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_tree->topLevelItem(g);
        int visible = 0;
        for (int i = 0; i < group->childCount(); ++i) {
            QTreeWidgetItem *it = group->child(i);
            const bool match = needle.isEmpty() || it->text(0).contains(needle, Qt::CaseInsensitive)
                               || it->text(1).contains(needle, Qt::CaseInsensitive)
                               || group->text(0).contains(needle, Qt::CaseInsensitive);
            it->setHidden(!match);
            visible += match;
        }
        group->setHidden(visible == 0);
    }
}

int KeyboardPage::visibleCommands() const
{
    int n = 0;
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g)
        for (int i = 0; i < m_tree->topLevelItem(g)->childCount(); ++i)
            n += !m_tree->topLevelItem(g)->child(i)->isHidden();
    return n;
}

bool KeyboardPage::exportTo(const QString &file, QString *error) const
{
    return commands::exportTo(file, m_changes, error);
}

bool KeyboardPage::importFrom(const QString &file, QString *error)
{
    QMap<QString, QString> in;
    if (!commands::importFrom(file, &in, error))
        return false;
    // a file put together by hand may give one shortcut to two commands: the first one keeps it
    QSet<QString> seen;
    for (const commands::Command &c : commands::all()) {
        const QString seq = commands::effective(c, in).toString(QKeySequence::PortableText);
        if (seq.isEmpty())
            continue;
        if (seen.contains(seq))
            in.insert(c.key, QString());
        seen.insert(seq);
    }
    m_changes = in;
    refreshAll();
    return true;
}

void KeyboardPage::apply() { commands::setCustom(m_changes); }

} // namespace qb
