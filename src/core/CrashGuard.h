#pragma once
// Crash reports and sessions (0.19).
//
// install() sets handlers for the fatal signals (segmentation fault, abort, bus
// error, floating point error, illegal instruction). When one of them arrives,
// a report is written into reportDir() with calls that are safe inside a signal
// handler (no memory is allocated, no locks are taken): the snapshot of the
// program and the system prepared beforehand (setSnapshot), the recent actions
// (note), the recent warnings of Qt, a backtrace, the memory of the process and
// the program files in memory (to translate the addresses later). Then the
// signal goes on to the system as before (core dump, exit status).
//
// startWatchdog() writes a freeze report in the same way when the main thread stops
// answering (an endless loop, a wait that does not end).
//
// A session marker per main window process tells the next start whether the
// last session ended normally (beginSession / endSession). onTermination makes
// SIGTERM, SIGINT and SIGHUP (log out, shutdown, kill) save the work first.
//
// Nothing here talks to the network: reports stay on the computer.
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace qb {
namespace crash {

// ~/.local/share/<App>/CrashReports
QString reportDir();
// ~/.local/share/<App>/sessions
QString sessionDir();

// Installs the handlers (once; later calls do nothing).
void install();
bool isInstalled();

// The text of "Program and system" that a report carries (UTF-8; at most 32 KiB are kept).
void setSnapshot(const QByteArray &text);
// One line of "Recent actions" (a command, a key, a window); never document text.
void note(const QString &what);
// Like note(), but replaces the last line when that came from the same group
// (typing: "typing: 57 keys" grows instead of adding a line per key).
void noteInGroup(const QString &group, const QString &what);
// The recent actions as a report would show them (oldest first).
QString recentActions();
// The build ID of the running program file (hex), empty when unknown.
QString buildId();
// Seconds since install() (or since the program started).
qint64 secondsSinceStart();
// The program's messages of this session (Qt notes and warnings, at most the last
// 1000 lines), for File > Options > General > Show Internal Log Console (0.21).
// logCount() counts every line ever added, so a window can show only new ones.
QStringList logLines();
quint64 logCount();

// ---- sessions of the main window
struct PreviousEnd {
    qint64 pid = 0;
    QDateTime started;
    QString version;
    QString project;   // the project open at the end, as far as known
    QString report;    // the crash report (or the freeze report of a freeze it did not come out of);
                       // empty: it ended without one (killed, power cut)
    QDateTime crashed; // time of that report
};
// Starts the marker of this process; returns how earlier sessions ended that did
// not end normally (newest first) and removes their markers.
QList<PreviousEnd> beginSession();
void setSessionProject(const QString &path);
void endSession();

// SIGTERM, SIGINT, SIGHUP: `save` runs in the main thread (from the event loop),
// then the session ends and the process exits with status 0.
void onTermination(std::function<void()> save);

// ---- freezes
// Watches the main thread from a thread of its own. When the main thread has not
// answered for `seconds`, a freeze report is written (freeze-<time>-<pid>.txt: the
// same snapshot, recent actions and warnings as a crash report, and where the main
// thread is stuck, taken with SIGUSR2). When it answers again, the report gets the
// length of the freeze. Needs install() first; a second call changes the time only.
// Nothing is written while a debugger holds the program.
void startWatchdog(int seconds = 30);
// A new time for the running watchdog (File > Options > Extras > Crash Reports, 0.21).
void setFreezeSeconds(int seconds);

// ---- reports
struct Report {
    QString file;
    QDateTime when;
    qint64 pid = 0;
    QString kind = QStringLiteral("crash"); // "crash" or "freeze"
    QString signal;   // "SIGSEGV" ("freeze" for a freeze report)
    QString meaning;  // "segmentation fault"
    QString version;
    bool recovered = false; // a freeze after which the program answered again
};
QList<Report> reports(); // crash and freeze reports, newest first
Report readHeader(const QString &file);
// The report with C++ names made readable (qb::TextColumnView::page ... instead of _ZN2qb...).
QString readable(const QString &text);
// Keeps the newest `keep` reports.
void prune(int keep = 20);

} // namespace crash
} // namespace qb
