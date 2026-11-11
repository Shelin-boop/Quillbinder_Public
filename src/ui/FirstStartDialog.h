#pragma once
// The questions of the first start (0.62): automatic backups (on or off, how many, which folder)
// and which settings come first (a project's own or the program's). Everything is filled in with
// the values in use, so OK alone keeps them; File > Options has the same settings. Shown once
// (key app/firstStartDone), also by an installation older than 0.62.
#include <QDialog>

class QCheckBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;

namespace qb {

class FirstStartDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FirstStartDialog(QWidget *parent = nullptr);
    // Takes the choices over and remembers that the questions were answered.
    void take();
    // Whether the questions are still to be asked (QB_NO_DIALOGS and QB_SKIP_FIRST_START: never).
    static bool due();
    static void markDone();

private:
    QCheckBox *m_backup;
    QSpinBox *m_keep;
    QLineEdit *m_folder;
};

} // namespace qb
