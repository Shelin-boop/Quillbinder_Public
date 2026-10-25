#pragma once
#include "core/Project.h"
#include <QPointer>
#include <QWidget>

class QLabel;
class QScrollArea;
class QStackedWidget;
class QTextBrowser;
class QPdfDocument;
class QPdfView;
class QPushButton;

namespace qb {

namespace media {
class Player;
}
namespace playback {
class MediaControls;
}

// Shows research files: images, PDF, web pages; audio and video in a player
// when the add-on "Media Playback" is installed (see MediaPlayback.h); other
// files can be opened with the desktop's default application.
class MediaView : public QWidget
{
    Q_OBJECT
public:
    explicit MediaView(QWidget *parent = nullptr);
    ~MediaView() override;
    void setProject(Project *p);
    void showItem(BinderItem *item);
    BinderItem *item() const { return m_item; }
    Project *project() const { return m_project; }
    void zoomBy(double factor);
    void fitToWindow();
    QString footerInfo() const;
    void pdfPage(int delta);

    // ---- audio and video
    // Shows a file in the player (also while it cannot be played: the reason)
    bool isPlayer() const;
    // A file is loaded and can be played
    bool isPlayable() const;
    bool isPlaying() const;
    bool hasVideo() const;
    void play();
    // `rewind`: the writer paused (pause and rewind applies)
    void pause(bool rewind = true);
    void playPause();
    void stopPlayback();
    void skip(double seconds);
    void seek(qint64 ms);
    qint64 position() const;
    qint64 duration() const;
    double rate() const;
    void setRate(double rate);
    void applyVolume();               // takes the volume of the settings
    QString playerError() const;
    // Stops, remembers the position (with the project) and unloads. Called
    // when the view is given something else to show.
    void release();
    // Copyholder and Quick Reference panel: the play controls inside the view
    // (an editor has them in its footer bar).
    void setOwnControls(bool on);

signals:
    void infoChanged();
    void playbackChanged();           // loaded / unloaded, state, rate, error
    void positionChanged(qint64 ms);

protected:
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    class Listener;
    friend class Listener;
    void updateImage();
    void showPlayer(BinderItem *item, const QString &path);
    void showOther(BinderItem *item, const QString &path);
    void refreshPlayerPage();
    void rememberPosition();
    // zoom of a picture, zoom and page of a PDF file, kept with the project (0.21.1)
    void restoreMediaPlace();
public:
    void rememberMediaPlace();
private:
    QPointer<Project> m_project;
    BinderItem *m_item = nullptr;
    QStackedWidget *m_stack;
    QScrollArea *m_imageArea;
    QLabel *m_image;
    QPixmap m_pixmap;
    double m_zoom = 0; // 0 = fit
    bool m_restoringPlace = false; // a PDF file is being opened at its page (0.21.1)
    QPdfDocument *m_pdf;
    QPdfView *m_pdfView;
    QTextBrowser *m_html;
    QWidget *m_other;
    QLabel *m_otherLabel;
    QLabel *m_otherHint;
    QPushButton *m_openButton;
    // player
    QWidget *m_playerPage;
    QStackedWidget *m_playerStack;    // video / audio card
    QWidget *m_audioCard;
    QLabel *m_audioIcon, *m_audioTitle, *m_audioInfo;
    QLabel *m_playerMessage;
    QPushButton *m_playerOpen;
    playback::MediaControls *m_ownControls = nullptr;
    media::Player *m_player = nullptr;
    Listener *m_listener = nullptr;
    QString m_playerItemId;
    qint64 m_restore = -1;            // position to go to once the file is loaded
};

} // namespace qb
