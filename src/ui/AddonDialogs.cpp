#include "ui/AddonDialogs.h"
#include "AppInfo.h"
#include "core/ItemIcons.h"
#include "core/Launcher.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <unistd.h>

namespace qb {

using namespace addons;

static QString kindLabel(const Manifest &m)
{
    if (m.kind == QLatin1String("core"))
        return QObject::tr("Program update");
    if (m.kind == QLatin1String("bundle"))
        return QObject::tr("Update bundle");
    return QObject::tr("Add-on");
}

static QString describeOne(const PackageInfo &info, bool heading)
{
    const Manifest &m = info.manifest;
    QString h;
    if (heading)
        h += QStringLiteral("<p><b>%1 %2</b> &nbsp;<span style='color:gray'>%3</span></p>")
                 .arg(m.name.toHtmlEscaped(), m.version.toHtmlEscaped(), kindLabel(m));
    if (!m.description.isEmpty())
        h += QStringLiteral("<p>%1</p>").arg(m.description.toHtmlEscaped());
    if (!info.provides.isEmpty() && m.kind != QLatin1String("bundle"))
        h += QStringLiteral("<p>%1 %2</p>").arg(QObject::tr("Contains:"), info.provides.join(QLatin1String(", ")).toHtmlEscaped());
    if (!m.whatsNew.isEmpty()) {
        h += QStringLiteral("<p>%1</p><ul>").arg(QObject::tr("What's new:"));
        for (const QString &w : m.whatsNew)
            h += QStringLiteral("<li>%1</li>").arg(w.toHtmlEscaped());
        h += QLatin1String("</ul>");
    }
    if (!info.note.isEmpty())
        h += QStringLiteral("<p><i>%1</i></p>").arg(info.note.toHtmlEscaped());
    return h;
}

QString describePackage(const PackageInfo &info)
{
    QString h = describeOne(info, false);
    for (const PackageInfo &n : info.nested)
        h += QLatin1String("<hr>") + describeOne(n, true);
    return h;
}

bool relaunchApplication()
{
    // Through the AppImage when there is one: its loader picks the active
    // program version. A plain build simply starts itself again.
    QString program = qEnvironmentVariable("APPIMAGE");
    if (program.isEmpty())
        program = QCoreApplication::applicationFilePath();
    QList<QByteArray> args = {QFile::encodeName(program)};
    for (const QString &a : QCoreApplication::arguments().mid(1))
        if (!a.endsWith(QLatin1String(".qbpkg"), Qt::CaseInsensitive) && a != QLatin1String("--install"))
            args << a.toLocal8Bit();
    QList<char *> argv;
    for (QByteArray &a : args)
        argv << a.data();
    argv << nullptr;

    // Started by hand rather than with QProcess: the new process must not
    // inherit our open files. One of them keeps the old AppImage mounted.
    const long maxFd = ::sysconf(_SC_OPEN_MAX);
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::setsid();
        for (int fd = 3; fd < maxFd; ++fd)
            ::close(fd);
        ::execv(argv.first(), argv.data());
        ::_exit(127);
    }
    return pid > 0;
}

bool runInstallPackage(QWidget *parent, const QString &file, bool *restart)
{
    if (restart)
        *restart = false;
    const PackageInfo info = inspect(file);
    if (!info.isValid()) {
        QMessageBox::warning(parent, QObject::tr("Install"),
                             QObject::tr("\"%1\" cannot be installed.\n\n%2").arg(QFileInfo(file).fileName(), info.error));
        return false;
    }
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Install %1").arg(kindLabel(info.manifest)));
    dlg.resize(560, 460);
    auto *lay = new QVBoxLayout(&dlg);
    auto *head = new QLabel(QStringLiteral("<h3>%1 %2</h3><span style='color:gray'>%3 &middot; %4</span>")
                                .arg(info.manifest.name.toHtmlEscaped(), info.manifest.version.toHtmlEscaped(),
                                     kindLabel(info.manifest), QFileInfo(file).fileName().toHtmlEscaped()),
                            &dlg);
    head->setWordWrap(true);
    lay->addWidget(head);
    auto *text = new QTextBrowser(&dlg);
    text->setHtml(describePackage(info));
    lay->addWidget(text, 1);
    bool hasCode = false;
    auto scan = [&](const PackageInfo &pi) {
        for (const FileEntry &fe : pi.manifest.files)
            hasCode = hasCode || fe.executable || fe.path.endsWith(QLatin1String(".so"));
    };
    scan(info);
    for (const PackageInfo &n : info.nested)
        scan(n);
    if (hasCode) {
        auto *warn = new QLabel(QObject::tr("This package contains program code. Install it only if you trust where it comes from."), &dlg);
        warn->setWordWrap(true);
        lay->addWidget(warn);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dlg);
    QPushButton *ok = buttons->addButton(QObject::tr("Install"), QDialogButtonBox::AcceptRole);
    ok->setDefault(true);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted)
        return false;

    QStringList log;
    QString err;
    bool needRestart = false;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool done = install(file, &log, &err, &needRestart);
    QApplication::restoreOverrideCursor();
    if (!done) {
        QMessageBox::warning(parent, QObject::tr("Install"), QObject::tr("The installation failed.\n\n%1").arg(err));
        return false;
    }
    if (needRestart) {
        QMessageBox box(QMessageBox::Information, QObject::tr("Install"), log.join(QLatin1Char('\n')), QMessageBox::NoButton, parent);
        box.setInformativeText(QObject::tr("Restart %1 to finish.").arg(QLatin1String(QB_APP_NAME)));
        QPushButton *now = box.addButton(QObject::tr("Restart Now"), QMessageBox::AcceptRole);
        box.addButton(QObject::tr("Later"), QMessageBox::RejectRole);
        box.exec();
        if (restart)
            *restart = box.clickedButton() == now;
    } else {
        QMessageBox::information(parent, QObject::tr("Install"), log.join(QLatin1Char('\n')));
    }
    return true;
}

// ------------------------------------------------------------------ manager

AddonManagerDialog::AddonManagerDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Add-on Manager"));
    resize(720, 600);
    auto *lay = new QVBoxLayout(this);

    lay->addWidget(new QLabel(tr("<b>Program version</b>"), this));
    m_versions = new QTreeWidget(this);
    m_versions->setHeaderLabels({tr("Version"), tr("Status")});
    m_versions->setRootIsDecorated(false);
    m_versions->setMaximumHeight(130);
    m_versions->header()->setStretchLastSection(true);
    lay->addWidget(m_versions);
    auto *vrow = new QHBoxLayout;
    m_useVersion = new QPushButton(tr("Use This Version"), this);
    m_removeVersion = new QPushButton(tr("Remove"), this);
    m_restartHint = new QLabel(this);
    vrow->addWidget(m_useVersion);
    vrow->addWidget(m_removeVersion);
    vrow->addWidget(m_restartHint, 1);
    lay->addLayout(vrow);

    lay->addSpacing(8);
    lay->addWidget(new QLabel(tr("<b>Add-ons</b> — untick to switch one off"), this));
    m_addons = new QTreeWidget(this);
    m_addons->setHeaderLabels({tr("Add-on"), tr("Version"), tr("Contains")});
    m_addons->setRootIsDecorated(false);
    m_addons->header()->setStretchLastSection(true);
    lay->addWidget(m_addons, 2);
    m_details = new QTextBrowser(this);
    m_details->setMaximumHeight(120);
    lay->addWidget(m_details, 1);

    auto *row = new QHBoxLayout;
    auto *installBtn = new QPushButton(icons::action(QStringLiteral("document-import")), tr("Install From File…"), this);
    m_removeAddon = new QPushButton(tr("Remove Add-on"), this);
    auto *folder = new QPushButton(tr("Open Folder"), this);
    auto *close = new QPushButton(tr("Close"), this);
    close->setDefault(true);
    row->addWidget(installBtn);
    row->addWidget(m_removeAddon);
    row->addWidget(folder);
    row->addStretch(1);
    row->addWidget(close);
    lay->addLayout(row);

    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(installBtn, &QPushButton::clicked, this, &AddonManagerDialog::installFromFile);
    connect(folder, &QPushButton::clicked, this, [] {
        QDir().mkpath(addonsRoot());
        QDesktopServices::openUrl(QUrl::fromLocalFile(addonsRoot()));
    });
    connect(m_versions, &QTreeWidget::itemSelectionChanged, this, &AddonManagerDialog::updateButtons);
    connect(m_addons, &QTreeWidget::itemSelectionChanged, this, &AddonManagerDialog::updateButtons);
    connect(m_addons, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *it, int col) {
        if (m_updating || col != 0)
            return;
        addons::setEnabled(it->data(0, Qt::UserRole).toString(), it->checkState(0) == Qt::Checked);
        if (it->data(0, Qt::UserRole + 1).toBool()) { // has plugins: needs a restart
            m_restart = true;
            m_restartHint->setText(tr("Changes take effect after a restart."));
        }
        emit contentChanged();
    });
    connect(m_useVersion, &QPushButton::clicked, this, [this] {
        QTreeWidgetItem *it = m_versions->currentItem();
        if (!it)
            return;
        QString err;
        const QString shown = it->data(0, Qt::UserRole).toString(); // refresh() below deletes the item
        const QString v = it->data(0, Qt::UserRole + 1).toBool() ? QString() : shown;
        if (!setActiveCore(v, &err)) {
            QMessageBox::warning(this, windowTitle(), err);
            return;
        }
        m_restart = true;
        refresh();
        m_restartHint->setText(tr("Version %1 starts the next time you open the program.").arg(shown));
    });
    connect(m_removeVersion, &QPushButton::clicked, this, [this] {
        QTreeWidgetItem *it = m_versions->currentItem();
        if (!it)
            return;
        const QString v = it->data(0, Qt::UserRole).toString();
        if (QMessageBox::question(this, windowTitle(), tr("Remove program version %1 from this computer?").arg(v))
            != QMessageBox::Yes)
            return;
        QString err;
        if (!removeCore(v, &err))
            QMessageBox::warning(this, windowTitle(), err);
        refresh();
    });
    connect(m_removeAddon, &QPushButton::clicked, this, [this] {
        QTreeWidgetItem *it = m_addons->currentItem();
        if (!it)
            return;
        if (QMessageBox::question(this, windowTitle(), tr("Remove the add-on \"%1\"?").arg(it->text(0))) != QMessageBox::Yes)
            return;
        QString err;
        if (!removeAddon(it->data(0, Qt::UserRole).toString(), &err))
            QMessageBox::warning(this, windowTitle(), err);
        refresh();
        emit contentChanged();
    });
    refresh();
}

void AddonManagerDialog::refresh()
{
    m_updating = true;
    m_versions->clear();
    for (const CoreVersion &cv : coreVersions()) {
        auto *it = new QTreeWidgetItem(m_versions);
        it->setText(0, cv.builtIn ? tr("%1 (built into the AppImage)").arg(cv.version) : cv.version);
        QStringList status;
        if (cv.running)
            status << tr("running now");
        if (cv.active && !cv.running)
            status << tr("starts next time");
        if (!cv.usable)
            status << tr("does not fit this AppImage");
        it->setText(1, status.join(QLatin1String(", ")));
        it->setData(0, Qt::UserRole, cv.version);
        it->setData(0, Qt::UserRole + 1, cv.builtIn);
        it->setData(0, Qt::UserRole + 2, cv.running);
        it->setData(0, Qt::UserRole + 3, cv.active);
        it->setData(0, Qt::UserRole + 4, cv.usable);
        it->setToolTip(0, cv.whatsNew.join(QLatin1Char('\n')));
        if (cv.running) {
            QFont f = it->font(0);
            f.setBold(true);
            it->setFont(0, f);
        }
    }
    m_versions->resizeColumnToContents(0);

    m_addons->clear();
    for (const Installed &in : installedAddons()) {
        auto *it = new QTreeWidgetItem(m_addons);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(0, in.enabled ? Qt::Checked : Qt::Unchecked);
        it->setText(0, in.manifest.name);
        it->setText(1, in.manifest.version);
        it->setText(2, in.provides.join(QLatin1String(", ")));
        it->setData(0, Qt::UserRole, in.manifest.id);
        bool plugins = false;
        for (const FileEntry &fe : in.manifest.files)
            plugins = plugins || fe.path.startsWith(QLatin1String("plugins/")) || fe.path.startsWith(QLatin1String("dictionaries/"));
        it->setData(0, Qt::UserRole + 1, plugins);
        QString d = in.manifest.description.toHtmlEscaped();
        if (!in.manifest.author.isEmpty())
            d += QStringLiteral("<br><span style='color:gray'>%1</span>").arg(in.manifest.author.toHtmlEscaped());
        it->setData(0, Qt::UserRole + 2, d);
    }
    if (m_addons->topLevelItemCount() == 0)
        m_details->setHtml(tr("<span style='color:gray'>No add-ons installed. Add-ons are files ending in .qbpkg; "
                              "choose \"Install From File…\" or double-click the file.</span>"));
    m_addons->resizeColumnToContents(0);
    m_updating = false;
    updateButtons();
}

void AddonManagerDialog::updateButtons()
{
    QTreeWidgetItem *v = m_versions->currentItem();
    const bool builtIn = v && v->data(0, Qt::UserRole + 1).toBool();
    const bool running = v && v->data(0, Qt::UserRole + 2).toBool();
    const bool active = v && v->data(0, Qt::UserRole + 3).toBool();
    const bool usable = v && v->data(0, Qt::UserRole + 4).toBool();
    m_useVersion->setEnabled(v && !active && usable);
    m_removeVersion->setEnabled(v && !builtIn && !running);
    QTreeWidgetItem *a = m_addons->currentItem();
    m_removeAddon->setEnabled(a != nullptr);
    if (a)
        m_details->setHtml(a->data(0, Qt::UserRole + 2).toString());
}

void AddonManagerDialog::installFromFile()
{
    const QString f = QFileDialog::getOpenFileName(this, tr("Install Update or Add-on"), QDir::homePath(),
                                                   tr("%1 packages (*.qbpkg)").arg(QLatin1String(QB_APP_NAME)));
    if (f.isEmpty())
        return;
    bool restart = false;
    if (runInstallPackage(this, f, &restart)) {
        refresh();
        emit contentChanged();
        if (restart) {
            m_restart = true;
            done(2); // caller restarts
        }
    }
}

} // namespace qb
