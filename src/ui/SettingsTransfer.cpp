#include "ui/SettingsTransfer.h"

#include "core/DocumentStore.h"
#include "core/Objects.h"
#include "core/Project.h"
#include "text/Styles.h"
#include "ui/AppSettings.h"
#include "ui/ProjectLook.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace qb {

using namespace settingsfile;

namespace settingstransfer {

QJsonObject defaults()
{
    QTemporaryDir tmp;
    QString err;
    auto p = Project::create(tmp.path(), QStringLiteral("Defaults"), &err);
    if (!p)
        return QJsonObject();
    // what File > New Project gives a blank project (MainWindow::showStartDialog)
    const AppSettings &as = AppSettings::instance();
    qb::Formatting nf = p->formatting;
    nf.fontFamily = as.newFontFamily;
    nf.fontSize = as.newFontSize;
    nf.lineSpacing = as.newLineSpacing;
    nf.firstLineIndent = as.newFirstLineIndent;
    nf.paragraphSpacing = as.newParagraphSpacing;
    nf.align = as.newAlign;
    StyleSheet saved;
    if (styles::fromXmlString(as.newStylesXml, &saved))
        nf = styles::withSheet(nf, saved);
    p->formatting = nf;
    QJsonObject s = capture(p.get());
    s.insert(key(Look), look::forNewProjects().toJson());
    return s;
}

} // namespace settingstransfer

static QString shortText(const QString &t, int n = 70)
{
    return t.size() > n ? t.left(n - 3) + QStringLiteral("...") : t;
}

SettingsImportDialog::SettingsImportDialog(const QString &fileName, const QJsonObject &file, const QJsonObject &project,
                                           const QJsonObject &defaults, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("settingsImportDialog"));
    setWindowTitle(tr("Load Project Settings"));
    resize(900, 620);
    auto *v = new QVBoxLayout(this);
    const QJsonObject sections = file.value(QStringLiteral("sections")).toObject();
    const QDateTime saved = QDateTime::fromString(file.value(QStringLiteral("saved")).toString(), Qt::ISODate);
    auto *head = new QLabel(tr("File: %1\nSaved %2 from the project “%3” by %4.")
                                .arg(QFileInfo(fileName).fileName(),
                                     saved.isValid() ? saved.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : tr("(no date)"),
                                     file.value(QStringLiteral("project")).toString(), file.value(QStringLiteral("app")).toString()),
                            this);
    head->setWordWrap(true);
    v->addWidget(head);

    for (int i = 0; i < SectionCount; ++i) {
        const auto s = Section(i);
        const QJsonValue fv = sections.value(key(s));
        m_differs[i] = !fv.isUndefined() && !same(project.value(key(s)), fv);
        if (m_differs[i]) {
            ++m_differing;
            m_texts[i] = differences(s, project.value(key(s)), fv);
        } else {
            m_texts[i] = fv.isUndefined() ? tr("The file has nothing for this section; the project keeps its settings.")
                                          : tr("The same in the project and in the file.");
        }
    }
    auto *summary = new QLabel(m_differing == 0
                                   ? tr("The project's settings are the same as in the file.")
                                   : tr("The project's settings differ from the file in %n section(s). Choose for each what to do; "
                                        "“Quillbinder default” is what a new project gets. Labels, status, section types, "
                                        "keywords, custom metadata and styles that documents use are always kept.",
                                        nullptr, m_differing),
                               this);
    summary->setObjectName(QStringLiteral("settingsImportSummary"));
    summary->setWordWrap(true);
    v->addWidget(summary);

    m_table = new QTreeWidget(this);
    m_table->setObjectName(QStringLiteral("settingsImportTable"));
    m_table->setRootIsDecorated(false);
    m_table->setColumnCount(3);
    m_table->setHeaderLabels({tr("Section"), tr("Difference"), tr("What to do")});
    m_table->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    for (int i = 0; i < SectionCount; ++i) {
        const auto s = Section(i);
        auto *row = new QTreeWidgetItem(m_table, {title(s), m_differs[i] ? shortText(m_texts[i]) : tr("same"), QString()});
        row->setData(0, Qt::UserRole, i);
        if (!m_differs[i]) {
            for (int c = 0; c < 3; ++c)
                row->setForeground(c, palette().color(QPalette::Disabled, QPalette::Text));
            continue;
        }
        auto *combo = new QComboBox(m_table);
        combo->setObjectName(QStringLiteral("settingsChoice_") + key(s));
        combo->addItem(tr("Take from the File"), int(TakeFile));
        combo->addItem(tr("Keep the Project's"), int(KeepProject));
        const bool hasDefault = defaults.contains(key(s));
        combo->addItem(tr("Quillbinder Default"), int(UseDefault));
        if (!hasDefault)
            combo->setItemData(2, 0, Qt::UserRole - 1); // not offered
        m_table->setItemWidget(row, 2, combo);
        m_combos[i] = combo;
    }
    v->addWidget(m_table, 1);
    m_details = new QLabel(this);
    m_details->setObjectName(QStringLiteral("settingsImportDetails"));
    m_details->setWordWrap(true);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_details->setMinimumHeight(60);
    v->addWidget(m_details);
    connect(m_table, &QTreeWidget::currentItemChanged, this, [this] { showDetails(); });

    auto *bar = new QHBoxLayout;
    auto *all = new QPushButton(tr("Take All from the File"), this);
    all->setObjectName(QStringLiteral("settingsTakeAll"));
    auto *none = new QPushButton(tr("Keep All of the Project's"), this);
    none->setObjectName(QStringLiteral("settingsKeepAll"));
    connect(all, &QPushButton::clicked, this, [this] { setAll(TakeFile); });
    connect(none, &QPushButton::clicked, this, [this] { setAll(KeepProject); });
    bar->addWidget(all);
    bar->addWidget(none);
    bar->addStretch(1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Load"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(m_differing > 0);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    bar->addWidget(buttons);
    v->addLayout(bar);
    for (int i = 0; i < m_table->topLevelItemCount(); ++i)
        if (m_differs[m_table->topLevelItem(i)->data(0, Qt::UserRole).toInt()]) {
            m_table->setCurrentItem(m_table->topLevelItem(i));
            break;
        }
    showDetails();
}

bool SettingsImportDialog::differs(Section s) const { return m_differs[s]; }

SettingsImportDialog::Choice SettingsImportDialog::choice(Section s) const
{
    return m_combos[s] ? Choice(m_combos[s]->currentData().toInt()) : KeepProject;
}

void SettingsImportDialog::setChoice(Section s, Choice c)
{
    if (m_combos[s])
        m_combos[s]->setCurrentIndex(qMax(0, m_combos[s]->findData(int(c))));
}

void SettingsImportDialog::setAll(Choice c)
{
    for (int i = 0; i < SectionCount; ++i)
        setChoice(Section(i), c);
}

QString SettingsImportDialog::details(Section s) const { return m_texts[s]; }

void SettingsImportDialog::showDetails()
{
    QTreeWidgetItem *row = m_table->currentItem();
    if (!row) {
        m_details->clear();
        return;
    }
    const int i = row->data(0, Qt::UserRole).toInt();
    m_details->setText(QStringLiteral("<b>%1</b>: %2").arg(title(Section(i)).toHtmlEscaped(), m_texts[i].toHtmlEscaped()));
}

} // namespace qb
