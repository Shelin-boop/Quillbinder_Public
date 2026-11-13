#pragma once
// 0.90.1 (the usability review of 0.90.0): what every window of the program gets, through one application-wide event
// filter (installed by MainWindow).
//
// - UR-02: a dialog taller or wider than the screen's usable area (the screen without the desktop's panels, less a
//   title bar) is made smaller when it is shown and moved into view; its contents scroll or shrink (File > Options has a
//   scrolling page area, Compile and the license list shrink their lists). Before, OK and Cancel could sit below the
//   bottom edge of a 1366 x 768 laptop.
// - UR-01: when a dialog closes and no window of the program has the keyboard (a desktop without a window manager, or
//   one that does not give the keyboard back), the window the dialog came from gets it again, in the control that had
//   it before (the text): a writer who closes a dialog with Esc and types on does not lose the words.
// - UR-13: the grey placeholder text of empty fields ("Quick search ...", the synopsis' "Synopsis: a short summary ...")
//   is kept at a contrast of 4.5:1 or more against the field, as explanation lines are (HintLabel). Qt draws it in the
//   text color at half strength, about 3:1.
#include <QRect>

class QDialog;
class QWidget;

namespace qb {
namespace windowcare {

// installs the filter once (later calls do nothing)
void install();

// the area a window of `w` may take on its screen: the screen without the desktop's panels, less room for a title bar
QRect usableArea(const QWidget *w);
// makes the window fit into `area` (frame included, never below its minimum size) and moves it inside; true when its
// frame fits now
bool fitInto(QWidget *window, const QRect &area);

// the placeholder color of a field: its text color moved toward the field's background as far as a contrast of 4.5:1
// allows (set for the active and inactive state; disabled fields keep Qt's)
void readablePlaceholder(QWidget *field);

// UR-16: a Close button at the bottom of a window that has none of its own (Qt's Print Preview); it closes the window
// as Esc does. Named "windowClose".
void addCloseButton(QDialog *dialog);

} // namespace windowcare
} // namespace qb
