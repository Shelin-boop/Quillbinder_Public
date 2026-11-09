// A small LD_PRELOAD library for the checks of qb_audit1_tests (0.64.1): makes write(), fsync() and
// fdatasync() fail for files below one folder, as a full disk (ENOSPC) or a failing drive (EIO) would.
//   QB_SHIM_DIR=<folder>   the files whose path starts with it
//   QB_SHIM_MODE=write     write() fails with ENOSPC
//   QB_SHIM_MODE=fsync     fsync() and fdatasync() fail with EIO
//   QB_SHIM_MODE=close     close() fails with EIO (the descriptor is closed all the same, as the kernel does)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

namespace {
bool below(int fd)
{
    const char *dir = getenv("QB_SHIM_DIR");
    if (!dir || !*dir || fd < 0)
        return false;
    char link[64], path[PATH_MAX];
    snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
    const ssize_t n = readlink(link, path, sizeof path - 1);
    if (n <= 0)
        return false;
    path[n] = 0;
    return strncmp(path, dir, strlen(dir)) == 0;
}
bool mode(const char *m)
{
    const char *v = getenv("QB_SHIM_MODE");
    return v && strcmp(v, m) == 0;
}
} // namespace

extern "C" {
ssize_t write(int fd, const void *buf, size_t count)
{
    using Fn = ssize_t (*)(int, const void *, size_t);
    static Fn real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "write"));
    if (mode("write") && below(fd)) {
        errno = ENOSPC;
        return -1;
    }
    return real(fd, buf, count);
}
int fsync(int fd)
{
    using Fn = int (*)(int);
    static Fn real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "fsync"));
    if (mode("fsync") && below(fd)) {
        errno = EIO;
        return -1;
    }
    return real(fd);
}
int close(int fd)
{
    using Fn = int (*)(int);
    static Fn real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "close"));
    const bool fail = mode("close") && below(fd);
    const int rc = real(fd);
    if (fail) {
        errno = EIO;
        return -1;
    }
    return rc;
}
int fdatasync(int fd)
{
    using Fn = int (*)(int);
    static Fn real = reinterpret_cast<Fn>(dlsym(RTLD_NEXT, "fdatasync"));
    if (mode("fsync") && below(fd)) {
        errno = EIO;
        return -1;
    }
    return real(fd);
}
}
