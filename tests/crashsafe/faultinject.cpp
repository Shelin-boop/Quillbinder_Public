/* Fault injection for the system test (section 3.2 of the procedure): a library loaded with LD_PRELOAD
 * into the test program or the real program. It passes every call through to the C library until a rule
 * says otherwise. The rules are read from the file named by QB_FAULT_CTL (looked at again at most every
 * 50 ms, or at once through qbfault_reload()); each fired fault is written as a line to QB_FAULT_LOG.
 *
 * A rule is one line:   <op> <pattern> <errno> [<after> [<times>]]
 *   op       open | write | fsync | close | rename | link | mkdir | unlink | truncate | kill
 *   pattern  a part of the path ("*" = every path); for write, fsync, close the path the file was opened with
 *   errno    a number or a name (ENOSPC, EIO, EACCES, EROFS, EDQUOT, EPERM)
 *   after    write and kill: bytes that still go through (summed over the matching files) before the
 *            fault; the write that crosses the limit is cut there (a partial write)
 *   times    how often the rule fires (0 or missing: always)
 * kill: SIGKILL to the own process once the limit is crossed (an interrupted write: power cut, kill -9).
 * While the library is loaded, open with O_TMPFILE answers EOPNOTSUPP, so that Qt writes named
 * temporary files (Qt falls back on that answer) whose paths the rules can match.
 * Written for this test only; no code of other projects. Compiled as C++ with C linkage (the project builds
 * C++ only), without _FORTIFY_SOURCE (its inline wrappers of open would clash with the definitions here). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

extern "C" {

#define MAX_RULES 32
#define MAX_FD 4096

enum { OP_OPEN, OP_WRITE, OP_FSYNC, OP_CLOSE, OP_RENAME, OP_LINK, OP_MKDIR, OP_UNLINK, OP_TRUNCATE, OP_KILL, OP_COUNT };
static const char *op_names[OP_COUNT] = {"open", "write", "fsync", "close", "rename", "link", "mkdir", "unlink", "truncate", "kill"};

struct rule {
    int op;
    char pattern[256];
    int err;
    long long after;
    long long passed;
    int times;
    int fired;
};

static struct rule rules[MAX_RULES];
static int nrules;
static char *fdpath[MAX_FD];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct timespec last_check;
static time_t ctl_mtime;
static long ctl_mtime_ns;
static off_t ctl_size = -1;
static __thread int inside;   /* no faults for the library's own calls */

static int (*real_open)(const char *, int, ...);
static int (*real_open64)(const char *, int, ...);
static int (*real_openat)(int, const char *, int, ...);
static int (*real_openat64)(int, const char *, int, ...);
static int (*real___open_2)(const char *, int);
static int (*real___open64_2)(const char *, int);
static ssize_t (*real_write)(int, const void *, size_t);
static ssize_t (*real_pwrite64)(int, const void *, size_t, off_t);
static int (*real_fsync)(int);
static int (*real_fdatasync)(int);
static int (*real_close)(int);
static int (*real_dup)(int);
static int (*real_dup2)(int, int);
static int (*real_dup3)(int, int, int);
static int (*real_rename)(const char *, const char *);
static int (*real_renameat)(int, const char *, int, const char *);
static int (*real_renameat2)(int, const char *, int, const char *, unsigned int);
static int (*real_linkat)(int, const char *, int, const char *, int);
static int (*real_mkdir)(const char *, mode_t);
static int (*real_unlink)(const char *);
static int (*real_ftruncate64)(int, off_t);

static void resolve(void)
{
    if (real_write)
        return;
    real_open = reinterpret_cast<decltype(real_open)>(dlsym(RTLD_NEXT, "open"));
    real_open64 = reinterpret_cast<decltype(real_open64)>(dlsym(RTLD_NEXT, "open64"));
    real_openat = reinterpret_cast<decltype(real_openat)>(dlsym(RTLD_NEXT, "openat"));
    real_openat64 = reinterpret_cast<decltype(real_openat64)>(dlsym(RTLD_NEXT, "openat64"));
    real___open_2 = reinterpret_cast<decltype(real___open_2)>(dlsym(RTLD_NEXT, "__open_2"));
    real___open64_2 = reinterpret_cast<decltype(real___open64_2)>(dlsym(RTLD_NEXT, "__open64_2"));
    real_pwrite64 = reinterpret_cast<decltype(real_pwrite64)>(dlsym(RTLD_NEXT, "pwrite64"));
    real_fsync = reinterpret_cast<decltype(real_fsync)>(dlsym(RTLD_NEXT, "fsync"));
    real_fdatasync = reinterpret_cast<decltype(real_fdatasync)>(dlsym(RTLD_NEXT, "fdatasync"));
    real_close = reinterpret_cast<decltype(real_close)>(dlsym(RTLD_NEXT, "close"));
    real_dup = reinterpret_cast<decltype(real_dup)>(dlsym(RTLD_NEXT, "dup"));
    real_dup2 = reinterpret_cast<decltype(real_dup2)>(dlsym(RTLD_NEXT, "dup2"));
    real_dup3 = reinterpret_cast<decltype(real_dup3)>(dlsym(RTLD_NEXT, "dup3"));
    real_rename = reinterpret_cast<decltype(real_rename)>(dlsym(RTLD_NEXT, "rename"));
    real_renameat = reinterpret_cast<decltype(real_renameat)>(dlsym(RTLD_NEXT, "renameat"));
    real_renameat2 = reinterpret_cast<decltype(real_renameat2)>(dlsym(RTLD_NEXT, "renameat2"));
    real_linkat = reinterpret_cast<decltype(real_linkat)>(dlsym(RTLD_NEXT, "linkat"));
    real_mkdir = reinterpret_cast<decltype(real_mkdir)>(dlsym(RTLD_NEXT, "mkdir"));
    real_unlink = reinterpret_cast<decltype(real_unlink)>(dlsym(RTLD_NEXT, "unlink"));
    real_ftruncate64 = reinterpret_cast<decltype(real_ftruncate64)>(dlsym(RTLD_NEXT, "ftruncate64"));
    real_write = reinterpret_cast<decltype(real_write)>(dlsym(RTLD_NEXT, "write"));
}

static int errno_of(const char *s)
{
    static const struct { const char *n; int v; } names[] = {
        {"ENOSPC", ENOSPC}, {"EIO", EIO}, {"EACCES", EACCES}, {"EROFS", EROFS}, {"EDQUOT", EDQUOT},
        {"EPERM", EPERM}, {"EFBIG", EFBIG}, {"ENOENT", ENOENT}, {"EBUSY", EBUSY}, {"EINTR", EINTR},
        /* 0.64.1: a file system that cannot sync; the program counts these at fsync as success */
        {"EINVAL", EINVAL}, {"ENOTSUP", ENOTSUP}, {"EOPNOTSUPP", EOPNOTSUPP}, {"ENOSYS", ENOSYS}};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i)
        if (strcmp(s, names[i].n) == 0)
            return names[i].v;
    return atoi(s);
}

static void load_rules_locked(void)
{
    const char *ctl = getenv("QB_FAULT_CTL");
    nrules = 0;
    if (!ctl || !real_open)
        return;
    int fd = real_open(ctl, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return;
    char buf[8192];
    ssize_t n = read(fd, buf, sizeof buf - 1);
    real_close(fd);
    if (n <= 0)
        return;
    buf[n] = 0;
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line && nrules < MAX_RULES; line = strtok_r(NULL, "\n", &save)) {
        char op[32] = {0}, pat[256] = {0}, err[32] = {0};
        long long after = 0;
        int times = 0;
        if (line[0] == '#' || sscanf(line, "%31s %255s %31s %lld %d", op, pat, err, &after, &times) < 3)
            continue;
        for (int k = 0; k < OP_COUNT; ++k)
            if (strcmp(op, op_names[k]) == 0) {
                struct rule *r = &rules[nrules++];
                memset(r, 0, sizeof *r);
                r->op = k;
                snprintf(r->pattern, sizeof r->pattern, "%s", pat);
                r->err = errno_of(err);
                r->after = after;
                r->times = times;
            }
    }
}

static void check_ctl_locked(int force)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long long ms = (now.tv_sec - last_check.tv_sec) * 1000LL + (now.tv_nsec - last_check.tv_nsec) / 1000000LL;
    if (!force && ms < 50)
        return;
    last_check = now;
    const char *ctl = getenv("QB_FAULT_CTL");
    struct stat st;
    if (!ctl || stat(ctl, &st) != 0) {
        nrules = 0;
        ctl_size = -1;
        return;
    }
    if (!force && st.st_mtim.tv_sec == ctl_mtime && st.st_mtim.tv_nsec == ctl_mtime_ns && st.st_size == ctl_size)
        return;
    ctl_mtime = st.st_mtim.tv_sec;
    ctl_mtime_ns = st.st_mtim.tv_nsec;
    ctl_size = st.st_size;
    load_rules_locked();
}

/* exported for the test program (dlsym): read the rules now */
void qbfault_reload(void)
{
    resolve();
    pthread_mutex_lock(&lock);
    check_ctl_locked(1);
    pthread_mutex_unlock(&lock);
}

static void log_fault(const struct rule *r, const char *path)
{
    const char *logf = getenv("QB_FAULT_LOG");
    if (!logf || !real_open)
        return;
    int fd = real_open(logf, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0)
        return;
    char line[600];
    int n = snprintf(line, sizeof line, "%s %s %d %s\n", op_names[r->op], r->pattern, r->err, path ? path : "?");
    if (n > 0)
        real_write(fd, line, (size_t)(n < (int)sizeof line ? n : (int)sizeof line - 1));
    real_close(fd);
}

static int matches(const struct rule *r, const char *path)
{
    if (strcmp(r->pattern, "*") == 0)
        return 1;
    return path && strstr(path, r->pattern) != NULL;
}

/* the first rule of op that matches path and may still fire; -1 if none */
static int find_rule(int op, const char *path)
{
    if (inside)
        return -1;
    pthread_mutex_lock(&lock);
    check_ctl_locked(0);
    int found = -1;
    for (int i = 0; i < nrules; ++i)
        if (rules[i].op == op && matches(&rules[i], path) && (rules[i].times == 0 || rules[i].fired < rules[i].times)) {
            found = i;
            break;
        }
    pthread_mutex_unlock(&lock);
    return found;
}

static int fire(int i, const char *path)
{
    pthread_mutex_lock(&lock);
    rules[i].fired++;
    struct rule copy = rules[i];
    pthread_mutex_unlock(&lock);
    inside = 1;
    log_fault(&copy, path);
    inside = 0;
    errno = copy.err;
    return -1;
}

static const char *path_of_fd(int fd)
{
    return fd >= 0 && fd < MAX_FD ? fdpath[fd] : NULL;
}

static void remember(int fd, const char *path, int flags)
{
    if (fd < 0 || fd >= MAX_FD || !path)
        return;
    if (!(flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)))
        return;
    pthread_mutex_lock(&lock);
    free(fdpath[fd]);
    fdpath[fd] = strdup(path);
    pthread_mutex_unlock(&lock);
}

static int open_common(int dirfd, const char *path, int flags, mode_t mode, int which)
{
    resolve();
    if (!inside && (flags & O_TMPFILE) == O_TMPFILE) {
        errno = EOPNOTSUPP;
        return -1;
    }
    if (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC)) {
        int r = find_rule(OP_OPEN, path);
        if (r >= 0)
            return fire(r, path);
    }
    int fd;
    switch (which) {
    case 0: fd = real_open(path, flags, mode); break;
    case 1: fd = real_open64 ? real_open64(path, flags, mode) : real_open(path, flags, mode); break;
    case 2: fd = real_openat(dirfd, path, flags, mode); break;
    default: fd = real_openat64 ? real_openat64(dirfd, path, flags, mode) : real_openat(dirfd, path, flags, mode); break;
    }
    if (fd >= 0 && !inside)
        remember(fd, path, flags);
    return fd;
}

static mode_t mode_arg(int flags, va_list ap)
{
    return (flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE ? (mode_t)va_arg(ap, int) : 0;
}

int open(const char *path, int flags, ...)
{
    va_list ap;
    va_start(ap, flags);
    mode_t m = mode_arg(flags, ap);
    va_end(ap);
    return open_common(AT_FDCWD, path, flags, m, 0);
}
int open64(const char *path, int flags, ...)
{
    va_list ap;
    va_start(ap, flags);
    mode_t m = mode_arg(flags, ap);
    va_end(ap);
    return open_common(AT_FDCWD, path, flags, m, 1);
}
int openat(int dirfd, const char *path, int flags, ...)
{
    va_list ap;
    va_start(ap, flags);
    mode_t m = mode_arg(flags, ap);
    va_end(ap);
    return open_common(dirfd, path, flags, m, 2);
}
int openat64(int dirfd, const char *path, int flags, ...)
{
    va_list ap;
    va_start(ap, flags);
    mode_t m = mode_arg(flags, ap);
    va_end(ap);
    return open_common(dirfd, path, flags, m, 3);
}
int __open_2(const char *path, int flags) { return open_common(AT_FDCWD, path, flags, 0, 0); }
int __open64_2(const char *path, int flags) { return open_common(AT_FDCWD, path, flags, 0, 1); }

/* write and kill rules: bytes up to "after" go through; the write that crosses it is cut */
static ssize_t write_common(int fd, const void *buf, size_t n, off_t off, int positional)
{
    resolve();
    const char *path = path_of_fd(fd);
    if (path && !inside) {
        for (int op = OP_WRITE; op <= OP_KILL; op += (OP_KILL - OP_WRITE)) {
            int i = find_rule(op, path);
            if (i < 0)
                continue;
            pthread_mutex_lock(&lock);
            long long room = rules[i].after - rules[i].passed;
            if (room > 0 && (long long)n <= room) {
                rules[i].passed += (long long)n;
                pthread_mutex_unlock(&lock);
                continue;   /* still under the limit of this rule */
            }
            pthread_mutex_unlock(&lock);
            if (room > 0) {
                ssize_t w = positional ? real_pwrite64(fd, buf, (size_t)room, off) : real_write(fd, buf, (size_t)room);
                pthread_mutex_lock(&lock);
                rules[i].passed += w > 0 ? w : 0;
                pthread_mutex_unlock(&lock);
                if (op == OP_KILL) {
                    fire(i, path);
                    kill(getpid(), SIGKILL);
                }
                fire(i, path);
                return w;   /* a partial write: the caller sees fewer bytes */
            }
            if (op == OP_KILL) {
                fire(i, path);
                kill(getpid(), SIGKILL);
            }
            return fire(i, path);
        }
    }
    return positional ? real_pwrite64(fd, buf, n, off) : real_write(fd, buf, n);
}

ssize_t write(int fd, const void *buf, size_t n) { return write_common(fd, buf, n, 0, 0); }
ssize_t pwrite64(int fd, const void *buf, size_t n, off_t off) { return write_common(fd, buf, n, off, 1); }
ssize_t pwrite(int fd, const void *buf, size_t n, off_t off) { return write_common(fd, buf, n, off, 1); }

int fsync(int fd)
{
    resolve();
    int i = find_rule(OP_FSYNC, path_of_fd(fd));
    return i >= 0 ? fire(i, path_of_fd(fd)) : real_fsync(fd);
}
int fdatasync(int fd)
{
    resolve();
    int i = find_rule(OP_FSYNC, path_of_fd(fd));
    return i >= 0 ? fire(i, path_of_fd(fd)) : real_fdatasync(fd);
}

/* a copy of a descriptor writes to the same file: it carries the path, so that a close rule hits the copy
 * the program closes and checks (0.64.1, safesave::commit) and not only the original Qt closes later unchecked */
static void copy_path(int from, int to)
{
    if (from < 0 || from >= MAX_FD || to < 0 || to >= MAX_FD || from == to || inside)
        return;
    pthread_mutex_lock(&lock);
    free(fdpath[to]);
    fdpath[to] = fdpath[from] ? strdup(fdpath[from]) : NULL;
    pthread_mutex_unlock(&lock);
}
int dup(int fd)
{
    resolve();
    int r = real_dup(fd);
    if (r >= 0)
        copy_path(fd, r);
    return r;
}
int dup2(int fd, int to)
{
    resolve();
    int r = real_dup2(fd, to);
    if (r >= 0)
        copy_path(fd, r);
    return r;
}
int dup3(int fd, int to, int flags)
{
    resolve();
    int r = real_dup3(fd, to, flags);
    if (r >= 0)
        copy_path(fd, r);
    return r;
}

int close(int fd)
{
    resolve();
    const char *path = path_of_fd(fd);
    int i = path ? find_rule(OP_CLOSE, path) : -1;
    int rc = real_close(fd);   /* the descriptor is closed in any case, as the kernel does */
    if (fd >= 0 && fd < MAX_FD && !inside) {
        pthread_mutex_lock(&lock);
        free(fdpath[fd]);
        fdpath[fd] = NULL;
        pthread_mutex_unlock(&lock);
    }
    if (i >= 0)
        return fire(i, path);
    return rc;
}

static int rename_rule(const char *from, const char *to)
{
    int i = find_rule(OP_RENAME, to);
    return i >= 0 ? i : find_rule(OP_RENAME, from);
}
int rename(const char *from, const char *to)
{
    resolve();
    int i = rename_rule(from, to);
    return i >= 0 ? fire(i, to) : real_rename(from, to);
}
int renameat(int fd1, const char *from, int fd2, const char *to)
{
    resolve();
    int i = rename_rule(from, to);
    return i >= 0 ? fire(i, to) : real_renameat(fd1, from, fd2, to);
}
int renameat2(int fd1, const char *from, int fd2, const char *to, unsigned int flags)
{
    resolve();
    int i = rename_rule(from, to);
    if (i >= 0)
        return fire(i, to);
    if (!real_renameat2) {
        errno = ENOSYS;
        return -1;
    }
    return real_renameat2(fd1, from, fd2, to, flags);
}
int linkat(int fd1, const char *from, int fd2, const char *to, int flags)
{
    resolve();
    int i = find_rule(OP_LINK, to);
    return i >= 0 ? fire(i, to) : real_linkat(fd1, from, fd2, to, flags);
}
int mkdir(const char *path, mode_t mode)
{
    resolve();
    int i = find_rule(OP_MKDIR, path);
    return i >= 0 ? fire(i, path) : real_mkdir(path, mode);
}
int unlink(const char *path)
{
    resolve();
    int i = find_rule(OP_UNLINK, path);
    return i >= 0 ? fire(i, path) : real_unlink(path);
}
int ftruncate64(int fd, off_t len)
{
    resolve();
    int i = find_rule(OP_TRUNCATE, path_of_fd(fd));
    return i >= 0 ? fire(i, path_of_fd(fd)) : real_ftruncate64(fd, len);
}
int ftruncate(int fd, off_t len) { return ftruncate64(fd, len); }

} // extern "C"
