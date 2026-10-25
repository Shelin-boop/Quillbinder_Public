#pragma once
// The windows around a mind map (0.17): the Mind Map tab of the inspector
// (MindMapPanel), the large window (MindMapWindow) with Scapple's menus and
// its inspector (Note Format, Note Styles, Document Settings), the find bar,
// the footer and the options.

#include "ui/MindMapController.h"
#include "ui/MindMapDoc.h"

#include <QDialog>
#include <QMainWindow>
#include <QPointer>
#include <QToolButton>
#include <QWidget>
#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSlider;
class QSpinBox;
class QStackedWidget;

namespace qb {

class Project;
class MindMapDoc;
class MindMapView;
class MindMapController;

// A button that shows a color; a click chooses another, its menu takes it away.
class ColorWell : public QToolButton
{
    Q_OBJECT
public:
    explicit ColorWell(const QString &noColorText, QWidget *parent = nullptr);
    void setColor(const QColor &c);
    QColor color() const { return m_color; }
signals:
    void colorChosen(const QColor &c); // invalid: no color

private:
    void refresh();
    QColor m_color;
};

class MindMapFindBar : public QWidget
{
    Q_OBJECT
public:
    explicit MindMapFindBar(MindMapController *ctl, QWidget *parent = nullptr);
    void open();
    void findNext(bool backward);
    void setFindText(const QString &t);
    QLineEdit *findField() const { return m_find; }
    QLineEdit *replaceField() const { return m_replace; }
    int mode() const { return m_mode; }
    void setMode(int m) { m_mode = m; }
    bool ignoreCase() const { return m_ignoreCase; }

private:
    MindMapController *m_ctl;
    QLineEdit *m_find, *m_replace;
    QLabel *m_status;
    int m_mode = 0;          // 0 contains, 1 starts with, 2 whole word, 3 ends with
    bool m_ignoreCase = true;
    bool m_selectedOnly = false;
};

class MindMapOptionsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MindMapOptionsDialog(QWidget *parent = nullptr);
    void accept() override;

private:
    QCheckBox *m_enterEnds, *m_enterCreates, *m_useSelected, *m_dashed, *m_autoSize, *m_shapeMagnetic;
    QSlider *m_faded;
    QLineEdit *m_text;
    QDoubleSpinBox *m_size;
    QSpinBox *m_width;
    QComboBox *m_noteBorder, *m_shapeBorder;
    ColorWell *m_noteFill, *m_shapeFill;
};

// The inspector of the large window: Note Format, Note Styles, Document Settings.
class MindMapFormatPanel : public QWidget
{
    Q_OBJECT
public:
    MindMapFormatPanel(MindMapController *ctl, QWidget *parent = nullptr);
    void refresh();

private:
    void refreshStyles();
    MindMapController *m_ctl;
    bool m_updating = false;
    QToolButton *m_b, *m_i, *m_u, *m_s;
    QDoubleSpinBox *m_size;
    ColorWell *m_textColor, *m_fill, *m_borderColor;
    QComboBox *m_align, *m_thickness, *m_borderStyle;
    QCheckBox *m_faded, *m_shadow, *m_magnetic;
    QListWidget *m_styles;
    ColorWell *m_background, *m_defaultText;
    QLabel *m_backgroundLabel = nullptr;
    QFontComboBox *m_font;
    QComboBox *m_direction;
};

// The footer: number of notes, movement mode, zoom.
class MindMapFooter : public QWidget
{
    Q_OBJECT
public:
    MindMapFooter(MindMapController *ctl, bool compact, QWidget *parent = nullptr);
    void refresh();

private:
    MindMapController *m_ctl;
    QLabel *m_count, *m_move;
    QSlider *m_zoom;
};

// The Mind Map tab of the inspector: the board of the inspector's document.
class MindMapPanel : public QWidget
{
    Q_OBJECT
public:
    explicit MindMapPanel(QWidget *parent = nullptr);
    void setItem(Project *project, const QString &itemId);
    MindMapView *view() const { return m_view; }
    MindMapController *controller() const { return m_ctl; }
    MindMapDoc *doc() const { return m_doc; }
signals:
    void openWindowRequested(qb::MindMapDoc *doc);

private:
    void rebuild();
    QPointer<Project> m_project;
    QString m_itemId;
    QPointer<MindMapDoc> m_doc;
    MindMapView *m_view = nullptr;
    QPointer<MindMapController> m_ctl;
    QWidget *m_bar = nullptr;
    QWidget *m_holder = nullptr;
    MindMapFindBar *m_findBar = nullptr;
    MindMapFooter *m_footer = nullptr;
    QLabel *m_empty = nullptr;
    QStackedWidget *m_stack = nullptr;
};

// The large window of a board (one per board; opened again it comes to the front).
class MindMapWindow : public QMainWindow
{
    Q_OBJECT
public:
    static MindMapWindow *open(MindMapDoc *doc, QWidget *parent);
    static MindMapWindow *existing(MindMapDoc *doc);
    MindMapView *view() const { return m_view; }
    MindMapController *controller() const { return m_ctl; }
    MindMapFormatPanel *formatPanel() const { return m_format; }

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    MindMapWindow(MindMapDoc *doc, QWidget *parent);
    void updateTitle();
    QPointer<MindMapDoc> m_doc;
    MindMapView *m_view = nullptr;
    MindMapController *m_ctl = nullptr;
    MindMapFormatPanel *m_format = nullptr;
    MindMapFindBar *m_findBar = nullptr;
    MindMapFooter *m_footer = nullptr;
    QWidget *m_side = nullptr;
};

} // namespace qb
