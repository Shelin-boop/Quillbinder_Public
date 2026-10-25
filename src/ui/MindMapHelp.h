#pragma once
// Help for the mind maps (0.18): a help page with pictures drawn by the
// program's own board (so they look like what the writer sees), and an
// example board to practise on, which belongs to no project.

#include <QImage>
#include <QString>

class QWidget;

namespace qb {

class MindMapDoc;
class MindMapWindow;

namespace mindmaphelp {

// Help > Mind Map Help (also the "?" of a board): one window, raised when open
QWidget *showHelp(QWidget *parent);
// a fresh practice board in a window of its own (nothing is saved)
MindMapWindow *openExampleBoard(QWidget *parent);
// the pictures of the help page: notes, drag, line, arrow, arrowback, both, label, shape
QImage picture(const QString &name);
// the board of the example (also for the tests)
void fillExampleBoard(MindMapDoc *doc);

} // namespace mindmaphelp
} // namespace qb
