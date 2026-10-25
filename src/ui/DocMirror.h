#pragma once
// Two text documents kept in step (0.21.1): a document of the project and a
// copy of it that is shown somewhere else, for example in the bookmark preview
// of the inspector.
//
// Why a copy at all: all views of a QTextDocument share one layout, which
// wraps at the width of the narrowest view and has one zoom. The preview in the
// narrow inspector would squeeze the text of the editor, and could not have a
// zoom of its own. A copy has its own layout and zoom.
//
// What the writer sees: typing or formatting in the copy changes the document
// (and so its file and every editor that shows it) at once, and the other way
// round. Each change is copied as the piece of text that changed, with its
// character and paragraph formatting.
//
// How it works: QTextDocument::contentsChange tells where a document changed
// (position, characters removed, characters added). The same span of the other
// document is replaced by the changed piece of this one. Changes that leave the
// text and its formats as they were (the spell-check highlighter announces its
// colors this way) are ignored, so the two never send each other the same change
// again. If the two ever differ in length afterwards, the copy is made anew from
// the document (the document is never rewritten as a whole).

#include <QObject>
#include <QPointer>

class QTextDocument;

namespace qb {

class DocMirror : public QObject
{
    Q_OBJECT
public:
    // `source` is the project's document, `copy` the one shown elsewhere; both
    // must have the same contents when the mirror starts (copy = source->clone()).
    DocMirror(QTextDocument *source, QTextDocument *copy, QObject *parent = nullptr);
    ~DocMirror() override;
    QTextDocument *source() const { return m_source; }
    QTextDocument *copy() const { return m_copy; }
    // number of changes copied each way (for the checks)
    int toSource() const { return m_toSource; }
    int toCopy() const { return m_toCopy; }
    // true while a change is being copied (the receiving document changes then)
    bool busy() const { return m_busy; }

signals:
    // The copy was made anew from the document (they had drifted apart);
    // whoever shows the copy should take the new one.
    void copyReplaced(QTextDocument *newCopy);

private:
    void changed(QTextDocument *from, int pos, int removed, int added);
    void rebuildCopy();
    QPointer<QTextDocument> m_source;
    QPointer<QTextDocument> m_copy;
    bool m_busy = false;
    int m_toSource = 0, m_toCopy = 0;
};

// Do the two documents have the same text and formats between `from` and `to`?
// (used by the mirror; also by the checks)
bool sameContents(QTextDocument *a, QTextDocument *b, int from = 0, int to = -1);

} // namespace qb
