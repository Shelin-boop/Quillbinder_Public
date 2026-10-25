#pragma once
// File > Print Settings: paper, header and footer, and how texts, index cards
// and the outliner are printed (core/PrintSettings.h).

#include "core/PrintSettings.h"

#include <QDialog>
#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QTabWidget;

namespace qb {

class PrintSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    enum Page { PageTab = 0, TextTab, CardsTab, OutlinerTab };
    PrintSettingsDialog(const PrintSettings &current, const PageSetup &pageView, QWidget *parent = nullptr);
    PrintSettings settings() const;
    void setSettings(const PrintSettings &s);
    bool useForNewProjects() const;
    void showTab(int tab);
    // the Preview button: shows the printout with the settings of the window
    void setPreviewFunction(const std::function<void(const PrintSettings &)> &f);

private:
    void paperChosen();
    void cardSizeChosen();
    void updateEnabled();
    double toUnit(double mm) const;
    double fromUnit(double v) const;

    bool m_metric;
    PageSetup m_pageView;
    QTabWidget *m_tabs;
    // page
    QComboBox *m_paper;
    QDoubleSpinBox *m_width, *m_height, *m_left, *m_right, *m_top, *m_bottom;
    QRadioButton *m_portrait, *m_landscape;
    QLineEdit *m_hl, *m_hc, *m_hr, *m_fl, *m_fc, *m_fr;
    QCheckBox *m_firstPage;
    // text
    QCheckBox *m_titles, *m_synopses, *m_notes, *m_comments, *m_endnotes, *m_highlights;
    QRadioButton *m_editorFont, *m_ownFont;
    QFontComboBox *m_font;
    QDoubleSpinBox *m_fontSize;
    QComboBox *m_lineSpacing;
    // index cards
    QComboBox *m_cardSize;
    QDoubleSpinBox *m_cardW, *m_cardH;
    QRadioButton *m_cardsGrid, *m_cardPerPage;
    QCheckBox *m_cardTitle, *m_cardSynopsis, *m_cardText, *m_cardLabel, *m_cardStatus, *m_cardKeywords, *m_cardPictures,
        *m_cardRuled, *m_cardBorder;
    QFontComboBox *m_cardFont;
    QDoubleSpinBox *m_cardFontSize;
    QLabel *m_cardInfo;
    // outliner
    QComboBox *m_outSynopses;
    QCheckBox *m_outAll, *m_outHeader, *m_outLines, *m_outLabels;
    QFontComboBox *m_outFont;
    QDoubleSpinBox *m_outFontSize;

    QCheckBox *m_newProjects;
    std::function<void(const PrintSettings &)> m_preview;
    QPushButton *m_previewButton = nullptr;
};

} // namespace qb
