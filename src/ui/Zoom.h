#pragma once
// Zooming with Ctrl + mouse wheel everywhere (0.21).
//
// What the writer sees: holding Ctrl and turning the mouse wheel makes the
// text of the area under the mouse larger or smaller, also when another area
// has the keyboard: the editors, the inspector, the binder, the outliner, the
// corkboard, a mind map, composition mode, tool windows. Each area keeps its
// zoom (for all projects) until it is changed again; File > Options > Editing
// sets the zoom each area starts with ("Default text zoom", as in Scrivener).
//
// How it works: one event filter on the whole application (install()) looks
// at every wheel event with Ctrl held. It walks from the widget under the
// mouse up to its window and stops at the first widget that was "marked":
//   - mark(widget, key): an area with a zoom factor stored under "key"; the
//     filter changes the factor and everybody listening to notifier() applies
//     it (applyFonts() scales the fonts of a widget and its children);
//   - setHandler(widget, fn): the widget decides itself what a wheel step
//     means (the editors change the project's text zoom, composition mode
//     its own zoom list, the corkboard the size of its cards);
//   - markOwn(widget): the widget already zooms on Ctrl + wheel by itself
//     (the mind map board); the filter leaves the event alone.
// A window without any mark (a tool window of an add-on, a dialog) gets the
// zoom of its fonts as a whole, remembered under "window/<its name>". The main
// window is never zoomed as a whole (its toolbars stay as they are).
//
// Why fonts and not a picture scale: Qt widgets have no general "zoom";
// larger fonts make rows, fields and lists grow with them, so everything stays
// sharp and usable (a scaled picture of a widget could not be edited).

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class QWidget;

namespace qb {
namespace zoom {

// The areas with a zoom of their own (the keys of the settings).
extern const char *const kEditor;      // main editors, copyholders, Quick Reference (one text layout per document)
extern const char *const kInspector;   // the inspector (or kInspector + "/" + tab, see perTab())
extern const char *const kBinder;
extern const char *const kOutliner;
extern const char *const kCorkboard;
extern const char *const kComposition;
extern const char *const kMindMap;
extern const char *const kObjects;     // the Objects window: the upper part and the list
// Parts with a zoom of their own (0.21.1): where an area shows two things, each has
// its own zoom (the writer asked for it: "zoom for the selection, zoom for the sheet").
extern const char *const kObjectsSheet;     // the Objects window: the sheet
extern const char *const kInspSynopsis;     // inspector, Notes tab: title and synopsis card
extern const char *const kInspNotes;        // inspector, Notes tab: the notes
extern const char *const kInspBookmarks;    // inspector, Bookmarks tab: the two lists
extern const char *const kInspPreview;      // inspector, Bookmarks tab: the text of the bookmarked document
extern const char *const kInspSnapList;     // inspector, Snapshots tab: the list
extern const char *const kInspSnapText;     // inspector, Snapshots tab: the text of a snapshot
extern const char *const kInspObjectList;   // inspector, Objects tab: category buttons, filter, list
extern const char *const kInspObjectSheet;  // inspector, Objects tab: the sheet

constexpr double kMin = 0.5;
constexpr double kMax = 3.0;
constexpr double kStep = 0.1;          // one notch of the wheel: 10 %

// The areas the Options window lists (key and the name shown).
QStringList areas();
QString areaName(const QString &key);

// The zoom of an area (1.0 = 100 %): the one last used, else its default.
double factor(const QString &key);
void setFactor(const QString &key, double f);     // stored at once, notifier() tells the area
double defaultFactor(const QString &key);         // File > Options > Editing > Default text zoom
void setDefaultFactor(const QString &key, double f);
void reset(const QString &key);                   // back to the default

// The inspector: one zoom for all its tabs (the standard), or one per tab
// (View > Zoom Inspector Tabs Separately). Since 0.21.1 the tabs with two parts
// (Notes, Bookmarks, Snapshots, Objects) have a zoom per part in either mode; the
// mode concerns the other tabs (Metadata, Comments, the bar of the mind map).
bool perTab();
void setPerTab(bool on);
QString inspectorKey(const QString &tab);         // kInspector or "inspector/<tab>"

// Since 0.21.1: the zooms of the parts start from the zoom they had as part of
// the whole inspector (or the Objects window) before; called once by install().
void migrateParts();

// Notes the change of a factor (also of the inspector mode).
class Notifier : public QObject
{
    Q_OBJECT
signals:
    void changed(const QString &key, double factor);
    void modeChanged();
};
Notifier &notifier();

// See the top of this file.
void install();
void mark(QWidget *w, const QString &key);
void setHandler(QWidget *w, const std::function<void(int steps)> &handler);
void markOwn(QWidget *w);
// The widget and its children are never zoomed by an area around them
// (for example text editors whose zoom comes from their document).
void skip(QWidget *w);

// Scales the fonts of a widget and of its children that have a font of
// their own (the theme's fonts) to the factor. Called again with another
// factor, it starts from the fonts before scaling, so nothing grows by
// itself; a font set meanwhile by somebody else (a theme change) becomes the
// new starting point.
void applyFonts(QWidget *root, double f);
// applyFonts now and whenever the area's factor or the theme changes.
void attachFonts(QWidget *root, const QString &key);
// The opposite: the fonts go back to 100 % and no longer follow the area.
void detachFonts(QWidget *root);
// Marks of a widget taken away again (an area that is split up, see the inspector).
void unmark(QWidget *w);

} // namespace zoom
} // namespace qb
