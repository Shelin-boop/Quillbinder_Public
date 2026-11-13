#pragma once
// 0.90.1 (the usability review of 0.90.0, UR-10 and UR-03, the writer's wish): every option of File > Options tells
// where its value is kept. A badge before the control: a green circle with "P" (kept in the open project) or a blue
// circle with "A" (kept by the program, for all projects); its tool tip says whether that place is fixed or depends on
// the choice "Settings for" (and, for the look, "Show and change the look of").
//
// A control is marked with the settings key it writes ("editing/smartQuotes"; "zoomDefault/*" = every key under the
// group; "look" = part of a project's look, settings/look.json). mark() puts the badge before the control (the control
// moves into a small holder in the same place of the layout); refresh() sets every badge and tool tip of a window.
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
// "P" or "A" for a key in this state
QString placeOf(const QString &key, const State &s);
// the sentence of the tool tip
QString explanation(const QString &key, const State &s);
// every marked control below root
void refresh(QWidget *root, const State &s);
// a control's own tool tip, changed later (a color's name): the sentence of where it is kept stays below it
void setToolTip(QWidget *control, const QString &base);

} // namespace optstore
} // namespace qb
