#pragma once
#include "core/Project.h"
#include "ui/QuickRef.h" // Quick Reference panels (their own file since 0.8)

#include <QPointer>
#include <QTimer>
#include <QWidget>

class QComboBox;
class QSlider;
class QLabel;
class QToolButton;
class QLineEdit;
class QPlainTextEdit;

namespace qb {

class TextColumnView;
class TextEditor;
class MediaView;

// Distraction free full screen writing.
class CompositionWindow : public QWidget
{
    Q_OBJECT
public:
    CompositionWindow(Project *p, const QList<BinderItem *> &items, bool combined, BinderItem *focusItem, int cursorPos);
    ~CompositionWindow() override;
    void showItems(const QList<BinderItem *> &items, bool combined);
    TextColumnView *view() const { return m_view; }

signals:
    void closed(qb::BinderItem *item, int cursorPos);
    void navigate(bool next);
    // Ctrl+[ / Ctrl+] (or whatever Back and Forward are set to): through the document history
    void historyRequested(bool back);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void closeEvent(QCloseEvent *e) override;

private:
    void updateLayout();
    void updateInspector();
    QPointer<Project> m_project;
    TextColumnView *m_view;
    QWidget *m_bar;
    QWidget *m_side;
    QLineEdit *m_sideTitle;
    QPlainTextEdit *m_sideSynopsis;
    TextEditor *m_sideNotes;
    QTimer m_hover;
    QPixmap m_background;
    double m_oldZoom = 1.0;
    bool m_pagesSuspended = false;
    QSlider *m_width, *m_fade;
    QComboBox *m_zoom;
    QLabel *m_count;
};

} // namespace qb
