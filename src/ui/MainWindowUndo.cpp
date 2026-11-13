// 0.90.1 (the usability review of 0.90.0, UR-12): Undo and Redo say what they undo or redo, act only where there is
// something to undo (the text field that has the keyboard), and the toolbar's Undo and Redo have a list of the steps
// beside them (choosing a step undoes it and every later one). The names come from ui/UndoNames.
#include "ui/MainWindow.h"
#include "ui/MenuClear.h"
#include "ui/UndoNames.h"

#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

namespace qb {

namespace {

constexpr int kListedSteps = 25;

// the field Undo and Redo work on: the one with the keyboard
QWidget *undoTarget()
{
    QWidget *fw = QApplication::focusWidget();
    if (qobject_cast<QTextEdit *>(fw) || qobject_cast<QPlainTextEdit *>(fw) || qobject_cast<QLineEdit *>(fw))
        return fw;
    return nullptr;
}

QTextDocument *documentOf(QWidget *w)
{
    if (auto *te = qobject_cast<QTextEdit *>(w))
        return te->isReadOnly() ? nullptr : te->document();
    if (auto *pe = qobject_cast<QPlainTextEdit *>(w))
        return pe->isReadOnly() ? nullptr : pe->document();
    return nullptr;
}

void undoOnce(QWidget *w, bool undo)
{
    if (auto *te = qobject_cast<QTextEdit *>(w))
        undo ? te->undo() : te->redo();
    else if (auto *pe = qobject_cast<QPlainTextEdit *>(w))
        undo ? pe->undo() : pe->redo();
    else if (auto *le = qobject_cast<QLineEdit *>(w))
        undo ? le->undo() : le->redo();
}

} // namespace

void MainWindow::setupUndoNames()
{
    m_undoMenu = new QMenu(this);
    m_undoMenu->setObjectName(QStringLiteral("undoList"));
    m_redoMenu = new QMenu(this);
    m_redoMenu->setObjectName(QStringLiteral("redoList"));
    connect(m_undoMenu, &QMenu::aboutToShow, this, [this] { fillUndoList(m_undoMenu, true); });
    connect(m_redoMenu, &QMenu::aboutToShow, this, [this] { fillUndoList(m_redoMenu, false); });
    m_undoNamesTimer = new QTimer(this);
    m_undoNamesTimer->setSingleShot(true);
    m_undoNamesTimer->setInterval(60);
    connect(m_undoNamesTimer, &QTimer::timeout, this, &MainWindow::updateUndoNames);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        // a menu or a toolbar button that takes the keyboard for a moment is not a new target
        if (now && !qobject_cast<QMenu *>(now) && !qobject_cast<QToolButton *>(now))
            watchUndoTarget();
    });
    placeToolButtons();
    watchUndoTarget();
}

void MainWindow::watchUndoTarget()
{
    for (const QMetaObject::Connection &c : std::as_const(m_undoWatch))
        disconnect(c);
    m_undoWatch.clear();
    QWidget *target = undoTarget();
    auto later = [this] {
        if (m_undoNamesTimer)
            m_undoNamesTimer->start();
    };
    if (QTextDocument *doc = documentOf(target)) {
        undonames::bookOf(doc); // the names start with the first step made here
        m_undoWatch << connect(doc, &QTextDocument::undoAvailable, this, later)
                    << connect(doc, &QTextDocument::redoAvailable, this, later)
                    << connect(doc, &QTextDocument::undoCommandAdded, this, later)
                    << connect(doc, &QTextDocument::contentsChanged, this, later);
    } else if (auto *le = qobject_cast<QLineEdit *>(target)) {
        m_undoWatch << connect(le, &QLineEdit::textChanged, this, later);
    }
    if (target)
        m_undoWatch << connect(target, &QObject::destroyed, this, later);
    updateUndoNames();
}

void MainWindow::updateUndoNames()
{
    QAction *undo = a.value(QStringLiteral("undo")), *redo = a.value(QStringLiteral("redo"));
    if (!undo || !redo)
        return;
    QWidget *target = undoTarget();
    QString u, r;
    bool canUndo = false, canRedo = false;
    if (QTextDocument *doc = documentOf(target)) {
        canUndo = doc->isUndoAvailable();
        canRedo = doc->isRedoAvailable();
        u = canUndo ? undonames::nextUndo(doc) : QString();
        r = canRedo ? undonames::nextRedo(doc) : QString();
    } else if (auto *le = qobject_cast<QLineEdit *>(target); le && !le->isReadOnly()) {
        canUndo = le->isUndoAvailable();
        canRedo = le->isRedoAvailable();
        u = canUndo ? tr("Typing") : QString();
        r = canRedo ? tr("Typing") : QString();
    }
    const QString undoText = u.isEmpty() ? tr("&Undo") : tr("&Undo %1").arg(u);
    const QString redoText = r.isEmpty() ? tr("&Redo") : tr("&Redo %1").arg(r);
    // the toolbar's buttons keep their short label (a label that changed with every step would move the toolbar)
    if (undo->text() != undoText) {
        undo->setText(undoText);
        undo->setIconText(tr("Undo"));
    }
    if (redo->text() != redoText) {
        redo->setText(redoText);
        redo->setIconText(tr("Redo"));
    }
    undo->setEnabled(canUndo);
    redo->setEnabled(canRedo);
    undo->setToolTip(canUndo ? tr("Undo %1").arg(u) : tr("Nothing to undo in the field that has the keyboard"));
    redo->setToolTip(canRedo ? tr("Redo %1").arg(r) : tr("Nothing to redo in the field that has the keyboard"));
}

void MainWindow::fillUndoList(QMenu *menu, bool undo)
{
    clearMenu(menu);
    QPointer<QWidget> target = undoTarget();
    QStringList steps;
    if (QTextDocument *doc = documentOf(target))
        steps = undo ? undonames::undoSteps(doc) : undonames::redoSteps(doc);
    else if (auto *le = qobject_cast<QLineEdit *>(target); le && !le->isReadOnly()
                                                             && (undo ? le->isUndoAvailable() : le->isRedoAvailable()))
        steps << tr("Typing");
    if (steps.isEmpty()) {
        menu->addAction(undo ? tr("Nothing to undo") : tr("Nothing to redo"))->setEnabled(false);
        return;
    }
    const int shown = qMin<int>(int(steps.size()), kListedSteps);
    for (int i = 0; i < shown; ++i) {
        // "1  Typing", "2  Paste" ...: the number says how many steps go
        QAction *ac = menu->addAction(QStringLiteral("%1  %2").arg(i + 1).arg(steps.at(i)));
        ac->setObjectName(QStringLiteral("%1Step%2").arg(undo ? QStringLiteral("undo") : QStringLiteral("redo")).arg(i + 1));
        connect(ac, &QAction::triggered, this, [this, target, undo, i] {
            if (!target)
                return;
            QTextDocument *doc = documentOf(target);
            if (!doc) {
                undoOnce(target, undo); // a line field: its one step
                updateUndoNames();
                return;
            }
            // until Qt's count of changes reaches the end of the chosen step (one Undo may take back several)
            const int goal = undo ? undonames::undoTargetOf(doc, i) : undonames::redoTargetOf(doc, i);
            for (int guard = 0; goal >= 0 && guard < 100000; ++guard) {
                const int now = doc->availableUndoSteps();
                if (undo ? now <= goal : now >= goal)
                    break;
                undoOnce(target, undo);
                if (doc->availableUndoSteps() == now)
                    break; // nothing moved
            }
            updateUndoNames();
        });
    }
    if (steps.size() > shown)
        menu->addAction(tr("and %1 earlier steps").arg(QLocale().toString(int(steps.size()) - shown)))->setEnabled(false);
}

} // namespace qb
