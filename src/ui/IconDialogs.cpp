#include "ui/IconDialogs.h"
#include <QMenu>
#include "core/IconCatalog.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "ui/MindMapUi.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace qb {

void fillIconChoiceMenu(QMenu *menu, const Project *project, const std::function<void(const QString &)> &chosen)
{
    auto entry = [&](QMenu *m, const QString &id, const QString &text) {
        QAction *a = m->addAction(icons::iconFor(project, id), QString(text).replace(QLatin1Char('&'), QLatin1String("&&")));
        a->setData(id);
        QObject::connect(a, &QAction::triggered, menu, [chosen, id] { chosen(id); });
    };
    // the groups in the order of the binder's menu (Book, Characters, Flag ...)
    for (const QString &g : icons::groups()) {
        QMenu *sub = menu->addMenu(icons::get(icons::groupIcon(g)), QObject::tr(g.toUtf8().constData()));
        sub->setObjectName(QStringLiteral("iconChoice_") + g);
        for (const icons::CatalogEntry &e : icons::inGroup(g))
            entry(sub, e.id, e.label);
    }
    menu->addSeparator();
    for (const icons::CatalogEntry &e : icons::inGroup(QString()))
        entry(menu, e.id, e.label);
    const QStringList own = icons::customNames(project);
    if (!own.isEmpty()) {
        menu->addSeparator();
        QMenu *sub = menu->addMenu(icons::get(QStringLiteral("qb-proofread")), QObject::tr("&Project Icons"));
        sub->setObjectName(QStringLiteral("iconChoice_Project"));
        for (const QString &n : own)
            entry(sub, QStringLiteral("custom:") + n, n);
    }
}

QStringList standardIconBar()
{
    return {QStringLiteral("qb-book-green"), QStringLiteral("qb-proofread"),   QStringLiteral("qb-checked"),
            QStringLiteral("qb-unchecked"),  QStringLiteral("qb-question"),    QStringLiteral("qb-flag-red"),
            QStringLiteral("qb-notepad-yellow"), QStringLiteral("qb-character-sheet"), QStringLiteral("qb-location-sheet"),
            QStringLiteral("qb-warning")};
}

// ------------------------------------------------------------------ icon from text

IconFromTextDialog::IconFromTextDialog(Project *project, QWidget *parent) : QDialog(parent), m_project(project)
{
    setWindowTitle(tr("Icon from Text"));
    auto *lay = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_text = new QLineEdit(this);
    m_text->setObjectName(QStringLiteral("iconText"));
    m_text->setMaxLength(3);
    m_text->setPlaceholderText(tr("Up to 3 characters, e.g. PR, Ch1, A"));
    form->addRow(tr("Text:"), m_text);
    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("iconName"));
    m_name->setPlaceholderText(tr("Name of the icon (the text when left empty)"));
    form->addRow(tr("Name:"), m_name);
    m_box = new ColorWell(tr("White"), this);
    m_box->setColor(Qt::white);
    form->addRow(tr("Box:"), m_box);
    m_ink = new ColorWell(tr("Black"), this);
    m_ink->setColor(QColor(26, 26, 26));
    form->addRow(tr("Letters:"), m_ink);
    m_border = new ColorWell(tr("No Border"), this);
    m_border->setColor(QColor(58, 58, 58));
    form->addRow(tr("Border:"), m_border);
    lay->addLayout(form);
    m_preview = new QLabel(this);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(80);
    lay->addWidget(m_preview);
    auto *hint = new QLabel(tr("A white box lets the label color shine through (View > Use Label Color In > Icons)."), this);
    hint->setWordWrap(true);
    lay->addWidget(hint);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &IconFromTextDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    connect(m_text, &QLineEdit::textChanged, this, &IconFromTextDialog::refresh);
    for (ColorWell *w : {m_box, m_ink, m_border})
        connect(w, &ColorWell::colorChosen, this, &IconFromTextDialog::refresh);
    refresh();
}

void IconFromTextDialog::refresh()
{
    const QColor border = m_border->color().isValid() ? m_border->color() : QColor(0, 0, 0, 0);
    const QImage big = icons::textIcon(m_text->text().isEmpty() ? QStringLiteral("?") : m_text->text(),
                                       m_box->color().isValid() ? m_box->color() : Qt::white,
                                       m_ink->color().isValid() ? m_ink->color() : Qt::black, border, 64);
    const QImage small = icons::textIcon(m_text->text().isEmpty() ? QStringLiteral("?") : m_text->text(),
                                         m_box->color().isValid() ? m_box->color() : Qt::white,
                                         m_ink->color().isValid() ? m_ink->color() : Qt::black, border, 18);
    QPixmap pm(120, 70);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.drawImage(0, 3, big);
    p.drawImage(90, 26, small);
    p.end();
    m_preview->setPixmap(pm);
}

void IconFromTextDialog::accept()
{
    const QString text = m_text->text().trimmed();
    if (text.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("Type the characters of the icon first."));
        return;
    }
    const QColor border = m_border->color().isValid() ? m_border->color() : QColor(0, 0, 0, 0);
    const QImage img = icons::textIcon(text, m_box->color().isValid() ? m_box->color() : Qt::white,
                                       m_ink->color().isValid() ? m_ink->color() : Qt::black, border, 64);
    QString err;
    m_created = icons::addCustom(m_project, m_name->text().trimmed().isEmpty() ? text : m_name->text().trimmed(), img, &err);
    if (m_created.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), err);
        return;
    }
    QDialog::accept();
}

// ------------------------------------------------------------------ manage icons

ManageIconsDialog::ManageIconsDialog(Project *project, QWidget *parent) : QDialog(parent), m_project(project)
{
    setWindowTitle(tr("Manage Icons"));
    resize(460, 420);
    auto *lay = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("The icons of this project (kept in the project, so they travel with it; "
                               "in Scrivener format they go into the project's Icons folder)."),
                            this);
    hint->setWordWrap(true);
    lay->addWidget(hint);
    auto *row = new QHBoxLayout;
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("projectIcons"));
    m_list->setIconSize(QSize(32, 32));
    row->addWidget(m_list, 1);
    auto *buttons = new QVBoxLayout;
    auto *addFileBtn = new QPushButton(tr("Add Picture…"), this);
    auto *fromText = new QPushButton(tr("From Text…"), this);
    auto *rename = new QPushButton(tr("Rename…"), this);
    auto *del = new QPushButton(tr("Delete"), this);
    del->setObjectName(QStringLiteral("deleteIcon"));
    auto *apply = new QPushButton(tr("Use for Selected Documents"), this);
    apply->setObjectName(QStringLiteral("useIcon"));
    for (QPushButton *b : {addFileBtn, fromText, rename, del})
        buttons->addWidget(b);
    buttons->addStretch(1);
    buttons->addWidget(apply);
    row->addLayout(buttons);
    lay->addLayout(row);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    connect(addFileBtn, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add Icons"), QDir::homePath(),
                                                                tr("Pictures (*.png *.svg *.jpg *.jpeg *.bmp *.gif *.ico *.webp)"));
        for (const QString &f : files)
            addFile(f);
    });
    connect(fromText, &QPushButton::clicked, this, [this] {
        IconFromTextDialog dlg(m_project, this);
        if (dlg.exec() == QDialog::Accepted)
            reload(icons::customName(dlg.createdIcon()));
    });
    connect(rename, &QPushButton::clicked, this, [this] {
        QListWidgetItem *it = m_list->currentItem();
        if (!it)
            return;
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("Rename Icon"), tr("New name:"), QLineEdit::Normal, it->text(), &ok);
        if (!ok || n.trimmed().isEmpty() || n == it->text())
            return;
        const QString from = it->text();
        if (!icons::renameCustom(m_project, from, n)) {
            QMessageBox::warning(this, tr("Rename Icon"), tr("An icon of that name exists already."));
            return;
        }
        // documents that use the icon keep it under its new name
        for (BinderItem *b : m_project->allItems())
            if (b->icon == QStringLiteral("custom:") + from) {
                b->icon = QStringLiteral("custom:") + n.trimmed();
                m_project->notifyItemChanged(b, false);
            }
        m_project->markIndexDirty();
        reload(n.trimmed());
    });
    connect(del, &QPushButton::clicked, this, [this] {
        QListWidgetItem *it = m_list->currentItem();
        if (!it)
            return;
        const QString name = it->text();
        int users = 0;
        for (BinderItem *b : m_project->allItems())
            if (b->icon == QStringLiteral("custom:") + name)
                ++users;
        if (users && QMessageBox::question(this, tr("Delete Icon"),
                                           tr("%n document(s) use this icon; they get their standard icon back.", nullptr, users))
                         != QMessageBox::Yes)
            return;
        for (BinderItem *b : m_project->allItems())
            if (b->icon == QStringLiteral("custom:") + name) {
                b->icon.clear();
                m_project->notifyItemChanged(b, false);
            }
        icons::removeCustom(m_project, name);
        m_project->markIndexDirty();
        reload();
    });
    connect(apply, &QPushButton::clicked, this, [this] {
        if (QListWidgetItem *it = m_list->currentItem()) {
            m_chosen = QStringLiteral("custom:") + it->text();
            accept();
        }
    });
    connect(m_list, &QListWidget::itemDoubleClicked, apply, &QPushButton::click);
    reload();
}

bool ManageIconsDialog::addFile(const QString &path)
{
    QImageReader r(path);
    r.setScaledSize(QSize());
    QImage img = r.read();
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Add Icon"), tr("%1 is not a picture the program can read.").arg(QFileInfo(path).fileName()));
        return false;
    }
    QString err;
    const QString id = icons::addCustom(m_project, QFileInfo(path).completeBaseName(), img, &err);
    if (id.isEmpty()) {
        QMessageBox::warning(this, tr("Add Icon"), err);
        return false;
    }
    reload(icons::customName(id));
    return true;
}

void ManageIconsDialog::reload(const QString &select)
{
    m_list->clear();
    for (const QString &n : icons::customNames(m_project)) {
        auto *it = new QListWidgetItem(QIcon(icons::customPath(m_project, n)), n, m_list);
        if (n == select)
            m_list->setCurrentItem(it);
    }
}

// ------------------------------------------------------------------ icon bar

IconBarDialog::IconBarDialog(Project *project, const QStringList &current, QWidget *parent) : QDialog(parent), m_project(project)
{
    setWindowTitle(tr("Icon Bar"));
    resize(640, 520);
    auto *lay = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("Choose up to %1 icons. A click on one of them in the icon bar gives it to the documents "
                               "selected in the binder (or to the document in the editor).")
                                .arg(kMax),
                            this);
    hint->setWordWrap(true);
    lay->addWidget(hint);
    auto *row = new QHBoxLayout;
    m_all = new QListWidget(this);
    m_all->setObjectName(QStringLiteral("iconBarAvailable"));
    m_all->setIconSize(QSize(22, 22));
    m_chosen = new QListWidget(this);
    m_chosen->setObjectName(QStringLiteral("iconBarChosen"));
    m_chosen->setIconSize(QSize(22, 22));
    m_chosen->setDragDropMode(QAbstractItemView::InternalMove);
    auto addEntry = [&](QListWidget *l, const QString &id, const QString &prefix = QString()) {
        auto *it = new QListWidgetItem(icons::iconFor(m_project, id), prefix + icons::label(id), l);
        it->setData(Qt::UserRole, id);
        return it;
    };
    for (const QString &g : icons::groups())
        for (const icons::CatalogEntry &e : icons::inGroup(g))
            addEntry(m_all, e.id, g + QStringLiteral(" ▸ "));
    for (const icons::CatalogEntry &e : icons::inGroup(QString()))
        addEntry(m_all, e.id);
    for (const QString &n : icons::customNames(m_project))
        addEntry(m_all, QStringLiteral("custom:") + n, tr("Project") + QStringLiteral(" ▸ "));
    for (const icons::Choice &c : icons::customChoices())
        addEntry(m_all, c.name, tr("More") + QStringLiteral(" ▸ "));
    auto *mid = new QVBoxLayout;
    auto *add = new QPushButton(tr("Add →"), this);
    add->setObjectName(QStringLiteral("iconBarAdd"));
    auto *remove = new QPushButton(tr("← Remove"), this);
    auto *up = new QPushButton(tr("Up"), this);
    auto *down = new QPushButton(tr("Down"), this);
    auto *standard = new QPushButton(tr("Standard Set"), this);
    mid->addStretch(1);
    for (QPushButton *b : {add, remove, up, down, standard})
        mid->addWidget(b);
    mid->addStretch(1);
    auto *left = new QVBoxLayout;
    left->addWidget(new QLabel(tr("All icons:"), this));
    left->addWidget(m_all);
    auto *right = new QVBoxLayout;
    m_count = new QLabel(this);
    right->addWidget(m_count);
    right->addWidget(m_chosen);
    row->addLayout(left, 1);
    row->addLayout(mid);
    row->addLayout(right, 1);
    lay->addLayout(row, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    for (const QString &id : current)
        addIcon(id);
    connect(add, &QPushButton::clicked, this, [this] {
        for (QListWidgetItem *it : m_all->selectedItems())
            if (!addIcon(it->data(Qt::UserRole).toString())) {
                QMessageBox::information(this, windowTitle(), tr("The icon bar holds at most %1 icons.").arg(kMax));
                break;
            }
    });
    connect(m_all, &QListWidget::itemDoubleClicked, add, &QPushButton::click);
    connect(remove, &QPushButton::clicked, this, [this] {
        delete m_chosen->currentItem();
        updateCount();
    });
    connect(m_chosen, &QListWidget::itemDoubleClicked, remove, &QPushButton::click);
    connect(up, &QPushButton::clicked, this, [this] {
        const int r = m_chosen->currentRow();
        if (r > 0) {
            m_chosen->insertItem(r - 1, m_chosen->takeItem(r));
            m_chosen->setCurrentRow(r - 1);
        }
    });
    connect(down, &QPushButton::clicked, this, [this] {
        const int r = m_chosen->currentRow();
        if (r >= 0 && r < m_chosen->count() - 1) {
            m_chosen->insertItem(r + 1, m_chosen->takeItem(r));
            m_chosen->setCurrentRow(r + 1);
        }
    });
    connect(standard, &QPushButton::clicked, this, [this] {
        m_chosen->clear();
        for (const QString &id : standardIconBar())
            addIcon(id);
    });
    m_all->setSelectionMode(QAbstractItemView::ExtendedSelection);
    updateCount();
}

bool IconBarDialog::addIcon(const QString &id)
{
    for (int i = 0; i < m_chosen->count(); ++i)
        if (m_chosen->item(i)->data(Qt::UserRole).toString() == id)
            return true;
    if (m_chosen->count() >= kMax)
        return false;
    auto *it = new QListWidgetItem(icons::iconFor(m_project, id), icons::label(id), m_chosen);
    it->setData(Qt::UserRole, id);
    updateCount();
    return true;
}

QStringList IconBarDialog::icons() const
{
    QStringList l;
    for (int i = 0; i < m_chosen->count(); ++i)
        l << m_chosen->item(i)->data(Qt::UserRole).toString();
    return l;
}

void IconBarDialog::updateCount() { m_count->setText(tr("In the icon bar (%1 of %2):").arg(m_chosen->count()).arg(kMax)); }

} // namespace qb
