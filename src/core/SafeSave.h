#pragma once
// Writing and committing a QSaveFile so that a failed write is never reported as saved (0.64.1, found while the
// fault cases of the system test were written, 2026-11-09): Qt 6.4's QSaveFile::commit() writes the
// last buffered bytes while it closes the file and syncs it to the disk, but it does not look at the
// result of either, so a full disk (ENOSPC) or an I/O error (EIO) there left a cut-off file in place
// of the old one and commit() returned true. Here the buffer is written and checked first, then the
// file is synced (checked), then committed. False: nothing replaced (the temporary file goes when the
// QSaveFile is destroyed).
#include <QSaveFile>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <io.h>
#endif

namespace qb {
namespace safesave {

// Writes data; a short write (a full disk after part of it: Qt reports no error then) cancels the file,
// so the commit below fails and nothing is replaced.
inline qint64 write(QSaveFile &f, const QByteArray &data)
{
    const qint64 n = f.write(data);
    if (n != data.size())
        f.cancelWriting();
    return n;
}

inline bool commit(QSaveFile &f)
{
    if (!f.isOpen() || f.error() != QFileDevice::NoError)
        return false;
    if (!f.flush() || f.error() != QFileDevice::NoError) {
        f.cancelWriting();
        return false;
    }
    const int fd = f.handle();
#ifdef Q_OS_UNIX
    // a file system that cannot sync (EINVAL, ENOTSUP on some network and FUSE mounts) is no error
    if (fd >= 0 && ::fsync(fd) != 0 && errno != EINVAL && errno != ENOTSUP && errno != EOPNOTSUPP && errno != ENOSYS) {
        f.cancelWriting();
        return false;
    }
    // Qt closes the temporary file only after it has replaced the old one, and never looks at the
    // result; a file system that reports write errors at close (NFS and other network file systems)
    // reports them for a copy of the descriptor too, so a copy is closed here first
    if (fd >= 0) {
        const int copy = ::dup(fd);
        if (copy >= 0 && ::close(copy) != 0 && errno != EINTR) {
            f.cancelWriting();
            return false;
        }
    }
#elif defined(Q_OS_WIN)
    if (fd >= 0 && ::_commit(fd) != 0) {
        f.cancelWriting();
        return false;
    }
#endif
    const bool done = f.commit();
    return done && f.error() == QFileDevice::NoError;
}

} // namespace safesave
} // namespace qb
