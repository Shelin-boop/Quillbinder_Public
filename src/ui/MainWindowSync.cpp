// A project in a folder that a sync program (OneDrive, Dropbox, Nextcloud, Syncthing ...) keeps
// the same on several computers (0.64): the lock that names the computer that has the project
// open, the notice when another computer takes it over, the versions kept on conflicts, and the
// texts whose files have not arrived yet. Works on files only (core/SyncSafety.h), whatever
// the sync program is.
#include "ui/MainWindow.h"

#include "core/DocumentStore.h"
#include "core/Platform.h"
#include "core/SyncSafety.h"

#include "AppInfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>

namespace qb {

namespace {

QString localTime(const QDateTime &utc)
{
    return utc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

QString ago(const QDateTime &utc)
{
    const qint64 mins = utc.secsTo(QDateTime::currentDateTimeUtc()) / 60;
    if (mins < 1)
        return QObject::tr("less than a minute ago");
    if (mins < 120)
        return QObject::tr("%n minute(s) ago", nullptr, int(mins));
    return localTime(utc);
}

} // namespace

QStringList MainWindow::lockFilesFor(const Project *p) const
{
    QStringList out;
    if (!p)
        return out;
    out << p->settingsDir() + QLatin1String("/lock");
    // a project in Scrivener format: its working copy is on this computer; the .scriv, which a
    // sync program may carry, gets a lock of this program too (Scrivener does not read it)
    if (!p->scrivenerPath.isEmpty())
        out << p->scrivenerPath + QLatin1String("/quillbinder.lock");
    return out;
}

bool MainWindow::confirmLock(const QString &lockFile)
{
    const syncsafe::LockInfo l = syncsafe::readLock(lockFile);
    const syncsafe::LockState st = syncsafe::judge(l, syncsafe::staleAfterSeconds(), platform::processAlive);
    QString text;
    const char *name = "";
    bool openIsDefault = false;
    switch (st) {
    case syncsafe::LockState::None:
    case syncsafe::LockState::Ours:
    case syncsafe::LockState::LeftOverHere:
        return true;
    case syncsafe::LockState::OtherWindow:
        name = "lockOtherWindow";
        text = tr("This project is open in another %1 window on this computer.\n\nOpening it twice can lose changes. "
                  "Open it anyway?")
                   .arg(QStringLiteral(QB_APP_NAME));
        break;
    case syncsafe::LockState::OtherDevice:
        name = "lockOtherDevice";
        text = tr("This project is open on the computer “%1”%2 since %3 (its last sign of life: %4).\n\n"
                  "If it is open here as well, the two computers can overwrite each other's work. Close it there "
                  "first and let your sync program finish, then open it here.")
                   .arg(l.device, l.user.isEmpty() ? QString() : tr(" (user %1)").arg(l.user),
                        l.opened.isValid() ? localTime(l.opened) : tr("an unknown time"), ago(l.alive));
        break;
    case syncsafe::LockState::OtherDeviceStale:
        name = "lockLeftOver";
        openIsDefault = true;
        text = tr("This project was not closed on the computer “%1”: its last sign of life there was %2.\n\n"
                  "If the project is closed there, or that computer is switched off, open it here. Let your sync "
                  "program finish first.")
                   .arg(l.device, localTime(l.alive));
        break;
    }
    QMessageBox box(QMessageBox::Warning, tr("Open Project"), text, QMessageBox::NoButton, this);
    box.setObjectName(QLatin1String(name));
    QPushButton *open = box.addButton(openIsDefault ? tr("Open") : tr("Open Anyway"), QMessageBox::AcceptRole);
    open->setObjectName(QStringLiteral("lockOpen"));
    QPushButton *cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(openIsDefault ? open : cancel);
    box.exec();
    return box.clickedButton() == open;
}

void MainWindow::takeLocks()
{
    m_lockOpened = QDateTime::currentDateTimeUtc();
    m_lockLost = false;
    for (const QString &f : lockFilesFor(m_project.get()))
        syncsafe::writeLock(f, m_lockOpened);
    if (!m_lockTimer) {
        m_lockTimer = new QTimer(this);
        connect(m_lockTimer, &QTimer::timeout, this, [this] { renewLocks(true); });
    }
    m_lockTimer->start(syncsafe::renewIntervalMs());
}

void MainWindow::renewLocks(bool write)
{
    if (!m_project || m_lockLost)
        return;
    for (const QString &f : lockFilesFor(m_project.get())) {
        const syncsafe::LockInfo l = syncsafe::readLock(f);
        const syncsafe::LockState st = syncsafe::judge(l, syncsafe::staleAfterSeconds(), platform::processAlive);
        if (st == syncsafe::LockState::OtherDevice || st == syncsafe::LockState::OtherWindow) {
            // another computer (or window) opened the project while it is open here
            m_lockLost = true;
            if (m_lockTimer)
                m_lockTimer->stop();
            auto *box = new QMessageBox(QMessageBox::Warning, tr("Project Opened Elsewhere"),
                                        tr("The project was opened on the computer “%1” at %2 while it is open here.\n\n"
                                           "Work on it on one computer at a time: changes made on both can get in each "
                                           "other's way. Your changes up to now are saved. Close the project here?")
                                            .arg(l.device, l.opened.isValid() ? localTime(l.opened) : ago(l.alive)),
                                        QMessageBox::NoButton, this);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->setObjectName(QStringLiteral("lockTakenNotice"));
            QPushButton *close = box->addButton(tr("Close the Project Here"), QMessageBox::AcceptRole);
            close->setObjectName(QStringLiteral("lockTakenClose"));
            QPushButton *keep = box->addButton(tr("Keep Working"), QMessageBox::RejectRole);
            box->setDefaultButton(keep);
            box->setEscapeButton(keep);
            connect(box, &QMessageBox::buttonClicked, this, [this, close](QAbstractButton *b) {
                if (b == close)
                    QTimer::singleShot(0, this, [this] { closeProject(); });
            });
            showWithoutTakingKeys(box, keep);
            return;
        }
        if (write || !l.exists)
            syncsafe::writeLock(f, m_lockOpened);
    }
}

void MainWindow::releaseLocks()
{
    if (m_lockTimer)
        m_lockTimer->stop();
    for (const QString &f : lockFilesFor(m_project.get()))
        syncsafe::removeOwnLock(f);
}

void MainWindow::connectSyncSafety(Project *p)
{
    connect(p, &Project::conflictKept, this, [this](const QString &itemId, const QString &what, const QString &copy, bool snapshot) {
        BinderItem *it = m_project && !itemId.isEmpty() ? m_project->item(itemId) : nullptr;
        QString line = it ? QStringLiteral("• “%1” (%2)").arg(it->title, what) : QStringLiteral("• %1").arg(what);
        if (snapshot)
            line += QLatin1String(": ") + tr("a snapshot of the document");
        if (!copy.isEmpty())
            line += (snapshot ? tr(", and ") : QStringLiteral(": ")) + tr("a copy in %1").arg(QDir::toNativeSeparators(copy));
        m_conflictNotes << line;
        QTimer::singleShot(0, this, &MainWindow::showConflictNotice);
    });
    connect(p, &Project::saved, this, [this] { renewLocks(false); });
    connect(p->docs(), &DocumentStore::missingFile, this, [this](const QString &id, const QString &) {
        BinderItem *it = m_project ? m_project->item(id) : nullptr;
        statusBar()->showMessage(tr("“%1”: its text has not arrived on this computer yet; it is shown empty and "
                                    "cannot be edited until the project is opened again.")
                                     .arg(it ? it->title : id),
                                 15000);
    });
    const QStringList missing = p->docs()->textsNotArrived();
    if (!missing.isEmpty()) {
        QStringList titles;
        for (const QString &id : missing.mid(0, 8))
            if (BinderItem *it = p->item(id))
                titles << QStringLiteral("• ") + it->title;
        if (missing.size() > 8)
            titles << tr("... and %n more", nullptr, int(missing.size() - 8));
        auto *box = new QMessageBox(QMessageBox::Warning, tr("Open Project"),
                                    tr("%n document(s) of this project have text, but their files are not on this computer "
                                       "(yet). Most likely your sync program has not finished.\n\n%1\n\nThey are shown "
                                       "empty, cannot be edited and are never saved over. Close the project, wait until "
                                       "the sync program is done, then open it again.",
                                       nullptr, int(missing.size()))
                                        .arg(titles.join(QLatin1Char('\n'))),
                                    QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->setObjectName(QStringLiteral("notArrivedNotice"));
        box->open();
    }
}

void MainWindow::showConflictNotice()
{
    if (m_conflictNotes.isEmpty())
        return;
    const QStringList lines = m_conflictNotes;
    m_conflictNotes.clear();
    auto *box = new QMessageBox(QMessageBox::Information, tr("Other Versions Kept"),
                                tr("Some files of the project were changed elsewhere (on another computer through your "
                                   "sync program, or by another program) after %1 read them, and were changed here as well. "
                                   "The other versions were kept before your changes were saved:\n\n%2\n\nA snapshot can be "
                                   "compared with the text in the inspector's Snapshots tab. To avoid this, work on the "
                                   "project on one computer at a time.")
                                    .arg(QStringLiteral(QB_APP_NAME), lines.join(QLatin1Char('\n'))),
                                QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setObjectName(QStringLiteral("conflictNotice"));
    showWithoutTakingKeys(box, box->button(QMessageBox::Ok));
}

// 0.64.2: a notice that comes after a save, while the writer may be typing, does not take the keys: it is shown
// beside the window (not modal, not made the active window), so the next keys still go into the text, and a Space
// meant for the text cannot press a button (before, the keys went into the notice, and a Space pressed the
// focused button: "Close the Project Here" closed the project, and the typing after it went nowhere: X-10).
void MainWindow::showWithoutTakingKeys(QMessageBox *box, QAbstractButton *safe)
{
    box->setWindowModality(Qt::NonModal);
    box->setAttribute(Qt::WA_ShowWithoutActivating);
    box->show();
    if (safe)
        safe->setFocus(Qt::OtherFocusReason);   // when the writer does go to the notice
}

} // namespace qb
