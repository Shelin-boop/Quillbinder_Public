#pragma once
#include "ui/DragDrop.h"
#include "core/Project.h"

#include <QDialog>
#include <QPointer>

class QProgressBar;
class QLabel;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QDateEdit;
class QTreeWidget;
class QLineEdit;

namespace qb {

// Project ▸ Project Targets (floating window, updates live)
class TargetsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TargetsDialog(Project *p, QWidget *parent = nullptr);
    void refresh();

private:
    QPointer<Project> m_project;
    QProgressBar *m_draftBar, *m_sessionBar;
    QLabel *m_draftLabel, *m_sessionLabel;
    QSpinBox *m_draftTarget, *m_sessionTarget;
    QComboBox *m_draftType, *m_sessionType, *m_reset;
    QDateEdit *m_deadline;
    QCheckBox *m_useDeadline, *m_auto, *m_anywhere;
    QList<QCheckBox *> m_days;
    bool m_loading = false;
    void store();
};

// Project ▸ Project Keywords
class KeywordsDialog : public QDialog
{
    Q_OBJECT
public:
    KeywordsDialog(Project *p, QWidget *parent = nullptr);
    void setSelectionProvider(std::function<QList<BinderItem *>()> f) { m_selection = f; }

signals:
    void searchRequested(const QString &keyword);

private:
    void refresh();
    Project *m_project;
    QTreeWidget *m_tree;
    std::function<QList<BinderItem *>()> m_selection;
};

// Project ▸ Writing History
class HistoryDialog : public QDialog
{
    Q_OBJECT
public:
    HistoryDialog(Project *p, QWidget *parent = nullptr);
};

// Project ▸ Statistics
class StatisticsDialog : public QDialog
{
    Q_OBJECT
public:
    StatisticsDialog(Project *p, QWidget *parent = nullptr);
};

// File ▸ Import ▸ Import and Split
class ImportSplitDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ImportSplitDialog(QWidget *parent = nullptr);
    QString file() const;
    QString separator() const;
    bool firstLineIsTitle() const;

private:
    QLineEdit *m_file, *m_sep;
    QCheckBox *m_title;
    QComboBox *m_mode;
};

// File ▸ Export ▸ Files
bool exportItems(Project *p, const QList<BinderItem *> &items, QWidget *parent);

// File > Export > Files: text format, metadata, notes, comments, snapshots, location.
class ExportFilesDialog : public QDialog
{
    Q_OBJECT
public:
    ExportFilesDialog(Project *p, const QList<BinderItem *> &items, QWidget *parent = nullptr);
    dnd::ExportOptions options() const;
    // Exports (after OK) and remembers the choices; returns the folder written to.
    QString run(QStringList *made = nullptr);

private:
    Project *m_project;
    QList<BinderItem *> m_items;
    QComboBox *m_format;
    QCheckBox *m_metadata, *m_synopsis, *m_children, *m_notes, *m_comments, *m_snapshots;
    QLineEdit *m_location, *m_folderName;
};

// Scrivener import with progress
std::unique_ptr<Project> runScrivenerImport(QWidget *parent, const QString &scrivPath = QString());

// Help window showing the bundled manual
void showManual(QWidget *parent, const QString &anchor = QString());
// Help > Quick Start Guide (0.40): docs/quick-start.md in a window of its own
void showQuickStartGuide(QWidget *parent);

} // namespace qb
