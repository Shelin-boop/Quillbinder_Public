#include "ui/MediaView.h"
#include "ui/ViewMemory.h"
#include <QTimer>
#include "core/BinderItem.h"
#include "core/ItemIcons.h"
#include "core/Project.h"
#include "media/MediaApi.h"
#include "ui/AppSettings.h"
#include "ui/MediaPlayback.h"

#include <QDesktopServices>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace qb {

// Passes what the engine's player reports on as signals of the view.
class MediaView::Listener : public media::PlayerListener
{
public:
    explicit Listener(MediaView *v) : m_view(v) {}
    void playerStateChanged() override
    {
        MediaView *v = m_view;
        if (v->m_restore >= 0 && v->m_player && v->m_player->status() == media::Status::Loaded
            && v->m_player->duration() > 0) {
            const qint64 at = v->m_restore;
            v->m_restore = -1;
            if (at < v->m_player->duration())
                v->m_player->setPosition(at);
        }
        v->refreshPlayerPage();
        emit v->playbackChanged();
        playback::hub().notifyChanged();
    }
    void playerPositionChanged(qint64 ms) override { emit m_view->positionChanged(ms); }
    void playerDurationChanged(qint64) override
    {
        playerStateChanged();
        emit m_view->positionChanged(m_view->position());
        emit m_view->infoChanged();
    }
    void playerMetaDataChanged() override { m_view->refreshPlayerPage(); }

private:
    MediaView *m_view;
};

MediaView::MediaView(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    m_stack = new QStackedWidget(this);
    lay->addWidget(m_stack);

    m_imageArea = new QScrollArea(m_stack);
    m_imageArea->setAlignment(Qt::AlignCenter);
    m_imageArea->setFrameShape(QFrame::NoFrame);
    m_imageArea->setBackgroundRole(QPalette::Dark);
    m_image = new QLabel;
    m_image->setAlignment(Qt::AlignCenter);
    m_imageArea->setWidget(m_image);
    m_imageArea->setWidgetResizable(false);
    m_stack->addWidget(m_imageArea);

    m_pdf = new QPdfDocument(this);
    m_pdfView = new QPdfView(m_stack);
    m_pdfView->setDocument(m_pdf);
    m_pdfView->setPageMode(QPdfView::PageMode::MultiPage);
    m_pdfView->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    m_stack->addWidget(m_pdfView);
    connect(m_pdfView->pageNavigator(), &QPdfPageNavigator::currentPageChanged, this, &MediaView::infoChanged);
    // the page is kept with the project (0.21.1), not while a file is being opened
    connect(m_pdfView->pageNavigator(), &QPdfPageNavigator::currentPageChanged, this, [this] {
        if (!m_restoringPlace)
            rememberMediaPlace();
    });

    m_html = new QTextBrowser(m_stack);
    m_html->setOpenExternalLinks(true);
    m_stack->addWidget(m_html);

    m_other = new QWidget(m_stack);
    auto *ol = new QVBoxLayout(m_other);
    ol->addStretch(1);
    m_otherLabel = new QLabel(m_other);
    m_otherLabel->setAlignment(Qt::AlignCenter);
    m_otherLabel->setWordWrap(true);
    ol->addWidget(m_otherLabel);
    m_openButton = new QPushButton(tr("Open with Default Application"), m_other);
    ol->addWidget(m_openButton, 0, Qt::AlignHCenter);
    m_otherHint = new QLabel(m_other);
    m_otherHint->setObjectName(QStringLiteral("mediaHint"));
    m_otherHint->setAlignment(Qt::AlignCenter);
    m_otherHint->setWordWrap(true);
    m_otherHint->setEnabled(false);
    m_otherHint->hide();
    ol->addSpacing(12);
    ol->addWidget(m_otherHint);
    ol->addStretch(2);
    m_stack->addWidget(m_other);
    auto openExternally = [this] {
        if (m_project && m_item)
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_project->mediaFilePath(m_item)));
    };
    connect(m_openButton, &QPushButton::clicked, this, openExternally);

    // audio and video
    m_playerPage = new QWidget(m_stack);
    m_playerPage->setObjectName(QStringLiteral("mediaPlayerPage"));
    m_playerPage->setFocusPolicy(Qt::ClickFocus);
    m_playerPage->installEventFilter(this);
    auto *pl = new QVBoxLayout(m_playerPage);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->setSpacing(0);
    m_playerStack = new QStackedWidget(m_playerPage);
    auto *videoHolder = new QWidget(m_playerStack);
    auto *vl = new QVBoxLayout(videoHolder);
    vl->setContentsMargins(0, 0, 0, 0);
    m_playerStack->addWidget(videoHolder);
    m_audioCard = new QWidget(m_playerStack);
    m_audioCard->installEventFilter(this);
    auto *al = new QVBoxLayout(m_audioCard);
    al->addStretch(1);
    m_audioIcon = new QLabel(m_audioCard);
    m_audioIcon->setAlignment(Qt::AlignCenter);
    al->addWidget(m_audioIcon);
    m_audioTitle = new QLabel(m_audioCard);
    m_audioTitle->setObjectName(QStringLiteral("mediaTitle"));
    m_audioTitle->setAlignment(Qt::AlignCenter);
    m_audioTitle->setWordWrap(true);
    QFont tf = m_audioTitle->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.3);
    tf.setBold(true);
    m_audioTitle->setFont(tf);
    al->addWidget(m_audioTitle);
    m_audioInfo = new QLabel(m_audioCard);
    m_audioInfo->setObjectName(QStringLiteral("mediaInfo"));
    m_audioInfo->setAlignment(Qt::AlignCenter);
    m_audioInfo->setWordWrap(true);
    m_audioInfo->setEnabled(false);
    al->addWidget(m_audioInfo);
    m_playerMessage = new QLabel(m_audioCard);
    m_playerMessage->setObjectName(QStringLiteral("mediaError"));
    m_playerMessage->setAlignment(Qt::AlignCenter);
    m_playerMessage->setWordWrap(true);
    m_playerMessage->hide();
    al->addSpacing(8);
    al->addWidget(m_playerMessage);
    m_playerOpen = new QPushButton(tr("Open with Default Application"), m_audioCard);
    m_playerOpen->hide();
    al->addWidget(m_playerOpen, 0, Qt::AlignHCenter);
    connect(m_playerOpen, &QPushButton::clicked, this, openExternally);
    al->addStretch(2);
    m_playerStack->addWidget(m_audioCard);
    pl->addWidget(m_playerStack, 1);
    m_stack->addWidget(m_playerPage);
    m_playerPage->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_playerPage, &QWidget::customContextMenuRequested, this, [this, openExternally](const QPoint &pos) {
        QMenu menu(this);
        QAction *pp = menu.addAction(isPlaying() ? tr("Pause") : tr("Play"), this, &MediaView::playPause);
        pp->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
        pp->setEnabled(isPlayable());
        menu.addAction(tr("Go to the Start"), this, [this] { seek(0); })->setEnabled(isPlayable());
        menu.addSeparator();
        menu.addAction(tr("Open with Default Application"), this, openExternally);
        menu.exec(m_playerPage->mapToGlobal(pos));
    });

    connect(&playback::hub(), &playback::Hub::settingsChanged, this, [this] {
        if (m_player)
            applyVolume();
    });
}

MediaView::~MediaView()
{
    blockSignals(true); // the editor around this view may be half gone already
    release();
    // the player owns its video widget, which is a child of this view for now
    delete m_player;
    m_player = nullptr;
    delete m_listener;
}

void MediaView::setProject(Project *p)
{
    if (m_project != p) {
        rememberMediaPlace();
        release();
        // the item belonged to the other project (0.40: it was kept, and when that project had been
        // closed and deleted, the next save of the window's places wrote through the freed item:
        // the rare crash on closing a project, "corrupted double-linked list")
        m_item = nullptr;
    }
    m_project = p;
}

void MediaView::showItem(BinderItem *item)
{
    if (item != m_item)
        rememberMediaPlace();
    if (item != m_item || (item && item->id != m_playerItemId))
        release();
    m_item = item;
    if (!m_project || !item)
        return;
    const QString path = m_project->mediaFilePath(item);
    const QString ext = item->fileExt.toLower();
    const bool av = item->type == ItemType::Media || (item->type == ItemType::File && mediaSuffixes().contains(ext));
    switch (item->type) {
    case ItemType::Image:
        m_pixmap = QPixmap(path);
        m_zoom = 0;
        restoreMediaPlace();
        updateImage();
        m_stack->setCurrentWidget(m_imageArea);
        break;
    case ItemType::Pdf:
        m_restoringPlace = true;
        m_pdf->load(path);
        m_pdfView->setZoomMode(QPdfView::ZoomMode::FitToWidth);
        m_stack->setCurrentWidget(m_pdfView);
        restoreMediaPlace();
        break;
    case ItemType::WebPage: {
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        m_html->setSearchPaths({QFileInfo(path).absolutePath()});
        m_html->setHtml(QString::fromUtf8(f.readAll()));
        m_stack->setCurrentWidget(m_html);
        break;
    }
    default:
        if (av)
            showPlayer(item, path);
        else
            showOther(item, path);
        break;
    }
    emit infoChanged();
    emit playbackChanged();
}

void MediaView::showOther(BinderItem *item, const QString &path)
{
    const QFileInfo fi(path);
    m_otherLabel->setText(QStringLiteral("<p><img src=\"\"/></p><h3>%1</h3><p>%2</p>")
                              .arg(item->title.toHtmlEscaped(),
                                   fi.exists() ? tr("%1 file, %2").arg(item->fileExt.toUpper(), QLocale().formattedDataSize(fi.size()))
                                               : tr("The file is missing.")));
    m_openButton->setEnabled(fi.exists());
    m_otherHint->hide();
    m_stack->setCurrentWidget(m_other);
}

void MediaView::showPlayer(BinderItem *item, const QString &path)
{
    QString why;
    if (!playback::start(&why) || !playback::canPlay(item->fileExt)) {
        showOther(item, path);
        if (why.isEmpty())
            why = tr("This kind of file is not played inside the program.");
        m_otherHint->setText(why);
        m_otherHint->show();
        return;
    }
    if (m_player && m_playerItemId == item->id && m_player->source() == path) {
        // shown again (the editor's view was refreshed): keep playing
        refreshPlayerPage();
        m_stack->setCurrentWidget(m_playerPage);
        return;
    }
    if (!m_player) {
        m_player = playback::engine()->createPlayer();
        if (!m_player) {
            showOther(item, path);
            return;
        }
        m_listener = new Listener(this);
        m_player->setListener(m_listener);
        QWidget *video = m_player->videoWidget();
        video->setParent(m_playerStack->widget(0));
        m_playerStack->widget(0)->layout()->addWidget(video);
        video->installEventFilter(this);
        video->setCursor(Qt::PointingHandCursor);
    }
    m_playerItemId = item->id;
    m_restore = -1;
    if (m_project && AppSettings::instance().mediaRememberPosition)
        m_restore = m_project->uiState.value(QStringLiteral("mediaPositions")).toMap().value(item->id, -1).toLongLong();
    if (m_restore <= 0)
        m_restore = -1;
    // the page first: the file may be loaded (and reported) inside setSource
    m_stack->setCurrentWidget(m_playerPage);
    applyVolume();
    m_player->setRate(1.0);
    m_player->setSource(path);
    playback::hub().add(this);
    playback::hub().touch(this); // the file just opened is the one Navigate > Media acts on
    refreshPlayerPage();
    playback::hub().notifyChanged();
}

void MediaView::refreshPlayerPage()
{
    if (!m_player || !m_item)
        return;
    const bool video = m_player->hasVideo() && m_player->errorString().isEmpty();
    m_playerStack->setCurrentIndex(video ? 0 : 1);
    if (video)
        return;
    const QVariantMap md = m_player->metaData();
    const QString icon = isAudioSuffix(m_item->fileExt) ? QStringLiteral("audio-x-generic") : QStringLiteral("video-x-generic");
    m_audioIcon->setPixmap(icons::get(icon).pixmap(96, 96));
    m_audioTitle->setText(md.value(QStringLiteral("title")).toString().isEmpty() ? m_item->title
                                                                                  : md.value(QStringLiteral("title")).toString());
    QStringList info;
    for (const char *k : {"artist", "album", "date"}) {
        const QString v = md.value(QString::fromLatin1(k)).toString();
        if (!v.isEmpty())
            info << v;
    }
    if (m_player->duration() > 0)
        info << playback::formatTime(m_player->duration());
    info << m_item->fileExt.toUpper();
    m_audioInfo->setText(info.join(QStringLiteral(" · ")));
    const QString err = m_player->errorString();
    m_playerMessage->setVisible(!err.isEmpty());
    m_playerOpen->setVisible(!err.isEmpty());
    if (!err.isEmpty())
        m_playerMessage->setText(tr("This file cannot be played here: %1").arg(err));
}

bool MediaView::isPlayer() const
{
    return m_player && !m_playerItemId.isEmpty() && m_stack->currentWidget() == m_playerPage;
}

bool MediaView::isPlayable() const
{
    return isPlayer() && m_player->errorString().isEmpty() && m_player->status() != media::Status::NoMedia;
}

bool MediaView::isPlaying() const
{
    return isPlayer() && m_player->state() == media::State::Playing;
}

bool MediaView::hasVideo() const
{
    return isPlayer() && m_player->hasVideo();
}

void MediaView::play()
{
    if (!isPlayable())
        return;
    playback::hub().touch(this);
    m_player->play();
}

void MediaView::pause(bool rewind)
{
    if (!isPlayer())
        return;
    playback::hub().touch(this);
    const bool wasPlaying = m_player->state() == media::State::Playing;
    m_player->pause();
    const AppSettings &s = AppSettings::instance();
    if (rewind && wasPlaying && s.mediaPauseRewind)
        seek(m_player->position() - qint64(s.mediaRewindSeconds * 1000));
    rememberPosition();
}

void MediaView::playPause()
{
    if (isPlaying())
        pause(true);
    else
        play();
}

void MediaView::stopPlayback()
{
    if (!isPlayer())
        return;
    playback::hub().touch(this);
    m_player->stop();
    m_player->setPosition(0);
}

void MediaView::skip(double seconds)
{
    if (!isPlayable())
        return;
    seek(m_player->position() + qint64(seconds * 1000));
}

void MediaView::seek(qint64 ms)
{
    if (!isPlayable())
        return;
    m_restore = -1; // the writer chose a place: the remembered one is not wanted
    playback::hub().touch(this);
    const qint64 d = m_player->duration();
    m_player->setPosition(qBound<qint64>(0, ms, d > 0 ? d : qMax<qint64>(0, ms)));
    emit positionChanged(m_player->position());
}

qint64 MediaView::position() const
{
    return isPlayer() ? m_player->position() : 0;
}

qint64 MediaView::duration() const
{
    return isPlayer() ? m_player->duration() : 0;
}

double MediaView::rate() const
{
    return isPlayer() ? m_player->rate() : 1.0;
}

void MediaView::setRate(double rate)
{
    if (!isPlayer())
        return;
    playback::hub().touch(this);
    m_player->setRate(qBound(0.25, rate, 4.0));
    emit playbackChanged();
    playback::hub().notifyChanged();
}

void MediaView::applyVolume()
{
    if (m_player)
        m_player->setVolume(AppSettings::instance().mediaVolume);
}

QString MediaView::playerError() const
{
    return isPlayer() ? m_player->errorString() : QString();
}

void MediaView::rememberPosition()
{
    if (!m_project || !m_player || m_playerItemId.isEmpty() || !AppSettings::instance().mediaRememberPosition)
        return;
    QVariantMap map = m_project->uiState.value(QStringLiteral("mediaPositions")).toMap();
    const qint64 pos = m_player->position(), dur = m_player->duration();
    // the start and the very end are not worth remembering
    if (pos < 1000 || (dur > 0 && pos > dur - 1000) || !m_player->errorString().isEmpty())
        map.remove(m_playerItemId);
    else
        map.insert(m_playerItemId, pos);
    m_project->uiState[QStringLiteral("mediaPositions")] = map;
}

void MediaView::release()
{
    if (!m_player || m_playerItemId.isEmpty())
        return;
    rememberPosition();
    m_player->stop();
    m_player->setSource(QString());
    m_playerItemId.clear();
    m_restore = -1;
    playback::hub().remove(this);
    emit playbackChanged();
}

void MediaView::setOwnControls(bool on)
{
    if (on && !m_ownControls) {
        m_ownControls = new playback::MediaControls(m_playerPage);
        m_ownControls->setView(this);
        m_playerPage->layout()->addWidget(m_ownControls);
    }
    if (m_ownControls)
        m_ownControls->setVisible(on);
}

bool MediaView::eventFilter(QObject *o, QEvent *e)
{
    const bool picture = m_player && o == m_player->videoWidget();
    if ((picture || o == m_audioCard) && e->type() == QEvent::MouseButtonRelease
        && static_cast<QMouseEvent *>(e)->button() == Qt::LeftButton) {
        m_playerPage->setFocus();
        if (picture)
            playPause();
        return false;
    }
    if (o == m_playerPage && e->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(e);
        if (ke->modifiers() == Qt::NoModifier) {
            switch (ke->key()) {
            case Qt::Key_Space: playPause(); return true;
            case Qt::Key_Left: skip(-AppSettings::instance().mediaSkipSeconds); return true;
            case Qt::Key_Right: skip(AppSettings::instance().mediaSkipSeconds); return true;
            case Qt::Key_Home: seek(0); return true;
            default: break;
            }
        }
    }
    return QWidget::eventFilter(o, e);
}

void MediaView::updateImage()
{
    if (m_pixmap.isNull()) {
        m_image->setText(tr("The image could not be loaded."));
        m_image->adjustSize();
        return;
    }
    QPixmap pm;
    if (m_zoom <= 0) {
        const QSize avail = m_imageArea->viewport()->size() - QSize(20, 20);
        pm = m_pixmap.width() > avail.width() || m_pixmap.height() > avail.height()
                 ? m_pixmap.scaled(avail, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                 : m_pixmap;
    } else {
        pm = m_pixmap.scaled(m_pixmap.size() * m_zoom, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    m_image->setPixmap(pm);
    m_image->resize(pm.size());
}

void MediaView::zoomBy(double factor)
{
    if (m_stack->currentWidget() == m_pdfView) {
        m_pdfView->setZoomMode(QPdfView::ZoomMode::Custom);
        m_pdfView->setZoomFactor(qBound(0.2, m_pdfView->zoomFactor() * factor, 6.0));
        rememberMediaPlace();
        return;
    }
    if (m_pixmap.isNull())
        return;
    if (m_zoom <= 0)
        m_zoom = double(m_image->width()) / m_pixmap.width();
    m_zoom = qBound(0.05, m_zoom * factor, 8.0);
    updateImage();
    rememberMediaPlace();
}

void MediaView::fitToWindow()
{
    if (m_stack->currentWidget() == m_pdfView) {
        m_pdfView->setZoomMode(QPdfView::ZoomMode::FitToWidth);
        rememberMediaPlace();
        return;
    }
    m_zoom = 0;
    updateImage();
    rememberMediaPlace();
}

void MediaView::rememberMediaPlace()
{
    if (!m_project || !m_item || (m_item->type != ItemType::Image && m_item->type != ItemType::Pdf))
        return;
    viewmem::MediaPlace m;
    if (m_item->type == ItemType::Image) {
        m.zoom = qMax(0.0, m_zoom);
    } else {
        m.pdfFitWidth = m_pdfView->zoomMode() == QPdfView::ZoomMode::FitToWidth;
        m.zoom = m.pdfFitWidth ? 0.0 : m_pdfView->zoomFactor();
        m.page = m_pdfView->pageNavigator()->currentPage();
    }
    viewmem::setMediaPlace(m_project, m_item->id, m);
}

void MediaView::restoreMediaPlace()
{
    if (!m_project || !m_item) {
        m_restoringPlace = false;
        return;
    }
    const viewmem::MediaPlace m = viewmem::mediaPlace(m_project, m_item->id);
    if (m.zoom < 0) {
        QTimer::singleShot(0, this, [this] { m_restoringPlace = false; });
        return;
    }
    if (m_item->type == ItemType::Image) {
        m_zoom = m.zoom; // 0: fit to the window
        return;
    }
    if (m_item->type != ItemType::Pdf)
        return;
    if (m.zoom > 0) {
        m_pdfView->setZoomMode(QPdfView::ZoomMode::Custom);
        m_pdfView->setZoomFactor(qBound(0.2, m.zoom, 6.0));
    }
    const int page = m.page;
    const QString id = m_item->id;
    // the page once the view has laid the file out
    QTimer::singleShot(0, this, [this, page, id] {
        if (m_item && m_item->id == id && page > 0 && page < m_pdf->pageCount())
            m_pdfView->pageNavigator()->jump(page, QPointF(), m_pdfView->pageNavigator()->currentZoom());
        m_restoringPlace = false;
    });
}

void MediaView::pdfPage(int delta)
{
    auto *nav = m_pdfView->pageNavigator();
    const int page = qBound(0, nav->currentPage() + delta, qMax(0, m_pdf->pageCount() - 1));
    nav->jump(page, QPointF(), nav->currentZoom());
    rememberMediaPlace();
}

QString MediaView::footerInfo() const
{
    if (!m_item)
        return QString();
    if (m_item->type == ItemType::Pdf)
        return tr("Page %1 of %2").arg(m_pdfView->pageNavigator()->currentPage() + 1).arg(m_pdf->pageCount());
    if (m_item->type == ItemType::Image && !m_pixmap.isNull())
        return tr("%1 × %2 px").arg(m_pixmap.width()).arg(m_pixmap.height());
    return m_item->fileExt.toUpper();
}

void MediaView::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    if (m_zoom <= 0 && m_stack->currentWidget() == m_imageArea)
        updateImage();
}

} // namespace qb
