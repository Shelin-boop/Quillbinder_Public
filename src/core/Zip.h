#pragma once
// Minimal ZIP writer/reader (deflate via zlib). Used for DOCX files and
// project backups.

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QList>
#include <QString>
#include <QStringList>

namespace qb {

class ZipWriter
{
public:
    explicit ZipWriter(const QString &path);
    ~ZipWriter();
    bool isOpen() const { return m_file.isOpen(); }
    bool addFile(const QString &name, const QByteArray &data, bool compress = true,
                 const QDateTime &time = QDateTime::currentDateTime());
    // A file from disk, read and compressed in pieces (any size; Zip64 above 4 GiB).
    bool addFileFromDisk(const QString &name, const QString &path, bool compress = true);
    bool addDirectory(const QString &diskDir, const QString &prefix); // recursive
    bool close();
    QString errorString() const { return m_error; }
    // files addDirectory could not read (they are left out; the rest is written)
    QStringList skipped() const { return m_skipped; }
    // Reads the end of a written ZIP file and its directory (not the data): the
    // number of entries, or -1 with error when the file is not a complete ZIP file.
    static qint64 verify(const QString &path, QString *error = nullptr);

private:
    struct Entry {
        QByteArray name;
        quint32 crc;
        quint64 compSize;
        quint64 size;
        quint16 method;
        quint16 time, date;
        quint64 offset;
    };
    QFile m_file;
    QList<Entry> m_entries;
    QString m_error;
    QStringList m_skipped;
    bool m_closed = false;
};

class ZipReader
{
public:
    explicit ZipReader(const QString &path);
    explicit ZipReader(const QByteArray &data);
    bool isValid() const { return m_valid; }
    QStringList fileNames() const;
    QByteArray fileData(const QString &name) const;
    bool extractAll(const QString &targetDir) const;

private:
    struct Entry {
        QString name;
        quint16 method;
        quint32 compSize, size;
        quint32 localOffset;
    };
    void parse();
    QByteArray m_data;
    QList<Entry> m_entries;
    bool m_valid = false;
};

} // namespace qb
