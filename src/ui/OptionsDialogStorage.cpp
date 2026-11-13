// 0.90.1 (the usability review of 0.90.0, UR-10 and UR-03): where each option of File > Options is kept. Every control
// is marked with the settings key it writes (the list of controls and keys was worked out from apply() and
// AppSettings::save(); a new option needs a line here, and qb_v0901_tests checks that every control has one).
#include "ui/OptionsDialog.h"

#include "core/Project.h"
#include "core/SettingsLayer.h"
#include "ui/AlignButtons.h"
#include "ui/KeyboardPage.h"
#include "ui/OptionStorage.h"
#include "ui/ProjectLook.h"
#include "ui/Theme.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QPushButton>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QScrollArea>
#include <QSpinBox>

namespace qb {

void OptionsDialog::markStorage()
{
    optstore::mark(m_autoQuit, QStringLiteral("app/autoQuit"));
    optstore::mark(m_autoQuitMinutes, QStringLiteral("app/autoQuitMinutes"));
    optstore::mark(m_autosave, QStringLiteral("app/autosave"));
    optstore::mark(m_backLinks, QStringLiteral("behaviour/backLinks"));
    optstore::mark(m_backupClose, QStringLiteral("backup/onClose"));
    optstore::mark(m_backupDir, QStringLiteral("backup/dir"));
    optstore::mark(m_backupKeep, QStringLiteral("backup/keep"));
    optstore::mark(m_backupManual, QStringLiteral("backup/onManualSave"));
    optstore::mark(m_backupOpen, QStringLiteral("backup/onOpen"));
    optstore::mark(m_binderIndent, QStringLiteral("appearance/binderIndent"));
    optstore::mark(m_blockCursor, QStringLiteral("editing/blockCursor"));
    optstore::mark(m_blockWidth, QStringLiteral("editing/blockCursorWidth"));
    optstore::mark(m_capI, QStringLiteral("editing/capitalizeI"));
    optstore::mark(m_capSentences, QStringLiteral("editing/capitalizeSentences"));
    optstore::mark(m_cardWidth, QStringLiteral("appearance/cardWidth"));
    optstore::mark(m_citations, QStringLiteral("app/citationsApp"));
    optstore::mark(m_city, QStringLiteral("app/city"));
    optstore::mark(m_compEsc, QStringLiteral("behaviour/compEsc"));
    optstore::mark(m_compHideMain, QStringLiteral("behaviour/compHideMain"));
    optstore::mark(m_compScreen, QStringLiteral("behaviour/compScreen"));
    optstore::mark(m_completions, QStringLiteral("editing/completions"));
    optstore::mark(m_corkDoubleClick, QStringLiteral("behaviour/corkDoubleClickNew"));
    optstore::mark(m_country, QStringLiteral("app/country"));
    optstore::mark(m_dashes, QStringLiteral("editing/smartDashes"));
    optstore::mark(m_dateCustom, QStringLiteral("editing/dateFormat"));
    optstore::mark(m_dateFormat, QStringLiteral("editing/dateFormat"));
    optstore::mark(m_detectLinks, QStringLiteral("editing/detectDocLinks"));
    optstore::mark(m_detectUrls, QStringLiteral("editing/detectUrls"));
    optstore::mark(m_deviceName, QStringLiteral("app/deviceName"));
    optstore::mark(m_docConverter, QStringLiteral("sharing/docConverter"));
    optstore::mark(m_dragChildren, QStringLiteral("behaviour/dragExportChildren"));
    optstore::mark(m_dragFormat, QStringLiteral("behaviour/dragExportFormat"));
    optstore::mark(m_dropOnText, QStringLiteral("behaviour/dropOnTextDocuments"));
    optstore::mark(m_ellipsis, QStringLiteral("editing/ellipsis"));
    optstore::mark(m_email, QStringLiteral("app/email"));
    optstore::mark(m_encoding, QStringLiteral("sharing/encoding"));
    optstore::mark(m_fixedWidth, QStringLiteral("appearance/fixedWidth"));
    optstore::mark(m_folderCork, QStringLiteral("behaviour/folderCorkboard"));
    optstore::mark(m_forename, QStringLiteral("app/forename"));
    optstore::mark(m_freezeSeconds, QStringLiteral("app/freezeSeconds"));
    optstore::mark(m_fullPath, QStringLiteral("appearance/showFullPath"));
    optstore::mark(m_htmlAsText, QStringLiteral("sharing/htmlAsText"));
    optstore::mark(m_importComments, QStringLiteral("sharing/commentsInline"));
    optstore::mark(m_importFootnotes, QStringLiteral("sharing/footnotesInline"));
    optstore::mark(m_importIgnoreStyles, QStringLiteral("sharing/ignoreStyles"));
    optstore::mark(m_keepReports, QStringLiteral("app/keepReports"));
    optstore::mark(m_labelBinder, QStringLiteral("appearance/labelInBinder"));
    optstore::mark(m_labelIcons, QStringLiteral("appearance/labelInIcons"));
    optstore::mark(m_linkOpens, QStringLiteral("behaviour/linkOpens"));
    optstore::mark(m_liveCounts, QStringLiteral("editing/countChars"));
    optstore::mark(m_mediaBackend, QStringLiteral("playback/backend"));
    optstore::mark(m_mediaRemember, QStringLiteral("playback/rememberPosition"));
    optstore::mark(m_mediaRewind, QStringLiteral("playback/rewindSeconds"));
    optstore::mark(m_mediaSkip, QStringLiteral("playback/skipSeconds"));
    optstore::mark(m_newAlign, QStringLiteral("editing/newAlign"));
    optstore::mark(m_newAutoFont, QStringLiteral("editing/newFontFamily"));
    optstore::mark(m_newFont, QStringLiteral("editing/newFontFamily"));
    optstore::mark(m_newIndent, QStringLiteral("editing/newFirstLineIndent"));
    optstore::mark(m_newParaSpacing, QStringLiteral("editing/newParagraphSpacing"));
    optstore::mark(m_newSize, QStringLiteral("editing/newFontSize"));
    optstore::mark(m_newSpacing, QStringLiteral("editing/newLineSpacing"));
    optstore::mark(m_noColorAnnotations, QStringLiteral("editing/noColorAnnotations"));
    optstore::mark(m_notesBeforePunct, QStringLiteral("editing/notesBeforePunctuation"));
    optstore::mark(m_openLast, QStringLiteral("app/openLast"));
    optstore::mark(m_overtype, QStringLiteral("editing/overtypeKey"));
    optstore::mark(m_phone, QStringLiteral("app/phone"));
    optstore::mark(m_portal, QStringLiteral("app/portal"));
    optstore::mark(m_quoteStyle, QStringLiteral("editing/quoteStyle"));
    optstore::mark(m_quotes, QStringLiteral("editing/smartQuotes"));
    optstore::mark(m_recentCount, QStringLiteral("app/recentCount"));
    optstore::mark(m_reloadZoom, QStringLiteral("editing/reloadZoom"));
    optstore::mark(m_reopenQuickRefs, QStringLiteral("behaviour/reopenQuickRefs"));
    optstore::mark(m_returnCreates, QStringLiteral("behaviour/returnCreatesItem"));
    optstore::mark(m_returnEndsSynopsis, QStringLiteral("behaviour/returnEndsSynopsis"));
    optstore::mark(m_rulerSnap, QStringLiteral("editing/rulerSnapMm"));
    optstore::mark(m_rulerUnit, QStringLiteral("editing/rulerUnit"));
    optstore::mark(m_scriptNoSmart, QStringLiteral("editing/scriptNoSmart"));
    optstore::mark(m_sepMerge, QStringLiteral("app/sepMerge"));
    optstore::mark(m_sepMergeCustom, QStringLiteral("app/sepMergeCustom"));
    optstore::mark(m_sepScratch, QStringLiteral("app/sepScratch"));
    optstore::mark(m_sepScratchCustom, QStringLiteral("app/sepScratchCustom"));
    optstore::mark(m_sharedTemplates, QStringLiteral("app/sharedTemplates"));
    optstore::mark(m_snapNotify, QStringLiteral("behaviour/snapshotNotify"));
    optstore::mark(m_snapOnSave, QStringLiteral("app/snapshotOnSave"));
    optstore::mark(m_spell, QStringLiteral("editing/spellCheck"));
    optstore::mark(m_spellLang, QStringLiteral("editing/spellLanguage"));
    optstore::mark(m_spellNumbers, QStringLiteral("editing/spellIgnoreNumbers"));
    optstore::mark(m_spellUnderline, QStringLiteral("editing/spellUnderline"));
    optstore::mark(m_stamps, QStringLiteral("appearance/stamps"));
    optstore::mark(m_state, QStringLiteral("app/state"));
    optstore::mark(m_street, QStringLiteral("app/street"));
    optstore::mark(m_subdocsAsFolders, QStringLiteral("behaviour/subdocsAsFolders"));
    optstore::mark(m_substitutionsOn, QStringLiteral("editing/substitutionsOn"));
    optstore::mark(m_surname, QStringLiteral("app/surname"));
    optstore::mark(m_tint, QStringLiteral("appearance/tintCards"));
    optstore::mark(m_titles, QStringLiteral("appearance/combinedTitles"));
    optstore::mark(m_toolbarIcons, QStringLiteral("toolbars/iconSize"));
    optstore::mark(m_typewriter, QStringLiteral("appearance/typewriter"));
    optstore::mark(m_zip, QStringLiteral("app/zip"));
    // not written through AppSettings::save()
    optstore::mark(m_projectFirst, QStringLiteral("app/projectFirst"));
    optstore::mark(m_language, QStringLiteral("app/language"));
    optstore::mark(m_groupDigits, QStringLiteral("app/groupDigits"));
    optstore::mark(m_exportComments, QStringLiteral("export/comments"));
    optstore::mark(m_mmEnterEnds, QStringLiteral("mindmap/enterEnds"));
    optstore::mark(m_mmEnterCreates, QStringLiteral("mindmap/enterCreates"));
    optstore::mark(m_mmUseSelected, QStringLiteral("mindmap/useSelectedStyle"));
    optstore::mark(m_mmDashed, QStringLiteral("mindmap/dashedLines"));
    optstore::mark(m_mmAutoSize, QStringLiteral("mindmap/autoSize"));
    optstore::mark(m_mmNoteWidth, QStringLiteral("mindmap/newNoteWidth"));
    optstore::mark(m_mmNoteBorder, QStringLiteral("mindmap/newNoteBorder"));
    optstore::mark(m_mmNoteFill, QStringLiteral("mindmap/newNoteFill"));
    optstore::mark(m_mmIconSize, QStringLiteral("mindmap/iconSize"));
    optstore::mark(m_mmBgLightBtn, QStringLiteral("mindmap/boardBackground"));
    optstore::mark(m_mmBgDarkBtn, QStringLiteral("mindmap/boardBackgroundDark"));
    optstore::mark(m_exportFormat, QStringLiteral("export/format"));
    optstore::mark(m_exportNotes, QStringLiteral("export/notes"));
    // Appearance: the colors and fonts of the window's areas are part of the look; the colors of menus and panels
    // (General Interface) and the interface font are the program's
    for (auto it = m_colorButtons.cbegin(); it != m_colorButtons.cend(); ++it)
        optstore::mark(it.value(), theme::isAreaColor(it.key()) ? QStringLiteral("look") : QStringLiteral("colors/") + it.key());
    for (auto it = m_fontButtons.cbegin(); it != m_fontButtons.cend(); ++it)
        optstore::mark(it.value(), theme::isAreaFont(it.key()) ? QStringLiteral("look") : QStringLiteral("fonts/") + it.key());
    // the shortcuts the writer changed (one value, keyboard/shortcuts)
    if (m_keyboard)
        if (auto *edit = m_keyboard->findChild<QKeySequenceEdit *>(QStringLiteral("shortcutEdit")))
            optstore::mark(edit, QStringLiteral("keyboard/shortcuts"));
    optstore::mark(m_mmFaded, QStringLiteral("mindmap/faded"));
    optstore::mark(m_mmNoteSize, QStringLiteral("mindmap/newNoteSize"));
    optstore::mark(m_mmNoteShape, QStringLiteral("mindmap/newNoteShape"));
    optstore::mark(m_zoomPercent, QStringLiteral("zoomDefault/*"));
    for (int i = 1; i <= 5; ++i)
        optstore::mark(findChild<QAbstractButton *>(QStringLiteral("optRevision%1").arg(i)), QStringLiteral("editing/revisionColors"));
    // the look of a project (settings/look.json) or the look for new projects
    optstore::mark(m_theme, QStringLiteral("look"));
    optstore::mark(m_textBorder, QStringLiteral("look"));
    optstore::mark(m_editorWidth, QStringLiteral("look"));
    // the writing tools' sections: their keys are under tools/<id>/
    for (int i = 0; i < m_toolSectionIds.size(); ++i)
        if (auto *sec = findChild<QScrollArea *>(QStringLiteral("toolSection:") + m_toolSectionIds.at(i)); sec && sec->widget())
            for (QWidget *w : sec->widget()->findChildren<QWidget *>()) {
                const bool input = qobject_cast<QCheckBox *>(w) || qobject_cast<QComboBox *>(w)
                                   || qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QLineEdit *>(w);
                // the line edit inside a spin box or a combo box is part of it
                const bool part = qobject_cast<QAbstractSpinBox *>(w->parentWidget()) || qobject_cast<QComboBox *>(w->parentWidget());
                if (input && !part)
                    optstore::mark(w, QStringLiteral("tools/") + m_toolSectionIds.at(i) + QStringLiteral("/*"));
            }
}

void OptionsDialog::refreshStorage()
{
    optstore::State st;
    st.projectOpen = m_hasProject;
    st.projectTitle = look::project() ? look::project()->title() : QString();
    st.thisProject = m_optScope == ThisProject;
    st.projectFirst = settingslayer::projectFirst();
    st.lookThisProject = m_scope == ScopeProject;
    optstore::refresh(this, st);
}

} // namespace qb
