#pragma once
#include "compile/CompileFormat.h"
#include "core/Project.h"

#include <QDialog>
#include <QHash>

class QComboBox;
class QListWidget;
class QTableWidget;
class QTreeWidget;
class QCheckBox;
class QLabel;
class QStackedWidget;
class QTabWidget;
class QTextBrowser;
class QToolButton;

namespace qb {

// File > Compile (0.21: laid out as Scrivener's Compile Overview).
//
// Top: "Compile For" (the kind of file). Three columns below it:
//  - Formats: the writer's own formats and the built-in ones (+, edit, delete,
//    import, export under the list);
//  - Section Layouts: one font for everything or "Determined by Section
//    Layout", which layout each section type gets in this format (with a
//    preview of how such a section begins) and "Assign Section Layouts...";
//  - four tabs: Contents (what is compiled, front and back matter, the sheets
//    of the Objects), Metadata (title, author, cover ...), Options, and the
//    project's own Replacements.
// Preview, Save Settings, Compile and Cancel at the bottom. Everything chosen
// here is kept with the project (CompileProjectSettings in ProjectTypes.h).
class CompileDialog : public QDialog
{
    Q_OBJECT
public:
    CompileDialog(Project *p, QWidget *parent = nullptr);
    enum Tab { ContentsTab = 0, MetadataTab, OptionsTab, ReplacementsTab };
    void showTab(Tab t);

private:
    void loadFormats(const QString &select);
    void refreshAssignments();
    void refreshContents();
    void refreshLayoutPreview();
    void saveSettings();
    void compileNow();
    void assignLayouts();          // "Assign Section Layouts..." (a window with pictures of the layouts)
    void readReplacements();       // the table of the Replacements tab -> m_settings
    compile::Format currentFormat() const;
    QWidget *buildContentsTab();
    QWidget *buildMetadataTab();
    QWidget *buildOptionsTab();
    QWidget *buildReplacementsTab();

public:
    // the Metadata tab (title, author, cover ... of the compiled file); was a window of its own until 0.20
    void editMetadata() { showTab(MetadataTab); }
    // Compile > Preview: the manuscript as PDF pages in a window
    void preview();
    // the manuscript as it would be compiled now (tests)
    compile::Manuscript manuscript();
    // How a section with this layout begins, as HTML (also the format editor's preview).
    static QString layoutPreviewHtml(const compile::Format &f, const compile::SectionLayout &l, const Project *p);

private:
    Project *m_project;
    QComboBox *m_exporter;
    QListWidget *m_formats;
    QTableWidget *m_assign;
    QComboBox *m_font = nullptr;
    QTextBrowser *m_layoutPreview = nullptr;
    QComboBox *m_group, *m_front, *m_back;
    QCheckBox *m_frontOn = nullptr, *m_backOn = nullptr, *m_objectSheets = nullptr;
    QToolButton *m_objectCategories = nullptr;
    QCheckBox *m_groupText;
    QTreeWidget *m_contents;
    QTabWidget *m_tabs = nullptr;
    QTableWidget *m_replacements = nullptr;
    QHash<QString, QWidget *> m_meta; // the fields of the Metadata tab by object name
    QLabel *m_summary;
    QList<compile::Format> m_formatList;
    CompileProjectSettings m_settings;
    bool m_contentsHooked = false;
};

// Edit Compile Format (FormatEditor.cpp): a list of sections on the left
// (Text, Section Layouts, Assignments, Styles, Page, Footnotes and Comments,
// Replacements, Transformations, Contents and E-book), the settings on the right.
class FormatEditorDialog : public QDialog
{
    Q_OBJECT
public:
    FormatEditorDialog(const compile::Format &f, Project *p, QWidget *parent = nullptr);
    compile::Format format() const { return m_format; }
    void accept() override;
    // the section shown (for tests and for "Edit Styles" from elsewhere)
    void showSection(int index);
    int sectionCount() const;

private:
    compile::Format m_format;
    Project *m_project;
    std::function<void()> m_apply;
    QListWidget *m_sections = nullptr;
    QStackedWidget *m_pages = nullptr;
};

} // namespace qb
