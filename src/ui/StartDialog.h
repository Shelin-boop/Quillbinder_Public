#pragma once
#include "core/ProjectTemplates.h"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QListWidget;
class QLabel;
class QPushButton;

namespace qb {

// "New Project" window: project templates, recent projects, open and import.
class StartDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Action { None, Create, Open, ImportScrivener };
    explicit StartDialog(QWidget *parent = nullptr, bool showRecentFirst = false);

    Action action() const { return m_action; }
    templates::Info selectedTemplate() const;
    QString projectName() const;
    QString location() const;
    QString openPath() const { return m_openPath; }
    // "Format: Scrivener 3 project (.scriv)"
    bool scrivenerFormat() const;

private:
    void fillTemplates(const QString &category);
    void updateDescription();

    QListWidget *m_categories;
    QListWidget *m_templates;
    QLabel *m_description;
    QLineEdit *m_name;
    QLineEdit *m_location;
    QComboBox *m_format = nullptr;
    QPushButton *m_create;
    QList<templates::Info> m_all;
    Action m_action = Action::None;
    QString m_openPath;
};

} // namespace qb
