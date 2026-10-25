#include "core/CrashGuard.h"
#include "AppInfo.h"
#include "core/Paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSocketNotifier>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <elf.h>
#include <execinfo.h>
#include <fcntl.h>
#include <link.h>
#include <signal.h>
#include <sys/syscall.h>
#include <thread>
#include <time.h>
#include <unistd.h>

namespace qb {
namespace crash {

namespace {

// ---------------------------------------------------------------- state read by the handler
// Everything the handler touches is prepared here in advance: it must not allocate
// memory or take locks (the crash may have happened inside malloc).

constexpr int kSnapSize = 32 * 1024;
char g_snap[2][kSnapSize];
int g_snapLen[2] = {0, 0};
std::atomic<int> g_snapIdx{0};

constexpr int kNotes = 48;
constexpr int kNoteLen = 200;
char g_notes[kNotes][kNoteLen];
std::atomic<unsigned> g_noteCount{0};

constexpr int kMsgs = 30;
constexpr int kMsgLen = 320;
char g_msgs[kMsgs][kMsgLen];
std::atomic<unsigned> g_msgCount{0};
QMutex g_msgMutex; // between writers (Qt messages come from every thread)
QtMessageHandler g_prevHandler = nullptr;
// all messages for the log console (0.21; not used by the signal handlers)
QStringList g_log;
quint64 g_logCount = 0;
QMutex g_logMutex;

char g_dir[4096];   // the report folder with '/' at the end
time_t g_start = 0;
pid_t g_mainTid = 0;
std::atomic<int> g_busy{0};
bool g_installed = false;
constexpr int kBufSize = 16 * 1024;
constexpr int kLineSize = 1024;
char g_buf[kBufSize]; // reading /proc files inside the handler
char g_line[kLineSize];
void *g_frames[128];
alignas(16) char g_altStack[64 * 1024]; // the handler runs here when the stack itself overflowed

const int kFatal[] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL};

// ---------------------------------------------------------------- async-signal-safe output

void put(int fd, const char *s, size_t n)
{
    while (n > 0) {
        const ssize_t w = ::write(fd, s, n);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            return;
        s += w;
        n -= size_t(w);
    }
}
void put(int fd, const char *s) { put(fd, s, std::strlen(s)); }

int formatNum(char *out, unsigned long long v, int base)
{
    char tmp[32];
    int i = 0;
    do {
        const int d = int(v % unsigned(base));
        tmp[i++] = char(d < 10 ? '0' + d : 'a' + d - 10);
        v /= unsigned(base);
    } while (v && i < 31);
    for (int k = 0; k < i; ++k)
        out[k] = tmp[i - 1 - k];
    out[i] = 0;
    return i;
}
void putNum(int fd, long long v)
{
    char b[32];
    if (v < 0) {
        put(fd, "-");
        v = -v;
    }
    put(fd, b, size_t(formatNum(b, (unsigned long long)v, 10)));
}
void putHex(int fd, unsigned long long v)
{
    char b[32];
    put(fd, "0x");
    put(fd, b, size_t(formatNum(b, v, 16)));
}

const char *sigName(int s)
{
    switch (s) {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGBUS: return "SIGBUS";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    }
    return "signal";
}
const char *sigMeaning(int s)
{
    switch (s) {
    case SIGSEGV: return "segmentation fault: the program used memory it must not use";
    case SIGABRT: return "abort: the program stopped itself after an internal error";
    case SIGBUS: return "bus error: memory that cannot be reached (often a file that went away)";
    case SIGFPE: return "arithmetic error";
    case SIGILL: return "illegal instruction";
    }
    return "fatal signal";
}

// copies the lines of a /proc file for which keep(line) is true; buf (kBufSize) and
// line (kLineSize) belong to the caller: the crash handler and the freeze watchdog
// each have their own
template<typename Keep>
void copyLines(int out, const char *path, Keep keep, char *buf, char *line)
{
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return;
    int len = 0;
    bool cut = false;
    for (;;) {
        const ssize_t n = ::read(fd, buf, kBufSize);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        for (ssize_t i = 0; i < n; ++i) {
            const char c = buf[i];
            if (c != '\n') {
                if (len < kLineSize - 2)
                    line[len++] = c;
                else
                    cut = true;
                continue;
            }
            line[len] = 0;
            if (keep(line)) {
                put(out, line, size_t(len));
                put(out, cut ? " ...\n" : "\n");
            }
            len = 0;
            cut = false;
        }
    }
    if (len > 0) {
        line[len] = 0;
        if (keep(line)) {
            put(out, line, size_t(len));
            put(out, "\n");
        }
    }
    ::close(fd);
}

bool processLine(const char *l)
{
    return std::strncmp(l, "Vm", 2) == 0 || std::strncmp(l, "Threads", 7) == 0 || std::strncmp(l, "State", 5) == 0;
}
bool codeLine(const char *l) { return std::strstr(l, " r-xp ") != nullptr; }

void putRing(int fd, char (*ring)[kNoteLen], unsigned count, int size)
{
    const unsigned n = std::min<unsigned>(count, unsigned(size));
    if (n == 0)
        put(fd, "(none)\n");
    for (unsigned i = count - n; i < count; ++i)
        put(fd, ring[i % unsigned(size)]);
}
void putMsgs(int fd)
{
    const unsigned count = g_msgCount.load();
    const unsigned n = std::min<unsigned>(count, unsigned(kMsgs));
    if (n == 0)
        put(fd, "(none)\n");
    for (unsigned i = count - n; i < count; ++i)
        put(fd, g_msgs[i % unsigned(kMsgs)]);
}

// <report folder><kind>-<unix time>-<pid>.txt into path (sizeof g_dir + 64 bytes)
void reportPath(char *path, const char *kind)
{
    size_t len = std::strlen(g_dir);
    std::memcpy(path, g_dir, len);
    const size_t k = std::strlen(kind);
    std::memcpy(path + len, kind, k);
    len += k;
    path[len++] = '-';
    len += size_t(formatNum(path + len, (unsigned long long)::time(nullptr), 10));
    path[len++] = '-';
    len += size_t(formatNum(path + len, (unsigned long long)::getpid(), 10));
    std::memcpy(path + len, ".txt", 5);
}

void putShared(int fd, const char *when, char *buf, char *line)
{
    put(fd, "\n== Program and system (prepared before the ");
    put(fd, when);
    put(fd, ")\n");
    const int idx = g_snapIdx.load();
    if (g_snapLen[idx] > 0)
        put(fd, g_snap[idx], size_t(g_snapLen[idx]));
    else
        put(fd, "(not prepared yet)\n");

    put(fd, "\n== Process at the ");
    put(fd, when);
    put(fd, "\n");
    copyLines(fd, "/proc/self/status", processLine, buf, line);

    put(fd, "\n== Recent actions (oldest first; no text of documents)\n");
    putRing(fd, g_notes, g_noteCount.load(), kNotes);

    put(fd, "\n== Recent warnings of the program\n");
    putMsgs(fd);
}

void writeReport(int fd, int sig, siginfo_t *si)
{
    const time_t now = ::time(nullptr);
    put(fd, "Quillbinder crash report\nFormat: 1\nVersion: " QB_APP_VERSION "\nSignal: ");
    putNum(fd, sig);
    put(fd, " ");
    put(fd, sigName(sig));
    put(fd, " (");
    put(fd, sigMeaning(sig));
    put(fd, ")\n");
    if (si) {
        put(fd, "Signal code: ");
        putNum(fd, si->si_code);
        if (si->si_code <= 0) { // sent by a process (kill, raise, abort)
            put(fd, " (sent by process ");
            putNum(fd, si->si_pid);
            put(fd, ")");
        } else if (sig != SIGABRT) {
            put(fd, " (address ");
            putHex(fd, (unsigned long long)(uintptr_t)si->si_addr);
            put(fd, ")");
        }
        put(fd, "\n");
    }
    put(fd, "Unix time: ");
    putNum(fd, (long long)now);
    put(fd, "\nSeconds since start: ");
    putNum(fd, (long long)(now - g_start));
    put(fd, "\nProcess: ");
    putNum(fd, ::getpid());
    put(fd, "\nThread: ");
    const pid_t tid = pid_t(::syscall(SYS_gettid));
    putNum(fd, tid);
    put(fd, tid == g_mainTid ? " (the main thread)\n" : " (another thread)\n");

    putShared(fd, "crash", g_buf, g_line);

    put(fd, "\n== Backtrace (innermost first)\n"
            "(The offsets refer to the program file named under Program above; the file name in front of the\n"
            " parentheses can be the name the program was started by, such as AppRun.)\n");
    const int n = ::backtrace(g_frames, int(sizeof g_frames / sizeof g_frames[0]));
    ::backtrace_symbols_fd(g_frames, n, fd);

    put(fd, "\n== Program files in memory (to translate the addresses above)\n");
    copyLines(fd, "/proc/self/maps", codeLine, g_buf, g_line);
    put(fd, "\n== End\n");
}

void onFatal(int sig, siginfo_t *si, void *)
{
    const int savedErrno = errno;
    if (g_busy.exchange(1) == 0) {
        char path[sizeof g_dir + 64];
        reportPath(path, "crash");
        const int fd = g_dir[0] ? ::open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600) : -1;
        if (fd >= 0) {
            writeReport(fd, sig, si);
            ::fsync(fd);
            ::close(fd);
            put(2, "\nQuillbinder ended because of an error. A crash report was written to\n  ");
            put(2, path);
            put(2, "\n");
        }
    } else {
        ::sleep(5); // another thread writes the report; the process ends when it is done
    }
    // on to the system as before: the default action (core dump, exit status)
    ::signal(sig, SIG_DFL);
    ::raise(sig);
    errno = savedErrno;
}

void onQtMessage(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    {
        static const char *const kinds[] = {"debug", "warning", "critical", "fatal", "info"};
        const QString line = QStringLiteral("+%1 s  %2: %3").arg(secondsSinceStart()).arg(QLatin1String(kinds[qBound(0, int(type), 4)]), msg);
        QMutexLocker lock(&g_logMutex);
        g_log.append(line);
        if (g_log.size() > 1000)
            g_log.removeFirst();
        ++g_logCount;
    }
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        const char *kind = type == QtWarningMsg ? "warning" : type == QtCriticalMsg ? "critical" : "fatal";
        QByteArray line = QStringLiteral("+%1 s  %2: %3").arg(secondsSinceStart()).arg(QLatin1String(kind), msg).toUtf8();
        line.replace('\n', ' ');
        if (line.size() > kMsgLen - 2) {
            int cut = kMsgLen - 6;
            while (cut > 0 && (uchar(line.at(cut)) & 0xC0) == 0x80)
                --cut;
            line = line.left(cut) + " ...";
        }
        QMutexLocker lock(&g_msgMutex);
        const unsigned slot = g_msgCount.load() % unsigned(kMsgs);
        std::memcpy(g_msgs[slot], line.constData(), size_t(line.size()));
        g_msgs[slot][line.size()] = '\n';
        g_msgs[slot][line.size() + 1] = 0;
        g_msgCount.fetch_add(1);
    }
    if (g_prevHandler)
        g_prevHandler(type, ctx, msg);
}

// ---------------------------------------------------------------- freezes

std::atomic<long long> g_beatMs{0};     // the main thread's last heartbeat (monotonic clock)
std::atomic<int> g_freezeSeconds{30};
std::atomic<int> g_traceFd{-1};         // where SIGUSR2 on the main thread writes its backtrace
std::atomic<int> g_traceDone{0};
void *g_traceFrames[128];
char g_wbuf[kBufSize];                  // the watchdog's own buffers
char g_wline[kLineSize];
bool g_watchdog = false;

long long monoMs()
{
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts); // stands still while the computer sleeps
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// SIGUSR2, sent by the watchdog to the main thread: where it is
void onTraceRequest(int, siginfo_t *, void *)
{
    const int savedErrno = errno;
    const int fd = g_traceFd.load();
    if (fd >= 0) {
        const int n = ::backtrace(g_traceFrames, int(sizeof g_traceFrames / sizeof g_traceFrames[0]));
        ::backtrace_symbols_fd(g_traceFrames, n, fd);
    }
    g_traceDone.store(1);
    errno = savedErrno;
}

// a debugger holds the program (a breakpoint looks like a freeze)
bool traced()
{
    std::FILE *f = std::fopen("/proc/self/status", "re");
    if (!f)
        return false;
    char line[256];
    bool t = false;
    while (std::fgets(line, sizeof line, f))
        if (std::strncmp(line, "TracerPid:", 10) == 0) {
            t = std::atol(line + 10) != 0;
            break;
        }
    std::fclose(f);
    return t;
}

// writes freeze-<time>-<pid>.txt into path; false when it could not be written
bool writeFreeze(long long stalledMs, char *path)
{
    if (!g_dir[0])
        return false;
    reportPath(path, "freeze");
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    const time_t now = ::time(nullptr);
    put(fd, "Quillbinder freeze report\nFormat: 1\nVersion: " QB_APP_VERSION
            "\nKind: freeze (the program did not respond)\nNot responding for: ");
    putNum(fd, stalledMs / 1000);
    put(fd, " s\nUnix time: ");
    putNum(fd, (long long)now);
    put(fd, "\nSeconds since start: ");
    putNum(fd, (long long)(now - g_start));
    put(fd, "\nProcess: ");
    putNum(fd, ::getpid());
    put(fd, "\n");
    putShared(fd, "freeze", g_wbuf, g_wline);

    put(fd, "\n== Where the main thread is (innermost first)\n"
            "(The offsets refer to the program file named under Program above; the file name in front of the\n"
            " parentheses can be the name the program was started by, such as AppRun.)\n");
    g_traceDone.store(0);
    g_traceFd.store(fd);
    if (::syscall(SYS_tgkill, ::getpid(), g_mainTid, SIGUSR2) == 0)
        for (int i = 0; i < 300 && !g_traceDone.load(); ++i) {
            timespec ts{0, 10 * 1000000L};
            ::nanosleep(&ts, nullptr);
        }
    g_traceFd.store(-1);
    const bool answered = g_traceDone.load() != 0;
    if (!answered)
        put(fd, "(the main thread did not answer: it may be waiting for the disk or the system)\n");

    put(fd, "\n== Program files in memory (to translate the addresses above)\n");
    copyLines(fd, "/proc/self/maps", codeLine, g_wbuf, g_wline);
    put(fd, "\n== End\n");
    ::fsync(fd);
    if (answered) // otherwise the main thread may still write into it later
        ::close(fd);
    put(2, "\nQuillbinder has not responded for a while. A freeze report was written to\n  ");
    put(2, path);
    put(2, "\n");
    return true;
}

void watchdogLoop()
{
    char path[sizeof g_dir + 64];
    bool frozen = false, written = false;
    long long lastBeat = 0;
    for (;;) {
        timespec ts{0, 500 * 1000000L};
        ::nanosleep(&ts, nullptr);
        const long long beat = g_beatMs.load();
        if (!frozen) {
            if (monoMs() - beat >= (long long)g_freezeSeconds.load() * 1000 && !traced()) {
                frozen = true;
                lastBeat = beat;
                written = writeFreeze(monoMs() - beat, path);
            }
        } else if (beat != lastBeat) { // the main thread answered again
            frozen = false;
            if (written) {
                const int fd = ::open(path, O_WRONLY | O_APPEND | O_CLOEXEC);
                if (fd >= 0) {
                    put(fd, "\n== Responded again after ");
                    putNum(fd, (beat - lastBeat) / 1000);
                    put(fd, " s\n");
                    ::close(fd);
                }
            }
        }
    }
}

// ---------------------------------------------------------------- termination signals

int g_termPipe[2] = {-1, -1};
std::function<void()> g_onTerm;

void onTermSignal(int sig)
{
    const int savedErrno = errno;
    const char c = char(sig);
    const ssize_t r = ::write(g_termPipe[1], &c, 1);
    (void)r;
    errno = savedErrno;
}

// ---------------------------------------------------------------- sessions

bool g_session = false;
QString g_sessionProject;
QDateTime g_sessionStart;

QString bootId()
{
    QFile f(QStringLiteral("/proc/sys/kernel/random/boot_id"));
    return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readAll()).trimmed() : QString();
}

QString markerPath(qint64 pid) { return sessionDir() + QStringLiteral("/%1.json").arg(pid); }

void writeMarker()
{
    QDir().mkpath(sessionDir());
    QJsonObject o;
    o.insert(QStringLiteral("pid"), QCoreApplication::applicationPid());
    o.insert(QStringLiteral("started"), g_sessionStart.toSecsSinceEpoch());
    o.insert(QStringLiteral("boot"), bootId());
    o.insert(QStringLiteral("version"), QStringLiteral(QB_APP_VERSION));
    o.insert(QStringLiteral("project"), g_sessionProject);
    QSaveFile f(markerPath(QCoreApplication::applicationPid()));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson());
        f.commit();
    }
}

// a running process of this program
bool isRunningInstance(qint64 pid)
{
    if (pid <= 0 || pid == QCoreApplication::applicationPid())
        return false;
    QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
    if (!comm.open(QIODevice::ReadOnly))
        return false;
    return QString::fromLatin1(comm.readAll()).trimmed() == QLatin1String(QB_APP_ID);
}

int buildIdCallback(struct dl_phdr_info *info, size_t, void *data)
{
    auto *out = static_cast<QString *>(data);
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) &ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_NOTE)
            continue;
        const char *p = reinterpret_cast<const char *>(info->dlpi_addr + ph.p_vaddr);
        const char *end = p + ph.p_memsz;
        while (p + sizeof(ElfW(Nhdr)) <= end) {
            const auto *nh = reinterpret_cast<const ElfW(Nhdr) *>(p);
            const char *name = p + sizeof(ElfW(Nhdr));
            const char *desc = name + ((nh->n_namesz + 3) & ~3u);
            if (nh->n_type == NT_GNU_BUILD_ID && nh->n_namesz == 4 && std::memcmp(name, "GNU", 4) == 0) {
                *out = QString::fromLatin1(QByteArray(desc, int(nh->n_descsz)).toHex());
                return 1;
            }
            p = desc + ((nh->n_descsz + 3) & ~3u);
        }
    }
    return 1; // the first object is the program itself
}

} // namespace

// ---------------------------------------------------------------- public

QString reportDir() { return dataDir() + QLatin1String("/CrashReports"); }
QString sessionDir() { return dataDir() + QLatin1String("/sessions"); }

void install()
{
    if (g_installed)
        return;
    g_installed = true;
    g_start = ::time(nullptr);
    g_mainTid = pid_t(::syscall(SYS_gettid));
    QDir().mkpath(reportDir());
    const QByteArray dir = QFile::encodeName(reportDir() + QLatin1Char('/'));
    if (dir.size() < int(sizeof g_dir))
        std::memcpy(g_dir, dir.constData(), size_t(dir.size()) + 1);
    // the first backtrace loads libgcc_s, which allocates: done now, not in the handler
    ::backtrace(g_frames, 4);
    stack_t ss{};
    ss.ss_sp = g_altStack;
    ss.ss_size = sizeof g_altStack;
    ::sigaltstack(&ss, nullptr);
    struct sigaction sa {};
    sa.sa_sigaction = onFatal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    for (int s : kFatal)
        ::sigaction(s, &sa, nullptr);
    struct sigaction tr {};
    tr.sa_sigaction = onTraceRequest;
    sigemptyset(&tr.sa_mask);
    tr.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
    ::sigaction(SIGUSR2, &tr, nullptr);
    g_prevHandler = qInstallMessageHandler(onQtMessage);
}

bool isInstalled() { return g_installed; }

void setSnapshot(const QByteArray &text)
{
    const int idx = 1 - g_snapIdx.load();
    int n = int(qMin<qsizetype>(text.size(), kSnapSize - 1));
    while (n > 0 && n < text.size() && (uchar(text.at(n)) & 0xC0) == 0x80)
        --n;
    n = qBound(0, n, kSnapSize - 1);
    std::memcpy(g_snap[idx], text.constData(), size_t(n));
    if (n > 0 && g_snap[idx][n - 1] != '\n' && n < kSnapSize - 1)
        g_snap[idx][n++] = '\n';
    g_snapLen[idx] = n;
    g_snapIdx.store(idx);
}

static QString g_lastGroup;

static void writeNote(const QString &what, bool replaceLast)
{
    QByteArray line = QStringLiteral("+%1 s  %2").arg(secondsSinceStart()).arg(what.simplified()).toUtf8();
    if (line.size() > kNoteLen - 2) {
        int cut = kNoteLen - 6;
        while (cut > 0 && (uchar(line.at(cut)) & 0xC0) == 0x80)
            --cut;
        line = line.left(cut) + " ...";
    }
    const unsigned count = g_noteCount.load();
    const bool replace = replaceLast && count > 0;
    const unsigned slot = (replace ? count - 1 : count) % unsigned(kNotes);
    std::memcpy(g_notes[slot], line.constData(), size_t(line.size()));
    g_notes[slot][line.size()] = '\n';
    g_notes[slot][line.size() + 1] = 0;
    if (!replace)
        g_noteCount.fetch_add(1);
}

void note(const QString &what)
{
    g_lastGroup.clear();
    writeNote(what, false);
}

void noteInGroup(const QString &group, const QString &what)
{
    const bool same = !group.isEmpty() && group == g_lastGroup;
    g_lastGroup = group;
    writeNote(what, same);
}

QString recentActions()
{
    QString out;
    const unsigned count = g_noteCount.load();
    const unsigned n = std::min<unsigned>(count, unsigned(kNotes));
    for (unsigned i = count - n; i < count; ++i)
        out += QString::fromUtf8(g_notes[i % unsigned(kNotes)]);
    return out;
}

QString buildId()
{
    QString id;
    ::dl_iterate_phdr(buildIdCallback, &id);
    return id;
}

qint64 secondsSinceStart() { return g_start ? qint64(::time(nullptr) - g_start) : 0; }

QStringList logLines()
{
    QMutexLocker lock(&g_logMutex);
    return g_log;
}

quint64 logCount()
{
    QMutexLocker lock(&g_logMutex);
    return g_logCount;
}

QList<PreviousEnd> beginSession()
{
    QList<PreviousEnd> out;
    QDir().mkpath(sessionDir());
    const QString boot = bootId();
    const QList<Report> all = reports();
    const QFileInfoList markers = QDir(sessionDir()).entryInfoList({QStringLiteral("*.json")}, QDir::Files);
    for (const QFileInfo &fi : markers) {
        QFile f(fi.absoluteFilePath());
        QJsonObject o;
        if (f.open(QIODevice::ReadOnly))
            o = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        const qint64 pid = o.value(QStringLiteral("pid")).toVariant().toLongLong();
        // another program window that is still running (same boot, same program)
        if (o.value(QStringLiteral("boot")).toString() == boot && isRunningInstance(pid))
            continue;
        PreviousEnd e;
        e.pid = pid;
        e.started = QDateTime::fromSecsSinceEpoch(o.value(QStringLiteral("started")).toVariant().toLongLong());
        e.version = o.value(QStringLiteral("version")).toString();
        e.project = o.value(QStringLiteral("project")).toString();
        for (const Report &r : all)
            if (r.pid == pid && !r.recovered && (!e.started.isValid() || r.when >= e.started.addSecs(-5))) {
                e.report = r.file;
                e.crashed = r.when;
                break;
            }
        if (pid > 0)
            out << e;
        QFile::remove(fi.absoluteFilePath());
    }
    std::sort(out.begin(), out.end(), [](const PreviousEnd &a, const PreviousEnd &b) { return a.started > b.started; });
    g_session = true;
    g_sessionStart = QDateTime::currentDateTime();
    writeMarker();
    return out;
}

void setSessionProject(const QString &path)
{
    g_sessionProject = path;
    if (g_session)
        writeMarker();
}

void endSession()
{
    if (!g_session)
        return;
    g_session = false;
    QFile::remove(markerPath(QCoreApplication::applicationPid()));
}

void onTermination(std::function<void()> save)
{
    g_onTerm = std::move(save);
    if (g_termPipe[0] >= 0)
        return;
    if (::pipe2(g_termPipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        g_termPipe[0] = g_termPipe[1] = -1;
        return;
    }
    auto *n = new QSocketNotifier(g_termPipe[0], QSocketNotifier::Read, qApp);
    QObject::connect(n, &QSocketNotifier::activated, qApp, [n] {
        char c = 0;
        const ssize_t r = ::read(g_termPipe[0], &c, 1);
        (void)r;
        n->setEnabled(false);
        note(QStringLiteral("termination signal %1: saving, then ending").arg(int(c)));
        if (g_onTerm)
            g_onTerm();
        endSession();
        std::fflush(nullptr);
        ::_exit(0);
    });
    struct sigaction sa {};
    sa.sa_handler = onTermSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    for (int s : {SIGTERM, SIGINT, SIGHUP})
        ::sigaction(s, &sa, nullptr);
}

void setFreezeSeconds(int seconds)
{
    g_freezeSeconds.store(qMax(1, seconds));
}

void startWatchdog(int seconds)
{
    g_freezeSeconds.store(qMax(1, seconds));
    if (g_watchdog || !g_installed)
        return;
    g_watchdog = true;
    g_beatMs.store(monoMs());
    auto *beat = new QTimer(qApp);
    beat->setInterval(500);
    QObject::connect(beat, &QTimer::timeout, [] { g_beatMs.store(monoMs()); });
    beat->start();
    std::thread(watchdogLoop).detach();
}

Report readHeader(const QString &file)
{
    Report r;
    r.file = file;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return r;
    const QString head = QString::fromUtf8(f.read(4096));
    if (head.startsWith(QLatin1String("Quillbinder freeze report"))) {
        r.kind = QStringLiteral("freeze");
        r.signal = QStringLiteral("freeze");
        r.meaning = QStringLiteral("the program did not respond");
        f.seek(0);
        r.recovered = f.readAll().contains("\n== Responded again after ");
    }
    for (const QString &line : head.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("== ")))
            break;
        const int colon = line.indexOf(QLatin1String(": "));
        if (colon < 0)
            continue;
        const QString key = line.left(colon), value = line.mid(colon + 2).trimmed();
        if (key == QLatin1String("Version"))
            r.version = value;
        else if (key == QLatin1String("Unix time"))
            r.when = QDateTime::fromSecsSinceEpoch(value.toLongLong());
        else if (key == QLatin1String("Process"))
            r.pid = value.toLongLong();
        else if (key == QLatin1String("Signal")) {
            static const QRegularExpression re(QStringLiteral("^\\d+ (\\S+) \\((.*)\\)$"));
            const auto m = re.match(value);
            r.signal = m.hasMatch() ? m.captured(1) : value;
            r.meaning = m.hasMatch() ? m.captured(2) : QString();
        }
    }
    if (!r.when.isValid()) {
        // from the name: crash-<unix time>-<pid>.txt (or freeze-...)
        const QStringList parts = QFileInfo(file).completeBaseName().split(QLatin1Char('-'));
        if (parts.size() == 3) {
            r.when = QDateTime::fromSecsSinceEpoch(parts.at(1).toLongLong());
            r.pid = parts.at(2).toLongLong();
        }
    }
    return r;
}

QList<Report> reports()
{
    QList<Report> out;
    for (const QFileInfo &fi : QDir(reportDir()).entryInfoList({QStringLiteral("crash-*.txt"), QStringLiteral("freeze-*.txt")}, QDir::Files))
        out << readHeader(fi.absoluteFilePath());
    std::sort(out.begin(), out.end(), [](const Report &a, const Report &b) { return a.when > b.when; });
    return out;
}

QString readable(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("\\b_Z[A-Za-z0-9_.$]+"));
    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        int status = -1;
        const QByteArray mangled = m.captured().toLatin1();
        char *d = abi::__cxa_demangle(mangled.constData(), nullptr, nullptr, &status);
        out += (status == 0 && d) ? QString::fromLatin1(d) : m.captured();
        std::free(d);
        last = m.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

void prune(int keep)
{
    const QList<Report> all = reports();
    for (int i = keep; i < all.size(); ++i)
        QFile::remove(all.at(i).file);
}

} // namespace crash
} // namespace qb
