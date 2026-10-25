#pragma once
// Reader of compound files ("OLE2", [MS-CFB]): the container of Word 97–2003
// documents. Only what reading needs: the streams at the top level.

#include <QByteArray>
#include <QHash>
#include <QString>

namespace qbdoc {

class CompoundFile
{
public:
    // False if the data is no compound file or is damaged; error() says why.
    bool open(const QByteArray &data);
    QString error() const { return m_error; }
    bool hasStream(const QString &name) const { return m_streams.contains(name); }
    QByteArray stream(const QString &name) const;

    static bool isCompoundFile(const QByteArray &data);

private:
    struct Entry {
        QString name;
        int type = 0;          // 1 storage, 2 stream, 5 root
        quint32 left = 0, right = 0, child = 0;
        quint32 start = 0;
        quint64 size = 0;
    };
    QByteArray chain(quint32 start, quint64 size, bool mini) const;
    quint32 next(quint32 sector, bool mini) const;
    QByteArray m_data;
    QString m_error;
    int m_sectorSize = 512;
    int m_miniSectorSize = 64;
    quint32 m_miniCutoff = 4096;
    QVector<quint32> m_fat;
    QVector<quint32> m_miniFat;
    QByteArray m_miniStream;
    QVector<Entry> m_entries;
    QHash<QString, int> m_streams;  // top-level streams -> entry
};

} // namespace qbdoc
