#pragma once
// Theme windows: a small picture of a theme, the theme list (use, copy,
// rename, delete, import, export) and the questions asked when a theme is
// chosen or saved.

#include "ui/Theme.h"

#include <QDialog>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

namespace qb {

// A miniature of the main window in the colors of a theme.
class ThemePreview : public QWidget
{
    Q_OBJECT
public:
    explicit ThemePreview(QWidget *parent = nullptr);
    void setColors(const QMap<theme::Role, QColor> &colors);
    void setFonts(const QMap<theme::FontRole, theme::FontSpec> &fonts);
    QSize sizeHint() const override { return QSize(300, 170); }
    QSize minimumSizeHint() const override { return QSize(220, 130); }

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    QMap<theme::Role, QColor> m_colors;
    QMap<theme::FontRole, theme::FontSpec> m_fonts;
};

namespace themeui {

// Makes a theme the theme in use. When the writer has colors or fonts of
// their own on top of the current theme, asks whether to keep them
// (ask = false: keeps them). Returns false when cancelled.
bool choose(QWidget *parent, const QString &id, bool ask = true);

// Asks for a name and saves theme + own changes as a new theme, which
// becomes the theme in use. Returns the new id or an empty string.
QString saveCurrentLook(QWidget *parent);
// The same without questions (also used by the tests).
QString saveLookAs(const theme::Theme &look, const QString &name, QString *error);

// File dialog + import. Returns the new id or an empty string.
QString importFromFile(QWidget *parent);

// A name that no other theme uses; empty when cancelled.
QString askName(QWidget *parent, const QString &title, const QString &suggestion, const QString &exceptId = QString());

} // namespace themeui

class ThemeManagerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ThemeManagerDialog(QWidget *parent = nullptr);
    void select(const QString &id);
    QString selectedId() const;

    // the buttons, without their questions
    QString duplicate(const QString &id, const QString &name, QString *error);
    void reload(const QString &selectId = QString());

private:
    void updateButtons();
    QListWidget *m_list;
    ThemePreview *m_preview;
    QLabel *m_info;
    QPushButton *m_use, *m_duplicate, *m_rename, *m_delete, *m_export;
};

} // namespace qb
