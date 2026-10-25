#pragma once
#include "core/Addons.h"

#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;
class QTreeWidget;
class QTextBrowser;

namespace qb {

// Shows what a package contains and installs it after confirmation.
// Returns true if something was installed. If the program has to be
// restarted and the user agrees, *restart is set.
bool runInstallPackage(QWidget *parent, const QString &file, bool *restart = nullptr);

// Starts the program again (through the AppImage, so that a newly
// installed program version is picked up).
bool relaunchApplication();

// Readable description of a package for the install window (HTML).
QString describePackage(const addons::PackageInfo &info);

// Help > Add-on Manager
class AddonManagerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AddonManagerDialog(QWidget *parent = nullptr);
    bool restartRequested() const { return m_restart; }

signals:
    void contentChanged(); // themes, templates ... may have changed

private:
    void refresh();
    void updateButtons();
    void installFromFile();

    QTreeWidget *m_versions;
    QTreeWidget *m_addons;
    QTextBrowser *m_details;
    QPushButton *m_useVersion, *m_removeVersion, *m_removeAddon;
    QLabel *m_restartHint;
    bool m_restart = false;
    bool m_updating = false;
};

} // namespace qb
