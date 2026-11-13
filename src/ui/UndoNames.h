#pragma once
// 0.90.1 (the usability review of 0.90.0, UR-12): Undo and Redo say what they will undo or redo ("Undo Typing",
// "Undo Paste", "Undo Bold"), as other writing programs do, and the arrows beside the toolbar's Undo and Redo list the
// steps (choosing one undoes it and every later one). Undo and Redo act on the text field that has the keyboard; with
// the keyboard elsewhere (the binder) they are off, so after Empty Trash an enabled Undo no longer looks as if it could
// bring the documents back.
//
// Qt's text documents keep no names for their undo steps. A small book of names hangs on each document (a child
// object): every new undo step gets the name of the command that made it (setPending, called for every menu command
// and for typing and deleting in the editor), or one worked out from the change (text added: Typing, text removed:
// Delete, the same text in another format: Formatting). Steps merged by Qt (a run of typing) keep the first name.
#include <QString>
#include <QStringList>

class QAction;
class QTextDocument;
class QObject;

namespace qb {
namespace undonames {

// the name the next undo step of doc takes (the command being carried out); forgotten when the event loop runs again
void setPending(QTextDocument *doc, const QString &name);
// the same for the document of every text field that may change now (the menu command's target): the focused one
void setPendingForFocus(const QString &name);
// the name of the step Undo (Redo) would take back (empty: none)
QString nextUndo(QTextDocument *doc);
QString nextRedo(QTextDocument *doc);
// every step Undo (Redo) can take back, the next one first
QStringList undoSteps(QTextDocument *doc);
QStringList redoSteps(QTextDocument *doc);
// Qt's count of single changes (availableUndoSteps) at which the step-th entry of the lists is undone (redone)
// completely; -1: no such step. One Undo may take back several changes (a command made in one edit block).
int undoTargetOf(QTextDocument *doc, int step);
int redoTargetOf(QTextDocument *doc, int step);
// the object that emits destroyed() / changed when the names of doc change (for the window's refresh)
QObject *bookOf(QTextDocument *doc);
// a command names the step it makes: call right after making the action, before its own slot is connected (the
// naming runs first)
void nameStepsOf(QAction *command);
// a command's text as a step name ("&Paste" -> "Paste", "Insert Table…" -> "Insert Table")
QString stepName(const QString &commandText);

} // namespace undonames
} // namespace qb
