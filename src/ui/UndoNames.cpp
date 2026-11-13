#include "ui/UndoNames.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QVariant>

namespace qb {
namespace undonames {

namespace {

// Qt counts its undo history in single changes (availableUndoSteps): one Undo can take back several (a command made
// in one edit block). The book notes where each named step ends in that count; a step runs from the end of the one
// before it to its own end.
struct Entry {
    int end;
    QString name;
};

class Book : public QObject
{
public:
    explicit Book(QTextDocument *doc) : QObject(doc), m_doc(doc)
    {
        setObjectName(QStringLiteral("qbUndoNames"));
        m_state = doc->availableUndoSteps();
        m_floor = m_state; // what came before the book has no names
        connect(doc, &QTextDocument::undoCommandAdded, this, &Book::added);
        connect(doc, &QTextDocument::contentsChange, this, &Book::changed);
        connect(doc, &QTextDocument::redoAvailable, this, [this](bool on) {
            if (on)
                m_undoneSinceAdd = true;
        });
    }

    void setPending(const QString &name)
    {
        m_pending = name;
        queueClear();
    }

    // the steps Undo can take back, the next one first: (name, the count to undo down to)
    QList<Entry> undoPlan()
    {
        reconcile();
        const int s = m_doc->availableUndoSteps();
        QList<Entry> out;
        // changes after the last named step (made without a signal): one unnamed step
        const int lastEnd = m_entries.isEmpty() ? m_floor : m_entries.last().end;
        if (s > lastEnd)
            out << Entry{qMax(lastEnd, 0), QCoreApplication::translate("qb::undonames", "Change")};
        for (int i = int(m_entries.size()) - 1; i >= 0; --i) {
            const int start = i > 0 ? m_entries.at(i - 1).end : m_floor;
            if (start < s)
                out << Entry{qMax(start, 0), m_entries.at(i).name};
        }
        // what came before the book (the text as it was opened, or steps made elsewhere)
        if (qMin(s, m_floor) > 0)
            out << Entry{0, QCoreApplication::translate("qb::undonames", "Change")};
        return out;
    }

    // the steps Redo can bring back, the next one first: (name, the count to redo up to)
    QList<Entry> redoPlan()
    {
        reconcile();
        const int s = m_doc->availableUndoSteps();
        QList<Entry> out;
        for (const Entry &e : std::as_const(m_entries))
            if (e.end > s)
                out << e;
        return out;
    }

private:
    void queueClear()
    {
        if (m_clearQueued)
            return;
        m_clearQueued = true;
        // forgotten once the command is done (the event loop runs again)
        QTimer::singleShot(0, this, [this] {
            m_pending.clear();
            m_clearQueued = false;
            m_guessLast = false;
            m_inPass = false;
        });
    }

    void added()
    {
        const int s = m_doc->availableUndoSteps();
        // the redo part is gone: Qt dropped it when this change came
        const int before = m_undoneSinceAdd ? m_state : qMax(m_state, m_entries.isEmpty() ? m_floor : m_entries.last().end);
        while (!m_entries.isEmpty() && m_entries.last().end > before)
            m_entries.removeLast();
        if (m_floor > before)
            m_floor = before;
        const QString name = m_pending.isEmpty() ? QCoreApplication::translate("qb::undonames", "Change") : m_pending;
        // a step continued in the same command (a correction while typing joins the typing's step): one name
        if (m_inPass && !m_undoneSinceAdd && !m_entries.isEmpty() && m_entries.last().name == name && m_entries.last().end <= s) {
            m_entries.last().end = s;
        } else {
            m_entries << Entry{s, name};
            m_guessLast = m_pending.isEmpty();
        }
        m_state = s;
        m_undoneSinceAdd = false;
        m_inPass = true;
        queueClear();
    }

    void changed(int, int removed, int addedChars)
    {
        m_state = m_doc->availableUndoSteps();
        if (!m_guessLast || m_entries.isEmpty())
            return;
        m_guessLast = false;
        QString guess;
        if (addedChars == removed)
            guess = QCoreApplication::translate("qb::undonames", "Formatting");
        else if (removed == 0)
            guess = QCoreApplication::translate("qb::undonames", "Typing");
        else if (addedChars == 0)
            guess = QCoreApplication::translate("qb::undonames", "Delete");
        else
            guess = QCoreApplication::translate("qb::undonames", "Change");
        m_entries.last().name = guess;
    }

    // the history was emptied or shortened (a whole new text)
    void reconcile()
    {
        // Qt 6.12 reports one change fewer to redo than its history holds (stack size - position - 1)
        const int total = m_doc->availableUndoSteps() + (m_doc->isRedoAvailable() ? m_doc->availableRedoSteps() + 1 : 0);
        while (!m_entries.isEmpty() && m_entries.last().end > total)
            m_entries.removeLast();
        if (m_floor > total)
            m_floor = total;
    }

    QTextDocument *m_doc;
    QList<Entry> m_entries;
    int m_floor = 0;     // the count when the book began (below it: no names)
    int m_state = 0;     // the count as last seen
    QString m_pending;
    bool m_clearQueued = false;
    bool m_guessLast = false;
    bool m_undoneSinceAdd = false;
    bool m_inPass = false; // a step was added since the event loop last ran
};

Book *book(QTextDocument *doc)
{
    if (!doc)
        return nullptr;
    // kept on the document (its children can be many: every list and table is one)
    if (auto *b = qobject_cast<QObject *>(doc->property("qbUndoBook").value<QObject *>()))
        return static_cast<Book *>(b);
    auto *b = new Book(doc);
    doc->setProperty("qbUndoBook", QVariant::fromValue<QObject *>(b));
    return b;
}

QTextDocument *documentOf(QWidget *w)
{
    if (auto *te = qobject_cast<QTextEdit *>(w))
        return te->document();
    if (auto *pe = qobject_cast<QPlainTextEdit *>(w))
        return pe->document();
    return nullptr;
}

QStringList namesOf(const QList<Entry> &plan)
{
    QStringList out;
    for (const Entry &e : plan)
        out << e.name;
    return out;
}

} // namespace

void setPending(QTextDocument *doc, const QString &name)
{
    if (Book *b = book(doc))
        b->setPending(name);
}

void setPendingForFocus(const QString &name)
{
    if (QTextDocument *doc = documentOf(QApplication::focusWidget()))
        setPending(doc, name);
}

QString nextUndo(QTextDocument *doc) { return undoSteps(doc).value(0); }
QString nextRedo(QTextDocument *doc) { return redoSteps(doc).value(0); }

QStringList undoSteps(QTextDocument *doc)
{
    Book *b = book(doc);
    return b && doc->isUndoAvailable() ? namesOf(b->undoPlan()) : QStringList();
}

QStringList redoSteps(QTextDocument *doc)
{
    Book *b = book(doc);
    return b && doc->isRedoAvailable() ? namesOf(b->redoPlan()) : QStringList();
}

int undoTargetOf(QTextDocument *doc, int step)
{
    Book *b = book(doc);
    const QList<Entry> plan = b ? b->undoPlan() : QList<Entry>();
    return step >= 0 && step < plan.size() ? plan.at(step).end : -1;
}

int redoTargetOf(QTextDocument *doc, int step)
{
    Book *b = book(doc);
    const QList<Entry> plan = b ? b->redoPlan() : QList<Entry>();
    return step >= 0 && step < plan.size() ? plan.at(step).end : -1;
}

QObject *bookOf(QTextDocument *doc) { return book(doc); }

void nameStepsOf(QAction *command)
{
    if (!command)
        return;
    QObject::connect(command, &QAction::triggered, command, [command] { setPendingForFocus(stepName(command->text())); });
}

QString stepName(const QString &commandText)
{
    QString s = commandText;
    s.remove(QLatin1Char('&'));
    s = s.section(QLatin1Char('\t'), 0, 0);
    while (s.endsWith(QChar(0x2026)) || s.endsWith(QLatin1Char('.')))
        s.chop(1);
    return s.trimmed();
}

} // namespace undonames
} // namespace qb
