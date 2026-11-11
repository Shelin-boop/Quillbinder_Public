#pragma once
// 0.70.0 (audit A F-03, B S-3): yes-or-no questions with a default that is safe. Qt's two-argument
// QMessageBox::question() makes Yes the default under the Fusion style (KDE), so Enter or a Space typed too early
// answered "Delete ... forever?" with Yes. Every question names its default here; a question before a step that
// cannot be undone names the step on its button ("Delete Snapshot") and has Cancel as default and escape button.
// Header-only: the writing tools (plugins) use it as well.
#include <QAbstractButton>
#include <QCoreApplication>
#include <QMessageBox>

namespace qb {
namespace ask {

inline QMessageBox::StandardButton question(QWidget *parent, const QString &title, const QString &text,
                                            QMessageBox::StandardButtons buttons,
                                            QMessageBox::StandardButton defaultButton, const QString &action = QString())
{
    QMessageBox box(action.isEmpty() ? QMessageBox::Question : QMessageBox::Warning, title, text, buttons, parent);
    if (!action.isEmpty()) {
        if (QAbstractButton *b = box.button(QMessageBox::Yes))
            b->setText(action);
        if (QAbstractButton *b = box.button(QMessageBox::No))
            b->setText(QCoreApplication::translate("qb::ask", "Cancel"));
    }
    box.setDefaultButton(defaultButton);
    if (box.button(QMessageBox::Cancel))
        box.setEscapeButton(QMessageBox::Cancel);
    else if (box.button(QMessageBox::No))
        box.setEscapeButton(QMessageBox::No);
    if (QAbstractButton *d = box.button(defaultButton))
        d->setFocus();
    const int r = box.exec();
    return box.clickedButton() ? box.standardButton(box.clickedButton()) : QMessageBox::StandardButton(r);
}

// Before a step that cannot be undone: true only when the button that names the step was pressed.
inline bool confirm(QWidget *parent, const QString &title, const QString &text, const QString &action)
{
    return question(parent, title, text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No, action) == QMessageBox::Yes;
}

} // namespace ask
} // namespace qb
