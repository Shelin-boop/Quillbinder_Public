#pragma once
// Format > Font > Show Fonts and Format > Paragraph > Paragraph.

#include "core/ProjectTypes.h"
#include "text/DirectFormat.h"

#include <QDialog>
#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace qb {

// The formatting of the paragraph that holds the cursor in the active editor
// ("Use Current" buttons). The main window installs the provider.
namespace currentFormatting {
void setProvider(std::function<bool(Formatting *)> provider);
bool get(Formatting *f);
} // namespace currentFormatting

// What the font dialog shows and returns.
struct FontChoice {
    direct::CharSpec spec;  // empty members = the project's font, size, color
    bool bold = false, italic = false, underline = false, strike = false;
};

class FontDialog : public QDialog
{
    Q_OBJECT
public:
    // project: the formatting the "default" entries stand for
    FontDialog(const FontChoice &current, const Formatting &project, const QString &sample, QWidget *parent = nullptr);
    FontChoice choice() const;
    // direct::CharMember bits the user changed
    int changedMembers() const;
    bool boldChanged() const { return choice().bold != m_initial.bold; }
    bool italicChanged() const { return choice().italic != m_initial.italic; }
    bool underlineChanged() const { return choice().underline != m_initial.underline; }
    bool strikeChanged() const { return choice().strike != m_initial.strike; }

private:
    void fillTypefaces();
    void updatePreview();
    void filterFamilies(const QString &text);
    void selectFamily(const QString &family);

    FontChoice m_initial;
    Formatting m_project;
    QLineEdit *m_filter;
    QListWidget *m_families, *m_typefaces, *m_sizes;
    QDoubleSpinBox *m_size;
    QCheckBox *m_underline, *m_strike;
    QComboBox *m_caps;
    QSpinBox *m_spacing;
    QPushButton *m_colorButton;
    QColor m_color;
    QLabel *m_preview;
    QString m_sample;
    bool m_filling = false;
};

class ParagraphDialog : public QDialog
{
    Q_OBJECT
public:
    // current: all attributes as shown in the editor
    explicit ParagraphDialog(const direct::ParaSpec &current, QWidget *parent = nullptr);
    // the attributes the user changed (bits)
    direct::ParaSpec result() const;
    // "Reset to Default" was chosen: remove the formatting set by hand
    bool resetRequested() const { return m_reset; }

private:
    void refreshTabs();
    void updateLineUnit();
    direct::ParaSpec values() const;

    direct::ParaSpec m_initial;
    QList<QTextOption::Tab> m_tabs;
    QComboBox *m_align, *m_lineMode, *m_tabType;
    QDoubleSpinBox *m_left, *m_right, *m_first, *m_before, *m_after, *m_line, *m_tabPos;
    QListWidget *m_tabList;
    bool m_reset = false;
};

// Documents > Convert > Text to Default Formatting
class DefaultFormattingDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DefaultFormattingDialog(int documents, QWidget *parent = nullptr);
    bool removeFonts() const;
    int paragraphBits() const; // direct::ParaBit set to remove

private:
    QCheckBox *m_fonts, *m_paragraphs, *m_keepAlign, *m_keepTabs;
};

} // namespace qb
