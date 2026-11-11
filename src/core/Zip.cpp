#include "core/Zip.h"
#include <QtGlobal>
#include <cerrno>
#include <cstring>
#ifdef Q_OS_UNIX
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <io.h>
#endif
#include "core/SaveBatch.h"
#include "core/PathSafety.h"
#include "core/SafeSave.h"

#include <QDir>
#include <limits>
#include <QDirIterator>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
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

static quint64 get64(const QByteArray &b, int pos)
{
    quint64 v = 0;
    for (int k = 0; k < 8; ++k)
        v |= quint64(uchar(b[pos + k])) << (8 * k);
    return v;
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

// 0.64.1 (audit ST-1): at most this much per entry (a declared size is not trusted further)
static constexpr quint32 kMaxEntrySize = 1024u * 1024u * 1024u;

static QByteArray inflateRaw(const QByteArray &in, quint64 size)
{
    if (size > kMaxEntrySize)
        return QByteArray();
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK)
        return QByteArray();
    QByteArray out;
    out.resize(qsizetype(size));
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.constData()));
    zs.avail_in = uInt(in.size());
    zs.next_out = reinterpret_cast<Bytef *>(out.data());
    zs.avail_out = uInt(out.size());
    const int r = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    // 0.64.1 (audit ST-1): a stream that does not end where the entry says is damaged
    if (r != Z_STREAM_END || zs.total_out != size)
        return QByteArray();
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
        // 0.70.0: progress and Cancel per piece of a megabyte, so that one large file (a video, a recording) neither
        // stops the progress bar nor keeps Cancel waiting until it is packed
        if (m_bytes && n > 0)
            m_bytes->fetch_add(n);
        if (m_cancel && m_cancel->load()) {
            m_cancelled = true;
            m_error = QObject::tr("cancelled");
            ok = false;
            break;
        }
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
    // 0.81.0 (audit of 0.80.0, F-05): a folder that is a link to a folder elsewhere is followed and packed as a
    // folder (before, the walk passed it over without a word, and the backup lacked its files without the
    // "(incomplete)" mark); a link back to a folder already packed or to a folder around the project (a loop), and a
    // link whose target is not there, are listed as skipped, so the backup says it is incomplete.
    QStringList files;
    {
        const QString rootCanon = QFileInfo(diskDir).canonicalFilePath();
        QSet<QString> visited{rootCanon};
        QStringList pending{diskDir};
        while (!pending.isEmpty()) {
            const QString dir = pending.takeFirst();
            const QFileInfoList list = QDir(dir).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                                                               QDir::Name);
            for (const QFileInfo &fi : list) {
                if (fi.isDir()) {
                    const QString canon = fi.canonicalFilePath();
                    if (fi.isSymLink()) {
                        const bool loop = canon.isEmpty() || visited.contains(canon) || rootCanon == canon
                                          || rootCanon.startsWith(canon + QLatin1Char('/'));
                        if (loop) {
                            m_skipped << fi.filePath();
                            continue;
                        }
                    }
                    visited.insert(canon);
                    pending << fi.filePath();
                } else if (fi.isFile()) {
                    files << fi.filePath();
                } else if (fi.isSymLink() && !fi.exists()) {
                    m_skipped << fi.filePath(); // a link to something that is not there (a drive not attached)
                }
                // sockets, pipes and devices are no project data (reading a pipe would wait for ever)
            }
        }
    }
    bool ok = true;
    for (const QString &path : std::as_const(files)) {
        if (m_cancel && m_cancel->load()) {
            m_cancelled = true;
            m_error = QObject::tr("cancelled");
            return false;
        }
        const QString rel = base.relativeFilePath(path);
        if (savebatch::isStaged(path) || rel == QLatin1String("settings/journal.json"))
            continue; // a save in progress (0.65.0): not project data
        if (rel == QLatin1String("settings/lock") || rel.endsWith(QLatin1String("/quillbinder.lock"))
            || rel == QLatin1String("quillbinder.lock"))
            continue; // 0.70.0 (audit A U-09): who has the project open (computer and login name) stays out
        const QString suffix = QFileInfo(path).suffix().toLower();
        static const QStringList packed = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("zip"),
                                           QStringLiteral("mp3"), QStringLiteral("mp4"), QStringLiteral("m4a"), QStringLiteral("ogg"),
                                           QStringLiteral("opus"), QStringLiteral("webm"), QStringLiteral("mkv"), QStringLiteral("mov"),
                                           QStringLiteral("avi"), QStringLiteral("flac"), QStringLiteral("webp"), QStringLiteral("qbpkg"),
                                           QStringLiteral("docx"), QStringLiteral("odt"), QStringLiteral("epub"), QStringLiteral("gz"),
                                           QStringLiteral("xz")};
        {
            QFile probe(path);
            bool unreadable = !probe.open(QIODevice::ReadOnly);
#ifdef QB_TEST_SWITCHES
            // the checks (0.65.0): a file that counts as unreadable (the test machine's root reads every file)
            const QString fake = qEnvironmentVariable("QB_TEST_UNREADABLE");
            unreadable = unreadable || (!fake.isEmpty() && path.endsWith(fake));
#endif
            if (unreadable) { // cannot be read: left out, the rest is written
                m_skipped << path;
                continue;
            }
        }
        if (!addFileFromDisk(prefix.isEmpty() ? rel : prefix + QLatin1Char('/') + rel, path, !packed.contains(suffix)))
            ok = false;
        if (m_cancelled)
            return false;
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
#ifdef Q_OS_UNIX
    // 0.70.0 (audit A F-11): the archive is on the disk before older backups are deleted
    if (ok && ::fsync(m_file.handle()) != 0 && errno != EINVAL && errno != ENOTSUP && errno != EOPNOTSUPP && errno != ENOSYS) {
        ok = false;
        m_error = QString::fromLocal8Bit(std::strerror(errno));
    }
#elif defined(Q_OS_WIN)
    if (ok && ::_commit(m_file.handle()) != 0)
        ok = false;
#endif
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
        count = get64(r, 32);
        cdSize = get64(r, 40);
        cdStart = get64(r, 48);
    }
    if (cdStart + cdSize > quint64(n))
        return fail(QStringLiteral("the central directory lies outside the file"));
    f.seek(qint64(cdStart));
    const QByteArray cd = f.read(qint64(cdSize));
    struct Listed {
        quint16 method;
        quint32 crc;
        quint64 compSize, size, offset;
        QByteArray name;
    };
    QList<Listed> listed;
    quint64 found = 0;
    for (int pos = 0; pos + 46 <= cd.size(); ++found) {
        if (get32(cd, pos) != 0x02014b50)
            return fail(QStringLiteral("damaged central directory"));
        const int nameLen = get16(cd, pos + 28), extraLen = get16(cd, pos + 30), commentLen = get16(cd, pos + 32);
        if (pos + 46 + nameLen + extraLen > cd.size())
            return fail(QStringLiteral("damaged central directory"));
        Listed l;
        l.method = get16(cd, pos + 10);
        l.crc = get32(cd, pos + 16);
        l.compSize = get32(cd, pos + 20);
        l.size = get32(cd, pos + 24);
        l.offset = get32(cd, pos + 42);
        l.name = cd.mid(pos + 46, nameLen);
        // Zip64 (large files): the real values in the extra field 0x0001, in this order, where 0xFFFFFFFF
        for (int x = pos + 46 + nameLen; x + 4 <= pos + 46 + nameLen + extraLen;) {
            const int id = get16(cd, x), len = get16(cd, x + 2);
            if (id == 0x0001) {
                int at = x + 4;
                auto take = [&](quint64 *v) {
                    if (*v == 0xFFFFFFFFu && at + 8 <= x + 4 + len && at + 8 <= cd.size()) {
                        quint64 r = 0;
                        for (int k = 0; k < 8; ++k)
                            r |= quint64(uchar(cd.at(at + k))) << (8 * k);
                        *v = r;
                        at += 8;
                    }
                };
                take(&l.size);
                take(&l.compSize);
                take(&l.offset);
            }
            x += 4 + len;
        }
        listed << l;
        pos += 46 + nameLen + extraLen + commentLen;
    }
    if (found != count)
        return fail(QStringLiteral("%1 entries listed, %2 found").arg(count).arg(found));
    // 0.64.1 (audit S-3): every entry is read back once and its contents checked against its CRC-32
    // (before, only the table of contents was checked)
    constexpr qint64 kChunk = 1 << 20;
    QByteArray outBuf(int(kChunk), Qt::Uninitialized);
    for (const Listed &l : std::as_const(listed)) {
        if (!f.seek(qint64(l.offset)))
            return fail(QStringLiteral("cannot read %1").arg(QString::fromUtf8(l.name)));
        const QByteArray lh = f.read(30);
        if (lh.size() < 30 || get32(lh, 0) != 0x04034b50)
            return fail(QStringLiteral("damaged entry %1").arg(QString::fromUtf8(l.name)));
        const qint64 dataStart = qint64(l.offset) + 30 + get16(lh, 26) + get16(lh, 28);
        if (quint64(dataStart) + l.compSize > quint64(n) || !f.seek(dataStart))
            return fail(QStringLiteral("entry %1 lies outside the file").arg(QString::fromUtf8(l.name)));
        uLong crc = crc32(0L, Z_NULL, 0);
        quint64 left = l.compSize, produced = 0;
        z_stream zs{};
        const bool deflated = l.method == 8;
        if (deflated && inflateInit2(&zs, -MAX_WBITS) != Z_OK)
            return fail(QStringLiteral("cannot unpack %1").arg(QString::fromUtf8(l.name)));
        int r = Z_OK;
        while (left > 0) {
            const QByteArray in = f.read(qMin<qint64>(kChunk, qint64(left)));
            if (in.isEmpty())
                break;
            left -= quint64(in.size());
            if (!deflated) {
                crc = crc32(crc, reinterpret_cast<const Bytef *>(in.constData()), uInt(in.size()));
                produced += quint64(in.size());
                continue;
            }
            zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.constData()));
            zs.avail_in = uInt(in.size());
            do {
                zs.next_out = reinterpret_cast<Bytef *>(outBuf.data());
                zs.avail_out = uInt(outBuf.size());
                r = inflate(&zs, Z_NO_FLUSH);
                const uInt got = uInt(outBuf.size()) - zs.avail_out;
                crc = crc32(crc, reinterpret_cast<const Bytef *>(outBuf.constData()), got);
                produced += got;
            } while (r == Z_OK && zs.avail_out == 0);
            if (r != Z_OK && r != Z_STREAM_END && r != Z_BUF_ERROR)
                break;
        }
        if (deflated)
            inflateEnd(&zs);
        if (left != 0 || produced != l.size || quint32(crc) != l.crc || (deflated && r != Z_STREAM_END))
            return fail(QStringLiteral("the contents of %1 do not match its checksum").arg(QString::fromUtf8(l.name)));
    }
    return qint64(count);
}

ZipReader::ZipReader(const QString &path) : m_file(std::make_shared<QFile>(path))
{
    if (!m_file->open(QIODevice::ReadOnly)) {
        m_error = m_file->errorString();
        m_file.reset();
        return;
    }
    m_size = m_file->size();
    // small archives are read whole and the file is closed (another program may change it afterwards); large ones
    // stay open and are read in pieces where needed (0.81.0: of any size, ZIP64; 0.65.0 to 0.80.0 mapped them, and
    // refused 2 GiB and more)
    constexpr qint64 kReadWholeUpTo = 64ll * 1024 * 1024;
    if (m_size <= kReadWholeUpTo) {
        m_data = m_file->readAll();
        m_file->close();
        m_file.reset();
        if (m_data.size() != m_size) {
            m_error = QObject::tr("the file could not be read whole");
            m_data.clear();
            return;
        }
    }
    parse();
}

ZipReader::ZipReader(const QByteArray &data) : m_data(data), m_size(data.size()) { parse(); }

QByteArray ZipReader::readAt(qint64 pos, qint64 len) const
{
    if (pos < 0 || len <= 0 || pos > m_size || len > m_size - pos)
        return QByteArray();
    if (m_file) {
        if (!m_file->seek(pos))
            return QByteArray();
        QByteArray b = m_file->read(len);
        return b.size() == len ? b : QByteArray();
    }
    return m_data.mid(qsizetype(pos), qsizetype(len));
}

void ZipReader::parse()
{
    m_valid = false;
    const qint64 n = m_size;
    auto bad = [this](const QString &why) {
        m_entries.clear();
        m_error = why;
    };
    if (n < 22)
        return bad(QObject::tr("too short for a ZIP file"));
    // the end record is in the last 64 KiB (its comment may be up to 65,535 bytes), the ZIP64 locator 20 bytes before it
    const qint64 tailStart = qMax<qint64>(0, n - 65557 - 20);
    const QByteArray tail = readAt(tailStart, n - tailStart);
    if (tail.size() != n - tailStart)
        return bad(QObject::tr("the file could not be read"));
    int eocd = -1;
    for (int i = int(tail.size()) - 22; i >= 0; --i)
        if (get32(tail, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0)
        return bad(QObject::tr("no end record: the file is cut off, or it is no ZIP file"));
    quint64 count = get16(tail, eocd + 10);
    quint64 cdSize = get32(tail, eocd + 12);
    quint64 cdStart = get32(tail, eocd + 16);
    if (eocd >= 20 && get32(tail, eocd - 20) == 0x07064b50) {
        // ZIP64: the locator names the ZIP64 end record, which holds the 64-bit count, size and place
        const quint64 z64 = get64(tail, eocd - 20 + 8);
        const QByteArray r = z64 < quint64(n) ? readAt(qint64(z64), 56) : QByteArray();
        if (r.size() < 56 || get32(r, 0) != 0x06064b50)
            return bad(QObject::tr("damaged ZIP64 end record"));
        count = get64(r, 32);
        cdSize = get64(r, 40);
        cdStart = get64(r, 48);
    }
    // 0.64.1 (audit ST-1): every offset and length from the file is checked against its size before it is used
    if (cdStart > quint64(n) || cdSize > quint64(n) - cdStart)
        return bad(QObject::tr("the table of contents lies outside the file"));
    // the table of contents is read whole: at most 512 MiB (millions of entries)
    if (cdSize > 512ull * 1024 * 1024 || count > cdSize / 46 + (cdSize == 0 ? 0 : 1))
        return bad(QObject::tr("damaged table of contents"));
    const QByteArray cd = cdSize == 0 ? QByteArray() : readAt(qint64(cdStart), qint64(cdSize));
    if (quint64(cd.size()) != cdSize)
        return bad(QObject::tr("the table of contents could not be read"));
    m_entries.reserve(qsizetype(count));
    qint64 pos = 0;
    for (quint64 i = 0; i < count; ++i) {
        if (pos + 46 > cd.size() || get32(cd, int(pos)) != 0x02014b50)
            return bad(QObject::tr("damaged table of contents"));
        const int at = int(pos);
        Entry e;
        e.method = get16(cd, at + 10);
        e.crc = get32(cd, at + 16);
        e.compSize = get32(cd, at + 20);
        e.size = get32(cd, at + 24);
        const int nameLen = get16(cd, at + 28);
        const int extraLen = get16(cd, at + 30);
        const int commentLen = get16(cd, at + 32);
        e.localOffset = get32(cd, at + 42);
        if (pos + 46 + nameLen + extraLen > cd.size())
            return bad(QObject::tr("damaged table of contents"));
        e.name = QString::fromUtf8(cd.mid(at + 46, nameLen));
        // ZIP64: the real values are in the extra field 0x0001, in this order, for each field that holds 0xFFFFFFFF
        const int extraEnd = at + 46 + nameLen + extraLen;
        for (int x = at + 46 + nameLen; x + 4 <= extraEnd;) {
            const int id = get16(cd, x), len = get16(cd, x + 2);
            if (x + 4 + len > extraEnd)
                break;
            if (id == 0x0001) {
                int p = x + 4;
                const int end = x + 4 + len;
                auto take = [&](quint64 *v) {
                    if (*v == 0xFFFFFFFFu && p + 8 <= end) {
                        *v = get64(cd, p);
                        p += 8;
                    }
                };
                take(&e.size);
                take(&e.compSize);
                take(&e.localOffset);
            }
            x += 4 + len;
        }
        m_entries.append(e);
        pos += qint64(46) + nameLen + extraLen + commentLen;
    }
    m_error.clear();
    m_valid = true;
}

QStringList ZipReader::fileNames() const
{
    QStringList l;
    for (const Entry &e : m_entries)
        l << e.name;
    return l;
}

quint64 ZipReader::totalSize() const
{
    quint64 n = 0;
    for (const Entry &e : m_entries)
        n += e.size;
    return n;
}

qint64 ZipReader::dataStart(const Entry &e) const
{
    if (e.localOffset >= quint64(m_size))
        return -1;
    const QByteArray lh = readAt(qint64(e.localOffset), 30);
    if (lh.size() < 30 || get32(lh, 0) != 0x04034b50)
        return -1;
    const qint64 start = qint64(e.localOffset) + 30 + get16(lh, 26) + get16(lh, 28);
    if (start > m_size || e.compSize > quint64(m_size - start))
        return -1; // cut off, or larger than the file
    return start;
}

QByteArray ZipReader::fileData(const QString &name, bool *ok) const
{
    if (ok)
        *ok = false;
    for (const Entry &e : m_entries) {
        if (e.name != name)
            continue;
        if (e.compSize > kMaxEntrySize || e.size > kMaxEntrySize)
            return QByteArray(); // in memory at most 1 GiB: larger entries go to disk with extractFile
        const qint64 start = dataStart(e);
        if (start < 0)
            return QByteArray();
        const QByteArray comp = readAt(start, qint64(e.compSize));
        if (quint64(comp.size()) != e.compSize)
            return QByteArray();
        QByteArray out;
        if (e.method == 0 && e.compSize == e.size)
            out = comp;
        else if (e.method == 8)
            out = inflateRaw(comp, e.size);
        else
            return QByteArray();
        if (quint64(out.size()) != e.size)
            return QByteArray(); // inflateRaw failed (it gives exactly the size or nothing)
        // 0.64.1 (audit ST-1): the contents must match the checksum the archive gives for them
        if (quint32(crc32(0L, reinterpret_cast<const Bytef *>(out.constData()), uInt(out.size()))) != e.crc)
            return QByteArray();
        if (ok)
            *ok = true;
        return out;
    }
    return QByteArray();
}

bool ZipReader::extractEntry(const Entry &e, const QString &path, QString *error) const
{
    auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    const qint64 start = dataStart(e);
    if (start < 0)
        return fail(QObject::tr("damaged in the archive"));
    const bool deflated = e.method == 8;
    if (!deflated && (e.method != 0 || e.compSize != e.size))
        return fail(QObject::tr("packed in a way this program cannot unpack"));
    // 0.81.0 (audit of 0.80.0, F-02): written under a temporary name, synced and checked to the end, then put in
    // place (a plain QFile showed a full disk only at its unchecked close: the restore reported success with
    // thousands of files cut short or empty)
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return fail(out.errorString());
    z_stream zs{};
    if (deflated && inflateInit2(&zs, -MAX_WBITS) != Z_OK)
        return fail(QStringLiteral("zlib"));
    constexpr qint64 kChunk = 1 << 20;
    QByteArray outBuf(int(kChunk), Qt::Uninitialized);
    uLong crc = crc32(0L, Z_NULL, 0);
    quint64 left = e.compSize, produced = 0;
    qint64 at = start;
    int r = Z_OK;
    bool damaged = false;
    QString writeError;
    auto put = [&](const char *d, qint64 len) {
        if (len <= 0)
            return true;
        if (out.write(d, len) != len) {
            writeError = out.errorString();
            return false;
        }
        if (m_bytes)
            m_bytes->fetch_add(len);
        return true;
    };
    while (left > 0 && writeError.isEmpty() && !damaged) {
        if (m_cancel && m_cancel->load()) {
            m_cancelled = true;
            break;
        }
        const QByteArray in = readAt(at, qMin<qint64>(kChunk, qint64(left)));
        if (in.isEmpty()) {
            damaged = true;
            break;
        }
        at += in.size();
        left -= quint64(in.size());
        if (!deflated) {
            crc = crc32(crc, reinterpret_cast<const Bytef *>(in.constData()), uInt(in.size()));
            produced += quint64(in.size());
            put(in.constData(), in.size());
            continue;
        }
        zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.constData()));
        zs.avail_in = uInt(in.size());
        do {
            zs.next_out = reinterpret_cast<Bytef *>(outBuf.data());
            zs.avail_out = uInt(outBuf.size());
            r = inflate(&zs, Z_NO_FLUSH);
            if (r != Z_OK && r != Z_STREAM_END && r != Z_BUF_ERROR) {
                damaged = true;
                break;
            }
            const uInt got = uInt(outBuf.size()) - zs.avail_out;
            produced += got;
            if (produced > e.size) { // more than the entry says it holds: damaged (the declared size is the limit)
                damaged = true;
                break;
            }
            crc = crc32(crc, reinterpret_cast<const Bytef *>(outBuf.constData()), got);
            if (!put(outBuf.constData(), got))
                break;
        } while (r == Z_OK && zs.avail_out == 0);
    }
    if (deflated)
        inflateEnd(&zs);
    if (m_cancelled) {
        out.cancelWriting();
        return fail(QObject::tr("cancelled"));
    }
    if (!writeError.isEmpty()) {
        out.cancelWriting();
        return fail(writeError);
    }
    if (damaged || left != 0 || produced != e.size || quint32(crc) != e.crc || (deflated && r != Z_STREAM_END)) {
        out.cancelWriting();
        return fail(QObject::tr("damaged in the archive (its contents do not match its checksum)"));
    }
    // the last buffered bytes: a full disk shows here for a small file (commit() would report only that it cancelled)
    if (!out.flush()) {
        const QString why = out.errorString();
        out.cancelWriting();
        return fail(why.isEmpty() ? QObject::tr("could not be written to the disk") : why);
    }
    if (!safesave::commit(out)) {
        const QString why = out.errorString();
        return fail(why.isEmpty() || why == QLatin1String("Unknown error") ? QObject::tr("could not be written to the disk") : why);
    }
    return true;
}

bool ZipReader::extractFile(const QString &name, const QString &path, QString *error) const
{
    for (const Entry &e : m_entries)
        if (e.name == name)
            return extractEntry(e, path, error);
    if (error)
        *error = QObject::tr("not in the archive");
    return false;
}

bool ZipReader::extractAll(const QString &targetDir, QStringList *failed) const
{
    bool ok = true;
    auto note = [&](const QString &name, const QString &why) {
        ok = false;
        if (failed)
            *failed << QStringLiteral("%1: %2").arg(name, why);
    };
    for (const Entry &e : m_entries) {
        if (m_cancel && m_cancel->load()) {
            m_cancelled = true;
            return false;
        }
        if (e.name.endsWith(QLatin1Char('/')))
            continue;
        // 0.64.1 (audit X-1): absolute names, drive letters, backslashes and "." or ".." parts are refused
        // (an absolute name was written exactly there, outside the target folder), and what is written
        // must lie inside the target folder
        if (!pathsafety::isSafeRelative(e.name)) {
            note(e.name, QObject::tr("a name that is not allowed"));
            continue;
        }
        const QString path = QDir(targetDir).filePath(e.name);
        if (!pathsafety::isInside(path, targetDir)) {
            note(e.name, QObject::tr("a name that is not allowed"));
            continue;
        }
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            note(e.name, QObject::tr("its folder could not be made"));
            continue;
        }
        // an entry that cannot be read whole and unchanged is not written (not even as an empty file)
        QString why;
        if (!extractEntry(e, path, &why)) {
            if (m_cancelled)
                return false;
            note(e.name, why);
        }
    }
    return ok;
}

} // namespace qb
