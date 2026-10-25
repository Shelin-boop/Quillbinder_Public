#pragma once
// Writing tool "Text Statistics" (Project > Text Statistics).
#include "TextAnalysis.h"
#include "tools/ToolApi.h"

#include <QObject>
#include <QPointer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTreeWidget;

class TextStatisticsWindow : public QWidget
{
    Q_OBJECT
public:
    TextStatisticsWindow(qb::tools::ToolHost *host, QWidget *parent);

public slots:
    // also called by the tests (QMetaObject::invokeMethod)
    void refresh();
    void setScope(int scope);                  // qb::tools::Scope
    void setLanguage(int language);            // textstats::Language
    QString overviewValue(const QString &key) const;
    QStringList topWords(int n) const;         // "word=count"
    int wordCountOf(const QString &word) const;
    QStringList phraseList() const;            // "phrase=count"
    int documentRows() const;
    bool exportCsv(const QString &file, int tab);

private:
    void fillOverview();
    void fillWords();
    void fillPhrases();
    void fillDocuments();

    qb::tools::ToolHost *m_host;
    QList<textstats::Input> m_inputs;
    textstats::Result m_result;
    QComboBox *m_scope, *m_lang;
    QLabel *m_summary;
    QTabWidget *m_tabs;
    QTreeWidget *m_overview;
    QTableWidget *m_words, *m_phrases, *m_docs;
    QCheckBox *m_hideCommon, *m_case;
    QSpinBox *m_minLength, *m_phraseMin, *m_phraseLen;
    QLineEdit *m_filter;
};

class TextStatisticsTool : public QObject, public qb::tools::ToolInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QB_TOOL_IID)
    Q_INTERFACES(qb::tools::ToolInterface)
public:
    QString id() const override { return QStringLiteral("text-statistics"); }
    QString name() const override { return tr("Text Statistics"); }
    QString version() const override { return QStringLiteral("1.0"); }
    QList<qb::tools::Command> commands() const override;
    void setHost(qb::tools::ToolHost *host) override { m_host = host; }
    void run(const QString &key) override;
    QString optionsPage() const override { return QString(); }
    qb::tools::OptionsSection *createOptions(QWidget *) override { return nullptr; }
    void shutdown() override;
    int openWindows() const override { return m_window && m_window->isVisible() ? 1 : 0; }

private:
    qb::tools::ToolHost *m_host = nullptr;
    QPointer<TextStatisticsWindow> m_window;
};
