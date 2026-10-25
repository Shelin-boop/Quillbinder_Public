#include "ui/ProjectSettingsDialog.h"
#include "text/DirectFormat.h"
#include "ui/AppSettings.h"
#include "ui/FormatDialogs.h"
#include "core/BinderModel.h"
#include "core/DocumentStore.h"
#include "core/ItemIcons.h"
#include "text/DisplayFormat.h"
#include "ui/FormattingChange.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace qb {

static QToolButton *btn(QWidget *p, const QString &icon, const QString &tip)
{
    auto *b = new QToolButton(p);
    b->setIcon(icons::action(icon));
    b->setToolTip(tip);
    return b;
}

ProjectSettingsDialog::ProjectSettingsDialog(Project *p, int page, QWidget *parent) : QDialog(parent), m_project(p)
{
    setWindowTitle(tr("Project Settings"));
    resize(820, 560);
    m_labels = p->labels;
    m_statuses = p->statuses;
    m_types = p->sectionTypes;
    m_structure = p->structureDefaults;
    m_fields = p->customFields;

    auto *lay = new QVBoxLayout(this);
    auto *main = new QHBoxLayout;
    m_pages = new QListWidget(this);
    m_pages->addItems({tr("Labels"), tr("Status"), tr("Section Types"), tr("Formatting"), tr("Custom Metadata"),
                       tr("Special Folders"), tr("General")});
    // the names of the pages fit (they were cut off, "Section Ty", in the screenshots of the 0.15.0 checks)
    int pagesWidth = 0;
    for (int i = 0; i < m_pages->count(); ++i)
        pagesWidth = qMax(pagesWidth, m_pages->fontMetrics().horizontalAdvance(m_pages->item(i)->text()));
    m_pages->setFixedWidth(pagesWidth + 2 * m_pages->frameWidth() + 24);
    m_pages->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    main->addWidget(m_pages);
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(labelsPage());
    m_stack->addWidget(statusPage());
    m_stack->addWidget(sectionPage());
    m_stack->addWidget(formattingPage());
    m_stack->addWidget(customPage());
    m_stack->addWidget(foldersPage());
    m_stack->addWidget(generalPage());
    main->addWidget(m_stack, 1);
    lay->addLayout(main, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    bb->setObjectName(QStringLiteral("projectSettingsButtons"));
    bb->button(QDialogButtonBox::Apply)->setToolTip(tr("Take over the settings now; the text of the project changes at once"));
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &ProjectSettingsDialog::apply);
    connect(m_pages, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    m_pages->setCurrentRow(qBound(0, page, 6));
}

// ------------------------------------------------------------------ labels

QWidget *ProjectSettingsDialog::labelsPage()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    auto *f = new QFormLayout;
    m_labelTitle = new QLineEdit(m_project->labelTitle, w);
    f->addRow(tr("Name of the \"Label\" field (e.g. POV):"), m_labelTitle);
    l->addLayout(f);
    m_labelTable = new QTableWidget(0, 2, w);
    m_labelTable->setHorizontalHeaderLabels({tr("Color"), tr("Label")});
    m_labelTable->horizontalHeader()->setStretchLastSection(true);
    m_labelTable->verticalHeader()->hide();
    m_labelTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    l->addWidget(m_labelTable, 1);
    auto refresh = [this] {
        m_labelTable->setRowCount(0);
        for (const Label &lab : m_labels) {
            const int r = m_labelTable->rowCount();
            m_labelTable->insertRow(r);
            auto *c = new QTableWidgetItem;
            c->setBackground(lab.color);
            c->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            m_labelTable->setItem(r, 0, c);
            m_labelTable->setItem(r, 1, new QTableWidgetItem(lab.name));
        }
    };
    refresh();
    connect(m_labelTable, &QTableWidget::cellChanged, this, [this](int r, int c) {
        if (c == 1 && r < m_labels.size())
            m_labels[r].name = m_labelTable->item(r, 1)->text();
    });
    connect(m_labelTable, &QTableWidget::cellDoubleClicked, this, [this, refresh](int r, int c) {
        if (c != 0 || r >= m_labels.size())
            return;
        const QColor col = QColorDialog::getColor(m_labels[r].color, this);
        if (col.isValid()) {
            m_labels[r].color = col;
            refresh();
        }
    });
    auto *row = new QHBoxLayout;
    auto *add = btn(w, QStringLiteral("list-add"), tr("Add label"));
    auto *rem = btn(w, QStringLiteral("list-remove"), tr("Remove label"));
    auto *up = btn(w, QStringLiteral("go-up"), tr("Move up"));
    auto *down = btn(w, QStringLiteral("go-down"), tr("Move down"));
    row->addWidget(add);
    row->addWidget(rem);
    row->addWidget(up);
    row->addWidget(down);
    row->addStretch(1);
    row->addWidget(new QLabel(tr("Double-click a color to change it."), w));
    l->addLayout(row);
    connect(add, &QToolButton::clicked, this, [this, refresh] {
        int id = -1;
        for (const Label &x : m_labels)
            id = qMax(id, x.id);
        m_labels.append({id + 1, tr("New Label"), QColor::fromHsv(int(m_labels.size() * 53) % 360, 150, 220)});
        refresh();
    });
    connect(rem, &QToolButton::clicked, this, [this, refresh] {
        const int r = m_labelTable->currentRow();
        if (r >= 0 && r < m_labels.size()) {
            m_labels.removeAt(r);
            refresh();
        }
    });
    connect(up, &QToolButton::clicked, this, [this, refresh] {
        const int r = m_labelTable->currentRow();
        if (r > 0) {
            m_labels.swapItemsAt(r, r - 1);
            refresh();
            m_labelTable->selectRow(r - 1);
        }
    });
    connect(down, &QToolButton::clicked, this, [this, refresh] {
        const int r = m_labelTable->currentRow();
        if (r >= 0 && r + 1 < m_labels.size()) {
            m_labels.swapItemsAt(r, r + 1);
            refresh();
            m_labelTable->selectRow(r + 1);
        }
    });
    return w;
}

QWidget *ProjectSettingsDialog::statusPage()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    auto *f = new QFormLayout;
    m_statusTitle = new QLineEdit(m_project->statusTitle, w);
    f->addRow(tr("Name of the \"Status\" field:"), m_statusTitle);
    l->addLayout(f);
    m_statusList = new QListWidget(w);
    auto refresh = [this] {
        m_statusList->clear();
        for (const Status &s : m_statuses) {
            auto *li = new QListWidgetItem(s.name);
            li->setFlags(li->flags() | Qt::ItemIsEditable);
            m_statusList->addItem(li);
        }
    };
    refresh();
    l->addWidget(m_statusList, 1);
    connect(m_statusList, &QListWidget::itemChanged, this, [this](QListWidgetItem *li) {
        const int r = m_statusList->row(li);
        if (r >= 0 && r < m_statuses.size())
            m_statuses[r].name = li->text();
    });
    auto *row = new QHBoxLayout;
    auto *add = btn(w, QStringLiteral("list-add"), tr("Add status"));
    auto *rem = btn(w, QStringLiteral("list-remove"), tr("Remove status"));
    auto *up = btn(w, QStringLiteral("go-up"), tr("Move up"));
    auto *down = btn(w, QStringLiteral("go-down"), tr("Move down"));
    row->addWidget(add);
    row->addWidget(rem);
    row->addWidget(up);
    row->addWidget(down);
    row->addStretch(1);
    l->addLayout(row);
    connect(add, &QToolButton::clicked, this, [this, refresh] {
        int id = -1;
        for (const Status &x : m_statuses)
            id = qMax(id, x.id);
        m_statuses.append({id + 1, tr("New Status")});
        refresh();
    });
    connect(rem, &QToolButton::clicked, this, [this, refresh] {
        const int r = m_statusList->currentRow();
        if (r >= 0) {
            m_statuses.removeAt(r);
            refresh();
        }
    });
    connect(up, &QToolButton::clicked, this, [this, refresh] {
        const int r = m_statusList->currentRow();
        if (r > 0) {
            m_statuses.swapItemsAt(r, r - 1);
            refresh();
            m_statusList->setCurrentRow(r - 1);
        }
    });
    connect(down, &QToolButton::clicked, this, [this, refresh] {
        const int r = m_statusList->currentRow();
        if (r >= 0 && r + 1 < m_statuses.size()) {
            m_statuses.swapItemsAt(r, r + 1);
            refresh();
            m_statusList->setCurrentRow(r + 1);
        }
    });
    return w;
}

// ------------------------------------------------------------------ section types

QWidget *ProjectSettingsDialog::sectionPage()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    auto *info = new QLabel(tr("Section types tell Compile what each document is (part, chapter, scene …). "
                               "\"Default types by structure\" assigns them automatically from the binder level; "
                               "level 0 is the Draft folder itself."),
                            w);
    info->setWordWrap(true);
    l->addWidget(info);
    auto *top = new QHBoxLayout;
    auto *left = new QVBoxLayout;
    left->addWidget(new QLabel(tr("Section types:"), w));
    m_typeList = new QListWidget(w);
    left->addWidget(m_typeList, 1);
    auto *row = new QHBoxLayout;
    auto *add = btn(w, QStringLiteral("list-add"), tr("Add section type"));
    auto *rem = btn(w, QStringLiteral("list-remove"), tr("Remove section type"));
    auto *up = btn(w, QStringLiteral("go-up"), tr("Move up"));
    auto *down = btn(w, QStringLiteral("go-down"), tr("Move down"));
    row->addWidget(add);
    row->addWidget(rem);
    row->addWidget(up);
    row->addWidget(down);
    row->addStretch(1);
    left->addLayout(row);
    top->addLayout(left, 1);

    auto *right = new QVBoxLayout;
    right->addWidget(new QLabel(tr("Default types by structure:"), w));
    m_structureTree = new QTreeWidget(w);
    m_structureTree->setHeaderLabels({tr("Documents"), tr("Section type")});
    m_structureTree->setRootIsDecorated(false);
    m_structureTree->header()->setStretchLastSection(true);
    m_structureTree->setColumnWidth(0, 200);
    right->addWidget(m_structureTree, 1);
    auto *row2 = new QHBoxLayout;
    auto *addLevel = new QPushButton(tr("Add Level"), w);
    auto *remLevel = new QPushButton(tr("Remove Level"), w);
    row2->addWidget(addLevel);
    row2->addWidget(remLevel);
    row2->addStretch(1);
    right->addLayout(row2);
    top->addLayout(right, 2);
    l->addLayout(top, 1);

    auto refreshTypes = [this] {
        m_typeList->clear();
        for (const SectionType &t : m_types) {
            auto *li = new QListWidgetItem(t.name);
            li->setFlags(li->flags() | Qt::ItemIsEditable);
            m_typeList->addItem(li);
        }
        refreshStructure();
    };
    refreshTypes();
    connect(m_typeList, &QListWidget::itemChanged, this, [this](QListWidgetItem *li) {
        const int r = m_typeList->row(li);
        if (r >= 0 && r < m_types.size()) {
            m_types[r].name = li->text();
            refreshStructure();
        }
    });
    connect(add, &QToolButton::clicked, this, [this, refreshTypes] {
        m_types.append({newId(), tr("New Type")});
        refreshTypes();
        m_typeList->setCurrentRow(m_typeList->count() - 1);
        m_typeList->editItem(m_typeList->currentItem());
    });
    connect(rem, &QToolButton::clicked, this, [this, refreshTypes] {
        const int r = m_typeList->currentRow();
        if (r >= 0 && m_types.size() > 1) {
            const QString id = m_types[r].id;
            m_types.removeAt(r);
            for (QStringList *list : {&m_structure.folders, &m_structure.fileGroups, &m_structure.files})
                for (QString &s : *list)
                    if (s == id)
                        s = m_types.first().id;
            refreshTypes();
        }
    });
    connect(up, &QToolButton::clicked, this, [this, refreshTypes] {
        const int r = m_typeList->currentRow();
        if (r > 0) {
            m_types.swapItemsAt(r, r - 1);
            refreshTypes();
            m_typeList->setCurrentRow(r - 1);
        }
    });
    connect(down, &QToolButton::clicked, this, [this, refreshTypes] {
        const int r = m_typeList->currentRow();
        if (r >= 0 && r + 1 < m_types.size()) {
            m_types.swapItemsAt(r, r + 1);
            refreshTypes();
            m_typeList->setCurrentRow(r + 1);
        }
    });
    auto listFor = [this](int kind) -> QStringList * {
        return kind == 0 ? &m_structure.folders : kind == 1 ? &m_structure.fileGroups : &m_structure.files;
    };
    connect(addLevel, &QPushButton::clicked, this, [this, listFor] {
        QTreeWidgetItem *cur = m_structureTree->currentItem();
        const int kind = cur ? cur->data(0, Qt::UserRole).toInt() : 0;
        QStringList *l = listFor(kind);
        l->append(l->isEmpty() ? (m_types.isEmpty() ? QString() : m_types.first().id) : l->last());
        refreshStructure();
    });
    connect(remLevel, &QPushButton::clicked, this, [this, listFor] {
        QTreeWidgetItem *cur = m_structureTree->currentItem();
        const int kind = cur ? cur->data(0, Qt::UserRole).toInt() : 0;
        QStringList *l = listFor(kind);
        if (l->size() > 1)
            l->removeLast();
        refreshStructure();
    });
    return w;
}

void ProjectSettingsDialog::refreshStructure()
{
    m_structureTree->clear();
    const QStringList kinds = {tr("folders"), tr("file groups"), tr("files")};
    QList<QStringList *> lists = {&m_structure.folders, &m_structure.fileGroups, &m_structure.files};
    for (int k = 0; k < 3; ++k) {
        QStringList *list = lists[k];
        if (list->isEmpty() && !m_types.isEmpty())
            list->append(m_types.first().id);
        for (int lvl = 0; lvl < list->size(); ++lvl) {
            QString label;
            if (list->size() == 1)
                label = tr("All %1").arg(kinds[k]);
            else if (lvl == list->size() - 1)
                label = tr("Level %1 %2 and deeper").arg(lvl).arg(kinds[k]);
            else if (lvl == 0)
                label = tr("Root %1").arg(kinds[k]);
            else
                label = tr("Level %1 %2").arg(lvl).arg(kinds[k]);
            auto *ti = new QTreeWidgetItem(m_structureTree, {label});
            ti->setData(0, Qt::UserRole, k);
            auto *cb = new QComboBox;
            for (const SectionType &t : m_types)
                cb->addItem(t.name, t.id);
            cb->setCurrentIndex(qMax(0, cb->findData(list->at(lvl))));
            m_structureTree->setItemWidget(ti, 1, cb);
            connect(cb, qOverload<int>(&QComboBox::activated), this, [list, lvl, cb](int) { (*list)[lvl] = cb->currentData().toString(); });
        }
    }
}

// ------------------------------------------------------------------ formatting

QWidget *ProjectSettingsDialog::formattingPage()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    auto *box = new QGroupBox(tr("Editor text"), w);
    auto *f = new QFormLayout(box);
    const qb::Formatting &fm = m_project->formatting;
    m_autoFont = new QCheckBox(tr("Automatic serif font"), box);
    m_autoFont->setChecked(fm.fontFamily.isEmpty());
    m_font = new QFontComboBox(box);
    m_font->setCurrentFont(QFont(display::resolvedFontFamily(fm)));
    m_font->setEnabled(!fm.fontFamily.isEmpty());
    m_size = new QDoubleSpinBox(box);
    m_size->setRange(6, 48);
    m_size->setSuffix(tr(" pt"));
    m_size->setValue(fm.fontSize);
    m_spacing = new QDoubleSpinBox(box);
    m_spacing->setRange(0.8, 3.0);
    m_spacing->setSingleStep(0.1);
    m_spacing->setValue(fm.lineSpacing);
    m_indent = new QDoubleSpinBox(box);
    m_indent->setDecimals(2);
    m_indent->setRange(0, units::fromPx(400));
    m_indent->setSingleStep(units::metric() ? 0.25 : 0.125);
    m_indent->setSuffix(units::suffix());
    m_indent->setValue(units::fromPx(fm.firstLineIndent));
    m_paraSpacing = new QDoubleSpinBox(box);
    m_paraSpacing->setDecimals(1);
    m_paraSpacing->setRange(0, 72);
    m_paraSpacing->setSuffix(tr(" pt"));
    m_paraSpacing->setValue(direct::pxToPt(fm.paragraphSpacing));
    m_justify = new QCheckBox(tr("Justify text"), box);
    m_justify->setChecked(fm.justify);
    f->addRow(QString(), m_autoFont);
    f->addRow(tr("Font:"), m_font);
    f->addRow(tr("Size:"), m_size);
    f->addRow(tr("Line spacing:"), m_spacing);
    f->addRow(tr("First line indent:"), m_indent);
    f->addRow(tr("Space after paragraphs:"), m_paraSpacing);
    f->addRow(QString(), m_justify);
    auto *useCurrent = new QPushButton(tr("Use Current"), box);
    useCurrent->setObjectName(QStringLiteral("useCurrentFormatting"));
    useCurrent->setToolTip(tr("Take font, size, spacing and indent from the paragraph that holds the cursor in the editor"));
    qb::Formatting probe;
    useCurrent->setEnabled(currentFormatting::get(&probe));
    f->addRow(QString(), useCurrent);
    connect(useCurrent, &QPushButton::clicked, this, [this] {
        qb::Formatting cur;
        if (!currentFormatting::get(&cur))
            return;
        m_autoFont->setChecked(cur.fontFamily.isEmpty());
        if (!cur.fontFamily.isEmpty())
            m_font->setCurrentFont(QFont(cur.fontFamily));
        m_size->setValue(cur.fontSize);
        m_spacing->setValue(cur.lineSpacing);
        m_indent->setValue(units::fromPx(cur.firstLineIndent));
        m_paraSpacing->setValue(direct::pxToPt(cur.paragraphSpacing));
        m_justify->setChecked(cur.justify);
        updatePreview();
    });
    m_ownFollows = new QCheckBox(tr("Change text with a font, size or spacing of its own as well (for example imported text)"), box);
    m_ownFollows->setObjectName(QStringLiteral("formattingOwnFollows"));
    m_ownFollows->setChecked(true);
    m_ownFollows->setToolTip(tr("Text in the main font, size and spacing of its document takes the new values; what differs "
                                "from it (larger headings, a passage in another font, a paragraph without indent) stays"));
    f->addRow(QString(), m_ownFollows);
    auto *hint = new QLabel(tr("This is the look of the text of the whole project. Apply and OK change all documents at once, and every "
                               "document that changes gets a snapshot first (Inspector ▸ Snapshots). Text formatted by hand "
                               "(Format ▸ Font, Format ▸ Paragraph, the ruler) changes only with the last check box."),
                            w);
    hint->setObjectName(QStringLiteral("formattingHint"));
    hint->setWordWrap(true);
    l->addWidget(box);
    l->addWidget(hint);
    m_preview = new QTextEdit(w);
    m_preview->setReadOnly(true);
    m_preview->setMaximumHeight(140);
    l->addWidget(m_preview);

    auto *store = new QGroupBox(tr("How documents are stored"), w);
    auto *sl = new QVBoxLayout(store);
    m_md = new QRadioButton(tr("Markdown (plain text files; fonts and paragraph formatting set by hand are stored as small markers)"), store);
    m_rtf = new QRadioButton(tr("RTF (rich text files; every document carries its complete formatting)"), store);
    (m_project->storage == StorageFormat::Rtf ? m_rtf : m_md)->setChecked(true);
    sl->addWidget(m_md);
    sl->addWidget(m_rtf);
    auto *note = new QLabel(tr("Changing this converts every document of the project. Take a backup first if you are "
                               "unsure (File ▸ Back Up ▸ Back Up Now)."),
                            store);
    note->setWordWrap(true);
    sl->addWidget(note);
    l->addWidget(store);
    l->addStretch(1);

    for (auto *sp : {m_size, m_spacing, m_indent, m_paraSpacing})
        connect(sp, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &ProjectSettingsDialog::updatePreview);
    connect(m_font, &QFontComboBox::currentFontChanged, this, &ProjectSettingsDialog::updatePreview);
    connect(m_justify, &QCheckBox::toggled, this, &ProjectSettingsDialog::updatePreview);
    connect(m_autoFont, &QCheckBox::toggled, this, [this](bool on) {
        m_font->setEnabled(!on);
        updatePreview();
    });
    updatePreview();
    return w;
}

void ProjectSettingsDialog::updatePreview()
{
    qb::Formatting f;
    f.fontFamily = m_autoFont->isChecked() ? QString() : m_font->currentFont().family();
    f.fontSize = m_size->value();
    f.lineSpacing = m_spacing->value();
    f.firstLineIndent = units::toPx(m_indent->value());
    f.paragraphSpacing = direct::ptToPx(m_paraSpacing->value());
    f.justify = m_justify->isChecked();
    QTextDocument *d = m_preview->document();
    d->clear();
    display::prepareDocument(d, f, 1.0);
    QTextCursor c(d);
    c.setBlockFormat(display::normalBlockFormat(f));
    c.insertText(tr("The lighthouse keeper counted the ships twice each evening, once by lamplight and once by memory, "
                    "and the numbers never quite agreed."));
    c.insertBlock(display::normalBlockFormat(f));
    c.insertText(tr("She wrote the difference in the margin of the logbook and left it for the morning."));
}

// ------------------------------------------------------------------ custom metadata

QWidget *ProjectSettingsDialog::customPage()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    auto *info = new QLabel(tr("Custom metadata fields appear in the inspector and as outliner columns. "
                               "For list fields, enter the choices separated by commas."),
                            w);
    info->setWordWrap(true);
    l->addWidget(info);
    m_fieldTable = new QTableWidget(0, 4, w);
    m_fieldTable->setHorizontalHeaderLabels({tr("Title"), tr("Type"), tr("Choices (lists)"), tr("Default")});
    m_fieldTable->horizontalHeader()->setStretchLastSection(true);
    m_fieldTable->setColumnWidth(0, 160);
    m_fieldTable->setColumnWidth(2, 220);
    m_fieldTable->verticalHeader()->hide();
    l->addWidget(m_fieldTable, 1);
    auto refresh = [this] {
        QSignalBlocker b(m_fieldTable);
        m_fieldTable->setRowCount(0);
        for (int i = 0; i < m_fields.size(); ++i) {
            const CustomField &cf = m_fields[i];
            m_fieldTable->insertRow(i);
            m_fieldTable->setItem(i, 0, new QTableWidgetItem(cf.title));
            auto *type = new QComboBox;
            type->addItems({tr("Text"), tr("Checkbox"), tr("List"), tr("Date")});
            type->setCurrentIndex(int(cf.type));
            m_fieldTable->setCellWidget(i, 1, type);
            connect(type, qOverload<int>(&QComboBox::currentIndexChanged), this,
                    [this, i](int t) { m_fields[i].type = CustomFieldType(t); });
            m_fieldTable->setItem(i, 2, new QTableWidgetItem(cf.options.join(QStringLiteral(", "))));
            m_fieldTable->setItem(i, 3, new QTableWidgetItem(cf.defaultValue));
        }
    };
    refresh();
    connect(m_fieldTable, &QTableWidget::cellChanged, this, [this](int r, int c) {
        if (r >= m_fields.size())
            return;
        const QString t = m_fieldTable->item(r, c)->text();
        if (c == 0)
            m_fields[r].title = t;
        else if (c == 2) {
            QStringList opts;
            for (const QString &o : t.split(QLatin1Char(',')))
                if (!o.trimmed().isEmpty())
                    opts << o.trimmed();
            m_fields[r].options = opts;
        } else if (c == 3)
            m_fields[r].defaultValue = t;
    });
    auto *row = new QHBoxLayout;
    auto *add = btn(w, QStringLiteral("list-add"), tr("Add field"));
    auto *rem = btn(w, QStringLiteral("list-remove"), tr("Remove field"));
    row->addWidget(add);
    row->addWidget(rem);
    row->addStretch(1);
    l->addLayout(row);
    connect(add, &QToolButton::clicked, this, [this, refresh] {
        CustomField cf;
        cf.id = newId();
        cf.title = tr("New Field");
        m_fields << cf;
        refresh();
    });
    connect(rem, &QToolButton::clicked, this, [this, refresh] {
        const int r = m_fieldTable->currentRow();
        if (r >= 0 && r < m_fields.size()) {
            m_fields.removeAt(r);
            refresh();
        }
    });
    return w;
}

// ------------------------------------------------------------------ folders / general

QWidget *ProjectSettingsDialog::foldersPage()
{
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);
    m_templatesFolder = new QComboBox(w);
    m_notesFolder = new QComboBox(w);
    for (QComboBox *cb : {m_templatesFolder, m_notesFolder}) {
        cb->addItem(tr("None"), QString());
        for (BinderItem *it : m_project->allItems())
            if (it->isFolder() && !it->isRootFolder() && !m_project->isInTrash(it))
                cb->addItem(icons::forItem(m_project, it), QString(it->level() * 2, QLatin1Char(' ')) + it->title, it->id);
    }
    m_templatesFolder->setCurrentIndex(qMax(0, m_templatesFolder->findData(m_project->templatesFolderId)));
    m_notesFolder->setCurrentIndex(qMax(0, m_notesFolder->findData(m_project->notesFolderId)));
    f->addRow(tr("Document templates folder:"), m_templatesFolder);
    auto *hint = new QLabel(tr("Documents inside the templates folder appear in Project ▸ New From Template."), w);
    hint->setWordWrap(true);
    f->addRow(QString(), hint);
    f->addRow(tr("Folder for new bookmarked documents:"), m_notesFolder);
    m_notesAsk = new QCheckBox(tr("Ask for the folder each time"), w);
    m_notesAsk->setObjectName(QStringLiteral("notesFolderAsk"));
    m_notesAsk->setChecked(!m_project->uiState.value(QStringLiteral("notesFolderDontAsk")).toBool());
    f->addRow(QString(), m_notesAsk);
    auto *notesHint = new QLabel(tr("Used by the \"+\" button below the bookmarks of a Quick Reference panel: the new document "
                                    "is created in this folder and added to the project bookmarks."),
                                 w);
    notesHint->setWordWrap(true);
    f->addRow(QString(), notesHint);
    auto *bgRow = new QHBoxLayout;
    m_compBackground = new QLineEdit(m_project->compositionBackground, w);
    auto *browse = new QPushButton(tr("Browse…"), w);
    bgRow->addWidget(m_compBackground, 1);
    bgRow->addWidget(browse);
    f->addRow(tr("Composition mode background image:"), bgRow);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString img = QFileDialog::getOpenFileName(this, tr("Background Image"), QString(),
                                                         tr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
        if (!img.isEmpty())
            m_compBackground->setText(img);
    });
    return w;
}

QWidget *ProjectSettingsDialog::generalPage()
{
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);
    m_title = new QLineEdit(m_project->title(), w);
    m_titleOverride = new QLineEdit(m_project->compile.titleOverride, w);
    m_titleOverride->setPlaceholderText(tr("same as project title"));
    m_authorOverride = new QLineEdit(m_project->compile.authorOverride, w);
    m_authorOverride->setPlaceholderText(tr("from File ▸ Options ▸ General"));
    f->addRow(tr("Project title:"), m_title);
    f->addRow(tr("Title for compiled documents:"), m_titleOverride);
    f->addRow(tr("Author for compiled documents:"), m_authorOverride);
    return w;
}

// ------------------------------------------------------------------ apply

void ProjectSettingsDialog::accept()
{
    apply();
    QDialog::accept();
}

void ProjectSettingsDialog::apply()
{
    Project *p = m_project;
    p->labelTitle = m_labelTitle->text().trimmed().isEmpty() ? tr("Label") : m_labelTitle->text().trimmed();
    p->statusTitle = m_statusTitle->text().trimmed().isEmpty() ? tr("Status") : m_statusTitle->text().trimmed();
    p->labels = m_labels;
    p->statuses = m_statuses;
    p->sectionTypes = m_types;
    p->structureDefaults = m_structure;
    const bool fieldsChanged = [&] {
        if (m_fields.size() != p->customFields.size())
            return true;
        for (int i = 0; i < m_fields.size(); ++i)
            if (m_fields[i].id != p->customFields[i].id)
                return true;
        return false;
    }();
    p->customFields = m_fields;

    qb::Formatting f;
    f.styles = p->formatting.styles;
    f.script = p->formatting.script;
    f.fontFamily = m_autoFont->isChecked() ? QString() : m_font->currentFont().family();
    f.fontSize = m_size->value();
    f.lineSpacing = m_spacing->value();
    f.firstLineIndent = units::toPx(m_indent->value());
    f.paragraphSpacing = direct::ptToPx(m_paraSpacing->value());
    f.justify = m_justify->isChecked();
    // the fields show rounded values: an untouched field keeps the exact old value
    if (qAbs(units::fromPx(f.firstLineIndent) - units::fromPx(p->formatting.firstLineIndent)) < 0.006)
        f.firstLineIndent = p->formatting.firstLineIndent;
    if (qAbs(direct::pxToPt(f.paragraphSpacing) - direct::pxToPt(p->formatting.paragraphSpacing)) < 0.06)
        f.paragraphSpacing = p->formatting.paragraphSpacing;
    const bool formattingChanged = f.fontFamily != p->formatting.fontFamily || f.fontSize != p->formatting.fontSize
                                   || f.lineSpacing != p->formatting.lineSpacing
                                   || f.firstLineIndent != p->formatting.firstLineIndent
                                   || f.paragraphSpacing != p->formatting.paragraphSpacing
                                   || f.justify != p->formatting.justify;
    if (formattingChanged) {
        const qb::Formatting oldF = p->formatting;
        p->formatting = f;
        p->docs()->applyFormatting();
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString err;
        const int n = formatchange::followProject(p, oldF, m_ownFollows->isChecked(), &err);
        QApplication::restoreOverrideCursor();
        if (n < 0)
            QMessageBox::warning(this, tr("Project Formatting"), tr("Some documents could not be changed:\n%1").arg(err));
    } else {
        p->formatting = f;
    }

    const StorageFormat want = m_rtf->isChecked() ? StorageFormat::Rtf : StorageFormat::Markdown;
    if (want != p->storage) {
        const auto answer = QMessageBox::question(
            this, tr("Convert Project"),
            want == StorageFormat::Rtf
                ? tr("Convert all documents from Markdown to RTF?")
                : tr("Convert all documents from RTF to Markdown?\n\nFonts, sizes and colors of individual "
                     "passages are not kept in Markdown; bold, italics, headings, notes and similar are."));
        if (answer == QMessageBox::Yes) {
            QString err;
            if (!p->docs()->convertStorage(want, &err))
                QMessageBox::warning(this, tr("Convert Project"), tr("Some documents could not be converted:\n%1").arg(err));
        }
        // the choice shows what the project uses (no second question with OK after Apply)
        (p->storage == StorageFormat::Rtf ? m_rtf : m_md)->setChecked(true);
    }

    p->templatesFolderId = m_templatesFolder->currentData().toString();
    p->notesFolderId = m_notesFolder->currentData().toString();
    p->uiState[QStringLiteral("notesFolderDontAsk")] = !m_notesAsk->isChecked() && !p->notesFolderId.isEmpty();
    p->compositionBackground = m_compBackground->text();
    p->setTitle(m_title->text().trimmed().isEmpty() ? p->title() : m_title->text().trimmed());
    p->compile.titleOverride = m_titleOverride->text().trimmed();
    p->compile.authorOverride = m_authorOverride->text().trimmed();
    if (fieldsChanged)
        p->model()->customFieldsChanged();
    p->model()->refreshAll();
    p->notifySettingsChanged();
}

} // namespace qb
