#include "ScreenGrab.h"

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

ScreenGrab::ScreenGrab(QObject *parent) : QObject(parent) {}

ScreenGrab::~ScreenGrab()
{
    delete m_tmp;
}

QImage ScreenGrab::grabScreen()
{
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen || QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return QImage();
    const QImage img = screen->grabWindow(0).toImage();
    if (img.isNull())
        return img;
    // a screen that could not be read comes back black
    bool allBlack = true;
    for (int y = 0; y < img.height() && allBlack; y += qMax(1, img.height() / 20))
        for (int x = 0; x < img.width(); x += qMax(1, img.width() / 20))
            if (qGray(img.pixel(x, y)) > 2) {
                allBlack = false;
                break;
            }
    return allBlack ? QImage() : img;
}

void ScreenGrab::start(bool preferSpectacle)
{
    const QString spectacle = QStandardPaths::findExecutable(QStringLiteral("spectacle"));
    if (!preferSpectacle || spectacle.isEmpty() || qEnvironmentVariableIsSet("QB_NO_SPECTACLE")) {
        startQt();
        return;
    }
    m_tmp = new QTemporaryDir;
    const QString out = m_tmp->filePath(QStringLiteral("shot.png"));
    m_proc = new QProcess(this);
    // background mode, region, no notification, output file
    m_proc->start(spectacle, {QStringLiteral("-b"), QStringLiteral("-r"), QStringLiteral("-n"), QStringLiteral("-o"), out});
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            startQt();
    });
    connect(m_proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, out] {
        const QImage img(out);
        // nothing chosen (Esc) gives no file
        emit finished(img, QString());
    });
}

void ScreenGrab::startQt()
{
    const QImage screen = grabScreen();
    if (screen.isNull()) {
        emit finished(QImage(), tr("This desktop does not let programs read the screen. On KDE Plasma, install "
                                   "Spectacle (sudo apt install kde-spectacle); it takes the picture for the scratch pad."));
        return;
    }
    auto *picker = new RegionPicker(screen);
    connect(picker, &RegionPicker::picked, this, [this](const QImage &img) { emit finished(img, QString()); });
    picker->showFullScreen();
    picker->raise();
    picker->activateWindow();
}

// ------------------------------------------------------------------ region picker

RegionPicker::RegionPicker(const QImage &screen) : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), m_screen(screen)
{
    setObjectName(QStringLiteral("regionPicker"));
    setAttribute(Qt::WA_DeleteOnClose);
    setCursor(Qt::CrossCursor);
    setWindowTitle(tr("Choose a part of the screen"));
}

void RegionPicker::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.drawImage(rect(), m_screen);
    p.fillRect(rect(), QColor(0, 0, 0, 90));
    const QRect sel = QRect(m_from, m_to).normalized();
    if (m_dragging && sel.isValid()) {
        const double sx = double(m_screen.width()) / qMax(1, width()), sy = double(m_screen.height()) / qMax(1, height());
        p.drawImage(sel, m_screen, QRect(int(sel.x() * sx), int(sel.y() * sy), int(sel.width() * sx), int(sel.height() * sy)));
        p.setPen(QPen(QColor(61, 174, 233), 2));
        p.drawRect(sel);
    }
    p.setPen(Qt::white);
    p.drawText(rect().adjusted(0, 20, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
               tr("Drag over the part of the screen you want. Esc cancels, Enter takes the whole screen."));
}

void RegionPicker::mousePressEvent(QMouseEvent *e)
{
    m_from = m_to = e->position().toPoint();
    m_dragging = true;
    update();
}

void RegionPicker::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging) {
        m_to = e->position().toPoint();
        update();
    }
}

void RegionPicker::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_dragging || m_done)
        return;
    m_to = e->position().toPoint();
    m_dragging = false;
    const QRect sel = QRect(m_from, m_to).normalized();
    if (sel.width() < 4 || sel.height() < 4) {
        update();
        return;
    }
    const double sx = double(m_screen.width()) / qMax(1, width()), sy = double(m_screen.height()) / qMax(1, height());
    m_done = true;
    emit picked(m_screen.copy(QRect(int(sel.x() * sx), int(sel.y() * sy), int(sel.width() * sx), int(sel.height() * sy))));
    close();
}

void RegionPicker::keyPressEvent(QKeyEvent *e)
{
    if (m_done)
        return;
    if (e->key() == Qt::Key_Escape) {
        m_done = true;
        emit picked(QImage());
        close();
    } else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        m_done = true;
        emit picked(m_screen);
        close();
    }
}
