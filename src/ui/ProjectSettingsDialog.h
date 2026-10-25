#pragma once
#include "core/Project.h"

#include <QDialog>

class QListWidget;
class QStackedWidget;
class QTableWidget;
class QLineEdit;
class QFontComboBox;
class QDoubleSpinBox;
class QCheckBox;
class QRadioButton;
class QComboBox;
class QTreeWidget;
class QTextEdit;

namespace qb {

class ProjectSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    enum Page { Labels = 0, Statuses, SectionTypes, Formatting, CustomMetadata, SpecialFolders, General };
    ProjectSettingsDialog(Project *p, int page, QWidget *parent = nullptr);
    void accept() override;
    // Takes over the settings without closing (the Apply button).
    void apply();

private:
    QWidget *labelsPage();
    QWidget *statusPage();
    QWidget *sectionPage();
    QWidget *formattingPage();
    QWidget *customPage();
    QWidget *foldersPage();
    QWidget *generalPage();
    void refreshStructure();
    void updatePreview();

    Project *m_project;
    QListWidget *m_pages;
    QStackedWidget *m_stack;
    // copies
    QList<Label> m_labels;
    QList<Status> m_statuses;
    QList<SectionType> m_types;
    StructureDefaults m_structure;
    QList<CustomField> m_fields;
    // widgets
    QLineEdit *m_labelTitle, *m_statusTitle;
    QTableWidget *m_labelTable;
    QListWidget *m_statusList;
    QListWidget *m_typeList;
    QTreeWidget *m_structureTree;
    QFontComboBox *m_font;
    QCheckBox *m_autoFont;
    QDoubleSpinBox *m_size, *m_spacing, *m_indent, *m_paraSpacing;
    QCheckBox *m_justify;
    QCheckBox *m_ownFollows = nullptr;
    QRadioButton *m_md, *m_rtf;
    QTextEdit *m_preview;
    QTableWidget *m_fieldTable;
    QComboBox *m_templatesFolder, *m_notesFolder;
    QCheckBox *m_notesAsk = nullptr;
    QLineEdit *m_compBackground;
    QLineEdit *m_title, *m_titleOverride, *m_authorOverride;
};

} // namespace qb
