#include "SaveBatch.h"

#include "PathSafety.h"
#include "SafeSave.h"
#include "SyncSafety.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSet>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <io.h>
#include <windows.h>
#endif

namespace qb {
namespace savebatch {

namespace {

Batch *g_current = nullptr;

QByteArray shaOfFile(const QString &path, bool *exists = nullptr)
{
    QFile f(path);
    if (exists)
        *exists = f.exists();
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f))
        return QByteArray();
    return h.result().toHex();
}

QByteArray shaOf(const QByteArray &data) { return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex(); }

// the folder's entry of a renamed or new file reaches the disk (POSIX: the rename itself is in the folder)
void syncFolder(const QString &dir)
{
#ifdef Q_OS_UNIX
    const int fd = ::open(QFile::encodeName(dir).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#else
    Q_UNUSED(dir);
#endif
}

bool writeSynced(const QString &path, const QByteArray &data, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QObject::tr("Cannot write %1: %2").arg(QDir::toNativeSeparators(path), f.errorString());
        return false;
    }
    bool ok = f.write(data) == data.size() && f.flush();
#ifdef Q_OS_UNIX
    if (ok && ::fsync(f.handle()) != 0 && errno != EINVAL && errno != ENOTSUP && errno != EOPNOTSUPP && errno != ENOSYS)
        ok = false;
    if (ok) {
        const int copy = ::dup(f.handle());
        if (copy >= 0 && ::close(copy) != 0 && errno != EINTR)
            ok = false;
    }
#elif defined(Q_OS_WIN)
    if (ok && ::_commit(f.handle()) != 0)
        ok = false;
#endif
    const QString why = f.errorString();
    f.close();
    if (!ok || f.error() != QFileDevice::NoError) {
        QFile::remove(path);
        if (error)
            *error = QObject::tr("Cannot write %1: %2")
                         .arg(QDir::toNativeSeparators(path),
                              why.isEmpty() || why == QLatin1String("Unknown error") ? QObject::tr("the disk may be full") : why);
        return false;
    }
    return true;
}

QString relativeTo(const QString &folder, const QString &path) { return QDir(folder).relativeFilePath(path); }

} // namespace

void syncDir(const QString &dir) { syncFolder(dir); }

bool isStaged(const QString &fileName) { return fileName.endsWith(QLatin1String(kStageSuffix)); }

bool replace(const QString &from, const QString &to)
{
#ifdef Q_OS_WIN
    return ::MoveFileExW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(from).utf16()),
                         reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(to).utf16()),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
           != 0;
#else
    return ::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0;
#endif
}

// ------------------------------------------------------------------ batch

Batch::Batch(const QString &folder, const QString &journal) : m_folder(folder), m_journal(journal) {}

Batch::~Batch()
{
    if (!m_done)
        drop();
}

bool Batch::contains(const QString &target) const
{
    for (const Item &i : m_items)
        if (i.target == target)
            return true;
    return false;
}

bool Batch::stage(const QString &target, const QByteArray &data, const QString &title, const QString &place,
                  QString *error)
{
    Item item;
    item.target = target;
    item.staged = target + QLatin1String(kStageSuffix);
    item.title = title;
    item.place = place;
    item.sha = shaOf(data);
    item.size = data.size();
    bool exists = false;
    item.before = shaOfFile(target, &exists);
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (!writeSynced(item.staged, data, error))
        return false;
    if (exists)
        QFile::setPermissions(item.staged, QFile::permissions(target));
    for (Item &i : m_items)
        if (i.target == target) {
            i = item; // staged again in the same save: the newer content counts
            return true;
        }
    m_items << item;
    return true;
}

void Batch::after(std::function<void()> done)
{
    if (done)
        m_after << std::move(done);
}

bool Batch::commit(QString *error)
{
    if (m_done)
        return true;
    bool journal = false;
    if (m_items.size() >= 2) {
        QJsonArray files;
        for (const Item &i : std::as_const(m_items)) {
            QJsonObject o;
            o.insert(QStringLiteral("file"), relativeTo(m_folder, i.target));
            o.insert(QStringLiteral("staged"), relativeTo(m_folder, i.staged));
            o.insert(QStringLiteral("sha256"), QString::fromLatin1(i.sha));
            o.insert(QStringLiteral("replaces"), QString::fromLatin1(i.before));
            o.insert(QStringLiteral("size"), double(i.size));
            if (!i.title.isEmpty())
                o.insert(QStringLiteral("title"), i.title);
            if (!i.place.isEmpty())
                o.insert(QStringLiteral("place"), i.place);
            files.append(o);
        }
        QJsonObject root;
        root.insert(QStringLiteral("what"), QStringLiteral("A save of Quillbinder in progress: the files below are written "
                                                         "beside their targets and then put in place. If this file is "
                                                         "still here, the next opening finishes the save."));
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("device"), syncsafe::deviceName());
        root.insert(QStringLiteral("key"), syncsafe::machineKey());
        root.insert(QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        root.insert(QStringLiteral("files"), files);
        QDir().mkpath(QFileInfo(m_journal).absolutePath());
        // 0.70.0 (audit A U-10): the folders of the staged files reach the disk before the journal names them
        {
            QSet<QString> staged;
            for (const Item &i : std::as_const(m_items))
                staged.insert(QFileInfo(i.staged).absolutePath());
            for (const QString &d : std::as_const(staged))
                syncFolder(d);
        }
        QString why;
        if (!safesave::writeFile(m_journal, QJsonDocument(root).toJson(), &why)) {
            if (error)
                *error = QObject::tr("The save could not be started (its journal was not written): %1").arg(why);
            drop();
            return false;
        }
        syncFolder(QFileInfo(m_journal).absolutePath());
        journal = true;
        m_journalWritten = true;
    }
    QSet<QString> dirs;
    for (const Item &i : std::as_const(m_items)) {
        if (!replace(i.staged, i.target)) {
            if (error)
                *error = journal ? QObject::tr("%1 could not be put in place; the journal of the save keeps it, and the "
                                               "next save or opening finishes it.")
                                       .arg(QDir::toNativeSeparators(i.target))
                                 : QObject::tr("%1 could not be put in place.").arg(QDir::toNativeSeparators(i.target));
            if (!journal)
                drop();
            else
                m_done = true; // the temporary files stay for recover()
            return false;
        }
        dirs.insert(QFileInfo(i.target).absolutePath());
    }
    for (const QString &d : std::as_const(dirs))
        syncFolder(d);
    if (journal) {
        QFile::remove(m_journal);
        syncFolder(QFileInfo(m_journal).absolutePath());
        m_journalWritten = false;
    }
    m_done = true;
    const auto steps = m_after;
    m_after.clear();
    for (const auto &f : steps)
        f();
    return true;
}

void Batch::drop()
{
    if (!m_journalWritten) // else the journal names them: the next save or opening finishes them
        for (const Item &i : std::as_const(m_items))
            QFile::remove(i.staged);
    m_items.clear();
    m_after.clear();
    m_done = true;
}

Batch *current() { return g_current; }

Scope::Scope(Batch *b) : m_prev(g_current) { g_current = b; }
Scope::~Scope() { g_current = m_prev; }

bool put(const QString &path, const QByteArray &data, QString *error, const QString &title, const QString &place,
         std::function<void()> done)
{
    // a file of another project (a second window's save started from this one) is not this batch's
    Batch *b = current();
    if (b && !QDir::cleanPath(QFileInfo(path).absoluteFilePath()).startsWith(QDir::cleanPath(QFileInfo(b->folder()).absoluteFilePath()) + QLatin1Char('/')))
        b = nullptr;
    if (b) {
        if (!b->stage(path, data, title, place, error))
            return false;
        b->after(std::move(done));
        return true;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QString why;
    if (!safesave::writeFile(path, data, &why)) {
        if (error)
            *error = QObject::tr("Cannot write %1").arg(why);
        return false;
    }
    if (done)
        done();
    return true;
}

// ------------------------------------------------------------------ recovery

QString Recovery::text() const
{
    QStringList parts;
    if (!finished.isEmpty())
        parts << QObject::tr("The last save of this project was cut off (the program ended, or the computer stopped). It "
                             "was finished now; these files were put in place:\n%1")
                     .arg(finished.join(QLatin1Char('\n')));
    if (!kept.isEmpty())
        parts << QObject::tr("These files had changed elsewhere since that save began, so they were not replaced; the "
                             "versions of the interrupted save are kept in settings/conflicts of the project folder:\n%1")
                     .arg(kept.join(QLatin1Char('\n')));
    if (!lost.isEmpty())
        parts << QObject::tr("These files of the interrupted save were not complete and are at their state before it:\n%1")
                     .arg(lost.join(QLatin1Char('\n')));
    if (dropped > 0 && finished.isEmpty() && kept.isEmpty() && lost.isEmpty())
        parts << QObject::tr("The last save of this project was cut off before it was complete. The project is as it was "
                             "at the save before; changes made after that save are not in it.");
    return parts.join(QStringLiteral("\n\n"));
}

Recovery recover(const QString &folder, const QString &journal, const QString &settingsDir)
{
    Recovery r;
    QSet<QString> handled;
    if (QFile::exists(journal)) {
        QFile f(journal);
        QJsonObject root;
        if (f.open(QIODevice::ReadOnly))
            root = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        const QString when = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH-mm-ss"));
        for (const QJsonValue &v : root.value(QStringLiteral("files")).toArray()) {
            const QJsonObject o = v.toObject();
            const QString rel = o.value(QStringLiteral("file")).toString();
            const QString stagedRel = o.value(QStringLiteral("staged")).toString();
            // a journal is data from the project folder: only files inside it, and only their own temporary name
            if (!pathsafety::isSafeRelative(rel) || stagedRel != rel + QLatin1String(kStageSuffix))
                continue;
            const QString target = QDir(folder).filePath(rel);
            const QString staged = QDir(folder).filePath(stagedRel);
            if (!pathsafety::isInside(target, folder))
                continue;
            handled.insert(QFileInfo(staged).absoluteFilePath());
            const QByteArray sha = o.value(QStringLiteral("sha256")).toString().toLatin1();
            const QByteArray before = o.value(QStringLiteral("replaces")).toString().toLatin1();
            QString name = o.value(QStringLiteral("title")).toString();
            const QString place = o.value(QStringLiteral("place")).toString();
            const QString label = (name.isEmpty() ? rel : name) + QStringLiteral(" (%1)").arg(place.isEmpty() ? rel : place);
            bool exists = false;
            const QByteArray now = shaOfFile(target, &exists);
            if (exists && now == sha) { // already in place before the cut
                QFile::remove(staged);
                continue;
            }
            if (!QFile::exists(staged) || shaOfFile(staged) != sha) {
                QFile::remove(staged);
                r.lost << label;
                continue;
            }
            if (!exists || now == before) {
                if (replace(staged, target))
                    r.finished << label;
                else
                    r.lost << label;
                continue;
            }
            // the target changed since the save began (another computer through a sync folder): both stay
            const QString dir = settingsDir + QStringLiteral("/conflicts");
            QDir().mkpath(dir);
            QString base = name.isEmpty() ? QFileInfo(rel).completeBaseName() : name;
            for (const QChar c : {QLatin1Char('/'), QLatin1Char('\\'), QLatin1Char(':'), QLatin1Char('*'), QLatin1Char('?'),
                                  QLatin1Char('"'), QLatin1Char('<'), QLatin1Char('>'), QLatin1Char('|')})
                base.replace(c, QLatin1Char('-'));
            const QString ext = QFileInfo(rel).suffix();
            QString copy = dir + QLatin1Char('/') + base + QStringLiteral(" (interrupted save %1)").arg(when)
                           + (ext.isEmpty() ? QString() : QLatin1Char('.') + ext);
            for (int n = 2; QFileInfo::exists(copy); ++n)
                copy = dir + QLatin1Char('/') + base + QStringLiteral(" (interrupted save %1 %2)").arg(when).arg(n)
                       + (ext.isEmpty() ? QString() : QLatin1Char('.') + ext);
            if (QFile::rename(staged, copy) || (QFile::copy(staged, copy) && QFile::remove(staged)))
                r.kept << label;
            else
                r.lost << label;
        }
        QFile::remove(journal);
        syncFolder(QFileInfo(journal).absolutePath());
    }
    // temporary files of a save without a journal: that save never began to land
    QDirIterator it(folder, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        if (!isStaged(p) || handled.contains(QFileInfo(p).absoluteFilePath()))
            continue;
        if (QFile::remove(p))
            ++r.dropped;
    }
    return r;
}

} // namespace savebatch
} // namespace qb
