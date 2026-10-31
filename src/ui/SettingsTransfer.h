#pragma once
// Project > Save Project Settings to File / Load Project Settings from File (0.40.2).
//
// The window that reads a settings file (core/SettingsFile.h) into the open project shows
// every section with what the project has now and what the file has. Where they differ,
// the writer chooses: take the file's, keep the project's, or use what a new project gets
// ("Quillbinder default"). The main window carries the choices out
// (MainWindow::importProjectSettings, MainWindowSettings.cpp).

#include "core/SettingsFile.h"

#include <QDialog>
#include <QJsonObject>

class QComboBox;
class QLabel;
class QTreeWidget;

namespace qb {

class Project;

namespace settingstransfer {
// What a new project gets, section by section (the settings for new projects in File >
// Options, the built-in labels, status ...).
QJsonObject defaults();
} // namespace settingstransfer

class SettingsImportDialog : public QDialog
{
    Q_OBJECT
public:
    enum Choice { TakeFile = 0, KeepProject = 1, UseDefault = 2 };

    // file: the file's header and sections; project, defaults: the sections now and for a new project.
    SettingsImportDialog(const QString &fileName, const QJsonObject &file, const QJsonObject &project,
                         const QJsonObject &defaults, QWidget *parent = nullptr);

    int differing() const { return m_differing; }
    bool differs(settingsfile::Section s) const;
    Choice choice(settingsfile::Section s) const;
    void setChoice(settingsfile::Section s, Choice c);
    void setAll(Choice c);
    // the details shown for a section (also for the tests)
    QString details(settingsfile::Section s) const;

private:
    void showDetails();

    QTreeWidget *m_table = nullptr;
    QLabel *m_details = nullptr;
    QComboBox *m_combos[settingsfile::SectionCount] = {};
    QString m_texts[settingsfile::SectionCount];
    bool m_differs[settingsfile::SectionCount] = {};
    int m_differing = 0;
};

} // namespace qb
