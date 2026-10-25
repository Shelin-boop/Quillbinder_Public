#include "ui/CompileDialog.h"
#include "compile/Compiler.h"
#include "compile/PluginManager.h"
#include "compile/PdfExporter.h"
#include "core/BinderModel.h"
#include "core/ItemIcons.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"
#include <QToolButton>
#include <QTextDocument>
#include <QTextBrowser>
#include <QSignalBlocker>
#include <QPainter>
#include <QMenu>
#include <QFontDatabase>
#include "core/Objects.h"

#include <QApplication>
#include <QPdfDocument>
#include <QPdfView>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QUuid>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
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
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace qb {

using namespace compile;

CompileDialog::CompileDialog(Project *p, QWidget *parent) : QDialog(parent), m_project(p)
{
    setWindowTitle(tr("Compile Overview"));
    resize(1180, 700);
    m_settings = p->compile;

    auto *lay = new QVBoxLayout(this);
    // "Compile For" in the middle at the top, as in Scrivener; the summary on the right
    auto *top = new QHBoxLayout;
    top->addStretch(1);
    top->addWidget(new QLabel(tr("Compile For:"), this));
    m_exporter = new QComboBox(this);
    m_exporter->setObjectName(QStringLiteral("compileExporter"));
    for (ExporterInterface *e : PluginManager::instance().exporters())
        m_exporter->addItem(e->name(), e->id());
    m_exporter->setCurrentIndex(qMax(0, m_exporter->findData(m_settings.exporterId)));
    top->addWidget(m_exporter);
    top->addStretch(1);
    m_summary = new QLabel(this);
    m_summary->setEnabled(false);
    top->addWidget(m_summary);
    lay->addLayout(top);

    auto *split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);

    // ---- formats
    auto *fw = new QWidget(split);
    auto *fl = new QVBoxLayout(fw);
    fl->setContentsMargins(0, 0, 0, 0);
    auto *fh = new QLabel(tr("Formats"), fw);
    fh->setAlignment(Qt::AlignCenter);
    fh->setFrameShape(QFrame::StyledPanel);
    fl->addWidget(fh);
    m_formats = new QListWidget(fw);
    m_formats->setObjectName(QStringLiteral("compileFormats"));
    fl->addWidget(m_formats, 1);
    auto *fb = new QHBoxLayout;
    fb->setSpacing(2);
    auto button = [fw](const char *name, const QString &icon, const QString &text, const QString &tip) {
        auto *b = new QPushButton(icons::action(icon), text, fw);
        b->setObjectName(QLatin1String(name));
        b->setToolTip(tip);
        b->setFlat(true);
        return b;
    };
    auto *dup = button("formatDuplicate", QStringLiteral("list-add"), QString(), tr("New format: a copy of the selected one to change (Duplicate && Edit)"));
    auto *edit = button("formatEdit", QStringLiteral("document-edit"), QString(), tr("Edit the selected format (your own formats only)"));
    auto *del = button("formatDelete", QStringLiteral("list-remove"), QString(), tr("Delete the selected format (your own formats only)"));
    auto *imp = button("formatImport", QStringLiteral("document-open"), QString(), tr("Import a compile format from a file (.qbformat)"));
    auto *exp = button("formatExport", QStringLiteral("document-save-as"), QString(),
                       tr("Save the selected format as a file, to use it on another computer or share it"));
    for (QPushButton *b : {dup, edit, del, imp, exp})
        fb->addWidget(b);
    fb->addStretch(1);
    fl->addLayout(fb);
    split->addWidget(fw);

    // ---- section layouts
    auto *aw = new QWidget(split);
    auto *al = new QVBoxLayout(aw);
    al->setContentsMargins(0, 0, 0, 0);
    auto *layoutsLabel = new QLabel(tr("Section Layouts"), aw);
    layoutsLabel->setAlignment(Qt::AlignCenter);
    layoutsLabel->setFrameShape(QFrame::StyledPanel);
    layoutsLabel->setToolTip(tr("How each section type looks in this format"));
    al->addWidget(layoutsLabel);
    auto *fontRow = new QHBoxLayout;
    fontRow->addWidget(new QLabel(tr("Font:"), aw));
    m_font = new QComboBox(aw);
    m_font->setObjectName(QStringLiteral("compileFontOverride"));
    m_font->setToolTip(tr("One font for the whole manuscript, or the fonts the format and the text have"));
    m_font->addItem(tr("Determined by Section Layout"), QString());
    for (const QString &fam : QFontDatabase::families())
        if (!QFontDatabase::isPrivateFamily(fam))
            m_font->addItem(fam, fam);
    if (!m_settings.fontOverride.isEmpty() && m_font->findData(m_settings.fontOverride) < 0)
        m_font->addItem(m_settings.fontOverride, m_settings.fontOverride); // a font that is not installed here
    m_font->setCurrentIndex(qMax(0, m_font->findData(m_settings.fontOverride)));
    m_font->setMaxVisibleItems(20);
    fontRow->addWidget(m_font, 1);
    al->addLayout(fontRow);
    m_assign = new QTableWidget(0, 2, aw);
    m_assign->setObjectName(QStringLiteral("compileAssignments"));
    m_assign->setHorizontalHeaderLabels({tr("Section type"), tr("Layout")});
    m_assign->horizontalHeader()->setStretchLastSection(true);
    m_assign->verticalHeader()->hide();
    m_assign->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_assign->setSelectionMode(QAbstractItemView::SingleSelection);
    m_assign->setColumnWidth(0, 150);
    al->addWidget(m_assign, 2);
    m_layoutPreview = new QTextBrowser(aw);
    m_layoutPreview->setObjectName(QStringLiteral("compileLayoutPreview"));
    m_layoutPreview->setToolTip(tr("How a section of the selected type begins in this format"));
    al->addWidget(m_layoutPreview, 1);
    auto *assignBtn = new QPushButton(tr("Assign Section Layouts..."), aw);
    assignBtn->setObjectName(QStringLiteral("compileAssignButton"));
    al->addWidget(assignBtn, 0, Qt::AlignHCenter);
    auto *hint = new QLabel(tr("Placeholders such as <$W:chapter> (One, Two ...), <$n> (1, 2 ...), <$R> (I, II ...), "
                               "<$title>, <$projecttitle>, <$author>, <$wc100> can be used in text and titles."),
                            aw);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    al->addWidget(hint);
    split->addWidget(aw);

    // ---- the four tabs
    m_tabs = new QTabWidget(split);
    m_tabs->setObjectName(QStringLiteral("compileTabs"));
    m_tabs->addTab(buildContentsTab(), icons::action(QStringLiteral("view-list-details")), QString());
    m_tabs->addTab(buildMetadataTab(), icons::action(QStringLiteral("tag")), QString());
    m_tabs->addTab(buildOptionsTab(), icons::action(QStringLiteral("configure")), QString());
    m_tabs->addTab(buildReplacementsTab(), icons::action(QStringLiteral("edit-find-replace")), QString());
    const QStringList tips = {tr("Contents: what is compiled"), tr("Metadata: title, author, cover ..."), tr("Options"),
                              tr("Replacements of this project")};
    for (int i = 0; i < tips.size(); ++i)
        m_tabs->setTabToolTip(i, tips[i]);
    split->addWidget(m_tabs);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    split->setStretchFactor(2, 4);
    lay->addWidget(split, 1);

    auto *bb = new QDialogButtonBox(this);
    auto *previewBtn = bb->addButton(tr("Preview"), QDialogButtonBox::HelpRole);
    previewBtn->setObjectName(QStringLiteral("compilePreview"));
    previewBtn->setToolTip(tr("Show the pages of the manuscript with this format (as PDF)"));
    connect(previewBtn, &QPushButton::clicked, this, &CompileDialog::preview);
    auto *saveBtn = bb->addButton(tr("Save Settings"), QDialogButtonBox::ApplyRole);
    saveBtn->setObjectName(QStringLiteral("compileSave"));
    auto *compileBtn = bb->addButton(tr("Compile..."), QDialogButtonBox::AcceptRole);
    compileBtn->setObjectName(QStringLiteral("compileNow"));
    bb->addButton(QDialogButtonBox::Cancel);
    compileBtn->setDefault(true);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, [this] {
        saveSettings();
        accept();
    });
    connect(compileBtn, &QPushButton::clicked, this, &CompileDialog::compileNow);

    connect(m_formats, &QListWidget::currentRowChanged, this, [this] {
        const int i = m_formats->currentItem() ? m_formats->currentItem()->data(Qt::UserRole).toInt() : -1;
        if (i >= 0 && i < m_formatList.size())
            m_settings.formatName = m_formatList[i].name;
        refreshAssignments();
        refreshContents();
    });
    connect(m_font, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        m_settings.fontOverride = m_font->currentData().toString();
        refreshLayoutPreview();
    });
    connect(m_assign, &QTableWidget::currentCellChanged, this, &CompileDialog::refreshLayoutPreview);
    connect(assignBtn, &QPushButton::clicked, this, &CompileDialog::assignLayouts);
    connect(dup, &QPushButton::clicked, this, [this] {
        Format f = currentFormat();
        f.builtin = false;
        f.name = f.name + tr(" (copy)");
        FormatEditorDialog dlg(f, m_project, this);
        if (dlg.exec() == QDialog::Accepted) {
            QString err;
            if (!saveUserFormat(dlg.format(), &err))
                QMessageBox::warning(this, tr("Compile"), err);
            loadFormats(dlg.format().name);
        }
    });
    connect(edit, &QPushButton::clicked, this, [this] {
        Format f = currentFormat();
        if (f.builtin) {
            QMessageBox::information(this, tr("Compile"),
                                     tr("Built-in formats cannot be changed. Use \"+\" (Duplicate & Edit) to make your own copy."));
            return;
        }
        const QString oldName = f.name;
        FormatEditorDialog dlg(f, m_project, this);
        if (dlg.exec() == QDialog::Accepted) {
            if (dlg.format().name != oldName)
                deleteUserFormat(oldName);
            QString err;
            saveUserFormat(dlg.format(), &err);
            loadFormats(dlg.format().name);
        }
    });
    connect(del, &QPushButton::clicked, this, [this] {
        const Format f = currentFormat();
        if (f.builtin)
            return;
        if (QMessageBox::question(this, tr("Delete Format"), tr("Delete the compile format \"%1\"?").arg(f.name))
            == QMessageBox::Yes) {
            deleteUserFormat(f.name);
            loadFormats(QString());
        }
    });
    connect(imp, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, tr("Import Compile Format"), QDir::homePath(),
                                                          tr("Compile formats (*.qbformat *.json)"));
        if (file.isEmpty())
            return;
        Format f;
        QString err;
        if (!importFormat(file, &f, &err)) {
            QMessageBox::warning(this, tr("Import Compile Format"), err);
            return;
        }
        QStringList names;
        for (const Format &x : allFormats())
            names << x.name;
        const QString base = f.name;
        for (int n = 2; names.contains(f.name); ++n)
            f.name = QStringLiteral("%1 (%2)").arg(base).arg(n);
        if (!saveUserFormat(f, &err)) {
            QMessageBox::warning(this, tr("Import Compile Format"), err);
            return;
        }
        loadFormats(f.name);
    });
    connect(exp, &QPushButton::clicked, this, [this] {
        const Format f = currentFormat();
        const QString file = QFileDialog::getSaveFileName(this, tr("Export Compile Format"),
                                                          QDir(QDir::homePath()).filePath(text::safeFileName(f.name) + QStringLiteral(".qbformat")),
                                                          tr("Compile formats (*.qbformat)"));
        if (file.isEmpty())
            return;
        QString err;
        if (!exportFormat(f, file, &err))
            QMessageBox::warning(this, tr("Export Compile Format"), err);
    });
    loadFormats(m_settings.formatName);
}

void CompileDialog::showTab(Tab t)
{
    if (m_tabs)
        m_tabs->setCurrentIndex(int(t));
}

QWidget *CompileDialog::buildContentsTab()
{
    Project *p = m_project;
    auto *cw = new QWidget;
    auto *cl = new QVBoxLayout(cw);
    auto *gf = new QHBoxLayout;
    m_group = new QComboBox(cw);
    m_group->setObjectName(QStringLiteral("compileGroup"));
    m_front = new QComboBox(cw);
    m_front->setObjectName(QStringLiteral("compileFront"));
    m_back = new QComboBox(cw);
    m_back->setObjectName(QStringLiteral("compileBack"));
    m_group->addItem(icons::forItem(p, p->draft()), p->draft()->title, QString());
    m_front->addItem(tr("None"), QString());
    m_back->addItem(tr("None"), QString());
    for (BinderItem *it : p->allItems()) {
        if (!it->isContainer() || it->isRootFolder() || p->isInTrash(it))
            continue;
        const QString name = QString(it->level() * 2, QLatin1Char(' ')) + it->title;
        if (p->isInDraft(it))
            m_group->addItem(icons::forItem(p, it), name, it->id);
        else {
            m_front->addItem(icons::forItem(p, it), name, it->id);
            m_back->addItem(icons::forItem(p, it), name, it->id);
        }
    }
    m_group->setCurrentIndex(qMax(0, m_group->findData(m_settings.compileGroupId)));
    m_front->setCurrentIndex(qMax(0, m_front->findData(m_settings.frontMatterId)));
    m_back->setCurrentIndex(qMax(0, m_back->findData(m_settings.backMatterId)));
    gf->addWidget(new QLabel(tr("Compile:"), cw));
    gf->addWidget(m_group, 1);
    cl->addLayout(gf);
    m_contents = new QTreeWidget(cw);
    m_contents->setObjectName(QStringLiteral("compileContents"));
    m_contents->setHeaderLabels({tr("Title"), tr("Section Type")});
    m_contents->header()->setStretchLastSection(true);
    m_contents->setColumnWidth(0, 230);
    m_contents->setRootIsDecorated(false);
    cl->addWidget(m_contents, 1);
    // front and back matter: a tick and the folder, as in Scrivener
    auto matter = [&](QCheckBox *&on, QComboBox *combo, const QString &text, const char *name) {
        auto *row = new QHBoxLayout;
        on = new QCheckBox(text, cw);
        on->setObjectName(QLatin1String(name));
        on->setChecked(combo->currentIndex() > 0);
        combo->setEnabled(on->isChecked());
        row->addWidget(on);
        row->addWidget(combo, 1);
        cl->addLayout(row);
        connect(on, &QCheckBox::toggled, this, [this, combo](bool checked) {
            combo->setEnabled(checked);
            if (!checked)
                combo->setCurrentIndex(0);
            else if (combo->currentIndex() == 0 && combo->count() > 1)
                combo->setCurrentIndex(1);
        });
    };
    matter(m_frontOn, m_front, tr("Add front matter:"), "compileFrontOn");
    matter(m_backOn, m_back, tr("Add back matter:"), "compileBackOn");
    // the sheets of the Objects (0.21): characters, locations ... after the back matter
    {
        auto *row = new QHBoxLayout;
        m_objectSheets = new QCheckBox(tr("Add object sheets:"), cw);
        m_objectSheets->setObjectName(QStringLiteral("compileObjectSheets"));
        m_objectSheets->setToolTip(tr("The characters, locations and other objects of the Objects sheet, after the back "
                                      "matter: a heading per category, a section per object with its fields"));
        m_objectSheets->setChecked(m_settings.objectSheets);
        m_objectCategories = new QToolButton(cw);
        m_objectCategories->setObjectName(QStringLiteral("compileObjectCategories"));
        m_objectCategories->setPopupMode(QToolButton::InstantPopup);
        m_objectCategories->setToolButtonStyle(Qt::ToolButtonTextOnly);
        auto *menu = new QMenu(m_objectCategories);
        auto refreshText = [this, menu] {
            QStringList on;
            int all = 0;
            for (QAction *a : menu->actions()) {
                ++all;
                if (a->isChecked())
                    on << a->text();
            }
            m_objectCategories->setText(on.size() == all ? tr("All categories") : on.isEmpty() ? tr("No category") : on.join(QStringLiteral(", ")));
        };
        if (p->objects())
            for (const objects::Category &c : p->objects()->schema().categories) {
                QAction *a = menu->addAction(c.name);
                a->setCheckable(true);
                a->setData(c.id);
                a->setChecked(m_settings.objectCategories.isEmpty() || m_settings.objectCategories.contains(c.id));
                connect(a, &QAction::toggled, this, [this, menu, refreshText] {
                    QStringList ids, all;
                    for (QAction *x : menu->actions()) {
                        all << x->data().toString();
                        if (x->isChecked())
                            ids << x->data().toString();
                    }
                    m_settings.objectCategories = ids.size() == all.size() ? QStringList() : ids;
                    refreshText();
                    refreshContents();
                });
            }
        m_objectCategories->setMenu(menu);
        refreshText();
        m_objectCategories->setEnabled(m_objectSheets->isChecked());
        row->addWidget(m_objectSheets);
        row->addWidget(m_objectCategories, 1);
        cl->addLayout(row);
        connect(m_objectSheets, &QCheckBox::toggled, this, [this](bool on) {
            m_settings.objectSheets = on;
            m_objectCategories->setEnabled(on);
            refreshContents();
        });
    }
    connect(m_group, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        m_settings.compileGroupId = m_group->currentData().toString();
        m_groupText->setEnabled(!m_settings.compileGroupId.isEmpty());
        refreshContents();
    });
    connect(m_front, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        m_settings.frontMatterId = m_front->currentData().toString();
        refreshContents();
    });
    connect(m_back, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        m_settings.backMatterId = m_back->currentData().toString();
        refreshContents();
    });
    return cw;
}

QWidget *CompileDialog::buildMetadataTab()
{
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);
    auto field = [&](const QString &value, const QString &objName, const QString &placeholder = QString()) {
        auto *e = new QLineEdit(value, w);
        e->setObjectName(objName);
        e->setPlaceholderText(placeholder);
        m_meta.insert(objName, e);
        return e;
    };
    AuthorInfo author{AppSettings::instance().forename, AppSettings::instance().surname};
    auto *title = field(m_settings.titleOverride, QStringLiteral("meta_title"), m_project->title());
    auto *subtitle = field(m_settings.subtitle, QStringLiteral("meta_subtitle"));
    auto *auth = field(m_settings.authorOverride, QStringLiteral("meta_author"), author.fullName());
    auto *lang = field(m_settings.language, QStringLiteral("meta_language"), QStringLiteral("en-US"));
    lang->setToolTip(tr("Language of the text, e.g. en-US, de-DE (e-books, Word)"));
    auto *subject = field(m_settings.subject, QStringLiteral("meta_subject"));
    auto *keywords = field(m_settings.keywords, QStringLiteral("meta_keywords"), tr("words, separated by commas"));
    auto *publisher = field(m_settings.publisher, QStringLiteral("meta_publisher"));
    auto *date = field(m_settings.date, QStringLiteral("meta_date"), tr("today (YYYY-MM-DD)"));
    auto *desc = new QPlainTextEdit(m_settings.description, w);
    desc->setObjectName(QStringLiteral("meta_description"));
    desc->setMaximumHeight(90);
    m_meta.insert(desc->objectName(), desc);
    auto *cover = new QComboBox(w);
    cover->setObjectName(QStringLiteral("meta_cover"));
    cover->addItem(tr("None"), QString());
    for (BinderItem *it : m_project->allItems())
        if (it->type == ItemType::Image && !m_project->isInTrash(it))
            cover->addItem(icons::forItem(m_project, it), it->title, it->id);
    cover->setCurrentIndex(qMax(0, cover->findData(m_settings.coverId)));
    m_meta.insert(cover->objectName(), cover);
    auto *ident = field(m_settings.identifier, QStringLiteral("meta_identifier"), tr("made when the first e-book is compiled"));
    f->addRow(tr("Title:"), title);
    f->addRow(tr("Subtitle:"), subtitle);
    f->addRow(tr("Author:"), auth);
    f->addRow(tr("Language:"), lang);
    f->addRow(tr("Subject:"), subject);
    f->addRow(tr("Keywords:"), keywords);
    f->addRow(tr("Publisher:"), publisher);
    f->addRow(tr("Date:"), date);
    f->addRow(tr("Description:"), desc);
    f->addRow(tr("Cover (e-book):"), cover);
    f->addRow(tr("Book identifier:"), ident);
    auto *hint = new QLabel(tr("Empty fields use the project title and the author set under File > Options > General > "
                               "Author Information. Placeholders such as <$projecttitle> work here too. The cover is a "
                               "picture of the binder."),
                            w);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    f->addRow(hint);
    return w;
}

QWidget *CompileDialog::buildOptionsTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    m_groupText = new QCheckBox(tr("Include the text of the compile group itself"), w);
    m_groupText->setObjectName(QStringLiteral("compileGroupText"));
    m_groupText->setChecked(m_settings.includeGroupText);
    m_groupText->setEnabled(!m_settings.compileGroupId.isEmpty());
    v->addWidget(m_groupText);
    connect(m_groupText, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.includeGroupText = on;
        refreshContents();
    });
    auto *note = new QLabel(tr("The look of the compiled file (text, page, headers and footers, footnotes and comments, "
                               "styles, transformations, contents and e-book) belongs to the format: choose a format "
                               "of your own on the left and edit it."),
                            w);
    note->setWordWrap(true);
    note->setEnabled(false);
    v->addWidget(note);
    v->addStretch(1);
    return w;
}

QWidget *CompileDialog::buildReplacementsTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    auto *intro = new QLabel(tr("Replacements of this project, made after those of the format (for example a name "
                                "spelled one way in the drafts and another in the book)."),
                             w);
    intro->setWordWrap(true);
    v->addWidget(intro);
    m_replacements = new QTableWidget(0, 6, w);
    m_replacements->setObjectName(QStringLiteral("compileReplacements"));
    m_replacements->setHorizontalHeaderLabels({tr("On"), tr("Replace"), tr("With"), tr("Regex"), tr("Case"), tr("Whole")});
    m_replacements->verticalHeader()->hide();
    m_replacements->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_replacements->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    for (int c : {0, 3, 4, 5})
        m_replacements->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    auto addRow = [this](const CompileReplacement &r) {
        const int row = m_replacements->rowCount();
        m_replacements->insertRow(row);
        auto check = [&](int col, bool on) {
            auto *it = new QTableWidgetItem;
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
            it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
            m_replacements->setItem(row, col, it);
        };
        check(0, r.enabled);
        m_replacements->setItem(row, 1, new QTableWidgetItem(r.find));
        m_replacements->setItem(row, 2, new QTableWidgetItem(r.replace));
        check(3, r.regex);
        check(4, r.matchCase);
        check(5, r.wholeWords);
    };
    for (const CompileReplacement &r : m_settings.replacements)
        addRow(r);
    v->addWidget(m_replacements, 1);
    auto *row = new QHBoxLayout;
    auto *add = new QPushButton(icons::action(QStringLiteral("list-add")), tr("Add"), w);
    add->setObjectName(QStringLiteral("compileAddReplacement"));
    auto *rem = new QPushButton(icons::action(QStringLiteral("list-remove")), tr("Remove"), w);
    rem->setObjectName(QStringLiteral("compileRemoveReplacement"));
    rem->setEnabled(false);
    row->addWidget(add);
    row->addWidget(rem);
    row->addStretch(1);
    v->addLayout(row);
    connect(add, &QPushButton::clicked, this, [this, addRow] {
        addRow(CompileReplacement());
        m_replacements->setCurrentCell(m_replacements->rowCount() - 1, 1);
        m_replacements->editItem(m_replacements->item(m_replacements->rowCount() - 1, 1));
    });
    connect(rem, &QPushButton::clicked, this, [this] {
        const int r = m_replacements->currentRow();
        if (r >= 0)
            m_replacements->removeRow(r);
    });
    connect(m_replacements, &QTableWidget::itemSelectionChanged, this, [this, rem] {
        rem->setEnabled(!m_replacements->selectedItems().isEmpty());
    });
    return w;
}

void CompileDialog::readReplacements()
{
    if (!m_replacements)
        return;
    m_settings.replacements.clear();
    for (int r = 0; r < m_replacements->rowCount(); ++r) {
        CompileReplacement x;
        auto on = [&](int c) { return m_replacements->item(r, c) && m_replacements->item(r, c)->checkState() == Qt::Checked; };
        x.enabled = on(0);
        x.find = m_replacements->item(r, 1) ? m_replacements->item(r, 1)->text() : QString();
        x.replace = m_replacements->item(r, 2) ? m_replacements->item(r, 2)->text() : QString();
        x.regex = on(3);
        x.matchCase = on(4);
        x.wholeWords = on(5);
        if (!x.find.isEmpty())
            m_settings.replacements << x;
    }
}

void CompileDialog::loadFormats(const QString &select)
{
    m_formatList = allFormats();
    QSignalBlocker b(m_formats);
    m_formats->clear();
    // own formats first, then the built-in ones, each under a heading (as Scrivener's
    // "Project Formats" and "Scrivener Formats")
    auto heading = [this](const QString &text) {
        auto *li = new QListWidgetItem(text);
        QFont f = li->font();
        f.setBold(true);
        f.setItalic(true);
        li->setFont(f);
        li->setFlags(Qt::NoItemFlags);
        li->setData(Qt::UserRole, -1);
        m_formats->addItem(li);
    };
    int selRow = -1, firstRow = -1;
    for (bool builtin : {false, true}) {
        bool any = false;
        for (int i = 0; i < m_formatList.size(); ++i) {
            if (m_formatList[i].builtin != builtin)
                continue;
            if (!any) {
                heading(builtin ? tr("Built-in Formats") : tr("My Formats"));
                any = true;
            }
            auto *li = new QListWidgetItem(icons::action(builtin ? QStringLiteral("document-properties") : QStringLiteral("document-edit")),
                                           m_formatList[i].name);
            li->setData(Qt::UserRole, i);
            m_formats->addItem(li);
            if (firstRow < 0)
                firstRow = m_formats->count() - 1;
            if (m_formatList[i].name == select)
                selRow = m_formats->count() - 1;
        }
    }
    m_formats->setCurrentRow(selRow >= 0 ? selRow : firstRow);
    m_settings.formatName = currentFormat().name;
    refreshAssignments();
    refreshContents();
}

Format CompileDialog::currentFormat() const
{
    const int i = m_formats->currentItem() ? m_formats->currentItem()->data(Qt::UserRole).toInt() : -1;
    return (i >= 0 && i < m_formatList.size()) ? m_formatList[i] : builtinFormats().first();
}

void CompileDialog::refreshAssignments()
{
    const Format f = currentFormat();
    const int keep = qMax(0, m_assign->currentRow());
    m_assign->setRowCount(0);
    AuthorInfo author{AppSettings::instance().forename, AppSettings::instance().surname};
    Compiler c(m_project, f, m_settings, author);
    for (const SectionType &st : m_project->sectionTypes) {
        const int r = m_assign->rowCount();
        m_assign->insertRow(r);
        auto *ti = new QTableWidgetItem(st.name);
        ti->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        QFont bf = ti->font();
        bf.setBold(true);
        ti->setFont(bf);
        ti->setData(Qt::UserRole, st.id);
        m_assign->setItem(r, 0, ti);
        auto *cb = new QComboBox;
        for (const SectionLayout &l : f.layouts)
            cb->addItem(l.name);
        CompileEntry e;
        e.sectionType = st.id;
        cb->setCurrentText(c.layoutNameFor(e));
        m_assign->setCellWidget(r, 1, cb);
        const QString key = f.name + QLatin1Char('|') + st.id;
        connect(cb, &QComboBox::textActivated, this, [this, key, r](const QString &t) {
            m_settings.layoutAssignments[key] = t;
            m_assign->setCurrentCell(r, 0);
            refreshContents();
            refreshLayoutPreview();
        });
    }
    // the names of the section types are short: the layouts get the room ("Chapter Hea…" in the 0.15.0 screenshots)
    m_assign->resizeColumnToContents(0);
    m_assign->setColumnWidth(0, qMin(150, m_assign->columnWidth(0) + 8));
    if (m_assign->rowCount())
        m_assign->setCurrentCell(qMin(keep, m_assign->rowCount() - 1), 0);
    refreshLayoutPreview();
}

QString CompileDialog::layoutPreviewHtml(const Format &format, const SectionLayout &l, const Project *p)
{
    Placeholders ph;
    ph.projectTitle = p ? p->title() : QStringLiteral("My Novel");
    ph.sectionTitle = tr("The Storm");
    QString html = QStringLiteral("<div style=\"font-family:'%1'\">").arg(format.fontFamily.toHtmlEscaped());
    if (l.pageBreakBefore)
        html += QStringLiteral("<p style=\"color:gray; font-size:small\">%1</p>").arg(tr("— new page —"));
    else if (!l.includeTitle && l.separator != QLatin1String("none"))
        html += QStringLiteral("<p align=\"center\">%1</p>")
                    .arg(l.separator == QLatin1String("blank") ? QStringLiteral("&nbsp;") : l.separator.toHtmlEscaped());
    if (l.includeTitle) {
        QString t = ph.apply(l.titlePrefix) + (l.showItemTitle ? tr("The Storm") : QString()) + ph.apply(l.titleSuffix);
        if (l.titleUppercase)
            t = t.toUpper();
        const char *align = (l.titleAlign & Qt::AlignHCenter) ? "center" : (l.titleAlign & Qt::AlignRight) ? "right" : "left";
        const double factor = l.titleStyle == Paragraph::Normal ? 1.0 : l.titleStyle == Paragraph::Title ? 1.6 : 1.3;
        for (int b = 0; b < qMin(3, l.blankLinesBeforeTitle); ++b)
            html += QStringLiteral("<p>&nbsp;</p>");
        for (const QString &line : t.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            html += QStringLiteral("<p align=\"%1\" style=\"font-size:%2em; %3\">%4</p>")
                        .arg(QLatin1String(align))
                        .arg(factor)
                        .arg(format.headingBold ? QStringLiteral("font-weight:bold") : QString(), line.trimmed().toHtmlEscaped());
    }
    if (l.includeSynopsis)
        html += QStringLiteral("<p><i>%1</i></p>").arg(tr("The synopsis of the section."));
    if (l.includeText)
        html += QStringLiteral("<p style=\"text-indent:%1px\">%2</p><p style=\"text-indent:20px\">%3</p>")
                    .arg(l.noIndentFirstParagraph || l.includeTitle ? 0 : 20)
                    .arg(tr("The wind rose in the night and the sea came up over the rocks below the lamp."),
                         tr("By morning the boats were gone."));
    if (!l.includeTitle && !l.includeText && !l.includeSynopsis)
        html += QStringLiteral("<p style=\"color:gray\">%1</p>").arg(tr("(nothing of the section is included)"));
    html += QStringLiteral("</div>");
    return html;
}

void CompileDialog::refreshLayoutPreview()
{
    if (!m_layoutPreview)
        return;
    const int r = m_assign->currentRow();
    auto *cb = r >= 0 ? qobject_cast<QComboBox *>(m_assign->cellWidget(r, 1)) : nullptr;
    Format f = currentFormat();
    if (!m_settings.fontOverride.isEmpty())
        f.fontFamily = m_settings.fontOverride;
    const SectionLayout *l = cb ? f.layout(cb->currentText()) : nullptr;
    if (!l) {
        m_layoutPreview->setHtml(QStringLiteral("<p style=\"color:gray\">%1</p>").arg(tr("Select a section type above.")));
        return;
    }
    m_layoutPreview->setHtml(QStringLiteral("<p style=\"color:gray; font-size:small\">%1</p>")
                                 .arg(tr("%1: how a section of this type begins").arg(m_assign->item(r, 0)->text().toHtmlEscaped()))
                             + layoutPreviewHtml(f, *l, m_project));
}

void CompileDialog::assignLayouts()
{
    // Section types on the left, the layouts of the format as pictures on the right:
    // a click on a picture gives the selected section type that layout.
    const Format f = currentFormat();
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("assignLayoutsDialog"));
    dlg.setWindowTitle(tr("Assign Section Layouts — %1").arg(f.name));
    dlg.resize(900, 560);
    auto *l = new QVBoxLayout(&dlg);
    auto *intro = new QLabel(tr("Choose a section type on the left, then the layout it gets in this format on the right."), &dlg);
    intro->setWordWrap(true);
    l->addWidget(intro);
    auto *h = new QHBoxLayout;
    auto *types = new QListWidget(&dlg);
    types->setObjectName(QStringLiteral("assignTypes"));
    types->setMaximumWidth(220);
    auto *layouts = new QListWidget(&dlg);
    layouts->setObjectName(QStringLiteral("assignLayouts"));
    layouts->setViewMode(QListView::IconMode);
    layouts->setIconSize(QSize(170, 120));
    layouts->setGridSize(QSize(190, 160));
    layouts->setResizeMode(QListView::Adjust);
    layouts->setMovement(QListView::Static);
    layouts->setWordWrap(true);
    h->addWidget(types);
    h->addWidget(layouts, 1);
    l->addLayout(h, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    l->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    // a small picture of each layout (the preview drawn on a page)
    for (const SectionLayout &sl : f.layouts) {
        QTextDocument d;
        d.setHtml(layoutPreviewHtml(f, sl, m_project));
        d.setTextWidth(320);
        QImage img(340, 240, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        {
            QPainter pp(&img);
            pp.translate(10, 8);
            d.drawContents(&pp, QRectF(0, 0, 320, 230));
            pp.resetTransform();
            pp.setPen(QColor(170, 170, 170));
            pp.drawRect(img.rect().adjusted(0, 0, -1, -1));
        }
        auto *li = new QListWidgetItem(QIcon(QPixmap::fromImage(img)), sl.name);
        li->setData(Qt::UserRole, sl.name);
        layouts->addItem(li);
    }
    QMap<QString, QString> chosen; // section type id -> layout
    AuthorInfo author{AppSettings::instance().forename, AppSettings::instance().surname};
    Compiler c(m_project, f, m_settings, author);
    for (const SectionType &st : m_project->sectionTypes) {
        CompileEntry e;
        e.sectionType = st.id;
        chosen.insert(st.id, c.layoutNameFor(e));
        auto *ti = new QListWidgetItem(st.name, types);
        ti->setData(Qt::UserRole, st.id);
    }
    auto showChoice = [&] {
        QListWidgetItem *t = types->currentItem();
        const QString now = t ? chosen.value(t->data(Qt::UserRole).toString()) : QString();
        const QSignalBlocker b(layouts);
        layouts->clearSelection();
        for (int i = 0; i < layouts->count(); ++i)
            if (layouts->item(i)->data(Qt::UserRole).toString() == now)
                layouts->setCurrentRow(i);
        for (int i = 0; i < types->count(); ++i)
            types->item(i)->setToolTip(chosen.value(types->item(i)->data(Qt::UserRole).toString()));
    };
    connect(types, &QListWidget::currentRowChanged, &dlg, showChoice);
    connect(layouts, &QListWidget::currentRowChanged, &dlg, [&](int row) {
        QListWidgetItem *t = types->currentItem();
        if (t && row >= 0)
            chosen[t->data(Qt::UserRole).toString()] = layouts->item(row)->data(Qt::UserRole).toString();
    });
    types->setCurrentRow(qMax(0, m_assign->currentRow()));
    showChoice();
    if (dlg.exec() != QDialog::Accepted)
        return;
    for (auto it = chosen.cbegin(); it != chosen.cend(); ++it)
        m_settings.layoutAssignments[f.name + QLatin1Char('|') + it.key()] = it.value();
    refreshAssignments();
    refreshContents();
}

void CompileDialog::refreshContents()
{
    m_contents->blockSignals(true);
    m_contents->clear();
    const Format f = currentFormat();
    AuthorInfo author{AppSettings::instance().forename, AppSettings::instance().surname};
    Compiler c(m_project, f, m_settings, author);
    int words = 0, docs = 0;
    for (const CompileEntry &e : c.entries()) {
        auto *ti = new QTreeWidgetItem(m_contents);
        QString prefix = e.part == CompileEntry::Front ? tr("[front] ") : e.part == CompileEntry::Back ? tr("[back] ") : QString();
        ti->setText(0, QString(qMax(0, e.level - 1) * 3, QLatin1Char(' ')) + prefix + e.item->title);
        ti->setIcon(0, icons::forItem(m_project, e.item));
        ti->setCheckState(0, e.included ? Qt::Checked : Qt::Unchecked);
        ti->setData(0, Qt::UserRole, e.item->id);
        const SectionType *st = m_project->sectionTypeById(e.sectionType);
        ti->setText(1, (st ? st->name : tr("?")) + QStringLiteral("  →  ") + c.layoutNameFor(e));
        if (e.item->sectionType.isEmpty()) {
            QFont fnt = ti->font(1);
            fnt.setItalic(true);
            ti->setFont(1, fnt);
        }
        if (e.included) {
            words += e.item->wordCount;
            ++docs;
        }
    }
    // the object sheets at the end (0.21): one line per category, not tickable here
    if (m_settings.objectSheets && m_project->objects()) {
        for (const objects::Category &cat : m_project->objects()->schema().categories) {
            if (!m_settings.objectCategories.isEmpty() && !m_settings.objectCategories.contains(cat.id))
                continue;
            const int n = int(m_project->objects()->objects(cat.id).size());
            if (n == 0)
                continue;
            auto *ti = new QTreeWidgetItem(m_contents);
            ti->setText(0, tr("[objects] ") + cat.name);
            ti->setIcon(0, icons::action(QStringLiteral("qb-objects")));
            ti->setText(1, n == 1 ? tr("1 object sheet") : tr("%1 object sheets").arg(n));
            ti->setFlags(Qt::ItemIsEnabled);
            ti->setData(0, Qt::UserRole + 1, true);
        }
    }
    if (!m_contentsHooked) {
        m_contentsHooked = true;
        connect(m_contents, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *ti, int col) {
            if (col != 0 || m_contents->signalsBlocked() || ti->data(0, Qt::UserRole + 1).toBool())
                return;
            m_settings.includeOverrides[ti->data(0, Qt::UserRole).toString()] = ti->checkState(0) == Qt::Checked;
        });
    }
    m_contents->blockSignals(false);
    // "1 document", not "1 documents" (found by looking at the window in the 0.14.0 checks)
    const QString docText = docs == 1 ? tr("1 document") : tr("%1 documents").arg(docs);
    const QString wordText = words == 1 ? tr("1 word") : tr("about %1 words").arg(QLocale().toString(words));
    m_summary->setText(docText + QStringLiteral(", ") + wordText);
}

void CompileDialog::saveSettings()
{
    m_settings.exporterId = m_exporter->currentData().toString();
    m_settings.formatName = currentFormat().name;
    m_settings.fontOverride = m_font ? m_font->currentData().toString() : QString();
    readReplacements();
    // the Metadata tab
    auto text = [this](const char *name) {
        QWidget *w = m_meta.value(QLatin1String(name));
        if (auto *e = qobject_cast<QLineEdit *>(w))
            return e->text().trimmed();
        if (auto *e = qobject_cast<QPlainTextEdit *>(w))
            return e->toPlainText().trimmed();
        if (auto *e = qobject_cast<QComboBox *>(w))
            return e->currentData().toString();
        return QString();
    };
    m_settings.titleOverride = text("meta_title");
    m_settings.subtitle = text("meta_subtitle");
    m_settings.authorOverride = text("meta_author");
    m_settings.language = text("meta_language");
    m_settings.subject = text("meta_subject");
    m_settings.keywords = text("meta_keywords");
    m_settings.publisher = text("meta_publisher");
    m_settings.date = text("meta_date");
    m_settings.description = text("meta_description");
    m_settings.coverId = text("meta_cover");
    m_settings.identifier = text("meta_identifier");
    // include overrides equal to the document's own flag are not needed
    for (auto it = m_settings.includeOverrides.begin(); it != m_settings.includeOverrides.end();) {
        BinderItem *item = m_project->item(it.key());
        if (!item || item->includeInCompile == it.value())
            it = m_settings.includeOverrides.erase(it);
        else
            ++it;
    }
    // "Include in compile" ticks are written back to the documents
    for (int i = 0; i < m_contents->topLevelItemCount(); ++i) {
        QTreeWidgetItem *ti = m_contents->topLevelItem(i);
        if (ti->data(0, Qt::UserRole + 1).toBool())
            continue; // a line of object sheets
        BinderItem *item = m_project->item(ti->data(0, Qt::UserRole).toString());
        const bool inc = ti->checkState(0) == Qt::Checked;
        if (item && m_project->isInDraft(item) && item->includeInCompile != inc) {
            item->includeInCompile = inc;
            m_project->notifyItemChanged(item, false);
            m_settings.includeOverrides.remove(item->id);
        }
    }
    m_project->compile = m_settings;
    m_project->markIndexDirty();
}

void CompileDialog::compileNow()
{
    saveSettings();
    ExporterInterface *exp = PluginManager::instance().exporter(m_settings.exporterId);
    if (!exp) {
        QMessageBox::warning(this, tr("Compile"), tr("The selected output format is not available."));
        return;
    }
    QString dir = m_settings.lastOutputDir;
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString title = m_settings.titleOverride.isEmpty() ? m_project->title() : m_settings.titleOverride;
    const QString suggested = QDir(dir).filePath(text::safeFileName(title) + QLatin1Char('.') + exp->fileExtension());
    const QString path = QFileDialog::getSaveFileName(this, tr("Compile to"), suggested,
                                                      tr("%1 (*.%2)").arg(exp->name(), exp->fileExtension()));
    if (path.isEmpty())
        return;
    m_project->compile.lastOutputDir = QFileInfo(path).absolutePath();
    if (exp->id().contains(QLatin1String("epub")) && m_project->compile.identifier.isEmpty()) {
        // an e-book keeps its id from one version to the next
        m_project->compile.identifier = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_settings.identifier = m_project->compile.identifier;
        m_project->markIndexDirty();
    }
    m_project->save();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    AuthorInfo author{AppSettings::instance().forename, AppSettings::instance().surname};
    Compiler c(m_project, currentFormat(), m_project->compile, author);
    const Manuscript m = c.build();
    QString err;
    const bool ok = exp->exportManuscript(m, path, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, tr("Compile"), tr("Compiling failed:\n%1").arg(err));
        return;
    }
    QMessageBox box(QMessageBox::Information, tr("Compile"),
                    tr("The manuscript was saved as\n%1").arg(path), QMessageBox::NoButton, this);
    auto *openBtn = box.addButton(tr("Open"), QMessageBox::AcceptRole);
    auto *folderBtn = box.addButton(tr("Show Folder"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == openBtn)
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    else if (box.clickedButton() == folderBtn)
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    accept();
}

Manuscript CompileDialog::manuscript()
{
    saveSettings();
    AuthorInfo author{AppSettings::instance().forename, AppSettings::instance().surname};
    Compiler c(m_project, currentFormat(), m_project->compile, author);
    return c.build();
}

void CompileDialog::preview()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const Manuscript m = manuscript();
    auto *tmp = new QTemporaryDir;
    const QString path = tmp->filePath(QStringLiteral("preview.pdf"));
    QString err;
    PdfReport rep;
    const bool ok = writePdf(m, path, &err, &rep);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        delete tmp;
        QMessageBox::warning(this, tr("Preview"), err);
        return;
    }
    auto *dlg = new QDialog(this);
    dlg->setObjectName(QStringLiteral("compilePreviewWindow"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // no translation files: "%n page(s)" would show the "(s)" (also below and in three other places, fixed in 0.14.0)
    const QString formatName = currentFormat().name;
    dlg->setWindowTitle(rep.pages == 1 ? tr("Preview — %1 (1 page)").arg(formatName)
                                       : tr("Preview — %1 (%2 pages)").arg(formatName, QString::number(rep.pages)));
    dlg->resize(760, 900);
    auto *l = new QVBoxLayout(dlg);
    auto *doc = new QPdfDocument(dlg);
    doc->load(path);
    auto *view = new QPdfView(dlg);
    view->setObjectName(QStringLiteral("compilePreviewView"));
    view->setDocument(doc);
    view->setPageMode(QPdfView::PageMode::MultiPage);
    view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    l->addWidget(view, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    l->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    connect(dlg, &QObject::destroyed, dlg, [tmp] { delete tmp; });
    dlg->show();
}

// The format editor is in FormatEditor.cpp.

} // namespace qb
