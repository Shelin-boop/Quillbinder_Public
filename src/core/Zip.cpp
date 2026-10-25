#include "core/Zip.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <zlib.h>

namespace qb {

static void put16(QByteArray &b, quint16 v)
{
    b.append(char(v & 0xff));
    b.append(char((v >> 8) & 0xff));
}

static void put32(QByteArray &b, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        b.append(char((v >> (8 * i)) & 0xff));
}

static quint16 get16(const QByteArray &b, int pos)
{
    return quint16(uchar(b[pos])) | (quint16(uchar(b[pos + 1])) << 8);
}

static quint32 get32(const QByteArray &b, int pos)
{
    return quint32(uchar(b[pos])) | (quint32(uchar(b[pos + 1])) << 8) | (quint32(uchar(b[pos + 2])) << 16)
           | (quint32(uchar(b[pos + 3])) << 24);
}

static QByteArray deflateRaw(const QByteArray &in)
{
    z_stream zs{};
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return QByteArray();
    QByteArray out;
    out.resize(int(deflateBound(&zs, uLong(in.size()))) + 16);
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.constData()));
    zs.avail_in = uInt(in.size());
    zs.next_out = reinterpret_cast<Bytef *>(out.data());
    zs.avail_out = uInt(out.size());
    const int r = deflate(&zs, Z_FINISH);
    const int produced = int(zs.total_out);
    deflateEnd(&zs);
    if (r != Z_STREAM_END)
        return QByteArray();
    out.resize(produced);
    return out;
}

static QByteArray inflateRaw(const QByteArray &in, quint32 size)
{
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK)
        return QByteArray();
    QByteArray out;
    out.resize(int(size));
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.constData()));
    zs.avail_in = uInt(in.size());
    zs.next_out = reinterpret_cast<Bytef *>(out.data());
    zs.avail_out = uInt(out.size());
    const int r = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (r != Z_STREAM_END && r != Z_OK)
        return QByteArray();
    out.resize(int(zs.total_out));
    return out;
}

ZipWriter::ZipWriter(const QString &path) : m_file(path)
{
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        m_error = m_file.errorString();
}

ZipWriter::~ZipWriter()
{
    if (!m_closed)
        close();
}

static void put64(QByteArray &b, quint64 v)
{
    for (int i = 0; i < 8; ++i)
        b.append(char((v >> (8 * i)) & 0xff));
}

static quint16 dosTime(const QDateTime &time)
{
    const QTime t = time.time();
    return quint16((t.hour() << 11) | (t.minute() << 5) | (t.second() / 2));
}

static quint16 dosDate(const QDateTime &time)
{
    const QDate d = time.date();
    return quint16(((qMax(1980, d.year()) - 1980) << 9) | (d.month() << 5) | d.day());
}

static const quint64 kMax32 = 0xFFFFFFFFULL;

bool ZipWriter::addFile(const QString &name, const QByteArray &data, bool compress, const QDateTime &time)
{
    if (!m_file.isOpen())
        return false;
    Entry e;
    e.name = name.toUtf8();
    e.size = quint64(data.size());
    e.crc = quint32(crc32(0L, reinterpret_cast<const Bytef *>(data.constData()), uInt(data.size())));
    QByteArray payload = data;
    e.method = 0;
    if (compress && data.size() > 64) {
        const QByteArray c = deflateRaw(data);
        if (!c.isEmpty() && c.size() < data.size()) {
            payload = c;
            e.method = 8;
        }
    }
    e.compSize = quint64(payload.size());
    e.time = dosTime(time);
    e.date = dosDate(time);
    e.offset = quint64(m_file.pos());

    QByteArray h;
    put32(h, 0x04034b50);
    put16(h, 20);
    put16(h, 0x0800); // UTF-8 names
    put16(h, e.method);
    put16(h, e.time);
    put16(h, e.date);
    put32(h, e.crc);
    put32(h, quint32(e.compSize));
    put32(h, quint32(e.size));
    put16(h, quint16(e.name.size()));
    put16(h, 0);
    h += e.name;
    if (m_file.write(h) != h.size() || m_file.write(payload) != payload.size()) {
        m_error = m_file.errorString();
        return false;
    }
    m_entries.append(e);
    return true;
}

bool ZipWriter::addFileFromDisk(const QString &name, const QString &path, bool compress)
{
    if (!m_file.isOpen())
        return false;
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("%1: %2").arg(path, in.errorString());
        return false;
    }
    const quint64 total = quint64(in.size());
    // above 4 GiB (or close to it, as deflate can grow a little): Zip64 sizes in the local header
    const bool big = total >= kMax32 - (1u << 20);
    Entry e;
    e.name = name.toUtf8();
    e.size = total;
    e.crc = 0;
    e.method = (compress && total > 64) ? 8 : 0;
    const QDateTime when = QFileInfo(path).lastModified();
    e.time = dosTime(when);
    e.date = dosDate(when);
    e.offset = quint64(m_file.pos());

    QByteArray h;
    put32(h, 0x04034b50);
    put16(h, big ? 45 : 20);
    put16(h, 0x0800);
    put16(h, e.method);
    put16(h, e.time);
    put16(h, e.date);
    put32(h, 0);                                  // crc, written when known
    put32(h, big ? quint32(kMax32) : 0);           // compressed size
    put32(h, big ? quint32(kMax32) : 0);           // size
    put16(h, quint16(e.name.size()));
    put16(h, big ? 20 : 0);
    h += e.name;
    if (big) {
        put16(h, 0x0001);
        put16(h, 16);
        put64(h, 0);
        put64(h, 0);
    }
    if (m_file.write(h) != h.size()) {
        m_error = m_file.errorString();
        return false;
    }
    const qint64 dataStart = m_file.pos();

    z_stream zs{};
    if (e.method == 8 && deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        m_error = QStringLiteral("zlib");
        return false;
    }
    QByteArray buf(1 << 20, Qt::Uninitialized), out(1 << 20, Qt::Uninitialized);
    uLong crc = crc32(0L, Z_NULL, 0);
    bool ok = true;
    for (;;) {
        const qint64 n = in.read(buf.data(), buf.size());
        if (n < 0) {
            m_error = QStringLiteral("%1: %2").arg(path, in.errorString());
            ok = false;
            break;
        }
        crc = crc32(crc, reinterpret_cast<const Bytef *>(buf.constData()), uInt(n));
        const bool last = n == 0 || in.atEnd();
        if (e.method == 0) {
            if (n > 0 && m_file.write(buf.constData(), n) != n) {
                m_error = m_file.errorString();
                ok = false;
                break;
            }
        } else {
            zs.next_in = reinterpret_cast<Bytef *>(buf.data());
            zs.avail_in = uInt(n);
            int r = Z_OK;
            do {
                zs.next_out = reinterpret_cast<Bytef *>(out.data());
                zs.avail_out = uInt(out.size());
                r = deflate(&zs, last ? Z_FINISH : Z_NO_FLUSH);
                const qint64 produced = out.size() - qint64(zs.avail_out);
                if (produced > 0 && m_file.write(out.constData(), produced) != produced) {
                    m_error = m_file.errorString();
                    ok = false;
                    break;
                }
            } while (ok && (zs.avail_out == 0 || (last && r != Z_STREAM_END)));
            if (!ok)
                break;
        }
        if (last)
            break;
    }
    if (e.method == 8)
        deflateEnd(&zs);
    if (!ok)
        return false;
    e.crc = quint32(crc);
    e.compSize = quint64(m_file.pos() - dataStart);
    if (!big && (e.compSize >= kMax32 || e.size >= kMax32)) {
        m_error = QStringLiteral("%1: the file grew while it was read").arg(path);
        return false;
    }
    // the crc and the sizes into the local header
    const qint64 endPos = m_file.pos();
    QByteArray patch;
    put32(patch, e.crc);
    put32(patch, big ? quint32(kMax32) : quint32(e.compSize));
    put32(patch, big ? quint32(kMax32) : quint32(e.size));
    bool patched = m_file.seek(qint64(e.offset) + 14) && m_file.write(patch) == patch.size();
    if (patched && big) {
        QByteArray z;
        put64(z, e.size);
        put64(z, e.compSize);
        patched = m_file.seek(qint64(e.offset) + 30 + e.name.size() + 4) && m_file.write(z) == z.size();
    }
    if (!patched || !m_file.seek(endPos)) {
        m_error = m_file.errorString();
        return false;
    }
    m_entries.append(e);
    return true;
}

bool ZipWriter::addDirectory(const QString &diskDir, const QString &prefix)
{
    QDir base(diskDir);
    QDirIterator it(diskDir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    bool ok = true;
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = base.relativeFilePath(path);
        const QString suffix = QFileInfo(path).suffix().toLower();
        static const QStringList packed = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("zip"),
                                           QStringLiteral("mp3"), QStringLiteral("mp4"), QStringLiteral("m4a"), QStringLiteral("ogg"),
                                           QStringLiteral("opus"), QStringLiteral("webm"), QStringLiteral("mkv"), QStringLiteral("mov"),
                                           QStringLiteral("avi"), QStringLiteral("flac"), QStringLiteral("webp"), QStringLiteral("qbpkg"),
                                           QStringLiteral("docx"), QStringLiteral("odt"), QStringLiteral("epub"), QStringLiteral("gz"),
                                           QStringLiteral("xz")};
        {
            QFile probe(path);
            if (!probe.open(QIODevice::ReadOnly)) { // cannot be read: left out, the rest is written
                m_skipped << path;
                continue;
            }
        }
        if (!addFileFromDisk(prefix.isEmpty() ? rel : prefix + QLatin1Char('/') + rel, path, !packed.contains(suffix)))
            ok = false;
    }
    return ok;
}

bool ZipWriter::close()
{
    if (m_closed)
        return true;
    m_closed = true;
    if (!m_file.isOpen())
        return false;
    const quint64 cdStart = quint64(m_file.pos());
    QByteArray cd;
    for (const Entry &e : m_entries) {
        QByteArray extra;
        const bool bigSize = e.size >= kMax32, bigComp = e.compSize >= kMax32, bigOffset = e.offset >= kMax32;
        if (bigSize || bigComp || bigOffset) {
            QByteArray v;
            if (bigSize)
                put64(v, e.size);
            if (bigComp)
                put64(v, e.compSize);
            if (bigOffset)
                put64(v, e.offset);
            put16(extra, 0x0001);
            put16(extra, quint16(v.size()));
            extra += v;
        }
        const quint16 version = extra.isEmpty() ? 20 : 45;
        put32(cd, 0x02014b50);
        put16(cd, version);
        put16(cd, version);
        put16(cd, 0x0800);
        put16(cd, e.method);
        put16(cd, e.time);
        put16(cd, e.date);
        put32(cd, e.crc);
        put32(cd, bigComp ? quint32(kMax32) : quint32(e.compSize));
        put32(cd, bigSize ? quint32(kMax32) : quint32(e.size));
        put16(cd, quint16(e.name.size()));
        put16(cd, quint16(extra.size()));
        put16(cd, 0);
        put16(cd, 0);
        put16(cd, 0);
        put32(cd, 0);
        put32(cd, bigOffset ? quint32(kMax32) : quint32(e.offset));
        cd += e.name;
        cd += extra;
    }
    const quint64 count = quint64(m_entries.size());
    const bool zip64 = count >= 0xFFFF || cdStart >= kMax32 || quint64(cd.size()) >= kMax32;
    QByteArray end;
    if (zip64) {
        const quint64 z64Start = cdStart + quint64(cd.size());
        put32(end, 0x06064b50);
        put64(end, 44);
        put16(end, 45);
        put16(end, 45);
        put32(end, 0);
        put32(end, 0);
        put64(end, count);
        put64(end, count);
        put64(end, quint64(cd.size()));
        put64(end, cdStart);
        put32(end, 0x07064b50);
        put32(end, 0);
        put64(end, z64Start);
        put32(end, 1);
    }
    put32(end, 0x06054b50);
    put16(end, 0);
    put16(end, 0);
    put16(end, zip64 ? 0xFFFF : quint16(count));
    put16(end, zip64 ? 0xFFFF : quint16(count));
    put32(end, zip64 ? quint32(kMax32) : quint32(cd.size()));
    put32(end, zip64 ? quint32(kMax32) : quint32(cdStart));
    put16(end, 0);
    bool ok = m_file.write(cd) == cd.size() && m_file.write(end) == end.size();
    if (!ok)
        m_error = m_file.errorString();
    ok = m_file.flush() && ok;
    m_file.close();
    return ok;
}

qint64 ZipWriter::verify(const QString &path, QString *error)
{
    auto fail = [&](const QString &why) -> qint64 {
        if (error)
            *error = why;
        return -1;
    };
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(f.errorString());
    const qint64 n = f.size();
    if (n < 22)
        return fail(QStringLiteral("too short"));
    const qint64 tailStart = qMax<qint64>(0, n - 65557 - 20);
    f.seek(tailStart);
    const QByteArray tail = f.read(n - tailStart);
    int eocd = -1;
    for (int i = int(tail.size()) - 22; i >= 0; --i)
        if (get32(tail, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0)
        return fail(QStringLiteral("no end of central directory"));
    quint64 count = get16(tail, eocd + 10);
    quint64 cdSize = get32(tail, eocd + 12);
    quint64 cdStart = get32(tail, eocd + 16);
    if (eocd >= 20 && get32(tail, eocd - 20) == 0x07064b50) {
        // Zip64: the locator names the Zip64 end record
        quint64 z64 = 0;
        for (int k = 0; k < 8; ++k)
            z64 |= quint64(uchar(tail.at(eocd - 20 + 8 + k))) << (8 * k);
        f.seek(qint64(z64));
        const QByteArray r = f.read(56);
        if (r.size() < 56 || get32(r, 0) != 0x06064b50)
            return fail(QStringLiteral("damaged Zip64 end record"));
        auto get64 = [&r](int pos) {
            quint64 v = 0;
            for (int k = 0; k < 8; ++k)
                v |= quint64(uchar(r.at(pos + k))) << (8 * k);
            return v;
        };
        count = get64(32);
        cdSize = get64(40);
        cdStart = get64(48);
    }
    if (cdStart + cdSize > quint64(n))
        return fail(QStringLiteral("the central directory lies outside the file"));
    f.seek(qint64(cdStart));
    const QByteArray cd = f.read(qint64(cdSize));
    quint64 found = 0;
    for (int pos = 0; pos + 46 <= cd.size(); ++found) {
        if (get32(cd, pos) != 0x02014b50)
            return fail(QStringLiteral("damaged central directory"));
        pos += 46 + get16(cd, pos + 28) + get16(cd, pos + 30) + get16(cd, pos + 32);
    }
    if (found != count)
        return fail(QStringLiteral("%1 entries listed, %2 found").arg(count).arg(found));
    return qint64(count);
}

ZipReader::ZipReader(const QString &path)
{
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        m_data = f.readAll();
        parse();
    }
}

ZipReader::ZipReader(const QByteArray &data) : m_data(data) { parse(); }

void ZipReader::parse()
{
    const int n = m_data.size();
    if (n < 22)
        return;
    int eocd = -1;
    for (int i = n - 22; i >= qMax(0, n - 65557); --i) {
        if (get32(m_data, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0)
        return;
    const int count = get16(m_data, eocd + 10);
    int pos = int(get32(m_data, eocd + 16));
    for (int i = 0; i < count; ++i) {
        if (pos + 46 > n || get32(m_data, pos) != 0x02014b50)
            return;
        Entry e;
        e.method = get16(m_data, pos + 10);
        e.compSize = get32(m_data, pos + 20);
        e.size = get32(m_data, pos + 24);
        const int nameLen = get16(m_data, pos + 28);
        const int extraLen = get16(m_data, pos + 30);
        const int commentLen = get16(m_data, pos + 32);
        e.localOffset = get32(m_data, pos + 42);
        e.name = QString::fromUtf8(m_data.mid(pos + 46, nameLen));
        m_entries.append(e);
        pos += 46 + nameLen + extraLen + commentLen;
    }
    m_valid = true;
}

QStringList ZipReader::fileNames() const
{
    QStringList l;
    for (const Entry &e : m_entries)
        l << e.name;
    return l;
}

QByteArray ZipReader::fileData(const QString &name) const
{
    for (const Entry &e : m_entries) {
        if (e.name != name)
            continue;
        const int lo = int(e.localOffset);
        if (lo + 30 > m_data.size() || get32(m_data, lo) != 0x04034b50)
            return QByteArray();
        const int nameLen = get16(m_data, lo + 26);
        const int extraLen = get16(m_data, lo + 28);
        const QByteArray comp = m_data.mid(lo + 30 + nameLen + extraLen, int(e.compSize));
        if (e.method == 0)
            return comp;
        if (e.method == 8)
            return inflateRaw(comp, e.size);
        return QByteArray();
    }
    return QByteArray();
}

bool ZipReader::extractAll(const QString &targetDir) const
{
    bool ok = true;
    for (const Entry &e : m_entries) {
        if (e.name.endsWith(QLatin1Char('/')) || e.name.contains(QLatin1String("..")))
            continue;
        const QString path = QDir(targetDir).filePath(e.name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            ok = false;
            continue;
        }
        f.write(fileData(e.name));
    }
    return ok;
}

} // namespace qb
