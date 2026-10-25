// File > Project Format: projects in Scrivener 3 format (.scriv) and in the
// program's own format, and converting between them (import/ScrivenerFormat.h).
#include "ui/MainWindow.h"

#include "core/Project.h"
#include "import/ScrivenerFormat.h"
#include "text/TextUtil.h"
#include "ui/AppSettings.h"

#include "AppInfo.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QSysInfo>
#include <QUrl>

namespace qb {

namespace {

struct WaitCursor {
    WaitCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
    ~WaitCursor() { QApplication::restoreOverrideCursor(); }
};

// "name.scriv" chosen in a save dialog
QString chooseScrivPath(QWidget *parent, const QString &title, const QString &suggestedName)
{
    QString start = AppSettings::instance().lastProjectDir;
    if (start.isEmpty() || !QDir(start).exists())
        start = QDir::homePath();
    QString f = QFileDialog::getSaveFileName(parent, title, QDir(start).filePath(text::safeFileName(suggestedName) + QLatin1String(".scriv")),
                                             QObject::tr("Scrivener projects (*.scriv)"));
    if (f.isEmpty())
        return QString();
    if (!f.endsWith(QLatin1String(".scriv"), Qt::CaseInsensitive))
        f += QLatin1String(".scriv");
    return f;
}

} // namespace

bool MainWindow::confirmNotOpenElsewhere(const QString &projectFolder)
{
    QFile lf(projectFolder + QLatin1String("/settings/lock"));
    if (!lf.open(QIODevice::ReadOnly))
        return true;
    const QStringList parts = QString::fromUtf8(lf.readAll()).split(QLatin1Char(' '));
    if (parts.size() != 2)
        return true;
    const bool sameHost = parts[0] == QSysInfo::machineHostName();
    const bool alive = sameHost && QFile::exists(QStringLiteral("/proc/") + parts[1])
                       && parts[1].toLongLong() != QCoreApplication::applicationPid();
    if (!alive && sameHost)
        return true;
    return QMessageBox::question(this, tr("Open Project"),
                                 tr("This project seems to be open in another window or on another computer (%1).\n\n"
                                    "Opening it twice can lose changes. Open it anyway?")
                                     .arg(parts[0]))
           == QMessageBox::Yes;
}

bool MainWindow::openScrivenerProject(const QString &path)
{
    const QString scrivDir = scrivener::scrivFolder(path);
    if (scrivener::scrivxIn(scrivDir).isEmpty() || !QFileInfo(scrivener::scrivxIn(scrivDir)).isFile()) {
        QMessageBox::warning(this, tr("Open Project"), tr("No Scrivener project was found at\n%1").arg(path));
        AppSettings::instance().removeRecent(path);
        return false;
    }
    if (m_project && QFileInfo(m_project->scrivenerPath) == QFileInfo(scrivDir))
        return true;
    const QString wc = scrivener::workingCopyFor(scrivDir);
    if (wc.isEmpty()) {
        // the first time: work on it in Scrivener format, or make a copy in our format
        QMessageBox box(QMessageBox::Question, tr("Open Scrivener Project"),
                        tr("\"%1\" is a Scrivener project.\n\n"
                           "Work in Scrivener format: %2 saves your changes into the .scriv, so you can go on in "
                           "Scrivener as well (not both programs at the same time).\n\n"
                           "Convert: %2 makes a project of its own from it; the Scrivener project is not changed.")
                            .arg(QFileInfo(scrivDir).fileName(), QStringLiteral(QB_APP_NAME)),
                        QMessageBox::NoButton, this);
        box.setObjectName(QStringLiteral("openScrivenerQuestion"));
        QPushButton *work = box.addButton(tr("Work in Scrivener Format"), QMessageBox::AcceptRole);
        work->setObjectName(QStringLiteral("workInScrivener"));
        QPushButton *convert = box.addButton(tr("Convert…"), QMessageBox::ActionRole);
        convert->setObjectName(QStringLiteral("convertScrivener"));
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(work);
        box.exec();
        if (box.clickedButton() == convert) {
            a[QStringLiteral("importScrivener")]->setData(scrivDir);
            a[QStringLiteral("importScrivener")]->trigger();
            a[QStringLiteral("importScrivener")]->setData(QVariant());
            return m_project != nullptr;
        }
        if (box.clickedButton() != work)
            return false;
    } else if (!confirmNotOpenElsewhere(wc)) {
        return false;
    }
    QString who;
    if (scrivener::lockedByScrivener(scrivDir, &who)
        && QMessageBox::question(this, tr("Open Scrivener Project"),
                                 tr("Scrivener has this project open (%1), or was not closed properly.\n\n"
                                    "If both programs work on it at the same time, changes are lost. Open it anyway?")
                                     .arg(who))
               != QMessageBox::Yes)
        return false;
    if (m_project && !closeProject())
        return false;
    QString err;
    scrivener::OpenResult r;
    {
        WaitCursor wait;
        r = scrivener::open(scrivDir, &err);
    }
    if (!r.project) {
        QMessageBox::warning(this, tr("Open Scrivener Project"), err.isEmpty() ? tr("The project could not be opened.") : err);
        return false;
    }
    const bool ok = adoptProject(std::move(r.project));
    QStringList notes;
    if (ok && r.reread && r.changedElsewhere > 0)
        notes << (r.changedElsewhere == 1
                      ? tr("1 document changed outside %1 since the last time; its new text was read.").arg(QStringLiteral(QB_APP_NAME))
                      : tr("%1 documents changed outside %2 since the last time; their new text was read.")
                            .arg(QString::number(r.changedElsewhere), QStringLiteral(QB_APP_NAME)));
    if (ok)
        notes << r.warnings;
    if (!notes.isEmpty())
        statusBar()->showMessage(notes.join(QLatin1Char(' ')), 10000);
    return ok;
}

void MainWindow::switchToScrivenerFormat()
{
    if (!m_project || !m_project->scrivenerPath.isEmpty()) {
        updateActions();
        return;
    }
    const QString dir = chooseScrivPath(this, tr("Save the Project in Scrivener Format"), m_project->title());
    if (dir.isEmpty()) {
        updateActions();
        return;
    }
    if (QFileInfo::exists(dir)) {
        QMessageBox::warning(this, tr("Project Format"), tr("There is a file or folder with this name already:\n%1").arg(dir));
        updateActions();
        return;
    }
    const QString oldFolder = m_project->folderPath();
    saveUiState();
    QString err;
    std::unique_ptr<Project> wc;
    {
        WaitCursor wait;
        wc = scrivener::convertToScrivener(m_project.get(), dir, &err);
    }
    if (!wc) {
        QMessageBox::warning(this, tr("Project Format"), err);
        updateActions();
        return;
    }
    // the working copy holds everything now; the window takes it
    adoptProject(std::move(wc));
    AppSettings::instance().removeRecent(QDir(oldFolder).filePath(QFileInfo(oldFolder).completeBaseName() + QLatin1String(QB_PROJECT_FILE_SUFFIX)));
    QMessageBox box(QMessageBox::Information, tr("Project Format"),
                    tr("From now on the project is saved in Scrivener format:\n%1\n\nThe project folder in %2's own format "
                       "stays as it was at this moment:\n%3\nYou can keep it as a copy or delete it.")
                        .arg(dir, QStringLiteral(QB_APP_NAME), oldFolder),
                    QMessageBox::Ok, this);
    box.setObjectName(QStringLiteral("formatSwitched"));
    box.exec();
}

void MainWindow::switchToQuillbinderFormat()
{
    if (!m_project || m_project->scrivenerPath.isEmpty()) {
        updateActions();
        return;
    }
    QString start = QFileInfo(m_project->scrivenerPath).absolutePath();
    const QString parent = QFileDialog::getExistingDirectory(
        this, tr("Where Should the Project in %1 Format Be Saved?").arg(QStringLiteral(QB_APP_NAME)), start);
    if (parent.isEmpty()) {
        updateActions();
        return;
    }
    const QString scriv = m_project->scrivenerPath;
    saveUiState();
    QString err;
    std::unique_ptr<Project> copy;
    {
        WaitCursor wait;
        copy = scrivener::copyAsQuillbinder(m_project.get(), parent, m_project->title(), &err);
    }
    if (!copy) {
        QMessageBox::warning(this, tr("Project Format"), err);
        updateActions();
        return;
    }
    const QString where = copy->folderPath();
    adoptProject(std::move(copy));
    QMessageBox box(QMessageBox::Information, tr("Project Format"),
                    tr("From now on the project is saved in %1's own format:\n%2\n\nThe Scrivener project stays as it was "
                       "at this moment:\n%3")
                        .arg(QStringLiteral(QB_APP_NAME), where, scriv),
                    QMessageBox::Ok, this);
    box.setObjectName(QStringLiteral("formatSwitched"));
    box.exec();
}

void MainWindow::saveCopyAsScrivener()
{
    if (!m_project)
        return;
    const QString dir = chooseScrivPath(this, tr("Save a Copy as Scrivener Project"), m_project->title());
    if (dir.isEmpty())
        return;
    if (QFileInfo::exists(dir)) {
        QMessageBox::warning(this, tr("Save a Copy"), tr("There is a file or folder with this name already:\n%1").arg(dir));
        return;
    }
    saveUiState();
    QString err;
    scrivener::WriteReport rep;
    bool ok;
    {
        WaitCursor wait;
        ok = scrivener::saveCopyAsScrivener(m_project.get(), dir, &rep, &err);
    }
    if (!ok) {
        QMessageBox::warning(this, tr("Save a Copy"), err);
        return;
    }
    QMessageBox box(QMessageBox::Information, tr("Save a Copy"),
                    tr("A copy of the project was saved in Scrivener format:\n%1\n\nThe project you are working on stays "
                       "where it is.")
                        .arg(dir),
                    QMessageBox::NoButton, this);
    box.setObjectName(QStringLiteral("copySaved"));
    QPushButton *show = box.addButton(tr("Show Folder"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Ok);
    box.exec();
    if (box.clickedButton() == show)
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(dir).absolutePath()));
}

void MainWindow::saveCopyAsQuillbinder()
{
    if (!m_project)
        return;
    const QString parent = QFileDialog::getExistingDirectory(
        this, tr("Save a Copy as %1 Project").arg(QStringLiteral(QB_APP_NAME)),
        QFileInfo(m_project->displayFolder()).absolutePath());
    if (parent.isEmpty())
        return;
    saveUiState();
    QString err;
    std::unique_ptr<Project> copy;
    {
        WaitCursor wait;
        copy = scrivener::copyAsQuillbinder(m_project.get(), parent, m_project->title(), &err);
    }
    if (!copy) {
        QMessageBox::warning(this, tr("Save a Copy"), err);
        return;
    }
    const QString where = copy->folderPath();
    copy.reset();
    QMessageBox box(QMessageBox::Information, tr("Save a Copy"),
                    tr("A copy of the project was saved in %1's own format:\n%2\n\nThe project you are working on stays "
                       "where it is.")
                        .arg(QStringLiteral(QB_APP_NAME), where),
                    QMessageBox::NoButton, this);
    box.setObjectName(QStringLiteral("copySaved"));
    QPushButton *show = box.addButton(tr("Show Folder"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Ok);
    box.exec();
    if (box.clickedButton() == show)
        QDesktopServices::openUrl(QUrl::fromLocalFile(parent));
}

} // namespace qb
