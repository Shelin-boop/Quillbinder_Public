#include "ui/ToolHost.h"

#include "AppInfo.h"
#include "compile/PluginManager.h"
#include "core/Addons.h"
#include "core/Launcher.h"
#include "core/Paths.h"
#include "text/TextUtil.h"
#include "ui/MainWindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <csignal>
#include <cstdio>
#include <unistd.h>

namespace qb {
namespace toolhost {

Host &Host::instance()
{
    static Host *h = new Host; // lives until the process ends (tools keep the pointer)
    return *h;
}

void Host::setWindow(MainWindow *w)
{
    m_window = w;
}

void Host::attachTools()
{
    if (m_attached)
        return;
    m_attached = true;
    for (tools::ToolInterface *t : compile::PluginManager::instance().tools())
        t->setHost(this);
}

QList<tools::Command> Host::allCommands() const
{
    QList<tools::Command> out;
    for (tools::ToolInterface *t : compile::PluginManager::instance().tools())
        out << t->commands();
    return out;
}

tools::ToolInterface *Host::toolFor(const QString &commandKey) const
{
    for (tools::ToolInterface *t : compile::PluginManager::instance().tools())
        for (const tools::Command &c : t->commands())
            if (c.key == commandKey)
                return t;
    return nullptr;
}

bool Host::runCommand(const QString &key)
{
    tools::ToolInterface *t = toolFor(key);
    if (!t)
        return false;
    attachTools();
    t->run(key);
    return true;
}

void Host::shutdownTools()
{
    for (tools::ToolInterface *t : compile::PluginManager::instance().tools())
        t->shutdown();
}

int Host::openToolWindows() const
{
    int n = 0;
    for (tools::ToolInterface *t : compile::PluginManager::instance().tools())
        n += t->openWindows();
    return n;
}

// ------------------------------------------------------------------ ToolHost

QString Host::appName() const { return QStringLiteral(QB_APP_NAME); }
QString Host::appVersion() const { return QStringLiteral(QB_APP_VERSION); }

QWidget *Host::window() const
{
    return m_window;
}

QStringList Host::contentDirs(const QString &kind) const
{
    QStringList dirs = addons::contentDirs(kind);
    // developers: extra data folders (QB_TOOL_DATA=<dir>, which holds <kind>/ sub-folders)
    for (const QString &d : qEnvironmentVariable("QB_TOOL_DATA").split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        const QString sub = QDir(d).filePath(kind);
        if (QFileInfo(sub).isDir())
            dirs.prepend(QDir::cleanPath(sub));
    }
    dirs.removeDuplicates();
    return dirs;
}

QString Host::dataDir(const QString &toolId) const
{
    const QString d = qb::dataDir() + QLatin1String("/tools/") + toolId;
    QDir().mkpath(d);
    return d;
}

QVariant Host::setting(const QString &toolId, const QString &key, const QVariant &def) const
{
    QSettings s;
    return s.value(QStringLiteral("tools/%1/%2").arg(toolId, key), def);
}

void Host::setSetting(const QString &toolId, const QString &key, const QVariant &value)
{
    QSettings s;
    s.setValue(QStringLiteral("tools/%1/%2").arg(toolId, key), value);
}

bool Host::hasProject() const { return m_window && m_window->project(); }
QString Host::projectTitle() const { return m_window ? m_window->toolProjectTitle() : QString(); }
bool Host::canInsertText() const { return m_window && m_window->toolCanInsert(); }
bool Host::insertText(const QString &text) { return m_window && m_window->toolInsertText(text); }
QString Host::selectedText() const { return m_window ? m_window->toolSelectedText() : QString(); }

QList<tools::TextDocument> Host::documents(tools::Scope scope) const
{
    return m_window ? m_window->toolDocuments(scope) : QList<tools::TextDocument>();
}

int Host::countWords(const QString &text) const { return text::countWords(text); }

void Host::showDocument(const QString &id)
{
    if (m_window)
        m_window->toolShowDocument(id);
}

void Host::searchProject(const QString &text)
{
    if (m_window)
        m_window->toolSearch(text);
}

QList<tools::ProjectInfo> Host::openProjects()
{
    QList<tools::ProjectInfo> out;
    auto add = [&out](const QJsonObject &o, const QString &key, bool self) {
        if (o.value(QStringLiteral("title")).toString().isEmpty())
            return; // an instance without a project (start window, standalone tool)
        tools::ProjectInfo pi;
        pi.key = key;
        pi.title = o.value(QStringLiteral("title")).toString();
        pi.file = o.value(QStringLiteral("file")).toString();
        pi.binder = o.value(QStringLiteral("binder")).toArray();
        pi.thisWindow = self;
        out << pi;
    };
    if (m_window && m_window->project())
        add(m_window->toolProjectInfo(), QStringLiteral("self"), true);
    for (const QString &peer : peers())
        add(query(peer, QJsonObject{{QStringLiteral("cmd"), QStringLiteral("info")}}), peer, false);
    // the same project open twice is not possible (lock file), so titles may repeat only for different files
    return out;
}

bool Host::sendToProject(const QString &key, const tools::SendRequest &request, QString *error)
{
    if (key == QLatin1String("self")) {
        if (!m_window || !m_window->project()) {
            if (error)
                *error = tr("No project is open in this window.");
            return false;
        }
        return m_window->receiveFromTool(request, error);
    }
    const QJsonObject reply = query(key, QJsonObject{{QStringLiteral("cmd"), QStringLiteral("send")},
                                                    {QStringLiteral("request"), toJson(request)}},
                                    30000);
    if (reply.isEmpty()) {
        if (error)
            *error = tr("The other window did not answer (it may have been closed).");
        return false;
    }
    if (!reply.value(QStringLiteral("ok")).toBool()) {
        if (error)
            *error = reply.value(QStringLiteral("error")).toString();
        return false;
    }
    return true;
}

QString Host::commandLine(const QString &commandKey) const
{
    // the AppImage when running from one (the installed program update starts through it as well)
    QString exe = qEnvironmentVariable("APPIMAGE");
    if (exe.isEmpty())
        exe = QCoreApplication::applicationFilePath();
    QString quoted = exe;
    if (quoted.contains(QLatin1Char(' ')))
        quoted = QLatin1Char('"') + quoted + QLatin1Char('"');
    return QStringLiteral("%1 --tool %2").arg(quoted, commandKey);
}

// ------------------------------------------------------------------ instance link

QString Host::linkDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    QString dir;
    if (!base.isEmpty() && QFileInfo(base).isWritable())
        dir = base + QLatin1String("/" QB_APP_ID);
    else
        dir = QDir::tempPath() + QStringLiteral("/" QB_APP_ID "-%1").arg(getuid());
    if (!QFileInfo::exists(dir)) {
        QDir().mkpath(dir);
        QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    return dir;
}

QString Host::linkPath() const
{
    return linkDir() + QStringLiteral("/%1.sock").arg(QCoreApplication::applicationPid());
}

bool Host::startLink()
{
    if (m_server)
        return m_server->isListening();
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    const QString path = linkPath();
    QLocalServer::removeServer(path);
    if (!m_server->listen(path))
        return false;
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        if (qEnvironmentVariableIsSet("QB_DEBUG_LINK"))
            std::fprintf(stderr, "link: new connection\n");
        while (QLocalSocket *s = m_server->nextPendingConnection()) {
            connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
            auto serve = [this, s] {
                if (qEnvironmentVariableIsSet("QB_DEBUG_LINK"))
                    std::fprintf(stderr, "link: data %d line %d\n", int(s->bytesAvailable()), int(s->canReadLine()));
                if (!s->canReadLine() || s->property("qbAnswered").toBool())
                    return;
                s->setProperty("qbAnswered", true);
                const QJsonObject req = QJsonDocument::fromJson(s->readLine()).object();
                const QJsonObject reply = handle(req);
                s->write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n');
                s->flush();
            };
            connect(s, &QLocalSocket::readyRead, this, serve);
            // the request may be here already (read while the connection was accepted)
            QTimer::singleShot(0, s, serve);
        }
    });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [path] { QLocalServer::removeServer(path); });
    return true;
}

QStringList Host::peers() const
{
    QStringList out;
    const QString own = linkPath();
    QDir d(linkDir());
    for (const QFileInfo &fi : d.entryInfoList({QStringLiteral("*.sock")}, QDir::System | QDir::Files)) {
        const QString path = fi.absoluteFilePath();
        if (path == own)
            continue;
        const qint64 pid = fi.completeBaseName().toLongLong();
        if (pid <= 0 || ::kill(pid_t(pid), 0) != 0) {
            QFile::remove(path); // left behind by a process that ended without cleaning up
            continue;
        }
        out << path;
    }
    return out;
}

QJsonObject Host::query(const QString &socket, const QJsonObject &request, int timeoutMs)
{
    QLocalSocket s;
    s.connectToServer(socket);
    if (!s.waitForConnected(qMin(timeoutMs, 1500)))
        return {};
    s.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    if (!s.waitForBytesWritten(timeoutMs))
        return {};
    QByteArray line;
    QElapsedTimer t;
    t.start();
    while (!line.endsWith('\n')) {
        if (!s.bytesAvailable() && !s.waitForReadyRead(qMax(1, timeoutMs - int(t.elapsed()))))
            break;
        line += s.readAll();
    }
    s.disconnectFromServer();
    return QJsonDocument::fromJson(line).object();
}

QJsonObject Host::handle(const QJsonObject &req)
{
    const QString cmd = req.value(QStringLiteral("cmd")).toString();
    if (cmd == QLatin1String("info")) {
        QJsonObject o;
        if (m_window && m_window->project())
            o = m_window->toolProjectInfo();
        o.insert(QStringLiteral("pid"), QCoreApplication::applicationPid());
        o.insert(QStringLiteral("standalone"), m_standalone);
        o.insert(QStringLiteral("version"), QStringLiteral(QB_APP_VERSION));
        return o;
    }
    if (cmd == QLatin1String("send")) {
        QString error;
        bool ok = false;
        if (!m_window || !m_window->project())
            error = tr("That window has no project open any more.");
        else
            ok = m_window->receiveFromTool(sendRequestFromJson(req.value(QStringLiteral("request")).toObject()), &error);
        return QJsonObject{{QStringLiteral("ok"), ok}, {QStringLiteral("error"), error}};
    }
    if (cmd == QLatin1String("run")) {
        const QString key = req.value(QStringLiteral("key")).toString();
        return QJsonObject{{QStringLiteral("ok"), runCommand(key)}};
    }
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("unknown request")}};
}

QJsonObject toJson(const tools::SendRequest &r)
{
    return QJsonObject{{QStringLiteral("file"), r.file},
                       {QStringLiteral("title"), r.title},
                       {QStringLiteral("mode"), r.mode},
                       {QStringLiteral("targetId"), r.targetId}};
}

tools::SendRequest sendRequestFromJson(const QJsonObject &o)
{
    tools::SendRequest r;
    r.file = o.value(QStringLiteral("file")).toString();
    r.title = o.value(QStringLiteral("title")).toString();
    r.mode = o.value(QStringLiteral("mode")).toString();
    r.targetId = o.value(QStringLiteral("targetId")).toString();
    return r;
}

} // namespace toolhost
} // namespace qb
