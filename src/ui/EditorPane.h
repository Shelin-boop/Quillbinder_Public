#pragma once
#include "core/Project.h"
#include <QPointer>
#include <QRegularExpression>
#include <QUrl>
#include <QVariantMap>
#include <QWidget>

class QLabel;
class QMenu;
class QLineEdit;
class QStackedWidget;
class QToolButton;
class QComboBox;
class QCheckBox;
class QProgressBar;
class QSlider;
class QSplitter;

namespace qb {

class TextEditor;
class TextColumnView;
class Corkboard;
class Outliner;
class MediaView;
namespace playback {
class MediaControls;
}
class Ruler;
class Copyholder;
class FilterBar;

enum class ViewMode { Single = 0, Combined = 1, Corkboard = 2, Outliner = 3, Media = 4, Empty = 5 };

class EditorPane : public QWidget
{
    Q_OBJECT
public:
    explicit EditorPane(QWidget *parent = nullptr);
    void setProject(Project *p);
    Project *project() const { return m_project; }

    void showItems(const QList<BinderItem *> &items, bool addToHistory = true, int forcedMode = -1);
    QList<BinderItem *> items() const { return m_items; }
    ViewMode mode() const { return m_mode; }
    bool isGroup() const;
    void setViewMode(ViewMode m);

    BinderItem *currentItem() const;
    QList<BinderItem *> selectedItems() const;
    TextEditor *currentEditor() const;
    TextColumnView *textView() const { return m_text; }
    Corkboard *corkboard() const { return m_cork; }
    Outliner *outliner() const { return m_outliner; }
    MediaView *media() const { return m_media; }
    Ruler *ruler() const { return m_ruler; }
    void updateRuler(); // shown above text when View > Ruler is on

    bool isLocked() const { return m_locked; }
    void setLocked(bool on);
    void setActive(bool on, bool splitExists);
    void setSplitIcon(Qt::Orientation o);
    // What a selection in the corkboard or outliner opens (the button in
    // their footer goes through these in turn; Copyholder only while one is open).
    enum Affects { AffectsNothing = 0, AffectsOtherEditor = 1, AffectsCopyholder = 2 };
    int selectionAffects() const { return m_affects; }
    void setSelectionAffects(int what);
    void cycleSelectionAffects();
    bool selectionAffectsOther() const { return m_affects == AffectsOtherEditor; }
    void setSelectionAffectsOther(bool on);

    // The copyholder: a second document shown inside this editor.
    Copyholder *copyholder() const { return m_copyholder; }
    bool hasCopyholder() const;
    BinderItem *copyholderItem() const;
    void openCopyholder(BinderItem *item);
    void closeCopyholder();
    QString copyholderPlacement() const; // left | right | top | bottom
    void setCopyholderPlacement(const QString &placement);
    // the text editor that has the keyboard focus if it is the copyholder's
    TextEditor *focusedCopyholderEditor() const;
    void setOutlinerSynopsis(bool on);
    // Corkboard arrangement: 0 grid, 1 freeform, 2 label threads (Corkboard::Mode)
    int corkMode() const;
    void setCorkMode(int mode);
    // The view groups open in is remembered per editor in the project
    // ("groupMode" + suffix); the second editor falls back to the first one's.
    void setStateSuffix(const QString &suffix) { m_stateSuffix = suffix; }
    ViewMode groupMode() const;
    void setGroupMode(ViewMode m);

    bool canGoBack() const { return m_historyIndex > 0; }
    bool canGoForward() const { return m_historyIndex + 1 < m_history.size(); }
    void goBack();
    void goForward();
    // The documents this editor has shown, oldest first (what Back and Forward
    // walk through); kept with the project.
    int historyCount() const { return int(m_history.size()); }
    int historyIndex() const { return m_historyIndex; }
    QString historyTitle(int index) const;
    void goToHistory(int index);
    void fillHistoryMenu(QMenu *menu, bool back);
    // The group the shown document belongs to.
    bool canGoToEnclosingGroup() const;
    void goToEnclosingGroup();
    void refresh();         // after structural changes
    void refreshHeader();
    void updateFooter();
    void showFindBar(bool replace);
    void findNext(bool backward);
    void setSearchHighlight(const QString &term);
    // Project search: the matches of the search are highlighted in every text
    // this editor shows from now on, Find Next walks through them, and the
    // find bar starts with findText. An invalid pattern ends it.
    void setSearchPattern(const QRegularExpression &re, const QString &findText);
    bool revealFirstMatch();
    QRegularExpression searchPattern() const { return m_searchRe; }
    void useSelectionForFind();
    bool findBarVisible() const;
    // Filter of corkboard and outliner (the bar opens with Find while one of them is shown).
    FilterBar *filterBar() const { return m_filterBar; }
    bool filterBarVisible() const;
    void showFilterBar();
    void closeFilter();
    void focusEditor();

    QVariantMap saveState() const;
    void restoreState(const QVariantMap &state);

signals:
    void activated(qb::EditorPane *pane);
    void currentItemChanged(qb::EditorPane *pane);
    void viewChanged(qb::EditorPane *pane);
    void splitButtonClicked(qb::EditorPane *pane, bool alternate);
    void linkActivated(qb::EditorPane *pane, const QString &href);
    void noteClicked(const QString &noteId);
    void itemContextMenuRequested(qb::EditorPane *pane, const QPoint &globalPos);
    void selectionChanged(qb::EditorPane *pane);
    void cursorMoved(qb::TextEditor *editor);
    void deleteRequested(qb::EditorPane *pane);
    void newItemRequested(qb::EditorPane *pane);   // corkboard / outliner (File > Options > Behaviors, 0.21)
    void filesDropped(qb::BinderItem *container, int row, const QList<QUrl> &urls);
    void openInOtherRequested(qb::EditorPane *pane, const QList<qb::BinderItem *> &items);
    void headerMenuRequested(qb::EditorPane *pane, const QPoint &globalPos);
    void itemsDroppedOnHeader(qb::EditorPane *pane, const QList<qb::BinderItem *> &items, bool alt);
    void navigateSibling(qb::EditorPane *pane, bool next);
    void quickReferenceRequested(qb::BinderItem *item);
    void copyholderChanged(qb::EditorPane *pane);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

protected:
    void changeEvent(QEvent *e) override;

private:
    struct HistoryEntry {
        QStringList ids;
        int mode;
    };
    void buildHeader();
    void buildFooter();
    void buildFindBar();
    void applyMode();
    void pushHistory();
    void updateCounts();
    void updatePageLabel();
    void placeCopyholder(const QString &placement);
    void updateAffectButtons();
    void editTarget();
    QList<BinderItem *> combinedItems() const;
    void emitActivated();
    QList<TextEditor *> findEditors() const;   // the texts the find bar works on
    QRegularExpression findPattern() const;
    void updateFindHighlights();
    void applyFilter();

    QPointer<Project> m_project;
    QList<BinderItem *> m_items;
    ViewMode m_mode = ViewMode::Empty;
    bool m_locked = false;
    bool m_active = false;
    int m_affects = AffectsNothing;
    QString m_stateSuffix;
    QList<HistoryEntry> m_history;
    int m_historyIndex = -1;

    // header
    QWidget *m_header;
    QToolButton *m_back, *m_forward, *m_iconButton, *m_contentsButton, *m_up, *m_down, *m_splitButton;
    QLineEdit *m_title;
    // views
    QStackedWidget *m_stack;
    TextColumnView *m_text;
    Corkboard *m_cork;
    Outliner *m_outliner;
    MediaView *m_media;
    QWidget *m_empty;
    Ruler *m_ruler;
    QSplitter *m_holderSplit;
    QWidget *m_mainArea;
    Copyholder *m_copyholder;
    QLabel *m_pageLabel;
    // find bar
    QWidget *m_findBar;
    QLineEdit *m_findEdit, *m_replaceEdit;
    QCheckBox *m_findCase, *m_findWhole;
    QWidget *m_replaceRow;
    QLabel *m_findCount;
    bool m_findInHolder = false;      // the find bar was opened from the copyholder
    QRegularExpression m_searchRe = QRegularExpression(QStringLiteral("(?")); // project search (invalid = none)
    FilterBar *m_filterBar;
    bool m_filterOpen = false;
    // footer
    QStackedWidget *m_footer;
    QComboBox *m_zoom;
    QToolButton *m_countButton;
    QLabel *m_scriptHint;
    QToolButton *m_scriptElement;
    QToolButton *m_targetButton;
    QProgressBar *m_targetBar;
    QLabel *m_mediaInfo;
    QStackedWidget *m_mediaFooter;          // picture / PDF controls, or the play controls
    playback::MediaControls *m_mediaControls;
    QToolButton *m_corkGrid, *m_corkFree, *m_corkLabels, *m_corkCommit, *m_affectCork, *m_affectOutliner;
    QSlider *m_cardSize;
    QToolButton *m_synopsisToggle;
    bool m_inPaletteChange = false;
};

} // namespace qb
