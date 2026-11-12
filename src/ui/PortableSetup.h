#pragma once
// Windows (port, W4b, 2026-11-12): the choices of the portable program that the first start's guide and File > Options
// > General > This Computer share: where Scratch Pad notes and transcriptions are kept (the writer's answer of
// 2026-11-12: "Menu setting and start up dialogue"), and the Start menu entry, the desktop shortcut and the project
// file types (each added or removed at once, ui/WinShell.h).
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

namespace qb {

class PortableSetup : public QWidget
{
    Q_OBJECT
public:
    explicit PortableSetup(QWidget *parent = nullptr);
    // A folder shown in a window: Windows' separators, and a line may break after each of them (a long path without
    // spaces would make the window wider than the screen).
    static QString shownPath(const QString &path);
    // Stores the place of notes and transcriptions (the shell items act at once).
    void apply();

private:
    void refreshShell();

    QRadioButton *m_documents;
    QRadioButton *m_program;
    QRadioButton *m_own;
    QLineEdit *m_ownFolder;
    struct ShellRow
    {
        int item;
        QLabel *state;
        QPushButton *button;
    };
    QList<ShellRow> m_shell;
};

} // namespace qb
