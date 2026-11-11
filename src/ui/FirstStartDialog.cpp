// The questions of the first start (0.62); see FirstStartDialog.h.
#include "ui/FirstStartDialog.h"
#include "AppInfo.h"
#include "core/Backup.h"
#include "core/SettingsLayer.h"
#include "ui/AppSettings.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace qb {

FirstStartDialog::FirstStartDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("firstStartDialog"));
    setWindowTitle(tr("%1: First Start").arg(QStringLiteral(QB_APP_NAME)));
    const AppSettings &s = AppSettings::instance();
    auto *v = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Two choices before you start. Everything is filled in already: OK keeps it as it is. "
                                "You can change all of it later in File > Options."),
                             this);
    intro->setWordWrap(true);
    v->addWidget(intro);

    auto *backups = new QGroupBox(tr("Backups"), this);
    auto *bf = new QFormLayout(backups);
    m_backup = new QCheckBox(tr("Make a backup of a project automatically when it is closed"), backups);
    m_backup->setObjectName(QStringLiteral("firstBackup"));
    m_backup->setChecked(s.backupOnClose);
    bf->addRow(m_backup);
    m_keep = new QSpinBox(backups);
    m_keep->setObjectName(QStringLiteral("firstBackupKeep"));
    m_keep->setRange(1, 100);
    m_keep->setValue(s.backupKeep);
    m_keep->setSuffix(tr(" backups of each project"));
    bf->addRow(tr("Keep the newest:"), m_keep);
    auto *row = new QHBoxLayout;
    m_folder = new QLineEdit(s.backupDir.isEmpty() ? backup::defaultDirectory() : s.backupDir, backups);
    m_folder->setObjectName(QStringLiteral("firstBackupFolder"));
    auto *choose = new QPushButton(tr("Choose…"), backups);
    row->addWidget(m_folder, 1);
    row->addWidget(choose);
    bf->addRow(tr("Folder:"), row);
    // 0.70.0 (audit A U-05): a backup on the same drive is lost with it
    auto *driveHint = new QLabel(tr("Best on another drive (a USB stick, a second disk, a folder your sync program "
                                    "copies), so that the backups outlive a failing disk. File > Back Up > Restore From "
                                    "Backup brings one back."),
                                 backups);
    driveHint->setObjectName(QStringLiteral("firstBackupHint"));
    driveHint->setWordWrap(true);
    bf->addRow(driveHint);
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Folder for Backups"), m_folder->text());
        if (!d.isEmpty())
            m_folder->setText(QDir::toNativeSeparators(d));
    });
    auto enable = [this](bool on) {
        m_keep->setEnabled(on);
        m_folder->setEnabled(on);
    };
    connect(m_backup, &QCheckBox::toggled, this, enable);
    enable(m_backup->isChecked());
    v->addWidget(backups);

    auto *settings = new QGroupBox(tr("Settings of projects"), this);
    auto *sv = new QVBoxLayout(settings);
    m_projectFirst = new QRadioButton(tr("A project's own settings come first; what a project does not set comes from "
                                         "the program's settings"),
                                      settings);
    m_projectFirst->setObjectName(QStringLiteral("firstProjectFirst"));
    m_programFirst = new QRadioButton(tr("The program's settings always (projects' own settings are not used)"), settings);
    m_programFirst->setObjectName(QStringLiteral("firstProgramFirst"));
    (settingslayer::projectFirst() ? m_projectFirst : m_programFirst)->setChecked(true);
    sv->addWidget(m_projectFirst);
    sv->addWidget(m_programFirst);
    auto *hint = new QLabel(tr("A project gets settings of its own in File > Options with \"This project\" at the top."), settings);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    sv->addWidget(hint);
    v->addWidget(settings);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    resize(620, sizeHint().height());
}

void FirstStartDialog::take()
{
    AppSettings &s = AppSettings::instance();
    s.backupOnClose = m_backup->isChecked();
    s.backupKeep = m_keep->value();
    const QString folder = QDir::fromNativeSeparators(m_folder->text().trimmed());
    s.backupDir = folder.isEmpty() ? backup::defaultDirectory() : folder;
    QDir().mkpath(s.backupDir);
    s.save();
    settingslayer::setProjectFirst(m_projectFirst->isChecked());
    markDone();
}

bool FirstStartDialog::due()
{
    if (qEnvironmentVariableIsSet("QB_NO_DIALOGS") || qEnvironmentVariableIsSet("QB_SKIP_FIRST_START"))
        return false;
    QSettings p(settingslayer::programFile(), QSettings::IniFormat);
    return !p.value(QStringLiteral("app/firstStartDone")).toBool();
}

void FirstStartDialog::markDone()
{
    QSettings p(settingslayer::programFile(), QSettings::IniFormat);
    p.setValue(QStringLiteral("app/firstStartDone"), true);
    p.sync();
}

} // namespace qb
