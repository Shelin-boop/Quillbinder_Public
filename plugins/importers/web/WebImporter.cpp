#include "WebImporter.h"

#include "Html.h"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QSettings>
#include <QSslSocket>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

#include <dlfcn.h>

namespace qbweb {

namespace {

const qint64 kMaxPage = 20 * 1024 * 1024;
const qint64 kMaxPicture = 12 * 1024 * 1024;
const int kMaxPictures = 60;

QByteArray userAgent()
{
    // a usual browser's name: some sites send other programs an empty page
    return "Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0";
}

QByteArray acceptLanguage()
{
    QStringList langs;
    for (const QString &l : QLocale().uiLanguages()) {
        if (!langs.contains(l))
            langs << l;
        if (langs.size() >= 3)
            break;
    }
    QByteArray out;
    double q = 1.0;
    for (const QString &l : langs) {
        if (!out.isEmpty())
            out += ',';
        out += l.toLatin1();
        if (q < 1.0)
            out += ";q=" + QByteArray::number(q, 'f', 1);
        q -= 0.2;
    }
    if (!langs.contains(QStringLiteral("en")))
        out += out.isEmpty() ? "en" : ",en;q=0.3";
    return out;
}

void stripImage(qb::importers::Content &c, int index)
{
    for (auto &p : c.paragraphs)
        for (int i = p.runs.size() - 1; i >= 0; --i)
            if (p.runs.at(i).image == index)
                p.runs.removeAt(i);
}

} // namespace

// ------------------------------------------------------------------ fetching

Fetcher::Fetcher(QObject *parent) : QObject(parent)
{
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    m_nam = new QNetworkAccessManager(this);
}

void Fetcher::cancel()
{
    m_cancelled = true;
    emit cancelRequested();
}

QNetworkReply *Fetcher::start(const QUrl &url, const QByteArray &accept, qint64 maxBytes)
{
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", userAgent());
    req.setRawHeader("Accept", accept);
    req.setRawHeader("Accept-Language", acceptLanguage());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setMaximumRedirectsAllowed(10);
    req.setTransferTimeout(30000);
    QNetworkReply *r = m_nam->get(req);
    r->setProperty("qbTooLarge", false);
    connect(r, &QNetworkReply::downloadProgress, r, [r, maxBytes](qint64 got, qint64) {
        if (got > maxBytes) {
            r->setProperty("qbTooLarge", true);
            r->abort();
        }
    });
    connect(this, &Fetcher::cancelRequested, r, &QNetworkReply::abort);
    return r;
}

Fetcher::Result Fetcher::finish(QNetworkReply *r, qint64 maxBytes)
{
    Result res;
    res.url = r->url();
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (r->property("qbTooLarge").toBool()) {
        res.error = QObject::tr("The file is larger than %1 MB.").arg(maxBytes / (1024 * 1024));
        return res;
    }
    if (r->error() != QNetworkReply::NoError && !(status >= 400 && status < 600)) {
        if (r->error() == QNetworkReply::OperationCanceledError)
            res.error = QObject::tr("Cancelled.");
        else if (r->error() == QNetworkReply::HostNotFoundError)
            res.error = QObject::tr("The server “%1” was not found (is the address right, is the computer online?).").arg(r->url().host());
        else
            res.error = r->errorString();
        return res;
    }
    if (status >= 400) {
        const QString reason = r->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        res.error = QObject::tr("The server answered %1 %2.").arg(status).arg(reason).trimmed();
        if (status == 401 || status == 403)
            res.error += QLatin1Char(' ') + QObject::tr("The page may need a login, or the site does not let programs read it.");
        return res;
    }
    res.data = r->readAll();
    const QString ct = r->header(QNetworkRequest::ContentTypeHeader).toString();
    res.contentType = ct.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    const int cs = ct.indexOf(QLatin1String("charset="), 0, Qt::CaseInsensitive);
    if (cs >= 0)
        res.charset = ct.mid(cs + 8).section(QLatin1Char(';'), 0, 0).remove(QLatin1Char('"')).trimmed();
    res.ok = true;
    return res;
}

Fetcher::Result Fetcher::get(const QUrl &url, const QByteArray &accept, qint64 maxBytes)
{
    m_cancelled = false;
    QNetworkReply *r = start(url, accept, maxBytes);
    QEventLoop loop;
    connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (!r->isFinished())
        loop.exec();
    Result res = finish(r, maxBytes);
    r->deleteLater();
    return res;
}

QList<Fetcher::Result> Fetcher::getAll(const QList<QUrl> &urls, const QByteArray &accept, qint64 maxBytes,
                                       const std::function<void(int, int)> &progress)
{
    m_cancelled = false;
    QList<Result> out;
    out.reserve(urls.size());
    for (int i = 0; i < urls.size(); ++i)
        out << Result();
    int next = 0, running = 0, done = 0;
    QEventLoop loop;
    std::function<void()> startMore = [&] {
        while (running < 4 && next < urls.size() && !m_cancelled) {
            const int idx = next++;
            QNetworkReply *r = start(urls.at(idx), accept, maxBytes);
            ++running;
            connect(r, &QNetworkReply::finished, &loop, [&, r, idx] {
                out[idx] = finish(r, maxBytes);
                r->deleteLater();
                --running;
                ++done;
                if (progress)
                    progress(done, urls.size());
                startMore();
                if (running == 0)
                    loop.quit();
            });
        }
    };
    startMore();
    if (running > 0)
        loop.exec();
    return out;
}

// ------------------------------------------------------------------ choices

Choices Choices::load()
{
    QSettings s;
    Choices c;
    c.articleOnly = s.value(QStringLiteral("importers/web/articleOnly"), true).toBool();
    c.pictures = s.value(QStringLiteral("importers/web/pictures"), true).toBool();
    c.asWebPage = s.value(QStringLiteral("importers/web/asWebPage"), false).toBool();
    return c;
}

void Choices::save() const
{
    QSettings s;
    s.setValue(QStringLiteral("importers/web/articleOnly"), articleOnly);
    s.setValue(QStringLiteral("importers/web/pictures"), pictures);
    s.setValue(QStringLiteral("importers/web/asWebPage"), asWebPage);
}

// ------------------------------------------------------------------ loading

Loader::Loader(QObject *parent) : QObject(parent), m_fetcher(new Fetcher(this)) {}

bool Loader::load(const QUrl &url, QString *error)
{
    m_loaded = false;
    m_pictures.clear();
    m_rawImage.clear();
    m_rawText.clear();
    m_html.clear();
    if (url.scheme() == QLatin1String("https") && !QSslSocket::supportsSsl()) {
        if (error)
            *error = tr("Secure web pages (https) cannot be fetched: the TLS support is missing (the add-on's TLS plugin "
                        "or the system's OpenSSL 3, package libssl3).");
        return false;
    }
    emit status(tr("Fetching %1 …").arg(url.host()));
    const Fetcher::Result r = m_fetcher->get(url, "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8", kMaxPage);
    if (!r.ok) {
        if (error)
            *error = r.error;
        return false;
    }
    m_url = r.url;
    const QString ct = r.contentType;
    const QByteArray head = r.data.left(1024).trimmed().toLower();
    const bool looksHtml = head.startsWith("<!doctype html") || head.contains("<html") || head.contains("<head")
                           || head.contains("<body") || head.contains("<p");
    if (ct.contains(QLatin1String("html")) || ct.contains(QLatin1String("xml")) || (ct.isEmpty() && looksHtml)) {
        m_html = decodeHtml(r.data, r.charset);
        m_isHtml = true;
    } else if (ct.startsWith(QLatin1String("image/"))) {
        m_rawImage = r.data;
        m_isHtml = false;
    } else if (ct.startsWith(QLatin1String("text/"))) {
        m_rawText = decodeHtml(r.data, r.charset.isEmpty() ? QStringLiteral("utf-8") : r.charset);
        m_isHtml = false;
    } else if (ct == QLatin1String("application/pdf")) {
        if (error)
            *error = tr("The address leads to a PDF file. Download it and add it with File > Import > Files.");
        return false;
    } else {
        if (error)
            *error = tr("The address does not lead to a web page (it is “%1”).").arg(ct);
        return false;
    }
    m_loaded = true;
    return true;
}

void Loader::extract(const Choices &c)
{
    if (m_isHtml) {
        ExtractOptions o;
        o.articleOnly = c.articleOnly;
        o.pictures = c.pictures;
        m_extracted = qbweb::extract(m_html, m_url, o);
        return;
    }
    m_extracted = Extracted();
    const QString name = QFileInfo(m_url.path()).completeBaseName();
    m_extracted.info.title = name.isEmpty() ? m_url.host() : name;
    if (!m_rawImage.isEmpty()) {
        qb::importers::Paragraph p;
        qb::importers::Run r;
        r.image = 0;
        p.runs << r;
        m_extracted.content.paragraphs << p;
        m_extracted.content.images << qb::importers::Image();
        ImageRef ref;
        ref.data = m_rawImage;
        m_extracted.images << ref;
    } else {
        for (const QString &para : m_rawText.split(QRegularExpression(QStringLiteral("\\n\\s*\\n")))) {
            qb::importers::Paragraph p;
            qb::importers::Run r;
            r.text = para.trimmed();
            r.text.replace(QLatin1Char('\n'), QChar(QChar::LineSeparator));
            if (r.text.isEmpty())
                continue;
            p.runs << r;
            m_extracted.content.paragraphs << p;
        }
    }
    m_extracted.content.title = m_extracted.info.title;
    m_extracted.words = countWords(m_extracted.content);
}

void Loader::fetchPictures()
{
    QList<QUrl> todo;
    for (const ImageRef &ref : m_extracted.images)
        if (ref.url.isValid() && !m_pictures.contains(ref.url.toString()) && !todo.contains(ref.url) && todo.size() < kMaxPictures)
            todo << ref.url;
    if (todo.isEmpty())
        return;
    emit status(todo.size() == 1 ? tr("Fetching 1 picture …") : tr("Fetching %1 pictures …").arg(todo.size()));
    const QList<Fetcher::Result> res =
        m_fetcher->getAll(todo, "image/webp,image/png,image/jpeg,image/gif;q=0.9,image/svg+xml;q=0.8", kMaxPicture,
                          [this](int done, int total) { emit status(tr("Fetching pictures: %1 of %2 …").arg(done).arg(total)); });
    for (int i = 0; i < todo.size(); ++i) {
        QByteArray data = res.at(i).ok ? res.at(i).data : QByteArray();
        if (!data.isEmpty() && QImage::fromData(data).isNull())
            data.clear(); // a format the program cannot show (AVIF …)
        if (!m_fetcher->cancelled() || res.at(i).ok)
            m_pictures.insert(todo.at(i).toString(), data);
    }
}

QByteArray Loader::pictureData(int index) const
{
    if (index < 0 || index >= m_extracted.images.size())
        return QByteArray();
    const ImageRef &ref = m_extracted.images.at(index);
    return ref.url.isValid() ? m_pictures.value(ref.url.toString()) : ref.data;
}

bool Loader::result(const Choices &c, const QString &title, const QString &workDir, qb::importers::WebPage *out, QString *error)
{
    qb::importers::Content content = m_extracted.content;
    QList<qb::importers::Image> images;
    QVector<int> map(content.images.size(), -1);
    QStringList exts;
    int missing = 0;
    for (int i = 0; i < content.images.size(); ++i) {
        QByteArray bytes = c.pictures ? pictureData(i) : QByteArray();
        QImage img;
        if (bytes.isEmpty() || !img.loadFromData(bytes)) {
            if (c.pictures)
                ++missing;
            continue;
        }
        QBuffer buf(&bytes);
        QByteArray format = QImageReader(&buf).format();
        if (img.width() > 1600 || format.isEmpty() || format == "svg" || format == "svgz") {
            // very large pictures are made smaller; drawings become pictures
            if (img.width() > 1600)
                img = img.scaledToWidth(1600, Qt::SmoothTransformation);
            QByteArray re;
            QBuffer out(&re);
            out.open(QIODevice::WriteOnly);
            format = img.hasAlphaChannel() ? QByteArrayLiteral("png") : QByteArrayLiteral("jpg");
            img.save(&out, format.constData(), 88);
            bytes = re;
        }
        qb::importers::Image im = content.images.at(i);
        im.data = bytes;
        map[i] = images.size();
        images << im;
        exts << (format == "jpeg" ? QStringLiteral("jpg") : QString::fromLatin1(format));
    }
    for (auto &p : content.paragraphs)
        for (int k = p.runs.size() - 1; k >= 0; --k) {
            int &idx = p.runs[k].image;
            if (idx < 0)
                continue;
            idx = idx < map.size() ? map.at(idx) : -1;
            if (idx < 0)
                p.runs.removeAt(k);
        }
    for (int k = content.paragraphs.size() - 1; k >= 0; --k)
        if (content.paragraphs.at(k).runs.isEmpty())
            content.paragraphs.removeAt(k);
    content.images = images;
    Q_UNUSED(stripImage);
    if (missing > 0)
        content.warnings << (missing == 1 ? tr("1 picture could not be fetched or is in a format the program does not show.")
                                          : tr("%1 pictures could not be fetched or are in a format the program does not show.").arg(missing));
    if (m_extracted.fromScriptData)
        content.warnings << tr("The page builds its text with a script; the text was taken from the page's data, "
                               "without formatting and pictures.");
    else if (m_isHtml && countWords(content) < 30 && content.images.isEmpty())
        content.warnings << tr("Little text was found on the page. It may be built by a script, need a login, or "
                               "be mostly pictures; “Only the article” off takes the whole page.");
    out->content = content;
    out->url = m_url.toString();
    out->title = title.trimmed().isEmpty() ? m_extracted.info.title : title.trimmed();
    out->content.title = out->title;
    out->siteName = m_extracted.info.siteName;
    out->author = m_extracted.info.byline;
    out->published = m_extracted.info.published;
    out->excerpt = m_extracted.info.excerpt;
    out->asWebPage = c.asWebPage;
    if (c.asWebPage) {
        // the page as HTML with its pictures in a folder beside it
        QDir dir(workDir);
        dir.mkpath(QStringLiteral("page-files"));
        QStringList names;
        const QString stamp = QString::number(qHash(m_url.toString()), 16);
        for (int i = 0; i < images.size(); ++i) {
            const QString name = QStringLiteral("page-files/%1-%2.%3").arg(stamp).arg(i + 1).arg(exts.value(i, QStringLiteral("png")));
            QSaveFile f(dir.filePath(name));
            if (f.open(QIODevice::WriteOnly)) {
                f.write(images.at(i).data);
                f.commit();
            }
            names << name;
        }
        const QString site = out->siteName.isEmpty() ? m_url.host() : out->siteName;
        const QString source = tr("From %1 · retrieved %2")
                                   .arg(QStringLiteral("<a href=\"%1\">%2</a>").arg(out->url.toHtmlEscaped(), site.toHtmlEscaped()),
                                        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        const QString html = toHtml(content, names, out->title, source);
        QSaveFile f(dir.filePath(QStringLiteral("page.html")));
        if (!f.open(QIODevice::WriteOnly)) {
            if (error)
                *error = tr("Cannot write the page.");
            return false;
        }
        f.write(html.toUtf8());
        f.commit();
        out->htmlFile = dir.filePath(QStringLiteral("page.html"));
    }
    return true;
}

// ------------------------------------------------------------------ the window

WebImportDialog::WebImportDialog(const QString &address, const QString &into, QWidget *parent)
    : QDialog(parent), m_loader(new Loader(this))
{
    setWindowTitle(tr("Import Web Page"));
    resize(760, 720);
    auto *l = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    auto *row = new QHBoxLayout;
    m_address = new QLineEdit(this);
    m_address->setObjectName(QStringLiteral("webAddress"));
    m_address->setPlaceholderText(QStringLiteral("https://…"));
    m_address->setClearButtonEnabled(true);
    QString start = address;
    if (start.isEmpty()) {
        // an address on the clipboard
        const QString clip = QApplication::clipboard()->text().trimmed();
        if ((clip.startsWith(QLatin1String("http://")) || clip.startsWith(QLatin1String("https://"))) && !clip.contains(QLatin1Char(' '))
            && !clip.contains(QLatin1Char('\n')))
            start = clip;
    }
    m_address->setText(start);
    m_load = new QPushButton(tr("&Load"), this);
    m_load->setObjectName(QStringLiteral("webLoad"));
    row->addWidget(m_address, 1);
    row->addWidget(m_load);
    auto *addrLabel = new QLabel(tr("&Address:"), this);
    addrLabel->setBuddy(m_address);
    form->addRow(addrLabel, row);
    m_title = new QLineEdit(this);
    m_title->setObjectName(QStringLiteral("webTitle"));
    form->addRow(tr("&Title:"), m_title);
    l->addLayout(form);
    m_preview = new QTextBrowser(this);
    m_preview->setObjectName(QStringLiteral("webPreview"));
    m_preview->setOpenLinks(false);
    m_preview->setMinimumHeight(300);
    m_preview->setPlaceholderText(tr("Type or paste the address of a page and press Load. What will be imported is shown here."));
    l->addWidget(m_preview, 1);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("webStatus"));
    m_status->setWordWrap(true);
    l->addWidget(m_status);
    const Choices ch = Choices::load();
    auto *keep = new QGroupBox(tr("From the page"), this);
    auto *kl = new QVBoxLayout(keep);
    m_articleOnly = new QCheckBox(tr("Only the article (leave out menus, adverts and comments)"), keep);
    m_articleOnly->setObjectName(QStringLiteral("webArticleOnly"));
    m_articleOnly->setChecked(ch.articleOnly);
    m_pictures = new QCheckBox(tr("Pictures"), keep);
    m_pictures->setObjectName(QStringLiteral("webPictures"));
    m_pictures->setChecked(ch.pictures);
    kl->addWidget(m_articleOnly);
    kl->addWidget(m_pictures);
    l->addWidget(keep);
    auto *as = new QGroupBox(tr("Import as"), this);
    auto *al = new QVBoxLayout(as);
    m_asText = new QRadioButton(tr("Text document: can be edited, annotated and compiled"), as);
    m_asText->setObjectName(QStringLiteral("webAsText"));
    m_asPage = new QRadioButton(tr("Web page document: read-only, for reference"), as);
    m_asPage->setObjectName(QStringLiteral("webAsPage"));
    (ch.asWebPage ? m_asPage : m_asText)->setChecked(true);
    al->addWidget(m_asText);
    al->addWidget(m_asPage);
    l->addWidget(as);
    auto *intoLabel = new QLabel(tr("The page goes into “%1”; its address becomes a bookmark of the document, "
                                    "author and date go into its notes.")
                                     .arg(into),
                                 this);
    intoLabel->setWordWrap(true);
    intoLabel->setEnabled(false);
    l->addWidget(intoLabel);
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("&Import"));
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!start.isEmpty());
    l->addWidget(m_buttons);

    connect(m_loader, &Loader::status, this, [this](const QString &s) { m_status->setText(s); });
    connect(m_load, &QPushButton::clicked, this, [this] {
        if (m_busy)
            m_loader->cancel();
        else
            loadNow();
    });
    // Enter loads (and shows the preview); Import is clicked on purpose
    m_load->setDefault(true);
    m_buttons->button(QDialogButtonBox::Ok)->setAutoDefault(false);
    m_buttons->button(QDialogButtonBox::Cancel)->setAutoDefault(false);
    connect(m_address, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!t.trimmed().isEmpty() && !m_busy);
    });
    connect(m_title, &QLineEdit::textEdited, this, [this] { m_titleTouched = true; });
    auto reextract = [this] {
        if (!m_loader->loaded() || m_busy)
            return;
        setBusy(true);
        m_loader->extract(choices());
        if (m_pictures->isChecked())
            m_loader->fetchPictures();
        setBusy(false);
        updatePreview();
    };
    connect(m_articleOnly, &QCheckBox::toggled, this, reextract);
    connect(m_pictures, &QCheckBox::toggled, this, reextract);
    connect(m_buttons, &QDialogButtonBox::accepted, this, [this] {
        if (m_busy)
            return;
        // not loaded yet, or another address: load first
        if (!m_loader->loaded() || m_loadedAddress != m_address->text().trimmed()) {
            m_importAfterLoad = true;
            loadNow();
            return;
        }
        choices().save();
        accept();
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, [this] {
        if (m_busy)
            m_loader->cancel();
        reject();
    });
    if (!address.isEmpty())
        QTimer::singleShot(0, this, &WebImportDialog::loadNow);
}

Choices WebImportDialog::choices() const
{
    Choices c;
    c.articleOnly = m_articleOnly->isChecked();
    c.pictures = m_pictures->isChecked();
    c.asWebPage = m_asPage->isChecked();
    return c;
}

QString WebImportDialog::title() const { return m_title->text().trimmed(); }

void WebImportDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_load->setText(busy ? tr("&Stop") : tr("&Load"));
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!busy && !m_address->text().trimmed().isEmpty());
    m_articleOnly->setEnabled(!busy);
    m_pictures->setEnabled(!busy);
    if (busy)
        QApplication::setOverrideCursor(Qt::BusyCursor);
    else
        QApplication::restoreOverrideCursor();
}

void WebImportDialog::loadNow()
{
    const QString typed = m_address->text().trimmed();
    if (typed.isEmpty())
        return;
    QUrl url = QUrl::fromUserInput(typed);
    if (!url.isValid() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))) {
        m_status->setText(tr("This is not the address of a web page."));
        m_importAfterLoad = false;
        return;
    }
    setBusy(true);
    QString err;
    const bool ok = m_loader->load(url, &err);
    if (ok) {
        m_loadedAddress = typed;
        m_loader->extract(choices());
        if (m_pictures->isChecked())
            m_loader->fetchPictures();
    }
    setBusy(false);
    if (!ok) {
        m_status->setText(err);
        m_preview->clear();
        m_importAfterLoad = false;
        return;
    }
    if (!m_titleTouched)
        m_title->setText(m_loader->extracted().info.title);
    updatePreview();
    if (m_importAfterLoad) {
        m_importAfterLoad = false;
        choices().save();
        accept();
    }
}

void WebImportDialog::updatePreview()
{
    const Extracted &e = m_loader->extracted();
    QStringList names;
    QTextDocument *doc = m_preview->document();
    int pics = 0;
    for (int i = 0; i < e.images.size(); ++i) {
        const QByteArray data = m_loader->pictureData(i);
        QImage img;
        if (!data.isEmpty() && img.loadFromData(data)) {
            const QString name = QStringLiteral("preview://%1").arg(i);
            doc->addResource(QTextDocument::ImageResource, QUrl(name), img);
            names << name;
            ++pics;
        } else {
            names << QString();
        }
    }
    m_preview->setHtml(toHtml(e.content, names, e.info.title, QString()));
    QStringList info;
    info << (e.words == 1 ? tr("1 word") : tr("%1 words").arg(QLocale().toString(e.words)));
    if (m_pictures->isChecked())
        info << (pics == 1 ? tr("1 picture") : tr("%1 pictures").arg(pics));
    if (!e.info.byline.isEmpty())
        info << tr("by %1").arg(e.info.byline);
    if (!e.info.siteName.isEmpty())
        info << e.info.siteName;
    if (!e.info.published.isEmpty())
        info << e.info.published;
    QString s = info.join(QStringLiteral(" · "));
    if (e.fromScriptData)
        s += QLatin1Char('\n') + tr("The page builds its text with a script: the text comes from the page's data.");
    m_status->setText(s);
}

} // namespace qbweb

// ------------------------------------------------------------------ the plugin

static void selfMarker() {}

WebImporter::WebImporter()
{
    // the add-on's Qt plugins (TLS): <add-on>/plugins/<this library> -> <add-on>/qtplugins
    Dl_info info;
    if (dladdr(reinterpret_cast<void *>(&selfMarker), &info) && info.dli_fname) {
        const QString dir = QFileInfo(QString::fromLocal8Bit(info.dli_fname)).absolutePath() + QStringLiteral("/../qtplugins");
        if (QFileInfo(dir).isDir())
            QCoreApplication::addLibraryPath(QDir::cleanPath(dir));
    }
}

bool WebImporter::importWebPage(QWidget *parent, const QString &address, const QString &title, bool interactive,
                                const QString &into, const QString &workDir, qb::importers::WebPage *out, QString *error)
{
    using namespace qbweb;
    if (interactive) {
        WebImportDialog dlg(address, into, parent);
        if (!title.isEmpty())
            dlg.findChild<QLineEdit *>(QStringLiteral("webTitle"))->setText(title);
        if (dlg.exec() != QDialog::Accepted)
            return false;
        return dlg.loader()->result(dlg.choices(), dlg.title(), workDir, out, error);
    }
    // no questions: the choices of last time, a progress window
    const QUrl url = QUrl::fromUserInput(address.trimmed());
    if (!url.isValid() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))) {
        if (error)
            *error = tr("“%1” is not the address of a web page.").arg(address);
        return false;
    }
    Loader loader;
    QProgressDialog progress(tr("Importing %1 …").arg(url.toDisplayString()), tr("Cancel"), 0, 0, parent);
    progress.setWindowTitle(tr("Import Web Page"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    QObject::connect(&progress, &QProgressDialog::canceled, &loader, &Loader::cancel);
    QObject::connect(&loader, &Loader::status, &progress, &QProgressDialog::setLabelText);
    const Choices c = Choices::load();
    QString err;
    if (!loader.load(url, &err)) {
        if (error && !loader.cancelled())
            *error = tr("%1: %2").arg(url.toDisplayString(), err);
        return false;
    }
    loader.extract(c);
    if (c.pictures)
        loader.fetchPictures();
    if (loader.cancelled())
        return false;
    progress.close();
    return loader.result(c, title, workDir, out, error);
}
