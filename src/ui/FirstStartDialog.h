#pragma once
// The questions of the first start (0.62): automatic backups (on or off, how many, which folder)
// and which settings come first (a project's own or the program's). Everything is filled in with
// the values in use, so OK alone keeps them; File > Options has the same settings. Shown once
// (key app/firstStartDone), also by an installation older than 0.62.
// Windows (W4b, the portable program): also the guide: where the program keeps everything, warnings for a folder in a
// sync service or with a long path, no standard backup folder, the place of notes and transcriptions, the Start menu
// entry, the desktop shortcut and the project file types (ui/PortableSetup.h). Help > This Computer shows it again.
#include <QDialog>

class QCheckBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;

namespace qb {
class PortableSetup;
}

namespace qb {

class FirstStartDialog : public QDialog
{
    Q_OBJECT
public:
    // again: opened from the Help menu (not the first start)
    explicit FirstStartDialog(QWidget *parent = nullptr, bool again = false);
    // Takes the choices over and remembers that the questions were answered.
    void take();
    // Whether the questions are still to be asked (QB_NO_DIALOGS and QB_SKIP_FIRST_START: never).
    static bool due();
    static void markDone();

private:
    QCheckBox *m_backup;
    QSpinBox *m_keep;
    QLineEdit *m_folder;
    PortableSetup *m_setup = nullptr;
};

} // namespace qb
