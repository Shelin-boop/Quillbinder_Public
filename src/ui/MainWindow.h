#pragma once
#include "core/Project.h"
#include "ui/EditorPane.h"
#include "text/DirectFormat.h"
#include "tools/ToolApi.h"

#include <QJsonObject>
#include <QDateTime>
#include <QMainWindow>
#include <QSet>
#include <QPointer>
#include <QVariantMap>
#include <memory>

class QAction;
class QActionGroup;
class QComboBox;
class QFontComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QMenu;
class QSplitter;
class QToolBar;
class QToolButton;
class QTextDocument;
class QTextEdit;
class QCompleter;
class QStandardItemModel;

namespace qb {
namespace script {
struct Format;
}


class BinderPanel;
class StylesPanel;
class FormatFindWindow;
class Inspector;
class TextEditor;
class CompositionWindow;
class QuickRefWindow;
class TargetLineEdit;
class FontScopeBar;
class ObjectTextEdit;
struct PrintSettings;
class ColorPickButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

    bool openProject(const QString &path);
    // a .scriv folder or its .scrivx (MainWindowFormat.cpp)
    bool openScrivenerProject(const QString &path);
    static QString projectFileFilter();
    // isNew: a project just made (new, imported, the welcome project): it gets the look for
    // new projects; an existing project keeps the look it has (0.40.2, ui/ProjectLook.h)
    bool adoptProject(std::unique_ptr<Project> p, bool isNew = false);
    // The project gets these styles; all text follows (MainWindowStyles.cpp; also used by the
    // settings import and the tests). renamed: old name -> new name.
    bool commitStyles(const StyleSheet &sheet, const QHash<QString, QString> &renamed = {});
    // Project > Save / Load Project Settings (0.40.2, MainWindowSettings.cpp). An empty file name
    // asks for one. importProjectSettings shows what differs and asks per section.
    bool exportProjectSettings(const QString &file = QString());
    bool importProjectSettings(const QString &file = QString());
    QJsonObject currentProjectSettings();
    // takes the sections over (key -> value as in a settings file); notes: what was kept
    void applyProjectSettings(const QJsonObject &sections, QStringList *notes);
    QStringList lastSettingsNotes() const { return m_lastSettingsNotes; }
    // File > Compare and Transfer with this file (0.18; also for the tests): false if not compared
    bool compareWithFile(const QString &path);
    bool closeProject();
    Project *project() const { return m_project.get(); }
    void showStartDialog(bool recentFirst);
    // First start from an AppImage: offer to add the program to the application menu.
    void offerDesktopIntegration();
    // Install a .qbpkg file (program update, add-on or bundle).
    void installPackage(const QString &file);
    // Message from the start-up loader (an update was switched off).
    void showStartNotice();
    // Saves the project, mind maps and tool notes without questions (termination signal; 0.19).
    void saveForTermination();
    bool isClosing() const { return m_closing; }
    // Help > Welcome Project, About, List of All Placeholders (MainWindowHelp.cpp; 0.40)
    void openWelcomeProject(bool firstStart = false);
    // At the very first start (and the first start of 0.40): makes and opens the welcome
    // project; false when it was shown before (or QB_NO_WELCOME is set).
    bool openWelcomeOnFirstStart();
    void showAbout();
    void showPlaceholders();
    static QString welcomeName();        // "Welcome to Quillbinder"
    static QString welcomeFolder();      // where the welcome project is (or would be) made

    // Window layouts (Window > Layouts); the state is described in Layouts.h.
    QVariantMap captureLayout(bool withWindowSize = false) const;
    void applyLayout(const QVariantMap &state);
    bool applyLayoutById(const QString &id);
    // Which editor a click in the binder loads: current | first | second | other
    QString binderAffects() const { return m_binderAffects; }
    void setBinderAffects(const QString &mode);

    // ---- writing tools (MainWindowTools.cpp): what toolhost::Host asks of the window
    QString toolProjectTitle() const;
    bool toolCanInsert() const;
    bool toolInsertText(const QString &text);
    QString toolSelectedText() const;
    QList<tools::TextDocument> toolDocuments(tools::Scope scope) const;
    void toolShowDocument(const QString &id);
    void toolSearch(const QString &text);
    QJsonObject toolProjectInfo() const;     // title, file, binder (instance link "info")
    // A note of the scratch pad (or another tool): new document or added to a text.
    bool receiveFromTool(const tools::SendRequest &request, QString *error);

    // ---- import (MainWindowImport.cpp)
    // File > Import > Web Page (address may be empty: the add-on's window asks);
    // interactive false: no questions (web links dropped on the binder). Without
    // a parent: the selected folder, beneath the selected document, else Research.
    BinderItem *importWebPage(const QString &address = QString(), bool interactive = true, BinderItem *parent = nullptr,
                              int row = -1, bool select = true);
    // Files and web links dropped on binder, corkboard or outliner; returns the files' documents
    // (web pages are fetched right after the drop has finished).
    QList<BinderItem *> importDropped(BinderItem *parent, int row, const QList<QUrl> &urls, bool select = true);

    // ---- options of File > Options that the window carries out (0.21)
    // File > Save: the project, then the snapshots and the backup the options ask for
    bool manualSave();
    // Automatic Quit: the time of the last key or mouse input (the tests set an earlier one)
    void noteInput(const QDateTime &when);
    bool autoQuitDue() const;
    // the documents edited since the last File > Save (for the snapshots of a manual save)
    QStringList editedSinceManualSave() const;

    // Help > About: version, credits, license (0.21.1: the credits)
    static QString aboutText();

    // ---- 0.21.1: the text field the format bar and the Format menu act on
    TextEditor *formatTarget() const { return editor(); }
    ObjectTextEdit *formatObjectTarget() const { return objectTextTarget(); }

protected:
    void closeEvent(QCloseEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    // File > Project Format (MainWindowFormat.cpp)
    void switchToScrivenerFormat();
    void switchToQuillbinderFormat();
    void saveCopyAsScrivener();
    void saveCopyAsQuillbinder();
    bool confirmNotOpenElsewhere(const QString &projectFolder);

    // construction
    void createActions();
    void createMenus();
    // additions of 0.17 (MainWindowExtras.cpp)
    void createExtraActions();
    void createFontMenu(QMenu *nav);
    void installFonts();
    void fillIconMenu(QMenu *menu);
    void rememberIcon(const QString &icon);
    QStringList iconBarIcons() const;
    void createIconBar();
    void rebuildIconBar();
    void chooseIconBar();
    void createColorButtons();
    void showDeveloperGuide();
    // additions of 0.18 (MainWindowExtras.cpp)
    void chooseCompareFile();
    // writing tools (MainWindowTools.cpp)
    void createToolActions();                     // "tool:<key>" for every command of the loaded tools
    QList<QAction *> toolActionsAt(const QString &place) const;
    void fillWritingToolsMenu(QMenu *menu);
    void updateToolActions();
    void createToolBars();
    void createFormatBar();
    // menus, toolbars, layouts, shortcut lists (MainWindowBars.cpp)
    void rebuildToolBars();
    QAction *barItem(const QString &id) const;
    // Entry with a short text for a submenu ("Files…" under Import); acts as the command itself.
    QAction *menuAlias(QMenu *menu, const QString &key, const QString &text);
    void customizeToolbars(int bar);
    void rebuildStyleActions();
    void rebuildLayoutActions();
    void fillLayoutMenu(QMenu *menu);
    void saveLayoutAs();
    void manageLayouts();
    void applyQuickRefFloating();
    EditorPane *binderTarget() const;
    void updateActions();
    void updateFormatBar();
    void updateViewButtons();
    void updateWindowTitle();
    void showFontDialog(TextEditor *e);
    void showParagraphDialog(TextEditor *e);
    void convertToDefaultFormatting();
    // Font list and size field of the format bar: the selection (or the whole
    // paragraph) gets the font at once, then the bar above the editors offers
    // the same for the paragraph's style or for all text of the project.
    void applyFontFromBar(const direct::CharSpec &spec, int members);
    void acceptFontOffer();

    // styles (MainWindowStyles.cpp)
    // target: the editor the entries act on (null = the active editor);
    // full: with the commands to create, redefine, edit and delete styles
    void fillStyleMenu(QMenu *menu, TextEditor *target, bool full);
    // the chosen style list with the style commands fixed below it: the format bar's list and
    // the right-click menus (0.40.3)
    void fillStyleList(QMenu *menu, TextEditor *target);
    void customizeStyleList();
    void rebuildStyleCombo();
    void refreshStyleUi();
    void newStyleFromSelection();
    void redefineStyleFromSelection(const QString &name = QString());
    void editStyle(const QString &name);
    void deleteStyle(const QString &name);
    void resetStyle(const QString &name);
    void moveStyle(const QString &name, int delta);
    void importStyles();
    void saveStylesAsDefault();
    void showStylesPanel();
    // "No Style" redefined from the selection: the project's main text style (0.40.3)
    void redefineNoStyleFromSelection();
    // the main text style as the paragraph at the cursor shows it ("Use Current" buttons)
    bool formattingAtCursor(TextEditor *e, Formatting *f) const;
    // the window that style dialogs and messages belong to: the Styles panel when the
    // command came from it (so they open above it, 0.40.3), else the main window
    QWidget *styleUiParent() { return m_styleUiParent ? m_styleUiParent.data() : static_cast<QWidget *>(this); }
    // audio and video (MainWindowMedia.cpp): Navigate > Media
    void createMediaActions();
    void createMediaMenu(QMenu *nav);
    void updateMediaActions();
    void insertMediaTimestamp();
    void goToMediaTimestamp();
    // scriptwriting (MainWindowScript.cpp)
    void createScriptActions();
    void createScriptMenu(QMenu *formatMenu);
    void fillScriptFormatMenu();
    void rebuildScriptActions();   // "script:<element>", one per element of the script format
    void updateScriptActions();
    void setScriptMode(bool on);   // the document with the cursor (or the selected documents)
    void chooseScriptFormat(const script::Format &format);
    bool commitScriptFormat(const script::Format &format, const QHash<QString, QString> &renamedElements = {});
    void showScriptSettings();
    // search tools (MainWindowSearch.cpp)
    void projectReplace();
    void showFormatFind();
    // Select the next (previous) text with the formatting asked for in the
    // Find by Formatting window; goes on into the following documents.
    bool findFormatting(bool backward);
    void applySearchHighlights();     // the matches of the project search in both editors
    void fillQuickSearch();
    void updateTargetsDisplay();
    void applySettings();
    void rebuildRecentMenu();

    // panes
    EditorPane *activePane() const { return m_active; }
    EditorPane *otherPane(EditorPane *p) const;
    void setActivePane(EditorPane *p);
    void setSplit(Qt::Orientation o, bool on);
    bool isSplit() const;
    EditorPane *ensureOtherPane();
    void loadInPane(EditorPane *pane, const QList<BinderItem *> &items);
    void connectPane(EditorPane *p);

    // item helpers
    QList<BinderItem *> actionItems() const; // selection in the focused place
    BinderItem *insertionPoint(int *row) const;
    BinderItem *newItem(ItemType type, const QString &templateId = QString());
    void moveSelection(int dx, int dy);
    void duplicateItems();
    void splitDocument(bool selectionAsTitle);
    void mergeDocuments();
    void setIconFor(const QString &icon);
    void openLink(EditorPane *from, const QString &href);
    // the object in the Objects tab of the inspector (0.20)
    void showObject(const QString &objectId);
    QuickRefWindow *openQuickReference(BinderItem *item);
    QList<QuickRefWindow *> quickRefWindows() const;
    // Format and Insert commands (and Save) also answer to their shortcuts in this window.
    void shareShortcuts(QWidget *window);
    void addToProjectBookmarks(const QList<BinderItem *> &items);
    void installToolbarDropTargets();
    void fillGoToMenu(QMenu *menu);
    void fillItemMenu(QMenu *menu);
    // lookPart: lookmenu::Part of what was clicked (binder, corkboard, outliner), -1: none
    void showItemContextMenu(const QPoint &globalPos, int lookPart = -1);
    void showHeaderMenu(EditorPane *pane, const QPoint &globalPos);
    void enterComposition();
    void showPageSetup();
    void importFiles();
    void explainWebImportAddon();
    void backupNow(bool chooseLocation);
    void convertFileTool();
    void printDocument();
    // File > Print Preview (with the given settings instead of the project's: the
    // Preview button of the print settings)
    void printPreview(const PrintSettings *settings = nullptr);
    void showPrintSettings();
    bool canPrint() const;
    void integrateDesktop(bool quiet);
    void restartApplication();
    void showAddonManager();
    void refreshInspector();
    void saveUiState();
    void restoreUiState();

    TextEditor *editor() const;
    // The text field the keyboard was in last (0.21.1): the format bar, the Format menu
    // and their shortcuts act on it while the keyboard is on the format bar itself (the
    // size field takes the keyboard when clicked) or in a menu.
    QPointer<QTextEdit> m_lastTextField;
    // a long text field of the Objects sheet with the cursor (0.21.1): bold, italic,
    // underline, strikethrough, text color and highlight act on it
    ObjectTextEdit *objectTextTarget() const;
    bool objectFormat(const QString &key, ObjectTextEdit *o);

    std::unique_ptr<Project> m_project;
    BinderPanel *m_binder = nullptr;
    Inspector *m_inspector = nullptr;
    QSplitter *m_mainSplit = nullptr;
    QSplitter *m_editorSplit = nullptr;
    EditorPane *m_panes[2] = {nullptr, nullptr};
    EditorPane *m_active = nullptr;
    QPointer<CompositionWindow> m_composition;
    QStringList m_lastSettingsNotes;
    QList<QAction *> m_toolActions;

    // toolbars
    QToolBar *m_toolbar = nullptr;
    QToolBar *m_formatBar = nullptr;
    QToolBar *m_iconBar = nullptr;
    ColorPickButton *m_textColorBtn = nullptr;
    ColorPickButton *m_highlightBtn = nullptr;
    QComboBox *m_styleCombo = nullptr;
    QMenu *m_styleMenu = nullptr;
    QPointer<StylesPanel> m_stylesPanel;
    QPointer<QWidget> m_styleUiParent;
    QPointer<FormatFindWindow> m_formatFind;
    QFontComboBox *m_fontCombo = nullptr;
    QDoubleSpinBox *m_sizeSpin = nullptr;
    QComboBox *m_lineCombo = nullptr;
    QList<QAction *> m_rtfOnly;
    QList<QAction *> m_markdownOnly;
    QHash<QString, QAction *> m_barItems;   // controls of the bars (id -> widget action)
    QString m_builtBars;                    // what the bars were built from
    QList<QToolButton *> m_barButtons;      // buttons among the controls (icon size, text)
    QList<QAction *> m_barOwned;            // separators and spaces of the bars
    QList<QAction *> m_scriptActions;       // "script:<element>", one per element of the script format
    QMenu *m_scriptMenu = nullptr, *m_scriptElementMenu = nullptr, *m_scriptFormatMenu = nullptr;
    QList<QAction *> m_styleActions;        // "style:<name>", one per style of the project
    QList<QAction *> m_layoutActions;       // "layout:<id>"
    QString m_binderAffects = QStringLiteral("current");
    QActionGroup *m_affectsGroup = nullptr;
    QMenu *m_layoutMenu = nullptr;
    TargetLineEdit *m_quickSearch = nullptr;
    QCompleter *m_completer = nullptr;
    QStandardItemModel *m_completerModel = nullptr;

    // actions
    QHash<QString, QAction *> a;
    QList<QAction *> m_mediaActions;
    QAction *m_mediaMissing = nullptr;
    double m_mediaVolumeBeforeMute = 1.0;
    QMenu *m_recentMenu = nullptr;
    QMenu *m_templateMenu = nullptr;
    QMenu *m_labelMenu = nullptr;
    QMenu *m_statusMenu = nullptr;
    QMenu *m_iconMenu = nullptr;
    QMenu *m_defaultTemplateMenu = nullptr;
    QMenu *m_addMenu = nullptr;
    QMenu *m_binderGearMenu = nullptr;
    QMenu *m_bookmarkMenu = nullptr;
    QActionGroup *m_viewGroup = nullptr;
    QActionGroup *m_revisionGroup = nullptr;
    bool m_updatingUi = false;
    QSet<QString> m_editedSinceSave;        // texts edited since the last File > Save (0.21)
    // documents without a name take the first line of their text (0.40.3, MainWindowTitles.cpp)
    void queueAutoTitle(const QString &id);
    void queueUntitledDocuments();
    void updateAutoTitles();
    QSet<QString> m_autoTitlePending;
    QTimer *m_autoTitleTimer = nullptr;
    QDateTime m_lastInput;                  // Automatic Quit (0.21)
    QTimer *m_autoQuitTimer = nullptr;
    void notifySnapshots(int count);        // File > Options > Behaviors > Snapshots (0.21)
    bool m_quickSearchMark = false; // the matches of a quick search are marked in the editor
    FontScopeBar *m_fontScope = nullptr;
    struct FontOffer {
        QPointer<TextEditor> editor;
        QPointer<QTextDocument> doc;
        int from = 0, to = 0;      // the text that got the font
        int members = 0;           // direct::Family, direct::Size
        direct::CharSpec spec;
        QString style;             // empty: the offer is for all text of the project
        bool character = false;    // style is a character style
    } m_fontOffer;
    void checkFontOffer();
    bool m_closing = false;
    bool m_maximizedBeforeFullScreen = false;
};

} // namespace qb
