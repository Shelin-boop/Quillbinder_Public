#pragma once
// The mind map of one binder item while it is open (0.17): the board, its undo
// history and its scene, shared by every view on it (the inspector's Mind Map
// tab and the large window). Saved by itself a moment after each change.

#include "core/MindMap.h"
#include "core/Project.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QUndoStack;

namespace qb {

class Project;
class MindMapScene;

// Options of the mind maps (File > Options of the mind map window); QSettings group "mindmap".
struct MindMapPrefs {
    bool enterEndsEditing = false;   // Enter ends editing (Shift+Enter: new line)
    bool enterCreatesNotes = false;  // Enter on a selected note (not editing): new note
    bool useSelectedStyle = true;    // a new note takes the look of the selected one
    bool dashedLines = true;         // lines without arrows are dashed
    bool autoSize = true;            // a short note becomes as narrow as its text when editing ends
    int fadedPercent = 60;           // how much faded notes fade
    QString newNoteText;             // placeholder of new notes (empty: "New Note")
    double newNoteSize = 12;
    double newNoteWidth = 160;
    QColor newNoteFill;
    int newNoteBorder = 0;
    int newNoteShape = 0;            // mindmap::BorderStyle of new notes (0.21; 0 = rounded)
    int iconSize = 16;               // size of the icons on notes and lines, px at 100 % (0.21)
    QColor newShapeFill = QColor(238, 238, 238);
    int newShapeBorder = 1;
    bool newShapeMagnetic = true;
    // board defaults (Document Settings > Set As Defaults)
    QColor boardBackground = QColor(253, 251, 240);
    // the background of boards without a color of their own in dark themes (0.21.1;
    // boardBackground is the one of light themes)
    QColor boardBackgroundDark = QColor(46, 46, 43);
    QColor defaultBackground(bool dark) const { return dark ? boardBackgroundDark : boardBackground; }
    QColor boardText;
    QString boardFont;
    bool boardRightToLeft = false;
    QByteArray defaultStylesXml;     // own default styles (empty: the program's)
    static MindMapPrefs &instance();
    void load();
    void save() const;
    QString placeholder() const;
};

class MindMapDoc : public QObject
{
    Q_OBJECT
public:
    MindMapDoc(Project *project, const QString &itemId, QObject *parent = nullptr);
    ~MindMapDoc() override;

    mindmap::Board board;
    Project *project() const { return m_project.data(); }
    QString itemId() const { return m_itemId; }
    // a board of no project (the example board of the help): the window shows this title
    void setTitle(const QString &t) { m_title = t; }
    QString title() const { return m_title; }
    QString path() const;
    MindMapScene *scene() const { return m_scene; }
    QUndoStack *undoStack() const { return m_undo; }
    bool locked() const { return board.settings.locked; }

    // Every change: begin() before, commit() after (one undo step), or
    // change(text, fn) for both. Nested calls make one step.
    void begin(const QString &text);
    void commit();
    void cancel();                       // restores the state of begin()
    void change(const QString &text, const std::function<void()> &fn);
    // puts a whole state back (undo / redo)
    void restore(const QByteArray &xml);

    bool save(QString *error = nullptr);
    bool isDirty() const { return m_dirty; }

    // Background (0.21.1): the board's own color for the kind of theme in use
    // (light or dark), invalid: the default of that kind (and follows it).
    QColor ownBackground() const;
    void setOwnBackground(const QColor &c);       // one undo step; invalid: back to the default
    // works out the color shown (theme, own colors, defaults) and draws the board anew
    void applyBackground();
    static bool darkMode();

signals:
    void boardChanged();                 // after every change (views update)
    void saved();

private:
    void touched();

    QPointer<Project> m_project;
    QString m_itemId;
    QString m_title;
    MindMapScene *m_scene = nullptr;
    QUndoStack *m_undo = nullptr;
    QByteArray m_before;
    QString m_text;
    int m_depth = 0;
    bool m_dirty = false;
    QTimer m_saveTimer;
};

namespace mindmaps {
// The open board of an item (made, and read from its file, on first use).
MindMapDoc *docFor(Project *project, const QString &itemId);
// Is there a board for the item (an open one with notes, or a file)?
bool hasMap(Project *project, const QString &itemId);
// Saves every open board of the project (on project save and close).
void saveAll(Project *project);
// Closes the boards of a project (after saving them).
void closeAll(Project *project);
// Every open board draws itself anew (after File > Options > Extras > Mind Map).
void refreshAll();
} // namespace mindmaps

} // namespace qb
