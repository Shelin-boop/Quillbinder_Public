#pragma once
#include <memory>
#include <QFile>
// Minimal ZIP writer/reader (deflate via zlib). Used for DOCX files and
// project backups.

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QList>
#include <QString>
#include <atomic>
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
    // 0.70.0 (audit A U-06): bytes read so far are added to *bytes; addDirectory stops when *cancel turns true
    // (both may be read and set from another thread)
    void setProgress(std::atomic<qint64> *bytes, const std::atomic<bool> *cancel) { m_bytes = bytes; m_cancel = cancel; }
    bool cancelled() const { return m_cancelled; }
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
    std::atomic<qint64> *m_bytes = nullptr;
    const std::atomic<bool> *m_cancel = nullptr;
    bool m_cancelled = false;
};

// 0.81.0 (audit of 0.80.0, U-01, F-03, F-02): the reader knows ZIP64 (archives of any size, 65,535 entries and more,
// entries of 4 GiB and more), as the writer has written it since 0.20; entries are unpacked to disk in pieces
// (any size), every file written is checked to the end (flush, sync, close: a full disk is noticed), and the names
// of the entries that could not be written are given.
class ZipReader
{
public:
    explicit ZipReader(const QString &path);
    explicit ZipReader(const QByteArray &data);
    bool isValid() const { return m_valid; }
    // why the archive is not valid: "" for a valid one
    QString errorString() const { return m_error; }
    QStringList fileNames() const;
    // The contents of an entry in memory; empty when it is not there, cut off, too large (more than 1 GiB: use
    // extractFile) or damaged (its checksum differs). ok tells an empty entry from one that could not be read (0.64.1).
    QByteArray fileData(const QString &name, bool *ok = nullptr) const;
    // One entry unpacked in pieces into the file path (written under a temporary name, checked, then put in
    // place): false with a reason, and no file at path.
    bool extractFile(const QString &name, const QString &path, QString *error = nullptr) const;
    // Every entry into targetDir; false when one or more could not be written: their names and reasons go
    // into *failed ("<name>: <reason>"), the others are written.
    bool extractAll(const QString &targetDir, QStringList *failed = nullptr) const;
    // bytes of the entries unpacked so far are added to *bytes; extractAll stops when *cancel turns true
    void setProgress(std::atomic<qint64> *bytes, const std::atomic<bool> *cancel) { m_bytes = bytes; m_cancel = cancel; }
    bool cancelled() const { return m_cancelled; }
    // the unpacked size of all entries
    quint64 totalSize() const;

private:
    struct Entry {
        QString name;
        quint16 method = 0;
        quint32 crc = 0;
        quint64 compSize = 0, size = 0;
        quint64 localOffset = 0;
    };
    void parse();
    QByteArray readAt(qint64 pos, qint64 len) const;
    qint64 dataStart(const Entry &e) const;
    bool extractEntry(const Entry &e, const QString &path, QString *error) const;
    // 0.65.0 (audit F17): a large archive is read in pieces from the open file, not read into memory
    std::shared_ptr<QFile> m_file;
    QByteArray m_data;
    qint64 m_size = 0;
    QList<Entry> m_entries;
    bool m_valid = false;
    QString m_error;
    std::atomic<qint64> *m_bytes = nullptr;
    const std::atomic<bool> *m_cancel = nullptr;
    mutable bool m_cancelled = false;
};

} // namespace qb
