#pragma once
// The commands of a mind map (0.17): Scapple's menus (File, Edit, View,
// Notes, Format), made once per view (the inspector's Mind Map tab and the
// large window each have one). Every command changes the board through
// MindMapDoc::change, so that it is one undo step and saved.

#include "core/MindMap.h"

#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QPointer>
#include <QPrinter>

#include <functional>

class QAction;
class QMenu;
class QMimeData;
class QPainter;
class QWidget;

namespace qb {

class MindMapDoc;
class MindMapScene;
class MindMapView;
class MindMapFindBar;

class MindMapController : public QObject
{
    Q_OBJECT
public:
    // window: the large window (its actions get real shortcuts); otherwise the
    // keys reach the actions through the view (the map in the inspector)
    MindMapController(MindMapDoc *doc, MindMapView *view, QWidget *owner, bool window);
    MindMapDoc *doc() const { return m_doc; }
    MindMapView *view() const { return m_view; }
    MindMapScene *scene() const;
    mindmap::Board &board() const;
    QAction *action(const QString &key) const { return m_a.value(key); }
    QList<QAction *> actions() const { return m_a.values(); }
    void setFindBar(MindMapFindBar *bar) { m_find = bar; }

    // menus (filled from the actions)
    void fillFileMenu(QMenu *m);
    void fillEditMenu(QMenu *m);
    void fillViewMenu(QMenu *m);
    void fillNotesMenu(QMenu *m);
    void fillFormatMenu(QMenu *m);
    void fillBackgroundMenu(QMenu *m);
    void fillContextMenu(QMenu *m);
    // Right-click on a line (0.21): its arrows, color, middle icon, label.
    void fillLineMenu(QMenu *m, int a, int b);
    // The submenus of the right-click menu of notes and shapes (0.21), also
    // used by the Format menu: colors from the shared palette, the shape, the icons.
    void fillColorMenu(QMenu *m, int which);        // 0 text, 1 fill, 2 border
    void fillShapeMenu(QMenu *m);
    void fillIconsMenu(QMenu *m, int placeUnderMouse = -1);
    // the edits behind them (one undo step each)
    void setColor(int which, const QColor &c);      // invalid: back to automatic / no fill
    void setIconAt(int place, const QString &iconId); // on the selected notes and shapes; empty: remove
    void clearIcons();
    void setLineArrows(int a, int b, bool toB, bool toA);
    void setLineColor(int a, int b, const QColor &c);
    void setLineIcon(int a, int b, const QString &iconId);
    // a short name of a note for menus ("Ashvale", "Picture", "Shape")
    QString itemName(int id) const;
    void fillStyleMenus(QMenu *apply, QMenu *redefine, QMenu *remove);
    void fillHelpMenu(QMenu *m);                     // Mind Map Help, Open Example Board (0.18)

    // commands the view calls
    void newNote();                                  // Notes > New (Stacked) Note
    int newNoteAt(const QPointF &scenePos, Qt::KeyboardModifiers mods = Qt::NoModifier, const QString &text = QString());
    void newConnectedNote(int dx, int dy, bool arrow);
    void noteBetween(int a, int b, const QPointF &pos);
    void editLineLabel(int a, int b);
    void deleteSelection();
    void indent(int delta);
    void nudge(double dx, double dy);
    void duplicate(const QList<int> &ids, const QPointF &offset);
    bool canAccept(const QMimeData *md) const;
    void dropData(const QMimeData *md, const QPointF &scenePos, Qt::KeyboardModifiers mods);
    void paste(const QPointF &scenePos, bool atPoint);
    void drawPageGuides(QPainter *p, const QRectF &rect);

    // styles
    void applyStyle(const mindmap::NoteStyle &style, const QList<int> &ids);
    void applyStyleByIndex(int index);               // Ctrl+Alt+1 ... 9 (0: Default)
    void newStyleFromSelection();
    void redefineStyle(const QString &styleId);
    void deleteStyle(const QString &styleId);
    QList<mindmap::NoteStyle> sortedStyles() const;
    // the look of new notes: the selected note's (option) or the default
    mindmap::Note templateNote() const;

    // exports (also used by the tests)
    bool exportPdf(const QString &path, bool background, bool fadedFull, QString *error = nullptr);
    bool exportPng(const QString &path, QString *error = nullptr);
    bool exportText(const QString &path, int kind, bool selectedOnly, const QString &separator, bool notesField,
                    QString *error = nullptr); // kind: 0 text, 1 list, 2 rtf, 3 opml
    int exportImages(const QString &dir, bool selectedOnly);
    void printBoard(QPrinter *printer);

    // find (the find bar of the window or the panel)
    bool find(const QString &text, bool backward, int mode, bool ignoreCase, bool wrap = true);
    int replaceAll(const QString &text, const QString &with, int mode, bool ignoreCase, bool selectedOnly);
    bool replaceCurrent(const QString &with);

    void updateActions();
    // changes the selected notes in one undo step (notes as narrow as their text stay so)
    void forNotes(const QString &undoText, const std::function<void(mindmap::Note &)> &fn, bool editingRange = false);
    // a link in a note was clicked: web and file addresses open, qb:// documents in the editor
    void openLink(const QString &href);
    // how documents of the project are opened (the main window sets it: as its document links)
    static void setDocumentOpener(std::function<void(const QString &)> fn);

signals:
    void inspectorToggled(bool on);
    void footerToggled(bool on);
    void findRequested();
    void openWindowRequested();

private:
    QAction *add(const QString &key, const QString &text, const QKeySequence &ks = QKeySequence(), const QString &icon = QString());
    void createActions();
    QList<int> targets() const;                     // the selected notes and shapes
    QColor currentColor(int which) const;           // of the first selected item (0 text, 1 fill, 2 border)
    void forItemsLook(const QString &undoText, const std::function<void(mindmap::Appearance &)> &fn);
    void toggleFont(int which);                     // 0 bold, 1 italic, 2 underline, 3 strike
    void setAlignment(mindmap::Align a);
    void chooseColor(int which);                    // 0 text, 1 fill, 2 border
    void clearColor(int which);
    void setBorderWeight(int w);
    void setBorderStyle(mindmap::BorderStyle s);
    void align(int how);                            // 0 left, 1 right, 2 top, 3 bottom, 4 h centers, 5 v centers
    void centerInShape(int how);                    // 0 both, 1 horizontally, 2 vertically
    void distribute(bool horizontal);
    void makeSame(bool width);
    void stack();
    void autoFit();
    void connectSelected(int how);                  // 0 hub, 1 all, 2 arrows from first, 3 remove arrows, 4 disconnect
    void newShape();
    void toggleMagnetic();
    void toggleFade();
    void toggleShadow();
    void splitNote();
    void mergeNotes();
    void selectStackedBelow();
    void selectOverlapping();
    void selectConnected(bool clusters);
    void invertSelection();
    void addLink();
    void copy(bool cut);
    void copyStyle();
    void pasteStyle();
    void importStyles();
    void importBoard();
    void exportDialog(int kind);
    void pageSetup();
    void printPreview();
    void print();
    void zoomToRandom();
    void zoomToSelection();
    void addToBinder();
    void showOptions();
    void jumpToSelection();
    void useSelectionForFind();
    void setWritingDirection(bool rtl);
    QPointF freeSpotNear(const QPointF &p) const;

    MindMapDoc *m_doc;
    MindMapView *m_view;
    QPointer<QWidget> m_owner;
    bool m_window;
    QHash<QString, QAction *> m_a;
    QPointer<MindMapFindBar> m_find;
    QByteArray m_styleClipboard;
    QPrinter m_printer;
    bool m_printerSet = false;
    QString m_lastFind;
};

} // namespace qb
