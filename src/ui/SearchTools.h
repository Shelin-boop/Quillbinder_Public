#pragma once
// Project Replace and Find by Formatting (Edit > Find).
#include "core/Project.h"
#include "core/Replace.h"
#include "text/FormatFind.h"

#include <QDialog>
#include <QPointer>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QStackedWidget;
class QToolButton;

namespace qb {

class ProjectReplaceDialog : public QDialog
{
    Q_OBJECT
public:
    ProjectReplaceDialog(Project *project, QWidget *parent = nullptr);
    // The documents "Selected documents only" stands for.
    void setSelection(const QList<BinderItem *> &items);
    ReplaceOptions options() const;
    void setOptions(const ReplaceOptions &o, bool selectionOnly);
    // What Replace All would change; shown in the dialog.
    ReplaceResult count();
    // ask: confirm first and report afterwards with message boxes
    ReplaceResult replaceAll(bool ask = true);
    QString resultText() const;
    static QString describe(const ReplaceResult &r, bool done);

signals:
    void replaced(const qb::ReplaceResult &result);

private:
    void updateButtons();
    QPointer<Project> m_project;
    QStringList m_selection;
    QLineEdit *m_find, *m_with;
    QCheckBox *m_case, *m_whole, *m_regex;
    QCheckBox *m_titles, *m_text, *m_notes, *m_synopses, *m_custom, *m_comments;
    QRadioButton *m_all, *m_selected;
    QCheckBox *m_trash, *m_snapshots;
    QLabel *m_result;
    QPushButton *m_countButton, *m_replaceButton;
};

// Tool window: choose a kind of formatting and step through the places that
// have it. The window only holds the question; the main window does the walking.
class FormatFindWindow : public QDialog
{
    Q_OBJECT
public:
    FormatFindWindow(Project *project, QWidget *parent = nullptr);
    void setProject(Project *project);
    fmtfind::Query query() const;
    void setQuery(const fmtfind::Query &q);
    bool allDocuments() const;
    void setAllDocuments(bool on);
    void setStatus(const QString &text);
    QString status() const;
    void refreshStyles();
    static QString kindName(fmtfind::Kind k);

signals:
    void findRequested(bool backward);

private:
    void kindChanged();
    void setColor(const QColor &c);
    QPointer<Project> m_project;
    QComboBox *m_kind, *m_scope, *m_revision, *m_style;
    QLineEdit *m_containing;
    QStackedWidget *m_stack;
    QCheckBox *m_limitColor;
    QToolButton *m_colorButton;
    QColor m_color = QColor(255, 235, 59);
    QCheckBox *m_bold, *m_italic, *m_underline, *m_strike;
    QLabel *m_status;
};

} // namespace qb
