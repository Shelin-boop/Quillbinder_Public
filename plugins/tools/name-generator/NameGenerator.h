#pragma once
// Writing tool "Name Generator" (Edit > Writing Tools > Name Generator).
#include "NameLists.h"
#include "tools/ToolApi.h"

#include <QObject>
#include <QPointer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;

class NameGeneratorWindow : public QWidget
{
    Q_OBJECT
public:
    NameGeneratorWindow(qb::tools::ToolHost *host, QWidget *parent);
    namegen::Options options() const;

public slots:
    // Also called by the tests (QMetaObject::invokeMethod).
    void generate();
    void copyNames();
    void insertNames();
    bool addListFromFile(const QString &file, const QString &name, const QString &kind);
    void removeOwnList();
    void reloadLists();
    QStringList results() const;
    // Ticks exactly these lists (ids); empty = all.
    void chooseLists(const QStringList &forenames, const QStringList &surnames);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void fillListWidgets();
    void saveOptions();
    void restoreOptions();
    void updateButtons();
    QStringList ticked(QListWidget *w) const;

    qb::tools::ToolHost *m_host;
    QList<namegen::NameList> m_lists;
    QList<namegen::Name> m_names;
    QComboBox *m_gender, *m_show;
    QListWidget *m_fore, *m_sur;
    QCheckBox *m_same, *m_middle;
    QSpinBox *m_count;
    QSlider *m_foreObsc, *m_surObsc, *m_allit, *m_double;
    QLineEdit *m_foreStarts, *m_surStarts;
    QListWidget *m_results;
    QPushButton *m_copy, *m_insert, *m_removeList;
    QLabel *m_status;
};

class NameGeneratorTool : public QObject, public qb::tools::ToolInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QB_TOOL_IID)
    Q_INTERFACES(qb::tools::ToolInterface)
public:
    QString id() const override { return QStringLiteral("name-generator"); }
    QString name() const override { return tr("Name Generator"); }
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
    QPointer<NameGeneratorWindow> m_window;
};
