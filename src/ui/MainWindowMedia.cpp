// Audio and video: Navigate > Media, Insert > Media Timestamp (MediaPlayback.h).
#include "ui/MainWindow.h"

#include "core/ItemIcons.h"
#include "media/MediaApi.h"
#include "ui/AppSettings.h"
#include "ui/EditorPane.h"
#include "ui/MediaPlayback.h"
#include "ui/MediaView.h"
#include "ui/TextEditor.h"

#include <QMenu>
#include <QTextBlock>
#include <QTextCursor>
#include <QToolTip>

namespace qb {

void MainWindow::createMediaActions()
{
    auto act = [this](const QString &key, const QString &text, const QString &icon = QString(),
                      const QKeySequence &ks = QKeySequence()) {
        auto *ac = new QAction(text, this);
        ac->setObjectName(key);
        QString plain = text;
        plain.remove(QLatin1Char('&'));
        ac->setIconText(plain);
        if (!icon.isEmpty())
            ac->setIcon(icons::action(icon));
        if (!ks.isEmpty())
            ac->setShortcut(ks);
        a.insert(key, ac);
        addAction(ac);
        m_mediaActions << ac;
        return ac;
    };
    using playback::hub;
    auto onTarget = [](auto fn) {
        return [fn] {
            if (MediaView *v = playback::hub().target())
                fn(v);
        };
    };
    connect(act(QStringLiteral("mediaPlayPause"), tr("&Play / Pause"), QStringLiteral("media-playback-start"),
                QKeySequence(Qt::CTRL | Qt::Key_Return)),
            &QAction::triggered, this, onTarget([](MediaView *v) { v->playPause(); }));
    connect(act(QStringLiteral("mediaStop"), tr("&Stop"), QStringLiteral("media-playback-stop")), &QAction::triggered, this,
            onTarget([](MediaView *v) { v->stopPlayback(); }));
    connect(act(QStringLiteral("mediaStart"), tr("Go to the S&tart"), QStringLiteral("media-skip-backward")),
            &QAction::triggered, this, onTarget([](MediaView *v) { v->seek(0); }));
    connect(act(QStringLiteral("mediaBack"), tr("Skip &Back"), QStringLiteral("media-seek-backward"),
                QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Comma)),
            &QAction::triggered, this, onTarget([](MediaView *v) { v->skip(-AppSettings::instance().mediaSkipSeconds); }));
    connect(act(QStringLiteral("mediaForward"), tr("Skip &Forward"), QStringLiteral("media-seek-forward"),
                QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Period)),
            &QAction::triggered, this, onTarget([](MediaView *v) { v->skip(AppSettings::instance().mediaSkipSeconds); }));
    QAction *rewind = act(QStringLiteral("mediaPauseRewind"), tr("Pause and &Rewind"), QStringLiteral("chronometer-reset"));
    rewind->setCheckable(true);
    connect(rewind, &QAction::triggered, this, [](bool on) {
        AppSettings::instance().mediaPauseRewind = on;
        AppSettings::instance().save();
        playback::hub().notifySettings();
    });
    auto stepRate = [](MediaView *v, int dir) {
        static const double steps[] = {0.5, 0.75, 0.9, 1.0, 1.1, 1.25, 1.5, 2.0};
        const double r = v->rate();
        double next = r;
        if (dir > 0) {
            for (double s : steps)
                if (s > r + 0.001) {
                    next = s;
                    break;
                }
        } else {
            for (double s : steps)
                if (s < r - 0.001)
                    next = s;
        }
        v->setRate(next);
    };
    connect(act(QStringLiteral("mediaFaster"), tr("F&aster")), &QAction::triggered, this,
            onTarget([stepRate](MediaView *v) { stepRate(v, 1); }));
    connect(act(QStringLiteral("mediaSlower"), tr("S&lower")), &QAction::triggered, this,
            onTarget([stepRate](MediaView *v) { stepRate(v, -1); }));
    connect(act(QStringLiteral("mediaNormalSpeed"), tr("&Normal Speed")), &QAction::triggered, this,
            onTarget([](MediaView *v) { v->setRate(1.0); }));
    auto setVolume = [](double v) {
        AppSettings &s = AppSettings::instance();
        s.mediaVolume = qBound(0.0, v, 1.0);
        s.save();
        playback::hub().notifySettings();
    };
    connect(act(QStringLiteral("mediaVolumeUp"), tr("Volume &Up")), &QAction::triggered, this,
            [setVolume] { setVolume(AppSettings::instance().mediaVolume + 0.1); });
    connect(act(QStringLiteral("mediaVolumeDown"), tr("Volume &Down")), &QAction::triggered, this,
            [setVolume] { setVolume(AppSettings::instance().mediaVolume - 0.1); });
    QAction *mute = act(QStringLiteral("mediaMute"), tr("&Mute"), QStringLiteral("player-volume-muted"));
    mute->setCheckable(true);
    connect(mute, &QAction::triggered, this, [this, setVolume](bool on) {
        if (on) {
            m_mediaVolumeBeforeMute = AppSettings::instance().mediaVolume > 0 ? AppSettings::instance().mediaVolume : 1.0;
            setVolume(0);
        } else {
            setVolume(m_mediaVolumeBeforeMute > 0 ? m_mediaVolumeBeforeMute : 1.0);
        }
    });
    connect(act(QStringLiteral("mediaTimestamp"), tr("Insert &Timestamp"), QStringLiteral("chronometer-lap"),
                QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Return)),
            &QAction::triggered, this, [this] { insertMediaTimestamp(); });
    connect(act(QStringLiteral("mediaGoToTimestamp"), tr("&Go to Timestamp at Cursor")), &QAction::triggered, this,
            [this] { goToMediaTimestamp(); });

    connect(&playback::hub(), &playback::Hub::changed, this, &MainWindow::updateMediaActions);
    connect(&playback::hub(), &playback::Hub::settingsChanged, this, &MainWindow::updateMediaActions);
    updateMediaActions();
}

void MainWindow::createMediaMenu(QMenu *nav)
{
    QMenu *m = nav->addMenu(icons::action(QStringLiteral("media-playback-start")), tr("&Media"));
    m->setObjectName(QStringLiteral("mediaMenu"));
    for (const char *k : {"mediaPlayPause", "mediaStop", "mediaStart", "mediaBack", "mediaForward", "", "mediaPauseRewind", "",
                          "mediaFaster", "mediaSlower", "mediaNormalSpeed", "", "mediaVolumeUp", "mediaVolumeDown", "mediaMute", "",
                          "mediaTimestamp", "mediaGoToTimestamp"}) {
        if (!*k)
            m->addSeparator();
        else
            m->addAction(a.value(QLatin1String(k)));
    }
    // without the add-on: say so, and where to get it
    m_mediaMissing = new QAction(tr("Audio and video need the add-on \"Media Playback\""), m);
    m_mediaMissing->setEnabled(false);
    m->addSeparator();
    m->addAction(m_mediaMissing);
    connect(m, &QMenu::aboutToShow, this, [this] {
        m_mediaMissing->setVisible(!playback::engine());
        updateMediaActions();
    });
}

void MainWindow::updateMediaActions()
{
    if (m_mediaActions.isEmpty())
        return;
    const AppSettings &s = AppSettings::instance();
    MediaView *v = playback::hub().target();
    const bool playable = v && v->isPlayable();
    const bool engine = playback::engine() != nullptr;
    for (const char *k : {"mediaPlayPause", "mediaStop", "mediaStart", "mediaBack", "mediaForward", "mediaFaster", "mediaSlower",
                          "mediaNormalSpeed", "mediaGoToTimestamp"})
        a[QLatin1String(k)]->setEnabled(playable);
    a[QStringLiteral("mediaTimestamp")]->setEnabled(playable);
    for (const char *k : {"mediaPauseRewind", "mediaVolumeUp", "mediaVolumeDown", "mediaMute"})
        a[QLatin1String(k)]->setEnabled(engine);
    const bool playing = v && v->isPlaying();
    a[QStringLiteral("mediaPlayPause")]->setIcon(
        icons::action(playing ? QStringLiteral("media-playback-pause") : QStringLiteral("media-playback-start")));
    a[QStringLiteral("mediaPauseRewind")]->setChecked(s.mediaPauseRewind);
    a[QStringLiteral("mediaMute")]->setChecked(s.mediaVolume <= 0);
    a[QStringLiteral("mediaBack")]->setText(tr("Skip &Back %1 Seconds").arg(s.mediaSkipSeconds));
    a[QStringLiteral("mediaForward")]->setText(tr("Skip &Forward %1 Seconds").arg(s.mediaSkipSeconds));
}

void MainWindow::insertMediaTimestamp()
{
    MediaView *v = playback::hub().target();
    TextEditor *e = editor();
    if (!v || !v->isPlayable() || !e || e->isReadOnly())
        return;
    const QString stamp = playback::formatTimestamp(v->position(), v->duration() >= 3600000);
    QTextCursor c = e->textCursor();
    c.beginEditBlock();
    // a space between the stamp and the text before it
    if (c.positionInBlock() > 0 && !c.block().text().at(c.positionInBlock() - 1).isSpace())
        c.insertText(QStringLiteral(" "));
    c.insertText(stamp + QLatin1Char(' '));
    c.endEditBlock();
    e->setTextCursor(c);
}

void MainWindow::goToMediaTimestamp()
{
    MediaView *v = playback::hub().target();
    TextEditor *e = editor();
    if (!v || !v->isPlayable() || !e)
        return;
    const QTextCursor c = e->textCursor();
    const qint64 ms = playback::parseTimestamp(c.block().text(), c.positionInBlock());
    if (ms < 0) {
        QToolTip::showText(e->viewport()->mapToGlobal(e->cursorRect().bottomLeft()),
                           tr("No timestamp (such as [03:07]) in the line of the cursor."), e);
        return;
    }
    v->seek(ms);
}

} // namespace qb
