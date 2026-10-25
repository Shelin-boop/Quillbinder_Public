#pragma once
// The text color and highlight buttons of the format bar (0.17). The button
// shows the color it applies (a bar under the symbol); a click applies it to
// the selected text, or to the text typed next. Resting the mouse on the
// button opens a small row of colors below it (red, blue, yellow, green,
// white, black, plus "none" and "other"); a click there chooses the color and
// applies it at once.

#include <QColor>
#include <QFrame>
#include <QPointer>
#include <QTimer>
#include <QToolButton>

namespace qb {

class ColorPickPopup;

class ColorPickButton : public QToolButton
{
    Q_OBJECT
public:
    enum Kind { TextColor, Highlight };
    ColorPickButton(Kind kind, QWidget *parent = nullptr);
    Kind kind() const { return m_kind; }
    QColor color() const { return m_color; }
    void setColor(const QColor &c);         // the button's color (not applied)
    ColorPickPopup *popup() const { return m_popup; }
    void showPopup();
    void hidePopup();
    static QList<QPair<QString, QColor>> palette();

signals:
    void applyColor(const QColor &c);       // invalid: automatic text color / no highlight
    void colorChanged(const QColor &c);

protected:
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    void refreshIcon();
    Kind m_kind;
    QColor m_color;
    QPointer<ColorPickPopup> m_popup;
    QTimer m_showTimer, m_hideTimer;
};

class ColorPickPopup : public QFrame
{
    Q_OBJECT
public:
    explicit ColorPickPopup(ColorPickButton *owner);
    QList<QToolButton *> swatches() const { return m_swatches; }

signals:
    void chosen(const QColor &c);
    void otherRequested();

protected:
    void leaveEvent(QEvent *e) override;
    void enterEvent(QEnterEvent *e) override;

private:
    ColorPickButton *m_owner;
    QList<QToolButton *> m_swatches;
};

} // namespace qb
