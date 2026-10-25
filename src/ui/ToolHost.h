#pragma once
// The program's side of the writing-tool interface (tools/ToolApi.h) and the
// link between running instances of the program.
//
// One Host per process. It gives the tools what they ask for from the main
// window (MainWindow::tool* functions) or, while the program runs a
// standalone command (--tool <key>, no main window), from nobody.
//
// Instance link: every process listens on a local socket
// $XDG_RUNTIME_DIR/<app id>/<pid>.sock (else /tmp/<app id>-<uid>/). A
// request is one line of JSON, the answer as well:
//   {"cmd":"info"}                 -> {"pid", "title", "file", "standalone", "binder"}
//   {"cmd":"send", "request":{..}} -> {"ok", "error"}     (SendRequest)
//   {"cmd":"run", "key":".."}      -> {"ok"}              (a tool command, e.g. show the scratch pad)
// So the scratch pad of one window can send notes to the project of another
// one, and `--tool scratch-pad.show` finds a scratch pad that is open already.

#include "tools/ToolApi.h"

#include <QJsonObject>
#include <QObject>

class QLocalServer;
class QWidget;

namespace qb {

class MainWindow;

namespace toolhost {

class Host : public QObject, public tools::ToolHost
{
    Q_OBJECT
public:
    static Host &instance();

    // ---- set-up
    void setWindow(MainWindow *w);           // null when the window goes
    MainWindow *mainWindow() const { return m_window; }
    // Gives every loaded tool the host (once).
    void attachTools();
    // Runs a command of a tool ("scratch-pad.show"); false if no tool has it.
    bool runCommand(const QString &key);
    QList<tools::Command> allCommands() const;
    tools::ToolInterface *toolFor(const QString &commandKey) const;
    // Asks every tool to save its work.
    void shutdownTools();
    int openToolWindows() const;
    // Standalone run: the command this process was started for.
    void setStandaloneCommand(const QString &key) { m_standalone = key; }
    QString standaloneCommand() const { return m_standalone; }

    // ---- instance link
    bool startLink();                         // listen; false if that failed (tools still work in this window)
    QString linkPath() const;                 // own socket
    static QString linkDir();
    // Other running instances (sockets that answer); stale sockets are removed.
    QStringList peers() const;
    static QJsonObject query(const QString &socket, const QJsonObject &request, int timeoutMs = 3000);

    // signals for the tools (ToolHost::events)
    void notifyProjectChanged() { emit projectChanged(); }
    void notifySettingsChanged() { emit settingsChanged(); }

    // ---- tools::ToolHost
    int apiVersion() const override { return 100; }
    QString appName() const override;
    QString appVersion() const override;
    QWidget *window() const override;
    QStringList contentDirs(const QString &kind) const override;
    QString dataDir(const QString &toolId) const override;
    QVariant setting(const QString &toolId, const QString &key, const QVariant &def = QVariant()) const override;
    void setSetting(const QString &toolId, const QString &key, const QVariant &value) override;
    bool hasProject() const override;
    QString projectTitle() const override;
    bool canInsertText() const override;
    bool insertText(const QString &text) override;
    QString selectedText() const override;
    QList<tools::TextDocument> documents(tools::Scope scope) const override;
    int countWords(const QString &text) const override;
    void showDocument(const QString &id) override;
    void searchProject(const QString &text) override;
    QList<tools::ProjectInfo> openProjects() override;
    bool sendToProject(const QString &key, const tools::SendRequest &request, QString *error) override;
    QString commandLine(const QString &commandKey) const override;
    QObject *events() const override { return const_cast<Host *>(this); }

signals:
    void projectChanged();
    void settingsChanged();

private:
    Host() = default;
    QJsonObject handle(const QJsonObject &request);
    MainWindow *m_window = nullptr;   // cleared by ~MainWindow
    QLocalServer *m_server = nullptr;
    QString m_standalone;
    bool m_attached = false;
};

inline Host &host() { return Host::instance(); }

// Request <-> JSON (also used by the tests).
QJsonObject toJson(const tools::SendRequest &r);
tools::SendRequest sendRequestFromJson(const QJsonObject &o);

} // namespace toolhost
} // namespace qb
