#include "core/WebArchive.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMimeDatabase>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringDecoder>
#include <QUrl>
#include <QUuid>

namespace qb::webarchive {

namespace {

QByteArray base64Lines(const QByteArray &data)
{
    const QByteArray b = data.toBase64();
    QByteArray out;
    out.reserve(b.size() + b.size() / 76 * 2 + 2);
    for (int i = 0; i < b.size(); i += 76)
        out += b.mid(i, 76) + "\r\n";
    return out;
}

QByteArray decodeQuotedPrintable(const QByteArray &in)
{
    QByteArray out;
    out.reserve(in.size());
    for (int i = 0; i < in.size(); ++i) {
        const char c = in.at(i);
        if (c != '=') {
            out += c;
            continue;
        }
        if (i + 1 < in.size() && (in.at(i + 1) == '\n' || in.at(i + 1) == '\r')) {
            // a soft line break
            ++i;
            if (in.at(i) == '\r' && i + 1 < in.size() && in.at(i + 1) == '\n')
                ++i;
            continue;
        }
        if (i + 2 < in.size()) {
            bool ok = false;
            const int v = in.mid(i + 1, 2).toInt(&ok, 16);
            if (ok) {
                out += char(v);
                i += 2;
                continue;
            }
        }
        out += c;
    }
    return out;
}

struct Part {
    QHash<QByteArray, QByteArray> headers;   // lower-case name -> value (folded lines joined)
    QByteArray body;
};

// Headers up to the first empty line; returns where the body starts
int readHeaders(const QByteArray &data, int from, QHash<QByteArray, QByteArray> *out)
{
    int pos = from;
    QByteArray lastName;
    while (pos < data.size()) {
        int end = data.indexOf('\n', pos);
        if (end < 0)
            end = int(data.size());
        QByteArray line = data.mid(pos, end - pos);
        if (line.endsWith('\r'))
            line.chop(1);
        pos = end + 1;
        if (line.isEmpty())
            break;
        if ((line.startsWith(' ') || line.startsWith('\t')) && !lastName.isEmpty()) {
            (*out)[lastName] += ' ' + line.trimmed();
            continue;
        }
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        lastName = line.left(colon).trimmed().toLower();
        out->insert(lastName, line.mid(colon + 1).trimmed());
    }
    return pos;
}

QByteArray param(const QByteArray &header, const QByteArray &name)
{
    const QRegularExpression re(QStringLiteral("(?:^|;)\\s*%1\\s*=\\s*(\"([^\"]*)\"|[^;\\s]+)").arg(QString::fromLatin1(name)),
                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(QString::fromLatin1(header));
    if (!m.hasMatch())
        return QByteArray();
    return (m.captured(2).isEmpty() ? m.captured(1) : m.captured(2)).toLatin1();
}

QList<Part> parts(const QByteArray &data, QByteArray *rootType)
{
    QList<Part> out;
    QHash<QByteArray, QByteArray> top;
    int bodyStart = readHeaders(data, 0, &top);
    *rootType = top.value("content-type");
    const QByteArray boundary = param(*rootType, "boundary");
    if (boundary.isEmpty()) {
        // a single page without parts
        Part p;
        p.headers = top;
        p.body = data.mid(bodyStart);
        out << p;
        return out;
    }
    const QByteArray sep = "--" + boundary;
    int pos = data.indexOf(sep, bodyStart);
    while (pos >= 0) {
        int start = pos + int(sep.size());
        if (data.mid(start, 2) == "--")
            break;   // the end
        const int lineEnd = data.indexOf('\n', start);
        if (lineEnd < 0)
            break;
        start = lineEnd + 1;
        const int next = data.indexOf(sep, start);
        Part p;
        const int body = readHeaders(data, start, &p.headers);
        int end = next < 0 ? int(data.size()) : next;
        // the line break before the boundary belongs to it
        if (end > body && data.at(end - 1) == '\n')
            --end;
        if (end > body && data.at(end - 1) == '\r')
            --end;
        QByteArray raw = data.mid(body, qMax(0, end - body));
        const QByteArray enc = p.headers.value("content-transfer-encoding").trimmed().toLower();
        if (enc == "base64")
            p.body = QByteArray::fromBase64(raw.simplified().replace(' ', QByteArray()));
        else if (enc == "quoted-printable")
            p.body = decodeQuotedPrintable(raw);
        else
            p.body = raw;
        out << p;
        pos = next;
    }
    return out;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data);
    return f.commit();
}

} // namespace

QByteArray pack(const QString &htmlFile, const QString &url)
{
    QFile f(htmlFile);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    const QByteArray html = f.readAll();
    const QString location = url.isEmpty() ? QStringLiteral("file:///page.html") : url;
    const QByteArray boundary = "----=_Quillbinder_" + QUuid::createUuid().toString(QUuid::Id128).toLatin1();
    QByteArray out;
    out += "From: <Saved by Quillbinder>\r\n";
    out += "MIME-Version: 1.0\r\n";
    out += "Content-Type: multipart/related; type=\"text/html\"; boundary=\"" + boundary + "\"\r\n\r\n";
    out += "--" + boundary + "\r\n";
    out += "Content-Type: text/html; charset=utf-8\r\n";
    out += "Content-Transfer-Encoding: base64\r\n";
    out += "Content-Location: " + QUrl(location).toEncoded() + "\r\n\r\n";
    out += base64Lines(html);
    const QDir base = QFileInfo(htmlFile).dir();
    const QUrl baseUrl(location);
    QMimeDatabase mime;
    for (const QFileInfo &d : base.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QDirIterator it(d.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
        QStringList files;
        while (it.hasNext())
            files << it.next();
        files.sort();
        for (const QString &path : std::as_const(files)) {
            QFile part(path);
            if (!part.open(QIODevice::ReadOnly))
                continue;
            const QString rel = base.relativeFilePath(path);
            QUrl relUrl;
            relUrl.setPath(rel);
            out += "--" + boundary + "\r\n";
            out += "Content-Type: " + mime.mimeTypeForFile(path).name().toLatin1() + "\r\n";
            out += "Content-Transfer-Encoding: base64\r\n";
            out += "Content-Location: " + baseUrl.resolved(relUrl).toEncoded() + "\r\n\r\n";
            out += base64Lines(part.readAll());
        }
    }
    out += "--" + boundary + "--\r\n";
    return out;
}

QString addressOf(const QByteArray &archive)
{
    QByteArray rootType;
    for (const Part &p : parts(archive, &rootType))
        if (p.headers.value("content-type").toLower().startsWith("text/html"))
            return QString::fromLatin1(p.headers.value("content-location")).trimmed();
    return QString();
}

bool unpack(const QByteArray &archive, const QString &outDir, QString *page, QString *url, QString *error)
{
    QByteArray rootType;
    const QList<Part> list = parts(archive, &rootType);
    int main = -1;
    for (int i = 0; i < list.size() && main < 0; ++i)
        if (list.at(i).headers.value("content-type").toLower().startsWith("text/html"))
            main = i;
    if (main < 0) {
        if (error)
            *error = QObject::tr("The saved page holds no web page.");
        return false;
    }
    if (!QDir().mkpath(outDir)) {
        if (error)
            *error = QObject::tr("Cannot make the folder %1.").arg(outDir);
        return false;
    }
    const QUrl mainUrl(QString::fromLatin1(list.at(main).headers.value("content-location")).trimmed());
    if (url)
        *url = mainUrl.toString();
    // the other parts: a file each, found by their address (and Content-ID); named first, written after (a style
    // sheet's own links are pointed to the files too, read from its own address)
    QHash<QString, QString> fileFor;
    QStringList names(list.size());
    QList<QUrl> locations(list.size());
    QMimeDatabase mime;
    for (int i = 0; i < list.size(); ++i) {
        if (i == main)
            continue;
        const Part &p = list.at(i);
        const QString loc = QString::fromLatin1(p.headers.value("content-location")).trimmed();
        QString cid = QString::fromLatin1(p.headers.value("content-id")).trimmed();
        if (cid.startsWith(QLatin1Char('<')) && cid.endsWith(QLatin1Char('>')))
            cid = cid.mid(1, cid.size() - 2);
        const QString type = QString::fromLatin1(p.headers.value("content-type")).section(QLatin1Char(';'), 0, 0).trimmed();
        QString suffix = mime.mimeTypeForName(type).preferredSuffix();
        if (suffix.isEmpty())
            suffix = QFileInfo(QUrl(loc).path()).suffix().left(8);
        // letters and digits only, and never the ending of a program (the folder may be opened from a file manager)
        for (const QChar c : std::as_const(suffix))
            if (!(c.isLetterOrNumber() && c.unicode() < 128)) {
                suffix.clear();
                break;
            }
        static const QStringList kProgram = {QStringLiteral("sh"), QStringLiteral("desktop"), QStringLiteral("exe"),
                                             QStringLiteral("bat"), QStringLiteral("cmd"), QStringLiteral("com"),
                                             QStringLiteral("lnk"), QStringLiteral("appimage"), QStringLiteral("run"),
                                             QStringLiteral("bin"), QStringLiteral("msi"), QStringLiteral("ps1"),
                                             QStringLiteral("vbs"), QStringLiteral("scr"), QStringLiteral("jar"),
                                             QStringLiteral("py"), QStringLiteral("pl")};
        if (kProgram.contains(suffix.toLower()))
            suffix = QStringLiteral("bin");
        names[i] = QStringLiteral("part%1%2").arg(i).arg(suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix);
        if (!loc.isEmpty()) {
            locations[i] = mainUrl.resolved(QUrl(loc));
            fileFor.insert(locations[i].toString(), names[i]);
        }
        if (!cid.isEmpty())
            fileFor.insert(QStringLiteral("cid:") + cid, names[i]);
    }
    static const QRegularExpression cssUrl(QStringLiteral("url\\(\\s*(['\"]?)([^)'\"]+)\\1\\s*\\)"), QRegularExpression::CaseInsensitiveOption);
    // url(...) in a style sheet or a page pointed to the files (refs read from `base`)
    auto rewriteCss = [&](const QString &text, const QUrl &base) {
        QString out;
        int at = 0;
        for (auto it = cssUrl.globalMatch(text); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            const QString r = m.captured(2).trimmed();
            QString local = fileFor.value(r);
            if (local.isEmpty() && !r.isEmpty() && !r.startsWith(QLatin1Char('#')))
                local = fileFor.value(base.resolved(QUrl(r)).toString());
            out += text.mid(at, m.capturedStart() - at);
            out += local.isEmpty() ? m.captured(0) : QStringLiteral("url(\"") + local + QStringLiteral("\")");
            at = m.capturedEnd();
        }
        return out + text.mid(at);
    };
    for (int i = 0; i < list.size(); ++i) {
        if (i == main)
            continue;
        const Part &p = list.at(i);
        QByteArray body = p.body;
        if (p.headers.value("content-type").toLower().startsWith("text/css"))
            body = rewriteCss(QString::fromUtf8(body), locations.at(i).isEmpty() ? mainUrl : locations.at(i)).toUtf8();
        if (!writeFile(outDir + QLatin1Char('/') + names.at(i), body)) {
            if (error)
                *error = QObject::tr("Cannot write %1.").arg(outDir + QLatin1Char('/') + names.at(i));
            return false;
        }
    }
    // the page in UTF-8, its links to parts pointing to the files
    const QByteArray charset = param(list.at(main).headers.value("content-type"), "charset");
    // the page's own encoding (windows-1252 and the like through ICU, which Qt's build carries), else UTF-8
    QStringDecoder dec(charset.isEmpty() ? "UTF-8" : charset.constData());
    if (!dec.isValid()) {
        const QByteArray cs = charset.toLower();
        dec = QStringDecoder(cs.startsWith("windows-125") || cs.startsWith("iso-8859") || cs.startsWith("cp125") ? QStringDecoder::Latin1
                                                                                                             : QStringDecoder::Utf8);
    }
    QString html = dec.decode(list.at(main).body);
    auto localFor = [&](const QString &ref) -> QString {
        const QString r = ref.trimmed();
        if (r.isEmpty() || r.startsWith(QLatin1Char('#')))
            return QString();
        if (fileFor.contains(r))
            return fileFor.value(r);
        return fileFor.value(mainUrl.resolved(QUrl(r)).toString());
    };
    static const QRegularExpression attr(QStringLiteral("\\b(src|href|background|poster)\\s*=\\s*(\"([^\"]*)\"|'([^']*)')"),
                                         QRegularExpression::CaseInsensitiveOption);
    QString rewritten;
    int last = 0;
    for (auto it = attr.globalMatch(html); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QString value = m.captured(3).isEmpty() ? m.captured(4) : m.captured(3);
        const QString local = localFor(value);
        rewritten += html.mid(last, m.capturedStart() - last);
        rewritten += local.isEmpty() ? m.captured(0) : m.captured(1) + QStringLiteral("=\"") + local + QLatin1Char('"');
        last = m.capturedEnd();
    }
    rewritten += html.mid(last);
    html = rewriteCss(rewritten, mainUrl);
    // the page is written as UTF-8 now
    static const QRegularExpression metaCharset(QStringLiteral("(<meta[^>]*charset\\s*=\\s*[\"']?)[A-Za-z0-9_:.\\-]+"),
                                                QRegularExpression::CaseInsensitiveOption);
    html.replace(metaCharset, QStringLiteral("\\1utf-8"));
    const QString index = outDir + QLatin1String("/index.html");
    if (!writeFile(index, html.toUtf8())) {
        if (error)
            *error = QObject::tr("Cannot write %1.").arg(index);
        return false;
    }
    if (page)
        *page = index;
    return true;
}

} // namespace qb::webarchive
