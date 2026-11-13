#pragma once
// 0.90.1 (the usability review of 0.90.0, UR-10 and UR-03, the writer's wish): every option of File > Options tells
// where its value is kept: the control's tool tip says whether it is kept in the open project or by the program for all
// projects, and whether that place is fixed or depends on the choice "Settings for" (and, for the look, "Show and change
// the look of"). 0.90.2: the badges before the controls (a green "P", a blue "A") are gone at the writer's wish (they
// drew too much attention, and the holders they needed pushed color buttons to the far right); the tool tip stays.
//
// A control is marked with the settings key it writes ("editing/smartQuotes"; "zoomDefault/*" = every key under the
// group; "look" = part of a project's look, settings/look.json). refresh() sets every marked control's tool tip and its
// property "qbPlace" ("P" or "A", for the checks).
#include <QString>

class QWidget;

namespace qb {
namespace optstore {

// what the window shows now
struct State {
    bool projectOpen = false;
    QString projectTitle;
    bool thisProject = false;     // "Settings for: This project"
    bool projectFirst = true;     // General > Which settings come first: a project's own
    bool lookThisProject = false; // Appearance: "Show and change the look of: This project"
};

void mark(QWidget *control, const QString &key);
// the key a control was marked with ("" = none)
QString keyOf(const QWidget *control);
// a key the program always keeps itself (never a project's)
bool fixedToProgram(const QString &key);
// the open project keeps a value of its own for the key
bool projectKeeps(const QString &key);
// "P" (the project) or "A" (the program) for a key in this state
QString placeOf(const QString &key, const State &s);
// the sentence of the tool tip
QString explanation(const QString &key, const State &s);
// every marked control below root
void refresh(QWidget *root, const State &s);
// a control's own tool tip, changed later (a color's name): the sentence of where it is kept stays below it
void setToolTip(QWidget *control, const QString &base);

} // namespace optstore
} // namespace qb
