#pragma once
// Where the writer was (0.21.1): the place in each document, the view of each
// mind map, the zoom of pictures and PDF files, the state of the inspector.
// Kept with the project in its window state (settings/ui.json, Project::uiState),
// which versions before 0.21.1 read and leave alone.
//
// Nothing here marks the project as changed: the places are written with the
// next save of the project (at the latest when it closes, see
// MainWindow::saveUiState), so looking around never causes a save by itself.

#include <QPointF>
#include <QString>
#include <QVariantMap>

namespace qb {

class Project;

namespace viewmem {

// ---- text: cursor and the first visible character, per document
struct TextPlace {
    int cursor = -1;      // position of the cursor (-1: none known)
    int top = -1;         // position of the first character at the top of the view
    QString topItem;      // combined text: the document at the top of the view
    QString cursorItem;   // combined text: the document with the cursor
    bool valid() const { return cursor >= 0 || top >= 0; }
};
// `key`: a document id, or groupKey() of documents shown together (combined text)
TextPlace textPlace(Project *p, const QString &key);
void setTextPlace(Project *p, const QString &key, const TextPlace &place);
QString groupKey(const QStringList &ids);

// ---- mind maps: zoom and the point in the middle of the view, per board and
// per kind of view ("inspector" = the inspector's tab, "window" = the large window)
struct BoardView {
    double zoom = 0;      // 0: not known (the default zoom of new boards applies)
    QPointF center;
    bool valid() const { return zoom > 0; }
};
BoardView boardView(Project *p, const QString &itemId, const QString &kind);
void setBoardView(Project *p, const QString &itemId, const QString &kind, const BoardView &v);

// ---- pictures and PDF files in an editor: zoom (0 = fit) and the PDF page
struct MediaPlace {
    double zoom = -1;     // -1: not known; 0: fit to the window
    int page = -1;        // PDF: page shown
    bool pdfFitWidth = false;
};
MediaPlace mediaPlace(Project *p, const QString &itemId);
void setMediaPlace(Project *p, const QString &itemId, const MediaPlace &m);

// ---- the inspector: tab, dividers, the bookmark and the object shown
QVariantMap inspectorState(Project *p);
void setInspectorState(Project *p, const QVariantMap &state);

// how many places of documents are kept at most (the oldest go first)
constexpr int kMaxTextPlaces = 1500;

} // namespace viewmem
} // namespace qb
