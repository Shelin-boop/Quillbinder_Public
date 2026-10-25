#pragma once
// File > Compare and Transfer (0.18): the window that shows what differs
// between the project (as Compile makes it) and a Word or LibreOffice file,
// chapter by chapter or change by change, and takes the chosen side over.

#include "import/Compare.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

namespace qb {

class Project;

class CompareDialog : public QDialog
{
    Q_OBJECT
public:
    CompareDialog(Project *p, compare::Result result, QWidget *parent = nullptr);

    compare::Result &result() { return m_result; }
    compare::Direction direction() const;
    void setDirection(compare::Direction d);
    void setByChapter(bool on);
    void setStepByStep(bool on);
    bool stepByStep() const;
    QTreeWidget *chapterList() const { return m_list; }
    QTextBrowser *view() const { return m_view; }
    // step by step: the change shown (pair, change), and the decisions
    QPair<int, int> currentStep() const { return {m_stepPair, m_stepChange}; }
    void take(bool yes);                 // Take / Skip of the current step
    int acceptedCount() const;
    // the transfer (also what the Transfer button does); the report for the window after it
    bool transfer(compare::Report *report, QString *error);
    QString outputPath() const;          // into the file: where the copy goes

signals:
    void transferred(const compare::Report &report);

private:
    void fillList();
    void updateRow(int pair);
    void showPair(int pair);
    void showStep();
    void nextStep(int delta);
    void updateMode();
    void updateButtons();
    QString changeHtml(const compare::Pair &pr, int ci, bool withContext) const;
    QString pairHtml(int pair) const;
    QString allHtml() const;
    QString rowSummary(const compare::Pair &pr) const;

    Project *m_project;
    compare::Result m_result;
    QLabel *m_info = nullptr;
    QRadioButton *m_intoProject = nullptr;
    QRadioButton *m_intoFile = nullptr;
    QCheckBox *m_byChapter = nullptr;
    QCheckBox *m_stepBox = nullptr;
    QTreeWidget *m_list = nullptr;
    QTextBrowser *m_view = nullptr;
    QWidget *m_stepBar = nullptr;
    QLabel *m_stepLabel = nullptr;
    QPushButton *m_takeBtn = nullptr, *m_skipBtn = nullptr, *m_backBtn = nullptr;
    QPushButton *m_transferBtn = nullptr;
    int m_shownPair = -1;
    int m_stepPair = -1, m_stepChange = -1;
    bool m_filling = false;
};

} // namespace qb
