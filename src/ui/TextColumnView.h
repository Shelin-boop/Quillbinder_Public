#pragma once
#include "core/Project.h"
#include <QPointer>
#include <QScrollArea>

class QLabel;
class QVBoxLayout;

namespace qb {

class TextEditor;

// A scrollable column showing one document or several documents one after
// another ("combined text"). Every section is its own editor on the shared
// document, so edits appear in all other views at once.
class TextColumnView : public QScrollArea
{
    Q_OBJECT
public:
    explicit TextColumnView(QWidget *parent = nullptr);

    void setProject(Project *p);
    Project *project() const { return m_project; }
    void setItems(const QList<BinderItem *> &items, bool combined);
    void clear();
    QList<BinderItem *> items() const;
    QList<TextEditor *> editors() const;
    TextEditor *activeEditor() const;
    BinderItem *activeItem() const;
    bool isCombined() const { return m_combined; }

    void focusItem(BinderItem *item, bool atEnd = false);
    void scrollToItem(BinderItem *item);
    void setTypewriter(bool on);
    bool typewriter() const { return m_typewriter; }
    void applySettings();
    void setColumnWidthOverride(int px);
    void setTopMargin(int px); // empty space above the first line
    // Colors of this view instead of the theme's editor colors (composition mode).
    void setCustomColors(const QColor &text, const QColor &paper, const QColor &outside);
    void applyTheme();
    // Page view (View > Text Editing > Page View) is a setting of the project;
    // a view that never shows pages (composition mode) switches this off.
    void setFollowsPageView(bool on);
    bool pageView() const;
    // Page of the cursor and number of pages of everything shown; false when not in page view.
    bool pageInfo(int *page, int *pages) const;
    void refreshTitles();
    void ensureCursorVisible(TextEditor *e);

    int scrollValue() const;
    void setScrollValue(int v);

    // Where the writer was in the documents shown (0.21.1): the cursor and the first
    // character at the top, kept with the project (ViewMemory.h). Remembered when other
    // documents are shown or the project closes; given back when these documents are
    // shown again, unless something else (a search result, a link) has moved the
    // cursor or the view in the meantime.
    void rememberPlace();
    bool restorePlace();
    // a place to come back to is about to be given back
    bool placePending() const { return m_placePending; }

signals:
    void pagesChanged();
    void activeEditorChanged(qb::TextEditor *editor);
    void activeItemChanged(qb::BinderItem *item);
    void linkActivated(const QString &href);
    void noteClicked(const QString &noteId);
    void cursorMoved(qb::TextEditor *editor);

protected:
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    struct Section {
        BinderItem *item = nullptr;
        QLabel *title = nullptr;
        TextEditor *editor = nullptr;
        QWidget *divider = nullptr;
    };
    void applyColors();
    void applyPageMode();
    void updateColumnWidth();
    void updateMinHeights();
    void onEditorFocused(TextEditor *e);
    void navigate(TextEditor *e, bool down);
    void page(TextEditor *e, bool down);
    int sectionIndex(TextEditor *e) const;
    QString placeKey() const;

    QPointer<Project> m_project;
    QWidget *m_container = nullptr;
    QWidget *m_column = nullptr;
    QVBoxLayout *m_colLayout = nullptr;
    QWidget *m_bottomSpacer = nullptr;
    QList<Section> m_sections;
    QPointer<TextEditor> m_active;
    bool m_combined = false;
    bool m_typewriter = false;
    int m_widthOverride = 0;
    bool m_followsPageView = true;
    bool m_placePending = false;
    int m_placeGen = 0;
    bool m_customColors = false;
    QColor m_text, m_paper, m_outside, m_pageSurround;
};

} // namespace qb
