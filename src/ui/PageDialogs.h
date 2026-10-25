#pragma once
// View > Text Editing > Page Size and Margins: the paper of the page view.

#include "core/PageSetup.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QRadioButton;

namespace qb {

class PageSetupDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PageSetupDialog(const PageSetup &current, QWidget *parent = nullptr);
    PageSetup setup() const;
    void setSetup(const PageSetup &s);
    bool useForNewProjects() const;

private:
    void paperChosen();
    void updatePreview();
    double toUnit(double mm) const;
    double fromUnit(double v) const;

    bool m_metric;
    QComboBox *m_paper;
    QDoubleSpinBox *m_width, *m_height, *m_left, *m_right, *m_top, *m_bottom;
    QRadioButton *m_portrait, *m_landscape;
    QCheckBox *m_newProjects;
    QLabel *m_preview, *m_info;
};

} // namespace qb
