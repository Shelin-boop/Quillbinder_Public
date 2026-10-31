#pragma once
#include "core/Project.h"
#include "text/DirectFormat.h"
#include "text/TextProps.h"

#include <QPointer>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextEdit>
#include <functional>

class QMenu;
class QCompleter;
class QStringListModel;

namespace qb {

// Bold, italic, underline and strikethrough of what is selected (0.40.1), for the
// buttons of the format bar and the commands that switch them. A state is on when
// every selected character has it; without a selection it is the format the next
// typed character gets. (Before, the format in front of the cursor decided: with a
// selection made from right to left, the character before the selection.)
struct ToggleState {
    bool bold = false, italic = false, underline = false, strike = false;
};
// `adjust` may take away from a character's format what its paragraph style brings along.
using FormatAdjust = std::function<QTextCharFormat(const QTextCharFormat &, const QTextBlock &)>;
ToggleState toggleStateOf(const QTextEdit *edit, const FormatAdjust &adjust = {});

// What the format bar shows (0.40.3): what is seen in the editor, including what a style
// brings along, for all of the selection. A value that differs within the selection is
// "mixed": the field stays empty, no button is on. (Before, the buttons showed only what
// was set by hand, and fonts, sizes and paragraphs followed one end of the selection.)
struct BarState {
    ToggleState toggles;
    bool superscript = false, subscript = false;
    QString family;           // empty: mixed
    double size = 0;          // points; 0: mixed
    Qt::Alignment align;      // AlignLeft, AlignHCenter, AlignRight, AlignJustify; empty: mixed
    double lineSpacing = -1;  // factor; 0: not a proportional spacing; -1: mixed
};

// Rich text editor used everywhere (main editor, combined text sections,
// notes, quick reference, composition mode). Several editors may show the
// same QTextDocument; their line width is kept in sync.
class TextEditor : public QTextEdit
{
    Q_OBJECT
public:
    explicit TextEditor(QWidget *parent = nullptr);
    ~TextEditor() override;

    void setProjectDocument(Project *project, BinderItem *item, QTextDocument *doc, bool isNotes = false);
    // Show an empty private document (the shared one is going away).
    void detachDocument();
    Project *project() const { return m_project; }
    BinderItem *item() const { return m_item; }
    bool isNotes() const { return m_isNotes; }

    // auto height: the editor grows with its text (used inside scroll areas)
    void setAutoHeight(bool on);
    bool autoHeight() const { return m_autoHeight; }
    int availableTextWidth() const;
    void setMaxTextWidth(int w); // fixed width editor column
    void setMinimumTextHeight(int h) { m_minHeight = h; updateHeight(); }

    // Page view. An editor in page mode asks for its document to be laid out
    // on pages; the layout belongs to the document, so every editor on that
    // document then shows the pages (isPaged()).
    void setPageMode(bool on);
    bool pageMode() const { return m_pageMode; }
    bool isPaged() const;
    // combined text: the last sheet ends below the text instead of being a full page.
    void setTrimLastPage(bool on);
    void setPageSurround(const QColor &outside, const QColor &edge);
    int pageCount() const;        // 0 when not paged
    int pageOfCursor() const;     // 1-based; 0 when not paged
    int pageAt(int position) const;
    void pageLayoutChanged();     // called when the pages of the document changed
    // distance of the text from the left / right edge of the viewport
    int textInsetLeft() const;
    int textInsetRight() const;

    // formatting commands
    // The state of the selection (see ToggleState): byHand = only what was set by hand,
    // not what a paragraph or character style brings along (what the buttons show).
    ToggleState toggleState(bool byHand) const;
    BarState barState() const;
    // set for the whole selection (or what is typed next), whatever it was before
    void setBold(bool on);
    void setItalic(bool on);
    void setUnderline(bool on);
    void setStrike(bool on);
    void toggleBold();
    void toggleItalic();
    void toggleUnderline();
    void toggleStrike();
    void toggleSuperscript();
    void toggleSubscript();
    void toggleHighlight(const QColor &color = QColor());
    // The color buttons of the format bar (0.17): the selected text gets the
    // color, without a selection the text typed next. Invalid: automatic text
    // color / no highlight.
    void applyTextColor(const QColor &color);
    void applyHighlight(const QColor &color);
    void setParagraphStyle(const QString &style);
    // selection or the word at the cursor; empty = remove the character style
    void setCharacterStyle(const QString &style);
    // paragraph or character style by name; empty = "No Style"
    void applyStyle(const QString &style);
    QString paragraphStyleAtCursor() const;
    bool paragraphStylesMixed() const; // the selected paragraphs have different styles (0.40.3)
    QString characterStyleAtCursor() const;
    void setAlignment(Qt::Alignment a);
    void toggleList(bool numbered);
    void indentList(int delta);
    void clearFormatting();
    void setTextColor(const QColor &c);
    void setFontFamily(const QString &f);
    void setFontSize(double pt);
    void setLineSpacing(double factor);

    // fonts and paragraph formatting set by hand (see text/DirectFormat.h)
    direct::CharSpec charSpecAtCursor() const;
    // members: direct::CharMember bits of the spec to set (the others stay)
    void applyCharSpec(const direct::CharSpec &spec, int members);
    double fontSizeAtCursor() const;      // points, as stored (not zoomed)
    QString fontFamilyAtCursor() const;
    void changeFontSize(double delta);    // points; all sizes of the selection change
    direct::ParaSpec paraSpecAtCursor() const; // all attributes as shown; bits = set by hand
    void applyParaSpec(const direct::ParaSpec &spec);
    void resetParagraphFormatting(int bits = direct::AllPara);
    void changeLeftIndent(double deltaPx);
    void copyFormatting();
    void pasteFormatting();
    static bool hasCopiedFormatting();

    // notes
    QString addLinkedNote(NoteKind kind, const QString &text = QString()); // returns note id
    void addInlineNote(NoteKind kind);
    void removeNoteAtCursor();
    bool selectNote(const QString &noteId);
    void setNoteText(const QString &noteId, const QString &text);
    void setNoteColor(const QString &noteId, const QColor &color);
    void convertNote(const QString &noteId, NoteKind to);
    void removeNote(const QString &noteId);
    QString noteIdAt(int position) const;
    QString noteText(const QString &noteId) const;
    // Alt+click on a comment or footnote: its text in a small window at the
    // mouse, without opening the inspector. Returns the window (it stores
    // the text when it closes).
    QWidget *showNotePopover(const QString &noteId, const QPoint &globalPos);

    // links / images
    void insertLink(const QString &href, const QString &textIfNoSelection);
    void insertImageFile(const QString &path);
    void editImageSize(QTextCursor imageCursor);
    QString anchorAtCursor() const;

    // search highlight (per view)
    void setSearchHighlights(const QString &term, bool caseSensitive = false);
    // every match of the pattern is highlighted (an invalid pattern: none)
    void setSearchPattern(const QRegularExpression &re);
    QRegularExpression searchPattern() const { return m_searchRe; }
    int searchMatchCount() const { return m_searchCount; }
    bool findNext(const QString &term, bool backward, bool caseSensitive, bool wholeWord, bool wrap = true);
    bool findNext(const QRegularExpression &re, bool backward, bool wrap = true);
    int replaceAll(const QString &term, const QString &with, bool caseSensitive, bool wholeWord);

    static void setRevisionLevel(int level);
    static int revisionLevel();

    QString wordUnderCursor(QTextCursor *wordCursor = nullptr) const;
    // The cursor is being moved by the mouse (a click, a drag, a right-click): the view
    // around the editor scrolls only when the cursor is out of sight, never by a margin
    // (0.40.2: a click or right-click near the edge of the view made it jump).
    bool cursorMovedByMouse() const { return m_mouseMoving > 0; }

    // ---- scriptwriting (ui/ScriptEditing.cpp)
    // The document is a script (script mode) and this editor shows its text:
    // Tab and Enter move between the elements of the script format.
    bool scriptActive() const;
    QString scriptElementAtCursor() const;          // "" = general text
    void setScriptElement(const QString &element);  // "" = general text; the paragraphs of the selection
    // The list of elements at the cursor; the letter of an element chooses it.
    QMenu *scriptElementsMenu();
    void showScriptElementsMenu();
    // What Tab and Enter do at the cursor (for the footer bar; "" = nothing special).
    QString scriptTabHint() const;
    QString scriptEnterHint() const;
    // Auto-completion: the texts offered at the cursor, and taking one.
    QStringList scriptCompletions(bool evenIfEmpty = false) const;
    void acceptScriptCompletion(const QString &text);
    bool scriptCompletionVisible() const;

signals:
    void focused(qb::TextEditor *editor);
    void navigateBeyond(qb::TextEditor *editor, bool down); // arrow keys past first/last line
    void pageRequest(qb::TextEditor *editor, bool down);
    void linkActivated(const QString &href);
    void noteClicked(const QString &noteId);
    void heightChanged();
    void splitRequested(qb::TextEditor *editor, bool selectionAsTitle);
    void documentLinkDropped(qb::TextEditor *editor, const QString &itemId);
    // the element at the cursor changed without the cursor moving
    void scriptElementChanged(qb::TextEditor *editor);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
    QMimeData *createMimeDataFromSelection() const override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    void updateHeight();
    void mergeFormat(const QTextCharFormat &fmt);
    bool isRtfProject() const;
    QList<QTextBlock> selectedBlocks() const;
    QTextCursor noteCursor(const QString &noteId) const;
    void smartTyping(QKeyEvent *e, bool *handled);
    void correctWordBefore(QKeyEvent *e);
    bool makeDocumentLink();
    void updateSearchSelections();
    struct ScriptPiece;
    bool scriptKeyPress(QKeyEvent *e);
    void scriptAfterKey(QKeyEvent *e);
    void scriptNewParagraph(const QString &element);
    bool scriptPiece(ScriptPiece *out) const;
    void updateScriptCompletion(bool evenIfEmpty);
    void hideScriptCompletion();

    QPointer<Project> m_project;
    QTextDocument *m_placeholder = nullptr;
    BinderItem *m_item = nullptr;
    bool m_isNotes = false;
    bool m_autoHeight = false;
    int m_minHeight = 0;
    int m_maxTextWidth = 0;
    bool m_pageMode = false;
    bool m_trimLastPage = false;
    bool m_healQueued = false;
    QColor m_pageOutside, m_pageEdge;
    QRegularExpression m_searchRe;
    bool m_searchOn = false;
    int m_searchCount = 0;
    int m_mouseMoving = 0;   // see cursorMovedByMouse()
    QCompleter *m_scriptCompleter = nullptr;
    QStringListModel *m_scriptModel = nullptr;
    static int s_revision;
};

// Lets the main window add entries to every editor's context menu.
void setTextEditorContextHook(std::function<void(TextEditor *, QMenu *, const QPoint &)> f);

// Keeps all editors that show the same document at one line width.
namespace widths {
void registerEditor(TextEditor *e);
void unregisterEditor(TextEditor *e);
void update(QTextDocument *doc);
} // namespace widths

} // namespace qb
