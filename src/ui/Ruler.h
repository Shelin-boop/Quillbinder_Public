#pragma once
// The ruler above the text: indents and tab stops of the paragraph that
// holds the cursor. Markers can be dragged; a click on a free place adds a
// tab stop; a tab stop dragged off the ruler is removed.

#include "text/DirectFormat.h"

#include <QPointer>
#include <QWidget>

namespace qb {

class TextEditor;

class Ruler : public QWidget
{
    Q_OBJECT
public:
    explicit Ruler(QWidget *parent = nullptr);
    void setEditor(TextEditor *editor);
    TextEditor *editor() const { return m_editor; }

    // geometry in ruler coordinates (also used by the UI tests)
    int originX() const;                 // left edge of the text
    int textWidth() const;               // width of the text column
    enum Marker { None, FirstLine, LeftIndent, RightIndent, Tab };
    int markerX(Marker m, int tabIndex = -1) const;

    QSize sizeHint() const override;

signals:
    void paragraphDialogRequested();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    direct::ParaSpec spec() const;
    Marker hit(const QPoint &p, int *tabIndex) const;
    double snap(double px, bool free) const;
    void apply(const direct::ParaSpec &s);

    QPointer<TextEditor> m_editor;
    Marker m_drag = None;
    int m_dragTab = -1;
    double m_dragValue = 0;   // px in paragraph coordinates while dragging
    bool m_dragRemove = false;
    bool m_moved = false;
    QPoint m_pressPos;
};

} // namespace qb
