#pragma once
// File > Options (0.21: laid out as Scrivener's Options window).
//
// What the writer sees: a row of large icons at the top (General, Editing,
// Behaviors, Appearance, Corrections, Keyboard, Sharing, Backup, and Extras
// for what Scrivener does not have: mind maps, objects, add-ons, crash
// reports). Each section shows a list of pages on the left (or tabs), the
// options on the right. At the bottom: Manage (save and load all options as
// a file, themes), Defaults (everything back to the standard), OK, Cancel,
// Apply.
//
// How it works: every page is built from the current settings
// (AppSettings, MindMapPrefs, the zoom settings); nothing changes until OK or
// Apply, which call apply() and write every field back. The sections and
// pages are found by their names (showSection), so other windows can open
// the dialog at a page ("Options > Behaviors > Playback").
//
// Why one class: the pages share the Apply button and need each other's
// values only rarely; a page is a function that builds widgets and stores
// the pointers the apply() function reads.

#include <memory>
#include <vector>
#include "tools/ToolApi.h"
#include "ui/Theme.h"

#include <QColor>
#include <QDialog>
#include <QHash>
#include <QPointer>

class QListWidget;
class QStackedWidget;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QPushButton;
class QLabel;
class QToolButton;
class QVBoxLayout;
class QTabWidget;
class QFormLayout;
class QTableWidget;

namespace qb {

class ThemePreview;
class KeyboardPage;

class OptionsDialog : public QDialog
{
    Q_OBJECT
public:
    // The pages of 0.20 (callers and tests open the dialog with these); each one
    // is now a section of the icon row (Plugins: Extras > Add-ons and Plugins).
    enum Page { General = 0, Appearance, Editing, Behaviors, Keyboard, Backup, Plugins };
    // The sections of the icon row, in Scrivener's order plus Extras.
    enum Section { SGeneral = 0, SEditing, SBehaviors, SAppearance, SCorrections, SKeyboard, SSharing, SBackup, SExtras, SectionCount };

    explicit OptionsDialog(QWidget *parent = nullptr, int page = 0);
    KeyboardPage *keyboardPage() const { return m_keyboard; }
    void accept() override;
    void apply();   // store everything without closing

    // Shows a section and, when given, a page of it by its name ("Playback", "Revisions").
    void showSection(int section, const QString &page = QString());
    QStringList pageNames(int section) const;   // the pages (or tabs) of a section, for tests and the manual

    // Appearance page, also used by the tests (the buttons open color and
    // font dialogs and then call these).
    void selectTheme(const QString &id);
    void setOwnColor(const QString &key, const QColor &color);          // invalid color: back to the theme
    void setOwnFont(const QString &key, const theme::FontSpec &font);   // unset font: back to the theme
    void resetLook();
    QColor shownColor(const QString &key) const;
    int ownChanges() const { return int(m_ownColors.size() + m_ownFonts.size()); }
    // Saves theme + own changes under a new name and selects it; empty on failure.
    QString saveLookAsTheme(const QString &name, QString *error);

    // Manage > Save Options to File / Load Options from File (also used by the tests)
    bool saveOptionsToFile(const QString &file, QString *error = nullptr);
    bool loadOptionsFromFile(const QString &file, QString *error = nullptr);
    // Defaults: every option back to the standard (asks first when "ask" is true)
    bool restoreDefaults(bool ask);

private:
    // building blocks
    QWidget *subPages(int section, const QList<std::pair<QString, QWidget *>> &pages, const QString &listName);
    QWidget *tabbed(int section, const QList<std::pair<QString, QWidget *>> &pages, const QString &tabsName);
    static QWidget *page(QFormLayout **form);   // a page with a form, top aligned

    // the sections
    QWidget *generalSection();
    QWidget *editingSection();
    QWidget *behaviorsSection();
    QWidget *appearanceSection();
    QWidget *correctionsSection();
    QWidget *sharingSection();
    QWidget *backupSection();
    QWidget *extrasSection();

    // Appearance: one page per part of the window, with its options, fonts and colors
    QWidget *appearancePage(int which);
    void refreshLook();
    void reloadThemes(const QString &selectId);
    theme::Theme shownTheme() const;
    void fillManageMenu();

    // sections of writing tools on the General page (tools/ToolApi.h)
    std::vector<std::unique_ptr<tools::OptionsSection>> m_toolSections;
    QListWidget *m_pages;                       // the icon row ("optionPages")
    QStackedWidget *m_stack;                    // one widget per section
    QHash<int, QListWidget *> m_subLists;       // the page list of a section
    QHash<int, QTabWidget *> m_tabs;            // ... or its tabs
    KeyboardPage *m_keyboard = nullptr;
    class QMenu *m_manageMenu = nullptr;

    // General
    QCheckBox *m_openLast, *m_portal;
    QSpinBox *m_recentCount, *m_autosave;
    QCheckBox *m_snapOnSave;
    QLineEdit *m_forename, *m_surname, *m_street, *m_city, *m_state, *m_zip, *m_country, *m_phone, *m_email;
    QLineEdit *m_sharedTemplates, *m_citations;
    QComboBox *m_sepMerge, *m_sepScratch;
    QLineEdit *m_sepMergeCustom, *m_sepScratchCustom;
    QCheckBox *m_autoQuit;
    QSpinBox *m_autoQuitMinutes;

    // Editing
    QCheckBox *m_reloadZoom;
    QComboBox *m_zoomArea;
    QSpinBox *m_zoomPercent;
    QHash<QString, double> m_zoomDefaults;      // area -> default zoom, applied with OK
    QComboBox *m_rulerUnit;
    QDoubleSpinBox *m_rulerSnap;
    QCheckBox *m_overtype, *m_dateCustom;
    QLineEdit *m_dateFormat;
    QComboBox *m_liveCounts;
    QCheckBox *m_noColorAnnotations, *m_notesBeforePunct, *m_blockCursor;
    QSpinBox *m_blockWidth;
    QList<QPushButton *> m_revButtons;
    QList<QColor> m_revColors;
    QCheckBox *m_newAutoFont, *m_newJustify;
    bool m_clearNewStyles = false;
    QFontComboBox *m_newFont;
    QDoubleSpinBox *m_newSize, *m_newSpacing, *m_newIndent, *m_newParaSpacing;

    // Behaviors
    QComboBox *m_compScreen;
    QCheckBox *m_compHideMain, *m_compEsc;
    QComboBox *m_linkOpens;
    QCheckBox *m_backLinks;
    QCheckBox *m_corkDoubleClick;
    QComboBox *m_dragFormat = nullptr;
    QCheckBox *m_dropOnText = nullptr, *m_dragChildren = nullptr, *m_reopenQuickRefs = nullptr;
    QCheckBox *m_folderCork, *m_subdocsAsFolders;
    QDoubleSpinBox *m_mediaRewind = nullptr;   // Playback (add-on "Media Playback")
    QSpinBox *m_mediaSkip = nullptr;
    QComboBox *m_mediaBackend = nullptr;
    QCheckBox *m_mediaRemember = nullptr;
    QCheckBox *m_returnEndsSynopsis, *m_returnCreates;
    QCheckBox *m_snapNotify;

    // Appearance
    QComboBox *m_theme;
    QCheckBox *m_fixedWidth, *m_titles, *m_typewriter, *m_stamps, *m_tint, *m_labelBinder;
    QCheckBox *m_labelIcons = nullptr, *m_fullPath = nullptr;
    QSpinBox *m_binderIndent = nullptr, *m_editorWidth = nullptr, *m_cardWidth = nullptr;
    QComboBox *m_toolbarIcons = nullptr;
    QListWidget *m_areas = nullptr;
    QStackedWidget *m_areaStack = nullptr;
    ThemePreview *m_preview = nullptr;
    QLabel *m_ownInfo = nullptr;
    QPushButton *m_resetLook = nullptr;
    QMap<QString, QColor> m_ownColors;
    QMap<QString, QString> m_ownFonts;
    QHash<QString, QPushButton *> m_colorButtons, m_fontButtons;
    QHash<QString, QToolButton *> m_colorResets, m_fontResets;
    QHash<QString, QLabel *> m_lookLabels;
    QMap<theme::Role, QColor> m_shown;

    // Corrections
    QCheckBox *m_capSentences, *m_capI, *m_quotes, *m_dashes, *m_ellipsis, *m_scriptNoSmart;
    QComboBox *m_quoteStyle;
    QCheckBox *m_detectUrls, *m_detectLinks, *m_completions, *m_substitutionsOn;
    QList<QPair<QString, QString>> m_substitutions;
    QCheckBox *m_spell, *m_spellNumbers;
    QComboBox *m_spellLang, *m_spellUnderline;

    // Sharing
    QCheckBox *m_importComments, *m_importFootnotes, *m_importIgnoreStyles, *m_htmlAsText;
    QComboBox *m_encoding, *m_exportFormat, *m_docConverter;
    QCheckBox *m_exportComments, *m_exportNotes;

    // Backup
    QCheckBox *m_backupClose, *m_backupOpen, *m_backupManual;
    QSpinBox *m_backupKeep;
    QLineEdit *m_backupDir;

    // Extras
    QCheckBox *m_mmEnterEnds, *m_mmEnterCreates, *m_mmUseSelected, *m_mmDashed, *m_mmAutoSize;
    QSpinBox *m_mmFaded, *m_mmIconSize, *m_mmNoteBorder;
    QDoubleSpinBox *m_mmNoteSize, *m_mmNoteWidth;
    QComboBox *m_mmNoteShape;
    QPushButton *m_mmNoteFill;
    QColor m_mmFill;
    // the background of boards without a color of their own, light and dark themes (0.21.1)
    QPushButton *m_mmBgLightBtn = nullptr, *m_mmBgDarkBtn = nullptr;
    QColor m_mmBgLight, m_mmBgDark;
    QLabel *m_objectsTemplates = nullptr;
    bool m_resetObjectTemplates = false;
    QSpinBox *m_freezeSeconds, *m_keepReports;
};

} // namespace qb
