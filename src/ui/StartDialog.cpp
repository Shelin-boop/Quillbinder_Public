#include "ui/StartDialog.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "ui/AppSettings.h"

#include "AppInfo.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace qb {

static const char *kRecent = "__recent";

StartDialog::StartDialog(QWidget *parent, bool showRecentFirst) : QDialog(parent)
{
    setWindowTitle(tr("%1 — Projects").arg(QLatin1String(QB_APP_NAME)));
    resize(860, 560);
    m_all = templates::all();

    auto *lay = new QVBoxLayout(this);
    auto *main = new QHBoxLayout;
    m_categories = new QListWidget(this);
    m_categories->setMaximumWidth(190);
    auto *recent = new QListWidgetItem(icons::action(QStringLiteral("document-open-recent")), tr("Recent Projects"));
    recent->setData(Qt::UserRole, QString::fromLatin1(kRecent));
    m_categories->addItem(recent);
    for (const QString &c : templates::categories()) {
        auto *li = new QListWidgetItem(icons::get(QStringLiteral("folder")), c);
        li->setData(Qt::UserRole, c);
        m_categories->addItem(li);
    }
    main->addWidget(m_categories);

    auto *right = new QVBoxLayout;
    m_templates = new QListWidget(this);
    m_templates->setViewMode(QListView::IconMode);
    m_templates->setIconSize(QSize(64, 64));
    m_templates->setGridSize(QSize(150, 110));
    m_templates->setResizeMode(QListView::Adjust);
    m_templates->setMovement(QListView::Static);
    m_templates->setWordWrap(true);
    right->addWidget(m_templates, 1);
    m_description = new QLabel(this);
    m_description->setWordWrap(true);
    m_description->setMinimumHeight(48);
    right->addWidget(m_description);
    main->addLayout(right, 1);
    lay->addLayout(main, 1);

    auto *form = new QHBoxLayout;
    form->addWidget(new QLabel(tr("Name:"), this));
    m_name = new QLineEdit(tr("My Project"), this);
    m_name->setObjectName(QStringLiteral("projectName"));
    form->addWidget(m_name, 1);
    form->addWidget(new QLabel(tr("Save in:"), this));
    QString loc = AppSettings::instance().lastProjectDir;
    if (loc.isEmpty() || !QDir(loc).exists())
        loc = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    m_location = new QLineEdit(loc, this);
    m_location->setObjectName(QStringLiteral("projectLocation"));
    form->addWidget(m_location, 2);
    auto *browse = new QPushButton(tr("Browse…"), this);
    form->addWidget(browse);
    form->addWidget(new QLabel(tr("Format:"), this));
    m_format = new QComboBox(this);
    m_format->setObjectName(QStringLiteral("projectFormat"));
    m_format->addItem(tr("%1 project").arg(QStringLiteral(QB_APP_NAME)));
    m_format->addItem(tr("Scrivener 3 project (.scriv)"));
    m_format->setToolTip(tr("Scrivener format: the project can be opened in Scrivener 3 as well (File > Project Format)"));
    form->addWidget(m_format);
    lay->addLayout(form);

    auto *buttons = new QHBoxLayout;
    auto *open = new QPushButton(icons::action(QStringLiteral("document-open")), tr("Open Other…"), this);
    auto *import = new QPushButton(icons::action(QStringLiteral("document-import")), tr("Import Scrivener Project…"), this);
    buttons->addWidget(open);
    buttons->addWidget(import);
    buttons->addStretch(1);
    auto *cancel = new QPushButton(tr("Cancel"), this);
    m_create = new QPushButton(tr("Create"), this);
    m_create->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(m_create);
    lay->addLayout(buttons);

    connect(m_categories, &QListWidget::currentRowChanged, this, [this](int r) {
        if (r >= 0)
            fillTemplates(m_categories->item(r)->data(Qt::UserRole).toString());
    });
    connect(m_templates, &QListWidget::currentRowChanged, this, &StartDialog::updateDescription);
    connect(m_templates, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *li) {
        if (li->data(Qt::UserRole + 1).toBool()) {
            m_openPath = li->data(Qt::UserRole).toString();
            m_action = Action::Open;
            accept();
        } else {
            m_create->click();
        }
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Where should the project be saved?"), m_location->text());
        if (!d.isEmpty())
            m_location->setText(d);
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_create, &QPushButton::clicked, this, [this] {
        QListWidgetItem *li = m_templates->currentItem();
        if (li && li->data(Qt::UserRole + 1).toBool()) {
            m_openPath = li->data(Qt::UserRole).toString();
            m_action = Action::Open;
            accept();
            return;
        }
        if (!li || m_name->text().trimmed().isEmpty())
            return;
        AppSettings::instance().lastProjectDir = m_location->text();
        AppSettings::instance().save();
        m_action = Action::Create;
        accept();
    });
    connect(open, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Open Project"), m_location->text(),
                                                       tr("All projects (*%1 *.scrivx);;%2 projects (*%1);;Scrivener projects (*.scrivx)")
                                                           .arg(QLatin1String(QB_PROJECT_FILE_SUFFIX), QLatin1String(QB_APP_NAME)));
        if (f.isEmpty())
            return;
        m_openPath = f;
        m_action = Action::Open;
        accept();
    });
    connect(import, &QPushButton::clicked, this, [this] {
        m_action = Action::ImportScrivener;
        accept();
    });

    const bool hasRecent = !AppSettings::instance().recentProjects.isEmpty();
    m_categories->setCurrentRow(showRecentFirst && hasRecent ? 0 : 1);
}

void StartDialog::fillTemplates(const QString &category)
{
    m_templates->clear();
    if (category == QLatin1String(kRecent)) {
        m_create->setText(tr("Open"));
        for (const QString &p : AppSettings::instance().recentProjects) {
            if (Project::resolveProjectFile(p).isEmpty() && Project::resolveProjectFile(QFileInfo(p).absolutePath()).isEmpty())
                continue;
            auto *li = new QListWidgetItem(icons::get(QStringLiteral("view-pim-journal")), QFileInfo(p).completeBaseName());
            li->setData(Qt::UserRole, p);
            li->setData(Qt::UserRole + 1, true);
            li->setToolTip(p);
            m_templates->addItem(li);
        }
    } else {
        m_create->setText(tr("Create"));
        for (const templates::Info &t : m_all) {
            if (t.category != category)
                continue;
            auto *li = new QListWidgetItem(icons::get(t.icon), t.name);
            li->setData(Qt::UserRole, t.id);
            li->setData(Qt::UserRole + 1, false);
            m_templates->addItem(li);
        }
    }
    if (m_templates->count())
        m_templates->setCurrentRow(0);
    updateDescription();
}

void StartDialog::updateDescription()
{
    QListWidgetItem *li = m_templates->currentItem();
    if (!li) {
        m_description->setText(QString());
        return;
    }
    if (li->data(Qt::UserRole + 1).toBool()) {
        m_description->setText(li->toolTip());
        return;
    }
    for (const templates::Info &t : m_all)
        if (t.id == li->data(Qt::UserRole).toString())
            m_description->setText(QStringLiteral("<b>%1</b><br>%2").arg(t.name.toHtmlEscaped(), t.description.toHtmlEscaped()));
}

templates::Info StartDialog::selectedTemplate() const
{
    QListWidgetItem *li = m_templates->currentItem();
    const QString id = li ? li->data(Qt::UserRole).toString() : QStringLiteral("blank");
    for (const templates::Info &t : m_all)
        if (t.id == id)
            return t;
    return m_all.first();
}

bool StartDialog::scrivenerFormat() const { return m_format && m_format->currentIndex() == 1; }

QString StartDialog::projectName() const { return m_name->text().trimmed(); }
QString StartDialog::location() const { return m_location->text(); }

} // namespace qb
