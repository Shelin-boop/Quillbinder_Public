#include "Cfb.h"

#include <QSet>
#include <QtEndian>

namespace qbdoc {

namespace {
constexpr quint32 kEndOfChain = 0xFFFFFFFE;
constexpr quint32 kFree = 0xFFFFFFFF;
constexpr quint32 kNoStream = 0xFFFFFFFF;

quint16 u16(const QByteArray &d, qint64 at)
{
    if (at < 0 || at + 2 > d.size())
        return 0;
    return qFromLittleEndian<quint16>(d.constData() + at);
}
quint32 u32(const QByteArray &d, qint64 at)
{
    if (at < 0 || at + 4 > d.size())
        return 0;
    return qFromLittleEndian<quint32>(d.constData() + at);
}
quint64 u64(const QByteArray &d, qint64 at)
{
    if (at < 0 || at + 8 > d.size())
        return 0;
    return qFromLittleEndian<quint64>(d.constData() + at);
}
} // namespace

bool CompoundFile::isCompoundFile(const QByteArray &data)
{
    return data.startsWith(QByteArray::fromHex("D0CF11E0A1B11AE1"));
}

quint32 CompoundFile::next(quint32 sector, bool mini) const
{
    const QVector<quint32> &fat = mini ? m_miniFat : m_fat;
    if (sector >= quint32(fat.size()))
        return kEndOfChain;
    return fat.at(int(sector));
}

QByteArray CompoundFile::chain(quint32 start, quint64 size, bool mini) const
{
    QByteArray out;
    const int ss = mini ? m_miniSectorSize : m_sectorSize;
    const QByteArray &src = mini ? m_miniStream : m_data;
    QSet<quint32> seen;
    quint32 s = start;
    while (s != kEndOfChain && s != kFree && quint64(out.size()) < size) {
        if (seen.contains(s))
            break; // a loop in a damaged file
        seen.insert(s);
        const qint64 at = mini ? qint64(s) * ss : qint64(s + 1) * ss;
        if (at < 0 || at >= src.size())
            break;
        out.append(src.mid(at, ss));
        s = next(s, mini);
    }
    if (quint64(out.size()) > size)
        out.truncate(int(size));
    return out;
}

bool CompoundFile::open(const QByteArray &data)
{
    m_data = data;
    m_error.clear();
    m_fat.clear();
    m_miniFat.clear();
    m_entries.clear();
    m_streams.clear();
    if (!isCompoundFile(data) || data.size() < 512) {
        m_error = QStringLiteral("not a compound file");
        return false;
    }
    const int shift = u16(data, 0x1E);
    if (shift != 9 && shift != 12) {
        m_error = QStringLiteral("unknown sector size");
        return false;
    }
    m_sectorSize = 1 << shift;
    m_miniSectorSize = 1 << qBound(6, int(u16(data, 0x20)), 12);
    m_miniCutoff = u32(data, 0x38) ? u32(data, 0x38) : 4096;
    const quint32 fatSectors = u32(data, 0x2C);
    const quint32 dirStart = u32(data, 0x30);
    const quint32 miniFatStart = u32(data, 0x3C);
    const quint32 miniFatSectors = u32(data, 0x40);
    quint32 difat = u32(data, 0x44);
    const quint32 difatSectors = u32(data, 0x48);

    // sectors of the FAT: 109 in the header, the rest in the DIFAT chain
    QVector<quint32> fatList;
    for (int i = 0; i < 109; ++i) {
        const quint32 s = u32(data, 0x4C + i * 4);
        if (s != kFree)
            fatList << s;
    }
    const int perDifat = m_sectorSize / 4 - 1;
    for (quint32 n = 0; n < difatSectors && difat != kEndOfChain && difat != kFree && n < 100000; ++n) {
        const qint64 at = qint64(difat + 1) * m_sectorSize;
        if (at + m_sectorSize > data.size())
            break;
        for (int i = 0; i < perDifat; ++i) {
            const quint32 s = u32(data, at + i * 4);
            if (s != kFree)
                fatList << s;
        }
        difat = u32(data, at + perDifat * 4);
    }
    if (quint32(fatList.size()) > fatSectors && fatSectors > 0)
        fatList.resize(int(fatSectors));
    for (quint32 s : fatList) {
        const qint64 at = qint64(s + 1) * m_sectorSize;
        for (int i = 0; i < m_sectorSize / 4; ++i)
            m_fat << u32(data, at + i * 4);
    }
    if (m_fat.isEmpty()) {
        m_error = QStringLiteral("the file has no allocation table");
        return false;
    }
    // directory
    const QByteArray dir = chain(dirStart, quint64(m_fat.size()) * m_sectorSize, false);
    for (int at = 0; at + 128 <= dir.size(); at += 128) {
        Entry e;
        const int nameLen = qMin<int>(u16(dir, at + 0x40), 64);
        QString name;
        for (int i = 0; i + 2 <= nameLen - 2; i += 2)
            name.append(QChar(u16(dir, at + i)));
        e.name = name;
        e.type = quint8(dir.at(at + 0x42));
        e.left = u32(dir, at + 0x44);
        e.right = u32(dir, at + 0x48);
        e.child = u32(dir, at + 0x4C);
        e.start = u32(dir, at + 0x74);
        e.size = shift == 9 ? u32(dir, at + 0x78) : u64(dir, at + 0x78);
        m_entries << e;
    }
    if (m_entries.isEmpty() || m_entries.first().type != 5) {
        m_error = QStringLiteral("the file has no root entry");
        return false;
    }
    // mini stream and its allocation table
    const Entry &root = m_entries.first();
    m_miniStream = chain(root.start, root.size, false);
    const QByteArray mf = chain(miniFatStart, quint64(miniFatSectors) * m_sectorSize, false);
    for (int i = 0; i + 4 <= mf.size(); i += 4)
        m_miniFat << u32(mf, i);
    // the streams directly inside the root storage (a red-black tree of siblings)
    QVector<quint32> todo;
    if (root.child != kNoStream)
        todo << root.child;
    QSet<quint32> seen;
    while (!todo.isEmpty()) {
        const quint32 i = todo.takeLast();
        if (i >= quint32(m_entries.size()) || seen.contains(i))
            continue;
        seen.insert(i);
        const Entry &e = m_entries.at(int(i));
        if (e.type == 2 && !m_streams.contains(e.name))
            m_streams.insert(e.name, int(i));
        if (e.left != kNoStream)
            todo << e.left;
        if (e.right != kNoStream)
            todo << e.right;
    }
    return true;
}

QByteArray CompoundFile::stream(const QString &name) const
{
    const int i = m_streams.value(name, -1);
    if (i < 0)
        return QByteArray();
    const Entry &e = m_entries.at(i);
    const quint64 size = qMin<quint64>(e.size, quint64(m_data.size()) * 2);
    return chain(e.start, size, size < m_miniCutoff);
}

} // namespace qbdoc
