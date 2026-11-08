// A writing tool built against the tool interface 1.0 (ToolApi10.h, as 0.11 to 0.60 had it), for
// the 0.61 tests: a program with interface 1.1 must still load it, give it its host and run it.
#include "ToolApi10.h"

#include <QObject>
#include <QVariant>

class OldTool : public QObject, public qb::tools::ToolInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QB_TOOL_IID)
    Q_INTERFACES(qb::tools::ToolInterface)
public:
    QString id() const override { return QStringLiteral("old-tool"); }
    QString name() const override { return QStringLiteral("Old Tool"); }
    QString version() const override { return QStringLiteral("1.0"); }
    QList<qb::tools::Command> commands() const override
    {
        qb::tools::Command c;
        c.key = QStringLiteral("old-tool.write");
        c.text = QStringLiteral("Old Tool Writes");
        c.place = QStringLiteral("edit/writing-tools");
        return {c};
    }
    void setHost(qb::tools::ToolHost *host) override { m_host = host; }
    void run(const QString &key) override
    {
        if (key != QLatin1String("old-tool.write") || !m_host)
            return;
        // what a 1.0 tool does: ask the host, write at the cursor, keep a setting
        const int v = m_host->apiVersion();
        m_host->setSetting(id(), QStringLiteral("apiVersion"), v);
        m_host->insertText(QStringLiteral("[old tool, host %1]").arg(v));
    }
    QString optionsPage() const override { return QString(); }
    qb::tools::OptionsSection *createOptions(QWidget *) override { return nullptr; }
    void shutdown() override {}
    int openWindows() const override { return 0; }

private:
    qb::tools::ToolHost *m_host = nullptr;
};

#include "OldTool.moc"
