#pragma once
// Format > Scriptwriting > Script Settings: the elements of the project's
// script format, their look, what Tab and Enter do, and auto-completion.
#include "text/Script.h"

#include <QDialog>
#include <QHash>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QTabWidget;
class QTextEdit;
class QToolButton;

namespace qb {

class ScriptSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ScriptSettingsDialog(const script::Format &format, QWidget *parent = nullptr);

    script::Format format() const { return m_f; }
    // elements that were renamed: name before -> name now
    QHash<QString, QString> renamed() const;

    // for the scripted checks
    void selectElement(const QString &name);
    void addElement();
    void removeElement();
    void moveElement(int delta);
    void useFormat(const script::Format &f);

    void accept() override;

private:
    void rebuildList();
    void fillCombos();
    void load();          // element -> widgets
    void store();         // widgets -> element
    void commitName();
    void updatePreview();
    void fillManageMenu();
    double toUnit(double mm) const;
    double fromUnit(double v) const;
    void applyUnit();
    script::Element *current();

    script::Format m_f;
    QStringList m_orig;   // name of each element when the window opened ("" = new)
    int m_cur = -1;
    bool m_loading = false;

    QLineEdit *m_formatName;
    QFontComboBox *m_font;
    QCheckBox *m_autoFont;
    QDoubleSpinBox *m_fontSize;
    QComboBox *m_unit;
    QListWidget *m_list;
    QTabWidget *m_tabs;
    // element
    QLineEdit *m_name, *m_key;
    QComboBox *m_role;
    QCheckBox *m_first;
    // font
    QCheckBox *m_bold, *m_italic, *m_underline, *m_caps;
    QToolButton *m_color;
    // paragraph
    QComboBox *m_align;
    QDoubleSpinBox *m_left, *m_right, *m_firstLine, *m_before, *m_line;
    QCheckBox *m_keep;
    // keys
    QComboBox *m_enter, *m_tabEmpty, *m_tabEnd;
    QLineEdit *m_tabInserts, *m_prefix, *m_suffix;
    // completion
    QPlainTextEdit *m_completions, *m_tabCompletions;
    QCheckBox *m_fromScript;
    QTextEdit *m_preview;
    QToolButton *m_manage;
};

} // namespace qb
