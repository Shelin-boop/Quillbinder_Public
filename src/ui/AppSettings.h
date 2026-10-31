#pragma once
#include "ui/Theme.h"

#include <QColor>
#include <QMap>
#include <QObject>
#include <QStringList>

namespace qb {

// Application-wide options (File > Options). Stored with QSettings in
// ~/.config/Quillbinder/Quillbinder.conf
class AppSettings : public QObject
{
    Q_OBJECT
public:
    static AppSettings &instance();
    void load();
    void save();
    void addRecent(const QString &projectPath);
    void removeRecent(const QString &projectPath);
    void notifyChanged() { save(); emit changed(); }
    // File > Options > Defaults (0.21): every option back to the program's standard (recent projects,
    // themes of one's own, layouts and shortcuts are kept)
    void resetToDefaults();
    // the standard Edit Substitutions
    static QList<QPair<QString, QString>> standardSubstitutions();
    // The own colors a new installation starts with (0.21.2, the writer's standard): the
    // main editor's paper #d1d495 and beside the text column #999999 (keys as in theme files).
    static QMap<QString, QColor> standardOwnColors();
    // 0.21.2: the writer's standard (formatting of new projects, the two editor colors, the
    // editor width of 500 px) is put once into settings made by an older version, at the
    // first load; the key "app/standard0212" records that it was done (File > Options >
    // Defaults removes it, so the standard comes back there as well).
    static constexpr const char *kStandardKey = "app/standard0212";

    // general
    QString forename, surname;
    int autosaveSeconds = 2;
    bool openLastProject = true;
    QStringList recentProjects;
    QString lastProjectDir;
    bool useDesktopPortal = true;

    // appearance
    QString theme = QStringLiteral("system"); // see theme::available()
    // The writer's own colors and fonts on top of the theme (keys as in
    // theme files, see Theme.h). Fonts are FontSpec::toString().
    QMap<QString, QColor> ownColors;
    QMap<QString, QString> ownFonts;
    bool fixedWidth = true;
    int editorWidth = 500;                // px at 100 % (760 before 0.21.2)
    // The border in the paper's color left and right of the text (0.40.2), px at 100 %. Like
    // the editor width and the colors of the areas, this is what a new project gets: every
    // project keeps its own look (ui/ProjectLook.h).
    int textBorder = 10;
    bool showTitlesInCombined = false;
    bool typewriterScrolling = false;
    double compositionPaperWidth = 0.55; // share of the screen
    double compositionFade = 0.0;
    int compositionZoomPercent = 125;
    bool showStatusStamps = true;
    bool tintCardsWithLabel = false;
    bool labelColorInBinder = true;
    bool labelColorInIcons = true;   // the label color shines through the light parts of binder icons (0.17)
    int binderIndent = 12;           // px per level in the binder (0.17; Qt's standard is 20)
    int cardWidth = 210;
    int cardsPerRow = 0; // 0 = automatic

    // editing
    bool countCharacters = false;
    bool spellCheck = true;
    QString spellLanguage = QStringLiteral("en_US");
    bool smartQuotes = true;
    bool smartDashes = true;
    bool showRuler = true;
    QString rulerUnit;            // "cm" | "in"; empty = from the system's region
    // formatting of new projects (File > Options > Editing); empty font = automatic
    // (0.21.2, the writer's standard: 11.0 pt, line spacing 1.09, no first line indent, 6 pt
    // after paragraphs; before: 13 pt, 1.2, 24 px, 4 px). New projects and Scrivener imports.
    QString newFontFamily;
    double newFontSize = 11.0;
    double newLineSpacing = 1.09;
    double newFirstLineIndent = 0.0;    // px at 100 %
    double newParagraphSpacing = 8.0;   // px (= 6 pt)
    Qt::Alignment newAlign = Qt::AlignLeft; // 0.40.3; before: newJustify (still written for older versions)
    QString newStylesXml;         // styles of new projects (Format > Style); empty = built-in
    QString newPageSetup;         // page of new projects (page view), JSON; empty = by region
    QString newPrintSettings;     // File > Print Settings of new projects, JSON; empty = the defaults
    QString welcomeFolder;        // where the welcome project was made last ("" = Documents; 0.40)
    bool welcomeShown = false;    // the welcome project was opened once (at the first start of 0.40 or later)

    // backup
    bool backupOnClose = true;
    bool backupOnOpen = false;
    int backupKeep = 5;
    QString backupDir;

    // behaviour
    QString linkOpens = QStringLiteral("other"); // other | current | window
    bool startWithCorkboard = true;               // folders open in corkboard
    bool floatQuickRef = false;                   // Quick Reference windows stay above the main window
    bool reopenQuickRefs = true;                  // Quick Reference panels come back with their project
    // dragging and dropping
    bool dropOnTextDocuments = true;              // a document dropped onto a text document becomes its subdocument
    QString dragExportFormat = QStringLiteral("rtf"); // text dragged to other programs: rtf | docx | md | txt
    bool dragExportChildren = true;               // ... with the subdocuments (folders become folders)
    // playback of audio and video (add-on "Media Playback")
    bool mediaPauseRewind = false;                // pausing rewinds a little (button in the footer bar)
    double mediaRewindSeconds = 2.0;              // ... by this much
    int mediaSkipSeconds = 5;                     // Navigate > Media > Skip Back / Skip Forward
    QString mediaBackend;                         // "ffmpeg" | "gstreamer"; empty = the engine's choice
    bool mediaRememberPosition = true;            // a media file opens where it was paused
    double mediaVolume = 1.0;                     // 0 ... 1, shared by all players

    // icons (Documents > Change Icon, 0.17)
    QStringList recentIcons;                      // the last five icons chosen, newest first
    QStringList iconBar;                          // the icons of the icon bar (at most 10); empty = standard set
    bool iconBarSet = false;                      // iconBar was chosen by the writer (an empty bar stays empty)
    // File > Compare and Transfer (0.18): decide by chapter / change by change
    bool compareByChapter = true;
    bool compareStepByStep = false;
    // the colors of the two color buttons of the format bar (0.17)
    QColor barTextColor = QColor(200, 30, 30);
    QColor barHighlightColor = QColor(255, 230, 0);

    // ---- 0.21: the options of File > Options that Scrivener has as well ----
    // General
    int recentCount = 12;                         // Saving: number of recent projects (File > Open Recent)
    bool snapshotOnManualSave = false;            // Saving: File > Save takes snapshots of the texts changed since
    QString street, city, state, zip, country, phone, email; // Author Information (placeholders <$street> ...)
    QString sharedTemplatesDir;                   // Shared Templates: more project templates (File > New Project)
    QString citationsApp;                         // Citations: the program Format > Bibliography/Citations opens
    QString sepMerge = QStringLiteral("return");  // Separators: return | empty | page | none | custom (display::insertSeparator)
    QString sepMergeCustom = QStringLiteral("---");
    QString sepScratch = QStringLiteral("return"); // ... between scratch pad notes added to a document
    QString sepScratchCustom = QStringLiteral("***");
    bool autoQuit = false;                        // Automatic Quit after a time without input
    int autoQuitMinutes = 30;
    // Editing
    bool reloadZoom = true;                       // the editors open with the zoom they had (else the default text zoom)
    double rulerSnapMm = 0;                       // the step of the ruler; 0 = 2.5 mm / 1/16 in
    bool overtypeKey = false;                     // the Insert key switches overtype on and off
    QString customDateFormat;                     // Insert > Current Date in this format (Qt's date format; empty = the region's)
    bool noColorInlineAnnotations = false;        // inline annotations keep the text color (only the bubble shows them)
    bool notesBeforePunctuation = false;          // a comment or footnote made on a selection stops before punctuation at its end
    bool blockCursor = false;                     // a wide insertion point
    int blockCursorWidth = 5;
    QList<QColor> revisionColors;                 // Revisions: five colors; empty = the standard ones
    // Corrections
    bool capitalizeSentences = false;
    bool capitalizeI = false;
    QString quoteStyle = QStringLiteral("en");    // en “…” ‘…’ | de „…“ ‚…‘ | fr «…» ‹…› | de2 »…« ›…‹ | sv ”…” ’…’
    bool ellipsis = false;                        // three periods become one character (off: "..." stays three periods)
    bool scriptNoSmart = true;                    // no smart quotes, dashes and ellipsis in script mode
    bool detectUrls = false;                      // a typed web address becomes a link
    bool detectDocLinks = false;                  // [[Title]] becomes a link to that document (made when missing)
    bool completions = true;                      // script mode: the list of completions
    bool substitutionsOn = false;                 // Edit Substitutions
    QList<QPair<QString, QString>> substitutions; // typed text -> replacement
    bool spellIgnoreNumbers = true;               // words with digits are not checked
    QString spellUnderline = QStringLiteral("waves"); // waves | dots | line
    // Behaviors
    QString compositionScreen = QStringLiteral("current"); // current | primary | <screen name>
    bool compositionHideMain = false;
    bool compositionEscCloses = true;
    bool backLinks = false;                       // a document link adds a bookmark back to the document it is in
    bool corkDoubleClickNew = true;               // a double click on the corkboard's background makes a new card
    bool subdocsAsFolders = true;                 // a text with subdocuments opens like a folder (corkboard ...), as before 0.21
    bool returnEndsSynopsis = true;               // Return ends editing a card's synopsis (Shift+Return: new line)
    bool returnCreatesItem = false;               // Return on a selected card or outliner row makes a new one
    bool snapshotNotify = false;                  // a message in the status bar when snapshots are taken
    // Appearance
    bool showFullPath = false;                    // the project's whole path in the title bar
    // Sharing
    bool importCommentsInline = false;            // comments of imported files become inline annotations
    bool importFootnotesInline = true;            // footnotes and endnotes become inline footnotes (as before 0.21)
    bool importIgnoreStyles = false;              // named styles of imported files are left out
    QString plainTextEncoding = QStringLiteral("auto"); // auto | UTF-8 | windows-1252 | ISO-8859-1 | ISO-8859-15 | UTF-16
    bool htmlAsText = false;                      // .html files become text documents (else web page documents)
    QString docConverter = QStringLiteral("builtin"); // .doc files: builtin (add-on) | libreoffice
    // Backup
    bool backupOnManualSave = false;
    // Extras: crash and freeze reports
    int freezeSeconds = 30;
    int keepReports = 20;

    // toolbars (View > Customize Toolbars); an empty list means the standard set
    QStringList toolbarMain, toolbarFormat;       // item ids, see ToolbarConfig.h
    int toolbarIconSize = 22;                     // main toolbar: 16 | 22 | 32
    QString toolbarStyle = QStringLiteral("icons"); // icons | beside | under

signals:
    void changed();
    // File > Options > General > Reset Warnings: every "do not ask again" is forgotten
    // (the main window clears the ones kept in the project, too)
    void warningsReset();

private:
    AppSettings() = default;
    void pushToModules();
};

// Lengths in the ruler and the paragraph dialog (centimetres or inches).
namespace units {
bool metric();
QString suffix();                 // " cm" / " in"
double fromPx(double px);         // pixels at 100 % -> unit
double toPx(double value);
double step();                    // snap distance of the ruler, in px
QString format(double px);        // "2.50 cm"
} // namespace units

} // namespace qb
