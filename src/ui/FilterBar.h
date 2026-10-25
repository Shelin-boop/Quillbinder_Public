#pragma once
#include "core/Project.h"
#include "core/ProjectTypes.h"

#include <QPointer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QToolButton;

namespace qb {

// The bar at the top of an editor that filters the corkboard and the
// outliner (Edit > Find > Find while one of them has the focus): a search
// field, options behind the triangle, and "Done".
class FilterBar : public QWidget
{
    Q_OBJECT
public:
    explicit FilterBar(QWidget *parent = nullptr);
    void setProject(Project *p);
    ItemFilter filter() const;
    void setFilter(const ItemFilter &f);
    void focusField();
    bool optionsVisible() const;
    void setOptionsVisible(bool on);
    void setCount(int shown, int total);
    QLineEdit *field() const { return m_edit; }

signals:
    void filterChanged(const qb::ItemFilter &f);
    void done();

private:
    void fillLists();
    void emitChanged();

    QPointer<Project> m_project;
    QToolButton *m_disclosure;
    QLineEdit *m_edit;
    QLabel *m_count;
    QWidget *m_options;
    QCheckBox *m_inTitle, *m_inSynopsis, *m_inKeywords, *m_inText, *m_invert;
    QComboBox *m_label, *m_status, *m_keyword, *m_compile;
    bool m_filling = false;
};

} // namespace qb
