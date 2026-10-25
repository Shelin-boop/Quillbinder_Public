#pragma once
// Format > Style: New / Redefine Style From Selection, the style editor and
// the Styles Panel.

#include "core/ProjectTypes.h"
#include "text/Styles.h"

#include <QDialog>
#include <QTextBlockFormat>
#include <QTextCharFormat>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QRadioButton;
class QTextEdit;
class QToolButton;

namespace qb {

namespace styleui {
// "Font: Noto Sans 14 pt, bold · Paragraph: centered, space before 12 pt"
QString describe(const Style &s);
// Show sample text in the look of a style.
void fillPreview(QTextEdit *view, const Style &s, const Formatting &f, const QString &sample = QString());
// Font for a style's row in lists and menus.
QFont rowFont(const Style &s, const QFont &base);
} // namespace styleui

// New Style From Selection / Redefine Style From Selection
class StyleFromSelectionDialog : public QDialog
{
    Q_OBJECT
public:
    enum Mode { New, Redefine };
    // bf / cf: formatting of the paragraph and the text at the selection.
    // preferred: style to redefine (the one at the cursor).
    StyleFromSelectionDialog(Mode mode, const Formatting &f, const QTextBlockFormat &bf, const QTextCharFormat &cf,
                             const QString &preferred, QWidget *parent = nullptr);
    Style style() const;          // complete style (New: new name; Redefine: name and role of the old one)

    void accept() override;

private:
    void kindChanged();
    void targetChanged();
    styles::CaptureOptions options() const;

    Mode m_mode;
    Formatting m_f;
    QTextBlockFormat m_bf;
    QTextCharFormat m_cf;
    QLineEdit *m_name = nullptr;
    QComboBox *m_target = nullptr;
    QRadioButton *m_all, *m_paraOnly, *m_charOnly;
    QCheckBox *m_family, *m_size;
    QComboBox *m_next;
    QLabel *m_nextLabel;
    QLabel *m_summary;
    QTextEdit *m_preview;
};

// Edit Style: name, font, paragraph formatting, following style
class StyleEditDialog : public QDialog
{
    Q_OBJECT
public:
    StyleEditDialog(const Style &style, const Formatting &f, QWidget *parent = nullptr);
    Style style() const { return m_style; }
    void accept() override;
    // After Apply: the style as the project has it now (a renamed style is known
    // under its new name from here on).
    void applied(const Style &s, const Formatting &f);
    QString originalName() const { return m_originalName; }

signals:
    // Apply: style() holds the new definition; the dialog stays open.
    void applyRequested();

private:
    bool takeInput();
    void editFont();
    void editParagraph();
    void refresh();

    Style m_style;
    QString m_originalName;
    Formatting m_f;
    QLineEdit *m_name;
    QComboBox *m_next = nullptr;
    QCheckBox *m_keep = nullptr;
    QLabel *m_summary;
    QTextEdit *m_preview;
};

// The Styles Panel: a small window that stays open next to the text.
class StylesPanel : public QDialog
{
    Q_OBJECT
public:
    explicit StylesPanel(QWidget *parent = nullptr);
    // Fill the list; paragraph / character: the styles at the cursor.
    void setStyles(const Formatting &f, bool hasProject);
    void setCurrent(const QString &paragraph, const QString &character);
    QString selectedStyle() const;       // empty = "No Style" row or nothing

signals:
    void applyRequested(const QString &name);
    void newRequested();
    void redefineRequested(const QString &name);
    void editRequested(const QString &name);
    void deleteRequested(const QString &name);
    void resetRequested(const QString &name);
    void moveRequested(const QString &name, int delta);
    void importRequested();
    void saveAsDefaultRequested();

private:
    void updateButtons();
    void showMenu(const QPoint &pos);

    QListWidget *m_list;
    QPushButton *m_apply;
    QToolButton *m_new, *m_redefine, *m_edit, *m_delete, *m_up, *m_down, *m_more;
    Formatting m_f;
    QString m_curPara, m_curChar;
    bool m_hasProject = false;
};

} // namespace qb
