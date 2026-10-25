#include "ui/MediaPlayback.h"

#include "compile/PluginManager.h"
#include "core/ItemIcons.h"
#include "core/ProjectTypes.h"
#include "media/MediaApi.h"
#include "ui/AppSettings.h"
#include "ui/MediaView.h"

#include <QActionGroup>
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QRegularExpression>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QWidgetAction>

namespace qb {
namespace playback {

namespace {
bool s_tried = false;
bool s_started = false;
QString s_error;
} // namespace

media::EngineInterface *engine()
{
    compile::PluginManager &pm = compile::PluginManager::instance();
    pm.load();
    return pm.mediaEngine();
}

bool start(QString *error)
{
    media::EngineInterface *e = engine();
    if (!e) {
        if (error)
            *error = QApplication::translate("playback",
                                             "Playing audio and video inside the program needs the add-on \"Media Playback\" "
                                             "(File > Install Update or Add-on; Help > Add-on Manager).");
        return false;
    }
    if (!s_tried) {
        s_tried = true;
        s_started = e->start(AppSettings::instance().mediaBackend, &s_error);
    }
    if (!s_started && error)
        *error = QApplication::translate("playback", "Audio and video cannot be played here: %1").arg(s_error);
    return s_started;
}

QString status()
{
    media::EngineInterface *e = engine();
    if (!e)
        return QApplication::translate("playback", "Not installed: audio and video files open with the desktop's default application.");
    QString s = QApplication::translate("playback", "%1 %2").arg(e->name(), e->version());
    if (e->isStarted())
        s += QStringLiteral(": ") + e->description();
    else if (s_tried)
        s += QStringLiteral(": ") + s_error;
    else
        s += QApplication::translate("playback", ": starts with the first audio or video file.");
    return s;
}

bool canPlay(const QString &suffix)
{
    media::EngineInterface *e = engine();
    return e && e->fileSuffixes().contains(suffix.toLower());
}

QString formatTime(qint64 ms, bool hours)
{
    const qint64 s = qMax<qint64>(0, ms) / 1000;
    if (hours || s >= 3600)
        return QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString formatTimestamp(qint64 ms, bool hours)
{
    const qint64 s = qMax<qint64>(0, ms) / 1000;
    if (hours || s >= 3600)
        return QStringLiteral("[%1]").arg(formatTime(ms, true));
    return QStringLiteral("[%1:%2]").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
}

qint64 parseTimestamp(const QString &line, int pos)
{
    static const QRegularExpression re(QStringLiteral("(?<!\\d)(?:(\\d{1,2}):)?(\\d{1,2}):(\\d{2})(?:[.,](\\d{1,3}))?(?!\\d)"));
    qint64 best = -1;
    auto it = re.globalMatch(line);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (m.capturedStart() > pos && best >= 0)
            break; // the nearest one before or around the cursor
        const int h = m.captured(1).toInt(), mi = m.captured(2).toInt(), se = m.captured(3).toInt();
        if (se > 59 || (m.capturedLength(1) && mi > 59))
            continue;
        QString frac = m.captured(4);
        const int msPart = frac.isEmpty() ? 0 : frac.leftJustified(3, QLatin1Char('0')).toInt();
        best = ((h * 60 + mi) * 60 + se) * 1000LL + msPart;
        if (m.capturedStart() > pos)
            break;
    }
    return best;
}

// ------------------------------------------------------------------ hub

Hub &hub()
{
    static Hub h;
    return h;
}

void Hub::add(MediaView *v)
{
    for (const QPointer<MediaView> &p : m_views)
        if (p == v)
            return;
    m_views << v;
    emit changed();
}

void Hub::remove(MediaView *v)
{
    m_views.removeIf([v](const QPointer<MediaView> &p) { return !p || p == v; });
    if (m_last == v)
        m_last = nullptr;
    emit changed();
}

void Hub::touch(MediaView *v)
{
    if (m_last == v)
        return;
    m_last = v;
    emit changed();
}

QList<MediaView *> Hub::views() const
{
    QList<MediaView *> out;
    for (const QPointer<MediaView> &p : m_views)
        if (p && p->isPlayer())
            out << p;
    return out;
}

MediaView *Hub::target() const
{
    if (m_last && m_last->isPlayer())
        return m_last;
    const QList<MediaView *> all = views();
    for (MediaView *v : all)
        if (v->isPlaying())
            return v;
    // prefer one in a visible window
    for (MediaView *v : all)
        if (v->isVisible())
            return v;
    return all.isEmpty() ? nullptr : all.first();
}

// ------------------------------------------------------------------ controls

namespace {
QToolButton *button(QWidget *parent, const QString &icon, const QString &tip, const QString &name)
{
    auto *b = new QToolButton(parent);
    b->setIcon(icons::action(icon));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setObjectName(name);
    b->setIconSize(QSize(16, 16));
    return b;
}
const double kRates[] = {0.5, 0.75, 0.9, 1.0, 1.1, 1.25, 1.5, 2.0};
QString rateText(double r)
{
    return QStringLiteral("%1×").arg(QLocale().toString(r, 'g', 3));
}
} // namespace

MediaControls::MediaControls(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("mediaControls"));
    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(4, 1, 4, 1);
    l->setSpacing(2);
    m_rewind = button(this, QStringLiteral("chronometer-reset"), QString(), QStringLiteral("mediaPauseRewind"));
    m_rewind->setCheckable(true);
    m_start = button(this, QStringLiteral("media-skip-backward"), tr("Go to the start"), QStringLiteral("mediaStartButton"));
    m_back = button(this, QStringLiteral("media-seek-backward"), QString(), QStringLiteral("mediaBackButton"));
    m_play = button(this, QStringLiteral("media-playback-start"), tr("Play (Ctrl+Return)"), QStringLiteral("mediaPlayButton"));
    m_forward = button(this, QStringLiteral("media-seek-forward"), QString(), QStringLiteral("mediaForwardButton"));
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setObjectName(QStringLiteral("mediaPosition"));
    m_slider->setRange(0, 0);
    m_slider->setSingleStep(1000);
    m_slider->setPageStep(10000);
    m_slider->setFocusPolicy(Qt::NoFocus);
    m_slider->setMinimumWidth(80);
    m_time = new QLabel(this);
    m_time->setObjectName(QStringLiteral("mediaTime"));
    m_time->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_speed = new QToolButton(this);
    m_speed->setObjectName(QStringLiteral("mediaSpeed"));
    m_speed->setAutoRaise(true);
    m_speed->setPopupMode(QToolButton::InstantPopup);
    m_speed->setToolTip(tr("Playing speed"));
    auto *speedMenu = new QMenu(m_speed);
    auto *rates = new QActionGroup(speedMenu);
    for (double r : kRates) {
        QAction *a = speedMenu->addAction(rateText(r));
        a->setCheckable(true);
        a->setData(r);
        rates->addAction(a);
    }
    m_speed->setMenu(speedMenu);
    connect(speedMenu, &QMenu::aboutToShow, this, [this, speedMenu] {
        const double r = m_view ? m_view->rate() : 1.0;
        for (QAction *a : speedMenu->actions())
            a->setChecked(qAbs(a->data().toDouble() - r) < 0.001);
    });
    connect(speedMenu, &QMenu::triggered, this, [this](QAction *a) {
        if (m_view)
            m_view->setRate(a->data().toDouble());
    });
    m_volume = button(this, QStringLiteral("player-volume"), tr("Volume (click: mute)"), QStringLiteral("mediaVolume"));
    m_volume->setPopupMode(QToolButton::MenuButtonPopup);
    auto *volMenu = new QMenu(m_volume);
    auto *wa = new QWidgetAction(volMenu);
    m_volumeSlider = new QSlider(Qt::Vertical);
    m_volumeSlider->setObjectName(QStringLiteral("mediaVolumeSlider"));
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setMinimumHeight(110);
    wa->setDefaultWidget(m_volumeSlider);
    volMenu->addAction(wa);
    m_volume->setMenu(volMenu);
    connect(m_volumeSlider, &QSlider::valueChanged, this, [](int v) {
        AppSettings &s = AppSettings::instance();
        if (qAbs(s.mediaVolume * 100 - v) < 0.5)
            return;
        s.mediaVolume = v / 100.0;
        s.save();
        hub().notifySettings();
    });
    connect(m_volume, &QToolButton::clicked, this, [] {
        // mute = volume 0; a second click gives the volume back
        static double before = 1.0;
        AppSettings &s = AppSettings::instance();
        if (s.mediaVolume > 0) {
            before = s.mediaVolume;
            s.mediaVolume = 0;
        } else {
            s.mediaVolume = before > 0 ? before : 1.0;
        }
        s.save();
        hub().notifySettings();
    });

    l->addWidget(m_rewind);
    l->addSpacing(6);
    l->addWidget(m_start);
    l->addWidget(m_back);
    l->addWidget(m_play);
    l->addWidget(m_forward);
    l->addSpacing(4);
    l->addWidget(m_slider, 1);
    l->addWidget(m_time);
    l->addWidget(m_speed);
    l->addWidget(m_volume);

    connect(m_rewind, &QToolButton::toggled, this, [](bool on) {
        AppSettings &s = AppSettings::instance();
        if (s.mediaPauseRewind == on)
            return;
        s.mediaPauseRewind = on;
        s.save();
        hub().notifySettings();
    });
    connect(m_start, &QToolButton::clicked, this, [this] {
        if (m_view)
            m_view->seek(0);
    });
    connect(m_back, &QToolButton::clicked, this, [this] {
        if (m_view)
            m_view->skip(-AppSettings::instance().mediaSkipSeconds);
    });
    connect(m_forward, &QToolButton::clicked, this, [this] {
        if (m_view)
            m_view->skip(AppSettings::instance().mediaSkipSeconds);
    });
    connect(m_play, &QToolButton::clicked, this, [this] {
        if (m_view)
            m_view->playPause();
    });
    connect(m_slider, &QSlider::sliderPressed, this, [this] { m_dragging = true; });
    connect(m_slider, &QSlider::sliderReleased, this, [this] {
        m_dragging = false;
        if (m_view)
            m_view->seek(m_slider->value());
    });
    connect(m_slider, &QSlider::sliderMoved, this, [this](int v) {
        m_time->setText(formatTime(v, m_slider->maximum() >= 3600000) + QStringLiteral(" / ")
                        + formatTime(m_slider->maximum(), m_slider->maximum() >= 3600000));
    });
    // clicks beside the handle (page steps) and the keyboard
    connect(m_slider, &QSlider::actionTriggered, this, [this](int action) {
        if (m_dragging || action == QAbstractSlider::SliderMove || !m_view)
            return;
        QMetaObject::invokeMethod(this, [this] {
            if (m_view)
                m_view->seek(m_slider->value());
        }, Qt::QueuedConnection);
    });
    connect(&hub(), &Hub::settingsChanged, this, &MediaControls::refreshSettings);
    refreshSettings();
    refresh();
}

MediaView *MediaControls::view() const
{
    return m_view;
}

void MediaControls::setView(MediaView *v)
{
    if (m_view == v)
        return;
    if (m_view)
        disconnect(m_view, nullptr, this, nullptr);
    m_view = v;
    if (v) {
        connect(v, &MediaView::playbackChanged, this, &MediaControls::refresh);
        connect(v, &MediaView::positionChanged, this, &MediaControls::refreshPosition);
    }
    refresh();
}

void MediaControls::refreshSettings()
{
    const AppSettings &s = AppSettings::instance();
    const QSignalBlocker b1(m_rewind), b2(m_volumeSlider);
    m_rewind->setChecked(s.mediaPauseRewind);
    m_rewind->setToolTip(tr("Pause and rewind: pausing goes back %1 seconds (Options > Behaviors > Playback)")
                             .arg(QLocale().toString(s.mediaRewindSeconds, 'g', 3)));
    m_back->setToolTip(tr("Skip back %1 seconds (Ctrl+Alt+,)").arg(s.mediaSkipSeconds));
    m_forward->setToolTip(tr("Skip forward %1 seconds (Ctrl+Alt+.)").arg(s.mediaSkipSeconds));
    m_volumeSlider->setValue(qRound(s.mediaVolume * 100));
    m_volume->setIcon(icons::action(s.mediaVolume > 0 ? QStringLiteral("player-volume") : QStringLiteral("player-volume-muted")));
}

void MediaControls::refresh()
{
    const bool ok = m_view && m_view->isPlayable();
    for (QWidget *w : {static_cast<QWidget *>(m_start), static_cast<QWidget *>(m_back), static_cast<QWidget *>(m_play),
                       static_cast<QWidget *>(m_forward), static_cast<QWidget *>(m_slider), static_cast<QWidget *>(m_speed)})
        w->setEnabled(ok);
    const bool playing = m_view && m_view->isPlaying();
    m_play->setIcon(icons::action(playing ? QStringLiteral("media-playback-pause") : QStringLiteral("media-playback-start")));
    m_play->setToolTip(playing ? tr("Pause (Ctrl+Return)") : tr("Play (Ctrl+Return)"));
    m_speed->setText(rateText(m_view ? m_view->rate() : 1.0));
    refreshPosition();
}

void MediaControls::refreshPosition()
{
    if (!m_view || !m_view->isPlayer()) {
        m_slider->setRange(0, 0);
        m_time->setText(QString());
        return;
    }
    const qint64 d = m_view->duration();
    const bool hours = d >= 3600000;
    if (m_slider->maximum() != int(d))
        m_slider->setRange(0, int(qMin<qint64>(d, INT_MAX)));
    // room for the longest time of this file, so the label does not jump while playing
    static const QRegularExpression digit(QStringLiteral("\\d"));
    const QString widest = formatTime(d, hours).replace(digit, QStringLiteral("0"));
    const int need = m_time->fontMetrics().horizontalAdvance(widest + QStringLiteral(" / ") + widest) + 6;
    if (m_time->minimumWidth() != need)
        m_time->setMinimumWidth(need);
    if (!m_dragging) {
        const QSignalBlocker b(m_slider);
        m_slider->setValue(int(m_view->position()));
        m_time->setText(formatTime(m_view->position(), hours) + QStringLiteral(" / ") + formatTime(d, hours));
    }
}

} // namespace playback
} // namespace qb
