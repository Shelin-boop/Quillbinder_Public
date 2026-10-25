#pragma once
// Crash reports and the system snapshot in the window (0.19): Help > System
// Information, Help > Crash Reports, the notice after an unexpected end, and the
// recorder of recent actions for the reports (core/CrashGuard.h does the rest).
#include "core/CrashGuard.h"

#include <QDialog>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace qb {

class Project;

namespace sysinfo {
// Program, versions, add-ons, system, desktop, screens, memory, folders and their
// free space, the settings that matter for saving, the open project. The home
// folder is written as "~".
QString text(const Project *project);
// What may cause trouble soon: a nearly full or read-only drive, a network file
// system, a project too large for a backup, little memory, long autosave, no
// backups, recent crash or freeze reports. Also the first part of text().
QStringList concerns(const Project *project);
// Prepares the snapshot that a crash report carries (call when it changes).
void refresh(const Project *project);
// Records commands, shortcuts, buttons, windows and keys other than typing for the
// crash reports. Typed letters are only counted, never recorded.
void installActivityRecorder();
} // namespace sysinfo

class SystemInfoDialog : public QDialog
{
    Q_OBJECT
public:
    SystemInfoDialog(const Project *project, QWidget *parent = nullptr);
    QString text() const;

private:
    QPlainTextEdit *m_text;
};

class CrashReportsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CrashReportsDialog(QWidget *parent = nullptr);
    void select(const QString &file);
    int count() const;
    QString shownText() const;

private:
    void reload();
    void showCurrent();
    QListWidget *m_list;
    QPlainTextEdit *m_text;
    QLabel *m_where;
    QPushButton *m_copy, *m_delete, *m_deleteAll;
};

// After sessions that ended unexpectedly: tells the user what happened. Returns
// true when the user chose not to reopen the last project this time (offered when
// the program ended within a minute of its start and a project would be reopened).
bool showPreviousEnds(QWidget *parent, const QList<crash::PreviousEnd> &ends, bool lastProjectWouldOpen);

// File > Options > General > Show Internal Log Console (as in Scrivener): a window
// with the program's messages of this session, new ones added while it is open.
// Useful when something does not work and the reason is not shown elsewhere.
void showLogConsole(QWidget *parent);

} // namespace qb
