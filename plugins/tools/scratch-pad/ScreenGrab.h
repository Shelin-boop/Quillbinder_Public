#pragma once
// Screenshot of a part of the screen for the scratch pad's camera button.
//
// KDE: Spectacle in region mode (works under Wayland, where a program may not
// read the screen itself). Elsewhere: the screen is read with Qt (X11) and a
// rectangle is chosen on a full-screen picture of it.
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QWidget>

class QProcess;
class QTemporaryDir;

class ScreenGrab : public QObject
{
    Q_OBJECT
public:
    explicit ScreenGrab(QObject *parent = nullptr);
    ~ScreenGrab() override;
    // Starts; finished() follows (a null image when cancelled or impossible; error says why).
    void start(bool preferSpectacle = true);

    // Full screen with Qt; null if the platform does not allow it (Wayland).
    static QImage grabScreen();

signals:
    void finished(const QImage &image, const QString &error);

private:
    void startQt();
    QProcess *m_proc = nullptr;
    QTemporaryDir *m_tmp = nullptr;
};

// Full-screen picture on which a rectangle is drawn with the mouse.
class RegionPicker : public QWidget
{
    Q_OBJECT
public:
    explicit RegionPicker(const QImage &screen);

signals:
    void picked(const QImage &part);   // null image: cancelled

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    QImage m_screen;
    QPoint m_from, m_to;
    bool m_dragging = false;
    bool m_done = false;
};
