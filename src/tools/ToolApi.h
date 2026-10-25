#pragma once
// Interface between the program and writing tools (version 1.0).
//
// A writing tool is a Qt plugin (a MODULE library, *.so) that implements
// qb::tools::ToolInterface. It is delivered as an add-on: the library goes
// into the add-on's plugins/ folder, its data into folders of its own
// (names/, ...), which the program hands out through ToolHost::contentDirs.
//
// A tool needs nothing from the program but this header and Qt (Core, Gui,
// Widgets). Everything it wants from the program goes through ToolHost, a
// class of pure virtual functions the program implements; nothing here has
// to be linked against the program. Plugins built for another interface
// version (QB_TOOL_IID) are not loaded.
//
// Every command of a tool becomes an action of the main window with the key
// "tool:<Command::key>": a menu entry at Command::place, a command in
// File > Options > Keyboard (shortcut can be changed there), a possible
// toolbar button. Commands marked `standalone` also run without a main
// window: `<program> --tool <key>` (the scratch pad uses this for a
// shortcut that works in every program).

#include <QJsonArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtPlugin>

class QObject;
class QWidget;

namespace qb {
namespace tools {

struct Command {
    QString key;        // unique: "<tool id>.<name>", e.g. "name-generator.show"
    QString text;       // menu text with "&" and "…"
    // Where the entry goes: "edit/writing-tools" (Edit > Writing Tools),
    // "window" (Window menu), "project" (Project menu, after Project Statistics).
    QString place;
    QString shortcut;   // standard shortcut as portable text ("Alt+Shift+Return"), may be empty
    QString icon;       // icon theme name, may be empty
    QString toolTip;
    bool needsProject = false;  // disabled while no project is open
    bool standalone = false;    // can run without a main window (--tool <key>)
};

// Which text a tool asks for (ToolHost::documents).
enum class Scope {
    Selection,        // selected text of the active editor
    Document,         // the text documents shown in the active editor (combined text: all of them)
    BinderSelection,  // the documents selected in the binder and the documents inside them
    Draft,            // text documents in the Draft folder marked "Include in Compile"
    Project           // every text document outside the Trash
};

struct TextDocument {
    QString id;         // binder id; empty for a selection
    QString title;
    QString path;       // enclosing folders: "Draft / Part One"
    QString text;       // plain text (inline annotations left out, as for the word count)
    bool inDraft = false;
};

// A running instance of the program (this one included) with its project.
struct ProjectInfo {
    QString key;        // what sendToProject wants
    QString title;      // project title
    QString file;       // project file
    bool thisWindow = false;
    // The binder without the Trash:
    // [ { "id", "title", "folder": bool, "text": bool, "children": [ ... ] } ]
    QJsonArray binder;
};

struct SendRequest {
    QString file;       // file to bring in: .txt .md .rtf .html .docx .odt, pictures, PDF ...
    QString title;      // title of the new document (mode "import")
    QString mode;       // "import": new document inside targetId (empty: the Research folder)
                        // "append": added to the end of the text document targetId
    QString targetId;
};

class ToolHost
{
public:
    virtual ~ToolHost() = default;
    virtual int apiVersion() const = 0;                 // 100 = 1.0
    virtual QString appName() const = 0;
    virtual QString appVersion() const = 0;
    // The main window, the parent for the tool's windows; null while the
    // program runs a standalone command (--tool) without a main window.
    virtual QWidget *window() const = 0;
    // Folders called `kind` ("names", ...) of all enabled add-ons.
    virtual QStringList contentDirs(const QString &kind) const = 0;
    // The tool's own folder ~/.local/share/<App>/tools/<toolId> (created).
    virtual QString dataDir(const QString &toolId) const = 0;
    // The program's settings file, group "tools/<toolId>".
    virtual QVariant setting(const QString &toolId, const QString &key, const QVariant &def = QVariant()) const = 0;
    virtual void setSetting(const QString &toolId, const QString &key, const QVariant &value) = 0;

    // ---- the project of this window
    virtual bool hasProject() const = 0;
    virtual QString projectTitle() const = 0;
    // Active text editor: text at the cursor (replaces the selection; one undo step).
    virtual bool canInsertText() const = 0;
    virtual bool insertText(const QString &text) = 0;
    virtual QString selectedText() const = 0;
    // Texts for statistics and the like; reads documents that are not open from their files.
    virtual QList<TextDocument> documents(Scope scope) const = 0;
    // Words counted by the program's own rule (the counts in footer bar and targets).
    virtual int countWords(const QString &text) const = 0;
    virtual void showDocument(const QString &id) = 0;    // in the active editor
    virtual void searchProject(const QString &text) = 0; // project search for a word or phrase

    // ---- every running instance
    virtual QList<ProjectInfo> openProjects() = 0;
    virtual bool sendToProject(const QString &key, const SendRequest &request, QString *error) = 0;
    // Command line that runs a standalone command (for a desktop-wide shortcut).
    virtual QString commandLine(const QString &commandKey) const = 0;

    // An object with the signals (connect with SIGNAL()):
    //   projectChanged()    a project was opened or closed in this window
    //   settingsChanged()   File > Options was applied
    virtual QObject *events() const = 0;
};

// A section a tool adds to File > Options (ToolInterface::optionsPage).
class OptionsSection
{
public:
    virtual ~OptionsSection() = default;
    virtual QWidget *widget() = 0;   // shown in a group box; owned by the options window
    virtual void apply() = 0;        // OK or Apply
};

class ToolInterface
{
public:
    virtual ~ToolInterface() = default;
    virtual QString id() const = 0;            // "name-generator"
    virtual QString name() const = 0;          // "Name Generator"
    virtual QString version() const = 0;
    virtual QList<Command> commands() const = 0;
    virtual void setHost(ToolHost *host) = 0;  // once, before any command runs
    virtual void run(const QString &commandKey) = 0;
    // Options: "" = none, "General" = a section on the General page.
    virtual QString optionsPage() const = 0;
    virtual OptionsSection *createOptions(QWidget *parent) = 0;  // null if none
    // The program ends (or the main window closes): save open work.
    virtual void shutdown() = 0;
    // Number of top-level windows of the tool that are open (a standalone
    // run ends when the last one closes).
    virtual int openWindows() const = 0;
};

} // namespace tools
} // namespace qb

#define QB_TOOL_IID "org.quillbinder.ToolInterface/1.0"
Q_DECLARE_INTERFACE(qb::tools::ToolInterface, QB_TOOL_IID)
