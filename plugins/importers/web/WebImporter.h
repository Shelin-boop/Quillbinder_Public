#pragma once
// Add-on "Web Page Import": File > Import > Web Page and web links dropped on
// the binder. Fetches the page and its pictures, finds the article
// (Article.h) and hands it to the program as a text document, or as a
// read-only web page document.
//
// HTTPS needs a TLS plugin of Qt, which the AppImage does not carry: the
// add-on brings Qt's OpenSSL plugin (qtplugins/tls) and adds the folder to
// the library paths when it is loaded; OpenSSL itself (libssl.so.3) comes
// from the system.

#include "Article.h"
#include "import/ImporterApi.h"

#include <QDialog>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QCheckBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QRadioButton;
class QTextBrowser;
class QDialogButtonBox;

namespace qbweb {

// Fetching, with an event loop inside (the caller's window stays usable).
class Fetcher : public QObject
{
    Q_OBJECT
public:
    explicit Fetcher(QObject *parent = nullptr);
    struct Result {
        bool ok = false;
        QByteArray data;
        QUrl url;            // after redirections
        QString contentType; // lower case, without parameters
        QString charset;
        QString error;
    };
    Result get(const QUrl &url, const QByteArray &accept, qint64 maxBytes);
    // Several at once (pictures); progress(done, total).
    QList<Result> getAll(const QList<QUrl> &urls, const QByteArray &accept, qint64 maxBytes,
                         const std::function<void(int, int)> &progress);
    void cancel();
    bool cancelled() const { return m_cancelled; }

signals:
    void cancelRequested();

private:
    QNetworkReply *start(const QUrl &url, const QByteArray &accept, qint64 maxBytes);
    static Result finish(QNetworkReply *r, qint64 maxBytes);
    QNetworkAccessManager *m_nam;
    bool m_cancelled = false;
};

struct Choices {
    bool articleOnly = true;
    bool pictures = true;
    bool asWebPage = false;
    static Choices load();
    void save() const;
};

// A page loaded and its pictures fetched.
class Loader : public QObject
{
    Q_OBJECT
public:
    explicit Loader(QObject *parent = nullptr);
    bool load(const QUrl &url, QString *error);          // the page
    void extract(const Choices &c);                      // from the loaded page (no network)
    void fetchPictures();                                // those of the article not fetched yet
    void cancel() { m_fetcher->cancel(); }
    bool cancelled() const { return m_fetcher->cancelled(); }
    const Extracted &extracted() const { return m_extracted; }
    QUrl url() const { return m_url; }
    bool loaded() const { return m_loaded; }
    QByteArray pictureData(int index) const;             // empty if missing or unreadable
    // The finished page for the program; writes the web page document's files into workDir.
    bool result(const Choices &c, const QString &title, const QString &workDir, qb::importers::WebPage *out, QString *error);

signals:
    void status(const QString &text);

private:
    Fetcher *m_fetcher;
    QUrl m_url;
    QString m_html;
    bool m_loaded = false;
    bool m_isHtml = true;
    QByteArray m_rawImage;  // the address was a picture
    QString m_rawText;      // or plain text
    Extracted m_extracted;
    QHash<QString, QByteArray> m_pictures; // address -> bytes (empty: failed)
};

class WebImportDialog : public QDialog
{
    Q_OBJECT
public:
    WebImportDialog(const QString &address, const QString &into, QWidget *parent = nullptr);
    Loader *loader() const { return m_loader; }
    Choices choices() const;
    QString title() const;
    // Loads the address now (tests and the Load button).
    void loadNow();

private:
    void updatePreview();
    void setBusy(bool busy);
    QLineEdit *m_address, *m_title;
    QPushButton *m_load;
    QTextBrowser *m_preview;
    QLabel *m_status;
    QCheckBox *m_articleOnly, *m_pictures;
    QRadioButton *m_asText, *m_asPage;
    QDialogButtonBox *m_buttons;
    Loader *m_loader;
    bool m_busy = false;
    bool m_titleTouched = false;
    bool m_importAfterLoad = false;
    QString m_loadedAddress;
};

} // namespace qbweb

class WebImporter : public QObject, public qb::importers::ImporterInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QB_IMPORTER_IID)
    Q_INTERFACES(qb::importers::ImporterInterface)
public:
    WebImporter();
    QString id() const override { return QStringLiteral("import-web"); }
    QString name() const override { return QStringLiteral("Web Page Import"); }
    QString version() const override { return QStringLiteral("1.0"); }
    QStringList suffixes() const override { return {}; }
    QString fileTypeName() const override { return QString(); }
    bool readFile(const QString &, const QString &, qb::importers::Content *, QString *error) override
    {
        if (error)
            *error = tr("This add-on imports web pages, not files.");
        return false;
    }
    bool importsWebPages() const override { return true; }
    bool importWebPage(QWidget *parent, const QString &address, const QString &title, bool interactive, const QString &into,
                       const QString &workDir, qb::importers::WebPage *out, QString *error) override;
};
